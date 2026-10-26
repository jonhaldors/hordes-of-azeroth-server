#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"

#include <algorithm>

namespace
{
    float const BONK_BESTIAL_WRATH_FRENZY       = 1.5f;
    float const BONK_BESTIAL_WRATH_OWN_DAMAGE   = 0.7f;
    float const BONK_TRUESHOT_MARKED            = 1.4f;
    float const BONK_TRUESHOT_UNMARKED          = 0.75f;
    float const BONK_SHATTERLORD_UNFROZEN       = 0.85f;
    uint32 const BONK_SHATTERLORD_FREEZE_STACKS = 6;
    float const BONK_BERSERKER_DAMAGE_TAKEN     = 1.15f;
    float const BONK_SPELL_SHIELD_CAP           = 0.5f;
    float const BONK_JUGGERNAUT_SHIELD_CAP      = 1.0f;
    float const BONK_JUGGERNAUT_REGEN_PER_SEC   = 0.1f;
    uint32 const BONK_WEAPON_MASTER_EVERY       = 5;
    float const BONK_ARCANE_SURGE_RECHARGE      = 2.0f;
    float const BONK_ARCANE_SURGE_HEALTH_COST   = 3.0f;
    float const BONK_LOW_HEALTH_GUARD           = 20.0f;
    float const BONK_ICE_BLOCK_RADIUS           = 10.0f;
    uint32 const BONK_ICE_BLOCK_CHILL           = 10;
    uint32 const BONK_KIT_ICE_BLOCK             = 160;
    uint32 const BONK_SPELL_TALENT_STUN         = 7922;

    BonkMobAI const* GetRunAI(Unit const* unit)
    {
        Creature const* creature = unit ? unit->ToCreature() : nullptr;
        return creature ? dynamic_cast<BonkMobAI const*>(creature->AI()) : nullptr;
    }
}

void BonkRun::ApplyMasteryTo(BonkWeapon& weapon) const
{
    uint32 const root = BonkMastery::GetRootWeapon(weapon.id);

    auto applies = m_mastery.spellApplies.find(root);
    if (applies != m_mastery.spellApplies.end() && weapon.applies == BONK_KEYWORD_NONE)
    {
        weapon.applies = applies->second.keyword;
        weapon.applyStacks = applies->second.stacks;
    }

    auto uses = m_mastery.usesBonus.find(root);
    if (uses != m_mastery.usesBonus.end() && weapon.uses != BONK_KEYWORD_NONE)
        weapon.usesPct += uses->second;

    auto heal = m_mastery.spellHeal.find(root);
    if (heal != m_mastery.spellHeal.end())
        weapon.healPct += heal->second;

    if (m_mastery.resetOnKill.count(root))
        weapon.flags |= BONK_WEAPON_FLAG_RESET_ON_KILL;

    auto execute = m_mastery.executeThreshold.find(root);
    if (execute != m_mastery.executeThreshold.end())
    {
        if (weapon.killBelowPct > 0.0f)
            weapon.killBelowPct = std::max(weapon.killBelowPct, execute->second);
        if (weapon.behavior == BONK_BEHAVIOR_EXECUTE)
            weapon.behaviorValue = std::max(weapon.behaviorValue, execute->second);
    }
}

float BonkRun::GetSpecDamage(uint32 weaponId) const
{
    if (m_mastery.specDamage.empty())
        return 0.0f;
    auto itr = m_mastery.specDamage.find(BonkMastery::GetSpec(weaponId));
    return itr != m_mastery.specDamage.end() ? itr->second : 0.0f;
}

float BonkRun::GetTalentDamagePercent() const
{
    float percent = 0.0f;
    if (IsEnraged())
        percent += m_mastery.enrageDamage;
    if (IsHowlFrenzied())
        percent += m_mastery.frenzyDamage;
    return percent;
}

