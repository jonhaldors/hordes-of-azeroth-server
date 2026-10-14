/*
 * Vanilla Bonk: ground telegraphs, red warnings that fill before an enemy
 * ability goes off.
 *
 * A warning is worn by stand-ins: creatures that wait unseen by the hero and
 * put on a telegraph's model when one is needed (tools\telegraphs,
 * StandIns.cs). The client fades every new object in over two seconds and out
 * as it goes, but a creature that has been there a while changes its look at
 * once, so a warning shows the moment it is placed and goes the moment it
 * fires. A piece of a warning is two stand-ins on one spot: one wears its
 * outline, the other its fill, put on late enough to be full when the ability
 * fires (fills come in a few lengths, BONK_TELEGRAPH_FILL_MS). A circle, cone
 * or ring is one piece, in the nearest of a set of sizes (the client eases a
 * change of scale over two seconds, so every size is a look of its own). A
 * line is a row of 3-yard square pieces and its fill sweeps along it from the
 * start, piece after piece.
 *
 * Stand-ins are moved to their spot unseen as the warning is placed. The
 * client turns a creature to a new facing over a moment, and restarts its
 * animation (so a fill) as it does, so a line or cone shows only once its
 * stand-ins have turned (GetBonkTelegraphLeadMs); a circle or ring keeps the
 * facing its stand-ins have. The client draws the models flat, depth-tested,
 * so each piece sits at the highest ground under it rather than in a bump.
 */

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
    BONK_TELEGRAPH_CIRCLE = 0,      // size: radius
    BONK_TELEGRAPH_CONE   = 1,      // size: length; 90 degrees wide, along the orientation
    BONK_TELEGRAPH_LINE   = 2,      // size: length (whole 3-yard pieces), along the orientation; 3 yards wide
    BONK_TELEGRAPH_RING   = 3,      // size: outer radius; safe inside half of it
    MAX_BONK_TELEGRAPH_SHAPE
};

char const* GetBonkTelegraphShapeName(BonkTelegraphShape shape);
bool ParseBonkTelegraphShape(std::string const& name, BonkTelegraphShape& shape);

// Whether a spot is inside a telegraph's shape, as it is drawn.
bool IsInBonkTelegraph(BonkTelegraphShape shape, float originX, float originY, float orientation, float size, float x, float y);

// How long after it is placed a warning of this shape shows (its stand-ins
// turn unseen first); it still fires when its time is up, so an ability that
// wants the whole warning seen adds this to it.
uint32 GetBonkTelegraphLeadMs(BonkTelegraphShape shape);

class BonkTelegraphs
{
    public:
        // Warns for `durationMs`, then calls `onFire` (if any) and takes the
        // warning away. `z` is a guess; the ground's height is used.
        void Add(Player* owner, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
            std::function<void()> onFire = nullptr);
        void Update(Player* owner, uint32 diff);
        // Takes every warning away without firing it; the stand-ins stay.
        void Clear(Player* owner);
        // Takes every warning away and sends the stand-ins off (a stage or the
        // run ending, the hero leaving).
        void Dismiss(Player* owner);
        // Keeps stand-ins by the owner from now on; each takes a moment to be
        // ready (its own fade-in), so ask for them early.
        void Warm() { m_warm = true; }
        // A warning asked for before enough stand-ins are ready waits for
        // them, rather than showing on new ones that fade in (GM previews).
        void SetPatient(bool patient) { m_patient = patient; }
        size_t Count() const { return m_telegraphs.size(); }

    private:
        // One spot of a warning: its outline and its fill, worn by a stand-in
        // each. A line's fill grows from its piece's start edge, so sits there.
        struct Piece
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float fillX = 0.0f, fillY = 0.0f;
            uint32 outlineDisplay = 0;
            uint32 fillDisplay = 0;
        };

        struct Telegraph
        {
            BonkTelegraphShape shape = BONK_TELEGRAPH_CIRCLE;
            float orientation = 0.0f;
            std::vector<Piece> pieces;
            std::vector<ObjectGuid> outlines;   // a stand-in per piece
            std::vector<ObjectGuid> fills;
            bool shown = false;
            bool filled = false;
            uint32 fillMs = 0;          // from the fills' going on to full
            int32 showInMs = 0;         // the outlines go on when this runs out
            int32 fillInMs = 0;         // the fills go on when this runs out, once shown
            int32 leftMs = 0;           // fires when this runs out
            std::function<void()> onFire;
        };

        struct StandIn
        {
            ObjectGuid guid;
            uint32 readyAtMs = 0;       // on m_clockMs; before then it is still fading in
            bool busy = false;
        };

        // A warning waiting for stand-ins (SetPatient).
        struct Request
        {
            BonkTelegraphShape shape = BONK_TELEGRAPH_CIRCLE;
            float x = 0.0f, y = 0.0f, z = 0.0f, orientation = 0.0f, size = 0.0f;
            uint32 durationMs = 0;
            std::function<void()> onFire;
        };

        // The shape's pieces on the ground and the fills that fit a warning
        // this long: their looks and how late they go on.
        static Telegraph Lay(Map* map, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs);
        // Moves a stand-in for every piece's outline and fill to its spot.
        void Reserve(Player* owner, Telegraph& telegraph);
        // Every piece's outline, or every piece's fill, on.
        void Show(Player* owner, Telegraph& telegraph, bool fill);
        void TakeOff(Player* owner, Telegraph const& telegraph);
        // A stand-in moved to a spot, still unseen, turned to `orientation` if
        // `turn` (else keeping its facing): a ready one if there is one, else
        // a new one (which fades in).
        ObjectGuid Move(Player* owner, float x, float y, float z, float orientation, bool turn);
        void Wear(Player* owner, ObjectGuid const& guid, uint32 display);
        void Undress(Player* owner, ObjectGuid const& guid);
        void Summon(Player* owner, uint32 count);
        // Stand-ins not in use; or only those ready.
        uint32 CountIdle(bool readyOnly) const;
        // Forgets stand-ins gone or left on another map, keeps enough ready,
        // and brings the idle ones along with the owner.
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
