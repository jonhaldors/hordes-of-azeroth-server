/*
 * Vanilla Bonk: the mini-event scheduler, BonkRun's share of the in-run
 * events (feature plan, Mini-events).
 *
 * One mini-event every BONK_MINI_EVENT_INTERVAL_MS of a stage, picked at
 * random but never the same twice running (while there is more than one),
 * at most one under way, held while a mini-boss or the boss is near
 * (IsQuietTime) and none once the boss is up.
 *
 * Some happen to the hero wherever they are, unmarked: the encircling horde
 * surrounds the hero and closes in, growing through the stage and with the
 * difficulty's crowd. Others will happen somewhere on the map, at one of the
 * stage's spawn points, marked with an event objective (BonkObjectives), and
 * stay under way until they are done (m_miniEventActive).
 */

#include "BonkRun.h"
#include "BonkPlayer.h"
#include "Player.h"
#include "Utilities/Random.h"

#include <cstring>

namespace
{
    uint32 const BONK_MINI_EVENT_INTERVAL_MS = 2 * MINUTE * IN_MILLISECONDS;
    uint32 const BONK_MINI_EVENT_RETRY_MS    = 10 * IN_MILLISECONDS;   // it could not start yet

    float const BONK_HORDE_BASE              = 8.0f;    // enemies in the ring at 0:00...
    float const BONK_HORDE_PER_MINUTE        = 2.2f;    // ...and more each minute, times the tier's crowd

    char const* const MINI_EVENT_NAMES[MAX_BONK_MINI_EVENT] = { "horde" };
}

char const* GetBonkMiniEventName(BonkMiniEventType type)
{
    return type < MAX_BONK_MINI_EVENT ? MINI_EVENT_NAMES[type] : "";
}

bool ParseBonkMiniEvent(char const* name, BonkMiniEventType& type)
{
    for (uint8 i = 0; i < MAX_BONK_MINI_EVENT; ++i)
        if (!strcmp(name, MINI_EVENT_NAMES[i]))
        {
            type = BonkMiniEventType(i);
            return true;
        }
    return false;
}

void BonkRun::UpdateMiniEvents(uint32 /*diff*/)
{
    // An event at a spot runs until it is done; none of those exist yet.
    if (m_miniEventActive)
    {
        EndMiniEvent();
        return;
    }

    if (!m_nextMiniEventMs)
        m_nextMiniEventMs = BONK_MINI_EVENT_INTERVAL_MS;
    if (m_elapsedMs < m_nextMiniEventMs || m_bossSpawned || IsQuietTime())
        return;

    if (StartMiniEvent(PickMiniEvent()))
        m_nextMiniEventMs = m_elapsedMs + BONK_MINI_EVENT_INTERVAL_MS;
    else
        m_nextMiniEventMs = m_elapsedMs + BONK_MINI_EVENT_RETRY_MS;
}

BonkMiniEventType BonkRun::PickMiniEvent() const
{
    std::vector<BonkMiniEventType> choices;
    for (uint8 i = 0; i < MAX_BONK_MINI_EVENT; ++i)
        if (BonkMiniEventType(i) != m_lastMiniEvent || MAX_BONK_MINI_EVENT == 1)
            choices.push_back(BonkMiniEventType(i));
    return choices[urand(0, uint32(choices.size()) - 1)];
}

bool BonkRun::StartMiniEvent(BonkMiniEventType type)
{
    bool started = false;
    switch (type)
    {
        case BONK_MINI_EVENT_HORDE: started = SpawnHorde(); break;
        default:                    break;
    }
    if (started)
        m_lastMiniEvent = type;
    return started;
}

bool BonkRun::SpawnHorde()
{
    float const minutes = m_elapsedMs / float(MINUTE * IN_MILLISECONDS);
    BonkMapEvent ring;
    ring.second = m_elapsedMs / IN_MILLISECONDS;
    ring.type = BONK_EVENT_RING;
    ring.count = uint32((BONK_HORDE_BASE + BONK_HORDE_PER_MINUTE * minutes) * m_tier.minAliveMultiplier);
    if (!SpawnFormation(ring))
        return false;
    m_player->GetBonkPlayer()->Notify("You are surrounded!");
    return true;
}

void BonkRun::EndMiniEvent()
{
    if (m_miniEvent.objectiveId)
        RemoveObjective(m_miniEvent.objectiveId);
    else
        m_spawnPoints.Release(m_miniEvent.spot.x, m_miniEvent.spot.y);
    m_miniEvent = MiniEvent();
    m_miniEventActive = false;
}

bool BonkRun::StartMiniEventNow(BonkMiniEventType type, std::string& error)
{
    if (m_state != STATE_RUNNING || m_paused)
    {
        error = "Only a running stage can have a mini-event.";
        return false;
    }
    if (m_miniEventActive)
        EndMiniEvent();
    if (!StartMiniEvent(type))
    {
        error = "It could not start here (no room for more enemies, or no ground around you).";
        return false;
    }
    m_nextMiniEventMs = m_elapsedMs + BONK_MINI_EVENT_INTERVAL_MS;
    return true;
}