float BonkRun::GetCapstoneDamageMultiplier(bool ownSpell, Unit const* victim) const
{
    float multiplier = 1.0f;
    if (ownSpell && m_mastery.Has(BONK_CAPSTONE_BESTIAL_WRATH))
        multiplier *= BONK_BESTIAL_WRATH_OWN_DAMAGE;
    if (!victim || !GetRunAI(victim))
        return multiplier;
    if (m_mastery.Has(BONK_CAPSTONE_TRUESHOT))
        multiplier *= GetKeywordStacks(victim, BONK_KEYWORD_HUNTERS_MARK) ? BONK_TRUESHOT_MARKED : BONK_TRUESHOT_UNMARKED;
    if (m_mastery.Has(BONK_CAPSTONE_SHATTERLORD) && !IsFrozen(victim))
        multiplier *= BONK_SHATTERLORD_UNFROZEN;
    return multiplier;
}

BonkRun::OwnedWeapon const* BonkRun::FindSummonOwner(Unit const* unit) const
{
    if (!unit || !unit->IsCreature())
        return nullptr;
    ObjectGuid const guid = unit->GetObjectGuid();
    for (OwnedWeapon const& owned : m_weapons)
        if (std::find(owned.pets.begin(), owned.pets.end(), guid) != owned.pets.end())
            return &owned;
    for (Snake const& snake : m_snakes)
        if (snake.guid == guid)
            return FindOwned(snake.weaponId);
    for (Beast const& beast : m_beasts)
        if (beast.guid == guid)
            return FindOwned(beast.weaponId);
    return nullptr;
}

float BonkRun::GetSummonDamageMultiplier(Unit const* attacker) const
{
    OwnedWeapon const* owner = FindSummonOwner(attacker);
    return owner ? 1.0f + GetSpecDamage(owner->weapon.id) / 100.0f : 1.0f;
}

float BonkRun::GetTalentCritChance(Unit const* victim, Unit const* attacker) const
{
    float chance = 0.0f;
    if (victim)
        for (auto const& itr : m_mastery.critVsKeyword)
            if (GetKeywordStacks(victim, BonkKeyword(itr.first)))
                chance += itr.second;
    if (attacker && m_mastery.summonCrit > 0.0f && IsSummon(attacker))
        chance += m_mastery.summonCrit;
    return chance;
}

bool BonkRun::IsWeaponMasterStrike(SpellEntry const* spell) const
{
    if (!spell || !m_mastery.Has(BONK_CAPSTONE_WEAPON_MASTER))
        return false;
    OwnedWeapon const* owned = FindOwnedBySpell(spell->Id);
    return owned && owned->casts && owned->casts % BONK_WEAPON_MASTER_EVERY == 0;
}

float BonkRun::GetKeywordTalentDamage(uint32 keyword) const
{
    auto itr = m_mastery.keywordDamage.find(keyword);
    return itr != m_mastery.keywordDamage.end() ? itr->second : 0.0f;
}

uint32 BonkRun::GetExtraSpreadTargets(uint32 keyword) const
{
    auto itr = m_mastery.spreadTargets.find(keyword);
    return itr != m_mastery.spreadTargets.end() ? itr->second : 0;
}

uint32 BonkRun::GetChillFreezeStacks(BonkKeywordInfo const& chill) const
{
    if (m_mastery.Has(BONK_CAPSTONE_SHATTERLORD))
        return std::min<uint32>(BONK_SHATTERLORD_FREEZE_STACKS, chill.maxStacks);
    return chill.maxStacks;
}

uint32 BonkRun::GetChillTalentMs() const
{
    return uint32(m_mastery.chillDuration * IN_MILLISECONDS);
}

void BonkRun::QueueTalentStun(OwnedWeapon const& owned, Unit* victim)
{
    if (m_mastery.spellStun.count(GetRootWeapon(owned.weapon)))
        m_pendingStuns.push_back(victim->GetObjectGuid());
}

void BonkRun::ApplyPendingStuns()
{
    if (m_pendingStuns.empty())
        return;
    std::vector<ObjectGuid> stuns;
    stuns.swap(m_pendingStuns);
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : stuns)
    {
        Creature* mob = map->GetCreature(guid);
        BonkMobAI const* ai = mob ? GetRunAI(mob) : nullptr;
        if (ai && ai->GetKind() == BONK_SPAWN_NORMAL && mob->IsAlive())
            m_player->CastSpell(mob, BONK_SPELL_TALENT_STUN, true);
    }
}

