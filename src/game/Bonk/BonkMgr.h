#ifndef MANGOS_BONKMGR_H
#define MANGOS_BONKMGR_H

#include "Common.h"
#include "Policies/Singleton.h"
#include "SharedDefines.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class Map;
class Player;

WorldLocation const BONK_CAMP_LOCATION(1, 16226.2f, 16257.0f, 13.2022f, 1.65007f);

uint32 const BONK_GO_CHEST      = 900001;
uint32 const BONK_GO_CHEST_FREE = 900002;
uint32 const BONK_GO_PORTAL     = 900003;
uint32 const BONK_GO_VAULT      = 900041;
uint32 const BONK_GO_SHARD      = 900007;
uint32 const BONK_GO_HEAL       = 900008;

uint32 const BONK_ORB_SMALL = 900100;
uint32 const BONK_ORB_LARGE = 900101;
uint32 const BONK_ORB_HUGE  = 900102;
uint32 const BONK_ORB_GRAND = 900103;
uint32 const BONK_SANGUINE_POOL = 900110;
uint32 const BONK_PICKUP_MARKER = 900111;
uint32 const BONK_HEAL_PICKUP = 900116;
uint32 const BONK_PROJECTILE = 900113;
uint32 const BONK_SNAKE = 900117;
uint32 const BONK_SPECTRAL_BEAST = 900118;
uint32 const BONK_PLAGUE_BEAST = 900119;
uint32 const BONK_HOWL_BOAR = 900120;
uint32 const BONK_HOWL_BEAR = 900121;
uint32 const BONK_HOWL_WYVERN = 900122;

struct BonkProjectileInfo
{
    uint32 spellId = 0;
    uint32 displayId = 0;
    float speed = 20.0f;
    float radius = 1.0f;
    float scale = 1.0f;
};

enum BonkSpawnKind : uint8
{
    BONK_SPAWN_NORMAL   = 0,
    BONK_SPAWN_ELITE    = 1,
    BONK_SPAWN_BOSS     = 2,
    BONK_SPAWN_MINIBOSS = 3,
    MAX_BONK_SPAWN_KIND
};

enum BonkEventType : uint8
{
    BONK_EVENT_MINIBOSS = 0,
    BONK_EVENT_ELITES   = 1,
    BONK_EVENT_SWARM    = 2,
    BONK_EVENT_CHESTS   = 3,
    BONK_EVENT_RING     = 4,
    BONK_EVENT_RUSH     = 5,
    MAX_BONK_EVENT
};

struct BonkMapEvent
{
    uint32 second = 0;
    BonkEventType type = BONK_EVENT_MINIBOSS;
    uint32 count = 1;
};

enum BonkArchetype : uint8
{
    BONK_ARCHETYPE_SWARMER = 0,
    BONK_ARCHETYPE_GRUNT   = 1,
    BONK_ARCHETYPE_BRUISER = 2,
    BONK_ARCHETYPE_CASTER  = 3,
    BONK_ARCHETYPE_CHARGER = 4,
    MAX_BONK_ARCHETYPE
};

enum BonkOrb : uint8
{
    BONK_ORB_BLUE  = 0,
    BONK_ORB_GREEN = 1,
    BONK_ORB_RED   = 2,
    BONK_ORB_PURPLE = 3,
};

struct BonkArchetypeInfo
{
    std::string name;
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float experience = 1.0f;
    uint32 firstSecond = 0;
    uint32 firstSecondLater = 0;
    BonkOrb orb = BONK_ORB_BLUE;
};

enum BonkWaveBeat : uint8
{
    BONK_BEAT_BUILD    = 0,
    BONK_BEAT_LULL     = 1,
    BONK_BEAT_WALL     = 2,
    BONK_BEAT_MINIBOSS = 3,
    MAX_BONK_WAVE_BEAT
};

struct BonkWave
{
    uint32 startSecond = 0;
    std::string name;
    BonkWaveBeat beat = BONK_BEAT_BUILD;
    uint32 roster[MAX_BONK_ARCHETYPE] = {};
    float crowdStart = 0.0f;
    float crowdEnd = 0.0f;
    uint32 spawnPerTick = 0;
    uint32 maxBruisers = 0;
    uint32 eliteIntervalSeconds = 0;
    uint32 elites = 0;
    uint32 minibosses = 0;
    uint32 miniEvents = 0;
};

struct BonkScalingPoint
{
    float minute = 0.0f;
    float hitShare = 0.0f;
    float health = 1.0f;
};

struct BonkMapCreature
{
    uint32 entry = 0;
    BonkSpawnKind kind = BONK_SPAWN_NORMAL;
    BonkArchetype archetype = BONK_ARCHETYPE_GRUNT;
    uint32 minSecond = 0;
    uint32 maxSecond = 0;
    uint32 weight = 1;
    uint32 maxAlive = 0;
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float speed = 0.7f;
    float scale = 1.0f;
};

enum BonkMapType : uint8
{
    BONK_MAP_OPEN     = 0,
    BONK_MAP_ENCLOSED = 1,
    BONK_MAP_ARENA    = 2,
    MAX_BONK_MAP_TYPE
};

uint32 GetBonkMapRewardPercent(BonkMapType type);

struct BonkMap
{
    uint32 id = 0;
    std::string key;
    std::string name;
    std::string icon;
    uint32 tierId = 0;
    BonkMapType type = BONK_MAP_OPEN;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
    uint32 durationSeconds = 600;
    int32 levelOffset = 0;
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float healthGrowthPerMinute = 0.1f;
    float damageGrowthPerMinute = 0.0f;
    float spawnMinDistance = 38.0f;
    float spawnMaxDistance = 48.0f;
    uint32 minibossSecond = 0;
    uint32 goldPerKill = 10;
    uint32 stageGold = 5000;
    uint32 coinsPerKill = 1;
    uint32 powerupIntervalSeconds = 40;
    uint32 powerupMax = 3;

