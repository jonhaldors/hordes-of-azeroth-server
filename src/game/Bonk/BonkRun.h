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
#include "ObjectGuid.h"
#include "nonstd/optional.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

class Creature;
class GameObject;
class Player;
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
        void OnMobStrayed(Creature* mob, BonkSpawnKind kind);
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
        float GetCritChance() const;
        // Damage times this on a crit: a spell crit adds 50%, a weapon crit
        // 100%, plus Crit Damage, and 50% more on rooted enemies with a
        // freezing spell. Past 100% crit chance, the rest is the chance the
        // hit crits a second time.
        float RollCritMultiplier(bool weaponAttack, Unit const* victim = nullptr) const;

        // Stat totals without worn gear (the addon adds gear itself), for
        // the character sheet.
        void SendStats() const;
        bool Pick(uint32 choice, std::string& error);
        bool Reroll(std::string& error);
        bool Banish(uint32 choice, std::string& error);
        // Turns the choice down for a few coins.
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
            uint32 casts = 0;               // for every altEvery-th cast
            int32 timer = 0;
            std::map<ObjectGuid, uint32> recentTargets;     // random-target weapons: enemy -> ms hit
            std::map<ObjectGuid, uint32> carriers;          // spreading weapons: enemy -> ms its spell wears off
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
        // Casts the weapon's spell on `target` for `damageShare` of its damage.
        void StrikeWith(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float damageShare);
        // A spreading weapon's spell jumps from an enemy that died carrying it.
        void SpreadFrom(Creature const* dead);
        bool HasBehavior(BonkWeaponBehavior behavior) const;
        // Behaviors that act after the cast: echoes and delayed bursts wait
        // here, orbs travel; spell Shields stack on the hero's Shield.
        void UpdatePendingCasts(uint32 diff);
        void LaunchOrb(OwnedWeapon& owned);
        void UpdateWeaponOrbs(uint32 diff);
        void DespawnWeaponOrbs();
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
        std::vector<ObjectGuid> m_chests;
        std::vector<ObjectGuid> m_powerups;
        std::vector<Orb> m_orbs;
        ObjectGuid m_overflowOrb;           // the red orb that gathers experience past the orb cap
        uint32 m_orbTimer = 0;
        std::vector<ShardDrop> m_shardDrops;
        std::vector<ObjectGuid> m_healDrops;
        uint32 m_healTimer = 0;
        uint32 m_shardTimer = 0;
        ObjectGuid m_portalGuid;
        int32 m_powerupSpawnTimer = 0;
        uint32 m_powerupCheckTimer = 0;

        uint32 m_kills = 0;
        uint32 m_goldEarned = 0;
        uint32 m_shardsEarned = 0;
        uint32 m_coins = 0;
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
        uint32 m_spellShield = 0;           // from Shield spells, soaked before the stat Shield
        bool m_wardBurst = false;           // a ward's Shield just broke
        uint32 m_shield = 0;                // absorb left
        uint32 m_shieldIdleMs = 0;          // since the hero last took damage
        uint32 m_shieldSendTimer = 0;
        uint32 m_shieldSent = 0;            // what the addon last heard
        uint32 m_shieldMaxSent = 0;
        uint32 m_chestsBought = 0;
        uint32 m_lastLevel = 1;
        uint32 m_pendingLevels = 0;

        std::vector<OwnedWeapon> m_weapons;
        std::map<uint32, uint8> m_passiveRanks;
        std::vector<Offer> m_offer;
        OfferSource m_offerSource = OFFER_LEVEL;

        float m_safeX = 0.0f, m_safeY = 0.0f, m_safeZ = 0.0f;
};

#endif
