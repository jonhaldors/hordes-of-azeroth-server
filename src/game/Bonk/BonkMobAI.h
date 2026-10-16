/*
 * Vanilla Bonk: AI for enemies spawned by a run. They always chase the
 * player who summoned them, never evade, and freeze while the run is paused.
 *
 * What an enemy does beyond walking up and hitting comes from bonk_mob and
 * bonk_mob_ability: ranged enemies keep their distance and cast, exploders
 * run in and blow up, and any enemy can have abilities on cooldowns. Elites
 * and mini-bosses also roll random affixes: at most one from each category
 * (pressure, sustain, bulk, enrage), as many as the tier says, each with a
 * glow of its own. A mini-event's treasure goblin runs from the player
 * instead (SetFleeing).
 */

#ifndef MANGOS_BONKMOBAI_H
#define MANGOS_BONKMOBAI_H

#include "CreatureAI.h"
#include "BonkMgr.h"
#include "ObjectGuid.h"

#include <string>
#include <vector>

class BonkPlayer;
class BonkRun;
class Player;
class SpellEntry;

class BonkMobAI : public CreatureAI
{
    public:
        explicit BonkMobAI(Creature* creature);

        // CreatureAiSetter for WorldObject::SummonCreature. Marks the creature
        // a run enemy (Creature::SetBonkEnemy) with the nameplate number set
        // by SetNextPlateId just before the summon.
        static void Install(Creature* creature);
        static void SetNextPlateId(uint32 id) { s_nextPlateId = id; }
        // The affixes the next elite or mini-boss set up rolls, whatever the
        // tier says (a Challenge Altar's guards, a bounty); -1: the tier's.
        static void SetNextAffixCount(int32 count) { s_nextAffixCount = count; }
        // An icon the next one shows on its nameplate before any affixes (a
        // bounty's skull); a static string.
        static void SetNextPlateIcon(char const* icon) { s_nextPlateIcon = icon; }
        // Its GUID as the client's UnitGUID prints it (ClassicAPI): the
        // addon's nameplates know an enemy by it.
        static std::string GetUnitGuidText(Creature const* creature);
        // Whether a creature going for its victim (Unit::Attack) sends its
        // aggro reaction, which the client voices. A run enemy does so only
        // the first time, and in a burst of them only one every
        // BONK_AGGRO_CRY_MS: a crowd re-engaging as a pause ends, or a wave
        // appearing at once, would start dozens of yells on one frame.
        static bool MayCry(Creature* creature);

        // Called by the run once the enemy's level, health, damage and speed are set.
        void Setup(BonkSpawnKind kind);
        // While a choice is open: stop, root, and drop the fight.
        void SetFrozen(bool frozen);
        // The run just moved it (ahead of the player, or out of a tree): it
        // stands a moment under a puff of smoke before it gives chase.
        void Arrive();
        BonkSpawnKind GetKind() const { return m_kind; }
        ObjectGuid GetOwnerGuid() const { return m_ownerGuid; }
        // Normal enemies only: what it is in the crowd.
        void SetArchetype(BonkArchetype archetype) { m_archetype = archetype; }
        BonkArchetype GetArchetype() const { return m_archetype; }

