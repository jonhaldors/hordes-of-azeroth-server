/*
 * Vanilla Bonk: AI for enemies spawned by a run. They always chase the
 * player who summoned them, never evade, and freeze while the run is paused.
 */

#ifndef MANGOS_BONKMOBAI_H
#define MANGOS_BONKMOBAI_H

#include "CreatureAI.h"
#include "BonkMgr.h"
#include "ObjectGuid.h"

class Player;

class BonkMobAI : public CreatureAI
{
    public:
        explicit BonkMobAI(Creature* creature);

        // CreatureAiSetter for WorldObject::SummonCreature.
        static void Install(Creature* creature);

        void SetKind(BonkSpawnKind kind) { m_kind = kind; }
        BonkSpawnKind GetKind() const { return m_kind; }

        void MoveInLineOfSight(Unit*) override {}
        void AttackStart(Unit* victim) override;
        void EnterEvadeMode() override;
        void UpdateAI(uint32 const diff) override;
        void JustDied(Unit* killer) override;

    private:
        Player* GetOwner() const;

        ObjectGuid m_ownerGuid;
        BonkSpawnKind m_kind = BONK_SPAWN_NORMAL;
        bool m_frozen = false;
        uint32 m_strayTimer = 0;
};

#endif
