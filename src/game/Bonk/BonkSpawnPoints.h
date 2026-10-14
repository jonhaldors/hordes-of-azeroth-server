/*
 * Vanilla Bonk: spawn points, the spots a stage's objectives (shrines, the
 * wandering vendor, mini-events) appear at.
 *
 * The stage gathers them a candidate at a time while it runs, around wherever
 * the hero is, so there is always a spot some way off worth walking to. A
 * spot is on the ground inside the arena, out of water, with a path from the
 * hero no longer than a detour (so not up a cliff or behind a wall), at least
 * BONK_SPOT_SPREAD from every other spot and clear of where the stage
 * started. Spots left far behind make room for new ones. An objective takes a
 * spot for as long as it stands, and gives it back after.
 */

#ifndef BONK_SPAWN_POINTS_H
#define BONK_SPAWN_POINTS_H

#include "Common.h"

#include <vector>

class Player;

struct BonkSpot
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    bool taken = false;
};

class BonkSpawnPoints
{
    public:
        // A new stage, starting at this spot.
        void Reset(float startX, float startY, float startZ);
        // Tries a candidate now and then; `zoneId` bounds the arena (0: the
        // whole map).
        void Update(Player* hero, uint32 zoneId, uint32 diff);

        // A free spot `minDistance` to `maxDistance` yards from the hero, now
        // taken; false if there is none yet.
        bool Take(Player const* hero, float minDistance, float maxDistance, BonkSpot& spot);
        // Gives back the spot taken at x, y.
        void Release(float x, float y);

        std::vector<BonkSpot> const& GetSpots() const { return m_spots; }

    private:
        bool TryCandidate(Player* hero, uint32 zoneId);
        bool IsClear(float x, float y) const;

        std::vector<BonkSpot> m_spots;
        float m_startX = 0.0f, m_startY = 0.0f, m_startZ = 0.0f;
        uint32 m_timer = 0;
};

#endif
