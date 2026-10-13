/*
 * Vanilla Bonk: one run. The character starts at level 1 and plays a chain
 * of stages, each on a map from the chosen tier's pool. Enemies spawn
 * around them in growing waves, weapons fire on their own, and every level
 * up or opened chest pauses the run to offer three upgrades, and power-ups
 * lying around give short buffs. When a stage's
 * timer runs out its boss arrives; killing it stops the spawns and opens a
 * portal to the next, harder stage. The run is endless: it ends when the
 * character dies or goes home through a portal. Gold earned is kept either
 * way. What the character bought at camp (upgrades, talents, gear) and its
 * class's Mastery are applied for the whole run, which in turn earns that
 * class Mastery experience.
 *
 * Kills drop experience orbs that fly to the player once they are close.
 * Spell ranks are uncapped: each card rolls a rarity that decides what the
 * rank adds, and from rank 8 a Legendary rank evolves the spell. A run
 * holds 5 spells and 6 passives.
 */

#ifndef MANGOS_BONKRUN_H
#define MANGOS_BONKRUN_H

#include "BonkMastery.h"
#include "BonkMgr.h"
#include "BonkTelegraph.h"
#include "ObjectGuid.h"
#include "nonstd/optional.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

class Creature;
class GameObject;
class Player;
class SpellAuraHolder;
class SpellCaster;
class Unit;

class BonkRun
{
    public:
        BonkRun(Player* player, BonkTier const& tier);

        bool Begin(std::string& error);
        void Update(uint32 diff);
        void Abort();       // logout: clean up without moving the player
        void Surrender();   // `.bonk stop`
        bool SkipTime(uint32 seconds, std::string& error);  // `.bonk skip`, GM testing

        bool IsActive() const { return m_state == STATE_RUNNING; }
        bool IsPaused() const { return m_paused; }
        bool IsFinished() const { return m_state == STATE_FINISHED; }
        bool IsPortalOpen() const { return m_state == STATE_PORTAL; }
        BonkMap const& GetMap() const { return m_map; }
        BonkTier const& GetTier() const { return m_tier; }
        uint32 GetStage() const { return m_stage; }

        void OnMobKilled(Creature* mob, BonkSpawnKind kind);
        // The enemy cannot walk to the player (spawned in a tree, on a roof).
        void OnMobStuck(Creature* mob);
        // Damage the player's weapons took off an enemy, for Leech.
        void OnWeaponDamage(uint32 damage);
        // An enemy's hit on the hero: Armor takes its share off, then the
        // Shield soaks what it can. Returns what is left for health.
        uint32 MitigateDamage(uint32 damage);
        // A Necrotic enemy hit the hero: healing is weaker for a while.
        void AddNecroticStack();
        // Power and the class's Mastery bonuses for `spell` (null for melee
        // swings) against `victim`, added up: applies to all damage the
        // player deals (SpellCaster::SpellDamageBonusDone and
        // MeleeDamageBonusDone).
        float GetDamageMultiplier(SpellEntry const* spell, Unit const* victim) const;

        // The run whose hero, or the hero's pet, attacks one of that run's
        // enemies; null for every other fight. Such attacks skip the normal
        // hit tables: they always land, except that elites and bosses dodge
        // (RollDodge), and they crit by the hero's Crit (GetCritChance).
        static BonkRun* FromAttack(SpellCaster const* attacker, Unit const* victim);
        // Elites and bosses dodge by the tier's chance, less the hero's Hit.
        bool RollDodge(Unit const* victim) const;
        // `spell` (null for a swing): some spells always crit, and so does
        // everything during Recklessness.
        float GetCritChance(SpellEntry const* spell = nullptr) const;
        // Times an enemy's damage to the hero: shouts make it weaker.
        float GetMobDamageMultiplier(Creature const* mob) const;
        // The hero dodged a swing (Revenge), or took a hit (Retaliation).
        void OnHeroDodge();
        void OnHeroStruck(Creature* attacker);
        // A hero's damage over time ticked on an enemy (Bloodthirst).
        void OnPeriodicDamage(uint32 spellId, uint32 damage);

