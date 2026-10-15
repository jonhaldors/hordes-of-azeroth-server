/*
 * Vanilla Bonk: a Vampire Survivors style game mode.
 *
 * BonkMgr owns the static game-mode data loaded from the world database
 * (difficulty tiers, the arena maps in their pools, spawn pools, timed
 * events, power-ups, weapons and passive upgrades, and what camp sells:
 * permanent upgrades, talents and gear power) and the glue between core
 * systems and the per-player state in BonkPlayer.
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
uint32 const BONK_GO_CHEST      = 900001;   // costs Valor to open
uint32 const BONK_GO_CHEST_FREE = 900002;   // dropped by elites and bosses
uint32 const BONK_GO_PORTAL     = 900003;   // leads to the next stage
uint32 const BONK_GO_SHARD      = 900007;   // a dropped Talent Shard, taken by walking over it
uint32 const BONK_GO_HEAL       = 900008;   // a dropped Turkey Leg, eaten by walking over it while hurt

// Experience orbs dropped by kills (creature templates, only their looks are
// used, jewels from the client patch, tools/telegraphs/Pickups.cs): blue for
// most trash, green for bruisers, red for elites and bosses. Orbs lying
// together merge up the colours to purple.
uint32 const BONK_ORB_SMALL = 900100;
uint32 const BONK_ORB_LARGE = 900101;
uint32 const BONK_ORB_HUGE  = 900102;
uint32 const BONK_ORB_GRAND = 900103;
// An invisible marker where a Sanguine enemy died; its pool's look is an aura on it.
uint32 const BONK_SANGUINE_POOL = 900110;
// An invisible marker under a turkey leg or Talent Shard; its highlight is an aura on it.
uint32 const BONK_PICKUP_MARKER = 900111;
// A hero's bolt in flight, wearing its spell's missile model (bonk_projectile);
// the spells it casts on enemies it touches are the hero's.
uint32 const BONK_PROJECTILE = 900113;

// A spell that flies as a projectile (bonk_projectile).
struct BonkProjectileInfo
{
    uint32 spellId = 0;
    uint32 displayId = 0;           // creature display wearing its missile model
    float speed = 20.0f;            // yards a second
    float radius = 1.0f;            // yards from its path an enemy is hit, before the enemy's own size
    float scale = 1.0f;             // size of the model
};

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
    BONK_EVENT_CHESTS   = 3,    // `count` chests that cost Valor
    BONK_EVENT_RING     = 4,    // `count` normal enemies in a ring closing in on the player
    BONK_EVENT_RUSH     = 5,    // `count` normal enemies charging in from one side
    MAX_BONK_EVENT
};

struct BonkMapEvent
{
    uint32 second = 0;              // seconds into the stage
    BonkEventType type = BONK_EVENT_MINIBOSS;
    uint32 count = 1;
};

// What a normal enemy is in the crowd (bonk_archetype): its health, damage,
// experience and orb, and from when in a stage it shows up.
enum BonkArchetype : uint8
{
    BONK_ARCHETYPE_SWARMER = 0,     // small, fast, comes in numbers
    BONK_ARCHETYPE_GRUNT   = 1,     // the standard walker
    BONK_ARCHETYPE_BRUISER = 2,     // big and slow, hits hard; its share of the crowd follows the spawn curve
    BONK_ARCHETYPE_CASTER  = 3,     // keeps its distance and casts
    BONK_ARCHETYPE_CHARGER = 4,     // charges in
    MAX_BONK_ARCHETYPE
};

enum BonkOrb : uint8
{
    BONK_ORB_BLUE  = 0,
    BONK_ORB_GREEN = 1,
    BONK_ORB_RED   = 2,
    BONK_ORB_PURPLE = 3,    // only from merging (BonkRun::MergeOrbs)
};

struct BonkArchetypeInfo
{
    std::string name;
    float healthMultiplier = 1.0f;  // times a grunt's
    float damageMultiplier = 1.0f;
    float experience = 1.0f;        // times a grunt's
    uint32 firstSecond = 0;         // into a stage, stage 1
    uint32 firstSecondLater = 0;    // the same from stage 2 on
    BonkOrb orb = BONK_ORB_BLUE;
};

// The spawn curve (bonk_spawn_curve): from `second` into a stage, the crowd
// is kept at least `minAlive` strong (rising to the next point's), a
// `bruiserShare` of new enemies are bruisers, and an elite comes every
// `eliteIntervalSeconds` (0 = none).
struct BonkSpawnPoint
{
    uint32 second = 0;
    float minAlive = 0.0f;
    float bruiserShare = 0.0f;      // 0..1
    uint32 eliteIntervalSeconds = 0;
};

struct BonkMapCreature
{
    uint32 entry = 0;
    BonkSpawnKind kind = BONK_SPAWN_NORMAL;
    BonkArchetype archetype = BONK_ARCHETYPE_GRUNT;    // normal enemies only
    uint32 minSecond = 0;           // first second of the run it can spawn
    uint32 maxSecond = 0;           // last second it can spawn, 0 = until the end
    uint32 weight = 1;
    uint32 maxAlive = 0;            // most of this enemy alive at once, 0 = no limit
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float speed = 0.7f;             // run speed rate, the player runs at 1.0
    float scale = 1.0f;
};

// One stage of a run: an arena map from a tier's pool.
// What kind of ground a map is (feature plan, Map types): smaller maps are
// harder to move in and will pay more (Phase 2).
enum BonkMapType : uint8
{
    BONK_MAP_OPEN     = 0,          // an open zone (Westfall)
    BONK_MAP_ENCLOSED = 1,          // an enclosed raid (Molten Core)
    BONK_MAP_ARENA    = 2,          // an arena (a dungeon's large open room)
    MAX_BONK_MAP_TYPE
};

// What a map type pays, in percent of gold, Darkmoon Tickets and Mastery
// experience: an open zone 100, an enclosed raid 125, an arena 150.
uint32 GetBonkMapRewardPercent(BonkMapType type);

struct BonkMap
{
    uint32 id = 0;
    std::string key;
    std::string name;
    std::string icon;               // Interface\Icons file name, for the arena window
    uint32 tierId = 0;              // the tier whose pool it belongs to
    BonkMapType type = BONK_MAP_OPEN;
    uint32 mapId = 0;
    uint32 zoneId = 0;              // arena boundary; 0 = the whole map
    float x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
    uint32 durationSeconds = 600;   // the stage boss arrives after this long
    int32 levelOffset = 0;          // mob level relative to the player
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    float healthGrowthPerMinute = 0.1f;
    float damageGrowthPerMinute = 0.0f;
    float spawnMinDistance = 38.0f; // enemies appear this far from the player
    float spawnMaxDistance = 48.0f;
    uint32 minibossSecond = 0;      // its first mini-boss event, 0 = none
    uint32 goldPerKill = 10;        // copper
    uint32 stageGold = 5000;        // copper for the stage boss, times the stage number
    uint32 coinsPerKill = 1;        // Valor, the in-run currency for chests
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
    std::string description;        // for the arena window
    std::string art;                // texture shown on its card in the arena window
    float healthMultiplier = 1.0f;
    float damageMultiplier = 1.0f;
    // Enemy health and damage at stage s: 1 + growth (s - 1) + accel (s - 1)(s - 2).
    float stageHealthGrowth = 0.25f;
    float stageDamageGrowth = 0.25f;
    float stageHealthAccel = 0.05f;
    float stageDamageAccel = 0.05f;
    // The spawn curve's crowd times this; past maxAlive the rest goes into
    // enemy health and damage instead.
    float minAliveMultiplier = 1.0f;
    uint32 maxAlive = 200;
    uint32 maxBruisers = 8;
    uint32 firstEliteSecond = 150;      // into a stage
    uint32 chestCost = 10;              // Valor for the first chest of a run
    float chestCostGrowth = 0.5f;       // price increase per chest bought
    uint32 unlockTierId = 0;            // reach unlockStage on this tier to unlock, 0 = always open
    uint32 unlockStage = 0;
    // Talent Shards, the talent trainer's currency: rare on purpose.
    float shardChanceNormal = 0.1f;     // % per normal kill
    float shardChanceElite = 12.0f;     // % per elite
    uint32 shardsMiniboss = 1;          // every mini-boss drops this many
    uint32 shardsBoss = 2;              // every stage boss drops this many
    // Chance elites dodge the hero's hits, %; mini-bosses and bosses dodge
    // BONK_BOSS_EXTRA_DODGE more. Hit takes it off; normal enemies never dodge.
    float eliteDodge = 0.0f;
    // Affixes every elite rolls; mini-bosses roll one more.
    uint8 eliteAffixes = 1;
    // Time between a charger's charges.
    uint32 chargeIntervalMs = 8000;
    // Class Mastery experience for the class played.
    uint32 masteryXpMinute = 10;        // per minute survived
    uint32 masteryXpMiniboss = 15;      // per mini-boss killed
    uint32 masteryXpBoss = 40;          // per stage boss killed

    std::vector<uint32> mapIds;     // the pool, bonk_map ids
};

enum BonkWeaponTarget : uint8
{
    BONK_TARGET_NEAREST   = 0,      // fire at the closest enemies
    BONK_TARGET_SELF_AREA = 1,      // area spell around the player
    BONK_TARGET_RANDOM    = 2,      // fire at random enemies in range
    BONK_TARGET_GROUND    = 3,      // ground effect dropped where the most enemies stand
    BONK_TARGET_STRONGEST = 4,      // fire at the strongest enemies in range: bosses, then elites, then the most health
    BONK_TARGET_FARTHEST  = 5,      // fire at the farthest enemies in range
    BONK_TARGET_CONE      = 6,      // every enemy in a cone towards the nearest one
    BONK_TARGET_SELF      = 7,      // the hero: a buff, a trap at their feet, a pet
    MAX_BONK_TARGET
};

// Extra traits of a weapon (bonk_weapon.flags).
enum BonkWeaponFlags : uint32
{
    BONK_WEAPON_FLAG_RESET_ON_KILL    = 0x01,  // a kill with kill_below_pct makes it ready again at once
    BONK_WEAPON_FLAG_ALWAYS_CRIT      = 0x02,  // its hits always crit
    BONK_WEAPON_FLAG_REFRESH_DOTS     = 0x04,  // its hits renew the hero's damage over time on the enemy
    BONK_WEAPON_FLAG_KNOCKBACK        = 0x08,  // knocks the enemies it hits away from the hero
    BONK_WEAPON_FLAG_EXPLODE_ON_DEATH = 0x10,  // MARK: alt bursts where a marked enemy dies, and the mark moves on
    BONK_WEAPON_FLAG_KILL_WITH_ALT    = 0x20,  // kill_below_pct kills are cast with alt
};

float const BONK_CLEAVE_RADIUS    = 6.0f;   // a cleave or pierce hits enemies this close to its target
float const BONK_SPREAD_RADIUS    = 10.0f;  // a spread reaches enemies this close to the one that died

// Rarity, in WoW item colours. A level-up card for a spell rolls one, and it
// decides what that rank adds; Luck raises the odds.
enum BonkRarity : uint8
{
    BONK_RARITY_COMMON    = 0,  // white
    BONK_RARITY_UNCOMMON  = 1,  // green
    BONK_RARITY_RARE      = 2,  // blue
    BONK_RARITY_EPIC      = 3,  // purple
    BONK_RARITY_LEGENDARY = 4,  // orange
    MAX_BONK_RARITY
};

// What a card for a spell brings. Ranks are uncapped.
enum BonkRankStep : uint8
{
    BONK_STEP_NEW              = 0,     // the spell itself, rank 1
    BONK_STEP_RANK             = 1,     // Common to Epic: damage, and upgrades from the spell's list
    BONK_STEP_EVOLVE           = 2,     // Legendary: the spell evolves
    MAX_BONK_STEP
};

// What a Rare or Epic rank can add besides damage (docs/Spells.md, Upgrades
// by spell type). Every spell has its own list with a step size for each
// (bonk_weapon_upgrade), so no card is dead.
enum BonkSpellUpgrade : uint8
{
    BONK_SPELL_UPGRADE_NONE         = 0,
    BONK_SPELL_UPGRADE_CLEAVE       = 1,      // spell cleave: % of the hit to up to BONK_SPELL_CLEAVE_TARGETS enemies near the target
    BONK_SPELL_UPGRADE_HIT_TWICE    = 2,      // % chance a hit lands again, for damage only, with its own crit
    BONK_SPELL_UPGRADE_HASTE        = 3,      // % Haste for this spell only
    BONK_SPELL_UPGRADE_RETIRED      = 4,      // was radius (% bigger area or reach); spells no longer grow by rank, only Area sizes them
    BONK_SPELL_UPGRADE_DURATION     = 5,      // % longer
    BONK_SPELL_UPGRADE_MARK         = 6,      // % more damage taken from its mark
    BONK_SPELL_UPGRADE_PET_DAMAGE   = 7,      // % more damage from its beasts
    BONK_SPELL_UPGRADE_PET_SIZE     = 8,      // % bigger beasts
    MAX_BONK_SPELL_UPGRADE
};

// The most a spell can gain of each upgrade, 0 = no cap.
float const BONK_SPELL_UPGRADE_CAP[MAX_BONK_SPELL_UPGRADE] = { 0.0f, 50.0f, 50.0f, 40.0f, 0.0f, 0.0f, 15.0f, 0.0f, 0.0f };
// Spell cleave strikes this many enemies near each target, within BONK_CLEAVE_RADIUS.
uint32 const BONK_SPELL_CLEAVE_TARGETS = 2;

struct BonkWeaponUpgrade
{
    BonkSpellUpgrade upgrade = BONK_SPELL_UPGRADE_NONE;
    float step = 0.0f;
};

// Common to Epic: damage, %, and how many upgrade steps from the spell's list
// (an Epic's two can be the same). A step the spell has no room left for is
// BONK_RANK_SPARE_DAMAGE instead.
float const BONK_RANK_DAMAGE[BONK_RARITY_LEGENDARY]   = { 10.0f, 18.0f, 15.0f, 15.0f };
uint8 const BONK_RANK_UPGRADES[BONK_RARITY_LEGENDARY] = { 0, 0, 1, 2 };
uint8 const BONK_MAX_RANK_UPGRADES                    = 2;
float const BONK_RANK_SPARE_DAMAGE                    = 10.0f;
// Legendary ranks evolve the spell. They are offered once a spell is at
// BONK_EVOLVE_RANK, more likely every rank after and with Luck; a spell that
// has not evolved by BONK_EVOLVE_PITY_RANK always gets one. A spell that has
// evolved, or can't yet, gets no more. Spreading spells spread from
// BONK_EVOLVE_RANK on.
uint32 const BONK_EVOLVE_RANK                  = 8;
uint32 const BONK_EVOLVE_PITY_RANK             = 16;
float const BONK_LEGENDARY_BASE_CHANCE         = 5.0f;
float const BONK_LEGENDARY_CHANCE_PER_RANK     = 5.0f;
float const BONK_LEGENDARY_CHANCE_PER_LUCK     = 0.2f;
// Odds of Common to Epic as weights, and how much each point of Luck raises
// each weight, %.
float const BONK_RARITY_WEIGHT[BONK_RARITY_LEGENDARY] = { 55.0f, 30.0f, 11.0f, 4.0f };
float const BONK_RARITY_LUCK[BONK_RARITY_LEGENDARY]   = { 0.0f, 1.0f, 2.0f, 3.0f };

char const* GetBonkRarityName(BonkRarity rarity);

// What a weapon does beyond casting its spell. `alt` is its alt_spell_id.
enum BonkWeaponBehavior : uint8
{
    BONK_BEHAVIOR_NONE          = 0,
    BONK_BEHAVIOR_CLEAVE        = 1,    // melee: each hit also strikes enemies next to its target (value: their share of the damage)
    BONK_BEHAVIOR_SPREAD        = 2,    // from rank 8, or once evolved: moves on to nearby enemies when one carrying it dies (value: how many)
    BONK_BEHAVIOR_ECHO          = 3,    // pulses again after value ms with a bigger area
    BONK_BEHAVIOR_SHIELD        = 4,    // casts nothing: gives the hero value % of max health as a spell Shield
    BONK_BEHAVIOR_SHIELD_PER_HIT = 5,   // value % of max health as a spell Shield for every enemy in reach
    BONK_BEHAVIOR_DELAYED       = 6,    // value ms later, alt bursts where each target or ground spot was
    BONK_BEHAVIOR_ORB           = 7,    // a slow orb moves ahead and casts the spell at enemies it passes (duration_ms, tick_ms)
    BONK_BEHAVIOR_FREEZE        = 8,    // alt (a root) on the enemies in front; crits on rooted enemies deal +50%
    BONK_BEHAVIOR_WARD          = 9,    // like SHIELD, and alt bursts around the hero when that Shield breaks
    BONK_BEHAVIOR_VULNERABLE    = 10,   // alt (a stacking debuff) on every enemy it hits
    BONK_BEHAVIOR_EXECUTE       = 11,   // alt_damage times the damage on enemies below value % health
    BONK_BEHAVIOR_SPIN          = 12,   // around the hero: pulses again every value ms for duration_ms, wherever the hero goes
    BONK_BEHAVIOR_LEECH         = 13,   // its damage over time heals the hero value % of the damage
    BONK_BEHAVIOR_MARK          = 14,   // enemies it hits take value % more damage from everything for duration_ms; alt is a look on them
    BONK_BEHAVIOR_DEMORALIZE    = 15,   // enemies wearing its spell deal value % less damage
    BONK_BEHAVIOR_BUFF          = 16,   // casts its spell on the hero: value % more Power for duration_ms
    BONK_BEHAVIOR_REVENGE       = 17,   // +value % Dodge; fires only after the hero dodges; duration_ms: hits taken that long after strike back
    BONK_BEHAVIOR_BURST         = 18,   // value more shots at each target, quickly after the first
    BONK_BEHAVIOR_TRAP          = 19,   // a trap (value: its gameobject) that goes off when an enemy steps on it; range is its radius
    BONK_BEHAVIOR_PET           = 20,   // beasts that fight for the hero: a hit's damage every tick_ms, value their size
    MAX_BONK_BEHAVIOR
};

// Shared debuffs every class feeds and pays off (docs/Spells.md, Keywords):
// one per keyword per enemy, carrying a stack count, always cast by the hero
// so all of a hero's spells add to the same stack (bonk_keyword).
enum BonkKeyword : uint8
{
    BONK_KEYWORD_NONE                 = 0,
    BONK_KEYWORD_CHILL                = 1,      // slower per stack; at its most stacks the enemy is Frozen
    BONK_KEYWORD_FROZEN               = 2,      // held in place; crits on it deal more (Shatter)
    BONK_KEYWORD_BURNING              = 3,      // damage over time per stack; spreads when its carrier dies
    BONK_KEYWORD_BLEED                = 4,
    BONK_KEYWORD_POISON               = 5,
    BONK_KEYWORD_FIRE_VULNERABILITY   = 6,      // more Fire damage taken per stack
    BONK_KEYWORD_SUNDER_ARMOR         = 7,      // more Physical damage taken per stack
    BONK_KEYWORD_SHADOW_VULNERABILITY = 8,      // more Shadow damage taken per stack
    BONK_KEYWORD_HUNTERS_MARK         = 9,      // more damage taken from everything
    BONK_KEYWORD_DAZED                = 10,     // slower
    MAX_BONK_KEYWORD,
    BONK_KEYWORD_VARIETY              = 255     // bonk_weapon.uses: every different keyword on the enemy counts
};

struct BonkKeywordInfo
{
    BonkKeyword id = BONK_KEYWORD_NONE;
    std::string name;
    std::string icon;               // Interface\Icons texture name, for the results screen
    uint32 spellId = 0;             // the debuff that carries it: its icon, text and look on the enemy
    uint8 maxStacks = 1;
    // % slower or more damage taken a stack; damage over time: a stack's
    // damage a second, as a share of a same-level enemy's health.
    float valuePerStack = 0.0f;
    uint32 tickMs = 0;              // damage over time: how often it ticks before Haste
    uint32 durationMs = 0;          // how long it stays after it was last applied
    bool spreadsOnDeath = false;    // half its stacks go to the nearest enemies when its carrier dies

    bool IsDamageOverTime() const
    {
        return id == BONK_KEYWORD_BURNING || id == BONK_KEYWORD_BLEED || id == BONK_KEYWORD_POISON;
    }
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
    // An evolved weapon replaces `evolvesFrom` when a Legendary rank of that
    // is picked; it is never offered otherwise.
    uint32 evolvesFrom = 0;
    float healPct = 0.0f;           // heals the player this % of max health per cast
    uint32 durationMs = 0;          // how long its spell lasts, 0 = the spell's own duration
    // Damage over time ticks this often instead of the spell's own rate, and
    // damageFraction is then its damage per second. 0 = keep the spell's.
    uint32 tickMs = 0;
    bool chains = false;            // jumps from enemy to enemy; Rare ranks add jumps
    BonkWeaponBehavior behavior = BONK_BEHAVIOR_NONE;
    float behaviorValue = 0.0f;     // see BonkWeaponBehavior
    uint8 baseTargets = 1;          // enemies it fires at before ranks and passives add more
    // A second spell: what a behavior casts, or, with altEvery, what every
    // altEvery-th cast is instead, for altDamage times the damage.
    uint32 altSpellId = 0;
    uint8 altEvery = 0;
    float altDamage = 1.0f;
    uint8 baseJumps = 0;            // pierces, bounces or cleaved enemies before ranks add more
    float killBelowPct = 0.0f;      // normal enemies below this % health die at once
    uint32 flags = 0;               // BonkWeaponFlags
    BonkKeyword applies = BONK_KEYWORD_NONE;    // put on every enemy each hit or tick strikes
    uint8 applyStacks = 0;                      // stacks of it per hit or tick
    // Pays off: usesPct % more damage for each stack of `uses` on the enemy,
    // or for each different keyword on it (BONK_KEYWORD_VARIETY).
    BonkKeyword uses = BONK_KEYWORD_NONE;
    float usesPct = 0.0f;
    std::vector<BonkWeaponUpgrade> upgrades;    // what its Rare and Epic ranks can add (bonk_weapon_upgrade)

    int32 damageEffectIndex = -1;   // effect whose base points carry the damage
    int32 tickEffectIndex = -1;     // damage over time besides damageEffectIndex (a hit that leaves it burning)
    float groundRadius = 0.0f;      // radius of its ground effect, if it has one
    bool weaponDamage = false;      // a melee or ranged attack: its damage replaces the weapon swing
    bool lasting = false;           // damage over time or a ground effect: ranks can make it last longer

    bool HasFlag(BonkWeaponFlags flag) const { return (flags & flag) != 0; }
    // Aimed at enemies one by one rather than an area around the hero or on the ground.
    bool IsAimed() const
    {
        return (target == BONK_TARGET_NEAREST || target == BONK_TARGET_RANDOM || target == BONK_TARGET_STRONGEST || target == BONK_TARGET_FARTHEST) &&
            behavior != BONK_BEHAVIOR_CLEAVE;
    }
    // Area spells, ground spells, cones, cleaves and traps grow in area or reach.
    bool HasArea() const
    {
        return target == BONK_TARGET_SELF_AREA || target == BONK_TARGET_GROUND || target == BONK_TARGET_CONE ||
            behavior == BONK_BEHAVIOR_CLEAVE || behavior == BONK_BEHAVIOR_TRAP;
    }
    // The step its ranks add of `upgrade`, 0 when that isn't on its list.
    float GetUpgradeStep(BonkSpellUpgrade upgrade) const
    {
        for (BonkWeaponUpgrade const& itr : upgrades)
            if (itr.upgrade == upgrade)
                return itr.step;
        return 0.0f;
    }
    // Count adds a target, a ground drop, a trap or a beast.
    bool HasTargets() const
    {
        if (target == BONK_TARGET_SELF)
            return behavior == BONK_BEHAVIOR_TRAP || behavior == BONK_BEHAVIOR_PET;
        return target != BONK_TARGET_SELF_AREA && target != BONK_TARGET_CONE;
    }
    // What Count adds one of.
    char const* GetTargetNoun() const
    {
        if (behavior == BONK_BEHAVIOR_TRAP)
            return "trap";
        if (behavior == BONK_BEHAVIOR_PET)
            return "beast";
        return target == BONK_TARGET_GROUND ? "pack" : "target";
    }
    // `count` of them, for tooltips: "3 targets", "lands on 2 packs".
    std::string DescribeTargets(uint32 count) const
    {
        std::string const noun = std::to_string(count) + " " + GetTargetNoun() + (count == 1 ? "" : "s");
        return target == BONK_TARGET_GROUND ? "lands on " + noun : noun;
    }
};

// `amount` of an upgrade on `weapon`, in tooltip words: "+10% radius",
// "passes through 2 additional enemies".
std::string DescribeBonkSpellUpgrade(BonkWeapon const& weapon, BonkSpellUpgrade upgrade, float amount);

// The hero's stats. Every source adds into one total per stat: worn gear,
// camp upgrades, Class Mastery and in-run picks (passives).
enum BonkPassiveEffect : uint8
{
    BONK_PASSIVE_POWER       = 0,   // + damage dealt, %
    BONK_PASSIVE_HASTE       = 1,   // spells fire and tick more often: interval / (1 + Haste), %
    BONK_PASSIVE_AMOUNT      = 2,   // Count: + bolts, ground drops, traps, beasts and targets; capped at BONK_MAX_COUNT
    BONK_PASSIVE_MOVE_SPEED  = 3,   // + run speed, %
    BONK_PASSIVE_MAX_HEALTH  = 4,   // + max health, %
    BONK_PASSIVE_RECOVERY    = 5,   // health regained per second, % of max
    BONK_PASSIVE_GREED       = 6,   // + gold, %
    BONK_PASSIVE_GROWTH      = 7,   // + experience, %
    BONK_PASSIVE_PICKUP      = 8,   // + range experience orbs and turkey legs are picked up from, %
    BONK_PASSIVE_LEECH       = 9,   // weapon damage dealt comes back as health, %
    BONK_PASSIVE_AREA        = 10,  // + area, reach and ground effect size, %; capped at BONK_MAX_AREA
    BONK_PASSIVE_DURATION    = 11,  // + how long lasting spells (damage over time, ground effects) last, %
    BONK_PASSIVE_ARMOR       = 12,  // damage taken / (1 + Armor / 100), points; no cap
    BONK_PASSIVE_HIT         = 13,  // - elite and boss dodge chance, %; capped at BONK_MAX_HIT
    BONK_PASSIVE_DODGE       = 14,  // + chance to dodge enemy melee, %; capped at BONK_MAX_DODGE
    BONK_PASSIVE_CRIT        = 15,  // + crit chance, %; past 100% a hit can crit twice
    BONK_PASSIVE_CRIT_DAMAGE = 16,  // + crit bonus, %: on top of a spell crit's 50% and a weapon crit's 100%
    BONK_PASSIVE_SHIELD      = 17,  // absorb before health, % of max health; refills after BONK_SHIELD_REGEN_DELAY_MS unhurt
    BONK_PASSIVE_LUCK        = 18,  // better rarity on chests, shrines and level ups (points, not %)
    MAX_BONK_PASSIVE_EFFECT
};

// Stat caps and bases. Level-ups stop offering a passive for a stat at its cap.
float const BONK_MAX_DODGE          = 60.0f;
float const BONK_MAX_HIT            = 100.0f;   // past it Hit is wasted
float const BONK_MAX_COUNT          = 5.0f;
float const BONK_MAX_AREA           = 50.0f;    // spells grow at most half again their size
float const BONK_MAX_HASTE          = 100.0f;   // a spell fires at most twice as fast, every source together
// Count: a spell around the hero or a cone goes off at most this many more
// times a cast (bolts and ground spells take the full Count).
uint32 const BONK_MAX_COUNT_PULSES  = 2;
float const BONK_MAX_GEAR_HASTE     = 20.0f;    // Haste from worn gear counts up to this
float const BONK_BASE_CRIT          = 5.0f;     // every hero's crit chance before stats
float const BONK_BOSS_EXTRA_DODGE   = 5.0f;     // mini-bosses and bosses dodge this much more than elites
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
    uint32 excludes = 0;            // a passive a run can't have alongside this one, either way round
};

// How an enemy fights, by creature entry (bonk_mob); entries without a row
// walk up and hit.
enum BonkMobRole : uint8
{
    BONK_ROLE_MELEE    = 0,
    BONK_ROLE_RANGED   = 1,     // stops `range` yards away and casts
    BONK_ROLE_EXPLODER = 2,     // runs in and, within `range` yards, casts its self abilities and is gone
    MAX_BONK_MOB_ROLE
};

enum BonkAbilityTarget : uint8
{
    BONK_ABILITY_PLAYER = 0,    // cast at the player
    BONK_ABILITY_SELF   = 1,    // on the enemy itself: areas around it, buffs
};

// A spell an enemy casts on a cooldown (bonk_mob_ability).
struct BonkMobAbility
{
    uint32 spellId = 0;
    BonkAbilityTarget target = BONK_ABILITY_PLAYER;
    uint32 cooldownMs = 8000;
    uint32 firstMs = 3000;          // before the first cast
    float minRange = 0.0f;          // only when the player is at least this far...
    float maxRange = 30.0f;         // ...and at most this far away
    float damage = 0.0f;            // hit damage in the enemy's melee hits, 0 = the spell's own
    int32 damageEffectIndex = -1;
};

struct BonkMob
{
    BonkMobRole role = BONK_ROLE_MELEE;
    float range = 0.0f;
    std::vector<BonkMobAbility> abilities;
};

// Money as players read it: "1g 20s", leaving out empty parts.
std::string FormatBonkMoney(uint32 copper);
// "1 Darkmoon Ticket", "3 Darkmoon Tickets" (Talent Shards before the renames).
std::string FormatBonkShards(uint32 count);

// The camp shops. With the addon, each shows its wares as talent trees.
enum BonkShop : uint8
{
    BONK_SHOP_NONE     = 0,
    BONK_SHOP_UPGRADES = 1,         // quartermaster: bonk_upgrade
    BONK_SHOP_TALENTS  = 2,         // talent trainer: bonk_talent
    BONK_SHOP_MASTERY  = 3,         // mastery trainer: bonk_mastery, the hero's class only
};

uint8 const BONK_SHOP_ROWS    = 7;
uint8 const BONK_SHOP_COLUMNS = 4;

struct BonkShopTree
{
    uint32 id = 0;
    BonkShop shop = BONK_SHOP_NONE;
    std::string name;
    std::string background;         // Interface\TalentFrame art, e.g. "WarriorArms"
};

// Where a purchase sits in its shop's trees, and what must be bought first.
struct BonkShopNode
{
    std::string icon;               // Interface\Icons file name
    uint32 tree = 0;                // bonk_shop_tree id; 0 = not shown in the trees
    uint8 row = 1;
    uint8 column = 1;
    uint32 requiredId = 0;          // another purchase of the same shop
    uint8 requiredRank = 0;         // ranks of it needed
};

// Permanent upgrade bought at camp; every rank adds to a passive effect in
// every run.
struct BonkUpgrade
{
    uint32 id = 0;
    std::string name;
    std::string description;
    BonkPassiveEffect effect = BONK_PASSIVE_POWER;
    float valuePerRank = 5.0f;
    uint8 maxRank = 5;
    uint32 cost = 5000;             // copper for the first rank
    float costGrowth = 1.4f;        // each rank costs this many times the one before
    BonkShopNode layout;

    uint32 GetCost(uint8 ownedRanks) const;
};

enum BonkTalentEffect : uint8
{
    // Difficulty toggles: switched on and off at camp, each pays bonus gold.
    BONK_TALENT_ENEMY_HEALTH = 0,   // + enemy health, %
    BONK_TALENT_ENEMY_DAMAGE = 1,   // + enemy damage, %
    BONK_TALENT_SPAWN_RATE   = 2,   // + enemies per wave, %
    BONK_TALENT_ELITE_RATE   = 3,   // + elites, %
    // Perks: always on once bought.
    BONK_TALENT_REROLL       = 4,   // rerolls of an upgrade choice per run
    BONK_TALENT_BANISH       = 5,   // upgrades that can be banished per run
    BONK_TALENT_REVIVE       = 6,   // revives per run
    BONK_TALENT_START_COINS  = 7,   // Valor at the start of a run
    BONK_TALENT_SKIP         = 8,   // upgrade choices that can be skipped per run, for a little Valor
    MAX_BONK_TALENT_EFFECT
};

// Run unlock bought at camp with Talent Shards.
struct BonkTalent
{
    uint32 id = 0;
    std::string name;
    std::string description;
    BonkTalentEffect effect = BONK_TALENT_REROLL;
    float valuePerRank = 1.0f;
    float goldBonus = 0.0f;         // toggles: + gold while switched on, %
    uint8 maxRank = 1;
    uint32 cost = 1;                // Talent Shards for the first rank
    float costGrowth = 1.0f;
    BonkShopNode layout;

    bool IsToggle() const { return effect <= BONK_TALENT_ELITE_RATE; }
    uint32 GetCost(uint8 ownedRanks) const;
};

// Everything a character brings into a run from camp.
struct BonkRunBonuses
{
    float passives[MAX_BONK_PASSIVE_EFFECT] = {};
    float enemyHealth = 0.0f;       // %
    float enemyDamage = 0.0f;       // %
    float spawnRate = 0.0f;         // %
    float eliteRate = 0.0f;         // %
    float goldBonus = 0.0f;         // %
    uint32 rerolls = 0;
    uint32 banishes = 0;
    uint32 skips = 0;
    uint32 revives = 0;
    uint32 startCoins = 0;
};

// Camp gear: what one item adds to each stat while worn.
struct BonkItemStat
{
    float stats[MAX_BONK_PASSIVE_EFFECT] = {};
    uint32 setId = 0;               // bonk_item_set, 0 = none
};

struct BonkItemSetBonus
{
    uint8 pieces = 2;               // pieces worn to get it
    float stats[MAX_BONK_PASSIVE_EFFECT] = {};
    std::string description;
};

struct BonkItemSet
{
    uint32 id = 0;
    std::string name;
    std::vector<uint32> items;              // its pieces
    std::vector<BonkItemSetBonus> bonuses;  // fewest pieces first
};

// Everything a player's worn gear adds up to, set bonuses included.
struct BonkGearTotals
{
    float passives[MAX_BONK_PASSIVE_EFFECT] = {};
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
        // The weapon `weaponId` evolves into, if any.
        BonkWeapon const* GetEvolution(uint32 weaponId) const;

        // How `spellId` flies, if it is a projectile.
        BonkProjectileInfo const* GetProjectile(uint32 spellId) const;

        BonkKeywordInfo const* GetKeyword(BonkKeyword id) const;
        // The keyword whose debuff is `spellId`, if any.
        BonkKeywordInfo const* GetKeywordBySpell(uint32 spellId) const;
        std::map<uint8, BonkKeywordInfo> const& GetKeywords() const { return m_keywords; }

        BonkMob const* GetMob(uint32 entry) const;

        BonkArchetypeInfo const& GetArchetype(BonkArchetype archetype) const { return m_archetypes[archetype]; }
        // The spawn curve, by second into a stage.
        std::vector<BonkSpawnPoint> const& GetSpawnCurve() const { return m_spawnCurve; }

        BonkPassive const* GetPassive(uint32 id) const;
        // Its description, and which passive it can't be taken with.
        std::string DescribePassive(BonkPassive const& passive) const;
        std::map<uint32, BonkPassive> const& GetPassives() const { return m_passives; }

        std::vector<BonkPowerup> const& GetPowerups() const { return m_powerups; }

        std::map<uint32, BonkShopTree> const& GetShopTrees() const { return m_shopTrees; }

        BonkUpgrade const* GetUpgrade(uint32 id) const;
        std::map<uint32, BonkUpgrade> const& GetUpgrades() const { return m_upgrades; }

        BonkTalent const* GetTalent(uint32 id) const;
        std::map<uint32, BonkTalent> const& GetTalents() const { return m_talents; }

        // Camp gear: what an item adds while worn, and the sets it belongs to.
        BonkItemStat const* GetItemStat(uint32 itemEntry) const;
        std::map<uint32, BonkItemStat> const& GetItemStats() const { return m_itemStats; }
        BonkItemSet const* GetItemSet(uint32 id) const;
        std::map<uint32, BonkItemSet> const& GetItemSets() const { return m_itemSets; }
        BonkGearTotals EvaluateGear(Player const* player) const;

        // Arena maps start empty: the run spawns every enemy itself.
        bool IsStaticCreatureSuppressed(Map const* map, uint32 dbGuid) const;
        bool IsArenaLocation(uint32 mapId, uint32 zoneId) const;

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
        void LoadShopTrees();
        void LoadUpgrades();
        void LoadTalents();
        void LoadItemStats();
        void LoadMobs();
        void LoadArchetypes();
        void LoadSpawnCurve();

        // Drops tree slots and requirements that don't fit, with a log line each.
        template <class Entry>
        void CheckShopLayout(char const* table, BonkShop shop, std::map<uint32, Entry>& entries) const;

        std::map<uint32, BonkTier> m_tiers;
        std::map<uint32, BonkMap> m_maps;
        std::map<uint32, BonkWeapon> m_weapons;
        std::map<uint8, BonkKeywordInfo> m_keywords;
        std::map<uint32, BonkProjectileInfo> m_projectiles;     // by spell id
        std::map<uint32, BonkPassive> m_passives;
        std::vector<BonkPowerup> m_powerups;
        std::map<uint32, BonkShopTree> m_shopTrees;
        std::map<uint32, BonkUpgrade> m_upgrades;
        std::map<uint32, BonkTalent> m_talents;
        std::map<uint32, BonkItemStat> m_itemStats;
        std::map<uint32, BonkItemSet> m_itemSets;
        std::map<uint32, BonkMob> m_mobs;
        BonkArchetypeInfo m_archetypes[MAX_BONK_ARCHETYPE];
        std::vector<BonkSpawnPoint> m_spawnCurve;   // by second
        std::map<uint32, std::set<uint32>> m_arenaZonesByMap;   // zone 0 = whole map
};

#define sBonkMgr MaNGOS::Singleton<BonkMgr>::Instance()

#endif
