#ifndef BONK_TELEGRAPH_H
#define BONK_TELEGRAPH_H

#include "Common.h"
#include "ObjectGuid.h"

#include <functional>
#include <string>
#include <vector>

class Map;
class Player;

enum BonkTelegraphShape : uint8
{
    BONK_TELEGRAPH_CIRCLE = 0,
    BONK_TELEGRAPH_CONE   = 1,
    BONK_TELEGRAPH_LINE   = 2,
    MAX_BONK_TELEGRAPH_SHAPE
};

char const* GetBonkTelegraphShapeName(BonkTelegraphShape shape);
bool ParseBonkTelegraphShape(std::string const& name, BonkTelegraphShape& shape);

bool IsInBonkTelegraph(BonkTelegraphShape shape, float originX, float originY, float orientation, float size, float x, float y);

uint32 GetBonkTelegraphLeadMs(BonkTelegraphShape shape);

uint32 GetBonkStandInSizeCount();
uint32 GetBonkStandInSizeIndex(float size);

class BonkTelegraphs
{
    public:
        void Add(Player* owner, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
            std::function<void()> onFire = nullptr, bool lingers = false, ObjectGuid const& caster = ObjectGuid(), uint32 windId = 0);
        void Update(Player* owner, uint32 diff);
        void Clear(Player* owner);
        void CancelFrom(Player* owner, ObjectGuid const& caster, uint32 windId = 0);
        void Dismiss(Player* owner);
        void Warm() { m_warm = true; }
        void SetPatient(bool patient) { m_patient = patient; }
        size_t Count() const { return m_telegraphs.size(); }
        size_t CountStandIns() const { return m_standIns.size(); }
        size_t CountBusyStandIns() const { return m_standIns.size() - CountIdle(false); }

        ObjectGuid Borrow(Player* owner, float x, float y, float z, uint32 display);
        ObjectGuid BorrowInvisible(Player* owner, float x, float y, float z);
        void Dress(Player* owner, ObjectGuid const& guid, uint32 display) { Wear(owner, guid, display); }
        void Release(Player* owner, ObjectGuid const& guid) { Undress(owner, guid); }
        void Resize(Player* owner, ObjectGuid const& guid, float stretch);

    private:
        struct Piece
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float fillX = 0.0f, fillY = 0.0f;
            uint32 outlineDisplay = 0;
            uint32 fillDisplay = 0;
            float stretch = 1.0f;
        };

        struct Telegraph
        {
            BonkTelegraphShape shape = BONK_TELEGRAPH_CIRCLE;
            float orientation = 0.0f;
            std::vector<Piece> pieces;
            std::vector<ObjectGuid> outlines;
            std::vector<ObjectGuid> fills;
            bool shown = false;
            bool filled = false;
            uint32 fillMs = 0;
            int32 showInMs = 0;
            int32 fillInMs = 0;
            int32 leftMs = 0;
            std::function<void()> onFire;
            ObjectGuid caster;
            uint32 windId = 0;
        };

        struct StandIn
        {
            ObjectGuid guid;
            uint32 readyAtMs = 0;
            bool busy = false;
            float baseScale = 1.0f;
        };

        struct Request
        {
            BonkTelegraphShape shape = BONK_TELEGRAPH_CIRCLE;
            float x = 0.0f, y = 0.0f, z = 0.0f, orientation = 0.0f, size = 0.0f;
            uint32 durationMs = 0;
            std::function<void()> onFire;
            bool lingers = false;
            ObjectGuid caster;
            uint32 windId = 0;
        };

        static Telegraph Lay(Map* map, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs);
        void Reserve(Player* owner, Telegraph& telegraph);
        void Show(Player* owner, Telegraph& telegraph, bool fill);
        void TakeOff(Player* owner, Telegraph const& telegraph);
        ObjectGuid Move(Player* owner, float x, float y, float z, float orientation, bool turn);
        void Wear(Player* owner, ObjectGuid const& guid, uint32 display, float stretch = 1.0f);
        void Undress(Player* owner, ObjectGuid const& guid);
        void Summon(Player* owner, uint32 count);
        uint32 CountIdle(bool readyOnly) const;
        void Tend(Player* owner, uint32 diff);

        std::vector<Telegraph> m_telegraphs;
        std::vector<StandIn> m_standIns;
        std::vector<Request> m_requests;
        uint32 m_clockMs = 0;
        uint32 m_gatherTimer = 0;
        uint32 m_mapId = 0;
        uint32 m_instanceId = 0;
        bool m_warm = false;
        bool m_patient = false;
};

#endif
