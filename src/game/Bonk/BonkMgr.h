/*
 * Vanilla Bonk: a Vampire Survivors style game mode.
 *
 * BonkMgr owns the static game-mode data loaded from the world database
 * (arena maps, their spawn pools, weapons and passive upgrades) and the
 * glue between core systems and the per-player state in BonkPlayer.
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

enum BonkSpawnKind : uint8
{
    BONK_SPAWN_NORMAL = 0,
    BONK_SPAWN_ELITE  = 1,
    BONK_SPAWN_BOSS   = 2,
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

struct BonkMap
{
    uint32 id = 0;
    std::string key;                // short name used by `.bonk start`
    std::string name;
    uint8 difficulty = 0;           // 0 easy, 1 normal, 2 hard
    uint32 mapId = 0;
    uint32 zoneId = 0;              // arena boundary; 0 = the whole map
    float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
    uint32 durationSeconds = 600;   // the boss arrives after this long
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
    uint32 victoryGold = 5000;      // copper

    std::vector<BonkMapCreature> creatures;

    char const* GetDifficultyName() const;
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

        BonkMap const* GetMap(uint32 id) const;
        BonkMap const* FindMap(std::string const& keyOrId) const;
        std::map<uint32, BonkMap> const& GetMaps() const { return m_maps; }

        BonkWeapon const* GetWeapon(uint32 id) const;
        std::map<uint32, BonkWeapon> const& GetWeapons() const { return m_weapons; }

        BonkPassive const* GetPassive(uint32 id) const;
        std::map<uint32, BonkPassive> const& GetPassives() const { return m_passives; }

        // Arena maps start empty: the run spawns every enemy itself.
        bool IsStaticCreatureSuppressed(Map const* map, uint32 dbGuid) const;
        bool IsArenaLocation(uint32 mapId, uint32 zoneId) const;

    private:
        void LoadMaps();
        void LoadMapCreatures();
        void LoadWeapons();
        void LoadPassives();

        std::map<uint32, BonkMap> m_maps;
        std::map<uint32, BonkWeapon> m_weapons;
        std::map<uint32, BonkPassive> m_passives;
        std::map<uint32, std::set<uint32>> m_arenaZonesByMap;   // zone 0 = whole map
};

#define sBonkMgr MaNGOS::Singleton<BonkMgr>::Instance()

#endif
