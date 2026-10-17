#include "BonkTelegraph.h"
#include "BonkMgr.h"
#include "Creature.h"
#include "Map.h"
#include "MotionMaster.h"
#include "NullCreatureAI.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

namespace
{
    uint32 const BONK_STANDIN_FIRST_DISPLAY = 18012;
    uint32 const BONK_TELEGRAPH_FILL_MS[] = { 1000, 1500, 2000, 2500, 3000 };
    uint32 const BONK_TELEGRAPH_FILL_COUNT = sizeof(BONK_TELEGRAPH_FILL_MS) / sizeof(BONK_TELEGRAPH_FILL_MS[0]);
    float const BONK_STANDIN_SIZES[] = { 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 6.0f, 6.5f, 7.0f, 7.5f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f };
    uint32 const BONK_STANDIN_SIZE_COUNT = sizeof(BONK_STANDIN_SIZES) / sizeof(BONK_STANDIN_SIZES[0]);
    uint32 const BONK_STANDIN_SIZED_SHAPES = 3;
    uint32 const BONK_STANDIN_LINE_DISPLAY = BONK_STANDIN_FIRST_DISPLAY +
        BONK_STANDIN_SIZED_SHAPES * (1 + BONK_TELEGRAPH_FILL_COUNT) * BONK_STANDIN_SIZE_COUNT;
    uint32 const BONK_LINE_FILL_PIECE_MS[] = { 60, 80, 100, 125, 150, 200, 250, 300 };
    uint32 const BONK_LINE_FILL_PIECE_COUNT = sizeof(BONK_LINE_FILL_PIECE_MS) / sizeof(BONK_LINE_FILL_PIECE_MS[0]);
    uint32 const BONK_LINE_MAX_PIECES = 12;
    float const BONK_LINE_PIECE_SIZE = 3.0f;

    uint32 const BONK_STANDIN_INVISIBLE   = 11686;
    uint32 const BONK_STANDIN_READY_MS    = 2500;
    uint32 const BONK_STANDIN_MIN_IDLE    = 16;
    uint32 const BONK_STANDIN_BATCH       = 8;
    uint32 const BONK_STANDIN_MAX         = 160;
    uint32 const BONK_STANDIN_GATHER_MS   = 1000;
    float const BONK_STANDIN_GATHER_RANGE = 15.0f;
    uint32 const BONK_STANDIN_LIFETIME_MS = 60 * MINUTE * IN_MILLISECONDS;
    float const BONK_STANDIN_CATCH_UP_SPEED = 200.0f;

    uint32 const BONK_TELEGRAPH_TURN_MS = 500;
    float const BONK_TELEGRAPH_MAX_RISE = 1.0f;
    float const BONK_TELEGRAPH_LIFT     = 0.05f;

    float const BONK_CONE_HALF_ANGLE = M_PI_F / 4.0f;
    float const BONK_RING_INNER      = 0.5f;
    float const BONK_TELEGRAPH_GROUND_ABOVE = 8.0f;
    float const BONK_TELEGRAPH_GROUND_SEARCH = 20.0f;

    char const* const BONK_TELEGRAPH_SHAPE_NAMES[MAX_BONK_TELEGRAPH_SHAPE] = { "circle", "cone", "line", "ring" };

    uint32 GetLinePieceCount(float length)
    {
        return std::min(BONK_LINE_MAX_PIECES, std::max(1u, uint32(length / BONK_LINE_PIECE_SIZE + 0.5f)));
    }

    uint32 GetSizedDisplay(BonkTelegraphShape shape, uint32 model, float size)
    {
        uint32 const shapeIndex = shape == BONK_TELEGRAPH_CIRCLE ? 0 : shape == BONK_TELEGRAPH_CONE ? 1 : 2;
        return BONK_STANDIN_FIRST_DISPLAY + (shapeIndex * (1 + BONK_TELEGRAPH_FILL_COUNT) + model) * BONK_STANDIN_SIZE_COUNT +
            GetBonkStandInSizeIndex(size);
    }

