/*
 * Vanilla Bonk: Class Mastery.
 *
 * Each class levels its own Mastery by being played: minutes survived in a
 * run, mini-bosses and stage bosses killed (bonk_tier sets how much each is
 * worth). Every Mastery level and every finished class quest gives a
 * Mastery Point for that class's trees (bonk_mastery_tree, three per class
 * after its WoW talent specs), spent at the Mastery trainer at camp. What the
 * points buy (bonk_mastery) makes that class's runs stronger; respecs are
 * free. Progress is per account and class (bonk_account_mastery,
 * bonk_account_mastery_node in the characters database).
 */

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
    BONK_MASTERY_PASSIVE        = 0,    // target: BonkPassiveEffect; adds to it like a camp upgrade
    BONK_MASTERY_SPELL_DAMAGE   = 1,    // target: weapon id (its evolutions too); + damage, %
    BONK_MASTERY_SCHOOL_DAMAGE  = 2,    // target: spell school mask; + damage, %
    BONK_MASTERY_SPELL_COOLDOWN = 3,    // target: weapon id; - cooldown, %
    BONK_MASTERY_SPELL_TARGETS  = 4,    // target: weapon id; + targets
    BONK_MASTERY_START_SPELL    = 5,    // target: weapon id; every run starts with it
    BONK_MASTERY_SHATTER        = 6,    // + damage to slowed or rooted enemies, %
    BONK_MASTERY_OPENER         = 7,    // target: school mask, 0 = any; + damage to enemies above 80% health, %
    BONK_MASTERY_EXECUTE        = 8,    // target: school mask, 0 = any; + damage to enemies below 30% health, %
    BONK_MASTERY_CLEARCASTING   = 9,    // chance a spell fires again at once, %
    BONK_MASTERY_SOUL_SIPHON    = 10,   // target: school mask; a kill with it heals this % of max health
    MAX_BONK_MASTERY_EFFECT
};

// Mastery levels: every level is one Mastery Point. Level `n` takes
// BASE + STEP * (n - 1) experience.
uint32 const BONK_MASTERY_MAX_LEVEL = 20;
uint32 const BONK_MASTERY_XP_BASE   = 100;
uint32 const BONK_MASTERY_XP_STEP   = 20;
// Like WoW talents: a tree's row `r` opens once (r - 1) times this many
// points are spent in that tree.
uint32 const BONK_MASTERY_POINTS_PER_ROW = 3;

struct BonkMasteryTree
{
    uint32 id = 0;
    uint8 classId = 0;
    std::string name;
    std::string background;         // Interface\TalentFrame art, e.g. "MageFrost"
    uint8 order = 0;                // left to right
};

struct BonkMasteryNode
{
    uint32 id = 0;
    uint8 classId = 0;
    uint32 tree = 0;                // bonk_mastery_tree id
    uint8 row = 1;
    uint8 column = 1;
    std::string icon;
    std::string name;
    std::string description;        // what one rank does
    uint8 maxRank = 1;
    uint32 requiredId = 0;          // another node of the same class
    uint8 requiredRank = 0;
    BonkMasteryEffect effect = BONK_MASTERY_PASSIVE;
    uint32 target = 0;
    float valuePerRank = 0.0f;
};

// What a hero's Mastery adds to a run. Spell bonuses are by root weapon id:
// an evolution counts as the weapon it grew from.
struct BonkMasteryBonuses
{
    float passives[MAX_BONK_PASSIVE_EFFECT] = {};
    std::map<uint32, float> spellDamage;
    std::map<uint32, float> spellCooldown;
    std::map<uint32, uint32> spellTargets;
    std::vector<std::pair<uint32, float>> schoolDamage;    // school mask, %
    std::vector<uint32> startSpells;
    float shatter = 0.0f;
    std::vector<std::pair<uint32, float>> opener;          // school mask (0 = any), %
    std::vector<std::pair<uint32, float>> execute;
    float clearcasting = 0.0f;
    std::vector<std::pair<uint32, float>> soulSiphon;      // school mask, % of max health
};

class BonkMastery
{
    public:
        void Load();

        std::vector<BonkMasteryTree const*> GetTrees(uint8 classId) const;
        BonkMasteryNode const* GetNode(uint32 id) const;
        std::map<uint32, BonkMasteryNode> const& GetNodes() const { return m_nodes; }
        bool HasTrees(uint8 classId) const;

        // Level reached with `xp`, and the experience of the level being worked on.
        static uint32 GetLevel(uint32 xp);
        static uint32 GetXpIntoLevel(uint32 xp);
        static uint32 GetXpForLevel(uint32 level);      // to go from level - 1 to level

        // What the ranks in `ranks` (node id -> rank) add up to.
        BonkMasteryBonuses Evaluate(std::map<uint32, uint8> const& ranks) const;

        // The weapon an evolution grew from, or the weapon itself.
        static uint32 GetRootWeapon(uint32 weaponId);

    private:
        std::map<uint32, BonkMasteryTree> m_trees;
        std::map<uint32, BonkMasteryNode> m_nodes;
};

#define sBonkMastery MaNGOS::Singleton<BonkMastery>::Instance()

#endif