        // Affixes, for the run: elite chests, what happens when it dies, and
        // the addon's nameplates.
        uint32 GetAffixCount() const;
        bool IsBolstering() const { return HasAffix(AFFIX_BOLSTERING); }
        // How close the hero must stay for its Necrotic stacks to hold, 0 = not Necrotic.
        float GetNecroticRange() const;
        bool IsSanguine() const { return HasAffix(AFFIX_SANGUINE); }
        // "Icon,Icon": the affixes' icons.
        std::string GetAffixIcons() const;
        // "Name,Name": the affixes, for the boss bar.
        std::string GetAffixNames() const;
        // "affixinfo~name~icon~description" for every affix, so the addon can
        // say what each does (the boss bar's affix icons).
        static void SendAffixInfo(BonkPlayer* bonk);
        // A Bolstering enemy died nearby: more health, damage and size.
        void Bolster();
        // A treasure goblin: it runs from the player and never attacks;
        // `blinks`: hit, it blinks further away now and then. It stands
        // `headStartMs` first, so the hero can close in.
        void SetFleeing(bool blinks, uint32 headStartMs = 0);
        bool IsFleeing() const { return m_fleeing; }
        // A Necrotic Shard or a demon portal: it stands where it is and
        // never attacks; the run's event spawns its adds.
        void SetStationary();
        // A caravan's raider: it goes for `target` (the run's caravan), not
        // the hero, until that is gone.
        void SetRaidTarget(ObjectGuid target);
        void ClearRaidTarget() { m_raidTarget.Clear(); }
        // A stampede's beast: a red line `length` long at `angle` from where
        // it stands for `windupMs`, then a dash along it that runs the hero
        // over like a charger's (`damage` in melee hits); it is gone at the
        // end, or when the run pauses.
        void StartStampede(Player* owner, float angle, float length, uint32 windupMs, float damage);

        void MoveInLineOfSight(Unit*) override {}
        void AttackStart(Unit* victim) override;
        void EnterEvadeMode() override;
        void UpdateAI(uint32 const diff) override;
        void DamageDeal(Unit* victim, uint32& damage) override;
        void DamageTaken(Unit* attacker, uint32& damage) override;
        void JustDied(Unit* killer) override;

    private:
        enum Affix : uint8
        {
            AFFIX_SWIFT,        // pressure: moves faster
            AFFIX_EXPLOSIVE,    // pressure: a ring of fire bursts around it while the player is close
            AFFIX_NECROTIC,     // pressure: its hits stack a healing reduction on the player
            AFFIX_VAMPIRIC,     // sustain: heals from the damage it deals
            AFFIX_BOLSTERING,   // sustain: when it dies, enemies nearby grow stronger
            AFFIX_SANGUINE,     // sustain: when it dies, leaves a pool that heals enemies
            AFFIX_BULWARK,      // bulk: shields itself again a while after a shield breaks
            AFFIX_FORTIFIED,    // bulk: more health
            AFFIX_FRENZIED,     // enrage: attacks faster
            AFFIX_RAGING,       // enrage: deals much more damage once below 30% health
            AFFIX_VOLCANIC,     // pressure: now and then fire bursts up under the player, after a warning
            MAX_AFFIX
        };

        Player* GetOwner() const;
        bool HasAffix(Affix affix) const { return (m_affixes & (1u << affix)) != 0; }
        void RollAffixes(uint32 count);
        void ScaleHealth(float factor);
        float GetMeleeHit() const;
        void Cast(BonkMobAbility const& ability, Player* owner, bool triggered);
        void UpdateAbilities(uint32 diff, Player* owner);
        void UpdateAffixes(uint32 diff, Player* owner);
        // Bosses' phases (Phase 3): a stage boss calls adds at 66% and 33%
        // health and enrages at 33%; from Heroic it then lays void zones
        // under the hero, and on Mythic it shields itself at 66%. A
        // mini-boss calls adds at 50%.
        void UpdatePhases(uint32 diff, Player* owner);
        void Explode(Player* owner);
        // Areas around the enemy (a Thunderclap, a Fire Nova, an exploder's
        // blast): it stops, a red circle shows how far the spell reaches,
        // and the spell goes off as the circle fills, so a hero outside it
        // is never hit. `abilityIndex` -1: an exploder's blast.
        void StartArea(int32 abilityIndex, Player* owner);
        // A signature move (BonkAbilityShape): a cone or a ring winds up like
        // an area; a barrage and a trail go off at once.
        void StartShape(int32 abilityIndex, Player* owner);
        // While one winds up: true, and the enemy does nothing else.
        bool UpdateArea(uint32 diff, Player* owner);
        // Casters: a bolt at where the player stands, under a red circle.
        void AimBolt(BonkMobAbility const& ability, SpellEntry const* spellInfo, Player* owner, BonkRun* run);
        void UpdateBolt(uint32 diff, Player* owner);
        void AimBoltNow(Player* owner);
        void LaunchBolt();
        // Chargers: stop, warn with a red line, dash along it. Any enemy with
        // a Charge ability does the same when it uses it; only chargers charge
        // on their own timer.
        bool UpdateCharge(uint32 diff, Player* owner);
        void StartCharge(Player* owner, float damage);
        void Dash(Player* owner);
        // While dashing: the player it runs into is hit.
        void UpdateDash(Player* owner);
        void EndCharge(Player* owner);
        // Fleeing: a little further from the player every so often.
        void UpdateFlee(uint32 diff, Player* owner);
        bool FindFleePoint(Player* owner, float distance, float& x, float& y, float& z) const;

