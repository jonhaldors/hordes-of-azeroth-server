/*
 * Vanilla Bonk: ground telegraphs.
 */

#include "BonkTelegraph.h"
#include "GameObject.h"
#include "Map.h"

#include <cmath>

namespace
{
    // A plain object whose look the telegraph sets.
    uint32 const BONK_GO_TELEGRAPH = 900012;

    // Display ids from tools\telegraphs: per shape an outline, then a fill
    // for each length below.
    uint32 const BONK_TELEGRAPH_FIRST_DISPLAY = 7000;
    uint32 const BONK_TELEGRAPH_FILL_MS[] = { 1000, 1500, 2000, 2500, 3000 };
    uint32 const BONK_TELEGRAPH_FILL_COUNT = sizeof(BONK_TELEGRAPH_FILL_MS) / sizeof(BONK_TELEGRAPH_FILL_MS[0]);

    float const BONK_CONE_HALF_ANGLE = M_PI_F / 4.0f;
    float const BONK_LINE_HALF_WIDTH = 0.1f;    // of its length
    float const BONK_RING_INNER      = 0.5f;    // of its radius
    // Objects go on their own this long after their warning, should the run
    // never take them away; a pause can hold a warning for minutes.
    uint32 const BONK_TELEGRAPH_LINGER_MS = 10 * MINUTE * IN_MILLISECONDS;

    char const* const BONK_TELEGRAPH_SHAPE_NAMES[MAX_BONK_TELEGRAPH_SHAPE] = { "circle", "cone", "line", "ring" };
}

char const* GetBonkTelegraphShapeName(BonkTelegraphShape shape)
{
    return shape < MAX_BONK_TELEGRAPH_SHAPE ? BONK_TELEGRAPH_SHAPE_NAMES[shape] : "";
}

bool ParseBonkTelegraphShape(std::string const& name, BonkTelegraphShape& shape)
{
    for (uint8 i = 0; i < MAX_BONK_TELEGRAPH_SHAPE; ++i)
        if (name == BONK_TELEGRAPH_SHAPE_NAMES[i])
        {
            shape = BonkTelegraphShape(i);
            return true;
        }
    return false;
}

bool IsInBonkTelegraph(BonkTelegraphShape shape, float originX, float originY, float orientation, float size, float x, float y)
{
    float const dx = x - originX, dy = y - originY;
    float const distance = std::sqrt(dx * dx + dy * dy);
    switch (shape)
    {
        case BONK_TELEGRAPH_RING:
            return distance <= size && distance >= size * BONK_RING_INNER;
        case BONK_TELEGRAPH_CONE:
        {
            if (distance > size)
                return false;
            if (distance < 0.01f)
                return true;
            float diff = std::fabs(std::atan2(dy, dx) - orientation);
            while (diff > M_PI_F)
                diff = std::fabs(diff - 2.0f * M_PI_F);
            return diff <= BONK_CONE_HALF_ANGLE;
        }
        case BONK_TELEGRAPH_LINE:
        {
            float const along = dx * std::cos(orientation) + dy * std::sin(orientation);
            float const across = -dx * std::sin(orientation) + dy * std::cos(orientation);
            return along >= 0.0f && along <= size && std::fabs(across) <= size * BONK_LINE_HALF_WIDTH;
        }
        default:
            return distance <= size;
    }
}

void BonkTelegraphs::Add(Map* map, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
    std::function<void()> onFire)
{
    if (shape >= MAX_BONK_TELEGRAPH_SHAPE || size <= 0.0f)
        return;

    Telegraph telegraph;
    telegraph.shape = shape;
    telegraph.x = x;
    telegraph.y = y;
    float const ground = map->GetHeight(x, y, z + 2.0f, true, 10.0f);
    telegraph.z = ground > INVALID_HEIGHT ? ground : z;
    telegraph.orientation = orientation;
    telegraph.size = size;
    telegraph.leftMs = int32(durationMs);
    telegraph.onFire = std::move(onFire);

    // The longest fill that fits, started late enough to be full on time;
    // a warning shorter than every fill gets the shortest, cut off early.
    uint32 fill = 0;
    for (uint32 i = 0; i < BONK_TELEGRAPH_FILL_COUNT; ++i)
        if (BONK_TELEGRAPH_FILL_MS[i] <= durationMs)
            fill = i;
    uint32 const firstDisplay = BONK_TELEGRAPH_FIRST_DISPLAY + uint32(shape) * (1 + BONK_TELEGRAPH_FILL_COUNT);
    telegraph.fillDisplay = firstDisplay + 1 + fill;
    telegraph.fillInMs = int32(durationMs) - int32(BONK_TELEGRAPH_FILL_MS[fill]);

    telegraph.outline = Place(map, firstDisplay, telegraph, durationMs + BONK_TELEGRAPH_LINGER_MS);
    if (telegraph.fillInMs <= 0)
        telegraph.fill = Place(map, telegraph.fillDisplay, telegraph, durationMs + BONK_TELEGRAPH_LINGER_MS);
    m_telegraphs.push_back(std::move(telegraph));
}

void BonkTelegraphs::Update(Map* map, uint32 diff)
{
    if (m_telegraphs.empty())
        return;

    std::vector<Telegraph> due;
    for (auto itr = m_telegraphs.begin(); itr != m_telegraphs.end();)
    {
        itr->leftMs -= int32(diff);
        if (itr->fill.IsEmpty())
        {
            itr->fillInMs -= int32(diff);
            if (itr->fillInMs <= 0 && itr->leftMs > 0)
                itr->fill = Place(map, itr->fillDisplay, *itr, uint32(itr->leftMs) + BONK_TELEGRAPH_LINGER_MS);
        }
        if (itr->leftMs > 0)
        {
            ++itr;
            continue;
        }
        due.push_back(std::move(*itr));
        itr = m_telegraphs.erase(itr);
    }

    // Firing can warn again (a new eruption), so only once the list is settled.
    for (Telegraph& telegraph : due)
    {
        Remove(map, telegraph);
        if (telegraph.onFire)
            telegraph.onFire();
    }
}

void BonkTelegraphs::Clear(Map* map)
{
    if (map)
        for (Telegraph const& telegraph : m_telegraphs)
            Remove(map, telegraph);
    m_telegraphs.clear();
}

ObjectGuid BonkTelegraphs::Place(Map* map, uint32 displayId, Telegraph const& telegraph, uint32 lifetimeMs)
{
    // As WorldObject::SummonGameObject, with the look and size set before
    // the client first sees it.
    GameObject* go = GameObject::CreateGameObject(BONK_GO_TELEGRAPH);
    if (!go->Create(map->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT), BONK_GO_TELEGRAPH, map, telegraph.x, telegraph.y, telegraph.z,
        telegraph.orientation, 0.0f, 0.0f, 0.0f, 0.0f, 100, GO_STATE_READY))
    {
        delete go;
        return ObjectGuid();
    }
    go->SetUInt32Value(GAMEOBJECT_DISPLAYID, displayId);
    go->SetObjectScale(telegraph.size);
    go->SetRespawnTime(lifetimeMs / IN_MILLISECONDS + 1);
    go->SetSpawnedByDefault(false);
    map->Add(go);
    return go->GetObjectGuid();
}

void BonkTelegraphs::Remove(Map* map, Telegraph const& telegraph)
{
    for (ObjectGuid const& guid : { telegraph.outline, telegraph.fill })
        if (!guid.IsEmpty())
            if (GameObject* go = map->GetGameObject(guid))
                go->Delete();
}
