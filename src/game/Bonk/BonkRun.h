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
#include "BonkObjectives.h"
#include "BonkSpawnPoints.h"
#include "BonkTelegraph.h"
#include "ObjectGuid.h"
#include "nonstd/optional.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

class Creature;
class GameObject;
class Item;
class Player;

// Mini-events (BonkRunEvents.cpp): something happening somewhere on the map,
// one at a time, every couple of minutes.
enum BonkMiniEventType : uint8
{
    BONK_MINI_EVENT_HORDE  = 0,     // a ring of enemies surrounds the hero and closes in
    BONK_MINI_EVENT_GOBLIN = 1,     // a treasure goblin runs from the hero; caught, Valor and a chest
    BONK_MINI_EVENT_BOUNTY = 2,     // a tougher elite with a skull; killed in time, a chest
    BONK_MINI_EVENT_KOBOLDS  = 3,   // kobolds grab the orbs lying about and run; killed, the experience and more
    BONK_MINI_EVENT_SCOURGE  = 4,   // Necrotic Shards raise undead until destroyed; Valor and experience a shard
    BONK_MINI_EVENT_PORTAL   = 5,   // a demon portal sends waves until destroyed; a chest
    BONK_MINI_EVENT_STAMPEDE = 6,   // a wall of beasts charges across the hero's way, warned; a hazard only
    BONK_MINI_EVENT_CARAVAN  = 7,   // a kodo caravan walks past while raiders go for it; kept alive, a chest
    MAX_BONK_MINI_EVENT
};

// The addon's and `.bonk event`'s name for one ("horde"), and back.
char const* GetBonkMiniEventName(BonkMiniEventType type);
bool ParseBonkMiniEvent(char const* name, BonkMiniEventType& type);

// Shrines (BonkRunShrines.cpp): places on the map worth walking to, a few
// per stage, each marked with a shrine objective.
enum BonkShrineType : uint8
{
    BONK_SHRINE_LEY_LINE = 0,       // stand in its ring a while, then pick a buff for the run
    BONK_SHRINE_GREED    = 1,       // click: one more Greed stack, more reward and more enemies
    BONK_SHRINE_ALTAR    = 2,       // click: an elite pack with two affixes guards a free chest
    BONK_SHRINE_MOONWELL = 3,       // click: heals the hero inside for a few seconds, double experience
    BONK_SHRINE_STONE    = 4,       // click: the stage's boss, early; the stage goes on after
    MAX_BONK_SHRINE
};

// `.bonk shrine`'s name for one ("leyline"), and back.
char const* GetBonkShrineName(BonkShrineType type);
bool ParseBonkShrine(char const* name, BonkShrineType& type);
class SpellAuraHolder;
class SpellCaster;
class Unit;

class BonkRun
{
    public:
        // `dungeon`: a dungeon mode (Phase F3): every stage in that map,
        // tougher and richer; nullptr for the difficulty's zones.
        BonkRun(Player* player, BonkTier const& tier, BonkMap const* dungeon = nullptr);
        bool IsDungeon() const { return m_dungeonId != 0; }

        bool Begin(std::string& error);
        void Update(uint32 diff);
        void Abort();       // logout: clean up without moving the player
        void Surrender();   // `.bonk stop`
        bool SkipTime(uint32 seconds, std::string& error);  // `.bonk skip`, GM testing
        bool StartMiniEventNow(BonkMiniEventType type, std::string& error);    // `.bonk event`, GM testing
        bool PlaceShrineNow(BonkShrineType type, std::string& error);          // `.bonk shrine`, GM testing
        // A shrine's model was clicked (go_bonk_shrine).
        void UseShrine(GameObject* go);
        // The Ley Line buff picked, 1-based (`.bonk leypick`).
        bool PickLeyBuff(uint32 index, std::string& error);
        // Greed Totem stacks this run: their reward and extra enemies, %.
        uint32 GetGreedRewardPercent() const;
        float GetGreedCrowdPercent() const;
        // The wandering vendor (BonkRunVendor.cpp): his goblin was clicked
        // (npc_bonk_vendor); `.bonk vendor buy|lock <slot>`, `reroll`,
        // `close`, slots from 1; `.bonk vendor spawn`, GM testing.
        void OpenVendor(Creature* vendor);
        bool BuyFromVendor(uint32 index, std::string& error);
        bool LockVendorSlot(uint32 index, std::string& error);
        bool RerollVendor(std::string& error);
        void CloseVendor();
        bool SpawnVendorNow(std::string& error);

        bool IsActive() const { return m_state == STATE_RUNNING; }
        bool IsPaused() const { return m_paused; }
        // The addon's pause menu (Escape) is open: the run stays paused until
        // it closes and no choice is waiting (`.bonk menu 1|0`).
        void SetMenuOpen(bool open);
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
        // Health the hero just gained, from any heal (Unit::ModifyHealth):
        // quests count it.
        void OnHeroHealed(uint32 amount) { m_healedUnreported += amount; m_minuteHealed += amount; }
        // An enemy's hit on the hero: Armor takes its share off, then the
        // Shield soaks what it can. Returns what is left for health.
        uint32 MitigateDamage(uint32 damage);
        // A hero's spell hit `target` for `damage` before crits and the
        // enemy's own modifiers: by its chance to hit twice, it hits again,
        // for damage only and with its own crit roll.
        void HitAgain(SpellEntry const* spellInfo, Unit* target, float damage, SpellEffectIndex effectIndex, WeaponAttackType attackType);
        // A Necrotic enemy hit the hero: healing is weaker for a while.
        void AddNecroticStack();
        // Power and the class's Mastery bonuses for `spell` (null for melee
        // swings) against `victim`, added up: applies to all damage the
        // player deals (SpellCaster::SpellDamageBonusDone and
        // MeleeDamageBonusDone). `powerShare`: how much of Power counts
        // (GetPowerShare).
        float GetDamageMultiplier(SpellEntry const* spell, Unit const* victim, float powerShare = 1.0f) const;
        // How much of the hero's Power `attacker`'s hits count: all of it
        // for the hero and their projectiles, BONK_SUMMON_POWER_SHARE for
        // a summon.
        float GetPowerShare(Unit const* attacker) const;

        // The run whose hero, or the hero's pet, attacks one of that run's
        // enemies; null for every other fight. Such attacks skip the normal
        // hit tables: they always land, except that elites and bosses dodge
        // (RollDodge), and they crit by the hero's Crit (GetCritChance).
        static BonkRun* FromAttack(SpellCaster const* attacker, Unit const* victim);
        // Milliseconds a spell flying at `speed` takes to reach one of the
        // run's enemies, as the client draws it; 0 for every other cast
        // (Spell::AddUnitTarget keeps its own timing).
        static uint32 GetMissileDelay(SpellCaster const* caster, Unit const* target, float speed);
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

