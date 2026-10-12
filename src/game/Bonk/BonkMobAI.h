/*
 * Vanilla Bonk: AI for enemies spawned by a run. They always chase the
 * player who summoned them, never evade, and freeze while the run is paused.
 *
 * What an enemy does beyond walking up and hitting comes from bonk_mob and
 * bonk_mob_ability: ranged enemies keep their distance and cast, exploders
 * run in and blow up, and any enemy can have abilities on cooldowns. Elites
 * and mini-bosses also roll random affixes: at most one from each category
 * (pressure, sustain, bulk, enrage), as many as the tier says, each with a
 * glow of its own.
 */

#ifndef MANGOS_BONKMOBAI_H
#define MANGOS_BONKMOBAI_H

#include "CreatureAI.h"
#include "BonkMgr.h"
#include "ObjectGuid.h"

#include <vector>

class Player;

class BonkMobAI : public CreatureAI
{
    public:
        explicit BonkMobAI(Creature* creature);

        // CreatureAiSetter for WorldObject::SummonCreature.
        static void Install(Creature* creature);

        // Called by the run once the enemy's level, health, damage and speed are set.
        void Setup(BonkSpawnKind kind);
        // While a choice is open: stop, root, and drop the fight.
        void SetFrozen(bool frozen);
        BonkSpawnKind GetKind() const { return m_kind; }
        ObjectGuid GetOwnerGuid() const { return m_ownerGuid; }

        // Affixes, for the run: elite chests, what happens when it dies, and
        // the addon's nameplates.
        uint32 GetAffixCount() const;
        bool IsBolstering() const { return HasAffix(AFFIX_BOLSTERING); }
        bool IsSanguine() const { return HasAffix(AFFIX_SANGUINE); }
        // "Icon,Icon": the affixes' icons.
        std::string GetAffixIcons() const;
        // A Bolstering enemy died nearby: more health, damage and size.
        void Bolster();

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
        void Explode(Player* owner);

        ObjectGuid m_ownerGuid;
        BonkSpawnKind m_kind = BONK_SPAWN_NORMAL;
        BonkMob const* m_mob = nullptr;
        std::vector<int32> m_abilityTimers;
        uint32 m_affixes = 0;
        int32 m_shieldTimer = 0;
        int32 m_burnTimer = 0;
        uint32 m_bolsterStacks = 0;
        bool m_enraged = false;     // Raging, below its health mark
        bool m_frozen = false;
        bool m_exploded = false;
        uint32 m_strayTimer = 0;
};

#endif
