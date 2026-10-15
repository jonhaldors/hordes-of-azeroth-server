/*
 * Vanilla Bonk: keywords (docs/Spells.md, Keywords). Shared debuffs every
 * class feeds and pays off: one per keyword per enemy, carrying a stack
 * count. The hero is always the caster, so all of a hero's spells add to the
 * same stack. The debuff's spell (bonk_keyword) gives its icon, hover text
 * and look on the enemy; every number is set here.
 *
 * Also the crowd control rules: Frozen and stuns hold a run enemy for at
 * most BONK_MAX_HOLD_MS, then it is immune to that for BONK_HOLD_IMMUNE_MS,
 * and an enemy is knocked back once in that while. Elites and bosses are
 * never held in place or knocked back (elites can be stunned).
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkRunStats.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"

#include <algorithm>

namespace
{
    uint32 const BONK_MAX_HOLD_MS            = 2000;    // Frozen and stuns
    uint32 const BONK_HOLD_IMMUNE_MS         = 6000;    // after a hold or a knockback, before the next
    float const BONK_KEYWORD_SPREAD_RANGE    = 8.0f;    // damage over time spreads to enemies this close to its dead carrier
    uint32 const BONK_KEYWORD_SPREAD_TARGETS = 2;
    // Damage over time stacks have no cap but what the 1.12 client can
    // show on a debuff; they are looked over this often.
    uint32 const BONK_MAX_DOT_STACKS         = 255;
    uint32 const BONK_DOT_CHECK_MS           = 200;
    // Hunter's Mark: the most damage it adds, and how far a dying Marked
    // enemy passes it on (to the BONK_KEYWORD_SPREAD_TARGETS nearest).
    float const BONK_MAX_HUNTERS_MARK        = 30.0f;
    float const BONK_MARK_SPREAD_RANGE       = 10.0f;

    BonkMobAI const* GetRunAI(Unit const* unit)
    {
        Creature const* creature = unit ? unit->ToCreature() : nullptr;
        return creature ? dynamic_cast<BonkMobAI const*>(creature->AI()) : nullptr;
    }

    bool TimeLeft(uint32 until, uint32 now)
    {
        return int32(until - now) > 0;
    }
}

void BonkRun::OnWeaponHit(Unit* victim, SpellEntry const* spell)
{
    if (m_state != STATE_RUNNING || !spell || !GetRunAI(victim))
        return;

    OwnedWeapon const* owned = FindOwnedBySpell(spell->Id);
    PendingKeyword pending;
    if (!owned || !GetWeaponKeyword(*owned, pending))
        return;

    // Not from inside the damage: applied at the next update.
    pending.mob = victim->GetObjectGuid();
    m_pendingKeywords.push_back(pending);
}

bool BonkRun::GetWeaponKeyword(OwnedWeapon const& owned, PendingKeyword& keyword) const
{
    // A pet's bite: its family's keyword (BonkOnDamage counts the bite for
    // the spell that called it).
    BonkWeapon const& weapon = owned.weapon;
    keyword.keyword = weapon.applies;
    uint32 stacks = weapon.applyStacks;
    if (weapon.behavior == BONK_BEHAVIOR_PET && keyword.keyword == BONK_KEYWORD_NONE)
        if (BonkPetFamily const* family = GetPetFamily())
        {
            keyword.keyword = family->applies;
            stacks = family->applyStacks;
        }
    BonkKeywordInfo const* info = sBonkMgr.GetKeyword(keyword.keyword);
    if (!info || !stacks)
        return false;

    // Its ranks: more stacks a hit, longer lasting, and harder hitting
    // stacks (damage over time locks its damage in when applied).
    stacks += uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_STACKS));
    keyword.stacks = uint8(std::min<uint32>(stacks, BONK_MAX_DOT_STACKS));
    keyword.durationMs = uint32(info->durationMs * GetWeaponDurationScale(owned));
    keyword.damageScale = 1.0f + (owned.damageBonus + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_PET_DAMAGE)) / 100.0f;
    return true;
}

void BonkRun::OnKillingBlow(Unit* victim)
{
    if (m_state != STATE_RUNNING || !GetRunAI(victim))
        return;

    // Damage over time spreads; Hunter's Mark moves on with the time it had.
    std::map<uint8, DyingKeyword> carried;
    for (auto const& itr : sBonkMgr.GetKeywords())
    {
        BonkKeywordInfo const& info = itr.second;
        if (info.id == BONK_KEYWORD_HUNTERS_MARK)
        {
            SpellAuraHolder const* holder = victim->GetSpellAuraHolder(info.spellId, m_player->GetObjectGuid());
            if (holder && holder->GetAuraDuration() > 0)
                carried[itr.first].leftMs = uint32(holder->GetAuraDuration());
            continue;
        }
        if (!info.spreadsOnDeath)
            continue;
        auto mob = m_dotStacks.find(victim->GetObjectGuid());
        if (mob == m_dotStacks.end())
            continue;
        auto stacks = mob->second.find(itr.first);
        if (stacks == mob->second.end() || stacks->second.empty())
            continue;
        int64 sum = 0;
        for (DotStack const& stack : stacks->second)
            sum += stack.points;
        DyingKeyword& dying = carried[itr.first];
        dying.stacks = uint32(stacks->second.size());
        dying.points = int32(sum / int64(dying.stacks));
    }
    if (!carried.empty())
        m_dyingKeywords[victim->GetObjectGuid()] = std::move(carried);
}

void BonkRun::UpdateKeywords(uint32 diff)
{
    if (!m_pendingKeywords.empty())
    {
        std::vector<PendingKeyword> pending;
        pending.swap(m_pendingKeywords);
        Map* map = m_player->GetMap();
        for (PendingKeyword const& keyword : pending)
            if (Creature* mob = map->GetCreature(keyword.mob))
                ApplyKeyword(mob, keyword.keyword, keyword.stacks, keyword.durationMs, keyword.damageScale);
    }
    UpdateDotStacks(diff);
}

bool BonkRun::ApplyKeyword(Creature* mob, BonkKeyword keyword, uint32 stacks, uint32 durationMs, float damageScale)
{
    BonkKeywordInfo const* info = sBonkMgr.GetKeyword(keyword);
    if (!info || !stacks || !mob || !mob->IsAlive() || !GetRunAI(mob) || !m_player->IsInWorld())
        return false;

    if (keyword == BONK_KEYWORD_FROZEN)
        return Freeze(mob);

    uint32 const length = durationMs ? durationMs : info->durationMs;

    // Damage over time: stacks of their own, each with the damage it was
    // applied with.
    if (info->IsDamageOverTime())
    {
        AddDotStacks(mob, *info, stacks, length, std::max(1, int32(GetKeywordPoints(*info) * damageScale + 0.5f)));
        return true;
    }

    uint32 const total = std::min<uint32>(GetKeywordStacks(mob, keyword) + stacks, info->maxStacks);

    // Chill: at its most stacks the enemy freezes and Chill starts over. One
    // still immune to freezing keeps the most Chill until it can freeze.
    if (keyword == BONK_KEYWORD_CHILL && total >= info->maxStacks && Freeze(mob))
    {
        mob->RemoveAurasByCasterSpell(info->spellId, m_player->GetObjectGuid());
        return true;
    }

    return AddKeywordAura(mob, *info, total, length) != nullptr;
}

void BonkRun::AddDotStacks(Creature* mob, BonkKeywordInfo const& info, uint32 stacks, uint32 durationMs, int32 points)
{
    std::vector<DotStack>& list = m_dotStacks[mob->GetObjectGuid()][info.id];
    DotStack stack;
    stack.untilMs = WorldTimer::getMSTime() + std::max(1u, durationMs);
    stack.points = points;
    list.insert(list.end(), stacks, stack);

    // Past what the client can show, the oldest go first.
    if (list.size() > BONK_MAX_DOT_STACKS)
        list.erase(list.begin(), list.begin() + (list.size() - BONK_MAX_DOT_STACKS));
    SyncDotAura(mob, info, list);
}

void BonkRun::UpdateDotStacks(uint32 diff)
{
    m_dotTimer += diff;
    if (m_dotTimer < BONK_DOT_CHECK_MS || m_dotStacks.empty())
        return;
    m_dotTimer = 0;

    uint32 const now = WorldTimer::getMSTime();
    Map* map = m_player->GetMap();
    for (auto mob = m_dotStacks.begin(); mob != m_dotStacks.end();)
    {
        Creature* creature = map->GetCreature(mob->first);
        if (!creature || !creature->IsAlive())
        {
            mob = m_dotStacks.erase(mob);
            continue;
        }

        for (auto keyword = mob->second.begin(); keyword != mob->second.end();)
        {
            std::vector<DotStack>& stacks = keyword->second;
            size_t const before = stacks.size();
            stacks.erase(std::remove_if(stacks.begin(), stacks.end(), [now](DotStack const& stack) { return !TimeLeft(stack.untilMs, now); }), stacks.end());

            // Gone from the enemy some other way (the debuff limit took it):
            // its stacks are forgotten.
            BonkKeywordInfo const* info = sBonkMgr.GetKeyword(BonkKeyword(keyword->first));
            bool const worn = info && creature->GetSpellAuraHolder(info->spellId, m_player->GetObjectGuid());
            if (!info || !worn || stacks.empty())
            {
                if (worn)
                    creature->RemoveAurasByCasterSpell(info->spellId, m_player->GetObjectGuid());
                keyword = mob->second.erase(keyword);
                continue;
            }
            if (stacks.size() != before)
                SyncDotAura(creature, *info, stacks);
            ++keyword;
        }
        if (mob->second.empty())
            mob = m_dotStacks.erase(mob);
        else
            ++mob;
    }
}

void BonkRun::SyncDotAura(Creature* mob, BonkKeywordInfo const& info, std::vector<DotStack> const& stacks)
{
    // The debuff carries their count and deals their sum: its per-stack
    // damage is their average. It lasts as long as the last of them.
    uint32 const now = WorldTimer::getMSTime();
    uint32 const count = uint32(stacks.size());
    int64 sum = 0;
    int32 leftMs = 1;
    for (DotStack const& stack : stacks)
    {
        sum += stack.points;
        leftMs = std::max(leftMs, int32(stack.untilMs - now));
    }
    int32 const average = std::max<int32>(1, int32((sum + count / 2) / count));

    SpellAuraHolder* holder = mob->GetSpellAuraHolder(info.spellId, m_player->GetObjectGuid());
    if (!holder)
    {
        AddKeywordAura(mob, info, count, uint32(leftMs), average);
        return;
    }

    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (Aura* aura = holder->GetAuraByEffectIndex(SpellEffectIndex(i)))
            aura->SetBasePoints(average);
    if (holder->GetStackAmount() != count)
        holder->SetStackAmount(count);      // works its damage out again from the new average
    else
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (Aura* aura = holder->GetAuraByEffectIndex(SpellEffectIndex(i)))
            {
                int32 points = aura->GetBasePoints();
                float const amount = count * m_player->CalculateSpellEffectValue(mob, holder->GetSpellProto(), SpellEffectIndex(i), &points);
                aura->ApplyModifier(false, true);
                aura->GetModifier()->m_amount = amount;
                aura->ApplyModifier(true, true);
            }
    holder->SetAuraMaxDuration(leftMs);
    holder->RefreshHolder();
}

uint32 BonkRun::GetKeywordStacks(Unit const* victim, BonkKeyword keyword) const
{
    if (!victim)
        return 0;
    if (keyword == BONK_KEYWORD_FROZEN)
        return IsFrozen(victim) ? 1 : 0;

    BonkKeywordInfo const* info = sBonkMgr.GetKeyword(keyword);
    if (!info)
        return 0;
    SpellAuraHolder const* holder = victim->GetSpellAuraHolder(info->spellId, m_player->GetObjectGuid());
    return holder ? holder->GetStackAmount() : 0;
}

uint32 BonkRun::CountKeywords(Unit const* victim) const
{
    uint32 count = 0;
    for (auto const& itr : sBonkMgr.GetKeywords())
        if (GetKeywordStacks(victim, itr.second.id))
            ++count;
    return count;
}

bool BonkRun::Freeze(Creature* mob)
{
    BonkMobAI const* ai = GetRunAI(mob);
    BonkKeywordInfo const* info = sBonkMgr.GetKeyword(BONK_KEYWORD_FROZEN);
    if (!ai || !info || !mob->IsAlive())
        return false;

    uint32 const now = WorldTimer::getMSTime();
    CrowdControl& control = m_crowdControl[mob->GetObjectGuid()];
    if (TimeLeft(control.frozenImmuneUntil, now))
        return false;

    uint32 const length = std::min(info->durationMs ? info->durationMs : BONK_MAX_HOLD_MS, BONK_MAX_HOLD_MS);
    control.shatterUntil = now + length;
    control.frozenImmuneUntil = now + length + BONK_HOLD_IMMUNE_MS;

    // Elites and bosses are never held in place, but count as Frozen.
    if (ai->GetKind() == BONK_SPAWN_NORMAL)
        AddKeywordAura(mob, *info, 1, length);
    return true;
}

bool BonkRun::IsFrozen(Unit const* victim) const
{
    auto itr = victim ? m_crowdControl.find(victim->GetObjectGuid()) : m_crowdControl.end();
    return itr != m_crowdControl.end() && TimeLeft(itr->second.shatterUntil, WorldTimer::getMSTime());
}

bool BonkRun::LimitStun(Creature* mob, int32& duration)
{
    if (!GetRunAI(mob))
        return true;

    uint32 const now = WorldTimer::getMSTime();
    CrowdControl& control = m_crowdControl[mob->GetObjectGuid()];
    if (TimeLeft(control.stunImmuneUntil, now))
        return false;

    if (duration <= 0 || duration > int32(BONK_MAX_HOLD_MS))
        duration = int32(BONK_MAX_HOLD_MS);
    control.stunImmuneUntil = now + uint32(duration) + BONK_HOLD_IMMUNE_MS;
    return true;
}

bool BonkRun::LimitRoot(Creature* mob, int32& duration)
{
    // A root from a hero's spell (Frost Nova, Frostbite, Entangling Roots)
    // follows Frozen's rule, as Frozen is a root too: trash is held at most
    // BONK_MAX_HOLD_MS, then neither roots nor Frozen take for
    // BONK_HOLD_IMMUNE_MS; elites and bosses are never rooted. Repeated
    // roots kept enemies held for good (stress test, Absolute Zero).
    BonkMobAI const* ai = GetRunAI(mob);
    if (!ai)
        return true;
    if (ai->GetKind() != BONK_SPAWN_NORMAL)
        return false;

    uint32 const now = WorldTimer::getMSTime();
    CrowdControl& control = m_crowdControl[mob->GetObjectGuid()];
    if (TimeLeft(control.frozenImmuneUntil, now))
        return false;

    if (duration <= 0 || duration > int32(BONK_MAX_HOLD_MS))
        duration = int32(BONK_MAX_HOLD_MS);
    control.frozenImmuneUntil = now + uint32(duration) + BONK_HOLD_IMMUNE_MS;
    return true;
}

bool BonkRun::CanKnockBack(Creature const* mob)
{
    BonkMobAI const* ai = GetRunAI(mob);
    if (!ai || ai->GetKind() != BONK_SPAWN_NORMAL)
        return false;

    uint32 const now = WorldTimer::getMSTime();
    CrowdControl& control = m_crowdControl[mob->GetObjectGuid()];
    if (TimeLeft(control.knockImmuneUntil, now))
        return false;
    control.knockImmuneUntil = now + BONK_HOLD_IMMUNE_MS;
    return true;
}

float BonkRun::GetKeywordDamageMultiplier(SpellEntry const* spell, Unit const* victim) const
{
    if (!GetRunAI(victim))
        return 1.0f;

    // Hunter's Mark: more damage from everything.
    float multiplier = 1.0f;
    if (GetKeywordStacks(victim, BONK_KEYWORD_HUNTERS_MARK))
        multiplier *= 1.0f + GetHuntersMarkBonus() / 100.0f;

    // The spell's payoff: more damage for each stack of its keyword on the
    // enemy, or for each different keyword on it. Never for each stack of
    // damage over time, which has no cap: a hundred stacks would make it a
    // multiplier past balancing (docs/Spells.md, Keywords).
    if (spell)
        if (OwnedWeapon const* owned = FindOwnedBySpell(spell->Id))
            if (owned->weapon.uses != BONK_KEYWORD_NONE && owned->weapon.usesPct > 0.0f)
            {
                uint32 count = 0;
                if (owned->weapon.uses == BONK_KEYWORD_VARIETY)
                    count = CountKeywords(victim);
                else if (BonkKeywordInfo const* used = sBonkMgr.GetKeyword(owned->weapon.uses))
                    if (!used->IsDamageOverTime())
                        count = GetKeywordStacks(victim, owned->weapon.uses);
                multiplier *= 1.0f + owned->weapon.usesPct * count / 100.0f;
            }
    return multiplier;
}

float BonkRun::GetHuntersMarkBonus() const
{
    BonkKeywordInfo const* mark = sBonkMgr.GetKeyword(BONK_KEYWORD_HUNTERS_MARK);
    if (!mark)
        return 0.0f;

    // The hero's spell that marks: it deals no damage, so its damage ranks
    // grow the Mark instead, and its Mark strength upgrades add to it.
    float bonus = mark->valuePerStack;
    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.applies == BONK_KEYWORD_HUNTERS_MARK)
            bonus = std::max(bonus, mark->valuePerStack * (1.0f + owned.damageBonus / 100.0f) + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_MARK));
    return std::min(bonus, BONK_MAX_HUNTERS_MARK);
}

SpellAuraHolder* BonkRun::AddKeywordAura(Creature* mob, BonkKeywordInfo const& info, uint32 stacks, uint32 durationMs, int32 points)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(info.spellId);
    if (!spellInfo)
        return nullptr;

    // Damage over time ticks every tickMs, faster with Haste.
    BonkPlayer::SpellTweak& tweak = m_player->GetBonkPlayer()->TweakSpell(info.spellId);
    tweak.tickMs = info.tickMs;
    tweak.tickScale = 1.0f / GetHasteRate();
    tweak.durationMs = 0;
    tweak.durationScale = 1.0f;

    // More stacks scale its effects; the same or more start its time over.
    if (SpellAuraHolder* holder = mob->GetSpellAuraHolder(info.spellId, m_player->GetObjectGuid()))
    {
        holder->SetStackAmount(stacks);
        return holder;
    }

    // A stack's worth; the core multiplies it by the stacks. Always a
    // debuff, whatever its spell's targets would make the core think.
    if (!points)
        points = GetKeywordPoints(info);
    SpellAuraHolder* holder = CreateSpellAuraHolder(spellInfo, mob, m_player, m_player);
    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        if (spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA)
        {
            Aura* aura = CreateAura(spellInfo, SpellEffectIndex(i), &points, holder, mob, m_player);
            aura->SetPositive(false);
            holder->AddAura(aura, SpellEffectIndex(i));
        }
    if (holder->IsEmptyHolder())
    {
        delete holder;
        return nullptr;
    }

    // Its own spell's duration doesn't count (some last forever).
    if (durationMs)
    {
        holder->SetPermanent(false);
        holder->SetAuraMaxDuration(int32(durationMs));
        holder->SetAuraDuration(int32(durationMs));
    }
    if (!mob->AddSpellAuraHolder(holder))
        return nullptr;
    if (stacks > 1)
        holder->SetStackAmount(stacks);
    return holder;
}

int32 BonkRun::GetKeywordPoints(BonkKeywordInfo const& info) const
{
    switch (info.id)
    {
        // Slower, by % a stack.
        case BONK_KEYWORD_CHILL:
        case BONK_KEYWORD_DAZED:
            return -int32(info.valuePerStack + 0.5f);
        // More damage taken, by % a stack.
        case BONK_KEYWORD_FIRE_VULNERABILITY:
        case BONK_KEYWORD_SUNDER_ARMOR:
        case BONK_KEYWORD_SHADOW_VULNERABILITY:
            return int32(info.valuePerStack + 0.5f);
        // A stack's damage each tick.
        case BONK_KEYWORD_BURNING:
        case BONK_KEYWORD_BLEED:
        case BONK_KEYWORD_POISON:
            return std::max(1, int32(GetMobHealth(GetMobLevel()) * info.valuePerStack * info.tickMs / IN_MILLISECONDS));
        // Frozen is a hold; Hunter's Mark is read by the damage formula.
        default:
            return 0;
    }
}

void BonkRun::SpreadKeywords(Creature const* dead)
{
    m_crowdControl.erase(dead->GetObjectGuid());

    m_dotStacks.erase(dead->GetObjectGuid());

    auto itr = m_dyingKeywords.find(dead->GetObjectGuid());
    if (itr == m_dyingKeywords.end())
        return;
    std::map<uint8, DyingKeyword> const carried = std::move(itr->second);
    m_dyingKeywords.erase(itr);

    for (auto const& keyword : carried)
    {
        BonkKeywordInfo const* info = sBonkMgr.GetKeyword(BonkKeyword(keyword.first));
        if (!info)
            continue;

        // Hunter's Mark moves on to the nearest enemies without it, for the
        // time it had left.
        if (info->id == BONK_KEYWORD_HUNTERS_MARK)
        {
            uint32 passed = 0;
            for (Creature* next : GetMobsInRange(BONK_MARK_SPREAD_RANGE, dead))
            {
                if (passed >= BONK_KEYWORD_SPREAD_TARGETS)
                    break;
                if (!GetKeywordStacks(next, BONK_KEYWORD_HUNTERS_MARK) && ApplyKeyword(next, info->id, 1, keyword.second.leftMs))
                    ++passed;
            }
            continue;
        }

        // Half its stacks, rounded down, go to the nearest enemies, so a
        // chain fades out (10, 5, 2, 1), each with a fresh timer and the
        // damage the dead one's stacks had.
        uint32 const half = keyword.second.stacks / 2;
        if (!half)
            continue;
        uint32 spread = 0;
        for (Creature* next : GetMobsInRange(BONK_KEYWORD_SPREAD_RANGE, dead))
        {
            if (spread >= BONK_KEYWORD_SPREAD_TARGETS)
                break;
            if (!next->IsAlive() || !GetRunAI(next))
                continue;
            AddDotStacks(next, *info, half, info->durationMs, keyword.second.points);
            ++spread;
        }
    }
}

void BonkRun::RebuildWeaponSpells()
{
    m_weaponSpells.clear();
    for (OwnedWeapon const& owned : m_weapons)
        for (uint32 spellId : BonkStats::GetWeaponSpells(owned.weapon))
            m_weaponSpells.emplace(spellId, owned.weapon.id);
}

BonkRun::OwnedWeapon const* BonkRun::FindOwnedBySpell(uint32 spellId) const
{
    auto itr = m_weaponSpells.find(spellId);
    return itr != m_weaponSpells.end() ? FindOwned(itr->second) : nullptr;
}