uint32 BonkRun::GetSpellShieldCap() const
{
    float const cap = m_mastery.Has(BONK_CAPSTONE_JUGGERNAUT) ? BONK_JUGGERNAUT_SHIELD_CAP : BONK_SPELL_SHIELD_CAP;
    return uint32(m_player->GetMaxHealth() * cap);
}

uint32 BonkRun::GetShieldBlockTalent() const
{
    return uint32(m_player->GetMaxHealth() * m_mastery.shieldBlock / 100.0f);
}

void BonkRun::RechargeJuggernautShield(uint32 diff, uint32 max)
{
    if (!m_mastery.Has(BONK_CAPSTONE_JUGGERNAUT) || m_spellShield >= max)
        return;
    m_juggernautCarry += max * BONK_JUGGERNAUT_REGEN_PER_SEC * diff / IN_MILLISECONDS;
    uint32 const gained = uint32(m_juggernautCarry);
    m_juggernautCarry -= gained;
    RefillShield(std::min(max, m_spellShield + gained));
}

uint32 BonkRun::ApplyTalentDamageTaken(uint32 damage) const
{
    if (m_mastery.Has(BONK_CAPSTONE_BERSERKER))
        damage = uint32(damage * BONK_BERSERKER_DAMAGE_TAKEN);
    if (m_bonuses.damageTakenCut > 0.0f)
        damage = uint32(damage * std::max(0.0f, 1.0f - m_bonuses.damageTakenCut / 100.0f));
    return damage;
}

bool BonkRun::IsIceBlocked() const
{
    return m_iceBlockUntilMs > m_totalMs;
}

uint32 BonkRun::GuardLowHealth(uint32 damage)
{
    if (!damage || m_lowHealthGuardStage == m_stage || (m_mastery.lastStand <= 0.0f && m_mastery.iceBlock <= 0.0f))
        return damage;

    uint32 const health = m_player->GetHealth();
    uint32 const floor = uint32(m_player->GetMaxHealth() * BONK_LOW_HEALTH_GUARD / 100.0f);
    if (health > damage && health - damage >= floor)
        return damage;

    m_lowHealthGuardStage = m_stage;
    if (m_mastery.iceBlock > 0.0f)
    {
        m_iceBlockUntilMs = m_totalMs + uint32(m_mastery.iceBlock * IN_MILLISECONDS);
        m_iceBlockBurstPending = true;
        return 0;
    }

    SupportScope scope(*this, TalentSupport(BONK_MASTERY_LAST_STAND));
    AddSpellShield(uint32(m_player->GetMaxHealth() * m_mastery.lastStand / 100.0f));
    return damage - AbsorbSpellShield(damage);
}

void BonkRun::BurstIceBlock()
{
    if (!m_iceBlockBurstPending)
        return;
    m_iceBlockBurstPending = false;
    if (!m_player->IsAlive())
        return;
    m_player->SendPlaySpellVisualKit(BONK_KIT_ICE_BLOCK);
    for (Creature* mob : GetMobsInRange(BONK_ICE_BLOCK_RADIUS))
        ApplyKeyword(mob, BONK_KEYWORD_CHILL, BONK_ICE_BLOCK_CHILL, 0, 1.0f, 0);
}

void BonkRun::CastOnStruck()
{
    if (m_mastery.struckCast.empty() || m_struckAtMs == m_talentStruckSeenMs)
        return;
    m_talentStruckSeenMs = m_struckAtMs;
    if (!m_player->IsAlive())
        return;

    for (auto const& itr : m_mastery.struckCast)
    {
        uint32& readyMs = m_struckCastReadyMs[itr.first];
        if (readyMs > m_totalMs)
            continue;
        OwnedWeapon* owned = nullptr;
        for (OwnedWeapon& candidate : m_weapons)
            if (GetRootWeapon(candidate.weapon) == itr.first)
                owned = &candidate;
        if (!owned)
            continue;

        readyMs = m_totalMs + uint32(itr.second * IN_MILLISECONDS);
        if (owned->weapon.behavior == BONK_BEHAVIOR_TRAP)
        {
            DropTraps(*owned, true);
            continue;
        }

        PendingCast nova;
        nova.weaponId = owned->weapon.id;
        nova.spellId = owned->weapon.spellId;
        nova.leftMs = 0;
        nova.selfArea = true;
        m_pendingCasts.push_back(nova);
    }
}

