/*
 * Vanilla Bonk: a Vampire Survivors style game mode.
 *
 * BonkMgr owns the static game-mode data loaded from the world database
 * (difficulty tiers, the arena maps in their pools, spawn pools, timed
 * events, power-ups, weapons and passive upgrades) and the glue between
 * core systems and the per-player state in BonkPlayer.
 */

#ifndef MANGOS_BONKMGR_H
#define MANGOS_BONKMGR_H

#include "Common.h"
#include "Policies/Singleton.h"
#include "SharedDefines.h"

#include <map>
#include <set>
#include <string>
#include <vector>

class Map;
class Player;

// Where players live between runs: GM Island.
WorldLocation const BONK_CAMP_LOCATION(1, 16226.2f, 16257.0f, 13.2022f, 1.65007f);

// Gameobjects a run places in the arena (world database ids).
uint32 const BONK_GO_CHEST      = 900001;   // costs coins to open
uint32 const BONK_GO_CHEST_FREE = 900002;   // dropped by elites and bosses
uint32 const BONK_GO_PORTAL     = 900003;   // leads to the next stage

enum BonkSpawnKind : uint8
{
    BONK_SPAWN_NORMAL   = 0,
    BONK_SPAWN_ELITE    = 1,
    BONK_SPAWN_BOSS     = 2,    // ends the stage
    BONK_SPAWN_MINIBOSS = 3,
    MAX_BONK_SPAWN_KIND
};

enum BonkEventType : uint8
{
    BONK_EVENT_MINIBOSS = 0,    // `count` mini-bosses
    BONK_EVENT_ELITES   = 1,    // `count` elites
    BONK_EVENT_SWARM    = 2,    // `count` normal enemies at once
    BONK_EVENT_CHESTS   = 3,    // `count` chests that cost coins
    MAX_BONK_EVENT
};

struct BonkMapEvent
{
    uint32 second = 0;              // seconds into the stage
    BonkEventType type = BONK_EVENT_MINIBOSS;
    uint32 count = 1;
};

struct BonkMapCreature
{
    uint32 entry = 0;
    BonkSpawnKind kind = BONK_SPAWN_NORMAL;
    uint32 minSecond = 0;           // first second of the run it can spawn
    uint32 maxSecond = 0;           // last second it can spawn, 0 = until the end
    uint32 weight = 1;
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float speed = 0.7f;             // run speed rate, the player runs at 1.0
    float scale = 1.0f;
};

// One stage of a run: an arena map from a tier's pool.
struct BonkMap
{
    uint32 id = 0;
    std::string key;
    std::string name;
    uint32 tierId = 0;              // the tier whose pool it belongs to
    uint32 mapId = 0;
    uint32 zoneId = 0;              // arena boundary; 0 = the whole map
    float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
    uint32 durationSeconds = 600;   // the stage boss arrives after this long
    int32 levelOffset = 0;          // mob level relative to the player
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float healthGrowthPerMinute = 0.1f;
    uint32 spawnIntervalMs = 2000;
    float spawnBatch = 2.0f;
    float spawnBatchGrowthPerMinute = 0.5f;
    uint32 maxAlive = 80;
    uint32 eliteIntervalSeconds = 90;
    uint32 goldPerKill = 10;        // copper
    uint32 stageGold = 5000;        // copper for the stage boss, times the stage number
    uint32 coinsPerKill = 1;        // in-run currency for chests
    uint32 powerupIntervalSeconds = 40; // a new power-up appears this often, 0 = none
    uint32 powerupMax = 3;          // most power-ups lying around at once

    std::vector<BonkMapCreature> creatures;
    std::vector<BonkMapEvent> events;   // sorted by time
};

// Battleground-style buff lying in the arena; walking over it casts the spell.
struct BonkPowerup
{
    uint32 entry = 0;               // gameobject_template, only its looks are used
    uint32 spellId = 0;
    uint32 weight = 1;
};