        // Darkmoon Cards (BonkRunCards.cpp).
        // An enemy hit the hero for `damage`, after Armor and Shields: Thorns.
        void OnHeroHitBy(Creature* attacker, uint32 damage);
        // The hero's own hit on an enemy, about to land: Gnomish Death Ray.
        void ModifyHeroHit(Unit* victim, uint32& damage);
        // The hero's own hit landed for `damage` (BonkOnDamage): Hand of
        // Ragnaros, Maelstrom. A card's own hits set off nothing.
        void OnHeroHit(Unit* victim, uint32 damage, SpellEntry const* spell);
        // `.bonk card <id>`: takes a card as if a chest gave it.
        bool GiveCard(uint32 cardId, std::string& error);

        // Run gear (BonkRunGear.cpp): the open gear choice, answered by
        // `.bonk gear equip` or `.bonk gear salvage`.
        bool ChooseGear(bool equip, std::string& error);
        // Glyphs (BonkRunGlyphs.cpp): the choice that is up, `.bonk glyph <n>`.
        bool ChooseGlyph(uint32 choice, std::string& error);
        // A hidden encounter's object used (BonkRunHidden.cpp): true if it
        // was this stage's.
        bool UseHidden(GameObject* go);
        // The hero used `item` (WorldSession::HandleUseItemOpcode): true if
        // the run handled it (the Hearthstone opens its extraction ring).
        bool UseItem(Item* item);
        // The Extraction Vault (Phase F8, BonkRunGear.cpp): beside the stage
        // portal and in the Hearthstone's ring. The finds put in it come home
        // with the hero (one; two with Pack Mule), the rest sell for gold.
        struct VaultFind
        {
            ObjectGuid guid;
            uint32 entry = 0;
            uint8 quality = 0;
            bool worn = false;
            uint32 sellCopper = 0;      // what it sells for if left out
            bool inVault = false;
        };
        bool IsOwnVault(GameObject const* vault) const;
        // The hero used the vault: the addon's window (gossip without it).
        void OpenVault();
        bool PutInVault(uint32 guidLow, std::string& error);
        bool TakeFromVault(uint32 guidLow, std::string& error);
        std::vector<VaultFind> GetVaultFinds() const;
        uint32 GetVaultSlots() const;
        // Going home now would bring nothing home though a find could come.
        bool IsVaultEmptyWithFinds() const;