    std::vector<BonkMapCreature> creatures;
    std::vector<BonkMapEvent> events;
};

struct BonkMapStart
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

enum BonkPowerupEffect : uint8
{
    BONK_POWERUP_SPELL      = 0,
    BONK_POWERUP_MAGNET     = 1,
    BONK_POWERUP_HOLY_NOVA  = 2,
    BONK_POWERUP_FROST_NOVA = 3,
    BONK_POWERUP_VALOR      = 4,
    MAX_BONK_POWERUP_EFFECT
};

struct BonkPowerup
{
    uint32 entry = 0;
    uint32 spellId = 0;
    uint32 weight = 1;
    BonkPowerupEffect effect = BONK_POWERUP_SPELL;
    float value = 0.0f;
};

struct BonkTier
{
    uint32 id = 0;
    std::string key;
    std::string name;
    std::string description;
    std::string art;
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float stageHealthGrowth = 0.25f;
    float stageDamageGrowth = 0.25f;
    float stageHealthAccel = 0.05f;
    float stageDamageAccel = 0.05f;
    float minAliveMultiplier = 1.0f;
    uint32 maxAlive = 200;
    uint32 maxBruisers = 8;
    uint32 firstEliteSecond = 150;
    uint32 chestCost = 10;
    float chestCostGrowth = 0.5f;
    uint32 unlockTierId = 0;
    uint32 unlockStage = 0;
    float shardChanceNormal = 0.1f;
    float shardChanceElite = 12.0f;
    uint32 shardsMiniboss = 1;
    uint32 shardsBoss = 2;
    float eliteDodge = 0.0f;
    uint8 eliteAffixes = 1;
    uint32 chargeIntervalMs = 8000;
    uint32 masteryXpMinute = 10;
    uint32 masteryXpMiniboss = 15;
    uint32 masteryXpBoss = 40;
    uint32 stages = 0;
    bool endless = false;

    std::vector<uint32> mapIds;
};

enum BonkWeaponTarget : uint8
{
    BONK_TARGET_NEAREST   = 0,
    BONK_TARGET_SELF_AREA = 1,
    BONK_TARGET_RANDOM    = 2,
    BONK_TARGET_GROUND    = 3,
    BONK_TARGET_STRONGEST = 4,
    BONK_TARGET_FARTHEST  = 5,
    BONK_TARGET_CONE      = 6,
    BONK_TARGET_SELF      = 7,
    BONK_TARGET_LINE      = 8,
    MAX_BONK_TARGET
};

enum BonkWeaponFlags : uint32
{
    BONK_WEAPON_FLAG_RESET_ON_KILL    = 0x01,
    BONK_WEAPON_FLAG_ALWAYS_CRIT      = 0x02,
    BONK_WEAPON_FLAG_REFRESH_DOTS     = 0x04,
    BONK_WEAPON_FLAG_KNOCKBACK        = 0x08,
    BONK_WEAPON_FLAG_EXPLODE_ON_DEATH = 0x10,
    BONK_WEAPON_FLAG_KILL_WITH_ALT    = 0x20,
    BONK_WEAPON_FLAG_NORMAL_ONLY      = 0x40,
    BONK_WEAPON_FLAG_CONSUME          = 0x80,
    BONK_WEAPON_FLAG_CLUSTER_BOMBS    = 0x100,
    BONK_WEAPON_FLAG_SNAKES           = 0x200,
    BONK_WEAPON_FLAG_SHATTER          = 0x400,
    BONK_WEAPON_FLAG_STAMPEDE         = 0x800,
    BONK_WEAPON_FLAG_PLAGUE           = 0x1000,
    BONK_WEAPON_FLAG_HOWL_BEASTS      = 0x2000,
    BONK_WEAPON_FLAG_FEEDING_FRENZY   = 0x4000,
    BONK_WEAPON_FLAG_LOOK_ONLY        = 0x8000,
    BONK_WEAPON_FLAG_BLOODSPLATTER    = 0x10000,
    BONK_WEAPON_FLAG_HEMORRHAGE       = 0x20000,
    BONK_WEAPON_FLAG_BLOOD_SHIELD     = 0x40000,
    BONK_WEAPON_FLAG_BLOODBATH        = 0x80000,
    BONK_WEAPON_FLAG_CHAIN_EXECUTION  = 0x100000,
    BONK_WEAPON_FLAG_SWEEP            = 0x200000,
};

float const BONK_CLEAVE_RADIUS    = 6.0f;
float const BONK_SPREAD_RADIUS    = 10.0f;

enum BonkRarity : uint8
{
    BONK_RARITY_COMMON    = 0,
    BONK_RARITY_UNCOMMON  = 1,
    BONK_RARITY_RARE      = 2,
    BONK_RARITY_EPIC      = 3,
    BONK_RARITY_LEGENDARY = 4,
    MAX_BONK_RARITY
};

enum BonkRankStep : uint8
{
    BONK_STEP_NEW              = 0,
    BONK_STEP_RANK             = 1,
    BONK_STEP_EVOLVE           = 2,
    MAX_BONK_STEP
};