    float GetGround(Map* map, float x, float y, float z)
    {
        float const ground = map->GetHeight(x, y, z + BONK_TELEGRAPH_GROUND_ABOVE, true, BONK_TELEGRAPH_GROUND_SEARCH);
        return ground > INVALID_HEIGHT ? ground : z;
    }

    float GetGroundTop(Map* map, float x, float y, float z, float reach)
    {
        float const middle = GetGround(map, x, y, z);
        float top = middle;
        for (uint32 i = 0; i < 8; ++i)
        {
            float const angle = i * M_PI_F / 4.0f;
            top = std::max(top, GetGround(map, x + reach * std::cos(angle), y + reach * std::sin(angle), middle));
        }
        return std::min(top, middle + BONK_TELEGRAPH_MAX_RISE) + BONK_TELEGRAPH_LIFT;
    }

    void InstallStandInAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
        creature->SetDisplayId(BONK_STANDIN_INVISIBLE);
        creature->SetFactionTemplateId(35);
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        creature->SetSpeedRate(MOVE_RUN, BONK_STANDIN_CATCH_UP_SPEED);
    }

    void PlaceStandIn(Creature* creature, float x, float y, float z, float orientation)
    {
        creature->DisableSpline();
        creature->GetMap()->CreatureRelocation(creature, x, y, z, orientation);
        creature->SetFacingTo(orientation);
    }
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

uint32 GetBonkTelegraphLeadMs(BonkTelegraphShape shape)
{
    return shape == BONK_TELEGRAPH_LINE || shape == BONK_TELEGRAPH_CONE ? BONK_TELEGRAPH_TURN_MS : 0;
}

uint32 GetBonkStandInSizeCount()
{
    return BONK_STANDIN_SIZE_COUNT;
}

uint32 GetBonkStandInSizeIndex(float size)
{
    uint32 nearest = 0;
    for (uint32 i = 1; i < BONK_STANDIN_SIZE_COUNT; ++i)
        if (std::fabs(BONK_STANDIN_SIZES[i] - size) < std::fabs(BONK_STANDIN_SIZES[nearest] - size))
            nearest = i;
    return nearest;
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
            float const length = GetLinePieceCount(size) * BONK_LINE_PIECE_SIZE;
            float const along = dx * std::cos(orientation) + dy * std::sin(orientation);
            float const across = -dx * std::sin(orientation) + dy * std::cos(orientation);
            return along >= 0.0f && along <= length && std::fabs(across) <= BONK_LINE_PIECE_SIZE / 2.0f;
        }
        default:
            return distance <= size;
    }
}

