/*
 * Vanilla Bonk: AI for enemies spawned by a run. They always chase the
 * player who summoned them, never evade, and freeze while the run is paused.
 *
 * What an enemy does beyond walking up and hitting comes from bonk_mob and
 * bonk_mob_ability: ranged enemies keep their distance and cast, exploders
 * run in and blow up, and any enemy can have abilities on cooldowns. Elites
 * and mini-bosses also roll random affixes.
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
            AFFIX_SWIFT,        // moves faster
            AFFIX_BULWARK,      // shields itself again a while after a shield breaks
            AFFIX_MOLTEN,       // burns the player when close
            AFFIX_VAMPIRIC,     // heals from the damage it deals
            AFFIX_FRENZIED,     // attacks faster
            MAX_AFFIX
        };

        Player* GetOwner() const;
        bool HasAffix(Affix affix) const { return (m_affixes & (1u << affix)) != 0; }
        void RollAffixes(uint32 count);
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
        bool m_frozen = false;
        bool m_exploded = false;
        uint32 m_strayTimer = 0;
};

#endif