enum BonkSpellUpgrade : uint8
{
    BONK_SPELL_UPGRADE_NONE         = 0,
    BONK_SPELL_UPGRADE_CLEAVE       = 1,
    BONK_SPELL_UPGRADE_HIT_TWICE    = 2,
    BONK_SPELL_UPGRADE_HASTE        = 3,
    BONK_SPELL_UPGRADE_RETIRED      = 4,
    BONK_SPELL_UPGRADE_DURATION     = 5,
    BONK_SPELL_UPGRADE_MARK         = 6,
    BONK_SPELL_UPGRADE_PET_DAMAGE   = 7,
    BONK_SPELL_UPGRADE_PET_SIZE     = 8,
    BONK_SPELL_UPGRADE_STACKS       = 9,
    BONK_SPELL_UPGRADE_SLOWED       = 10,
    BONK_SPELL_UPGRADE_FRENZY       = 11,
    BONK_SPELL_UPGRADE_DEPLOY       = 12,
    BONK_SPELL_UPGRADE_TRAP_AREA    = 13,
    BONK_SPELL_UPGRADE_SHIELD_SIZE  = 14,
    BONK_SPELL_UPGRADE_TRIGGER_COOLDOWN = 15,
    MAX_BONK_SPELL_UPGRADE
};

float const BONK_SPELL_UPGRADE_CAP[MAX_BONK_SPELL_UPGRADE] = { 0.0f, 50.0f, 50.0f, 40.0f, 0.0f, 0.0f, 15.0f, 0.0f, 0.0f, 0.0f, 15.0f, 0.0f, 20.0f, 60.0f, 0.0f, 40.0f };
uint32 const BONK_SPELL_CLEAVE_TARGETS = 2;

struct BonkWeaponUpgrade
{
    BonkSpellUpgrade upgrade = BONK_SPELL_UPGRADE_NONE;
    float step = 0.0f;
};

float const BONK_RANK_DAMAGE[BONK_RARITY_LEGENDARY]   = { 10.0f, 18.0f, 15.0f, 15.0f };
uint8 const BONK_RANK_UPGRADES[BONK_RARITY_LEGENDARY] = { 0, 0, 1, 2 };
uint8 const BONK_MAX_RANK_UPGRADES                    = 2;
float const BONK_RANK_SPARE_DAMAGE                    = 10.0f;
uint32 const BONK_SPREAD_RANK                  = 8;
uint32 const BONK_EVOLVE_RANK                  = 10;
float const BONK_LEGENDARY_BASE_CHANCE         = 2.0f;
float const BONK_LEGENDARY_CHANCE_PER_RANK     = 1.0f;
float const BONK_LEGENDARY_MAX_RANK_CHANCE     = 15.0f;
float const BONK_LEGENDARY_CHANCE_PER_LUCK     = 0.1f;
float const BONK_RARITY_WEIGHT[BONK_RARITY_LEGENDARY] = { 55.0f, 30.0f, 11.0f, 4.0f };
float const BONK_RARITY_LUCK[BONK_RARITY_LEGENDARY]   = { 0.0f, 1.0f, 2.0f, 3.0f };

char const* GetBonkRarityName(BonkRarity rarity);

enum BonkWeaponBehavior : uint8
{
    BONK_BEHAVIOR_NONE          = 0,
    BONK_BEHAVIOR_CLEAVE        = 1,
    BONK_BEHAVIOR_SPREAD        = 2,
    BONK_BEHAVIOR_ECHO          = 3,
    BONK_BEHAVIOR_SHIELD        = 4,
    BONK_BEHAVIOR_SHIELD_PER_HIT = 5,
    BONK_BEHAVIOR_DELAYED       = 6,
    BONK_BEHAVIOR_ORB           = 7,
    BONK_BEHAVIOR_FREEZE        = 8,
    BONK_BEHAVIOR_WARD          = 9,
    BONK_BEHAVIOR_VULNERABLE    = 10,
    BONK_BEHAVIOR_EXECUTE       = 11,
    BONK_BEHAVIOR_SPIN          = 12,
    BONK_BEHAVIOR_LEECH         = 13,
    BONK_BEHAVIOR_MARK          = 14,
    BONK_BEHAVIOR_DEMORALIZE    = 15,
    BONK_BEHAVIOR_BUFF          = 16,
    BONK_BEHAVIOR_REVENGE       = 17,
    BONK_BEHAVIOR_BURST         = 18,
    BONK_BEHAVIOR_TRAP          = 19,
    BONK_BEHAVIOR_PET           = 20,
    BONK_BEHAVIOR_WRATH         = 21,
    BONK_BEHAVIOR_RICOCHET      = 22,
    BONK_BEHAVIOR_TRICK_SHOTS   = 23,
    BONK_BEHAVIOR_CHAIN_DETONATION = 24,
    BONK_BEHAVIOR_STEADY_AIM    = 25,
    BONK_BEHAVIOR_ARROW_STORM   = 26,
    BONK_BEHAVIOR_GRAVITY_PULL  = 27,
    BONK_BEHAVIOR_FLARE         = 28,
    BONK_BEHAVIOR_SHARD_BURST   = 29,
    BONK_BEHAVIOR_VENOM_BOLTS   = 30,
    BONK_BEHAVIOR_VENOM_CLOUD   = 31,
    BONK_BEHAVIOR_ICE_PATH      = 32,
    BONK_BEHAVIOR_DEEP_WOUNDS   = 33,
    BONK_BEHAVIOR_BLOODTHIRST   = 34,
    BONK_BEHAVIOR_WHIRLWIND     = 35,
    BONK_BEHAVIOR_SHIELD_SLAM   = 36,
    BONK_BEHAVIOR_WEAKEN        = 37,
    BONK_BEHAVIOR_WAKE_OF_BLADES = 38,
    BONK_BEHAVIOR_FOURTH_SWING  = 39,
    BONK_BEHAVIOR_BLOOD_AURA    = 40,
    BONK_BEHAVIOR_BLOOD_POOL    = 41,
    BONK_BEHAVIOR_BLOODLUST     = 42,
    BONK_BEHAVIOR_AFTERSHOCK    = 43,
    BONK_BEHAVIOR_BLADESTORM    = 44,
    BONK_BEHAVIOR_STEEL_CYCLONE = 45,
    BONK_BEHAVIOR_AEGIS         = 46,
    BONK_BEHAVIOR_SPIKED_SHIELD = 47,
    BONK_BEHAVIOR_ROLLING_THUNDER = 48,
    BONK_BEHAVIOR_DEMORALIZING_SHOUT = 49,
    BONK_BEHAVIOR_VENGEANCE     = 50,
    BONK_BEHAVIOR_BERSERKER_RAGE = 51,
    BONK_BEHAVIOR_AVENGERS_SHIELD = 52,
    BONK_BEHAVIOR_BULWARK       = 53,
    MAX_BONK_BEHAVIOR
};