        // Keywords (BonkKeywords.cpp). A hit or tick of one of the hero's
        // spells on an enemy (BonkOnDamage): the keyword the spell applies
        // goes on it at the next update.
        void OnWeaponHit(Unit* victim, SpellEntry const* spell);
        // One of the hero's pets bit `victim` (BonkOnDamage): a frenzied
        // pet's bite strikes the enemies in front of it too.
        void OnPetBite(Creature* pet, Unit* victim);
        // The hero's hit about to kill an enemy: its damage over time stacks
        // are noted, as the core strips its debuffs before it counts as dead.
        void OnKillingBlow(Unit* victim);
        // Puts `stacks` of `keyword` on a run enemy (`.bonk keyword` too),
        // for `durationMs` (0: the keyword's own); damage over time stacks
        // deal `damageScale` times a stack's damage (the rank of the spell
        // that applied them) and count for `sourceSpellId`, the hero's spell
        // that applied them (0: the keyword itself).
        bool ApplyKeyword(Creature* mob, BonkKeyword keyword, uint32 stacks, uint32 durationMs = 0, float damageScale = 1.0f, uint32 sourceSpellId = 0);
        // A tick of damage over time (`spellId`: the keyword's spell) on
        // `victim`, shared out among the hero's spells that put its stacks
        // on, by their damage, largest share first; empty if it is not one
        // the run knows of. The results screen counts it for them.
        void CreditDamageOverTime(Unit const* victim, uint32 spellId, uint32 damage, std::vector<std::pair<uint32, uint32>>& credits) const;
        uint32 GetKeywordStacks(Unit const* victim, BonkKeyword keyword) const;
        // A hero's stun on a run enemy (Spell::DoSpellHitOnUnit): cut to at
        // most BONK_MAX_HOLD_MS; false while the enemy is still immune after
        // its last one.
        bool LimitStun(Creature* mob, int32& duration);
        // The same for a root (Frozen's rule; never elites or bosses).
        bool LimitRoot(Creature* mob, int32& duration);
        // A red ground warning for an enemy ability; `onFire` runs when it
        // fills. Warnings are dropped, unfired, when the run pauses or the
        // stage ends.
        void AddTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
            std::function<void()> onFire = nullptr);
        // Objectives (BonkObjectives): a marked spot worth walking to, often
        // one of the stage's spawn points (BonkSpawnPoints), which it holds
        // until it is taken away. All go when the stage ends.
        uint32 AddObjective(BonkObjectiveKind kind, float x, float y, float z, std::string const& label, uint32 seconds);
        void RemoveObjective(uint32 id);
        BonkObjectives const& GetObjectives() const { return m_objectives; }
        // A free spawn point `minDistance` to `maxDistance` yards from the
        // hero, now taken; false if there is none yet.
        bool TakeSpawnPoint(float minDistance, float maxDistance, BonkSpot& spot) { return m_spawnPoints.Take(m_player, minDistance, maxDistance, spot); }
        BonkSpawnPoints const& GetSpawnPoints() const { return m_spawnPoints; }
        // Boss phases (BonkMobAI::UpdatePhases): the difficulty's step, 0 to
        // 2; a wave round a boss; a void zone, burning ground that hits the
        // hero for `damage` (the enemy's own, as Erupt) each second inside,
        // for `durationMs`, under a red circle. Paused or a new stage, gone.
        uint32 GetDifficultyStep() const { return std::min(2u, m_tier.eliteAffixes ? m_tier.eliteAffixes - 1 : 0u); }
        void SpawnPhaseAdds(Creature const* boss, uint32 count, uint32 elites);
        void StartVoidZone(ObjectGuid source, float x, float y, float z, float radius, uint32 damage, uint32 durationMs);
        // Volcanic: fire bursts up where a warning was, and the enemy that
        // called it hits the hero for `damage` if they are still inside.
        void Erupt(ObjectGuid mobGuid, float x, float y, float z, float radius, uint32 damage);
        // Casters' bolts fly at a spot, not at the hero: an invisible
        // target there for the enemy's spell (it takes the hit, the hero
        // never sees it), and the bolt landing: `damage` from `spellId` if
        // the hero is still within `radius`.
        ObjectGuid PlaceBoltTarget(float x, float y, float z, uint32 lifetimeMs);
        // A caster's bolt that lands in `landMs`: false once BONK_MAX_BOLTS are
        // in the air (readable, and lighter on the server).
        bool ReserveBolt(uint32 landMs);
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
        // `.bonk stress` (BonkRunStress.cpp), a GM test: on, the hero is
        // maxed and the crowd kept at the cap, with the server tick reported
        // every 10 seconds; off, a summary.
        void ToggleStress();
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

        // A spell the hero has. Its ranks are uncapped; each added damage and
        // the upgrades its rarity rolled, kept here (read with GetSpellUpgrade).
        struct OwnedWeapon
        {
            BonkWeapon weapon;
            uint32 rank = 1;
            float damageBonus = 0.0f;       // %
            float upgrades[MAX_BONK_SPELL_UPGRADE] = {};  // by BonkSpellUpgrade; kept through evolving
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
            bool isCard = false;        // a Darkmoon Card (isWeapon false): id is the card
            uint32 id = 0;              // the weapon (as owned, before evolving), passive or card
            uint32 newLevel = 1;        // the rank it brings
            BonkRarity rarity = BONK_RARITY_COMMON;
            BonkRankStep step = BONK_STEP_NEW;     // weapons: what the card brings
            // A rank's upgrade steps, rolled with the card so it can show them;
            // NONE where the spell had no room left (spare damage instead).
            BonkSpellUpgrade upgrades[BONK_MAX_RANK_UPGRADES] = {};
            bool locked = false;        // chests: comes back at the next chest if not picked
        };

        struct Orb
        {
            ObjectGuid guid;
            uint32 experience = 0;
            bool pulled = false;        // flying to the player
            float aimX = 0.0f, aimY = 0.0f, aimZ = 0.0f;    // where it is flying
            BonkOrb colour = BONK_ORB_BLUE;
            uint32 droppedMs = 0;       // WorldTimer ms
            uint32 mergeId = 0;         // sliding into a merge (OrbMerge::id)
        };
        // Orbs sliding into one (BonkRun::MergeOrbs): the anchor stays put
        // and takes the next colour once the others reach it.
        struct OrbMerge
        {
            uint32 id = 0;
            ObjectGuid anchor;
            BonkOrb colour = BONK_ORB_BLUE;     // what the anchor becomes
            int32 leftMs = 0;
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
        // Keeps the spawn points just found that make good starting spots.
        void LearnStarts(std::vector<BonkSpot> const& spots);
        void StartStage();
        void UpdateRunning(uint32 diff);
        void UpdateOffers();
        void UpdateEvents();
        // The mini-event scheduler (BonkRunEvents.cpp): one every
        // BONK_MINI_EVENT_INTERVAL_MS, never the same twice running, one at
        // a time, none in a quiet time or once the boss is up.
        void UpdateMiniEvents(uint32 diff);
        // One of `allowed` (bits by BonkMiniEventType), never the last one
        // while another is allowed.
        BonkMiniEventType PickMiniEvent(uint32 allowed) const;
        bool StartMiniEvent(BonkMiniEventType type);
        bool SpawnHorde();
        bool SpawnTreasureGoblin();
        bool SpawnBounty();
        bool SpawnKoboldThieves();
        bool SpawnScourgeInvasion();
        bool SpawnDemonPortal();
        bool SpawnStampede();
        bool SpawnCaravan();
        // The caravan under way: its walk, its raiders and their blows; true
        // once it is over (arrived or lost).
        bool UpdateCaravan(uint32 diff);
        void PauseCaravan(bool paused);
        void EndCaravan(bool arrived);
        // The caravan itself (BonkRun.cpp, beside the other helpers): friendly,
        // no AI, walking where the run sends it.
        Creature* SummonCaravan(uint32 entry, float x, float y, float z, float orientation);
        // Kobolds, shards, a portal: several targets (or one that stands),
        // under way until they are all dead or the time is up.
        void StartMiniEventGroup(std::string const& label, uint32 seconds);
        void UpdateMiniEventGroup(uint32 diff);
        void OnMiniEventGroupKilled(Creature* mob);
        // Adds round a shard or a portal.
        void SpawnEventAdds(Creature const* source, uint32 count, uint32 eliteCount);
        // Whether `guid` is a mini-event's target (chased, never brought
        // along as a straggler).
        bool IsMiniEventTarget(ObjectGuid const& guid) const;
        // Kobold thieves (BonkRun.cpp, beside the orbs): an orb lying
        // `minDistance` to `maxDistance` from the hero, and taking every
        // orb within `range` of a spot; their experience.
        bool FindLyingOrb(float minDistance, float maxDistance, float& x, float& y, float& z) const;
        uint32 StealOrbs(float x, float y, float range);
        // A goblin or a bounty: under way until it dies or its time is up.
        void StartMiniEventTarget(Creature* target, std::string const& label, uint32 seconds);
        void UpdateMiniEventTarget(uint32 diff);
        void OnMiniEventTargetKilled(Creature* mob);
        // Its time left for the addon, when the second changes (or `force`).
        void SendMiniEventTimer(bool force = false) const;
        void EndMiniEvent();
        // Shrines (BonkRunShrines.cpp).
        void UpdateShrines(uint32 diff);
        bool PlaceShrine(BonkShrineType type);
        BonkShrineType PickShrineType() const;
        void RemoveShrine(size_t index);
        void UpdateLeyLine(size_t index, uint32 diff);
        void UpdateMoonwell(size_t index, uint32 diff);
        void UpdateAltar(size_t index);
        void OfferLeyBuffs();
        void SendLeyOffer() const;
        void SendGreed() const;
        bool IsInMoonwell(float x, float y) const;
        // How long a Moonwell's waters stir (Attunement adds to it).
        uint32 GetMoonwellSeconds() const;
        // A new stage: the last one's shrines gone, this one's to place.
        void ResetShrines();
        void DespawnShrines();
        // The wandering vendor (BonkRunVendor.cpp).
        void ResetVendor();             // a new stage: when he comes
        void UpdateVendor(uint32 diff);
        bool IsBossUp() const;          // a boss or mini-boss alive
        bool SpawnVendor();
        void StockVendor(bool reroll);
        void CheckVendorStock();
        uint32 GetVendorPrice(Offer const* card) const;     // null: the special slot
        uint32 GetVendorRerollCost() const;
        void ApplyVendorCard(Offer const& card);
        void SendVendor() const;
        void DespawnVendor();
        void UpdateSpawns(uint32 diff);
        void UpdateWeapons(uint32 diff);
        void UpdateBoundary(uint32 diff);
        void UpdateNatives(uint32 diff);
        void UpdateRecovery(uint32 diff);
        // Tells quests the run's crits and the healing since last time.
        void ReportQuestCounts();
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
        struct VoidZone
        {
            ObjectGuid source;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float radius = 0.0f;
            uint32 damage = 0;
            uint32 leftMs = 0;
            uint32 tickMs = 0;
        };
        std::vector<VoidZone> m_voidZones;
        std::vector<uint32> m_boltLandings;     // run time each bolt in the air lands
        void UpdateVoidZones(uint32 diff);
        void DespawnPools();
        void RunEvent(BonkMapEvent const& event);
        void OpenPortal(Creature* boss);
        void PlacePortal(float x, float y, float baseZ);

        void PruneMobs();
        // An enemy of `kind` whose time has come; normal enemies of
        // `archetype` only (-1: any).
        BonkMapCreature const* PickCreature(BonkSpawnKind kind, int32 archetype = -1) const;
        // A normal enemy for the crowd, by the timeline block's roster;
        // bruisers only while there is room for one.
        BonkMapCreature const* PickTrash() const;
        uint32 CountTrash() const;
        uint32 CountBruisers() const;
        uint32 GetMaxBruisers() const;
        // The stage timeline (bonk_wave): the block in force (by stage
        // time), whether an archetype is in its roster, and the crowd of the
        // roster's archetypes to keep alive, at most the tier's cap; past the
        // cap the rest makes enemies tougher (GetCrowdOverflow).
        BonkWave const& GetWave() const;
        bool IsInRoster(BonkArchetype archetype) const;
        uint32 CountRoster() const;
        float GetCurveMinAlive() const;
        uint32 GetSpawnTarget() const;
        float GetCrowdOverflow() const;
        // A new block: its elites, mini-bosses and mini-event slot, and the
        // addon's word of what comes next.
        void UpdateWaves();
        void EnterWave(size_t index);
        uint32 GetFirstMinibossSecond() const;
        std::string DescribeTimelineMarks() const;
        // The boss still up a while after it came: ever stronger rings.
        void UpdateOvertime(uint32 diff);
        // Enemy scaling now (bonk_scaling, by run time): an enemy hit's share
        // of the hero's base health, and enemy health's multiplier.
        BonkScalingPoint GetScalingNow() const;
        // The class's own health at its level: no gear, talents or picks.
        float GetHeroBaseHealth() const;
        // No elites or events in a lull or a mini-boss block, or shortly
        // before the boss.
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
        // What the hero pays for one: the cost less Haggle. Rewards that are
        // "a chest's worth" (salvage, skips, a goblin) go by the cost.
        uint32 GetChestPrice() const;
        // A camp upgrade's utility, as brought into this run.
        float GetUtility(BonkUpgradeUtility utility) const { return m_bonuses.utility[utility]; }
        // Hidden encounters (BonkRunHidden.cpp): rolled at each stage's
        // start, placed once a spawn point turns up, sprung by the hero.
        struct HiddenEncounter
        {
            BonkHidden const* hidden = nullptr;
            bool placed = false;
            bool triggered = false;
            uint32 placeTimer = 0;
            BonkSpot spot;
            ObjectGuid object;
            ObjectGuid boss;
        };
        HiddenEncounter m_hidden;
        void RollHidden();
        void UpdateHidden(uint32 diff);
        void TriggerHidden();
        void OnHiddenKilled(Creature const* mob);
        void ClearHidden();

        // Glyphs (BonkRunGlyphs.cpp): one to pick at the start, one after
        // the first mini-boss.
        void QueueGlyphPick();
        void OfferGlyphs();
        void SendGlyphOffer() const;
        void SendGlyphs() const;
        BonkRarity RollGlyphRarity() const;
        // The run's Glyph with `effect`, if it has one, and its values.
        BonkGlyph const* GetGlyph(BonkGlyphEffect effect) const;
        float GetGlyphValue(BonkGlyphEffect effect, uint8 index) const;
        // Vengeance's Power, for health missing.
        float GetGlyphPower() const;
        void OnGlyphKill(Creature const* mob, BonkSpawnKind kind);
        // Soul Harvest's empowered hit.
        void ModifyGlyphHit(Unit* victim, uint32& damage);
        void UpdateGlyphs(uint32 diff);
        void StrikeStorm(BonkGlyph const& glyph);
        void DropMeteor(BonkGlyph const& glyph);
        std::vector<BonkGlyph const*> m_glyphs;         // taken, in order
        std::vector<uint32> m_glyphOffer;               // the choice up now
        uint32 m_pendingGlyphPicks = 0;
        uint32 m_glyphPicksGiven = 0;                   // offered this run
        std::set<uint32> m_glyphCardIds;                // cards Glyphs gave: not in the card row
        uint32 m_glyphKills = 0;
        uint32 m_glyphHasteUntilMs = 0;                 // Bloodlust, run time
        bool m_soulHarvestReady = false;
        uint32 m_divineShieldStage = 0;                 // the stage Divine Shield last saved the hero in
        int32 m_glyphStormTimer = 0;
        int32 m_glyphMeteorTimer = 0;

        void SpawnPowerup();
        void UpdatePowerups(uint32 diff, bool spawn);
        void UsePowerup(BonkPowerup const& powerup);
        void UpdateObjectives(uint32 diff);
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
        // The total after its cap (Dodge, Hit, Count, Area, Haste).
        float GetStat(BonkPassiveEffect effect) const;
        // A stat's cap, 0 for none.
        static float GetStatCap(BonkPassiveEffect effect);
        // How far experience orbs and turkey legs come to the hero from.
        float GetMagnetRange() const;
        // What worn gear adds to a stat, after the gear Haste cap.
        float GetGearTotal(BonkPassiveEffect effect) const;
        // 1 + Haste: how many times as often spells fire and tick.
        float GetHasteRate() const;
        float GetWeaponDamage(OwnedWeapon const& owned) const;
        // What its ranks added of `upgrade`, up to that upgrade's cap; 0 when
        // the spell (after evolving, say) doesn't use it.
        float GetSpellUpgrade(OwnedWeapon const& owned, BonkSpellUpgrade upgrade) const;
        // 1 + Duration + its own duration upgrades: how much longer it lasts.
        float GetWeaponDurationScale(OwnedWeapon const& owned) const;
        // 1 + Haste + its own Haste: how many times as often it fires and ticks.
        float GetWeaponHasteRate(OwnedWeapon const& owned) const;
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
        // A thrown trap (ground target): it shows where it lands and goes
        // off at once.
        void ThrowTrap(OwnedWeapon& owned, float x, float y, float z);
        // `spellInfo` hits `target` for `damage` (before bonuses and crit):
        // damage only, with its own crit roll, as a spell hit; the weapon's
        // keyword follows through BonkOnDamage.
        // `caster`: who deals it (a summon), the hero if null.
        void DealSpellHit(SpellEntry const* spellInfo, Unit* target, float damage, Unit* caster = nullptr, bool forceCrit = false);
        // Aimed Shot: every enemy in a line from the hero at `angle`, as
        // wide as the weapon's area lets it be.
        std::vector<Creature*> GetMobsInLine(OwnedWeapon const& owned, float range, float angle) const;
        // Pack Leader: its wolf howls and the hero's other pets frenzy.
        // False without its wolf, or with no enemy about.
        bool Howl(OwnedWeapon& owned);
        // What a frenzied pet's damage is multiplied by.
        float GetFrenzyDamage() const;
        bool IsPetFrenzied() const;
        // The class's defensive, which fires by itself (the Hunter's
        // Deterrence: more Dodge for a while when health runs low).
        void UpdateDefensive(uint32 diff);
        // Beasts fight beside the hero for as long as the spell is held.
        void UpdatePets(OwnedWeapon& owned);
        void UpdatePetTargets(uint32 diff);
        // A Call Pet beast's bite before frenzy: the spell's damage a second
        // shared between its bites and its family's strikes.
        float GetPetBiteDamage(OwnedWeapon const& owned, BonkPetFamily const& family) const;
        // Its family's skills, by themselves: strikes at its prey in reach,
        // a dash while its prey is far, a pounce at a new prey far off.
        void UsePetAbilities(OwnedWeapon& owned, Creature* pet, Creature* prey);
        bool Pounce(OwnedWeapon& owned, Creature* pet, Creature* prey);
        void PetStrike(OwnedWeapon& owned, Creature* pet, Creature* prey, BonkPetAbility const& ability);
        void DespawnPets();
        // Summons (docs/Spells.md, Summons): whether `unit` is one of the
        // hero's; how many more `owned` may have under BONK_MAX_SUMMONS,
        // counting the other spells' first; a summon's size from `base`
        // with Area and its summon size upgrade, capped together at
        // BONK_MAX_AREA.
        bool IsSummon(Unit const* unit) const;
        uint32 GetSummonRoom(OwnedWeapon const& owned) const;
        float GetSummonScale(OwnedWeapon const& owned, float base) const;
        // The hero's pet family for Call Pet: the Cat until the Stable
        // Master lets them choose.
        BonkPetFamily const* GetPetFamily() const;
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
        // Experience the next level takes; past the level cap, the last real level's.
        uint32 GetNextLevelXp() const;
        // runxp~level~xp~next: the run's level and experience, which go on
        // past the level cap. Sent at most every BONK_RUN_XP_SEND_MS while
        // orbs come in, at once on a level, and with the full state.
        void SendRunXp() const;
        void UpdateRunXp(bool levelled);
        bool m_runXpDirty = false;
        uint32 m_runXpSentAt = 0;           // WorldTimer ms
        void GiveGold(uint32 copper);
        // What GiveGold multiplies by: Greed, the map type, the gold talents,
        // a dungeon mode.
        float GetGoldMultiplier() const;

        void DropOrb(Creature const* mob, uint32 experience, BonkOrb colour);
        // An orb of `colour` on the ground; false (experience given at once)
        // if it could not be placed.
        bool SpawnOrb(float x, float y, float z, uint32 experience, BonkOrb colour);
        void UpdateOrbs(uint32 diff);
        // Orbs of a colour lying together slide into one of the next colour.
        void UpdateOrbMerges(uint32 diff);
        void MergeOrbs();
        void FinishOrbMerge(OrbMerge const& merge);
        void CollectAllOrbs();
        // Every orb on the ground flies to the hero (Gnomish Magnet).
        void PullAllOrbs();
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
        // left. `kept`: locked cards that stay. `drawn`: the cards go there,
        // nothing shown (the wandering vendor's shelf).
        bool OfferUpgrades(OfferSource source, std::vector<Offer> const* avoid = nullptr, std::vector<Offer> const* kept = nullptr,
            std::vector<Offer>* drawn = nullptr);
        // The card's name and icon (an evolution shows what it becomes).
        std::string DescribeOffer(Offer const& offer, std::string* icon) const;
        std::string DescribeStep(Offer const& offer) const;
        OwnedWeapon* FindOwned(uint32 weaponId);
        OwnedWeapon const* FindOwned(uint32 weaponId) const;

        // Spell ranks: the rarity a card rolls (at least `minimum`, Legendary
        // by its own chance), and the upgrades that rarity adds to this spell.
        BonkRarity RollRarity(BonkRarity minimum) const;
        void RollUpgrades(OwnedWeapon const& owned, Offer& offer) const;
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
        bool IsPassiveCapped(BonkPassive const& passive) const;
        // Open spell and passive slots: the starting ones plus Sayge's.
        size_t GetSpellSlots() const;
        size_t GetPassiveSlots() const;
        // The choice is made (picked or skipped): the next pending one follows.
        void CloseOffer(uint32 pickedIndex);
        // What a banish remembers: spells, passives and cards apart.
        static std::pair<bool, uint32> GetBanishKey(Offer const& offer);

        // Darkmoon Cards (BonkRunCards.cpp): a chest's choice, the cards held,
        // their stats and effects.
        struct OwnedCard
        {
            BonkCard const* card = nullptr;
            uint32 copies = 0;
        };
        bool OfferCards(size_t offerSize, std::vector<Offer> const* avoid, std::vector<Offer> const* kept, std::vector<Offer>* drawn);
        // Chests may offer it: not a legendary's own, and the account has it.
        bool IsCardInPool(BonkCard const& card) const;
        // Camp upgrades at a run's start: Lucky Draw's card, Head Start's
        // levels (each with its pick).
        void GiveStartCard();
        void GiveStartLevels();
        void AddCard(BonkCard const& card);
        void RemoveCard(BonkCard const& card);
        // Worn gear's cards (a legendary's effect, bonk_item_card): held while
        // the item is worn (UpdateGear).
        void SyncGearCards();
        std::vector<uint32> m_gearCards;                        // card ids from worn gear, sorted
        uint32 GetCardCopies(uint32 cardId) const;
        // The effect's card's `index` value (0 without the card), its copies
        // added up; chances with diminishing returns; cooldowns shortened.
        float GetCardTotal(BonkCardEffect effect, uint8 index) const;
        float GetCardChance(BonkCardEffect effect) const;
        uint32 GetCardCooldown(BonkCardEffect effect) const;
        bool HasCard(BonkCardEffect effect) const { return m_cardCopies[effect] > 0; }
        // Card Power (Bloodlust Brooch, Zandalarian Hero Charm) and, against
        // elites and bosses, Mark of the Champion's.
        float GetCardPower(Unit const* victim) const;
        void UpdateCards(uint32 diff);
        // Heals the hero; with Lifegiving Gem what overflows becomes Shield.
        // `textSpellId` names the heal in combat text (0: none).
        void HealHero(uint32 amount, uint32 textSpellId = 0);
        // The Insignia's immunity, after a hit and on its cooldown.
        bool IsCardImmune() const { return m_cardImmuneUntilMs > m_totalMs; }
        void OnCardHitTaken();
        void OnShieldBroken();
        bool RollFreeChest() const;
        // cards~id:copies:rarity:icon:maxCopies,... and a cardtip~ line each,
        // then the cooldowns running.
        void SendCards() const;
        // cardcd~id~leftMs~totalMs: a card's cooldown, for the card row's
        // timer; sent as it starts, with the cards and after a pause.
        void StartCardCooldown(BonkCardEffect effect, uint32 ms);
        void SendCardCooldown(BonkCardEffect effect) const;
        void SendCardCooldowns() const;
        // cardnow~id~text: what a card is doing now ("+4.05% Power" for
        // Bloodlust Brooch), sent when it changes; empty text clears it.
        std::string DescribeCardNow(BonkCard const& card) const;
        void SendCardStatus(bool force) const;
        mutable std::map<uint32, std::string> m_cardNowSent;    // what the addon last heard, by card
        uint32 m_cardCooldownTotals[MAX_BONK_CARD_EFFECT] = {};
        std::vector<OwnedCard> m_cards;                         // in the order they were taken
        float m_cardStats[MAX_BONK_PASSIVE_EFFECT] = {};        // their stats added up
        uint32 m_cardCopies[MAX_BONK_CARD_EFFECT] = {};         // copies held, by effect
        BonkCard const* m_cardByEffect[MAX_BONK_CARD_EFFECT] = {};
        int32 m_cardCooldowns[MAX_BONK_CARD_EFFECT] = {};       // ms left
        uint32 m_cardTickMs = 0;                                // the once-a-second card heals
        uint32 m_cardImmuneUntilMs = 0;                         // run ms
        uint32 m_cardSwiftUntilMs = 0;
        uint32 m_cardSurgeUntilMs = 0;
        uint32 m_cardKills = 0;                                 // Bloodlust Brooch: kills since it was taken
        float m_cardValorCarry = 0.0f;                          // Valor short of a whole one
        uint32 m_soulstones = 0;
        uint32 m_cardCritsSeen = 0;
        uint32 m_stillSinceMs = 0;                              // Basic Campfire: run ms the hero last moved
        bool m_cardProc = false;                                // a card's own hit is landing

        // Run gear (BonkRunGear.cpp): sacks on the ground, the choice they
        // open, the items worn for the run and what the hero wore coming in.
        struct GearDrop
        {
            ObjectGuid guid;
            uint32 entry = 0;
            ObjectGuid marker;
        };
        void DropGear(Creature const* mob, BonkSpawnKind kind);
        uint8 RollGearQuality(BonkSpawnKind kind) const;
        void UpdateGearDrops(uint32 diff);
        void CollectAllGear();
        void OfferGear();
        void SendGearOffer() const;
        void CloseGearOffer();
        uint32 GetGearSalvage(uint32 entry) const;
        bool EquipRunGear(uint32 entry, std::string& error);
        void SnapshotBroughtGear();
        void RestoreBroughtGear();
        // The run ends: run gear comes off. With `extract` (going home) the
        // finds in the vault come home by mail and the rest sell for gold;
        // without, only greens and blues sell.
        void ResolveRunGear(bool extract);
        void PlaceVault(float x, float y, float baseZ);
        void DespawnVault();
        void SendVault() const;
        bool CheckVaultReach(std::string& error) const;
        ObjectGuid FindRunGear(uint32 guidLow) const;
        bool IsInVault(ObjectGuid const& guid) const;
        // What a find sells for at the run's end, Greed and the rest included.
        uint32 GetGearGold(uint8 quality) const;
        ObjectGuid m_vaultGuid;
        std::vector<ObjectGuid> m_vault;                        // finds going home, first put in first
        std::vector<GearDrop> m_gearDrops;
        std::vector<uint32> m_pendingGear;                      // sacks picked up, waiting for their choice
        uint32 m_gearOfferEntry = 0;                            // the choice open now
        uint32 m_gearTimer = 0;
        std::vector<ObjectGuid> m_runGear;                      // items made in this run
        std::map<uint8, ObjectGuid> m_broughtGear;              // equipment slot -> item worn coming in

        // The run's Hearthstone (BonkRunGear.cpp): found now and then, used
        // from the bags; standing in its ring takes the hero home with the loot.
        void MaybeDropHearthstone(BonkSpawnKind kind);
        void UpdateHearth(uint32 diff);
        void EndHearth();
        // How long the hero stands in the ring (Hearthstone Lore shortens it).
        uint32 GetHearthMs() const;
        bool m_hearthDropped = false;                           // one a run
        bool m_hearthActive = false;
        uint32 m_hearthMs = 0;                                  // time stood in the ring
        uint32 m_hearthSendMs = 0;
        bool m_hearthShown = false;                             // the capture bar is up
        float m_hearthX = 0.0f, m_hearthY = 0.0f, m_hearthZ = 0.0f;
        ObjectGuid m_hearthRing;

        void SendState() const;
        void SendLoadout() const;
        void SendOffer() const;

        // Second Wind, or with `soulstone` a Soulstone card (at its health).
        // `glyph`: Divine Shield's, once a stage.
        void Revive(bool soulstone = false, BonkGlyph const* glyph = nullptr);
        void UpdateGear();
        // The spawn curve's elite interval now (more elites with the talent), 0 = none.
        uint32 GetEliteIntervalMs() const;

        // A choice is open: the player and every enemy stand still.
        void SetPaused(bool paused);
        bool m_menuOpen = false;
        bool m_victory = false;             // the difficulty's last stage boss is dead: the run is won
        uint32 m_dungeonId = 0;             // a dungeon mode's bonk_map id, 0 for the difficulty's zones

        void Finish(bool wentHome);
        void Exit();
        void CleanUp();

        Player* m_player;
        BonkTier const m_tier;
        BonkRunBonuses const m_bonuses;     // from camp, fixed for the run
        BonkMasteryBonuses const m_mastery; // the class's Mastery, fixed for the run
        std::map<uint32, uint32> m_spellRoots;  // spell id -> root weapon id
        uint32 m_masteryXp = 0;             // from kills; minutes are added at the end
        mutable uint32 m_crits = 0;         // the hero's crits this run (RollCritMultiplier)
        uint32 m_critsReported = 0;
        uint32 m_healedUnreported = 0;      // health healed, not yet told to quests
        // More for quests, told once a second (ReportQuestCounts).
        uint32 m_leechUnreported = 0;
        uint32 m_dodgesUnreported = 0;
        uint32 m_armorUnreported = 0;       // damage Armor took off enemy hits
        uint32 m_projectilesUnreported = 0;
        uint32 m_castAnimationMs = 0;       // when a bolt's cast animation last played
        uint32 m_maxHealthReported = 0;
        uint32 m_unhurtReported = 0;        // seconds, in steps
        uint32 m_overtimeReported = 0;      // seconds, in steps
        std::map<std::pair<bool, uint32>, uint32> m_ranksReported;   // (spell?, id) -> rank
        bool m_hurt = false;                // an enemy hit the hero this run
        uint32 m_lastHurtMs = 0;            // run time of the last enemy hit
        // Bosses and mini-bosses up: when each arrived, and the damage the
        // hero had taken by then.
        struct BossArrival
        {
            uint32 ms = 0;
            uint32 damageTaken = 0;
        };
        std::map<ObjectGuid, BossArrival> m_bossArrivals;
        uint32 m_damageTaken = 0;           // this run, after Armor and Shields
        // The run log (LevelUp.log): a line a minute, how the run ended, and
        // each boss and mini-boss fight.
        void LogMinute();
        void LogDeath();
        void LogBossFight(Creature const* mob, BonkSpawnKind kind, BossArrival const& arrival);
        uint32 m_nextLogMinute = 1;
        uint32 m_minuteDamage = 0;          // taken this minute, after Armor and Shields
        uint32 m_minuteHealed = 0;
        uint32 m_minuteKills = 0;           // m_kills when the minute began
        uint32 m_minuteLowestHealth = 100;  // %
        std::string m_lastStriker;          // "Name (grunt)": the last enemy to hit the hero
        uint32 m_lastStrikeDamage = 0;
        std::vector<std::pair<uint32, uint32>> m_recentHits;    // run ms, damage: the last 10 seconds
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
        uint32 m_rewardMs = 0;              // the same, times each stage's map type reward (Mastery minutes)
        size_t m_nextEvent = 0;
        std::vector<BonkMapEvent> m_heldEvents;     // waiting out a quiet time
        // The mini-event at a spot under way, if `m_miniEventActive`.
        struct MiniEvent
        {
            BonkMiniEventType type = BONK_MINI_EVENT_HORDE;
            uint32 objectiveId = 0;
            BonkSpot spot;
            ObjectGuid target;              // a goblin or a bounty
            std::vector<ObjectGuid> targets;    // kobolds, shards, a portal (StartMiniEventGroup)
            std::map<ObjectGuid, uint32> stolen;    // kobolds: the experience each carries
            std::map<ObjectGuid, uint32> objectives;    // shards, a portal: their pins
            int32 spawnMs = 0;              // shards, a portal: to their next adds
            uint32 waves = 0;               // a portal's so far
            uint32 total = 0;               // targets it started with
            // A caravan: where it walks, how far it got, its raiders, its blows.
            std::vector<BonkSpot> path;
            size_t pathIndex = 0;
            std::vector<ObjectGuid> raiders;
            uint32 tickMs = 0;
            uint32 blows = 0;               // taken so far
            uint32 maxBlows = 0;            // it falls at this many
            uint8 warned = 0;               // health warnings given
            std::string label;              // its timer's
            uint32 leftMs = 0;
            uint32 totalSeconds = 0;
            mutable uint32 shownSeconds = 0;    // last sent to the addon
        };
        MiniEvent m_miniEvent;
        bool m_miniEventActive = false;
        // A shrine standing this stage.
        struct Shrine
        {
            BonkShrineType type = BONK_SHRINE_LEY_LINE;
            BonkSpot spot;
            uint32 objectiveId = 0;
            ObjectGuid model;
            uint32 progressMs = 0;          // Ley Line: time stood in it; Moonwell: its bar is up
            uint32 leftMs = 0;              // Moonwell: time it still heals
            uint32 tickMs = 0;              // Moonwell: to its next heal
            bool active = false;            // Moonwell, Challenge Altar: set off
            std::vector<ObjectGuid> pack;   // Challenge Altar: its guards
        };
        std::vector<Shrine> m_shrines;
        uint32 m_shrinesToPlace = 0;        // left to place this stage
        uint32 m_shrineTimer = 0;
        bool m_moonwellPlaced = false;      // at most one a stage
        bool m_stonePlaced = false;         // exactly one a stage...
        uint32 m_stoneAt = 0;               // ...placed when m_shrinesToPlace counts down to this
        bool m_captureShown = false;        // the addon shows a capture bar
        uint32 m_captureSendMs = 0;
        uint32 m_greedStacks = 0;           // the whole run
        float m_shrineBonuses[MAX_BONK_PASSIVE_EFFECT] = {};    // Ley Line buffs picked, the whole run
        // A Ley Line buff card: a stat, how much, its rarity.
        struct LeyBuff
        {
            BonkPassiveEffect effect = BONK_PASSIVE_POWER;
            float value = 0.0f;
            BonkRarity rarity = BONK_RARITY_COMMON;
            uint32 cardId = 0;              // a Darkmoon Card in place of a stat (Core loop v2 phase E)
        };
        std::vector<LeyBuff> m_leyOffer;    // shown now
        uint32 m_pendingLeyOffers = 0;      // captured, not yet offered
        // The wandering vendor: a slot of his shelf.
        enum VendorSlotState : uint8
        {
            VENDOR_FOR_SALE = 0,
            VENDOR_SOLD     = 1,
            VENDOR_GONE     = 2,            // no longer fits the build
        };
        struct VendorSlot
        {
            Offer card;                     // unless a charge
            bool isCharge = false;          // the special slot: a Reroll, Banish or Skip charge
            uint8 charge = 0;
            uint32 price = 0;               // Valor
            bool locked = false;            // back at the next visit if not bought
            VendorSlotState state = VENDOR_FOR_SALE;
        };
        std::vector<VendorSlot> m_vendorStock;  // while he is here
        std::vector<VendorSlot> m_vendorKept;   // locked, for his next visit
        std::vector<uint32> m_vendorVisits;     // stage seconds he still comes at, earliest first
        ObjectGuid m_vendorGuid;
        uint32 m_vendorObjective = 0;
        uint32 m_vendorLeftMs = 0;
        uint32 m_vendorCheckMs = 0;
        uint32 m_vendorRerolls = 0;             // this visit
        bool m_vendorOpen = false;              // his shelf is shown; the run is paused
        uint32 m_nextMiniEventMs = 0;       // into the stage
        BonkMiniEventType m_lastMiniEvent = MAX_BONK_MINI_EVENT;
        float m_gruntRadius = 0.0f;         // the biggest grunt this stage: bruisers are bigger
        int32 m_spawnTimer = 0;
        int32 m_eliteTimer = 0;
        size_t m_waveIndex = 0;             // the timeline block in force
        bool m_waveEntered = false;         // this stage's first block began
        size_t m_waveWarned = 0;            // the block the addon was told is coming
        uint32 m_miniEventSlots = 0;        // this block's mini-event, not yet started (bits by type)
        uint32 m_overtimeWaves = 0;
        int32 m_overtimeTimer = 0;
        float m_spawnDamageScale = 1.0f;    // overtime's shades hit harder
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
        uint32 m_orbTimer = 0;
        std::vector<OrbMerge> m_orbMerges;
        uint32 m_orbMergeTimer = 0;
        uint32 m_lastOrbMergeId = 0;
        std::vector<ShardDrop> m_shardDrops;
        // Turkey legs lying where an enemy died: creatures (BONK_HEAL_PICKUP),
        // so they can fly to a hurt hero as the experience orbs do.
        struct HealDrop
        {
            ObjectGuid guid;
            bool pulled = false;        // flying to the hero, aimed at:
            float aimX = 0.0f, aimY = 0.0f, aimZ = 0.0f;
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
            bool recast = false;        // Count: the spell around the hero or its cone again
            bool field = false;         // a trap's field ticks: its damage and keyword on everything inside
            bool springTrap = false;    // a thrown trap (worn by stand-in `target`) goes off
            bool releaseLook = false;   // ...and its stand-in goes back
            bool petCleave = false;     // a frenzied pet's bite (`target` bitten) strikes the enemies in front of it
            bool pounce = false;        // a pet (`source`) lands its pounce on `target`
            float share = 1.0f;         // of the weapon's damage
            ObjectGuid target;
            ObjectGuid source;          // petCleave: the pet
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
        // A Call Pet beast's skills: when each is ready again (ms, by the
        // family's ability order), and until when it is pouncing.
        struct PetState
        {
            std::vector<uint32> readyAtMs;
            uint32 busyUntilMs = 0;
        };
        std::map<ObjectGuid, PetState> m_petStates;
        uint32 m_petFrenzyUntil = 0;        // ms: Pack Leader's frenzy
        bool m_petsFrenzied = false;        // as the pets were last set up
        int32 m_defensiveTimer = 0;         // until the class defensive can fire again
        bool m_dodged = false;              // a dodge Revenge has not answered yet
        uint32 m_retaliateUntil = 0;        // ms: hits taken until then strike back
        std::vector<ObjectGuid> m_retaliations;     // enemies that hit the hero meanwhile
        float m_bleedHeal = 0.0f;           // Bloodthirst healing not yet given
        BonkTelegraphs m_telegraphs;
        BonkSpawnPoints m_spawnPoints;
        BonkObjectives m_objectives;
        uint32 m_shield = 0;                // absorb left
        uint32 m_shieldIdleMs = 0;          // since the hero last took damage
        uint32 m_shieldSendTimer = 0;
        uint32 m_shieldSent = 0;            // what the addon last heard
        uint32 m_shieldMaxSent = 0;
        uint32 m_chestsBought = 0;
        uint32 m_lastLevel = 1;
        uint32 m_pendingLevels = 0;
        // Past the level cap the run counts levels on: experience gathers
        // here, in hundredths of a grunt's kill, and each level is a pick.
        uint32 m_overcapLevel = 0;          // levels gained past the cap
        uint32 m_overcapXp = 0;

        // Keywords (BonkKeywords.cpp).
        void UpdateKeywords(uint32 diff);
        void SpreadKeywords(Creature const* dead);
        // Frozen: held for a moment, then immune for a while. Elites and
        // bosses are never held but count as Frozen for Shatter crits.
        bool Freeze(Creature* mob);
        bool IsFrozen(Unit const* victim) const;
        uint32 CountKeywords(Unit const* victim) const;
        // Payoffs and Hunter's Mark, times the damage of `spell` (null for
        // a swing) on `victim`; the vulnerabilities are debuffs the core reads.
        float GetKeywordDamageMultiplier(SpellEntry const* spell, Unit const* victim) const;
        // % more damage Hunter's Mark adds: the keyword's, grown by the
        // ranks and Mark strength of the hero's spell that applies it.
        float GetHuntersMarkBonus() const;
        // % more damage `victim` takes while a slow of the hero's holds it
        // (Frost Trap), grown by that spell's ranks and upgrade.
        float GetSlowedBonus(Unit const* victim) const;
        // `points`: a stack's damage or effect, 0 for the keyword's own.
        SpellAuraHolder* AddKeywordAura(Creature* mob, BonkKeywordInfo const& info, uint32 stacks, uint32 durationMs, int32 points = 0);
        int32 GetKeywordPoints(BonkKeywordInfo const& info) const;
        // Damage over time (Burning, Bleed, Poison), Megabonk-style: each
        // stack runs out on its own and keeps the damage it was applied
        // with; the debuff carries their count and deals their sum.
        struct DotStack
        {
            uint32 untilMs = 0;
            int32 points = 0;           // its damage a tick, before Power
            uint32 spellId = 0;         // the hero's spell that put it on (CreditDamageOverTime), 0: none
        };
        void AddDotStacks(Creature* mob, BonkKeywordInfo const& info, uint32 stacks, uint32 durationMs, int32 points, uint32 spellId);
        // Drops stacks that ran out and shows the rest on the debuff.
        void UpdateDotStacks(uint32 diff);
        void SyncDotAura(Creature* mob, BonkKeywordInfo const& info, std::vector<DotStack> const& stacks);
        // Knockbacks: never elites or bosses, and once per enemy every while.
        bool CanKnockBack(Creature const* mob);
        void RebuildWeaponSpells();
        OwnedWeapon const* FindOwnedBySpell(uint32 spellId) const;

        struct PendingKeyword
        {
            ObjectGuid mob;
            BonkKeyword keyword = BONK_KEYWORD_NONE;
            uint8 stacks = 0;
            uint32 durationMs = 0;      // 0: the keyword's own
            float damageScale = 1.0f;
            uint32 spellId = 0;         // the hero's spell it comes from
        };
        std::vector<PendingKeyword> m_pendingKeywords;
        // The keyword `owned` puts on what it hits (a pet: its family's),
        // with its stacks, how long they last and how hard they hit; false
        // if it applies none.
        bool GetWeaponKeyword(OwnedWeapon const& owned, PendingKeyword& keyword) const;
        // An enemy dealt a killing blow: what it carried, to pass on.
        struct DyingKeyword
        {
            uint32 stacks = 0;
            int32 points = 0;           // damage over time: a stack's, on average
            uint32 spellId = 0;         // ...and the spell that put most of them on
            uint32 leftMs = 0;          // Hunter's Mark: the time it had left
        };
        std::map<ObjectGuid, std::map<uint8, DyingKeyword>> m_dyingKeywords;
        std::map<ObjectGuid, std::map<uint8, std::vector<DotStack>>> m_dotStacks;  // enemy -> keyword -> its stacks
        uint32 m_dotTimer = 0;
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

        // `.bonk stress` (BonkRunStress.cpp).
        struct StressTicks
        {
            uint32 count = 0;
            uint64 totalMs = 0;
            uint32 worstMs = 0;
            uint32 slow = 0;                // over twice the 25 ms tick
            uint64 runUs = 0;               // of them, spent in BonkRun::Update
            uint32 worstRunUs = 0;
            void Add(uint32 tickMs, uint32 usedUs);
        };
        struct Stress
        {
            bool on = false;
            float bonuses[MAX_BONK_PASSIVE_EFFECT] = {};    // read by GetPassiveTotal
            uint32 startMs = 0;             // run time (m_totalMs) it was turned on
            uint32 reportMs = 0;            // run time of the last report
            uint32 holdCheckMs = 0;
            StressTicks window;             // since the last report
            StressTicks total;              // since it was turned on
            uint32 alive = 0;               // enemies at the last hold check
            uint32 held = 0;                // of them, held
            std::map<ObjectGuid, uint32> heldSince;     // enemy held -> run time it was first seen held
            std::set<ObjectGuid> heldReported;          // held too long, reported once
        };
        // Times one BonkRun::Update while the stress test is on.
        class StressTick
        {
            public:
                StressTick(BonkRun& run, uint32 diff);
                ~StressTick();
            private:
                BonkRun& m_run;
                uint32 m_diff;
                uint64 m_startUs;
        };
        void StartStress();
        void StopStress();
        // After each timed update: the hold check and the report.
        void UpdateStress();
        void CheckStressHolds();
        void ReportStress(StressTicks const& ticks, bool summary) const;
        Stress m_stress;
};

#endif
