/*
 * Vanilla Bonk: ground telegraphs, red warnings that fill before an enemy
 * ability goes off.
 *
 * Every shape is two gameobjects from the client patch that
 * tools\telegraphs builds (Data\patch-3.MPQ): an outline at full size, and a
 * fill that grows to it over the warning. The fills come in a few lengths
 * (BONK_TELEGRAPH_FILL_MS); a longer warning starts its fill late, so it is
 * full when the ability fires.
 */

#ifndef BONK_TELEGRAPH_H
#define BONK_TELEGRAPH_H

#include "Common.h"
#include "ObjectGuid.h"

#include <functional>
#include <string>
#include <vector>

class Map;

enum BonkTelegraphShape : uint8
{
    BONK_TELEGRAPH_CIRCLE = 0,      // size: radius
    BONK_TELEGRAPH_CONE   = 1,      // size: length; 90 degrees wide, along the orientation
    BONK_TELEGRAPH_LINE   = 2,      // size: length; a fifth as wide, along the orientation
    BONK_TELEGRAPH_RING   = 3,      // size: outer radius; safe inside half of it
    MAX_BONK_TELEGRAPH_SHAPE
};

char const* GetBonkTelegraphShapeName(BonkTelegraphShape shape);
bool ParseBonkTelegraphShape(std::string const& name, BonkTelegraphShape& shape);

// Whether a spot is inside a telegraph's shape.
bool IsInBonkTelegraph(BonkTelegraphShape shape, float originX, float originY, float orientation, float size, float x, float y);

class BonkTelegraphs
{
    public:
        // Warns for `durationMs`, then calls `onFire` (if any) and takes the
        // warning away. `z` is a guess; the ground's height is used.
        void Add(Map* map, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
            std::function<void()> onFire = nullptr);
        void Update(Map* map, uint32 diff);
        // Takes every warning away without firing it.
        void Clear(Map* map);
        size_t Count() const { return m_telegraphs.size(); }

    private:
        struct Telegraph
        {
            BonkTelegraphShape shape = BONK_TELEGRAPH_CIRCLE;
            float x = 0.0f, y = 0.0f, z = 0.0f, orientation = 0.0f, size = 1.0f;
            ObjectGuid outline;
            ObjectGuid fill;
            uint32 fillDisplay = 0;
            int32 fillInMs = 0;         // the fill appears when this runs out
            int32 leftMs = 0;           // fires when this runs out
            std::function<void()> onFire;
        };

        static ObjectGuid Place(Map* map, uint32 displayId, Telegraph const& telegraph, uint32 lifetimeMs);
        static void Remove(Map* map, Telegraph const& telegraph);

        std::vector<Telegraph> m_telegraphs;
};

#endif