enum BonkKeyword : uint8
{
    BONK_KEYWORD_NONE                 = 0,
    BONK_KEYWORD_CHILL                = 1,
    BONK_KEYWORD_FROZEN               = 2,
    BONK_KEYWORD_BURNING              = 3,
    BONK_KEYWORD_BLEED                = 4,
    BONK_KEYWORD_POISON               = 5,
    BONK_KEYWORD_FIRE_VULNERABILITY   = 6,
    BONK_KEYWORD_SUNDER_ARMOR         = 7,
    BONK_KEYWORD_SHADOW_VULNERABILITY = 8,
    BONK_KEYWORD_HUNTERS_MARK         = 9,
    BONK_KEYWORD_DAZED                = 10,
    MAX_BONK_KEYWORD,
    BONK_KEYWORD_VARIETY              = 255
};

struct BonkKeywordInfo
{
    BonkKeyword id = BONK_KEYWORD_NONE;
    std::string name;
    std::string icon;
    uint32 spellId = 0;
    uint8 maxStacks = 1;
    float valuePerStack = 0.0f;
    uint32 tickMs = 0;
    uint32 durationMs = 0;
    bool spreadsOnDeath = false;

    bool IsDamageOverTime() const
    {
        return id == BONK_KEYWORD_BURNING || id == BONK_KEYWORD_BLEED || id == BONK_KEYWORD_POISON;
    }
};

struct BonkWeapon
{
    uint32 id = 0;
    std::string name;
    std::string icon;
    std::string description;
    uint32 spellId = 0;
    BonkWeaponTarget target = BONK_TARGET_NEAREST;
    uint32 cooldownMs = 2000;
    float range = 30.0f;
    float damageFraction = 0.5f;
    uint32 classMask = 0;
    uint32 starterClassMask = 0;
    uint32 evolvesFrom = 0;
    float healPct = 0.0f;
    uint32 durationMs = 0;
    uint32 tickMs = 0;
    bool chains = false;
    BonkWeaponBehavior behavior = BONK_BEHAVIOR_NONE;
    float behaviorValue = 0.0f;
    uint8 baseTargets = 1;
    uint32 altSpellId = 0;
    uint8 altEvery = 0;
    float altDamage = 1.0f;
    uint8 baseJumps = 0;
    float killBelowPct = 0.0f;
    uint32 flags = 0;
    BonkKeyword applies = BONK_KEYWORD_NONE;
    uint8 applyStacks = 0;
    BonkKeyword uses = BONK_KEYWORD_NONE;
    float usesPct = 0.0f;
    std::vector<BonkWeaponUpgrade> upgrades;

    int32 damageEffectIndex = -1;
    int32 tickEffectIndex = -1;
    float groundRadius = 0.0f;
    bool weaponDamage = false;
    bool lasting = false;

