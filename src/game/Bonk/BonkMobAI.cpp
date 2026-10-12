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

    // An enemy that cannot walk to the player this long is moved: the core
    // would make it evade (and take no damage) after 3 seconds.
    uint32 const BONK_UNREACHABLE_MS = 1500;

    // Most creature templates are warriors without mana; casters get plenty.
    uint32 const BONK_CASTER_MANA = 1000000;

    // Affixes.
    float const BONK_SWIFT_SPEED        = 1.5f;     // times the enemy's speed
    uint32 const BONK_SPELL_SHIELD      = 17;       // Power Word: Shield
    float const BONK_SHIELD_HEALTH      = 0.15f;    // absorbs this share of max health
    uint32 const BONK_SHIELD_INTERVAL   = 15000;    // after the last shield broke or ran out
    uint32 const BONK_SPELL_BURN        = 20602;    // Fire Nova, 10 yards around the enemy
    float const BONK_BURN_RANGE         = 9.0f;
    float const BONK_BURN_DAMAGE        = 0.5f;     // melee hits
    uint32 const BONK_BURN_INTERVAL     = 3000;
    float const BONK_VAMPIRIC_HEAL      = 0.5f;     // share of damage dealt
    float const BONK_FRENZIED_SPEED     = 1.5f;     // times the enemy's attack speed

    char const* const BONK_AFFIX_NAMES[] = { "Swift", "Bulwark", "Molten", "Vampiric", "Frenzied" };
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

    // Elites and bosses shrug off stuns and other crowd control; slows still
    // work. Spells that stun still deal their damage.
    if (kind != BONK_SPAWN_NORMAL)
    {
        for (AuraType aura : { SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_CHARM })
        {
            m_creature->ApplySpellImmune(0, IMMUNITY_STATE, aura, true);
            m_creature->RemoveSpellsCausingAura(aura);
        }
        m_creature->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, true);
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

    if (HasAffix(AFFIX_FRENZIED))
        m_creature->SetAttackTime(BASE_ATTACK, uint32(m_creature->GetAttackTime(BASE_ATTACK) / BONK_FRENZIED_SPEED));

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

    // The run freezes every enemy when it pauses; this catches any it missed.
    if (run->IsPaused())
    {
        SetFrozen(true);
        return;
    }
    SetFrozen(false);

    // Leashing is measured from where combat started, so keep that under the mob.
    m_creature->SetCombatStartPosition(m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ());

    if (m_creature->GetVictim() != owner)
        AttackStart(owner);

    // Spawned in a tree or up on a roof: move it before it starts to evade.
    if (m_creature->m_targetNotReachableTimer > BONK_UNREACHABLE_MS)
    {
        m_creature->m_targetNotReachableTimer = 0;
        run->OnMobStuck(m_creature);
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

void BonkMobAI::SetFrozen(bool frozen)
{
    if (m_frozen == frozen)
        return;
    m_frozen = frozen;
    // Its auras (damage over time) and casts wait as well.
    m_creature->SetTimeFrozen(frozen && m_creature->IsAlive());

    if (frozen)
    {
        m_creature->InterruptNonMeleeSpells(false);
        m_creature->AttackStop();
        m_creature->GetMotionMaster()->Clear(false);
        m_creature->GetMotionMaster()->MoveIdle();
        // Rooted, so a chase path still being worked out cannot start it moving.
        m_creature->AddUnitState(UNIT_STATE_ROOT);
        m_creature->SetRooted(true);
        return;
    }

    m_creature->ClearUnitState(UNIT_STATE_ROOT);
    m_creature->SetRooted(false);
    if (Player* owner = GetOwner())
        AttackStart(owner);
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
    // The shield lasts longer than the wait, so the wait only starts once the
    // old shield is gone; otherwise it never comes down.
    if (HasAffix(AFFIX_BULWARK))
    {
        if (m_creature->HasAura(BONK_SPELL_SHIELD))
            m_shieldTimer = BONK_SHIELD_INTERVAL;
        else
        {
            m_shieldTimer -= int32(diff);
            if (m_shieldTimer <= 0)
            {
                m_shieldTimer = BONK_SHIELD_INTERVAL;
                int32 const absorb = std::max(1, int32(m_creature->GetMaxHealth() * BONK_SHIELD_HEALTH));
                m_creature->CastCustomSpell(m_creature, BONK_SPELL_SHIELD, absorb, {}, {}, true);
            }
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

    // Armor takes its share off every hit and spell, then the Shield soaks
    // what it can.
    if (victim->GetObjectGuid() == m_ownerGuid)
        if (BonkRun* run = static_cast<Player*>(victim)->GetBonkPlayer()->GetRun())
            damage = run->MitigateDamage(damage);

    if (HasAffix(AFFIX_VAMPIRIC))
        m_creature->ModifyHealth(int32(damage * BONK_VAMPIRIC_HEAL));
}

void BonkMobAI::DamageTaken(Unit* attacker, uint32& damage)
{
    // Only what actually comes off the enemy counts towards Leech.
    if (!damage || !attacker || attacker->GetObjectGuid() != m_ownerGuid)
        return;

    if (BonkRun* run = static_cast<Player*>(attacker)->GetBonkPlayer()->GetRun())
        run->OnWeaponDamage(std::min(damage, m_creature->GetHealth()));
}

void BonkMobAI::JustDied(Unit* /*killer*/)
{
    // The corpse's despawn timer runs on its own clock.
    m_creature->SetTimeFrozen(false);

    // An exploder that already blew up is not a kill.
    Player* owner = GetOwner();
    if (!owner || m_exploded)
        return;

    if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
        run->OnMobKilled(m_creature, m_kind);
}