        ObjectGuid m_ownerGuid;
        BonkSpawnKind m_kind = BONK_SPAWN_NORMAL;
        BonkArchetype m_archetype = BONK_ARCHETYPE_GRUNT;
        BonkMob const* m_mob = nullptr;
        std::vector<int32> m_abilityTimers;
        uint32 m_affixes = 0;
        int32 m_shieldTimer = 0;
        int32 m_burnTimer = 0;
        int32 m_eruptionTimer = 3000;       // the first comes a little after it appears
        uint32 m_bolsterStacks = 0;
        bool m_enraged = false;     // Raging, below its health mark
        uint8 m_phase = 0;                  // boss phases passed
        bool m_phaseEnraged = false;        // a stage boss's last phase
        bool m_voidZones = false;           // laying void zones
        int32 m_voidTimer = 0;
        bool m_frozen = false;
        bool m_cried = false;               // its aggro reaction was sent, or passed over
        bool m_exploded = false;
        // An area winding up (StartArea): which ability (-1 the exploder's
        // blast), until it goes off, and where the enemy stands for it.
        bool m_areaWinding = false;
        int32 m_areaAbility = -1;
        int32 m_areaTimer = 0;
        float m_areaX = 0.0f, m_areaY = 0.0f;
        float m_areaOrientation = 0.0f;    // a cone's
        int32 m_arriveTimer = 0;           // standing where the run moved it
        enum ChargeState : uint8 { CHARGE_NONE, CHARGE_WINDUP, CHARGE_DASH };
        ChargeState m_chargeState = CHARGE_NONE;
        int32 m_chargeTimer = 2000;         // to the next charge, the dash, or its end
        float m_chargeX = 0.0f, m_chargeY = 0.0f, m_chargeZ = 0.0f;    // where the dash starts
        float m_chargeAngle = 0.0f, m_chargeLength = 0.0f;
        float m_chargeDamage = 0.0f;        // the dash's hit, in melee hits
        float m_dashX = 0.0f, m_dashY = 0.0f, m_dashZ = 0.0f;          // where it ends
        float m_dashFromX = 0.0f, m_dashFromY = 0.0f;   // where it was at the last look
        bool m_dashHit = false;                          // it has run into the player
        // An aimed bolt (AimBolt): cast at a stand-in target, which moves to
        // where the player will be as the cast ends.
        ObjectGuid m_boltTarget;
        uint32 m_boltSpell = 0;
        uint32 m_boltDamage = 0;
        uint32 m_boltLead = 0;              // ms between the aim and the cast's end
        int32 m_boltStartTimer = 0;         // until the cast starts
        int32 m_boltAimTimer = 0;           // until it aims
        bool m_boltStarted = false;
        bool m_boltAimed = false;
        std::string m_plateIcon;
        bool m_fleeing = false;
        bool m_blinks = false;
        bool m_blinkPending = false;        // hit: blinks at its next update
        int32 m_fleeTimer = 0;              // to its next step away
        bool m_fleeStanding = false;        // still at its head start
        bool m_stationary = false;          // SetStationary
        ObjectGuid m_raidTarget;            // SetRaidTarget
        bool m_stampede = false;            // StartStampede
        int32 m_blinkTimer = 0;             // to its next blink

        // Install's nameplate number for the next summon; per thread, as maps
        // may update side by side.
        static thread_local uint32 s_nextPlateId;
        static thread_local int32 s_nextAffixCount;
        static thread_local char const* s_nextPlateIcon;
};

#endif
