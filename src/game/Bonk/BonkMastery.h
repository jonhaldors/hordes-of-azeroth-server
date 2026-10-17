#ifndef MANGOS_BONKMASTERY_H
#define MANGOS_BONKMASTERY_H

#include "Common.h"
#include "BonkMgr.h"
#include "Policies/Singleton.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

enum BonkMasteryEffect : uint8
{
    BONK_MASTERY_PASSIVE        = 0,
    BONK_MASTERY_SPELL_DAMAGE   = 1,
    BONK_MASTERY_SCHOOL_DAMAGE  = 2,
    BONK_MASTERY_SPELL_COOLDOWN = 3,
    BONK_MASTERY_SPELL_TARGETS  = 4,
    BONK_MASTERY_START_SPELL    = 5,
    BONK_MASTERY_SHATTER        = 6,
    BONK_MASTERY_OPENER         = 7,
    BONK_MASTERY_EXECUTE        = 8,
    BONK_MASTERY_CLEARCASTING   = 9,
    BONK_MASTERY_SOUL_SIPHON    = 10,
    BONK_MASTERY_SPELL_SLOW     = 11,
    MAX_BONK_MASTERY_EFFECT
};

uint32 const BONK_MASTERY_MAX_LEVEL = 20;
uint32 const BONK_MASTERY_XP_BASE   = 100;
uint32 const BONK_MASTERY_XP_STEP   = 20;
uint32 const BONK_MASTERY_POINTS_PER_ROW = 3;

struct BonkMasteryTree
{
    uint32 id = 0;
    uint8 classId = 0;
    std::string name;
    std::string background;
    uint8 order = 0;
};

struct BonkMasteryNode
{
    uint32 id = 0;
    uint8 classId = 0;
    uint32 tree = 0;
    uint8 row = 1;
    uint8 column = 1;
    std::string icon;
    std::string name;
    std::string description;
    uint8 maxRank = 1;
    uint32 requiredId = 0;
    uint8 requiredRank = 0;
    BonkMasteryEffect effect = BONK_MASTERY_PASSIVE;
    uint32 target = 0;
    float valuePerRank = 0.0f;
};

struct BonkMasteryBonuses
{
    float passives[MAX_BONK_PASSIVE_EFFECT] = {};
    std::map<uint32, float> spellDamage;
    std::map<uint32, float> spellCooldown;
    std::map<uint32, uint32> spellTargets;
    std::vector<std::pair<uint32, float>> schoolDamage;
    std::vector<uint32> startSpells;
    float shatter = 0.0f;
    std::vector<std::pair<uint32, float>> opener;
    std::vector<std::pair<uint32, float>> execute;
    float clearcasting = 0.0f;
    std::vector<std::pair<uint32, float>> soulSiphon;
    std::map<uint32, float> spellSlow;
};

class BonkMastery
{
    public:
        void Load();

        std::vector<BonkMasteryTree const*> GetTrees(uint8 classId) const;
        BonkMasteryNode const* GetNode(uint32 id) const;
        std::map<uint32, BonkMasteryNode> const& GetNodes() const { return m_nodes; }
        bool HasTrees(uint8 classId) const;

        static uint32 GetLevel(uint32 xp);
        static uint32 GetXpIntoLevel(uint32 xp);
        static uint32 GetXpForLevel(uint32 level);

        BonkMasteryBonuses Evaluate(std::map<uint32, uint8> const& ranks) const;

        static uint32 GetRootWeapon(uint32 weaponId);

    private:
        std::map<uint32, BonkMasteryTree> m_trees;
        std::map<uint32, BonkMasteryNode> m_nodes;
};

#define sBonkMastery MaNGOS::Singleton<BonkMastery>::Instance()

#endif
