#include "BonkSpawnPoints.h"

#include "GridMap.h"
#include "Map.h"
#include "PathFinder.h"
#include "Player.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <cmath>

namespace
{
    uint32 const BONK_SPOT_TRY_MS        = 150;     // one candidate this often
    uint32 const BONK_SPOT_MAX           = 40;
    // WoW zones are big: objectives sit a good walk away (BonkRun takes them
    // 120-250 yards off), so spots gather well beyond that.
    float const BONK_SPOT_MIN_DISTANCE   = 80.0f;   // candidates this far from the hero...
    float const BONK_SPOT_MAX_DISTANCE   = 320.0f;  // ...to this far
    float const BONK_SPOT_SPREAD         = 70.0f;   // apart from each other
    float const BONK_SPOT_START_CLEAR    = 60.0f;   // from where the stage started
    float const BONK_SPOT_FORGET         = 450.0f;  // a full list drops free spots this far behind
    float const BONK_SPOT_GROUND_ABOVE   = 40.0f;   // ground searched from this far above the hero...
    float const BONK_SPOT_GROUND_DEPTH   = 120.0f;  // ...down this far
    float const BONK_SPOT_WATER_DEPTH    = 1.0f;    // deeper water than this is no spot
    float const BONK_SPOT_MAX_DETOUR     = 1.6f;    // the path may be this much longer than the straight line
    float const BONK_SPOT_SNAP           = 3.0f;    // the path must end this close to the spot

    float DistanceSq(float x1, float y1, float x2, float y2)
    {
        return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
    }
}

void BonkSpawnPoints::Reset(float startX, float startY, float startZ)
{
    m_spots.clear();
    m_startX = startX;
    m_startY = startY;
    m_startZ = startZ;
    m_timer = 0;
}

void BonkSpawnPoints::Update(Player* hero, uint32 zoneId, uint32 diff)
{
    m_timer += diff;
    if (m_timer < BONK_SPOT_TRY_MS)
        return;
    m_timer = 0;

    if (!hero->IsInWorld() || hero->IsBeingTeleported())
        return;

    if (m_spots.size() >= BONK_SPOT_MAX)
    {
        // Full: make room only by forgetting a free spot far behind.
        auto farthest = m_spots.end();
        float farthestSq = BONK_SPOT_FORGET * BONK_SPOT_FORGET;
        for (auto itr = m_spots.begin(); itr != m_spots.end(); ++itr)
        {
            float const distanceSq = DistanceSq(itr->x, itr->y, hero->GetPositionX(), hero->GetPositionY());
            if (!itr->taken && distanceSq > farthestSq)
            {
                farthest = itr;
                farthestSq = distanceSq;
            }
        }
        if (farthest == m_spots.end())
            return;
        m_spots.erase(farthest);
    }

    TryCandidate(hero, zoneId);
}

bool BonkSpawnPoints::TryCandidate(Player* hero, uint32 zoneId)
{
    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float const distance = frand(BONK_SPOT_MIN_DISTANCE, BONK_SPOT_MAX_DISTANCE);
    float x = hero->GetPositionX() + distance * std::cos(angle);
    float y = hero->GetPositionY() + distance * std::sin(angle);
    if (!IsClear(x, y))
        return false;

    Map const* map = hero->GetMap();
    float z = map->GetHeight(x, y, hero->GetPositionZ() + BONK_SPOT_GROUND_ABOVE, true, BONK_SPOT_GROUND_DEPTH);
    if (z <= INVALID_HEIGHT)
        return false;
    if (zoneId && map->GetTerrain()->GetZoneId(x, y, z) != zoneId)
        return false;
    if (map->GetTerrain()->IsInWater(x, y, z + BONK_SPOT_WATER_DEPTH))
        return false;

    // Ground behind a wall, up a cliff or inside rock has a height too; only
    // spots with a path from the hero will do.
    PathFinder path(hero);
    path.ExcludeSteepSlopes();
    path.calculate(x, y, z);
    if (!(path.getPathType() & PATHFIND_NOT_USING_PATH))
    {
        if (!(path.getPathType() & PATHFIND_NORMAL) || (path.getPathType() & PATHFIND_INCOMPLETE) ||
            path.Length() > distance * BONK_SPOT_MAX_DETOUR)
            return false;
        Vector3 const end = path.getActualEndPosition();
        if (DistanceSq(end.x, end.y, x, y) > BONK_SPOT_SNAP * BONK_SPOT_SNAP)
            return false;
        x = end.x;
        y = end.y;
        z = end.z;
        if (!IsClear(x, y))
            return false;
    }

    BonkSpot spot;
    spot.x = x;
    spot.y = y;
    spot.z = z;
    m_spots.push_back(spot);
    return true;
}

bool BonkSpawnPoints::IsClear(float x, float y) const
{
    if (DistanceSq(x, y, m_startX, m_startY) < BONK_SPOT_START_CLEAR * BONK_SPOT_START_CLEAR)
        return false;
    return std::none_of(m_spots.begin(), m_spots.end(),
        [x, y](BonkSpot const& spot) { return DistanceSq(x, y, spot.x, spot.y) < BONK_SPOT_SPREAD * BONK_SPOT_SPREAD; });
}

bool BonkSpawnPoints::Take(Player const* hero, float minDistance, float maxDistance, BonkSpot& spot)
{
    std::vector<BonkSpot*> fits;
    for (BonkSpot& candidate : m_spots)
    {
        if (candidate.taken)
            continue;
        float const distanceSq = DistanceSq(candidate.x, candidate.y, hero->GetPositionX(), hero->GetPositionY());
        if (distanceSq >= minDistance * minDistance && distanceSq <= maxDistance * maxDistance)
            fits.push_back(&candidate);
    }
    if (fits.empty())
        return false;

    BonkSpot* chosen = fits[urand(0, uint32(fits.size()) - 1)];
    chosen->taken = true;
    spot = *chosen;
    return true;
}

void BonkSpawnPoints::Release(float x, float y)
{
    for (BonkSpot& spot : m_spots)
        if (DistanceSq(x, y, spot.x, spot.y) < 1.0f)
            spot.taken = false;
}
