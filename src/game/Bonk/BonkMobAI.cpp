/*
 * Vanilla Bonk: AI for run enemies.
 */

#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Utilities/Random.h"

namespace
{
    // Enemies this far from the player for this long are recycled.
    float const BONK_STRAY_DISTANCE = 70.0f;
    uint32 const BONK_STRAY_DESPAWN_MS = 5000;

    // Most creature templates are warriors without mana; casters get plenty.
    uint32 const BONK_CASTER_MANA = 1000000;

    // Affixes.
    float const BONK_SWIFT_SPEED        = 1.5f;     // times the enemy's speed
    uint32 const BONK_SPELL_SHIELD      = 17;       // Power Word: Shield
    float const BONK_SHIELD_HEALTH      = 0.25f;    // absorbs this share of max health
    uint32 const BONK_SHIELD_INTERVAL   = 16000;    // after Weakened Soul wears off
    uint32 const BONK_SPELL_BURN        = 20602;    // Fire Nova, 10 yards around the enemy
    float const BONK_BURN_RANGE         = 9.0f;
    float const BONK_BURN_DAMAGE        = 0.5f;     // melee hits
    uint32 const BONK_BURN_INTERVAL     = 3000;
    float const BONK_VAMPIRIC_HEAL      = 0.5f;     // share of damage dealt
    uint32 const BONK_SPELL_CHILL       = 7321;     // Chilled: slower movement and attacks

    char const* const BONK_AFFIX_NAMES[] = { "Swift", "Bulwark", "Molten", "Vampiric", "Frozen" };
}

BonkMobAI::BonkMobAI(Creature* creature) : CreatureAI(creature)
{
    if (creature->IsTemporarySummon())
        m_ownerGuid = static_cast<TemporarySummon*>(creature)->GetSummonerGuid();
}

void BonkMobAI::Install(Creature* creature)
{
    creature->GetMotionMaster()->Initialize();
    creature->SetAI(new BonkMobAI(creature));
}

Player* BonkMobAI::GetOwner() const
{
    return m_creature->GetMap()->GetPlayer(m_ownerGuid);
}

void BonkMobAI::Setup(BonkSpawnKind kind)
{
    m_kind = kind;
    m_mob = sBonkMgr.GetMob(m_creature->GetEntry());

    if (m_mob && !m_mob->abilities.empty())
    {
        for (BonkMobAbility const& ability : m_mob->abilities)
            m_abilityTimers.push_back(int32(ability.firstMs));

        if (m_creature->GetPowerType() != POWER_MANA)
            m_creature->SetPowerType(POWER_MANA);
        m_creature->SetModifierValue(UNIT_MOD_MANA, BASE_VALUE, float(BONK_CASTER_MANA));
        m_creature->SetMaxPower(POWER_MANA, BONK_CASTER_MANA);
        m_creature->SetPower(POWER_MANA, BONK_CASTER_MANA);
    }

    if (kind == BONK_SPAWN_ELITE)
        RollAffixes(1);
    else if (kind == BONK_SPAWN_MINIBOSS)
        RollAffixes(2);
}

void BonkMobAI::RollAffixes(uint32 count)
{
    std::vector<Affix> available;
    for (uint8 affix = 0; affix < MAX_AFFIX; ++affix)
        available.push_back(Affix(affix));

    std::string names;
    for (uint32 i = 0; i < count && !available.empty(); ++i)
    {
        uint32 const pick = urand(0, uint32(available.size() - 1));
        Affix const affix = available[pick];
        available.erase(available.begin() + pick);
        m_affixes |= 1u << affix;
        names += (names.empty() ? "" : ", ") + std::string(BONK_AFFIX_NAMES[affix]);
    }

    if (HasAffix(AFFIX_SWIFT))
    {
        m_creature->SetSpeedRate(MOVE_WALK, m_creature->GetSpeedRate(MOVE_WALK) * BONK_SWIFT_SPEED);
        m_creature->SetSpeedRate(MOVE_RUN, m_creature->GetSpeedRate(MOVE_RUN) * BONK_SWIFT_SPEED);
    }

    if (!names.empty())
        if (Player* owner = GetOwner())
            owner->GetBonkPlayer()->Notify(std::string(m_creature->GetName()) + ": " + names);
}