    bool HasFlag(BonkWeaponFlags flag) const { return (flags & flag) != 0; }
    bool IsKeywordOnly() const { return damageEffectIndex < 0 && !weaponDamage; }
    bool IsWhirlwind() const
    {
        return behavior == BONK_BEHAVIOR_WHIRLWIND || behavior == BONK_BEHAVIOR_BLADESTORM || behavior == BONK_BEHAVIOR_STEEL_CYCLONE;
    }
    bool IsShieldBlock() const
    {
        return behavior == BONK_BEHAVIOR_SHIELD || behavior == BONK_BEHAVIOR_AEGIS || behavior == BONK_BEHAVIOR_SPIKED_SHIELD;
    }
    bool IsWeaken() const
    {
        return behavior == BONK_BEHAVIOR_WEAKEN || behavior == BONK_BEHAVIOR_ROLLING_THUNDER || behavior == BONK_BEHAVIOR_DEMORALIZING_SHOUT;
    }
    bool IsRevenge() const
    {
        return behavior == BONK_BEHAVIOR_REVENGE || behavior == BONK_BEHAVIOR_VENGEANCE || behavior == BONK_BEHAVIOR_BERSERKER_RAGE;
    }
    bool IsShieldSlam() const
    {
        return behavior == BONK_BEHAVIOR_SHIELD_SLAM || behavior == BONK_BEHAVIOR_AVENGERS_SHIELD || behavior == BONK_BEHAVIOR_BULWARK;
    }
    bool IsTrigger() const
    {
        return behavior == BONK_BEHAVIOR_DEEP_WOUNDS || behavior == BONK_BEHAVIOR_ROLLING_THUNDER || IsRevenge();
    }
    bool IsAimed() const
    {
        return (target == BONK_TARGET_NEAREST || target == BONK_TARGET_RANDOM || target == BONK_TARGET_STRONGEST || target == BONK_TARGET_FARTHEST) &&
            behavior != BONK_BEHAVIOR_CLEAVE;
    }
    bool HasArea() const
    {
        return target == BONK_TARGET_SELF_AREA || target == BONK_TARGET_GROUND || target == BONK_TARGET_CONE ||
            behavior == BONK_BEHAVIOR_CLEAVE || behavior == BONK_BEHAVIOR_TRAP;
    }
    float GetUpgradeStep(BonkSpellUpgrade upgrade) const
    {
        for (BonkWeaponUpgrade const& itr : upgrades)
            if (itr.upgrade == upgrade)
                return itr.step;
        return 0.0f;
    }
    bool HasTargets() const
    {
        if (target == BONK_TARGET_SELF)
            return behavior == BONK_BEHAVIOR_TRAP || behavior == BONK_BEHAVIOR_PET;
        return target != BONK_TARGET_SELF_AREA && target != BONK_TARGET_CONE;
    }
    char const* GetTargetNoun() const
    {
        if (behavior == BONK_BEHAVIOR_TRAP)
            return "trap";
        if (behavior == BONK_BEHAVIOR_PET)
            return "beast";
        if (behavior == BONK_BEHAVIOR_WRATH)
            return "wolf";
        return target == BONK_TARGET_GROUND ? "pack" : "target";
    }
    std::string DescribeTargets(uint32 count) const
    {
        std::string const number = std::to_string(count);
        std::string const plural = count == 1 ? "" : "s";
        if (behavior == BONK_BEHAVIOR_TRAP)
            return (target == BONK_TARGET_GROUND ? "Throws " : "Places ") + number + " trap" + plural + ".";
        if (behavior == BONK_BEHAVIOR_PET)
            return "Calls up to " + number + " beast" + plural + ".";
        if (target == BONK_TARGET_GROUND)
            return "Lands on " + number + " pack" + plural + " of enemies.";
        return "Targets up to " + number + (count == 1 ? " enemy." : " enemies.");
    }
};

std::string DescribeBonkSpellUpgrade(BonkWeapon const& weapon, BonkSpellUpgrade upgrade, float amount);

uint32 const BONK_MAX_SUMMONS        = 8;
float const BONK_SUMMON_POWER_SHARE  = 0.6f;

uint32 const BONK_PET_FAMILY_CAT = 1;

enum BonkPetAbilityKind : uint8
{
    BONK_PET_ABILITY_STRIKE = 0,
    BONK_PET_ABILITY_DASH   = 1,
    BONK_PET_ABILITY_POUNCE = 2,
    MAX_BONK_PET_ABILITY_KIND
};

struct BonkPetAbility
{
    uint32 spellId = 0;
    BonkPetAbilityKind kind = BONK_PET_ABILITY_STRIKE;
    uint32 cooldownMs = 0;
    float damage = 0.0f;
    uint8 stacks = 0;
    float value = 0.0f;
};

struct BonkPetFamily
{
    uint32 id = 0;
    std::string name;
    uint32 entry = 0;
    float scale = 1.0f;
    uint32 attackMs = 1500;
    BonkKeyword applies = BONK_KEYWORD_NONE;
    uint8 applyStacks = 0;
    std::vector<BonkPetAbility> abilities;
};

enum BonkPassiveEffect : uint8
{
    BONK_PASSIVE_POWER       = 0,
    BONK_PASSIVE_HASTE       = 1,
    BONK_PASSIVE_AMOUNT      = 2,
    BONK_PASSIVE_MOVE_SPEED  = 3,
    BONK_PASSIVE_MAX_HEALTH  = 4,
    BONK_PASSIVE_RECOVERY    = 5,
    BONK_PASSIVE_GREED       = 6,
    BONK_PASSIVE_GROWTH      = 7,
    BONK_PASSIVE_PICKUP      = 8,
    BONK_PASSIVE_LEECH       = 9,
    BONK_PASSIVE_AREA        = 10,
    BONK_PASSIVE_DURATION    = 11,
    BONK_PASSIVE_ARMOR       = 12,
    BONK_PASSIVE_HIT         = 13,
    BONK_PASSIVE_DODGE       = 14,
    BONK_PASSIVE_CRIT        = 15,
    BONK_PASSIVE_CRIT_DAMAGE = 16,
    BONK_PASSIVE_SHIELD      = 17,
    BONK_PASSIVE_LUCK        = 18,
    BONK_PASSIVE_POISON_DAMAGE = 19,
    MAX_BONK_PASSIVE_EFFECT
};

float const BONK_MAX_DODGE          = 60.0f;
float const BONK_MAX_HIT            = 100.0f;
float const BONK_MAX_COUNT          = 5.0f;
float const BONK_MAX_AREA           = 50.0f;
float const BONK_MAX_HASTE          = 100.0f;
uint32 const BONK_MAX_COUNT_PULSES  = 2;
float const BONK_MAX_GEAR_HASTE     = 20.0f;
float const BONK_BASE_CRIT          = 5.0f;
float const BONK_BOSS_EXTRA_DODGE   = 5.0f;
uint32 const BONK_SHIELD_REGEN_DELAY_MS = 4000;

struct BonkPassive
{
    uint32 id = 0;
    std::string name;
    std::string icon;
    std::string description;
    BonkPassiveEffect effect = BONK_PASSIVE_POWER;
    float valuePerRank = 10.0f;
    uint8 maxRank = 5;
    uint32 excludes = 0;
};

