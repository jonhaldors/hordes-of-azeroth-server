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
struct BonkEnemyHit;

class BonkMobAI : public CreatureAI
{
    public:
        explicit BonkMobAI(Creature* creature);

        static void Install(Creature* creature);
        static void SetNextPlateId(uint32 id) { s_nextPlateId = id; }
        static void SetNextAffixCount(int32 count) { s_nextAffixCount = count; }
        static void SetNextPlateIcon(char const* icon) { s_nextPlateIcon = icon; }
        static void SetNextFlyer(bool flyer) { s_nextFlyer = flyer; }
        static std::string GetUnitGuidText(Creature const* creature);
        static bool MayCry(Creature* creature);

        void Setup(BonkSpawnKind kind);
        void SetFrozen(bool frozen);
        void Arrive();
        bool CanBePulled() const { return !m_stampede && !m_stationary && !m_fleeing; }
        void Pull(float x, float y, float z, float speed, uint32 settleMs);
        BonkSpawnKind GetKind() const { return m_kind; }
        bool IsFlyer() const { return m_creature->IsBonkFlyer(); }
        ObjectGuid GetOwnerGuid() const { return m_ownerGuid; }
        void SetArchetype(BonkArchetype archetype) { m_archetype = archetype; }
        BonkArchetype GetArchetype() const { return m_archetype; }

        uint32 GetAffixCount() const;
        bool IsBolstering() const { return HasAffix(AFFIX_BOLSTERING); }
        float GetNecroticRange() const;
        bool IsSanguine() const { return HasAffix(AFFIX_SANGUINE); }
        std::string GetAffixIcons() const;
        std::string GetAffixNames() const;
        static void SendAffixInfo(BonkPlayer* bonk);
        void Bolster();
        void SetFleeing(bool blinks, uint32 headStartMs = 0);
        void SetFleeHeading(float heading);
        bool IsFleeing() const { return m_fleeing; }
        void SetStationary();
        void SetPassive(bool passive);
        void SetRaidTarget(ObjectGuid target);
        void ClearRaidTarget() { m_raidTarget.Clear(); }
        void StartStampede(Player* owner, float angle, float length, uint32 windupMs, float damage);
        void LandWindup(uint32 windId);
        bool IsWindingUp(uint32 windId) const;
        static void KnockBackHero(Player* hero, float angle);
        std::string Describe() const;

