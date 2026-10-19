#ifndef MANGOS_BONKMASTERY_H
#define MANGOS_BONKMASTERY_H

#include "Common.h"
#include "BonkMgr.h"
#include "Policies/Singleton.h"

#include <map>
#include <set>
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
    BONK_MASTERY_STARTER_UNLOCK = 5,
    BONK_MASTERY_SHATTER        = 6,
    BONK_MASTERY_OPENER         = 7,
    BONK_MASTERY_EXECUTE        = 8,
    BONK_MASTERY_CLEARCASTING   = 9,
    BONK_MASTERY_SOUL_SIPHON    = 10,
    BONK_MASTERY_SPELL_SLOW     = 11,
    BONK_MASTERY_SPEC_DAMAGE    = 12,
    BONK_MASTERY_KEYWORD_DAMAGE = 13,
    BONK_MASTERY_CRIT_VS_KEYWORD = 14,
    BONK_MASTERY_USES_BONUS     = 15,
    BONK_MASTERY_SPELL_APPLIES  = 16,
    BONK_MASTERY_SPELL_HEAL     = 17,
    BONK_MASTERY_RESET_ON_KILL  = 18,
    BONK_MASTERY_EXECUTE_THRESHOLD = 19,
    BONK_MASTERY_SPELL_STUN     = 20,
    BONK_MASTERY_STRUCK_CAST    = 21,
    BONK_MASTERY_ENRAGE_DURATION = 22,
    BONK_MASTERY_ENRAGE_HASTE   = 23,
    BONK_MASTERY_ENRAGE_DAMAGE  = 24,
    BONK_MASTERY_SHIELD_BLOCK   = 25,
    BONK_MASTERY_DASH_RECHARGE  = 26,
    BONK_MASTERY_CHILL_DURATION = 27,
    BONK_MASTERY_SPREAD_TARGETS = 28,
    BONK_MASTERY_MARK_BONUS     = 29,
    BONK_MASTERY_SUMMON_CRIT    = 30,
    BONK_MASTERY_PACK_POWER     = 31,
    BONK_MASTERY_FRENZY_DAMAGE  = 32,
    BONK_MASTERY_LAST_STAND     = 33,
    BONK_MASTERY_ICE_BLOCK      = 34,
    BONK_MASTERY_BLINK_SHIELD   = 35,
    BONK_MASTERY_ENTRAPMENT     = 36,
    BONK_MASTERY_TRAP_DURATION  = 37,
    BONK_MASTERY_CAPSTONE       = 38,
    MAX_BONK_MASTERY_EFFECT
};

enum BonkCapstone : uint8
{
    BONK_CAPSTONE_NONE          = 0,
    BONK_CAPSTONE_BESTIAL_WRATH = 1,
    BONK_CAPSTONE_TRUESHOT      = 2,
    BONK_CAPSTONE_TRAPMASTER    = 3,
    BONK_CAPSTONE_WEAPON_MASTER = 4,
    BONK_CAPSTONE_BERSERKER     = 5,
    BONK_CAPSTONE_JUGGERNAUT    = 6,
    BONK_CAPSTONE_ARCANE_SURGE  = 7,
    BONK_CAPSTONE_GLASS_CANNON  = 8,
    BONK_CAPSTONE_SHATTERLORD   = 9,
    BONK_CAPSTONE_ASCENDANCE    = 10,
    BONK_CAPSTONE_DOOMHAMMER    = 11,
    BONK_CAPSTONE_TOTEMIC_MASTERY = 12,
    BONK_CAPSTONE_ARCHANGEL     = 13,
    BONK_CAPSTONE_PAIN_SUPPRESSION = 14,
    BONK_CAPSTONE_SHADOWFORM    = 15,
    BONK_CAPSTONE_MASTER_POISONER = 16,
    BONK_CAPSTONE_BLADE_DANCER  = 17,
    BONK_CAPSTONE_MASTER_OF_SUBTLETY = 18,
    MAX_BONK_CAPSTONE
};

uint32 const BONK_MASTERY_MAX_LEVEL = 30;
uint32 const BONK_MASTERY_XP_BASE   = 60;
uint32 const BONK_MASTERY_XP_STEP   = 6;
uint32 const BONK_MASTERY_POINTS_PER_ROW = 5;

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
    uint32 param = 0;
    float valuePerRank = 0.0f;
};

struct BonkMasteryApply
{
    BonkKeyword keyword = BONK_KEYWORD_NONE;
    uint8 stacks = 0;
};

struct BonkMasteryBonuses
{
    float passives[MAX_BONK_PASSIVE_EFFECT] = {};
    std::map<uint32, float> spellDamage;
    std::map<uint32, float> spellCooldown;
    std::map<uint32, uint32> spellTargets;
    std::vector<std::pair<uint32, float>> schoolDamage;
    float shatter = 0.0f;
    std::vector<std::pair<uint32, float>> opener;
    std::vector<std::pair<uint32, float>> execute;
    float clearcasting = 0.0f;
    std::vector<std::pair<uint32, float>> soulSiphon;
    std::map<uint32, float> spellSlow;
    std::map<uint32, float> specDamage;
    std::map<uint32, float> keywordDamage;
    std::map<uint32, float> critVsKeyword;
    std::map<uint32, float> usesBonus;
    std::map<uint32, BonkMasteryApply> spellApplies;
    std::map<uint32, float> spellHeal;
    std::set<uint32> resetOnKill;
    std::map<uint32, float> executeThreshold;
    std::map<uint32, float> spellStun;
    std::map<uint32, float> struckCast;
    std::map<uint32, uint32> spreadTargets;
    std::set<uint32> entrapment;
    float enrageDuration = 0.0f;
    float enrageHaste = 0.0f;
    float enrageDamage = 0.0f;
    float shieldBlock = 0.0f;
    float dashRecharge = 0.0f;
    float chillDuration = 0.0f;
    float markBonus = 0.0f;
    float summonCrit = 0.0f;
    float packPower = 0.0f;
    float frenzyDamage = 0.0f;
    float lastStand = 0.0f;
    float iceBlock = 0.0f;
    float blinkShield = 0.0f;
    float trapDuration = 0.0f;
    BonkCapstone capstone = BONK_CAPSTONE_NONE;

    bool Has(BonkCapstone which) const { return capstone == which; }
};

class BonkMastery
{
    public:
        void Load();

        std::vector<BonkMasteryTree const*> GetTrees(uint8 classId) const;
        BonkMasteryNode const* GetNode(uint32 id) const;
        BonkMasteryTree const* GetTree(uint32 id) const;
        BonkMasteryNode const* FindStarterUnlock(uint8 classId, uint32 weaponId) const;
        std::map<uint32, BonkMasteryNode> const& GetNodes() const { return m_nodes; }
        bool HasTrees(uint8 classId) const;

        static uint32 GetLevel(uint32 xp);
        static uint32 GetXpIntoLevel(uint32 xp);
        static uint32 GetXpForLevel(uint32 level);

        BonkMasteryBonuses Evaluate(std::map<uint32, uint8> const& ranks) const;

        static uint32 GetRootWeapon(uint32 weaponId);
        static uint32 GetSpec(uint32 weaponId);

    private:
        static void ApplyCapstoneCost(BonkMasteryBonuses& bonuses);

        std::map<uint32, BonkMasteryTree> m_trees;
        std::map<uint32, BonkMasteryNode> m_nodes;
};

#define sBonkMastery MaNGOS::Singleton<BonkMastery>::Instance()

#endif
