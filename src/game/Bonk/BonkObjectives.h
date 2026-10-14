/*
 * Vanilla Bonk: objective markers, the places in a stage worth walking to
 * (shrines, the wandering vendor, mini-events, extraction points).
 *
 * Each stands as a tall pillar of light in its kind's colour, as big as
 * Arathi Basin's banner auras and seen from far off (gameobject templates
 * BONK_GO_OBJECTIVE + kind, so their looks are world data), and the addon
 * pins it on the minimap and the world map:
 * "objective~id~kind~x~y~mapId~label~seconds" when one appears (seconds: how
 * long until it leaves, 0 = it stays), "objectiveclear~id" when it goes and
 * "objectiveclear" when the stage ends.
 */

#ifndef BONK_OBJECTIVES_H
#define BONK_OBJECTIVES_H

#include "Common.h"
#include "ObjectGuid.h"

#include <string>
#include <vector>

class Player;

enum BonkObjectiveKind : uint8
{
    BONK_OBJECTIVE_SHRINE  = 0,
    BONK_OBJECTIVE_VENDOR  = 1,
    BONK_OBJECTIVE_EVENT   = 2,
    BONK_OBJECTIVE_EXTRACT = 3,
    MAX_BONK_OBJECTIVE_KIND
};

// The addon's name for a kind ("shrine"), and back.
char const* GetBonkObjectiveKindName(BonkObjectiveKind kind);
bool ParseBonkObjectiveKind(char const* name, BonkObjectiveKind& kind);

struct BonkObjective
{
    uint32 id = 0;
    BonkObjectiveKind kind = BONK_OBJECTIVE_SHRINE;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::string label;
    uint32 leftMs = 0;          // until it leaves; 0 = it stays
    ObjectGuid pillar;
};

class BonkObjectives
{
    public:
        // Marks a spot; returns the objective's id.
        uint32 Add(Player* hero, BonkObjectiveKind kind, float x, float y, float z, std::string const& label, uint32 seconds);
        // Takes one away (gone from the world, the map and the minimap).
        void Remove(Player* hero, uint32 id);
        // Takes them all away, at the end of a stage.
        void Clear(Player* hero);
        // Counts down the ones that leave, and takes them away when they do;
        // returns those that left.
        std::vector<BonkObjective> Update(Player* hero, uint32 diff);
        // Tells the addon about every one again (after a reload).
        void Resend(Player* hero) const;

        BonkObjective const* Get(uint32 id) const;
        std::vector<BonkObjective> const& GetAll() const { return m_objectives; }

    private:
        void Send(Player* hero, BonkObjective const& objective) const;
        void Despawn(Player* hero, BonkObjective const& objective) const;

        std::vector<BonkObjective> m_objectives;
        uint32 m_nextId = 1;
};

#endif