BonkTelegraphs::Telegraph BonkTelegraphs::Lay(Map* map, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size,
    uint32 durationMs)
{
    Telegraph telegraph;
    telegraph.shape = shape;
    telegraph.orientation = orientation;
    uint32 const lead = GetBonkTelegraphLeadMs(shape);
    telegraph.showInMs = int32(lead);
    uint32 const seenMs = durationMs > lead ? durationMs - lead : 0;

    if (shape == BONK_TELEGRAPH_LINE)
    {
        uint32 const count = GetLinePieceCount(size);
        float const dirX = std::cos(orientation), dirY = std::sin(orientation);

        uint32 grow = 0;
        for (uint32 i = 0; i < BONK_LINE_FILL_PIECE_COUNT; ++i)
            if (BONK_LINE_FILL_PIECE_MS[i] * count <= seenMs)
                grow = i;
        telegraph.fillMs = BONK_LINE_FILL_PIECE_MS[grow] * count;

        for (uint32 i = 0; i < count; ++i)
        {
            Piece piece;
            float const along = (i + 0.5f) * BONK_LINE_PIECE_SIZE;
            piece.x = x + along * dirX;
            piece.y = y + along * dirY;
            piece.z = GetGroundTop(map, piece.x, piece.y, z, BONK_LINE_PIECE_SIZE / 2.0f);
            piece.fillX = x + i * BONK_LINE_PIECE_SIZE * dirX;
            piece.fillY = y + i * BONK_LINE_PIECE_SIZE * dirY;
            piece.outlineDisplay = BONK_STANDIN_LINE_DISPLAY + (count > 1 && i == 0 ? 1 : count > 1 && i + 1 == count ? 2 : 0);
            piece.fillDisplay = BONK_STANDIN_LINE_DISPLAY + 3 + grow * BONK_LINE_MAX_PIECES + i;
            telegraph.pieces.push_back(piece);
        }
    }
    else
    {
        uint32 fill = 0;
        for (uint32 i = 0; i < BONK_TELEGRAPH_FILL_COUNT; ++i)
            if (BONK_TELEGRAPH_FILL_MS[i] <= seenMs)
                fill = i;
        telegraph.fillMs = BONK_TELEGRAPH_FILL_MS[fill];

        Piece piece;
        piece.x = piece.fillX = x;
        piece.y = piece.fillY = y;
        piece.z = shape == BONK_TELEGRAPH_CONE ?
            GetGroundTop(map, x + size / 2.0f * std::cos(orientation), y + size / 2.0f * std::sin(orientation), z, size / 2.0f) :
            GetGroundTop(map, x, y, z, size * 0.8f);
        piece.outlineDisplay = GetSizedDisplay(shape, 0, size);
        piece.fillDisplay = GetSizedDisplay(shape, 1 + fill, size);
        telegraph.pieces.push_back(piece);
    }

    telegraph.fillInMs = int32(durationMs) - int32(telegraph.fillMs);
    return telegraph;
}

void BonkTelegraphs::Add(Player* owner, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
    std::function<void()> onFire)
{
    if (shape >= MAX_BONK_TELEGRAPH_SHAPE || size <= 0.0f || !owner->IsInWorld())
        return;

    Telegraph telegraph = Lay(owner->GetMap(), shape, x, y, z, orientation, size, durationMs);
    if (m_patient && CountIdle(true) < 2 * telegraph.pieces.size())
    {
        Request request;
        request.shape = shape;
        request.x = x;
        request.y = y;
        request.z = z;
        request.orientation = orientation;
        request.size = size;
        request.durationMs = durationMs;
        request.onFire = std::move(onFire);
        m_requests.push_back(std::move(request));
        m_warm = true;
        return;
    }

    telegraph.leftMs = int32(durationMs);
    telegraph.onFire = std::move(onFire);
    Reserve(owner, telegraph);
    if (telegraph.showInMs <= 0)
    {
        Show(owner, telegraph, false);
        if (telegraph.fillInMs <= 0)
            Show(owner, telegraph, true);
    }
    m_telegraphs.push_back(std::move(telegraph));
}

void BonkTelegraphs::Update(Player* owner, uint32 diff)
{
    if (!owner->IsInWorld() || owner->IsBeingTeleported())
        return;

    m_clockMs += diff;
    Tend(owner, diff);

    while (!m_requests.empty())
    {
        Request& request = m_requests.front();
        uint32 const pieces = request.shape == BONK_TELEGRAPH_LINE ? GetLinePieceCount(request.size) : 1;
        if (CountIdle(true) < 2 * pieces)
            break;
        Request ready = std::move(request);
        m_requests.erase(m_requests.begin());
        bool const patient = m_patient;
        m_patient = false;
        Add(owner, ready.shape, ready.x, ready.y, ready.z, ready.orientation, ready.size, ready.durationMs, std::move(ready.onFire));
        m_patient = patient;
    }

    if (m_telegraphs.empty())
        return;

    std::vector<Telegraph> due;
    for (auto itr = m_telegraphs.begin(); itr != m_telegraphs.end();)
    {
        itr->leftMs -= int32(diff);
        itr->showInMs -= int32(diff);
        itr->fillInMs -= int32(diff);
        if (itr->leftMs > 0)
        {
            if (!itr->shown && itr->showInMs <= 0)
                Show(owner, *itr, false);
            if (itr->shown && !itr->filled && itr->fillInMs <= 0)
                Show(owner, *itr, true);
        }
        if (itr->leftMs > 0)
        {
            ++itr;
            continue;
        }
        due.push_back(std::move(*itr));
        itr = m_telegraphs.erase(itr);
    }

    for (Telegraph& telegraph : due)
    {
        TakeOff(owner, telegraph);
        if (telegraph.onFire)
            telegraph.onFire();
    }
}