float BonkMobAI::GetMeleeHit() const
{
    return (m_creature->GetFloatValue(UNIT_FIELD_MINDAMAGE) + m_creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2.0f;
}

void BonkMobAI::AttackStart(Unit* victim)
{
    // Only ever fight the player this run belongs to.
    Player* owner = GetOwner();
    if (!owner || victim != owner)
        return;

    CreatureAI::AttackStart(victim);

    // Ranged enemies stop short and cast from there.
    if (m_mob && m_mob->role == BONK_ROLE_RANGED && m_creature->GetVictim() == victim)
        m_creature->GetMotionMaster()->MoveChase(victim, m_mob->range);
}

void BonkMobAI::EnterEvadeMode()
{
    Player* owner = GetOwner();
    BonkRun* run = owner ? owner->GetBonkPlayer()->GetRun() : nullptr;
    if (run && run->IsActive() && owner->IsAlive() && m_creature->IsAlive())
    {
        AttackStart(owner);
        return;
    }

    CreatureAI::EnterEvadeMode();
}

void BonkMobAI::UpdateAI(uint32 const diff)
{
    if (m_creature->HasCreatureState(CSTATE_DESPAWNING))
        return;

    Player* owner = GetOwner();
    BonkRun* run = owner ? owner->GetBonkPlayer()->GetRun() : nullptr;
    if (!run || !run->IsActive() || !owner->IsAlive())
    {
        m_creature->DespawnOrUnsummon();
        return;
    }

    if (run->IsPaused())
    {
        if (!m_frozen)
        {
            m_frozen = true;
            m_creature->InterruptNonMeleeSpells(false);
            m_creature->AttackStop();
            m_creature->GetMotionMaster()->Clear(false);
            m_creature->GetMotionMaster()->MoveIdle();
            m_creature->StopMoving();
        }
        return;
    }

    // Leashing is measured from where combat started, so keep that under the mob.
    m_creature->SetCombatStartPosition(m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ());

    if (m_frozen || m_creature->GetVictim() != owner)
    {
        m_frozen = false;
        AttackStart(owner);
    }

    if (!m_creature->IsWithinDistInMap(owner, BONK_STRAY_DISTANCE))
    {
        m_strayTimer += diff;
        if (m_strayTimer >= BONK_STRAY_DESPAWN_MS)
        {
            m_strayTimer = 0;
            run->OnMobStrayed(m_creature, m_kind);

            // Bosses are brought back instead of recycled.
            if (m_kind != BONK_SPAWN_BOSS && m_kind != BONK_SPAWN_MINIBOSS)
            {
                m_creature->DespawnOrUnsummon();
                return;
            }
        }
    }
    else
        m_strayTimer = 0;

    if (m_mob && m_mob->role == BONK_ROLE_EXPLODER)
    {
        if (m_creature->IsWithinDistInMap(owner, m_mob->range))
            Explode(owner);
        return;
    }

    UpdateAffixes(diff, owner);
    UpdateAbilities(diff, owner);
    DoMeleeAttackIfReady();
}

void BonkMobAI::Cast(BonkMobAbility const& ability, Player* owner, bool triggered)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(ability.spellId);
    if (!spellInfo)
        return;

    Unit* target = ability.target == BONK_ABILITY_SELF ? static_cast<Unit*>(m_creature) : owner;

    // Spell damage follows the enemy's own hits, so it scales with the stage.
    optional<int32> basePoints[MAX_EFFECT_INDEX];
    if (ability.damage > 0.0f && ability.damageEffectIndex >= 0)
        basePoints[ability.damageEffectIndex] = std::max(1, int32(GetMeleeHit() * ability.damage));

    if (target != m_creature)
        m_creature->SetFacingToObject(target);
    m_creature->CastCustomSpell(target, spellInfo, basePoints[0], basePoints[1], basePoints[2], triggered);
}

void BonkMobAI::UpdateAbilities(uint32 diff, Player* owner)
{
    if (!m_mob)
        return;

    for (int32& timer : m_abilityTimers)
        timer -= int32(diff);

    if (m_creature->IsNonMeleeSpellCasted(false))
        return;

    // One cast at a time: the first ready ability with the player in range.
    float const distance = m_creature->GetDistance(owner);
    for (size_t i = 0; i < m_mob->abilities.size(); ++i)
    {
        BonkMobAbility const& ability = m_mob->abilities[i];
        if (m_abilityTimers[i] > 0 || distance < ability.minRange || distance > ability.maxRange)
            continue;
        if (ability.target == BONK_ABILITY_PLAYER && !m_creature->IsWithinLOSInMap(owner))
            continue;

        Cast(ability, owner, false);
        m_abilityTimers[i] = int32(ability.cooldownMs * frand(0.85f, 1.15f));
        return;
    }
}

void BonkMobAI::UpdateAffixes(uint32 diff, Player* owner)
{
    if (HasAffix(AFFIX_BULWARK))
    {
        m_shieldTimer -= int32(diff);
        if (m_shieldTimer <= 0)
        {
            m_shieldTimer = BONK_SHIELD_INTERVAL;
            int32 const absorb = std::max(1, int32(m_creature->GetMaxHealth() * BONK_SHIELD_HEALTH));
            m_creature->CastCustomSpell(m_creature, BONK_SPELL_SHIELD, absorb, {}, {}, true);
        }
    }

    if (HasAffix(AFFIX_MOLTEN))
    {
        m_burnTimer -= int32(diff);
        if (m_burnTimer <= 0 && m_creature->IsWithinDistInMap(owner, BONK_BURN_RANGE))
        {
            m_burnTimer = BONK_BURN_INTERVAL;
            int32 const damage = std::max(1, int32(GetMeleeHit() * BONK_BURN_DAMAGE));
            m_creature->CastCustomSpell(m_creature, BONK_SPELL_BURN, damage, {}, {}, true);
        }
    }
}

void BonkMobAI::Explode(Player* owner)
{
    if (m_exploded)
        return;
    m_exploded = true;
    m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);

    // Blows up with everything it has, and leaves no reward behind.
    if (m_mob)
        for (BonkMobAbility const& ability : m_mob->abilities)
            if (ability.target == BONK_ABILITY_SELF)
                Cast(ability, owner, true);

    m_creature->DespawnOrUnsummon(400);
}

void BonkMobAI::DamageDeal(Unit* victim, uint32& damage)
{
    if (!damage || victim == m_creature)
        return;

    if (HasAffix(AFFIX_VAMPIRIC))
        m_creature->ModifyHealth(int32(damage * BONK_VAMPIRIC_HEAL));

    if (HasAffix(AFFIX_FROZEN) && victim->IsPlayer())
        m_creature->CastSpell(victim, BONK_SPELL_CHILL, true);
}

void BonkMobAI::JustDied(Unit* /*killer*/)
{
    // An exploder that already blew up is not a kill.
    Player* owner = GetOwner();
    if (!owner || m_exploded)
        return;

    if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
        run->OnMobKilled(m_creature, m_kind);
}