        using CreatureAI::JustSummoned;
        void JustSummoned(Creature* summoned) override;
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
            AFFIX_SWIFT,
            AFFIX_EXPLOSIVE,
            AFFIX_NECROTIC,
            AFFIX_VAMPIRIC,
            AFFIX_BOLSTERING,
            AFFIX_SANGUINE,
            AFFIX_BULWARK,
            AFFIX_FORTIFIED,
            AFFIX_FRENZIED,
            AFFIX_RAGING,
            AFFIX_VOLCANIC,
            MAX_AFFIX
        };

        Player* GetOwner() const;
        void Halt();
        bool HasAffix(Affix affix) const { return (m_affixes & (1u << affix)) != 0; }
        void RollAffixes(uint32 count);
        void ScaleHealth(float factor);
        float GetMeleeHit() const;
        float GetHitScale(BonkRun const* run) const;
        BonkEnemyHit MakeHit(BonkRun const* run, uint32 spellId, uint32 damage, uint32 windId = 0) const;
        uint32 GetAreaDamage(BonkMobAbility const& ability, SpellEntry const* spellInfo) const;
        uint32 GetShapeDamage(BonkMobAbility const& ability) const;
        void Cast(BonkMobAbility const& ability, Player* owner, bool triggered);
        void UpdateAbilities(uint32 diff, Player* owner);
        void UpdateAffixes(uint32 diff, Player* owner);
        void UpdatePhases(uint32 diff, Player* owner);
        void StartArea(int32 abilityIndex, Player* owner);
        void StartShape(int32 abilityIndex, Player* owner);
        void WindUp(int32 abilityIndex, float orientation, uint32 durationMs);
        bool StartLeap(int32 abilityIndex, Player* owner, BonkRun* run, uint32 damage);
        void StartLines(int32 abilityIndex, Player* owner, BonkRun* run, uint32 damage);
        void StartPools(BonkMobAbility const& ability, Player* owner, BonkRun* run);
        std::vector<float> GetLineAngles() const;
        bool IsInLines(float x, float y) const;
        void LaunchLeap();
        bool UpdateArea(uint32 diff, Player* owner);
        void ResolveArea(Player* owner);
        void ResumeChase(Player* owner);
        void AimBolt(BonkMobAbility const& ability, SpellEntry const* spellInfo, Player* owner, BonkRun* run);
        void UpdateBolt(uint32 diff, Player* owner);
        void AimBoltNow(Player* owner);
        void LaunchBolt();
        bool UpdateCharge(uint32 diff, Player* owner);
        void StartCharge(Player* owner, float damage);
        void WarnCharge(BonkRun* run, uint32 windupMs);
        void Dash(Player* owner);
        void UpdateDash(Player* owner);
        void EndCharge(Player* owner);
        void UpdateFlee(uint32 diff, Player* owner);
        void UpdateProgressWatch(uint32 diff, Player* owner, BonkRun* run);
        void ResetProgressWatch();
        void UpdatePerchChase(BonkRun const* run);
        Unit* GetCastTarget(Creature* target);
        float GetFleeAngle(Player* owner) const;
        bool FindFleePoint(Player* owner, float distance, float& x, float& y, float& z) const;

        ObjectGuid m_ownerGuid;
        BonkSpawnKind m_kind = BONK_SPAWN_NORMAL;
        BonkArchetype m_archetype = BONK_ARCHETYPE_GRUNT;
        BonkMob const* m_mob = nullptr;
        std::vector<int32> m_abilityTimers;
        uint32 m_affixes = 0;
        int32 m_shieldTimer = 0;
        int32 m_burnTimer = 0;
        int32 m_eruptionTimer = 3000;
        uint32 m_bolsterStacks = 0;
        bool m_enraged = false;
        uint8 m_phase = 0;
        bool m_phaseEnraged = false;
        bool m_voidZones = false;
        int32 m_voidTimer = 0;
        bool m_frozen = false;
        bool m_cried = false;
        bool m_areaWinding = false;
        int32 m_areaAbility = -1;
        int32 m_areaTimer = 0;
        float m_areaX = 0.0f, m_areaY = 0.0f;
        float m_areaOrientation = 0.0f;
        uint32 m_lineCount = 0;
        float m_lineSpread = 0.0f;
        float m_lineLength = 0.0f;
        float m_leapX = 0.0f, m_leapY = 0.0f, m_leapZ = 0.0f;
        int32 m_leapTravelMs = 0;
        bool m_leapLaunched = false;
        int32 m_abilityGapTimer = 0;
        float m_watchX = 0.0f, m_watchY = 0.0f;
        uint32 m_watchTimer = 0;
        uint32 m_windCount = 0;
        uint32 m_areaWindId = 0;
        uint32 m_chargeWindId = 0;
        int32 m_arriveTimer = 0;
        enum ChargeState : uint8 { CHARGE_NONE, CHARGE_WINDUP, CHARGE_DASH };
        ChargeState m_chargeState = CHARGE_NONE;
        int32 m_chargeTimer = 2000;
        float m_chargeX = 0.0f, m_chargeY = 0.0f, m_chargeZ = 0.0f;
        float m_chargeAngle = 0.0f, m_chargeLength = 0.0f;
        float m_chargeDamage = 0.0f;
        float m_dashX = 0.0f, m_dashY = 0.0f, m_dashZ = 0.0f;
        float m_dashFromX = 0.0f, m_dashFromY = 0.0f;
        bool m_dashHit = false;
        ObjectGuid m_boltTarget;
        uint32 m_boltSpell = 0;
        uint32 m_boltDamage = 0;
        uint32 m_boltLead = 0;
        int32 m_boltStartTimer = 0;
        int32 m_boltAimTimer = 0;
        bool m_boltStarted = false;
        bool m_boltAimed = false;
        std::string m_plateIcon;
        bool m_fleeing = false;
        bool m_blinks = false;
        bool m_blinkPending = false;
        int32 m_fleeTimer = 0;
        bool m_fleeStanding = false;
        bool m_stationary = false;
        bool m_passive = false;
        ObjectGuid m_raidTarget;
        float m_raidAngle = 0.0f;
        bool m_hasFleeHeading = false;
        float m_fleeHeading = 0.0f;
        bool m_stampede = false;
        int32 m_blinkTimer = 0;

        static thread_local uint32 s_nextPlateId;
        static thread_local int32 s_nextAffixCount;
        static thread_local char const* s_nextPlateIcon;
        static thread_local bool s_nextFlyer;
};

#endif