void BonkTelegraphs::Clear(Player* owner)
{
    if (owner->FindMap())
        for (Telegraph const& telegraph : m_telegraphs)
            TakeOff(owner, telegraph);
    m_telegraphs.clear();
    m_requests.clear();
}

void BonkTelegraphs::Dismiss(Player* owner)
{
    Clear(owner);
    if (Map* map = owner->FindMap())
        for (StandIn const& standIn : m_standIns)
            if (Creature* creature = map->GetCreature(standIn.guid))
                creature->DespawnOrUnsummon();
    m_standIns.clear();
    m_warm = false;
}

void BonkTelegraphs::Reserve(Player* owner, Telegraph& telegraph)
{
    bool const turn = telegraph.shape == BONK_TELEGRAPH_LINE || telegraph.shape == BONK_TELEGRAPH_CONE;
    for (Piece const& piece : telegraph.pieces)
    {
        telegraph.outlines.push_back(Move(owner, piece.x, piece.y, piece.z, telegraph.orientation, turn));
        telegraph.fills.push_back(Move(owner, piece.fillX, piece.fillY, piece.z, telegraph.orientation, turn));
    }
}

void BonkTelegraphs::Show(Player* owner, Telegraph& telegraph, bool fill)
{
    for (size_t i = 0; i < telegraph.pieces.size(); ++i)
    {
        if (fill)
            Wear(owner, telegraph.fills[i], telegraph.pieces[i].fillDisplay);
        else
            Wear(owner, telegraph.outlines[i], telegraph.pieces[i].outlineDisplay);
    }
    if (fill)
        telegraph.filled = true;
    else
        telegraph.shown = true;
}

void BonkTelegraphs::TakeOff(Player* owner, Telegraph const& telegraph)
{
    for (ObjectGuid const& guid : telegraph.outlines)
        Undress(owner, guid);
    for (ObjectGuid const& guid : telegraph.fills)
        Undress(owner, guid);
}

ObjectGuid BonkTelegraphs::Move(Player* owner, float x, float y, float z, float orientation, bool turn)
{
    Map* map = owner->GetMap();
    StandIn* chosen = nullptr;
    Creature* chosenCreature = nullptr;
    float chosenDistance = 0.0f;
    for (StandIn& standIn : m_standIns)
    {
        if (standIn.busy || standIn.readyAtMs > m_clockMs)
            continue;
        Creature* creature = map->GetCreature(standIn.guid);
        if (!creature)
            continue;
        float const distance = creature->GetDistance2d(x, y);
        if (!chosen || distance < chosenDistance)
        {
            chosen = &standIn;
            chosenCreature = creature;
            chosenDistance = distance;
        }
    }
    if (chosen)
    {
        PlaceStandIn(chosenCreature, x, y, z, turn ? orientation : chosenCreature->GetOrientation());
        chosen->busy = true;
        return chosen->guid;
    }

    Creature* creature = owner->SummonCreature(BONK_PICKUP_MARKER, x, y, z, orientation, TEMPSUMMON_TIMED_DESPAWN, BONK_STANDIN_LIFETIME_MS,
        false, 0, &InstallStandInAI);
    if (!creature)
        return ObjectGuid();
    StandIn standIn;
    standIn.guid = creature->GetObjectGuid();
    standIn.readyAtMs = m_clockMs + BONK_STANDIN_READY_MS;
    standIn.busy = true;
    m_standIns.push_back(standIn);
    return standIn.guid;
}