enum BonkMobRole : uint8
{
    BONK_ROLE_MELEE    = 0,
    BONK_ROLE_RANGED   = 1,
    BONK_ROLE_EXPLODER = 2,
    MAX_BONK_MOB_ROLE
};

enum BonkAbilityTarget : uint8
{
    BONK_ABILITY_PLAYER = 0,
    BONK_ABILITY_SELF   = 1,
};

enum BonkAbilityShape : uint8
{
    BONK_SHAPE_SPELL   = 0,
    BONK_SHAPE_CONE    = 1,
    BONK_SHAPE_RING    = 2,
    BONK_SHAPE_BARRAGE = 3,
    BONK_SHAPE_TRAIL   = 4,
    MAX_BONK_ABILITY_SHAPE
};

struct BonkMobAbility
{
    uint32 spellId = 0;
    BonkAbilityTarget target = BONK_ABILITY_PLAYER;
    uint32 cooldownMs = 8000;
    uint32 firstMs = 3000;
    float minRange = 0.0f;
    float maxRange = 30.0f;
    float damage = 0.0f;
    int32 damageEffectIndex = -1;
    BonkAbilityShape shape = BONK_SHAPE_SPELL;
    uint32 count = 0;
    float size = 0.0f;
    float belowHealth = 100.0f;
};

struct BonkMob
{
    BonkMobRole role = BONK_ROLE_MELEE;
    float range = 0.0f;
    std::vector<BonkMobAbility> abilities;
};

std::string FormatBonkMoney(uint32 copper);
std::string FormatBonkShards(uint32 count);

enum BonkShop : uint8
{
    BONK_SHOP_NONE     = 0,
    BONK_SHOP_UPGRADES = 1,
    BONK_SHOP_TALENTS  = 2,
    BONK_SHOP_MASTERY  = 3,
};

uint8 const BONK_SHOP_ROWS    = 7;
uint8 const BONK_SHOP_COLUMNS = 4;

struct BonkShopTree
{
    uint32 id = 0;
    BonkShop shop = BONK_SHOP_NONE;
    std::string name;
    std::string background;
};

struct BonkShopNode
{
    std::string icon;
    uint32 tree = 0;
    uint8 row = 1;
    uint8 column = 1;
    uint32 requiredId = 0;
    uint8 requiredRank = 0;
};

enum BonkUpgradeUtility : uint8
{
    BONK_UTILITY_DASH_RANGE    = 0,
    BONK_UTILITY_DASH_COOLDOWN = 1,
    BONK_UTILITY_DASH_CHARGES  = 2,
    BONK_UTILITY_HAGGLE        = 3,
    BONK_UTILITY_SCRAPPER      = 4,
    BONK_UTILITY_CARD_CHOICES  = 5,
    BONK_UTILITY_LEVEL_CHOICES = 6,
    BONK_UTILITY_EVENT_TIME    = 7,
    BONK_UTILITY_SHRINE_SPEED  = 8,
    BONK_UTILITY_HEARTHSTONE   = 9,
    BONK_UTILITY_START_LEVEL   = 10,
    BONK_UTILITY_START_CARD    = 11,
    BONK_UTILITY_PACK_MULE     = 12,
    BONK_UTILITY_POOL_TOGGLES  = 13,
    BONK_UTILITY_START_GLYPH   = 14,
    MAX_BONK_UPGRADE_UTILITY
};
uint8 const BONK_UPGRADE_UTILITY_BASE = 100;

struct BonkUpgrade
{
    uint32 id = 0;
    std::string name;
    std::string description;
    BonkPassiveEffect effect = BONK_PASSIVE_POWER;
    int8 utility = -1;
    float valuePerRank = 5.0f;
    uint8 maxRank = 5;
    uint32 cost = 5000;
    float costGrowth = 1.4f;
    BonkShopNode layout;

    uint32 GetCost(uint8 ownedRanks) const;
};

enum BonkTalentEffect : uint8
{
    BONK_TALENT_ENEMY_HEALTH = 0,
    BONK_TALENT_ENEMY_DAMAGE = 1,
    BONK_TALENT_SPAWN_RATE   = 2,
    BONK_TALENT_ELITE_RATE   = 3,
    BONK_TALENT_REROLL       = 4,
    BONK_TALENT_BANISH       = 5,
    BONK_TALENT_REVIVE       = 6,
    BONK_TALENT_START_COINS  = 7,
    BONK_TALENT_SKIP         = 8,
    BONK_TALENT_SPELL_SLOT   = 9,
    BONK_TALENT_PASSIVE_SLOT = 10,
    BONK_TALENT_SECOND_SIGHT = 11,
    MAX_BONK_TALENT_EFFECT
};

struct BonkTalent
{
    uint32 id = 0;
    std::string name;
    std::string description;
    BonkTalentEffect effect = BONK_TALENT_REROLL;
    float valuePerRank = 1.0f;
    float goldBonus = 0.0f;
    uint8 maxRank = 1;
    uint32 cost = 1;
    float costGrowth = 1.0f;
    BonkShopNode layout;
    std::vector<uint32> rankQuests;

    bool IsToggle() const { return effect <= BONK_TALENT_ELITE_RATE; }
    uint32 GetCost(uint8 ownedRanks) const;
};

struct BonkRunBonuses
{
    float passives[MAX_BONK_PASSIVE_EFFECT] = {};
    float utility[MAX_BONK_UPGRADE_UTILITY] = {};
    float enemyHealth = 0.0f;
    float enemyDamage = 0.0f;
    float spawnRate = 0.0f;
    float eliteRate = 0.0f;
    float goldBonus = 0.0f;
    uint32 rerolls = 0;
    uint32 banishes = 0;
    uint32 skips = 0;
    uint32 revives = 0;
    uint32 startCoins = 0;
    uint32 spellSlots = 0;
    uint32 passiveSlots = 0;
    bool secondSight = false;
};