        // Keywords (BonkKeywords.cpp). A hit or tick of one of the hero's
        // spells on an enemy (BonkOnDamage): the keyword the spell applies
        // goes on it at the next update.
        void OnWeaponHit(Unit* victim, SpellEntry const* spell);
        // The hero's hit about to kill an enemy: its damage over time stacks
        // are noted, as the core strips its debuffs before it counts as dead.
        void OnKillingBlow(Unit* victim);
        // Puts `stacks` of `keyword` on a run enemy (`.bonk keyword` too).
        bool ApplyKeyword(Creature* mob, BonkKeyword keyword, uint32 stacks);
        uint32 GetKeywordStacks(Unit const* victim, BonkKeyword keyword) const;
        // A hero's stun on a run enemy (Spell::DoSpellHitOnUnit): cut to at
        // most BONK_MAX_HOLD_MS; false while the enemy is still immune after
        // its last one.
        bool LimitStun(Creature* mob, int32& duration);
        // A red ground warning for an enemy ability; `onFire` runs when it
        // fills. Warnings are dropped, unfired, when the run pauses or the
        // stage ends.
        void AddTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
            std::function<void()> onFire = nullptr);
        // Volcanic: fire bursts up where a warning was, and the enemy that
        // called it hits the hero for `damage` if they are still inside.
        void Erupt(ObjectGuid mobGuid, float x, float y, float z, float radius, uint32 damage);
        // Casters' bolts fly at a spot, not at the hero: an invisible
        // target there for the enemy's spell (it takes the hit, the hero
        // never sees it), and the bolt landing: `damage` from `spellId` if
        // the hero is still within `radius`.
        ObjectGuid PlaceBoltTarget(float x, float y, float z, uint32 lifetimeMs);
        void LandBolt(ObjectGuid mobGuid, ObjectGuid targetGuid, uint32 spellId, float x, float y, float radius, uint32 damage);
        // Damage times this on a crit: a spell crit adds 50%, a weapon crit
        // 100%, plus Crit Damage, and 50% more on rooted enemies with a
        // freezing spell. Past 100% crit chance, the rest is the chance the
        // hit crits a second time.
        float RollCritMultiplier(bool weaponAttack, Unit const* victim = nullptr) const;

        // Stat totals without worn gear (the addon adds gear itself), for
        // the character sheet.
        void SendStats() const;
        // `.bonk scaling`: enemy health right now, and how many hits each
        // spell needs to kill a grunt, a bruiser and an elite.
        std::vector<std::string> DescribeScaling() const;
        bool Pick(uint32 choice, std::string& error);
        bool Reroll(std::string& error);
        bool Banish(uint32 choice, std::string& error);
        // Turns the choice down for a little Valor.
        bool Skip(std::string& error);
        // Chests: a locked card comes back at the next chest if not picked.
        bool Lock(uint32 choice, std::string& error);

        // Chests and the portal call these when the player uses them.
        void UseChest(GameObject* chest);
        bool IsOwnPortal(GameObject const* portal) const;
        BonkMap const* GetNextMap() const;
        void EnterNextStage();
        void GoHome();

        // Everything the addon shows, for when it (re)connects mid-run.
        void SendFullState() const;

    private:
        enum State
        {
            STATE_ENTERING,     // teleporting into a stage
            STATE_RUNNING,
            STATE_PORTAL,       // stage boss dead, waiting at the portal
            STATE_ENDING,       // results on screen, about to return to camp
            STATE_FINISHED,
        };

        enum OfferSource
        {
            OFFER_LEVEL,
            OFFER_CHEST,
        };

        // A spell the hero has. Its ranks are uncapped; each added what its
        // rarity rolled (BonkRankStep), kept here.
        struct OwnedWeapon
        {
            BonkWeapon weapon;
            uint32 rank = 1;
            float damageBonus = 0.0f;       // %
            float hasteBonus = 0.0f;        // %
            float areaBonus = 0.0f;         // %
            float durationBonus = 0.0f;     // %
            float rangeBonus = 0.0f;        // %
            uint32 extraTargets = 0;        // targets, or drops for ground spells
            uint32 extraJumps = 0;          // pierces, bounces, spreads or cleaved enemies
            uint8 epicSteps = 0;            // Epic ranks that added a target
            std::string rarities;           // the BonkRarity each rank after the first rolled, one digit per rank
            uint32 casts = 0;               // for every altEvery-th cast
            int32 timer = 0;
            std::map<ObjectGuid, uint32> recentTargets;     // random-target weapons: enemy -> ms hit
            std::map<ObjectGuid, uint32> carriers;          // spreading weapons: enemy -> ms its spell wears off
            std::vector<ObjectGuid> pets;                   // beasts it called
            bool killed = false;                            // a kill this cast, for RESET_ON_KILL
        };

        struct Offer
        {
            bool isWeapon = true;
            uint32 id = 0;              // the weapon (as owned, before evolving) or passive
            uint32 newLevel = 1;        // the rank it brings
            BonkRarity rarity = BONK_RARITY_COMMON;
            BonkRankStep step = BONK_STEP_NEW;     // weapons: what the rank adds
            bool locked = false;        // chests: comes back at the next chest if not picked
        };

        struct Orb
        {
            ObjectGuid guid;
            uint32 experience = 0;
            bool pulled = false;        // flying to the player
            float aimX = 0.0f, aimY = 0.0f, aimZ = 0.0f;    // where it is flying
        };

        // Talent Shards lying where an enemy died.
        struct ShardDrop
        {
            ObjectGuid guid;
            uint32 count = 0;
            ObjectGuid marker;          // wears the shard's beam
        };

        BonkMap const* ChooseNextMap();
        bool TeleportToStage(BonkMap const& map, std::string& error);
        void StartStage();
        void UpdateRunning(uint32 diff);
        void UpdateOffers();
        void UpdateEvents();
        void UpdateSpawns(uint32 diff);
        void UpdateWeapons(uint32 diff);
        void UpdateBoundary(uint32 diff);
        void UpdateNatives(uint32 diff);
        void UpdateRecovery(uint32 diff);
        // Refills the Shield after a while unhurt and keeps its look and the
        // addon's bar in step.
        void UpdateShield(uint32 diff);
        uint32 GetMaxShield() const;
        // Necrotic stacks clear once no living Necrotic enemy is in range.
        void UpdateNecrotic(uint32 diff);
        void SetNecroticStacks(uint32 stacks);
        // What a heal on the hero comes to after Necrotic.
        uint32 ReduceHealing(uint32 heal) const;
        // Affixes that act when an elite dies: Bolstering and Sanguine.
        void OnAffixedDeath(Creature* mob);
        void UpdatePools(uint32 diff);
        void DespawnPools();
        void RunEvent(BonkMapEvent const& event);
        void OpenPortal(Creature* boss);
        void PlacePortal(float x, float y, float baseZ);

        void PruneMobs();
        enum class BruiserPick { ANY, NONE, ONLY };
        // An enemy of `kind` whose time has come; normal enemies also wait
        // for their archetype's time.
        BonkMapCreature const* PickCreature(BonkSpawnKind kind, BruiserPick bruisers = BruiserPick::ANY) const;
        // A normal enemy for the crowd: a bruiser by the spawn curve's
        // share, while there is room for one.
        BonkMapCreature const* PickTrash() const;
        uint32 CountTrash() const;
        uint32 CountBruisers() const;
        // The spawn curve right now (by stage time): the point in force and
        // the crowd to keep alive, at most the tier's cap; past the cap the
        // rest makes enemies tougher (GetCrowdOverflow).
        BonkSpawnPoint const& GetSpawnPoint() const;
        float GetCurveMinAlive() const;
        uint32 GetSpawnTarget() const;
        float GetCrowdOverflow() const;
        // No elites or events within 30 seconds of the mini-boss or the boss.
        bool IsQuietTime() const;
        // An event that waits out a quiet time.
        static bool IsQuietEvent(BonkEventType type);
        uint32 CountAlive(uint32 entry) const;
        bool FindSpawnPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const;
        bool FindSpawnPositionAt(float angle, float distance, float& x, float& y, float& z) const;

        // Where the player is heading: their movement over the last second or
        // two, smoothed. False while they stand still.
        void UpdateHeading(uint32 diff);
        bool GetHeading(float& angle) const;
        // The heading, or where the player faces while standing still.
        float GetForwardAngle() const;
        // "Ahead": in a cone around `forward`. Only so many enemies may be
        // there at once (GetFrontCap); new ones go to the sides or behind.
        bool IsAhead(float x, float y, float forward) const;
        uint32 CountAhead(float forward) const;
        uint32 GetFrontCap() const;
        // A spot for an enemy that appears (spawned, or moved out of a
        // tree): anywhere around the player while there is room ahead. Ahead
        // it is far off, to walk in from the edge of sight; elsewhere it is
        // `minDistance` to `maxDistance` away.
        bool FindMobPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const;
        // Enemies left far behind (whether or not they still move: the map
        // stops updating those far enough away) come back ahead of the player
        // in packs, spaced so there is a way between them; bosses alone. With
        // no room ahead for a while, they come back at the player's side.
        void UpdateStragglers(uint32 diff);
        bool PlacePack(std::vector<Creature*> const& pack, float heading);
        bool PlaceAside(Creature* mob, float heading);
        void MoveMob(Creature* mob, float x, float y, float z);
        Creature* SpawnMob(BonkMapCreature const& creature);
        Creature* SpawnMobAt(BonkMapCreature const& creature, float x, float y, float z);
        uint32 SpawnFormation(BonkMapEvent const& event);
        // Alive enemies within `range` of `center` (the player if none), nearest first.
        std::vector<Creature*> GetMobsInRange(float range, WorldObject const* center = nullptr) const;
        void DespawnMobs();

        GameObject* PlaceObject(uint32 entry, float x, float y, float z, uint32 lifetimeSeconds);
        // `choices`: cards it offers when opened.
        bool SpawnChest(bool free, float x, float y, float z, uint32 choices = 3);
        // Elites drop a chest by chance, better with every affix; mini-bosses
        // and bosses always do.
        void DropChest(Creature const* mob, BonkSpawnKind kind);
        bool SpawnChestNearby();
        uint32 GetChestCost() const;
        void SpawnPowerup();
        void UpdatePowerups(uint32 diff, bool spawn);
        void DespawnObjects();

        uint32 GetMobLevel() const;
        // What an enemy spawned now gets, before affixes.
        uint32 GetSpawnHealth(BonkSpawnKind kind, BonkArchetype archetype, float creatureMultiplier) const;
        float GetMobHealth(uint32 level) const;
        float GetStageHealthMultiplier() const;
        float GetStageDamageMultiplier() const;
        // Minutes fought this run, over all stages: what enemies scale with.
        float GetRunMinutes() const { return m_totalMs / float(MINUTE * IN_MILLISECONDS); }
        float GetPassiveTotal(BonkPassiveEffect effect) const;
        // The total after its cap (Armor, Dodge, Hit).
        float GetStat(BonkPassiveEffect effect) const;
        // 1 + Haste: how many times as often spells fire and tick.
        float GetHasteRate() const;
        float GetWeaponDamage(OwnedWeapon const& owned) const;
        uint32 GetWeaponCooldown(OwnedWeapon const& owned) const;
        // How much its area, reach and spreading grow: level and the Area passive.
        float GetWeaponArea(OwnedWeapon const& owned) const;
        // Enemies it fires at (projectiles, melee targets, damage over time).
        uint32 GetWeaponTargets(OwnedWeapon const& owned) const;
        void FireWeapon(OwnedWeapon& owned);
        // Base points for `damageShare` of the weapon's damage. Ground ticks hit
        // as hard as the strike; a hit's burn adds up to one more hit.
        void GetBasePoints(OwnedWeapon const& owned, SpellEntry const* spellInfo, float damageShare, nonstd::optional<int32>* basePoints) const;
        // Casts the weapon's spell on `target` for `damageShare` of its damage,
        // from the hero or from `source` (a projectile) on the hero's behalf.
        // A follow-up (a burst's later shots) sets nothing else off.
        void StrikeWith(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float damageShare, bool followUp = false,
            Creature* source = nullptr);
        // Normal enemies below the weapon's kill_below_pct die to its hit.
        bool CanKill(BonkWeapon const& weapon, Creature const* target) const;
        // Bosses first, then mini-bosses and elites, then the most health.
        void SortStrongestFirst(std::vector<Creature*>& mobs) const;
        // Enemies in a cone of `range` towards the nearest one.
        std::vector<Creature*> GetMobsInCone(OwnedWeapon const& owned, float range) const;
        void KnockBack(Creature* mob);
        // Marks: the extra damage an enemy takes, and what happens when one dies.
        float GetMarkBonus(Unit const* victim) const;
        void OnMarkedDeath(Creature const* mob);
        // Battle Shout and the like: Power while it lasts, and Recklessness.
        float GetBuffPower() const;
        bool IsReckless() const;
        // Traps lie where they were dropped until an enemy steps on one.
        void DropTraps(OwnedWeapon& owned);
        void UpdateTraps(uint32 diff);
        void SpringTrap(OwnedWeapon& owned, float x, float y, float z);
        void DespawnTraps();
        // Beasts fight beside the hero for as long as the spell is held.
        void UpdatePets(OwnedWeapon& owned);
        void UpdatePetTargets(uint32 diff);
        void DespawnPets();
        // A spell's area scaled so its radius comes to `radius`.
        void ScaleRadius(SpellEntry const* spellInfo, float radius);
        // A spreading weapon's spell jumps from an enemy that died carrying it.
        void SpreadFrom(Creature const* dead);
        bool HasBehavior(BonkWeaponBehavior behavior) const;
        // Behaviors that act after the cast: echoes and delayed bursts wait
        // here, orbs travel; spell Shields stack on the hero's Shield.
        void UpdatePendingCasts(uint32 diff);
        void LaunchOrb(OwnedWeapon& owned);
        void UpdateWeaponOrbs(uint32 diff);
        void DespawnWeaponOrbs();
        // Projectiles: a spell with a bonk_projectile row flies from `from`
        // towards where `target` will be, and hits the enemies it touches.
        // A spell that hits several enemies at once (Multi-Shot) fires that
        // many in a fan; the rest pass through the weapon's pierces.
        void LaunchProjectiles(OwnedWeapon& owned, BonkProjectileInfo const& info, SpellEntry const* spellInfo, float damageShare,
            WorldObject const* from, Creature* target);
        void LaunchProjectile(OwnedWeapon& owned, BonkProjectileInfo const& info, uint32 spellId, float damageShare,
            WorldObject const* from, float angle, uint32 pierce);
        void UpdateProjectiles(uint32 diff);
        void DespawnProjectiles();
        // The direction from `from` that meets `target` where it will be.
        float GetLeadAngle(WorldObject const* from, Unit const* target, float speed) const;
        void AddSpellShield(uint32 amount);
        void CastSelfArea(OwnedWeapon& owned, SpellEntry const* spellInfo, float areaScale);
        void AddWeapon(BonkWeapon const& weapon);
        void ApplyPassives();

        // Class Mastery: per-spell bonuses by root weapon (evolutions count
        // as the weapon they grew from), Soul Siphon, and the experience the
        // run earns, handed over once when it ends.
        uint32 GetRootWeapon(BonkWeapon const& weapon) const;
        void SoulSiphon(uint32 killingSpell);
        void AwardMastery();

        void GiveExperience(uint32 xp);
        void GiveGold(uint32 copper);

        void DropOrb(Creature const* mob, uint32 experience, BonkOrb colour);
        void UpdateOrbs(uint32 diff);
        void CollectAllOrbs();
        void DespawnOrbs();

        // Kept when taken; the stage's leftovers are taken for the player when
        // it leaves through the portal, and lost when it dies.
        void DropShards(Creature const* mob, BonkSpawnKind kind);
        void UpdateShards(uint32 diff);
        void CollectAllShards();
        // Turkey legs: a few enemies drop one; walking over it while hurt
        // heals. Magnet widens the reach.
        void DropHeal(Creature const* mob, BonkSpawnKind kind);
        void UpdateHeals(uint32 diff);
        void GiveShards(uint32 count);
        // An invisible marker at a pickup wearing `spellId` as its highlight.
        ObjectGuid PlaceMarker(GameObject const* go, uint32 spellId, float scale);
        // Takes a pickup's object and its marker off the ground.
        void RemovePickup(ObjectGuid const& goGuid, ObjectGuid const& markerGuid);

        // `avoid`: cards just shown, kept out of a reroll when others are
        // left. `kept`: locked cards that stay.
        bool OfferUpgrades(OfferSource source, std::vector<Offer> const* avoid = nullptr, std::vector<Offer> const* kept = nullptr);
        // The card's name and icon (an evolution shows what it becomes).
        std::string DescribeOffer(Offer const& offer, std::string* icon) const;
        std::string DescribeStep(Offer const& offer) const;
        OwnedWeapon* FindOwned(uint32 weaponId);
        OwnedWeapon const* FindOwned(uint32 weaponId) const;

        // Spell ranks: the rarity a card rolls (at least `minimum`, Legendary
        // by its own chance), and what that rarity adds to this spell.
        BonkRarity RollRarity(BonkRarity minimum) const;
        BonkRankStep RollStep(OwnedWeapon const& owned, BonkRarity rarity) const;
        Offer MakeRankOffer(OwnedWeapon const& owned, BonkRarity minimum) const;
        bool CanEvolve(OwnedWeapon const& owned) const;
        float GetLegendaryChance(OwnedWeapon const& owned) const;
        // damage:kills of each spell, in loadout order, for the state line.
        std::string DescribeWeaponStats() const;
        void ApplyStep(OwnedWeapon& owned, Offer const& offer);
        // A locked card brought to a later choice: false if it no longer
        // fits; otherwise its rank is brought up to date.
        bool RefreshKeptOffer(Offer& offer) const;
        bool IsPassiveAllowed(BonkPassive const& passive) const;
        // The choice is made (picked or skipped): the next pending one follows.
        void CloseOffer(uint32 pickedIndex);

        void SendState() const;
        void SendLoadout() const;
        void SendOffer() const;

        void Revive();
        void UpdateGear();
        // The spawn curve's elite interval now (more elites with the talent), 0 = none.
        uint32 GetEliteIntervalMs() const;

        // A choice is open: the player and every enemy stand still.
        void SetPaused(bool paused);

        void Finish(bool wentHome);
        void Exit();
        void CleanUp();

        Player* m_player;
        BonkTier const m_tier;
        BonkRunBonuses const m_bonuses;     // from camp, fixed for the run
        BonkMasteryBonuses const m_mastery; // the class's Mastery, fixed for the run
        std::map<uint32, uint32> m_spellRoots;  // spell id -> root weapon id
        uint32 m_masteryXp = 0;             // from kills; minutes are added at the end
        bool m_masteryAwarded = false;
        uint32 m_siphonWindow = 0;          // start of the second Soul Siphon is capped in
        float m_siphonHealed = 0.0f;        // % of max health healed in it
        BonkGearTotals m_gear;              // what worn gear adds, set bonuses included
        uint32 m_rerolls = 0;
        uint32 m_banishes = 0;
        uint32 m_skips = 0;
        uint32 m_revives = 0;
        std::vector<Offer> m_lockedOffers;  // for the next chest
        std::set<std::pair<bool, uint32>> m_banished;   // (is weapon, id)
        BonkMap m_map;
        uint32 m_stage = 1;
        uint32 m_nextMapId = 0;
        std::set<uint32> m_playedMaps;      // this lap through the tier's pool
        State m_state = STATE_ENTERING;
        bool m_paused = false;
        bool m_wentHome = false;

        uint32 m_enterTimer = 0;
        uint32 m_elapsedMs = 0;             // this stage
        uint32 m_totalMs = 0;               // the whole run
        size_t m_nextEvent = 0;
        std::vector<BonkMapEvent> m_heldEvents;     // waiting out a quiet time
        float m_gruntRadius = 0.0f;         // the biggest grunt this stage: bruisers are bigger
        int32 m_spawnTimer = 0;
        int32 m_eliteTimer = 0;
        uint32 m_boundaryTimer = 0;
        uint32 m_nativeTimer = 0;
        uint32 m_recoveryTimer = 0;
        uint32 m_stateTimer = 0;
        int32 m_endTimer = 0;

        bool m_bossSpawned = false;
        ObjectGuid m_bossGuid;
        std::vector<ObjectGuid> m_mobs;
        uint32 m_lastPlateId = 0;           // enemies' nameplate numbers, 1 to BONK_MAX_PLATE_ID over and over
        float m_moveX = 0.0f, m_moveY = 0.0f;       // the player's smoothed movement, yards a second
        float m_lastX = 0.0f, m_lastY = 0.0f;       // where they were at the last sample
        uint32 m_headingTimer = 0;
        std::map<ObjectGuid, uint32> m_farSince;    // enemy far from the player -> stage time it got there
        uint32 m_stragglerTimer = 0;
        struct PackSpot
        {
            float x = 0.0f, y = 0.0f;
            uint32 untilMs = 0;         // stage time
        };
        std::vector<PackSpot> m_packSpots;          // where packs landed lately
        std::vector<ObjectGuid> m_chests;
        std::vector<ObjectGuid> m_powerups;
        std::vector<Orb> m_orbs;
        ObjectGuid m_overflowOrb;           // the red orb that gathers experience past the orb cap
        uint32 m_orbTimer = 0;
        std::vector<ShardDrop> m_shardDrops;
        // Turkey legs lying where an enemy died.
        struct HealDrop
        {
            ObjectGuid guid;
            ObjectGuid marker;          // wears the leg's sparkle
        };
        std::vector<HealDrop> m_healDrops;
        uint32 m_healTimer = 0;
        uint32 m_shardTimer = 0;
        ObjectGuid m_portalGuid;
        int32 m_powerupSpawnTimer = 0;
        uint32 m_powerupCheckTimer = 0;

        uint32 m_kills = 0;
        uint32 m_goldEarned = 0;
        uint32 m_shardsEarned = 0;
        uint32 m_coins = 0;                 // Valor: the run's currency for chests (coins in code and data)
        float m_leechCarry = 0.0f;          // Leech healing not yet given
        uint32 m_necroticStacks = 0;
        uint32 m_necroticCheckMs = 0;
        struct Pool
        {
            ObjectGuid marker;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            uint32 leftMs = 0;
        };
        std::vector<Pool> m_pools;          // Sanguine
        uint32 m_poolTimer = 0;
        uint32 m_elitesWithoutChest = 0;
        std::map<ObjectGuid, uint32> m_chestChoices;    // chest -> cards it offers
        std::vector<uint32> m_pendingChestChoices;      // chests opened, waiting for their choice
        // A cast a behavior holds back: an echo, or a delayed burst at a
        // target (followed while it lives) or a spot.
        struct PendingCast
        {
            uint32 weaponId = 0;
            uint32 spellId = 0;
            int32 leftMs = 0;
            bool echo = false;
            bool selfArea = false;      // the spell again around the hero (a spin, a shockwave)
            bool strike = false;        // a burst's later shot at `target`
            float share = 1.0f;         // of the weapon's damage
            ObjectGuid target;
            float x = 0.0f, y = 0.0f, z = 0.0f;
        };
        std::vector<PendingCast> m_pendingCasts;
        struct WeaponOrb
        {
            ObjectGuid creature;
            uint32 weaponId = 0;
            int32 leftMs = 0;
            int32 tickMs = 0;
        };
        std::vector<WeaponOrb> m_weaponOrbs;
        struct Projectile
        {
            ObjectGuid creature;
            uint32 weaponId = 0;
            uint32 spellId = 0;             // cast on each enemy it touches
            float damageShare = 1.0f;
            float x = 0.0f, y = 0.0f, z = 0.0f;     // where it set off
            float dirX = 0.0f, dirY = 0.0f;
            float speed = 0.0f;             // yards a second
            float length = 0.0f;            // how far it flies
            float radius = 0.0f;            // hits enemies this close to its path, plus their own size
            uint32 pierce = 0;              // enemies it passes through before it stops
            uint32 flownMs = 0;
            float checked = 0.0f;           // of its path, looked at for enemies
            std::vector<ObjectGuid> struck;
        };
        std::vector<Projectile> m_projectiles;
        uint32 m_spellShield = 0;           // from Shield spells, soaked before the stat Shield
        bool m_wardBurst = false;           // a ward's Shield just broke
        struct Mark
        {
            uint32 weaponId = 0;
            uint32 untilMs = 0;
        };
        std::map<ObjectGuid, Mark> m_marks;             // enemy -> the mark it wears
        std::map<uint32, uint32> m_buffs;               // weapon -> ms its buff runs out
        struct Trap
        {
            uint32 weaponId = 0;
            ObjectGuid object;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            int32 armMs = 0;            // goes off from when this runs out
            int32 leftMs = 0;
        };
        std::vector<Trap> m_traps;
        uint32 m_trapTimer = 0;
        uint32 m_petTimer = 0;
        bool m_dodged = false;              // a dodge Revenge has not answered yet
        uint32 m_retaliateUntil = 0;        // ms: hits taken until then strike back
        std::vector<ObjectGuid> m_retaliations;     // enemies that hit the hero meanwhile
        float m_bleedHeal = 0.0f;           // Bloodthirst healing not yet given
        BonkTelegraphs m_telegraphs;
        uint32 m_shield = 0;                // absorb left
        uint32 m_shieldIdleMs = 0;          // since the hero last took damage
        uint32 m_shieldSendTimer = 0;
        uint32 m_shieldSent = 0;            // what the addon last heard
        uint32 m_shieldMaxSent = 0;
        uint32 m_chestsBought = 0;
        uint32 m_lastLevel = 1;
        uint32 m_pendingLevels = 0;

        // Keywords (BonkKeywords.cpp).
        void UpdateKeywords();
        void SpreadKeywords(Creature const* dead);
        // Frozen: held for a moment, then immune for a while. Elites and
        // bosses are never held but count as Frozen for Shatter crits.
        bool Freeze(Creature* mob);
        bool IsFrozen(Unit const* victim) const;
        uint32 CountKeywords(Unit const* victim) const;
        // Payoffs and Hunter's Mark, times the damage of `spell` (null for
        // a swing) on `victim`; the vulnerabilities are debuffs the core reads.
        float GetKeywordDamageMultiplier(SpellEntry const* spell, Unit const* victim) const;
        SpellAuraHolder* AddKeywordAura(Creature* mob, BonkKeywordInfo const& info, uint32 stacks, uint32 durationMs);
        int32 GetKeywordPoints(BonkKeywordInfo const& info) const;
        // Knockbacks: never elites or bosses, and once per enemy every while.
        bool CanKnockBack(Creature const* mob);
        void RebuildWeaponSpells();
        OwnedWeapon const* FindOwnedBySpell(uint32 spellId) const;

        struct PendingKeyword
        {
            ObjectGuid mob;
            BonkKeyword keyword = BONK_KEYWORD_NONE;
            uint8 stacks = 0;
        };
        std::vector<PendingKeyword> m_pendingKeywords;
        std::map<ObjectGuid, std::map<uint8, uint32>> m_dyingKeywords;  // enemy dealt a killing blow -> damage over time stacks it had
        struct CrowdControl
        {
            uint32 frozenImmuneUntil = 0;   // ms
            uint32 stunImmuneUntil = 0;
            uint32 knockImmuneUntil = 0;
            uint32 shatterUntil = 0;        // counts as Frozen until then
        };
        std::map<ObjectGuid, CrowdControl> m_crowdControl;
        std::map<uint32, uint32> m_weaponSpells;    // spell id -> the owned weapon that casts it

        std::vector<OwnedWeapon> m_weapons;
        std::map<uint32, uint8> m_passiveRanks;
        std::vector<Offer> m_offer;
        OfferSource m_offerSource = OFFER_LEVEL;

        float m_safeX = 0.0f, m_safeY = 0.0f, m_safeZ = 0.0f;
};

#endif