// A difficulty: a pool of maps played one after another, each stage harder.
struct BonkTier
{
    uint32 id = 0;
    std::string key;                // short name used by `.bonk start`
    std::string name;
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float stageHealthGrowth = 0.75f;    // extra enemy health per stage after the first
    float stageDamageGrowth = 0.35f;
    float stageSpawnGrowth = 0.2f;      // extra enemies per wave per stage
    uint32 chestCost = 10;              // coins for the first chest of a run
    float chestCostGrowth = 0.5f;       // price increase per chest bought
    uint32 unlockTierId = 0;            // reach unlockStage on this tier to unlock, 0 = always open
    uint32 unlockStage = 0;

    std::vector<uint32> mapIds;     // the pool, bonk_map ids
};

enum BonkWeaponTarget : uint8
{
    BONK_TARGET_NEAREST   = 0,      // fire at the closest enemies
    BONK_TARGET_SELF_AREA = 1,      // area spell around the player
    BONK_TARGET_RANDOM    = 2,      // fire at random enemies in range
};

struct BonkWeapon
{
    uint32 id = 0;
    std::string name;
    std::string icon;               // Interface\Icons texture name
    std::string description;
    uint32 spellId = 0;
    BonkWeaponTarget target = BONK_TARGET_NEAREST;
    uint32 cooldownMs = 2000;
    float range = 30.0f;            // targeting range, or radius for area weapons
    float damageFraction = 0.5f;    // hit damage as a share of a same-level mob's health
    uint32 classMask = 0;           // classes that can be offered it, 0 = all
    uint32 starterClassMask = 0;    // classes that start every run with it
    uint8 maxLevel = 8;

    int32 damageEffectIndex = -1;   // effect whose base points carry the damage
};

enum BonkPassiveEffect : uint8
{
    BONK_PASSIVE_MIGHT       = 0,   // + weapon damage, %
    BONK_PASSIVE_COOLDOWN    = 1,   // - weapon cooldown, %
    BONK_PASSIVE_AMOUNT      = 2,   // + projectiles
    BONK_PASSIVE_MOVE_SPEED  = 3,   // + run speed, %
    BONK_PASSIVE_MAX_HEALTH  = 4,   // + max health, %
    BONK_PASSIVE_RECOVERY    = 5,   // health regained per second, % of max
    BONK_PASSIVE_GREED       = 6,   // + gold, %
    BONK_PASSIVE_GROWTH      = 7,   // + experience, %
    MAX_BONK_PASSIVE_EFFECT
};

struct BonkPassive
{
    uint32 id = 0;
    std::string name;
    std::string icon;
    std::string description;
    BonkPassiveEffect effect = BONK_PASSIVE_MIGHT;
    float valuePerRank = 10.0f;
    uint8 maxRank = 5;
};

class BonkMgr
{
    public:
        void Load();

        BonkTier const* GetTier(uint32 id) const;
        BonkTier const* FindTier(std::string const& keyOrId) const;
        std::map<uint32, BonkTier> const& GetTiers() const { return m_tiers; }

        BonkMap const* GetMap(uint32 id) const;
        std::map<uint32, BonkMap> const& GetMaps() const { return m_maps; }

        BonkWeapon const* GetWeapon(uint32 id) const;
        std::map<uint32, BonkWeapon> const& GetWeapons() const { return m_weapons; }

        BonkPassive const* GetPassive(uint32 id) const;
        std::map<uint32, BonkPassive> const& GetPassives() const { return m_passives; }

        std::vector<BonkPowerup> const& GetPowerups() const { return m_powerups; }

        // Arena maps start empty: the run spawns every enemy itself.
        bool IsStaticCreatureSuppressed(Map const* map, uint32 dbGuid) const;
        bool IsArenaLocation(uint32 mapId, uint32 zoneId) const;

    private:
        void LoadTiers();
        void LoadMaps();
        void LoadMapCreatures();
        void LoadMapEvents();
        void LoadWeapons();
        void LoadPassives();
        void LoadPowerups();

        std::map<uint32, BonkTier> m_tiers;
        std::map<uint32, BonkMap> m_maps;
        std::map<uint32, BonkWeapon> m_weapons;
        std::map<uint32, BonkPassive> m_passives;
        std::vector<BonkPowerup> m_powerups;
        std::map<uint32, std::set<uint32>> m_arenaZonesByMap;   // zone 0 = whole map
};

#define sBonkMgr MaNGOS::Singleton<BonkMgr>::Instance()

#endif