struct BonkItemStat
{
    float stats[MAX_BONK_PASSIVE_EFFECT] = {};
    uint32 setId = 0;
    uint32 unlockTier = 0;
};

struct BonkItemSetBonus
{
    uint8 pieces = 2;
    float stats[MAX_BONK_PASSIVE_EFFECT] = {};
    std::string description;
};

struct BonkItemSet
{
    uint32 id = 0;
    std::string name;
    std::vector<uint32> items;
    std::vector<BonkItemSetBonus> bonuses;
};

struct BonkGearTotals
{
    float passives[MAX_BONK_PASSIVE_EFFECT] = {};
};

enum BonkCardEffect : uint8
{
    BONK_CARD_NONE          = 0,
    BONK_CARD_HEALTHSTONE   = 1,
    BONK_CARD_BANDAGE       = 2,
    BONK_CARD_CAMPFIRE      = 3,
    BONK_CARD_JERKY         = 4,
    BONK_CARD_CRIT_HEAL     = 5,
    BONK_CARD_OVERHEAL      = 6,
    BONK_CARD_SHIELD_NOVA   = 7,
    BONK_CARD_THORNS        = 8,
    BONK_CARD_HIT_IMMUNE    = 9,
    BONK_CARD_SWIFTNESS     = 10,
    BONK_CARD_SOULSTONE     = 11,
    BONK_CARD_FIRE_BURST    = 12,
    BONK_CARD_ECHO          = 13,
    BONK_CARD_CHAIN         = 14,
    BONK_CARD_DEATH_RAY     = 15,
    BONK_CARD_KILL_POWER    = 16,
    BONK_CARD_POWER_SURGE   = 17,
    BONK_CARD_BIG_GAME      = 18,
    BONK_CARD_VALOR         = 19,
    BONK_CARD_MAGNET        = 20,
    BONK_CARD_FREE_CHEST    = 21,
    BONK_CARD_SHADOW_BURST  = 22,
    BONK_CARD_ASCENSION     = 23,
    MAX_BONK_CARD_EFFECT
};

struct BonkCard
{
    uint32 id = 0;
    std::string name;
    std::string icon;
    std::string description;
    BonkRarity rarity = BONK_RARITY_COMMON;
    BonkPassiveEffect stats[2] = { MAX_BONK_PASSIVE_EFFECT, MAX_BONK_PASSIVE_EFFECT };
    float statValues[2] = {};
    BonkCardEffect effect = BONK_CARD_NONE;
    float values[3] = {};
    uint32 cooldownMs = 0;
    uint8 maxCopies = 0;
    bool inChests = true;
};

uint8 const BONK_HIDDEN_USE_TRIGGER  = 0;
uint8 const BONK_HIDDEN_NEAR_TRIGGER = 1;

struct BonkHidden
{
    uint32 id = 0;
    std::string name;
    uint32 mapId = 0;
    float chance = 0.0f;
    uint8 trigger = BONK_HIDDEN_USE_TRIGGER;
    uint32 objectEntry = 0;
    uint32 bossEntry = 0;
    float bossScale = 1.0f;
    uint32 addEntry = 0;
    uint32 addCount = 0;
    uint32 chestChoices = 3;
    std::string yell;
    std::string foundText;
};

enum BonkGlyphEffect : uint8
{
    BONK_GLYPH_NONE           = 0,
    BONK_GLYPH_PACKRAT        = 1,
    BONK_GLYPH_THRIFT         = 2,
    BONK_GLYPH_SECOND_WIND    = 3,
    BONK_GLYPH_LEY_LINE       = 4,
    BONK_GLYPH_KILL_HASTE     = 5,
    BONK_GLYPH_CHEST_TRAP     = 6,
    BONK_GLYPH_HALF_ARMOR     = 7,
    BONK_GLYPH_DIVINE_SHIELD  = 8,
    BONK_GLYPH_SOUL_HARVEST   = 9,
    BONK_GLYPH_VENGEANCE      = 10,
    BONK_GLYPH_NO_HEALS       = 11,
    BONK_GLYPH_GAMBLER        = 12,
    BONK_GLYPH_STORM          = 13,
    BONK_GLYPH_METEOR         = 14,
    MAX_BONK_GLYPH_EFFECT
};

struct BonkGlyph
{
    uint32 id = 0;
    std::string name;
    std::string icon;
    std::string description;
    std::string drawback;
    BonkRarity rarity = BONK_RARITY_COMMON;
    BonkGlyphEffect effect = BONK_GLYPH_NONE;
    float values[3] = {};
    uint32 cardId = 0;
    bool starter = false;
};

struct BonkGear
{
    uint32 entry = 0;
    uint8 quality = 2;
    uint32 keepEntry = 0;
    uint32 allowableClass = 0;
};

class BonkMgr
{
    public:
        void Load();

        BonkTier const* GetTier(uint32 id) const;
        BonkTier const* FindTier(std::string const& keyOrId) const;
        BonkMap const* FindDungeon(std::string const& key) const;
        std::vector<BonkMap const*> GetDungeons() const;
        std::map<uint32, BonkTier> const& GetTiers() const { return m_tiers; }

        BonkMap const* GetMap(uint32 id) const;
        std::vector<BonkMapStart> GetMapStarts(uint32 bonkMapId) const;
        bool AddMapStart(uint32 bonkMapId, BonkMapStart const& start, float spread, size_t max);
        std::map<uint32, BonkMap> const& GetMaps() const { return m_maps; }

