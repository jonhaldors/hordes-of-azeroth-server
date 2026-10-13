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
    if (!owned || owned->weapon.applies == BONK_KEYWORD_NONE || !owned->weapon.applyStacks)
        return;

    // Not from inside the damage: applied at the next update.
    PendingKeyword pending;
    pending.mob = victim->GetObjectGuid();
    pending.keyword = owned->weapon.applies;
    pending.stacks = owned->weapon.applyStacks;
    m_pendingKeywords.push_back(pending);
}

void BonkRun::OnKillingBlow(Unit* victim)
{
    if (m_state != STATE_RUNNING || !GetRunAI(victim))
        return;

    std::map<uint8, uint32> stacks;
    for (auto const& itr : sBonkMgr.GetKeywords())
        if (itr.second.spreadsOnDeath)
            if (uint32 const count = GetKeywordStacks(victim, itr.second.id))
                stacks[itr.first] = count;
    if (!stacks.empty())
        m_dyingKeywords[victim->GetObjectGuid()] = std::move(stacks);
}

void BonkRun::UpdateKeywords()
{
    if (m_pendingKeywords.empty())
        return;

    std::vector<PendingKeyword> pending;
    pending.swap(m_pendingKeywords);
    Map* map = m_player->GetMap();
    for (PendingKeyword const& keyword : pending)
        if (Creature* mob = map->GetCreature(keyword.mob))
            ApplyKeyword(mob, keyword.keyword, keyword.stacks);
}

bool BonkRun::ApplyKeyword(Creature* mob, BonkKeyword keyword, uint32 stacks)
{
    BonkKeywordInfo const* info = sBonkMgr.GetKeyword(keyword);
    if (!info || !stacks || !mob || !mob->IsAlive() || !GetRunAI(mob) || !m_player->IsInWorld())
        return false;

    if (keyword == BONK_KEYWORD_FROZEN)
        return Freeze(mob);

    uint32 const total = std::min<uint32>(GetKeywordStacks(mob, keyword) + stacks, info->maxStacks);

    // Chill: at its most stacks the enemy freezes and Chill starts over. One
    // still immune to freezing keeps the most Chill until it can freeze.
    if (keyword == BONK_KEYWORD_CHILL && total >= info->maxStacks && Freeze(mob))
    {
        mob->RemoveAurasByCasterSpell(info->spellId, m_player->GetObjectGuid());
        return true;
    }

    return AddKeywordAura(mob, *info, total, info->durationMs) != nullptr;
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
    if (BonkKeywordInfo const* mark = sBonkMgr.GetKeyword(BONK_KEYWORD_HUNTERS_MARK))
        if (GetKeywordStacks(victim, BONK_KEYWORD_HUNTERS_MARK))
            multiplier *= 1.0f + mark->valuePerStack / 100.0f;

    // The spell's payoff: more damage for each stack of its keyword on the
    // enemy, or for each different keyword on it.
    if (spell)
        if (OwnedWeapon const* owned = FindOwnedBySpell(spell->Id))
            if (owned->weapon.uses != BONK_KEYWORD_NONE && owned->weapon.usesPct > 0.0f)
            {
                uint32 const count = owned->weapon.uses == BONK_KEYWORD_VARIETY ? CountKeywords(victim) : GetKeywordStacks(victim, owned->weapon.uses);
                multiplier *= 1.0f + owned->weapon.usesPct * count / 100.0f;
            }
    return multiplier;
}

SpellAuraHolder* BonkRun::AddKeywordAura(Creature* mob, BonkKeywordInfo const& info, uint32 stacks, uint32 durationMs)
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
    int32 points = GetKeywordPoints(info);
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

    auto itr = m_dyingKeywords.find(dead->GetObjectGuid());
    if (itr == m_dyingKeywords.end())
        return;
    std::map<uint8, uint32> const stacks = std::move(itr->second);
    m_dyingKeywords.erase(itr);

    // Half its stacks, rounded down, go to the nearest enemies, so a chain
    // fades out (10, 5, 2, 1).
    std::vector<Creature*> const nearby = GetMobsInRange(BONK_KEYWORD_SPREAD_RANGE, dead);
    for (auto const& keyword : stacks)
    {
        uint32 const half = keyword.second / 2;
        if (!half)
            continue;
        uint32 spread = 0;
        for (Creature* next : nearby)
        {
            if (spread >= BONK_KEYWORD_SPREAD_TARGETS)
                break;
            if (ApplyKeyword(next, BonkKeyword(keyword.first), half))
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