ObjectGuid BonkTelegraphs::Borrow(Player* owner, float x, float y, float z, uint32 display)
{
    if (!owner->IsInWorld() || !CountIdle(true))
        return ObjectGuid();
    ObjectGuid const guid = Move(owner, x, y, z, 0.0f, false);
    Wear(owner, guid, display);
    return guid;
}

ObjectGuid BonkTelegraphs::BorrowInvisible(Player* owner, float x, float y, float z)
{
    return Borrow(owner, x, y, z, BONK_STANDIN_INVISIBLE);
}

void BonkTelegraphs::Wear(Player* owner, ObjectGuid const& guid, uint32 display)
{
    if (!guid.IsEmpty())
        if (Creature* creature = owner->GetMap()->GetCreature(guid))
            creature->SetDisplayId(display);
}

void BonkTelegraphs::Undress(Player* owner, ObjectGuid const& guid)
{
    for (StandIn& standIn : m_standIns)
        if (standIn.guid == guid)
        {
            if (Map* map = owner->FindMap())
                if (Creature* creature = map->GetCreature(guid))
                    creature->SetDisplayId(BONK_STANDIN_INVISIBLE);
            standIn.busy = false;
            return;
        }
}

void BonkTelegraphs::Summon(Player* owner, uint32 count)
{
    for (uint32 i = 0; i < count; ++i)
        if (Creature* creature = owner->SummonCreature(BONK_PICKUP_MARKER, owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ(), 0.0f,
            TEMPSUMMON_TIMED_DESPAWN, BONK_STANDIN_LIFETIME_MS, false, 0, &InstallStandInAI))
        {
            StandIn standIn;
            standIn.guid = creature->GetObjectGuid();
            standIn.readyAtMs = m_clockMs + BONK_STANDIN_READY_MS;
            m_standIns.push_back(standIn);
        }
}

uint32 BonkTelegraphs::CountIdle(bool readyOnly) const
{
    uint32 count = 0;
    for (StandIn const& standIn : m_standIns)
        if (!standIn.busy && (!readyOnly || standIn.readyAtMs <= m_clockMs))
            ++count;
    return count;
}

void BonkTelegraphs::Tend(Player* owner, uint32 diff)
{
    Map* map = owner->GetMap();
    if (map->GetId() != m_mapId || map->GetInstanceId() != m_instanceId)
    {
        m_standIns.clear();
        m_mapId = map->GetId();
        m_instanceId = map->GetInstanceId();
    }
    m_standIns.erase(std::remove_if(m_standIns.begin(), m_standIns.end(),
        [map](StandIn const& standIn) { return !map->GetCreature(standIn.guid); }), m_standIns.end());

    if (m_warm && CountIdle(false) < BONK_STANDIN_MIN_IDLE && m_standIns.size() < BONK_STANDIN_MAX)
        Summon(owner, std::min<uint32>(BONK_STANDIN_BATCH, BONK_STANDIN_MAX - uint32(m_standIns.size())));

    m_gatherTimer += diff;
    if (m_gatherTimer < BONK_STANDIN_GATHER_MS)
        return;
    m_gatherTimer = 0;
    for (StandIn const& standIn : m_standIns)
        if (!standIn.busy)
            if (Creature* creature = map->GetCreature(standIn.guid))
                if (!creature->IsWithinDistInMap(owner, BONK_STANDIN_GATHER_RANGE))
                    PlaceStandIn(creature, owner->GetPositionX(), owner->GetPositionY(), owner->GetPositionZ(), creature->GetOrientation());
}