void BonkRun::UpdateTalents()
{
    BurstIceBlock();
    CastOnStruck();
    if (m_mastery.Has(BONK_CAPSTONE_BERSERKER) && m_player->IsAlive())
        Enrage();
}

void BonkRun::OnTalentDash()
{
    if (!m_mastery.Has(BONK_CAPSTONE_TRAPMASTER))
        return;
    for (OwnedWeapon& owned : m_weapons)
        if (owned.weapon.behavior == BONK_BEHAVIOR_TRAP && owned.weapon.target == BONK_TARGET_SELF)
            DropTraps(owned, true);
}

void BonkRun::OnTalentBlink()
{
    if (m_mastery.blinkShield > 0.0f)
    {
        SupportScope scope(*this, TalentSupport(BONK_MASTERY_BLINK_SHIELD));
        AddSpellShield(uint32(m_player->GetMaxHealth() * m_mastery.blinkShield / 100.0f));
    }

    if (m_mastery.Has(BONK_CAPSTONE_ARCANE_SURGE))
    {
        uint32 const cost = uint32(m_player->GetMaxHealth() * BONK_ARCANE_SURGE_HEALTH_COST / 100.0f);
        uint32 const health = m_player->GetHealth();
        if (health > 1)
            m_player->ModifyHealth(-int32(std::min(cost, health - 1)));
    }
}

bool BonkRun::IsTalentBlinkDash() const
{
    return m_mastery.Has(BONK_CAPSTONE_ARCANE_SURGE);
}

float BonkRun::GetDashRechargeRate() const
{
    float rate = GetBlinkRechargeRate() * (1.0f + m_mastery.dashRecharge / 100.0f);
    if (m_mastery.Has(BONK_CAPSTONE_ARCANE_SURGE))
        rate *= BONK_ARCANE_SURGE_RECHARGE;
    return rate;
}

float BonkRun::GetTrapTalentDuration(OwnedWeapon const& owned) const
{
    return owned.weapon.behavior == BONK_BEHAVIOR_TRAP ? m_mastery.trapDuration : 0.0f;
}

void BonkRun::SpringEntrapment(OwnedWeapon const& owned, float x, float y, float radius)
{
    if (!m_mastery.entrapment.count(GetRootWeapon(owned.weapon)))
        return;
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : m_mobs)
        if (Creature* mob = map->GetCreature(guid))
        {
            BonkMobAI const* ai = GetRunAI(mob);
            if (ai && ai->GetKind() == BONK_SPAWN_NORMAL && mob->IsAlive() && mob->GetDistance2d(x, y) <= radius)
                ApplyKeyword(mob, BONK_KEYWORD_FROZEN, 1, 0, 1.0f, owned.weapon.spellId);
        }
}

void BonkRun::PreferMarked(std::vector<Creature*>& mobs) const
{
    if (!m_mastery.Has(BONK_CAPSTONE_TRUESHOT))
        return;
    std::stable_partition(mobs.begin(), mobs.end(), [this](Creature* mob) { return GetKeywordStacks(mob, BONK_KEYWORD_HUNTERS_MARK) != 0; });
}

float BonkRun::GetPackPowerShare() const
{
    return HasBehavior(BONK_BEHAVIOR_WRATH) ? m_mastery.packPower / 100.0f : 0.0f;
}

bool BonkRun::IsPetFrenzied() const
{
    return IsHowlFrenzied() || m_mastery.Has(BONK_CAPSTONE_BESTIAL_WRATH);
}

float BonkRun::GetBestialWrathFrenzy() const
{
    return BONK_BESTIAL_WRATH_FRENZY;
}