        BonkWeapon const* GetWeapon(uint32 id) const;
        std::map<uint32, BonkWeapon> const& GetWeapons() const { return m_weapons; }
        std::vector<BonkWeapon const*> GetEvolutions(uint32 weaponId) const;
        bool HasStarterSpell(uint32 classMask) const;
        std::string DescribeEvolutionNames(uint32 weaponId) const;

        BonkProjectileInfo const* GetProjectile(uint32 spellId) const;

        BonkKeywordInfo const* GetKeyword(BonkKeyword id) const;
        BonkKeywordInfo const* GetKeywordBySpell(uint32 spellId) const;
        std::map<uint8, BonkKeywordInfo> const& GetKeywords() const { return m_keywords; }

        BonkMob const* GetMob(uint32 entry) const;

        BonkArchetypeInfo const& GetArchetype(BonkArchetype archetype) const { return m_archetypes[archetype]; }
        std::vector<BonkWave> const& GetWaves() const { return m_waves; }
        std::vector<BonkScalingPoint> const& GetScaling() const { return m_scaling; }

        BonkPassive const* GetPassive(uint32 id) const;
        std::string DescribePassive(BonkPassive const& passive) const;
        std::map<uint32, BonkPassive> const& GetPassives() const { return m_passives; }

        std::vector<BonkPowerup> const& GetPowerups() const { return m_powerups; }

        BonkCard const* GetCard(uint32 id) const;
        std::map<uint32, BonkCard> const& GetCards() const { return m_cards; }
        std::map<uint32, BonkGlyph> const& GetGlyphs() const { return m_glyphs; }
        std::vector<BonkHidden> const& GetHiddenEncounters() const { return m_hiddenEncounters; }
        BonkGlyph const* GetGlyph(uint32 id) const;

        uint32 GetItemCard(uint32 itemEntry) const;

        BonkGear const* GetGear(uint32 entry) const;
        std::map<uint32, BonkGear> const& GetGear() const { return m_gear; }

        std::map<uint32, BonkShopTree> const& GetShopTrees() const { return m_shopTrees; }

        BonkUpgrade const* GetUpgrade(uint32 id) const;
        std::map<uint32, BonkUpgrade> const& GetUpgrades() const { return m_upgrades; }

        BonkTalent const* GetTalent(uint32 id) const;
        std::map<uint32, BonkTalent> const& GetTalents() const { return m_talents; }

        BonkItemStat const* GetItemStat(uint32 itemEntry) const;
        std::map<uint32, BonkItemStat> const& GetItemStats() const { return m_itemStats; }
        BonkItemSet const* GetItemSet(uint32 id) const;
        std::map<uint32, BonkItemSet> const& GetItemSets() const { return m_itemSets; }
        BonkGearTotals EvaluateGear(Player const* player) const;

        bool IsStaticCreatureSuppressed(Map const* map, uint32 dbGuid) const;
        bool IsArenaLocation(uint32 mapId, uint32 zoneId) const;

        BonkPetFamily const* GetPetFamily(uint32 id) const;

    private:
        void LoadTiers();
        void LoadMaps();
        void LoadMapCreatures();
        void LoadMapEvents();
        void LoadKeywords();
        void LoadProjectiles();
        void LoadWeapons();
        void LoadPassives();
        void LoadPowerups();
        void LoadCards();
        void LoadItemCards();
        void LoadGlyphs();
        void LoadHiddenEncounters();
        void LoadGear();
        void LoadShopTrees();
        void LoadUpgrades();
        void LoadTalents();
        void LoadItemStats();
        void LoadMobs();
        void LoadArchetypes();
        void LoadWaves();
        void LoadScaling();
        void LoadPetFamilies();
        void LoadPetAbilities();

        template <class Entry>
        void CheckShopLayout(char const* table, BonkShop shop, std::map<uint32, Entry>& entries) const;

        std::map<uint32, BonkTier> m_tiers;
        std::map<uint32, BonkMap> m_maps;
        std::map<uint32, BonkWeapon> m_weapons;
        std::map<uint8, BonkKeywordInfo> m_keywords;
        std::map<uint32, BonkProjectileInfo> m_projectiles;
        std::map<uint32, BonkPassive> m_passives;
        std::vector<BonkPowerup> m_powerups;
        std::map<uint32, BonkCard> m_cards;
        std::map<uint32, uint32> m_itemCards;
        std::map<uint32, BonkGlyph> m_glyphs;
        std::vector<BonkHidden> m_hiddenEncounters;
        std::map<uint32, BonkGear> m_gear;
        std::map<uint32, BonkShopTree> m_shopTrees;
        std::map<uint32, BonkUpgrade> m_upgrades;
        mutable std::mutex m_mapStartLock;
        std::map<uint32, std::vector<BonkMapStart>> m_mapStarts;
        std::map<uint32, BonkTalent> m_talents;
        std::map<uint32, BonkItemStat> m_itemStats;
        std::map<uint32, BonkItemSet> m_itemSets;
        std::map<uint32, BonkMob> m_mobs;
        BonkArchetypeInfo m_archetypes[MAX_BONK_ARCHETYPE];
        std::vector<BonkWave> m_waves;
        std::vector<BonkScalingPoint> m_scaling;
        std::map<uint32, std::set<uint32>> m_arenaZonesByMap;
        std::map<uint32, BonkPetFamily> m_petFamilies;
};

#define sBonkMgr MaNGOS::Singleton<BonkMgr>::Instance()

#endif
