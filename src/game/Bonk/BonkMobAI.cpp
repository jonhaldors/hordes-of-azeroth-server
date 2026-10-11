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
#include "TemporarySummon.h"

namespace
{
    // Enemies this far from the player for this long are recycled.
    float const BONK_STRAY_DISTANCE = 70.0f;
    uint32 const BONK_STRAY_DESPAWN_MS = 5000;
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

void BonkMobAI::AttackStart(Unit* victim)
{
    // Only ever fight the player this run belongs to.
    Player* owner = GetOwner();
    if (!owner || victim != owner)
        return;

    CreatureAI::AttackStart(victim);
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

    DoMeleeAttackIfReady();
}

void BonkMobAI::JustDied(Unit* /*killer*/)
{
    Player* owner = GetOwner();
    if (!owner)
        return;

    if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
        run->OnMobKilled(m_creature, m_kind);
}
