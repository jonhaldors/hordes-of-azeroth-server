#ifndef MANGOS_BONKRUN_H
#define MANGOS_BONKRUN_H

#include "BonkMastery.h"
#include "BonkMgr.h"
#include "BonkObjectives.h"
#include "BonkSpawnPoints.h"
#include "BonkTelegraph.h"
#include "ObjectGuid.h"
#include "nonstd/optional.hpp"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

class Creature;
class GameObject;
class Item;
class Player;

enum BonkMiniEventType : uint8
{
    BONK_MINI_EVENT_HORDE  = 0,
    BONK_MINI_EVENT_GOBLIN = 1,
    BONK_MINI_EVENT_BOUNTY = 2,
    BONK_MINI_EVENT_KOBOLDS  = 3,
    BONK_MINI_EVENT_SCOURGE  = 4,
    BONK_MINI_EVENT_PORTAL   = 5,
    BONK_MINI_EVENT_STAMPEDE = 6,
    BONK_MINI_EVENT_CARAVAN  = 7,
    MAX_BONK_MINI_EVENT
};

char const* GetBonkMiniEventName(BonkMiniEventType type);
bool ParseBonkMiniEvent(char const* name, BonkMiniEventType& type);

enum BonkShrineType : uint8
{
    BONK_SHRINE_LEY_LINE = 0,
    BONK_SHRINE_GREED    = 1,
    BONK_SHRINE_ALTAR    = 2,
    BONK_SHRINE_MOONWELL = 3,
    BONK_SHRINE_STONE    = 4,
    MAX_BONK_SHRINE
};

char const* GetBonkShrineName(BonkShrineType type);
bool ParseBonkShrine(char const* name, BonkShrineType& type);

struct BonkEnemyHit
{
    ObjectGuid caster;
    ObjectGuid looker;
    std::string striker;
    uint32 windId = 0;
    uint32 spellId = 0;
    uint32 damage = 0;
    float scale = 1.0f;
    uint32 castKit = 0;
    uint32 impactKit = 0;
    bool facesShape = false;
};

enum class BonkHeroLook : uint8
{
    SpikedShield,
    SteadyAim,
    Bladestorm,
    Vengeance,
    VengeanceCharged,
    ArcanePower,
    PhaseRush,
    Count
};

enum class BonkHeroRing : uint8
{
    BloodAura,
    IcePath,
    TimeBubble,
    Count
};

class SpellAuraHolder;
class SpellCaster;
class Unit;

class BonkRun
{
    public:
        BonkRun(Player* player, BonkTier const& tier, BonkRunChoice const& choice = BonkRunChoice());
        bool IsDungeon() const { return m_dungeonId != 0; }
        bool IsChallenge() const { return m_dungeonId != 0 || m_invaderId != 0; }
        bool IsInvasion() const { return m_invaderId != 0; }

        bool Begin(std::string& error);
        void Update(uint32 diff);
        void Abort();
        void Surrender();
        bool SkipTime(uint32 seconds, std::string& error);
        bool StartMiniEventNow(BonkMiniEventType type, std::string& error);
        bool PlaceShrineNow(BonkShrineType type, std::string& error);
        bool PlacePowerupNow(uint32 entry, std::string& error);
        void UseShrine(GameObject* go);
        bool PickLeyBuff(uint32 index, std::string& error);
        uint32 GetGreedRewardPercent() const;
        float GetGreedCrowdPercent() const;
        void OpenVendor(Creature* vendor);
        bool BuyFromVendor(uint32 index, std::string& error);
        bool LockVendorSlot(uint32 index, std::string& error);
        bool RerollVendor(std::string& error);
        void CloseVendor();
        bool SpawnVendorNow(std::string& error);

        bool IsActive() const { return m_state == STATE_RUNNING; }
        bool SaveFromDeath();
        bool IsPaused() const { return m_paused; }
        bool IsHeroPerched() const { return m_perched; }
        uint32 GetGlyphDashCharges() const { return uint32(GetGlyphValue(BONK_GLYPH_DASH, 0)); }
        float GetGlyphDashRecharge() const { return 1.0f + GetGlyphValue(BONK_GLYPH_DASH, 1) / 100.0f; }
        void SetMenuOpen(bool open);
        bool IsFinished() const { return m_state == STATE_FINISHED; }
        bool IsPortalOpen() const { return m_state == STATE_PORTAL; }
        BonkMap const& GetMap() const { return m_map; }
        BonkTier const& GetTier() const { return m_tier; }
        uint32 GetEliteAffixes() const { return m_tier.eliteAffixes + m_bonuses.eliteAffixes; }
        uint32 GetEvolveRank() const;
        void BringBossEarlier();
        uint32 GetStage() const { return m_stage; }

        void OnMobKilled(Creature* mob, BonkSpawnKind kind, Unit* killer = nullptr);
        void OnMobStuck(Creature* mob);
        void OnWeaponDamage(uint32 damage);
        void OnHeroHealed(uint32 amount) { m_healedUnreported += amount; m_minuteHealed += amount; }
        uint32 MitigateDamage(uint32 damage);
        void HitAgain(SpellEntry const* spellInfo, Unit* target, float damage, SpellEffectIndex effectIndex, WeaponAttackType attackType);
        void AddNecroticStack();
        float GetDamageMultiplier(SpellEntry const* spell, Unit const* victim, float powerShare = 1.0f, float extraPower = 0.0f) const;
        float RollDamageSpread() const;
        void FloatSummonDamage(Unit* victim, uint32 damage, SpellEntry const* spellInfo) const;
        float GetPowerShare(Unit const* attacker) const;

        static BonkRun* FromAttack(SpellCaster const* attacker, Unit const* victim);
        static uint32 GetMissileDelay(SpellCaster const* caster, Unit const* target, float speed);
        BonkSpot const& GetHeroPositionThisTick() const { return m_heroNow; }
        void ReportSandboxSwing(Creature const* mob) const;
        bool RollDodge(Unit const* victim) const;
        float GetCritChance(SpellEntry const* spell = nullptr, Unit const* victim = nullptr, Unit const* attacker = nullptr) const;
        float GetSummonDamageMultiplier(Unit const* attacker) const;
        float GetMobDamageMultiplier(Creature const* mob) const;
        void OnHeroDodge(Unit* attacker);
        void OnHeroStruck(Creature* attacker);
        void OnPeriodicDamage(uint32 spellId, uint32 damage);
        void OnCriticalStrike(SpellCaster const* caster, SpellEntry const* spell, Unit const* victim);
        void CountThunderHit(SpellEntry const* spell);
        static void SetPeriodicHit(bool periodic);
        float GetDotGrowth(Unit const* victim, uint32 spellId) const;

        void OnHeroHitBy(Creature* attacker, uint32 damage);
        void ModifyHeroHit(Unit* victim, uint32& damage);
        void OnHeroHit(Unit* victim, uint32 damage, SpellEntry const* spell);
        bool GiveCard(uint32 cardId, std::string& error);
        bool ForceEvolve(uint32 weaponId, std::string& error);
        bool GivePassive(uint32 passiveId, std::string& error);

        enum GearChoice
        {
            GEAR_CHOICE_EQUIP,
            GEAR_CHOICE_LOOT,
            GEAR_CHOICE_SALVAGE,
        };
        bool ChooseGear(GearChoice choice, uint8 slot, std::string& error);
        bool ChooseGlyph(uint32 choice, std::string& error);
        bool UseHidden(GameObject* go);
        bool UseItem(Item* item);
        struct VaultFind
        {
            ObjectGuid guid;
            uint32 entry = 0;
            uint8 quality = 0;
            bool worn = false;
            uint32 sellCopper = 0;
            bool inVault = false;
        };
        bool IsOwnVault(GameObject const* vault) const;
        void OpenVault();
        void CloseVault();
        bool PutInVault(uint32 guidLow, std::string& error);
        bool TakeFromVault(uint32 guidLow, std::string& error);
        std::vector<VaultFind> GetVaultFinds() const;
        std::string DescribeBroughtGearFor(uint32 entry) const;
        uint32 GetVaultSlots() const;
        void FitVaultToSlots();
        bool IsVaultEmptyWithFinds() const;

        void OnWeaponHit(Unit* victim, SpellEntry const* spell);
        void OnPetBite(Creature* pet, Unit* victim);
        void OnKillingBlow(Unit* victim);
        bool ApplyKeyword(Creature* mob, BonkKeyword keyword, uint32 stacks, uint32 durationMs = 0, float damageScale = 1.0f, uint32 sourceSpellId = 0);
        void CreditDamageOverTime(Unit const* victim, uint32 spellId, uint32 damage, std::vector<std::pair<uint32, uint32>>& credits) const;
        uint32 GetKeywordStacks(Unit const* victim, BonkKeyword keyword) const;
        bool LimitStun(Creature* mob, int32& duration);
        bool LimitRoot(Creature* mob, int32& duration);
        void AddTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
            std::function<void()> onFire = nullptr, bool lingers = false);
        void AddEnemyTelegraph(Creature const* caster, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size,
            uint32 durationMs, std::function<void()> onFire);
        uint32 AddObjective(BonkObjectiveKind kind, float x, float y, float z, std::string const& label, uint32 seconds);
        void RemoveObjective(uint32 id);
        BonkObjectives const& GetObjectives() const { return m_objectives; }
        bool TakeSpawnPoint(float minDistance, float maxDistance, BonkSpot& spot) { return m_spawnPoints.Take(m_player, minDistance, maxDistance, spot); }
        BonkSpawnPoints const& GetSpawnPoints() const { return m_spawnPoints; }
        uint32 GetDifficultyStep() const { return std::min(2u, m_tier.eliteAffixes ? m_tier.eliteAffixes - 1 : 0u); }
        void SpawnPhaseAdds(Creature const* boss, uint32 count, uint32 elites);
        void StartVoidZone(BonkEnemyHit const& hit, float x, float y, float z, float radius, uint32 durationMs);
        void Erupt(BonkEnemyHit const& hit, float x, float y, float z, float radius);
        void ShowKitAt(uint32 kit, float x, float y, float z);
        void ShowLineImpacts(uint32 kit, float x, float y, float z, float orientation, float length);
        ObjectGuid PlaceBoltTarget(float x, float y, float z, uint32 lifetimeMs);
        void LaunchEnemyMissile(ObjectGuid caster, ObjectGuid target, uint32 spellId, uint32 delayMs, uint32 flightMs, float x, float y, float z);
        bool ReserveBolt(uint32 landMs);
        void LandBolt(BonkEnemyHit const& hit, ObjectGuid targetGuid, float x, float y, float radius);
        void LandWarning(BonkEnemyHit const& hit, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size);
        static std::string DescribeStriker(Creature const* attacker);
        std::vector<std::string> DescribeMob(Creature const* mob) const;
        float RollCritMultiplier(bool weaponAttack, Unit const* victim = nullptr) const;

        void SendStats() const;
        std::vector<std::string> DescribeScaling() const;
        void ToggleStress();
        bool Pick(uint32 choice, std::string& error);
        bool Reroll(std::string& error);
        bool Banish(uint32 choice, std::string& error);
        bool Skip(std::string& error);
        bool Lock(uint32 choice, std::string& error);

        void UseChest(GameObject* chest);
        bool IsOwnPortal(GameObject const* portal) const;
        BonkMap const* GetNextMap() const;
        void EnterNextStage();
        void GoHome();

        void SendFullState() const;

        void OnDash();
        void LogPerfDip();
        bool IsBlinkDash() const;
        uint32 GetBlinkCharges() const;
        float GetBlinkRechargeRate() const;
        float GetDashRechargeRate() const;
        uint32 GetBlinkInvulnerableMs() const;

        void MakeSandbox(bool godBefore) { m_sandbox = true; m_sandboxGodBefore = godBefore; }
        bool IsSandbox() const { return m_sandbox; }
        void ShowSandboxMenu(Creature* keeper, uint32 menu);
        void SelectSandboxOption(Creature* keeper, uint32 sender, uint32 action);
        bool RunSandboxVerb(std::string const& verb, uint32 first, uint32 second, std::string& error);
        void LeaveSandbox();
        time_t GetSkyTime() const;

    private:
        enum State
        {
            STATE_ENTERING,
            STATE_RUNNING,
            STATE_PORTAL,
            STATE_ENDING,
            STATE_FINISHED,
        };

        enum OfferSource
        {
            OFFER_LEVEL,
            OFFER_CHEST,
        };

        struct OwnedWeapon
        {
            BonkWeapon weapon;
            uint32 rank = 1;
            float damageBonus = 0.0f;
            float upgrades[MAX_BONK_SPELL_UPGRADE] = {};
            std::string rarities;
            uint32 casts = 0;
            int32 timer = 0;
            std::map<ObjectGuid, uint32> recentTargets;
            std::map<ObjectGuid, uint32> carriers;
            std::vector<ObjectGuid> pets;
            bool killed = false;
            bool held = false;
            bool pieResync = false;
            mutable uint32 shownIntervalMs = 0;
            mutable std::string shownDamage;
        };

        struct Offer
        {
            bool isWeapon = true;
            bool isCard = false;
            uint32 id = 0;
            uint32 newLevel = 1;
            BonkRarity rarity = BONK_RARITY_COMMON;
            BonkRankStep step = BONK_STEP_NEW;
            uint32 evolveTo = 0;
            BonkSpellUpgrade upgrades[BONK_MAX_RANK_UPGRADES] = {};
            bool locked = false;
        };

        struct Orb
        {
            ObjectGuid guid;
            uint32 experience = 0;
            bool pulled = false;
            float aimX = 0.0f, aimY = 0.0f, aimZ = 0.0f;
            BonkOrb colour = BONK_ORB_BLUE;
            uint32 droppedMs = 0;
            uint32 mergeId = 0;
        };
        struct OrbMerge
        {
            uint32 id = 0;
            ObjectGuid anchor;
            BonkOrb colour = BONK_ORB_BLUE;
            int32 leftMs = 0;
        };

        struct ShardDrop
        {
            ObjectGuid guid;
            uint32 count = 0;
            ObjectGuid marker;
        };

        BonkMap const* ChooseNextMap();
        bool TeleportToStage(BonkMap const& map, std::string& error);
        void LearnStarts(std::vector<BonkSpot> const& spots);
        void StartStage();
        void UpdateRunning(uint32 diff);
        void UpdateOffers();
        void DropOffers();
        void OfferLevelsBeforeDing();
        void UpdateEvents();
        void UpdateMiniEvents(uint32 diff);
        BonkMiniEventType PickMiniEvent(uint32 allowed) const;
        bool StartMiniEvent(BonkMiniEventType type);
        bool SpawnHorde();
        bool SpawnTreasureGoblin();
        bool SpawnBounty();
        bool SpawnKoboldThieves();
        bool FindKoboldSpot(float heading, float& x, float& y, float& z) const;
        bool SpawnScourgeInvasion();
        bool SpawnDemonPortal();
        bool SpawnStampede();
        bool SpawnCaravan();
        bool UpdateCaravan(uint32 diff);
        void PauseCaravan(bool paused);
        void EndCaravan(bool arrived);
        Creature* SummonCaravan(uint32 entry, float x, float y, float z, float orientation);
        void StartMiniEventGroup(std::string const& label, uint32 seconds);
        void UpdateMiniEventGroup(uint32 diff);
        void OnMiniEventGroupKilled(Creature* mob);
        void SpawnEventAdds(Creature const* source, uint32 count, uint32 eliteCount);
        bool IsMiniEventTarget(ObjectGuid const& guid) const;
        bool FindLyingOrb(float heading, float spread, float minDistance, float maxDistance, float& x, float& y, float& z) const;
        uint32 StealOrbs(float x, float y, float range);
        void StartMiniEventTarget(Creature* target, std::string const& label, uint32 seconds);
        void UpdateMiniEventTarget(uint32 diff);
        void OnMiniEventTargetKilled(Creature* mob);
        void SendMiniEventTimer(bool force = false) const;
        void EndMiniEvent();
        void DespawnMiniEvent();
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
        uint32 GetMoonwellSeconds() const;
        void ResetShrines();
        void DespawnShrines();
        void ResetVendor();
        void UpdateVendor(uint32 diff);
        bool IsBossUp() const;
        bool SpawnVendor();
        void StockVendor(bool reroll);
        void CheckVendorStock();
        uint32 GetVendorPrice(Offer const* card) const;
        uint32 GetVendorRerollCost() const;
        uint32 GetVendorRestocksLeft() const;
        uint32 GetSkyKey() const;
        void UpdateSky();
        void RefreshHeroPositionNow();
        BonkSpot m_heroNow;
        void ClearSky();
        void ApplyVendorCard(Offer const& card);
        void SendVendor() const;
        void DespawnVendor();
        void UpdateSpawns(uint32 diff);
        void UpdateWeapons(uint32 diff);
        void UpdateBoundary(uint32 diff);
        void UpdateNatives(uint32 diff);
        void UpdateRecovery(uint32 diff);
        void ReportQuestCounts();
        void UpdateShield(uint32 diff);
        uint32 GetMaxShield() const;
        void UpdateNecrotic(uint32 diff);
        void SetNecroticStacks(uint32 stacks);
        uint32 ReduceHealing(uint32 heal) const;
        void OnAffixedDeath(Creature* mob);
        void UpdatePools(uint32 diff);
        struct VoidZone
        {
            BonkEnemyHit hit;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float radius = 0.0f;
            uint32 leftMs = 0;
            uint32 tickMs = 0;
        };
        std::vector<VoidZone> m_voidZones;
        std::vector<uint32> m_boltLandings;
        struct ImpactLook
        {
            ObjectGuid guid;
            uint32 kit = 0;
            uint32 leftMs = 0;
        };
        std::vector<ImpactLook> m_impactLooks;
        void UpdateVoidZones(uint32 diff);
        void UpdateImpactLooks(uint32 diff);
        void ShowImpact(BonkEnemyHit const& hit, float x, float y, float z);
        void ShowAttackLook(BonkEnemyHit const& hit, BonkTelegraphShape shape, float x, float y, float z, float orientation, float size);
        Creature* SummonHazard(float x, float y, float z);
        void StrikeHero(BonkEnemyHit const& hit, uint32 spellId, Unit* source);
        void NoteStruck(std::string const& striker);
        void DespawnPools();
        void RunEvent(BonkMapEvent const& event);
        void OpenPortal(Creature* boss);
        void PlacePortal(float x, float y, float baseZ);

        void PruneMobs();
        BonkMapCreature const* PickCreature(BonkSpawnKind kind, int32 archetype = -1) const;
        BonkMapCreature const* PickTrash() const;
        uint32 CountTrash() const;
        uint32 CountBruisers() const;
        uint32 GetMaxBruisers() const;
        BonkWave const& GetWave() const;
        bool IsInRoster(BonkArchetype archetype) const;
        uint32 CountRoster() const;
        float GetCurveMinAlive() const;
        float GetOpeningProgress() const;
        float GetOpeningEase() const;
        uint32 GetSpawnTarget() const;
        float GetCrowdOverflow() const;
        void UpdateWaves();
        void EnterWave(size_t index);
        uint32 GetFirstMinibossSecond() const;
        std::string DescribeTimelineMarks() const;
        void UpdateOvertime(uint32 diff);
        BonkScalingPoint GetScalingNow() const;
        float GetHeroBaseHealth() const;
        bool IsQuietTime() const;
        static bool IsQuietEvent(BonkEventType type);
        uint32 CountAlive(uint32 entry) const;
        bool FindSpawnPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const;
        bool FindSpawnPositionAt(float angle, float distance, float& x, float& y, float& z) const;

        void UpdateHeading(uint32 diff);
        bool GetHeading(float& angle) const;
        float GetForwardAngle() const;
        bool IsAhead(float x, float y, float forward) const;
        uint32 CountAhead(float forward) const;
        uint32 GetFrontCap() const;
        bool FindMobPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const;
        void UpdateLanes(uint32 diff);
        void UpdatePerch(uint32 diff);
        bool IsHeroReachable() const;
        bool IsReachableFrom(float x, float y, float z, Creature const* prober) const;
        bool HasStrandedMobs() const;
        bool FindSpawnPathFrom(float fromX, float fromY, float fromZ, float distance, float& x, float& y, float& z) const;
        void UpdateFlyers(uint32 diff);
        void SpawnFlyerPack(uint32 count);
        bool FindFlyerPosition(float& x, float& y, float& z) const;
        BonkMapCreature const* PickFlyer() const;
        uint32 CountFlyers() const;
        bool IsLane(uint32 sector) const;
        bool IsLaneEdge(uint32 sector) const;
        float GetSectorAngle(uint32 sector) const;
        uint32 GetSectorOf(float x, float y) const;
        std::vector<float> CountSectorCrowd() const;
        int32 PickSector(std::vector<float> const& crowd, bool frontOnly) const;
        bool FindSectorPosition(uint32 sector, float minDistance, float maxDistance, float& x, float& y, float& z) const;
        void SpawnTrashPacks(uint32 count);
        void UpdateStragglers(uint32 diff);
        bool PlacePack(std::vector<Creature*> const& pack, float heading);
        bool PlaceAside(Creature* mob, float heading);
        void MoveMob(Creature* mob, float x, float y, float z);
        Creature* SpawnMob(BonkMapCreature const& creature);
        Creature* SpawnMobAt(BonkMapCreature const& creature, float x, float y, float z, bool veteran = false);
        bool RollVeteran(BonkMapCreature const& creature) const;
        uint32 SpawnFormation(BonkMapEvent const& event);
        std::vector<Creature*> GetMobsInRange(float range, WorldObject const* center = nullptr) const;
        void DespawnMobs();

        GameObject* PlaceObject(uint32 entry, float x, float y, float z, uint32 lifetimeSeconds);
        bool SpawnChest(bool free, float x, float y, float z, uint32 choices = 3);
        void DropChest(Creature const* mob, BonkSpawnKind kind);
        bool SpawnChestNearby();
        bool IsFarFromChests(float x, float y) const;
        uint32 GetChestCost() const;
        uint32 GetChestPrice() const;
        float GetUtility(BonkUpgradeUtility utility) const { return m_bonuses.utility[utility]; }
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

        void QueueGlyphPick();
        void OfferGlyphs();
        void SendGlyphOffer() const;
        void SendGlyphs() const;
        BonkRarity RollGlyphRarity() const;
        BonkGlyph const* GetGlyph(BonkGlyphEffect effect) const;
        float GetGlyphValue(BonkGlyphEffect effect, uint8 index) const;
        float GetGlyphPower() const;
        void OnGlyphKill(Creature const* mob, BonkSpawnKind kind);
        void ModifyGlyphHit(Unit* victim, uint32& damage);
        void UpdateGlyphs(uint32 diff);
        void UpdateGlyphStances();
        void UpdateGlyphChest();
        void RankUpStarter(uint32 ranks);
        bool IsGlyphStill() const;
        bool IsLastStand() const;
        float GetAmbitionReward(BonkSpawnKind kind) const;
        void StrikeStorm(BonkGlyph const& glyph);
        void DropMeteor(BonkGlyph const& glyph);
        std::vector<BonkGlyph const*> m_glyphs;
        std::vector<uint32> m_glyphOffer;
        uint32 m_pendingGlyphPicks = 0;
        uint32 m_glyphPicksGiven = 0;
        std::set<uint32> m_glyphCardIds;
        uint32 m_glyphKills = 0;
        uint32 m_glyphHasteUntilMs = 0;
        uint32 m_glyphMovedMs = 0;
        uint32 m_nomadPenaltyUntilMs = 0;
        bool m_glyphStill = false;
        bool m_glyphLastStand = false;
        bool m_nomadPenalty = false;
        bool m_glyphChestDue = false;
        uint32 m_soulHarvestUntilMs = 0;
        uint32 m_glyphMiniBosses = 0;
        uint32 m_divineShieldStage = 0;
        int32 m_glyphStormTimer = 0;
        int32 m_glyphMeteorTimer = 0;

        void SpawnPowerup(WorldObject const* at = nullptr);
        void UpdatePowerups(uint32 diff, bool spawn);
        void UsePowerup(BonkPowerup const& powerup);
        void UpdateObjectives(uint32 diff);
        void DespawnObjects();

        uint32 GetMobLevel() const;
        uint32 GetSpawnHealth(BonkSpawnKind kind, BonkArchetype archetype, float creatureMultiplier) const;
        float GetMobHealth(uint32 level) const;
        float GetStageHealthMultiplier() const;
        float GetStageDamageMultiplier() const;
        float GetRunMinutes() const { return m_totalMs / float(MINUTE * IN_MILLISECONDS); }
        float GetPassiveTotal(BonkPassiveEffect effect) const;
        float GetStat(BonkPassiveEffect effect) const;
        static float GetStatCap(BonkPassiveEffect effect);
        float GetMagnetRange() const;
        float GetGearTotal(BonkPassiveEffect effect) const;
        float GetHasteRate() const;
        float GetWeaponDamage(OwnedWeapon const& owned) const;
        float GetSpellUpgrade(OwnedWeapon const& owned, BonkSpellUpgrade upgrade) const;
        uint32 GetSpellCount(OwnedWeapon const& owned) const;
        float GetWeaponDurationScale(OwnedWeapon const& owned) const;
        float GetWeaponHasteRate(OwnedWeapon const& owned) const;
        uint32 GetWeaponCooldown(OwnedWeapon const& owned) const;
        float GetWeaponArea(OwnedWeapon const& owned) const;
        uint32 GetWeaponTargets(OwnedWeapon const& owned) const;
        void FireWeapon(OwnedWeapon& owned);
        void GetBasePoints(OwnedWeapon const& owned, SpellEntry const* spellInfo, float damageShare, nonstd::optional<int32>* basePoints) const;
        void StrikeWith(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float damageShare, bool followUp = false,
            Creature* source = nullptr);
        bool CanKill(BonkWeapon const& weapon, Creature const* target) const;
        void SortStrongestFirst(std::vector<Creature*>& mobs) const;
        std::vector<Creature*> GetMobsInCone(OwnedWeapon const& owned, float range) const;
        void KnockBack(Creature* mob);
        float GetMarkBonus(Unit const* victim) const;
        void OnMarkedDeath(Creature const* mob);
        float GetBuffPower() const;
        bool IsReckless() const;
        void DropTraps(OwnedWeapon& owned, bool armed = false);
        void UpdateTraps(uint32 diff);
        void SpringTrap(OwnedWeapon& owned, float x, float y, float z);
        void DespawnTraps();
        void ThrowTrap(OwnedWeapon& owned, float x, float y, float z);
        void ShowTrapSprung(OwnedWeapon const& owned, ObjectGuid const& standIn);
        void DealSpellHit(SpellEntry const* spellInfo, Unit* target, float damage, Unit* caster = nullptr, bool forceCrit = false);
        std::vector<Creature*> GetMobsInLine(OwnedWeapon const& owned, float range, float angle) const;
        bool Howl(OwnedWeapon& owned);
        float GetFrenzyDamage() const;
        bool IsPetFrenzied() const;
        bool IsHowlFrenzied() const;
        float GetBestialWrathFrenzy() const;
        float GetPackPowerShare() const;
        void UpdateDefensive(uint32 diff);
        void UpdatePets(OwnedWeapon& owned);
        void UpdatePetTargets(uint32 diff);
        uint32 GetPetPreyRank(Creature const* mob) const;
        float GetPetBiteDamage(OwnedWeapon const& owned, BonkPetFamily const& family) const;
        void StartPetAttack(OwnedWeapon const& owned, Creature* pet, Creature* target);
        void UsePetAbilities(OwnedWeapon& owned, Creature* pet, Creature* prey);
        bool Pounce(OwnedWeapon& owned, Creature* pet, Creature* prey);
        void PetStrike(OwnedWeapon& owned, Creature* pet, Creature* prey, BonkPetAbility const& ability);
        bool PetPulse(OwnedWeapon& owned, Creature* pet, BonkPetAbility const& ability, Creature* coneTarget = nullptr);
        void DespawnPets();
        bool IsSummon(Unit const* unit) const;
        uint32 GetSummonRoom(OwnedWeapon const& owned);
        uint32 GetWantedSummons(OwnedWeapon const& owned);
        float GetSummonGrowth(OwnedWeapon const& owned) const;
        float GetSummonReach(OwnedWeapon const& owned) const;
        float GetSummonLookScale(OwnedWeapon const& owned, float base) const;
        BonkPetFamily const* GetPetFamily(OwnedWeapon const& owned) const;
        void ScaleRadius(SpellEntry const* spellInfo, float radius);
        void SpreadFrom(Creature const* dead);
        bool HasBehavior(BonkWeaponBehavior behavior) const;
        void UpdatePendingCasts(uint32 diff);
        void LaunchOrb(OwnedWeapon& owned);
        void UpdateWeaponOrbs(uint32 diff);
        void DespawnWeaponOrbs();
        void LaunchProjectiles(OwnedWeapon& owned, BonkProjectileInfo const& info, SpellEntry const* spellInfo, float damageShare,
            WorldObject const* from, Creature* target);
        void LaunchProjectile(OwnedWeapon& owned, BonkProjectileInfo const& info, uint32 spellId, float damageShare,
            WorldObject const* from, float angle, uint32 pierce);
        void UpdateProjectiles(uint32 diff);
        void DespawnProjectiles();
        float GetLeadAngle(WorldObject const* from, Unit const* target, float speed) const;
        void AddSpellShield(uint32 amount);
        void CastSelfArea(OwnedWeapon& owned, SpellEntry const* spellInfo, float areaScale);
        void AddWeapon(BonkWeapon const& weapon);
        void ApplyPassives();

        uint32 GetRootWeapon(BonkWeapon const& weapon) const;
        void SoulSiphon(uint32 killingSpell);
        void AwardMastery();

        void GiveExperience(uint32 xp);
        uint32 GetNextLevelXp() const;
        void SendRunXp() const;
        mutable uint32 m_orbsSinceXpSent = 0;
        void UpdateRunXp(bool levelled);
        bool m_runXpDirty = false;
        uint32 m_runXpSentAt = 0;
        void GiveGold(uint32 copper);
        float GetGoldMultiplier() const;

        void DropOrb(Creature const* mob, uint32 experience, BonkOrb colour);
        bool SpawnOrb(float x, float y, float z, uint32 experience, BonkOrb colour);
        void UpdateOrbs(uint32 diff);
        void UpdateOrbMerges(uint32 diff);
        void MergeOrbs();
        void FinishOrbMerge(OrbMerge const& merge);
        void CollectAllOrbs();
        void PullAllOrbs();
        void DespawnOrbs();

        void DropShards(Creature const* mob, BonkSpawnKind kind);
        void UpdateShards(uint32 diff);
        void CollectAllShards();
        void DropHeal(Creature const* mob, BonkSpawnKind kind);
        void UpdateHeals(uint32 diff);
        void GiveShards(uint32 count);
        ObjectGuid PlaceMarker(GameObject const* go, uint32 spellId, float scale);
        void RemovePickup(ObjectGuid const& goGuid, ObjectGuid const& markerGuid);

        bool OfferUpgrades(OfferSource source, std::vector<Offer> const* avoid = nullptr, std::vector<Offer> const* kept = nullptr,
            std::vector<Offer>* drawn = nullptr);
        std::string DescribeOffer(Offer const& offer, std::string* icon) const;
        std::string DescribeStep(Offer const& offer) const;
        std::string DescribeRankUp(Offer const& offer, BonkWeapon const& weapon) const;
        struct WeaponStrength
        {
            std::string lead;
            std::string tail;
            std::string label;
            float value = 0.0f;
            BonkSpellUpgrade folded = BONK_SPELL_UPGRADE_NONE;
            std::string Describe() const;
        };
        bool GetWeaponStrength(BonkWeapon const& weapon, float damageBonus, float const* upgrades, WeaponStrength& strength) const;
        void AddRankGains(OwnedWeapon& owned, Offer const& offer) const;
        bool RankDamageCounts(OwnedWeapon const& owned) const;
        float GetRankDamage(OwnedWeapon const* owned, BonkRarity rarity) const;
        uint8 GetRankUpgradeCount(OwnedWeapon const* owned, BonkRarity rarity) const;
        bool KeepsWindowUptime(OwnedWeapon const& owned, float const* rolled, BonkSpellUpgrade upgrade, float step) const;
        float GetShownHit(OwnedWeapon const& owned, float extraPower) const;
        float GetShownSpread(OwnedWeapon const& owned) const;
        void ResendShownDamage(OwnedWeapon const& owned) const;
        float GetPetSwingFactor(OwnedWeapon const& owned) const;
        float GetShownTick(OwnedWeapon const& owned, float extraPower) const;
        std::string DescribeShownRange(OwnedWeapon const& owned, float extraPower = 0.0f) const;
        std::string DescribeShownDamage(OwnedWeapon const& owned) const;
        void DescribeOfferDamage(Offer const& offer, std::string& before, std::string& after, std::string& label) const;
        void DescribeOfferStats(Offer const& offer, std::vector<BonkStatChange>& stats) const;
        void AddDamageStats(OwnedWeapon const& owned, OwnedWeapon const& next, std::vector<BonkStatChange>& stats) const;
        void AddUpgradeStats(OwnedWeapon const& owned, OwnedWeapon const& next, bool hasDamage, std::vector<BonkStatChange>& stats) const;
        void GetSpellUpgrades(OwnedWeapon const* owned, float* upgrades) const;
        OwnedWeapon* FindOwned(uint32 weaponId);
        OwnedWeapon const* FindOwned(uint32 weaponId) const;

        BonkRarity RollRarity(BonkRarity minimum) const;
        void RollUpgrades(OwnedWeapon const& owned, Offer& offer) const;
        std::vector<BonkWeaponUpgrade const*> GetUpgradeOptions(OwnedWeapon const& owned, BonkRarity rarity, float const* rolled) const;
        void PickUpgradesInTurn(OwnedWeapon const& owned, Offer& offer, size_t& nextUpgrade) const;
        bool HasteShortensCooldown(OwnedWeapon const& owned, float alreadyRolled, float step) const;
        bool OfferGainsAnything(OwnedWeapon const& owned, Offer const& offer) const;
        float GetRawWeaponHaste(OwnedWeapon const& owned) const;
        Offer MakeRankOffer(OwnedWeapon const& owned, BonkRarity minimum) const;
        Offer MakeEvolveOffer(OwnedWeapon const& owned, uint32 evolveTo) const;
        std::vector<BonkWeapon const*> GetOpenEvolutions(OwnedWeapon const& owned) const;
        bool IsOpenEvolution(OwnedWeapon const& owned, uint32 evolvedId) const;
        bool IsEvolutionBanished(uint32 evolvedId) const;
        uint32 PickEvolution(OwnedWeapon const& owned) const;
        bool CanEvolve(OwnedWeapon const& owned) const;
        float GetLegendaryChance(OwnedWeapon const& owned) const;
        std::string DescribeEvolutionChance(OwnedWeapon const& owned, uint32& chance) const;
        OwnedWeapon const* FindAscensionWeapon() const;
        void UseAscension(OwnedWeapon const& owned);
        void ShowAllEvolutions(std::vector<Offer>& cards) const;
        std::string DescribeWeaponStats() const;
        void ApplyStep(OwnedWeapon& owned, Offer const& offer);
        void EvolveWeapon(OwnedWeapon& owned, BonkWeapon const& evolved);
        bool RefreshKeptOffer(Offer& offer) const;
        bool IsPassiveAllowed(BonkPassive const& passive) const;
        bool IsPassiveCapped(BonkPassive const& passive) const;
        size_t GetSpellSlots() const;
        size_t GetPassiveSlots() const;
        void CloseOffer(uint32 pickedIndex);
        static std::pair<bool, uint32> GetBanishKey(Offer const& offer);

        struct OwnedCard
        {
            BonkCard const* card = nullptr;
            uint32 copies = 0;
        };
        bool OfferCards(size_t offerSize, std::vector<Offer> const* avoid, std::vector<Offer> const* kept, std::vector<Offer>* drawn);
        bool IsCardInPool(BonkCard const& card) const;
        void GiveStartCard();
        void GiveStartLevels();
        void AddCard(BonkCard const& card);
        void RemoveCard(BonkCard const& card);
        void SyncGearCards();
        std::vector<uint32> m_gearCards;
        uint32 GetCardCopies(uint32 cardId) const;
        float GetCardTotal(BonkCardEffect effect, uint8 index) const;
        float GetCardChance(BonkCardEffect effect) const;
        uint32 GetCardCooldown(BonkCardEffect effect) const;
        bool HasCard(BonkCardEffect effect) const { return m_cardCopies[effect] > 0; }
        float GetCardPower(Unit const* victim) const;
        void UpdateCards(uint32 diff);
        void HealHero(uint32 amount, uint32 textSpellId = 0);
        bool IsCardImmune() const { return m_cardImmuneUntilMs > m_totalMs; }
        void OnCardHitTaken();
        void OnShieldBroken();
        bool RollFreeChest() const;
        void SendCards() const;
        void StartCardCooldown(BonkCardEffect effect, uint32 ms);
        void SendCardCooldown(BonkCardEffect effect) const;
        void SendCardCooldowns() const;
        std::string DescribeCardNow(BonkCard const& card) const;
        void SendCardStatus(bool force) const;
        mutable std::map<uint32, std::string> m_cardNowSent;
        uint32 m_cardCooldownTotals[MAX_BONK_CARD_EFFECT] = {};
        std::vector<OwnedCard> m_cards;
        float m_cardStats[MAX_BONK_PASSIVE_EFFECT] = {};
        uint32 m_cardCopies[MAX_BONK_CARD_EFFECT] = {};
        BonkCard const* m_cardByEffect[MAX_BONK_CARD_EFFECT] = {};
        int32 m_cardCooldowns[MAX_BONK_CARD_EFFECT] = {};
        uint32 m_cardTickMs = 0;
        uint32 m_cardImmuneUntilMs = 0;
        uint32 m_cardSwiftUntilMs = 0;
        uint32 m_cardSurgeUntilMs = 0;
        uint32 m_cardKills = 0;
        float m_cardValorCarry = 0.0f;
        uint32 m_soulstones = 0;
        uint32 m_cardCritsSeen = 0;
        uint32 m_stillSinceMs = 0;
        bool m_cardProc = false;

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
        bool EquipRunGear(uint32 entry, uint8 slot, std::string& error);
        Item* StoreRunGear(uint32 entry, std::string& error);
        bool OfferSandboxGear(uint32 entry, std::string& error);
        std::vector<uint8> GetGearSlotChoices(uint32 entry) const;
        void SnapshotBroughtGear();
        void RestoreBroughtGear();
        void ResolveRunGear(bool extract);
        void PlaceVault(float x, float y, float baseZ);
        void DespawnVault();
        void SendVault() const;
        bool CheckVaultReach(std::string& error) const;
        ObjectGuid FindRunGear(uint32 guidLow) const;
        bool IsInVault(ObjectGuid const& guid) const;
        uint32 GetGearGold(uint8 quality) const;
        ObjectGuid m_vaultGuid;
        std::vector<ObjectGuid> m_vault;
        std::vector<GearDrop> m_gearDrops;
        std::vector<uint32> m_pendingGear;
        uint32 m_gearOfferEntry = 0;
        uint32 m_gearTimer = 0;
        std::vector<ObjectGuid> m_runGear;
        std::map<uint8, ObjectGuid> m_broughtGear;

        void MaybeDropHearthstone(BonkSpawnKind kind);
        void UpdateHearth(uint32 diff);
        void EndHearth();
        uint32 GetHearthMs() const;
        bool m_hearthDropped = false;
        bool m_hearthActive = false;
        uint32 m_hearthMs = 0;
        uint32 m_hearthSendMs = 0;
        bool m_hearthShown = false;
        float m_hearthX = 0.0f, m_hearthY = 0.0f, m_hearthZ = 0.0f;
        ObjectGuid m_hearthRing;

        void SendState() const;
        void SendLoadout() const;
        void SendWeaponInfo(uint32 index, OwnedWeapon const& owned) const;
        void NoteWeaponFired(OwnedWeapon& owned);
        void SendCooldownPie(uint32 index, OwnedWeapon const& owned) const;
        void SendOffer() const;

        void Revive(bool soulstone = false, BonkGlyph const* glyph = nullptr, bool stillAlive = false);
        uint32 PreventDeath(uint32 damage);
        void SmiteAroundRevive();
        void UpdateGear();
        uint32 GetEliteIntervalMs() const;

        void SetPaused(bool paused);
        void HoldAuraTimers(uint32 diff);
        uint32 m_pauseAuraMs = 0;
        bool m_menuOpen = false;
        bool m_victory = false;
        uint32 m_dungeonId = 0;
        uint32 m_invaderId = 0;
        uint32 m_startMapId = 0;
        void ApplyInvader();

        void Finish(bool wentHome);
        void Exit();
        void CleanUp();

        Player* m_player;
        BonkTier const m_tier;
        BonkRunBonuses m_bonuses;
        BonkMasteryBonuses m_mastery;
        std::map<uint32, uint32> m_spellRoots;
        uint32 m_masteryXp = 0;
        mutable uint32 m_crits = 0;
        uint32 m_critsReported = 0;
        uint32 m_healedUnreported = 0;
        uint32 m_leechUnreported = 0;
        uint32 m_dodgesUnreported = 0;
        uint32 m_armorUnreported = 0;
        uint32 m_projectilesUnreported = 0;
        uint32 m_castAnimationMs = 0;
        uint32 m_maxHealthReported = 0;
        uint32 m_unhurtReported = 0;
        uint32 m_overtimeReported = 0;
        std::map<std::pair<bool, uint32>, uint32> m_ranksReported;
        bool m_hurt = false;
        uint32 m_lastHurtMs = 0;
        struct BossArrival
        {
            uint32 ms = 0;
            uint32 damageTaken = 0;
        };
        std::map<ObjectGuid, BossArrival> m_bossArrivals;
        uint32 m_damageTaken = 0;
        void LogMinute();
        void LogDeath();
        void LogBossFight(Creature const* mob, BonkSpawnKind kind, BossArrival const& arrival);
        uint32 m_nextLogMinute = 1;
        uint32 m_minuteDamage = 0;
        uint32 m_minuteHealed = 0;
        uint32 m_minuteKills = 0;
        uint32 m_minuteLowestHealth = 100;
        uint32 m_minuteClientObjectsPeak = 0;
        struct PerfSecond
        {
            uint32 runSecond = 0;
            uint32 clientObjects = 0;
            uint32 mobs = 0;
            uint32 orbs = 0;
            uint32 pulledOrbs = 0;
            uint32 standIns = 0;
            uint32 busyStandIns = 0;
            uint32 telegraphs = 0;
            uint32 hits = 0;
            uint32 casts = 0;
            uint32 pendingCasts = 0;
            uint32 pets = 0;
            uint32 packets = 0;
            uint32 bytes = 0;
            std::string topPackets;
        };
        std::vector<PerfSecond> m_perfSeconds;
        bool m_packetsPrimed = false;
        uint32 m_minutePackets = 0;
        uint32 m_minutePacketBytes = 0;
        uint32 m_minutePacketSeconds = 0;
        uint32 m_secondHits = 0;
        uint32 m_secondCasts = 0;
        bool m_perfDipLogged = false;
        uint32 m_runSpec = 0;
        uint32 m_perfDipLoggedMs = 0;
        void SamplePerfSecond();
        uint32 CountClientObjects() const;
        std::string DescribeClientObjects() const;
        std::string m_lastStriker;
        uint32 m_lastStrikeDamage = 0;
        std::vector<std::pair<uint32, uint32>> m_recentHits;
        bool m_masteryAwarded = false;
        uint32 m_siphonWindow = 0;
        float m_siphonHealed = 0.0f;
        BonkGearTotals m_gear;
        uint32 m_rerolls = 0;
        uint32 m_banishes = 0;
        uint32 m_skips = 0;
        uint32 m_revives = 0;
        std::vector<Offer> m_lockedOffers;
        std::set<std::pair<bool, uint32>> m_banished;
        BonkMap m_map;
        uint32 m_stage = 1;
        uint32 m_nextMapId = 0;
        std::set<uint32> m_playedMaps;
        State m_state = STATE_ENTERING;
        bool m_paused = false;
        bool m_wentHome = false;

        uint32 m_enterTimer = 0;
        uint32 m_elapsedMs = 0;
        uint32 m_totalMs = 0;
        uint32 m_rewardMs = 0;
        size_t m_nextEvent = 0;
        std::vector<BonkMapEvent> m_heldEvents;
        struct MiniEvent
        {
            BonkMiniEventType type = BONK_MINI_EVENT_HORDE;
            uint32 objectiveId = 0;
            BonkSpot spot;
            ObjectGuid target;
            std::vector<ObjectGuid> targets;
            std::map<ObjectGuid, uint32> stolen;
            std::map<ObjectGuid, uint32> objectives;
            int32 spawnMs = 0;
            uint32 waves = 0;
            uint32 total = 0;
            std::vector<BonkSpot> path;
            size_t pathIndex = 0;
            std::vector<ObjectGuid> raiders;
            uint32 tickMs = 0;
            uint32 blows = 0;
            uint32 maxBlows = 0;
            uint8 warned = 0;
            std::string label;
            uint32 leftMs = 0;
            uint32 totalSeconds = 0;
            mutable uint32 shownSeconds = 0;
        };
        MiniEvent m_miniEvent;
        bool m_miniEventActive = false;
        struct Shrine
        {
            BonkShrineType type = BONK_SHRINE_LEY_LINE;
            BonkSpot spot;
            uint32 objectiveId = 0;
            ObjectGuid model;
            uint32 progressMs = 0;
            uint32 leftMs = 0;
            uint32 tickMs = 0;
            bool active = false;
            std::vector<ObjectGuid> pack;
        };
        std::vector<Shrine> m_shrines;
        struct PendingSummon
        {
            BonkMapCreature const* creature = nullptr;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            uint32 affixes = 0;
            ObjectGuid altar;
            bool boss = false;
            int32 leftMs = 0;
            int32 glowMs = 0;
            std::string countdown;
        };
        std::vector<PendingSummon> m_pendingSummons;
        void QueueSummon(BonkMapCreature const* creature, float x, float y, float z, uint32 affixes, ObjectGuid const& altar, bool boss,
            std::string const& countdown);
        void UpdatePendingSummons(uint32 diff);
        bool IsAltarWaking(ObjectGuid const& altar) const;
        uint32 m_shrinesToPlace = 0;
        uint32 m_shrineTimer = 0;
        bool m_moonwellPlaced = false;
        uint32 m_leyLinesPlaced = 0;
        bool m_stonePlaced = false;
        uint32 m_stoneAt = 0;
        bool m_captureShown = false;
        uint32 m_captureSendMs = 0;
        uint32 m_greedStacks = 0;
        float m_shrineBonuses[MAX_BONK_PASSIVE_EFFECT] = {};
        struct LeyBuff
        {
            BonkPassiveEffect effect = BONK_PASSIVE_POWER;
            float value = 0.0f;
            BonkRarity rarity = BONK_RARITY_COMMON;
            uint32 cardId = 0;
        };
        std::vector<LeyBuff> m_leyOffer;
        uint32 m_pendingLeyOffers = 0;
        enum VendorSlotState : uint8
        {
            VENDOR_FOR_SALE = 0,
            VENDOR_SOLD     = 1,
            VENDOR_GONE     = 2,
        };
        struct VendorSlot
        {
            Offer card;
            bool isCharge = false;
            uint8 charge = 0;
            uint32 price = 0;
            bool locked = false;
            VendorSlotState state = VENDOR_FOR_SALE;
        };
        std::vector<VendorSlot> m_vendorStock;
        std::vector<VendorSlot> m_vendorKept;
        static std::vector<VendorSlot> KeepVendorPlaces(std::vector<VendorSlot> const& previous, std::vector<VendorSlot> stock);
        std::vector<uint32> m_vendorVisits;
        ObjectGuid m_vendorGuid;
        uint32 m_vendorObjective = 0;
        uint32 m_vendorLeftMs = 0;
        uint32 m_vendorCheckMs = 0;
        uint32 m_vendorRerolls = 0;
        BonkRarity m_rarityFloor = BONK_RARITY_COMMON;
        mutable uint32 m_skyKey = 0;
        mutable time_t m_skyTime = 0;
        uint32 m_skySentKey = 0;
        mutable uint8 m_skyWeather = 0;
        mutable float m_skyWeatherGrade = 0.0f;
        bool m_vendorOpen = false;
        bool m_vaultOpen = false;
        uint32 m_nextMiniEventMs = 0;
        BonkMiniEventType m_lastMiniEvent = MAX_BONK_MINI_EVENT;
        float m_gruntRadius = 0.0f;
        int32 m_spawnTimer = 0;
        int32 m_eliteTimer = 0;
        size_t m_waveIndex = 0;
        bool m_waveEntered = false;
        size_t m_waveWarned = 0;
        uint32 m_miniEventSlots = 0;
        uint32 m_overtimeWaves = 0;
        int32 m_overtimeTimer = 0;
        float m_spawnDamageScale = 1.0f;
        float m_spawnHealthScale = 1.0f;
        uint32 m_boundaryTimer = 0;
        uint32 m_nativeTimer = 0;
        uint32 m_recoveryTimer = 0;
        uint32 m_stateTimer = 0;
        int32 m_endTimer = 0;

        bool m_bossSpawned = false;
        ObjectGuid m_bossGuid;
        std::vector<ObjectGuid> m_mobs;
        uint32 m_lastPlateId = 0;
        float m_moveX = 0.0f, m_moveY = 0.0f;
        float m_lastX = 0.0f, m_lastY = 0.0f;
        uint32 m_headingTimer = 0;
        std::map<ObjectGuid, uint32> m_farSince;
        uint32 m_stragglerTimer = 0;
        struct PackSpot
        {
            float x = 0.0f, y = 0.0f;
            uint32 untilMs = 0;
        };
        std::vector<PackSpot> m_packSpots;
        mutable std::vector<uint32> m_sectorBlockedUntil;
        int32 m_lanes[2] = { -1, -1 };
        uint32 m_laneTimer = 0;
        bool m_perched = false;
        uint32 m_perchTimer = 0;
        uint32 m_perchChecks = 0;
        uint32 m_reachChecks = 0;
        uint32 m_perchedMs = 0;
        uint32 m_flyerTimer = 0;
        bool m_hasGround = false;
        mutable bool m_reachFound = false;
        mutable float m_reachX = 0.0f, m_reachY = 0.0f, m_reachZ = 0.0f;
        uint32 m_lastStruckMs = 0;
        float m_groundX = 0.0f;
        float m_groundY = 0.0f;
        float m_groundZ = 0.0f;
        std::vector<ObjectGuid> m_chests;
        std::map<ObjectGuid, uint32> m_chestObjectives;
        void ForgetChestObjective(ObjectGuid const& chest);
        std::vector<ObjectGuid> m_powerups;
        std::map<ObjectGuid, uint32> m_powerupObjectives;
        GameObject* PlacePowerup(BonkPowerup const& powerup, float x, float y, float z, uint32 lifetimeSeconds);
        void ForgetPowerupObjective(ObjectGuid const& powerup);
        std::vector<Orb> m_orbs;
        uint32 m_orbTimer = 0;
        std::vector<OrbMerge> m_orbMerges;
        uint32 m_orbMergeTimer = 0;
        uint32 m_lastOrbMergeId = 0;
        std::vector<ShardDrop> m_shardDrops;
        struct HealDrop
        {
            ObjectGuid guid;
            ObjectGuid sparkle;
            bool pulled = false;
            float aimX = 0.0f, aimY = 0.0f, aimZ = 0.0f;
        };
        std::vector<HealDrop> m_healDrops;
        void MoveHealDrop(HealDrop const& drop, Creature* leg, float x, float y, float z);
        void StopHealDrop(HealDrop const& drop, Creature* leg);
        void RemoveHealDrop(HealDrop const& drop, Creature* leg, bool vanish);
        uint32 m_healTimer = 0;
        uint32 m_shardTimer = 0;
        ObjectGuid m_portalGuid;
        int32 m_powerupSpawnTimer = 0;
        uint32 m_powerupCheckTimer = 0;

        uint32 m_kills = 0;
        uint32 m_goldEarned = 0;
        uint32 m_shardsEarned = 0;
        uint32 m_coins = 0;
        float m_leechCarry = 0.0f;
        uint32 m_necroticStacks = 0;
        uint32 m_necroticCheckMs = 0;
        struct Pool
        {
            ObjectGuid marker;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            uint32 leftMs = 0;
        };
        std::vector<Pool> m_pools;
        uint32 m_poolTimer = 0;
        uint32 m_elitesWithoutChest = 0;
        std::map<ObjectGuid, uint32> m_chestChoices;
        std::vector<uint32> m_pendingChestChoices;
        struct PendingCast
        {
            uint32 weaponId = 0;
            uint32 spellId = 0;
            int32 leftMs = 0;
            bool echo = false;
            bool selfArea = false;
            bool strike = false;
            bool recast = false;
            bool field = false;
            bool springTrap = false;
            bool releaseLook = false;
            bool petCleave = false;
            bool pounce = false;
            bool ricochet = false;
            bool trickShot = false;
            bool detonate = false;
            bool flareSplit = false;
            bool shardBurst = false;
            bool venomBolts = false;
            bool keywordCloud = false;
            bool bomblet = false;
            bool shatter = false;
            bool wakeOfBlades = false;
            bool aftershock = false;
            bool chainExecution = false;
            bool shieldThrow = false;
            bool blinkLanding = false;
            bool pyroblastSplash = false;
            bool iceShard = false;
            bool frostShatter = false;
            bool livingBomb = false;
            bool meteor = false;
            bool meteorShower = false;
            bool phaseRush = false;
            bool presenceOfMind = false;
            bool windfury = false;
            bool staticDischarge = false;
            bool maelstrom = false;
            bool overload = false;
            bool halo = false;
            bool penanceLaunch = false;
            bool penanceHit = false;
            bool killingSpree = false;
            bool fanOfKnives = false;
            bool shadowyShuriken = false;
            bool hammerOfWrath = false;
            bool lightsHammer = false;
            bool lightsHammerLaunch = false;
            bool justiceHammer = false;
            int32 stunMs = 0;
            float incoming = 0.0f;
            float radius = 0.0f;
            uint32 kit = 0;
            uint32 hops = 0;
            float share = 1.0f;
            ObjectGuid target;
            ObjectGuid source;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            std::vector<ObjectGuid> struck;
        };
        std::vector<PendingCast> m_pendingCasts;
        void QueueRicochet(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, SpellCaster const* from, float share,
            std::vector<ObjectGuid> struck, uint32 hops);
        void Ricochet(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        Creature* PickRicochetTarget(WorldObject const* from, std::vector<ObjectGuid> const& struck) const;
        void QueueTrickShot(Creature const* dead, uint32 killingSpell);
        void FireTrickShot(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        std::vector<uint32> m_trickShotTimes;
        bool TakeRateSlot(std::vector<uint32>& times, size_t cap);
        std::vector<Creature*> GetMobsNear(float x, float y, float radius) const;
        void PlayKitAt(ObjectGuid const& guid, float x, float y, float z, uint32 kit, bool atFeet);

        void QueueDetonations(OwnedWeapon const& owned, SpellEntry const* spellInfo, float share, std::vector<Creature*> struck);
        void Detonate(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);

        void UpdateSteadyAim();
        void RetimeSteadyWeapons();
        bool IsSteadyAiming(OwnedWeapon const& owned) const;
        float GetSteadyWidth(OwnedWeapon const& owned) const;
        bool m_steady = false;
        uint32 m_steadySinceMs = 0;
        uint32 m_steadyPulseMs = 0;
        float m_steadyX = 0.0f, m_steadyY = 0.0f;

        struct ArrowStorm
        {
            uint32 weaponId = 0;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            ObjectGuid aim;
            float aimX = 0.0f, aimY = 0.0f, aimZ = 0.0f;
            int32 leftMs = 0;
            int32 tickMs = 0;
            int32 aimMs = 0;
            int32 rainMs = 0;
        };
        std::vector<ArrowStorm> m_arrowStorms;
        void StartArrowStorm(OwnedWeapon const& owned, SpellEntry const* spellInfo, float x, float y, float z);
        void UpdateArrowStorms(uint32 diff);
        void AimArrowStorm(ArrowStorm& storm, float radius) const;
        void DriftArrowStorm(ArrowStorm& storm, float speed, uint32 diff) const;
        void RainArrowStorm(OwnedWeapon& owned, SpellEntry const* spellInfo, ArrowStorm const& storm, float radius);
        uint32 GetArrowStormTickMs(OwnedWeapon const& owned) const;

        void GravityPull(OwnedWeapon const& owned, SpellEntry const* spellInfo, float x, float y, float z, float radius);
        void PullMob(Creature* mob, float x, float y, float z);
        bool CanPull(Creature const* mob);

        struct Flare
        {
            float x = 0.0f, y = 0.0f;
            float radius = 0.0f;
            uint32 untilMs = 0;
        };
        std::vector<Flare> m_flares;
        std::vector<uint32> m_flareSplitTimes;
        void LightFlare(OwnedWeapon const& owned, float x, float y, float z, float radius);
        Flare const* FindFlareAt(float x, float y);
        void QueueFlareSplit(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, float share, SpellCaster const* from);
        void FireFlareSplit(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);

        void QueueShardBurst(Creature const* dead);
        void ShardBurst(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        std::vector<uint32> m_shardBurstTimes;

        void OnPoisonedDeath(Creature const* dead);
        void QueueVenom(OwnedWeapon const& owned, Creature const* dead);
        void FireVenomBolts(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        std::vector<uint32> m_venomBoltTimes;
        struct KeywordCloud
        {
            uint32 weaponId = 0;
            float x = 0.0f, y = 0.0f;
            float radius = 0.0f;
            int32 leftMs = 0;
            int32 pulseMs = 0;
            int32 pulseEveryMs = 0;
            uint32 stacks = 0;
            ObjectGuid look;
            uint32 lookAura = 0;
            ObjectGuid area;
        };
        struct KeywordCloudKind
        {
            uint32 lifeMs = 0;
            int32 pulseEveryMs = 0;
            uint32 stacks = 0;
            size_t maxAlive = 0;
            uint32 lookSpellId = 0;
            bool bloodRing = false;
            uint32 burstKit = 0;
        };
        std::vector<KeywordCloud> m_keywordClouds;
        std::vector<uint32> m_venomCloudTimes;
        KeywordCloudKind GetCloudKind(OwnedWeapon const& owned) const;
        void StartKeywordCloud(OwnedWeapon const& owned, float x, float y, float z);
        void ShowBloodRingAt(KeywordCloud& cloud, float z);
        void EndKeywordCloud(KeywordCloud const& cloud);
        void ClearKeywordClouds();
        void UpdateKeywordClouds(uint32 diff);
        void PulseKeywordCloud(OwnedWeapon const& owned, KeywordCloud const& cloud);

        void ScatterBomblets(OwnedWeapon const& owned, SpellEntry const* spellInfo, float x, float y, float z);
        void Bomblet(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);

        struct Snake
        {
            ObjectGuid guid;
            uint32 weaponId = 0;
            uint32 untilMs = 0;
        };
        std::vector<Snake> m_snakes;
        void ReleaseSnakes(OwnedWeapon const& owned, float x, float y, float z);
        void DressSnakes();
        void UpdateSnakes();
        void DespawnSnakes();
        bool IsSnake(ObjectGuid const& guid) const;
        void QueuePetCleave(OwnedWeapon const& owned, Creature* pet, Unit* victim);
        float GetPetCleaveDamage(OwnedWeapon const& owned) const;

        void QueueShatter(OwnedWeapon const& owned, float x, float y, float z);
        void Shatter(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);

        struct IcePath
        {
            uint32 weaponId = 0;
            int32 leftMs = 0;
            int32 slowMs = 0;
        };
        IcePath m_icePath;
        void FormIcePath(OwnedWeapon const& owned);
        void UpdateIcePath(uint32 diff);
        void SlowInIcePath(Creature* mob, SpellEntry const* spellInfo);
        void SlowNormalMob(Creature* mob, SpellEntry const* spellInfo, int32 const* points, int32 durationMs);
        void ApplyMasterySlow(Creature* mob, float percent);
        std::vector<std::pair<ObjectGuid, float>> m_pendingSlows;

        void ApplyMasteryTo(BonkWeapon& weapon) const;
        float GetSpecDamage(uint32 weaponId) const;
        float GetTalentDamagePercent() const;
        float GetCapstoneDamageMultiplier(bool ownSpell, Unit const* victim) const;
        OwnedWeapon const* FindSummonOwner(Unit const* unit) const;
        float GetTalentCritChance(Unit const* victim, Unit const* attacker) const;
        bool IsWeaponMasterStrike(SpellEntry const* spell) const;
        float GetKeywordTalentDamage(uint32 keyword) const;
        uint32 GetExtraSpreadTargets(uint32 keyword) const;
        uint32 GetChillFreezeStacks(BonkKeywordInfo const& chill) const;
        uint32 GetChillTalentMs() const;
        void QueueTalentStun(OwnedWeapon const& owned, Unit* victim);
        void ApplyPendingStuns();
        uint32 GetSpellShieldCap() const;
        uint32 GetShieldBlockTalent() const;
        void RechargeJuggernautShield(uint32 diff, uint32 max);
        uint32 ApplyTalentDamageTaken(uint32 damage) const;
        bool IsIceBlocked() const;
        uint32 GuardLowHealth(uint32 damage);
        void BurstIceBlock();
        void CastOnStruck();
        void UpdateTalents();
        void OnTalentDash();
        void OnTalentBlink();
        bool IsTalentBlinkDash() const;
        float GetTrapTalentDuration(OwnedWeapon const& owned) const;
        void SpringEntrapment(OwnedWeapon const& owned, float x, float y, float radius);
        void PreferMarked(std::vector<Creature*>& mobs) const;
        std::vector<ObjectGuid> m_pendingStuns;
        std::map<uint32, uint32> m_struckCastReadyMs;
        uint32 m_talentStruckSeenMs = 0;
        uint32 m_lowHealthGuardStage = 0;
        uint32 m_iceBlockUntilMs = 0;
        bool m_iceBlockBurstPending = false;
        float m_juggernautCarry = 0.0f;

        enum BeastKind : uint8
        {
            BEAST_SPECTRAL,
            BEAST_BOAR,
            BEAST_BEAR,
            BEAST_WYVERN,
        };
        struct Beast
        {
            ObjectGuid guid;
            uint32 weaponId = 0;
            BeastKind kind = BEAST_SPECTRAL;
            uint32 untilMs = 0;
            int32 actMs = 0;
            bool acted = false;
            bool charging = false;
            int32 chargeDelayMs = 0;
            int32 chargeLeftMs = 0;
            float share = 0.0f;
            float speed = 0.0f;
            float lastX = 0.0f, lastY = 0.0f;
            float toX = 0.0f, toY = 0.0f, toZ = 0.0f;
            std::vector<ObjectGuid> struck;
        };
        struct BeastHit
        {
            ObjectGuid beast;
            ObjectGuid victim;
            uint32 weaponId = 0;
            float share = 0.0f;
        };
        std::vector<Beast> m_beasts;
        int32 m_stampedeTimer = 0;
        int32 m_plagueTimer = 0;
        uint32 m_screechTimer = 0;
        bool m_screeching = false;
        uint32 m_frenzyGlowMs = 0;
        bool FindLargestGroup(float range, float& x, float& y, float& z) const;
        void UpdateStampede(uint32 diff);
        bool LaunchStampede(OwnedWeapon const& owned);
        void CallHowlBeast(OwnedWeapon const& owned);
        Creature* SummonBeast(OwnedWeapon const& owned, BeastKind kind, float x, float y, float z, float orientation, uint32 lifeMs);
        void MakeRoomForBeast(bool spectral);
        void UpdateBeasts(uint32 diff);
        void UpdateCharge(Beast& beast, Creature* creature, uint32 diff, std::vector<BeastHit>& hits);
        void QueueCharge(Beast& beast, float x, float y, float z, float share, float speed);
        void BeginCharge(Beast& beast, Creature* creature);
        void EndCharge(Beast& beast, Creature* creature);
        void ResumeCharges();
        bool ActBeast(Beast& beast, Creature* creature, uint32 diff, std::vector<ObjectGuid>& mauls);
        void RemoveBeasts(std::vector<ObjectGuid> const& gone);
        bool Maul(Creature* bear, OwnedWeapon const& owned);
        float GetBeastHitDamage(OwnedWeapon const& owned) const;
        void DressBeasts();
        void DespawnBeasts();
        bool IsBeast(ObjectGuid const& guid) const;
        std::vector<Creature*> GetAllPets(bool frenziedOnly) const;
        bool IsScreeching() const;
        void UpdateScreech(uint32 diff, bool force);
        void PulsePlague(uint32 diff);
        void FeedFrenzy();
        void GlowFrenzy();
        Creature* SummonPet(OwnedWeapon const& owned, uint32 entry, float x, float y, float z, float orientation);
        float GetPoisonDamageScale() const;

        bool m_keywordlessHits = false;
        void ClearHunterEvolutions();
        bool UsesShotSources() const;
        Creature* SummonShotSource();
        void WarmShotSources();
        Creature* TakeShotSource(float x, float y, float z);
        void UpdateShotSources(uint32 diff);
        void DespawnShotSources();
        std::vector<ObjectGuid> m_shotSources;
        std::set<ObjectGuid> m_heldShotSources;
        size_t m_nextShotSource = 0;
        uint32 m_shotSourceTimer = 0;
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
            uint32 spellId = 0;
            float damageShare = 1.0f;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float dirX = 0.0f, dirY = 0.0f;
            float speed = 0.0f;
            float length = 0.0f;
            float radius = 0.0f;
            uint32 pierce = 0;
            uint32 flownMs = 0;
            float checked = 0.0f;
            std::vector<ObjectGuid> struck;
        };
        std::vector<Projectile> m_projectiles;
        struct EnemyMissile
        {
            ObjectGuid caster;
            ObjectGuid target;
            ObjectGuid creature;
            uint32 spellId = 0;
            uint32 delayMs = 0;
            uint32 leftMs = 0;
            bool launched = false;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            float speed = 0.0f;
        };
        std::vector<EnemyMissile> m_enemyMissiles;
        void UpdateEnemyMissiles(uint32 diff);
        void DropEnemyMissilesFrom(ObjectGuid const& caster);
        void FlyEnemyMissile(EnemyMissile& missile);
        void LandEnemyMissile(EnemyMissile const& missile);
        void HoldEnemyMissiles(bool hold);
        uint32 m_spellShield = 0;
        bool m_wardBurst = false;
        struct Mark
        {
            uint32 weaponId = 0;
            uint32 untilMs = 0;
        };
        std::map<ObjectGuid, Mark> m_marks;
        std::map<uint32, uint32> m_buffs;
        struct Trap
        {
            uint32 weaponId = 0;
            ObjectGuid standIn;
            ObjectGuid object;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            int32 armMs = 0;
            int32 leftMs = 0;
        };
        std::vector<Trap> m_traps;
        void PutAwayTrap(Trap const& trap);
        uint32 m_trapTimer = 0;
        uint32 m_petTimer = 0;
        struct PetState
        {
            std::vector<uint32> readyAtMs;
            uint32 busyUntilMs = 0;
        };
        std::map<ObjectGuid, PetState> m_petStates;
        std::vector<ObjectGuid> m_drainBeams;
        struct ImpVolley
        {
            ObjectGuid pet;
            ObjectGuid prey;
            uint32 weaponId;
            uint32 ability;
            uint32 atMs;
        };
        std::vector<ImpVolley> m_impVolleys;
        struct WarlockMinion
        {
            ObjectGuid guid;
            uint32 weaponId;
            uint32 untilMs;
            float bite;
        };
        std::vector<WarlockMinion> m_warlockMinions;
        struct PendingInfernal
        {
            uint32 weaponId;
            float x, y, z;
            uint32 atMs;
        };
        std::vector<PendingInfernal> m_pendingInfernals;
        struct PendingSeed
        {
            ObjectGuid source;
            float x, y, z;
            uint32 depth;
            uint32 weaponId;
        };
        std::vector<PendingSeed> m_pendingSeeds;
        std::map<ObjectGuid, uint32> m_seedDepth;
        std::map<ObjectGuid, uint32> m_seedHitReadyMs;
        std::map<ObjectGuid, uint32> m_impPrey;
        std::map<ObjectGuid, uint32> m_doomAt;
        std::map<ObjectGuid, uint32> m_seducedUntil;
        std::map<ObjectGuid, uint32> m_devourAffixReadyMs;
        std::deque<uint32> m_souls;
        uint32 m_darkPactUntilMs = 0;
        uint32 m_darkHarvestReadyMs = 0;
        uint32 m_voidSlamReadyMs = 0;
        uint32 m_seductionReadyMs = 0;
        std::deque<ObjectGuid> m_nightfallQueue;
        std::vector<uint32> m_nightfallMs;
        std::map<ObjectGuid, uint32> m_consumedShadows;
        std::map<ObjectGuid, uint32> m_spellLockReadyMs;
        uint32 m_devourTimer = 0;
        uint32 m_petFrenzyUntil = 0;
        bool m_petsFrenzied = false;
        int32 m_defensiveTimer = 0;
        float m_bleedHeal = 0.0f;

        static bool IsPeriodicHit();
        bool IsEnraged() const { return m_enrageUntilMs > m_totalMs; }
        float GetEnrageHaste() const;
        void Enrage();
        void EnrageOnKill(uint32 killingSpell);
        void ResolveCriticalStrikes();
        void OpenDeepWound(OwnedWeapon const& owned, Creature* mob);
        void PruneDeepWounds();
        OwnedWeapon const* FindOwnedByBehavior(BonkWeaponBehavior behavior) const;
        bool WasStruckRecently() const;
        uint32 GetShieldGain(OwnedWeapon const& owned) const;
        static float GetShieldPercent(BonkWeapon const& weapon, float damageBonus, float sizeUpgrade);
        static float GetHealPercent(BonkWeapon const& weapon, float damageBonus);
        float GetShieldSlamMultiplier(OwnedWeapon const& owned) const;
        void SpinWhirlwind(OwnedWeapon const& owned, SpellEntry const* spellInfo);
        void ShowWhirlwindSpin();
        void HealFromStrikes(OwnedWeapon const& owned, SpellEntry const* spellInfo, uint32 hits);
        void ResetWarriorState();
        void UpdateWarrior(uint32 diff);
        OwnedWeapon const* FindOwnedByFlag(BonkWeaponFlags flag) const;
        static bool SwingsWeapon(BonkWeapon const& weapon);
        void SwingWeapon(OwnedWeapon const& owned);
        uint32 GetSwingEmote() const;
        void OnAroundStrike(OwnedWeapon const& owned, Creature* mob);
        void OnAroundCast(OwnedWeapon const& owned, SpellEntry const* spellInfo, uint32 struck);
        PendingCast MakeHeroSpotCast(OwnedWeapon const& owned, SpellEntry const* spellInfo, int32 delayMs) const;
        void QueueWakeEcho(OwnedWeapon const& owned, SpellEntry const* spellInfo);
        void QueueAftershock(OwnedWeapon const& owned, SpellEntry const* spellInfo);
        void ResolveWarriorCast(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void EchoBlades(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void EruptAftershock(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void ExplodeExecuted(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        bool SwingWide(OwnedWeapon& owned, SpellEntry const* spellInfo, float areaScale);
        void PulseBloodAura(OwnedWeapon const& owned);
        float GetBloodAuraReach(OwnedWeapon const& owned) const;
        void UpdateBloodAuraRing();
        void ShowHeroLook(BonkHeroLook look, bool shown);
        void ShowHeroRing(BonkHeroRing ring, float reach);
        uint32 GetRingSpell(BonkHeroRing ring, float reach) const;
        void HideHeroRing(BonkHeroRing ring);
        void RemoveHeroRingAuras(BonkHeroRing ring);
        void RemoveHeroLooks();
        uint32 m_heroRingSpells[size_t(BonkHeroRing::Count)] = {};
        void OnBleedingDeath(Creature const* dead);
        KeywordCloudKind GetBloodPoolKind() const;
        void SplatterBlood(OwnedWeapon const& owned, Creature* struck, uint32 stacks);
        bool IsGrowingDotSpell(uint32 spellId) const;
        void ApplyBloodlust();
        bool IsBloodbathing(OwnedWeapon const& owned) const;
        void ShowBloodbath();
        void FormBloodShield(uint32 amount);
        void BreakBloodShield();
        void BurstBloodShield();
        void QueueChainExecution(Creature const* dead, uint32 killingSpell);
        bool TakeChainExecutionSlot(OwnedWeapon const& owned);
        uint32 SweepExecute(OwnedWeapon& owned, SpellEntry const* spellInfo, float nearRange);
        void OnWhirlwindSpin(OwnedWeapon const& owned);
        void StartBladestorm(OwnedWeapon const& owned);
        void UpdateBladestorm(uint32 diff);
        void PulseBladestorm(uint32 diff);
        int32 GetBladestormPulseMs(OwnedWeapon const& owned) const;
        void StartSteelCyclone(OwnedWeapon const& owned);
        void UpdateSteelCyclones(uint32 diff);
        void NoteHeroHit(uint32 damage, bool absorbed);
        void ReleaseAegis();
        void PulseSpikedShield(uint32 diff);
        void StrikeAroundHero(OwnedWeapon const& owned, uint32 kit);
        bool IsThunderReady(OwnedWeapon const& owned) const;
        uint32 GetThunderHitsNeeded(OwnedWeapon const& owned) const;
        void SpendThunder(OwnedWeapon& owned);
        std::string DescribeThunderCount(OwnedWeapon const& owned) const;
        void DemoralizingShout(OwnedWeapon const& owned, SpellEntry const* spellInfo, std::vector<Creature*> const& hit);
        float TakeVengeanceShare(OwnedWeapon const& owned);
        void UpdateVengeanceLook();
        void OnRevenge(OwnedWeapon const& owned);
        void ThrowShield(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, float share);
        void LaunchShield(OwnedWeapon const& owned, SpellEntry const* spellInfo, Unit* from, Creature* target, float share,
            std::vector<ObjectGuid> struck, uint32 hops);
        void LandShield(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        Creature* PickShieldBounce(WorldObject const* from, float x, float y, std::vector<ObjectGuid> const& struck) const;
        void GainBulwark(OwnedWeapon const& owned, uint32 struck);
        void ShowStrikeImpact(SpellEntry const* spellInfo, Creature* target);
        void ShowAreaImpact(SpellEntry const* spellInfo, float radius);
        void ShowStrike(Unit* caster, SpellEntry const* spellInfo, Unit* target) const;
        void ResolveMageCast(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        bool HurlPyroblasts(OwnedWeapon& owned, SpellEntry const* spellInfo, std::vector<Creature*> mobs);
        void SplashPyroblast(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void SortMostBurningFirst(std::vector<Creature*>& mobs) const;
        void QueueMissiles(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, float share);
        void FireMissileVolley(OwnedWeapon& owned, uint32 extraMissiles);
        void LandBlink(OwnedWeapon& owned);
        uint32 GetElementalFamily() const;
        std::vector<Creature*> PickGroupCenters(std::vector<Creature*> const& mobs, float radius, uint32 count) const;
        bool IsFrostSpellOnFrozen(SpellEntry const* spell, Unit const* victim) const;
        void ResetMageState();
        void UpdateMage(uint32 diff);
        void ResetShamanState();
        void UpdateShaman(uint32 diff);
        void OnShamanStrike(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float share);
        void OnShamanPetBite(Creature* pet, Unit* victim);
        void OnShamanKill(Unit* victim);
        void OnShamanPetPulse(OwnedWeapon& owned, Creature* pet, float radius);
        void OnShamanCloudPulse(OwnedWeapon const& owned, KeywordCloud const& cloud);
        bool GetShamanCloudKind(OwnedWeapon const& owned, KeywordCloudKind& kind) const;
        bool IsLavaBurstOnBurning(SpellEntry const* spell, Unit const* victim) const;
        float GetShamanDamageMultiplier(SpellEntry const* spell, Unit const* victim) const;
        uint32 GetShamanExtraPets(OwnedWeapon const& owned);
        bool ApplyShamanKeyword(OwnedWeapon const& owned, Creature* mob, BonkKeyword keyword, uint32 stacks);
        void BurnWithFlametongue(OwnedWeapon& owned, Creature* target, uint32 stacks);
        void SpendStaticShock(Creature* target);
        void QueueOverload(OwnedWeapon const& owned, Creature* mob);
        void StormNearDischarge(PendingCast const& cast);
        void BurstFireNovaTotems();
        void RainFromHealingTotems();
        void DressElementalSpirits();
        bool OwnsShamanSpell(uint32 weaponId) const;
        uint32 GetStaticDischargeStacks(BonkKeywordInfo const& info) const;
        void SortMostStaticFirst(std::vector<Creature*>& mobs) const;
        uint32 GetWindfuryEvery() const;
        uint32 GetMaelstromHits() const;
        void OnShamanStruck(Creature* attacker);
        void ResolveShamanCast(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        uint32 GetShamanPetFamily(OwnedWeapon const& owned) const;
        void QueueWindfury(OwnedWeapon const& owned, Creature* target, float share);
        void SwingWindfury(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void CountMaelstromHit();
        void ReleaseMaelstrom();
        void QueueStaticDischarge(Creature* mob, uint32 sourceSpellId);
        void DischargeStatic(PendingCast const& cast);
        bool DealShamanHit(SpellEntry const* spellInfo, uint32 creditSpellId, Unit* target, float damage);
        void DealTotemHit(OwnedWeapon const& owned, SpellEntry const* spellInfo, Unit* target, uint32 damage);
        void ChargeLightningShield();
        void ShowLightningShield(uint32 charges);
        void ReplantTotems();
        void FlowHealingStreams();
        void ResetPriestState();
        void UpdatePriest(uint32 diff);
        void UpdateBodyAndSoul();
        void OnPriestStrike(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float share);
        void OnPriestOverheal();
        bool GetPriestCloudKind(OwnedWeapon const& owned, KeywordCloudKind& kind) const;
        uint32 ApplyPriestDamageTaken(uint32 damage) const;
        void OnPriestShieldAbsorbed(uint32 absorbed);
        void ReflectShieldDamage(Creature* attacker);
        void SpikeDecay(OwnedWeapon const& owned, Creature* target);
        void OnPriestPeriodic(uint32 spellId, uint32 damage);
        void DrainVampiricTouch();
        void GraspWithVoidTendrils();
        float GetPriestDamageMultiplier(Unit const* victim) const;
        float GetPriestMoveSpeed() const;
        uint32 GetPriestPetFamily(OwnedWeapon const& owned) const;
        uint32 GetWarlockPetFamily(OwnedWeapon const& owned) const;
        bool IsCasterPetFamily(BonkPetFamily const& family) const;
        bool FireWarlockSpell(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void UpdateWarlock(uint32 diff);
        void OnWarlockKill(Unit* victim);
        void OnWarlockPetStrike(OwnedWeapon& owned, Creature* pet, Creature* prey, uint32 abilityIndex);
        float GetWarlockPetStrikeScale(OwnedWeapon const& owned, Creature const* prey) const;
        void QueueImpVolley(OwnedWeapon const& owned, Creature* pet, Creature* prey, uint32 abilityIndex);
        void FireImpVolleys();
        void AddWarlockStacks(OwnedWeapon const& owned, SpellEntry const* spellInfo, std::vector<Creature*> mobs, BonkKeyword keyword);
        uint32 GetWarlockAbilitySpell(OwnedWeapon const& owned, uint32 spellId) const;
        void DressWarlockPet(OwnedWeapon const& owned, Creature* pet) const;
        void SummonWarlockMinion(OwnedWeapon const& owned, uint32 entry, float x, float y, float z, uint32 lifeMs, float bite, float scale);
        void DressWarlockMinions();
        void UpdateWarlockMinions();
        void DespawnWarlockMinions();
        std::vector<ObjectGuid> GetWarlockMinionGuids() const;
        uint32 GetWarlockMinionWeapon(ObjectGuid const& guid) const;
        bool HasWarlockSpell(uint32 weaponId) const;
        float GetDarkPactScale() const;
        float GetWarlockDamageTakenScale(Unit const* victim) const;
        bool IsWarlockFullLash(OwnedWeapon const& owned) const;
        uint32 GetWarlockExtraDrops(OwnedWeapon const& owned) const;
        float GetHellfireReach(OwnedWeapon const& owned) const;
        void OnWarlockStrike(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float share);
        void OnNightfallBolt(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target);
        void OnWarlockGroundDrop(OwnedWeapon const& owned, float x, float y, float z);
        void CrashInfernals();
        void OnWarlockEvolvedPetStrike(OwnedWeapon& owned, Creature* pet, Creature* prey);
        void OnWarlockEvolvedKill(Unit* victim);
        void PassAgony(Creature* dead);
        void AddSoul();
        void ShowSouls();
        void ExpireSouls();
        void BurstSeeds();
        void HarvestDecay();
        void OnAgonyCurse(OwnedWeapon const& owned, Creature* mob);
        void FallDoom();
        void SlamVoid();
        void SeduceStrongest();
        void OnConsumedBurst(OwnedWeapon const& owned);
        void OnDrainOverheal(uint32 heal);
        void ImmolateDemons(OwnedWeapon const& owned, SpellEntry const* spellInfo);
        void UpdateWarlockEvolutions();
        void SpreadCorruption(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void CurseWithAgony(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void DrainLife(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void LatchDrainBeam(Creature* mob);
        void ReleaseDrainBeam(ObjectGuid const& guid);
        void ReleaseDrainBeams();
        void BurnHellfire(OwnedWeapon& owned, SpellEntry const* spellInfo);
        OwnedWeapon* FindOwnedByRoot(uint32 rootId);
        float GetConsumedGrowth(ObjectGuid const& pet) const;
        void FireNightfall();
        void BurstConsumedShadows();
        void DevourHarmfulEffect();
        bool ApplyWeaponKeyword(OwnedWeapon const& owned, Creature* mob, BonkKeyword keyword, uint32 stacks);
        bool FirePriestSpell(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void OnPriestCast(OwnedWeapon const& owned);
        void ShortenHolyWord(uint32 ms);
        Creature* FindPriestTarget(float range) const;
        void HealPriest(float percent, uint32 textSpellId, bool atonement);
        void PulseRenew(OwnedWeapon& owned);
        void RingHalo(OwnedWeapon& owned, SpellEntry const* spellInfo, bool fresh);
        void ResolvePriestCast(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void RaisePowerWordShield(OwnedWeapon& owned);
        void OnPriestStruck(Creature* attacker);
        void FirePenance(OwnedWeapon& owned);
        void LaunchPenanceBolt(OwnedWeapon& owned, SpellEntry const* bolt, PendingCast const& cast);
        void LandPenanceBolt(OwnedWeapon& owned, SpellEntry const* bolt, PendingCast const& cast);
        void HoldMob(Creature* mob, SpellEntry const* spellInfo, AuraType type, int32 durationMs);
        void ScreamPsychic(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void BindShackle(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void FlayMind(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void SortMostDecayFirst(std::vector<Creature*>& mobs) const;
        void BlastMind(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void OnPriestPetBite(Creature* pet, Unit* victim);
        std::vector<std::pair<ObjectGuid, uint32>> m_recentAttackers;
        std::map<ObjectGuid, float> m_penanceIncoming;
        Creature* PickPenanceTarget(OwnedWeapon const& owned, ObjectGuid const& wanted) const;
        ObjectGuid m_mindFlayTarget;
        bool m_bodyAndSoul = false;
        struct PriestHold
        {
            ObjectGuid mob;
            uint32 weaponId = 0;
            uint32 spellId = 0;
            uint32 untilMs = 0;
            uint32 nextTickMs = 0;
            float share = 1.0f;
        };
        std::vector<PriestHold> m_priestHolds;
        void TrackPriestHold(OwnedWeapon const& owned, Creature* mob, uint32 spellId, int32 durationMs, float share);
        void TickPriestHolds();
        bool LimitShackle(Creature* mob, int32 durationMs);
        uint32 m_flayCarried = 0;
        uint32 m_searingLightCast = 0;
        std::vector<uint32> m_echoOfLightTimes;
        bool m_inEchoOfLight = false;
        float m_vampiricHeal = 0.0f;
        uint32 m_reflectStored = 0;
        void ResetRogueState();
        void UpdateRogue(uint32 diff);
        bool FireRogueSpell(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void ResolveRogueCast(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        bool IsRogueNear() const;
        void SwingRogueBlades();
        void StabRogue(OwnedWeapon& owned, SpellEntry const* look, Creature* target, float share, bool flurries);
        void DealRogueDamage(uint32 creditSpellId, SpellEntry const* fallback, Unit* target, float damage);
        void OnRogueHit(Creature* target, float damage, bool flurries, uint32 creditSpellId = 0);
        void StingWithInstantPoison(Creature* target, float damage, bool showLook);
        void PoisonWithRogue(OwnedWeapon const& owned, Creature* mob, uint32 stacks);
        void PoisonFromCoatings(Creature* target);
        void OpenOnRogueTarget(Creature* target, bool forced);
        bool IsBladeFlurryActive(OwnedWeapon const& flurry) const;
        void FlurryFrom(Creature* target, float damage, uint32 creditSpellId);
        void StartBladeFlurry(OwnedWeapon& owned);
        void StabNearest(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void StabOffHand(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, std::vector<Creature*> const& taken);
        void SlashOnward(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target);
        void Mutilate(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target);
        bool OpenRogueWindow(OwnedWeapon& owned, uint32& untilMs, uint32 auraSpellId);
        void StartShadowDance(OwnedWeapon& owned);
        void StartKillingSpree(OwnedWeapon& owned);
        void QueueSpreeStrike(OwnedWeapon const& owned, int32 delayMs, bool last);
        void StrikeSpree(OwnedWeapon& owned, SpellEntry const* spellInfo, bool last);
        void FireRogueFinishers();
        void ThrowFanOfKnives(OwnedWeapon& owned, SpellEntry const* spellInfo, bool fresh);
        void ThrowDeadlyFan();
        bool GetRogueCloudKind(OwnedWeapon const& owned, KeywordCloudKind& kind) const;
        void OnRogueCloudPulse(OwnedWeapon const& owned, KeywordCloud const& cloud);
        void SortMostPoisonFirst(std::vector<Creature*>& mobs) const;
        void CastEnvenom(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void SplashVenom(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target);
        void Exsanguinate(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target);
        void TossShurikens(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void BounceShuriken(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* from, std::vector<Creature*>& struck);
        void QueueShurikenImpact(Unit const* from, Creature* target);
        void QueueShadowyShuriken(OwnedWeapon const& owned, float x, float y, float z);
        void BurstShadowyShuriken(OwnedWeapon& owned, PendingCast const& cast);
        void Evade(OwnedWeapon& owned);
        bool IsHiddenInSmoke() const;
        void OnRogueDodge(Unit* attacker);
        float GetRogueDamageMultiplier(Unit const* victim) const;
        float GetRogueCritChance(SpellEntry const* spell, float chance) const;
        void ResetPaladinState();
        bool FirePaladinSpell(OwnedWeapon& owned, SpellEntry const* spellInfo);
        bool HitAsPaladin(SpellEntry const* look, uint32 creditSpellId, Creature* target, float damage);
        float GetRighteousShare(BonkWeapon const& weapon, float damageBonus) const;
        void ResolvePaladinStrike(Creature* target, bool crit = false);
        OwnedWeapon const* FindOwnedSeal(uint32 rootId) const;
        bool StrikeAsPaladin(SpellEntry const* look, uint32 creditSpellId, Creature* target, float damage, float twice);
        void BurstSealOfCommand(Creature* target);
        void HealFromSealOfLight(bool crit);
        void HealPaladin(float percent, uint32 textSpellId);
        std::vector<Creature*> GetPaladinTargets(float range) const;
        void StrikeAsCrusader(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void PassJudgement(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void ThrowHammerOfWrath(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void LandHammerOfWrath(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void DropLightsHammers(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void LaunchLightsHammer(OwnedWeapon const& owned, PendingCast const& cast);
        void LandLightsHammer(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void Consecrate(OwnedWeapon& owned);
        bool GetPaladinCloudKind(OwnedWeapon const& owned, KeywordCloudKind& kind) const;
        void OnPaladinCloudPulse(OwnedWeapon const& owned, KeywordCloud const& cloud);
        bool IsStandingInConsecration() const;
        bool IsPaladinNear() const;
        void CastExorcism(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void SpinDivineStorm(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void TriggerArtOfWar();
        float GetPaladinCritChance(SpellEntry const* spell, float chance) const;
        void PulseRetributionAura(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void OnPaladinStruck(Creature* attacker);
        void HammerOfJustice(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void LandJusticeHammer(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void ChargeDivineShield(OwnedWeapon& owned);
        bool BlockWithDivineShield(uint32 damage);
        void ShowDivineBubble(int32 lengthMs);
        bool IsDivinelyShielded() const;
        void CallAvengingHammer(OwnedWeapon const& owned, Creature* target);
        void JudgeTarget(Creature* target);
        bool IsJudged(Unit const* victim) const;
        float GetVengeanceScale(Creature* target);
        void DropExplodingHammer(OwnedWeapon const& owned);
        void BurstSanctifiedWrath(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, float damage);
        void TryPaladinLastStands();
        uint32 ApplyPaladinDamageTaken(uint32 damage) const;
        float GetSealOfLightHeal(float damageBonus) const;
        bool GetPaladinStrength(BonkWeapon const& weapon, float damageBonus, WeaponStrength& strength) const;
        float GetPaladinDamageMultiplier(Unit const* victim) const;
        std::vector<uint32> m_sealOfLightTimes;
        std::vector<uint32> m_retributionTimes;
        uint32 m_paladinStrikes = 0;
        uint32 m_divineShieldUntilMs = 0;
        bool m_divineShieldCharged = false;
        uint32 m_artOfWarReadyMs = 0;
        uint32 m_layOnHandsReadyMs = 0;
        uint32 m_ardentDefenderReadyMs = 0;
        uint32 m_crusaderStrikes = 0;
        uint32 m_lightsHammerVolleys = 0;
        std::map<ObjectGuid, uint32> m_judgedUntilMs;
        std::map<ObjectGuid, uint32> m_vengeanceStacks;
        std::set<ObjectGuid> m_rogueTouched;
        std::map<ObjectGuid, uint32> m_findWeakness;
        std::map<ObjectGuid, uint32> m_ambushReadyMs;
        std::vector<uint32> m_riposteTimes;
        uint32 m_roguePruneMs = 0;
        uint32 m_bladeFlurryUntilMs = 0;
        uint32 m_envenomUntilMs = 0;
        uint32 m_shadowDanceUntilMs = 0;
        uint32 m_spreeBonus = 0;
        float m_smokeX = 0.0f;
        float m_smokeY = 0.0f;
        float m_smokeRadius = 0.0f;
        uint32 m_smokeUntilMs = 0;
        float m_barrierX = 0.0f;
        float m_barrierY = 0.0f;
        float m_barrierRadius = 0.0f;
        uint32 m_barrierUntilMs = 0;
        uint32 m_priestTickMs = 0;
        uint32 m_voidTendrilsMs = 0;
        uint32 m_maelstromHits = 0;
        uint32 m_lightningCharges = 0;
        uint32 m_shamanTickMs = 0;
        uint32 m_shamanTicks = 0;
        uint32 m_doomWindsReadyMs = 0;
        uint32 m_doomWindsUntilMs = 0;
        uint32 m_lavaSurgeReadyMs = 0;
        std::vector<uint32> m_spiritPackUntil;
        std::vector<uint32> m_staticDischargeTimes;
        void OnMageStrike(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float share, Unit const* from);
        void OnMageSelfArea(OwnedWeapon& owned);
        void OnMageGroundDrop(OwnedWeapon const& owned, float x, float y, float z);
        void WalkFire(OwnedWeapon const& owned, SpellEntry const* spellInfo, float x, float y, float z, float radius);
        void OnFrozen(Creature* mob, uint32 sourceSpellId);
        bool BurstIceNovas(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void BurstIceNova(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* center);
        void QueueIceShard(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, Unit const* from, float share,
            std::vector<ObjectGuid> struck, uint32 hops);
        void BounceIceShard(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        Creature* PickFrozenBounce(WorldObject const* from, std::vector<ObjectGuid> const& struck) const;
        void ShatterFrozen(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        struct FrostStorm
        {
            uint32 weaponId = 0;
            float x = 0.0f, y = 0.0f;
            float radius = 0.0f;
            uint32 untilMs = 0;
        };
        std::vector<FrostStorm> m_frostStorms;
        void AddFrostStorm(OwnedWeapon const& owned, float x, float y);
        void DeepFreeze(Creature* mob, FrostStorm const& storm);
        void Frostbite(Creature* mob, OwnedWeapon const& owned);
        void GainIceBarrier(OwnedWeapon const& owned);
        uint32 m_iceBarrierCast = 0;
        float m_iceBarrierGiven = 0.0f;
        std::vector<ObjectGuid> m_frostboltTargets;
        void NoteFrostboltTarget(Creature* target);
        struct Wildfire
        {
            ObjectGuid guid;
            uint32 weaponId = 0;
            uint32 untilMs = 0;
        };
        std::vector<Wildfire> m_wildfires;
        int32 m_wildfireMs = 0;
        void KindleWildfire(OwnedWeapon const& owned, Creature* target);
        void SpreadWildfire(uint32 diff);
        void PlantLivingBomb(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, Unit const* from);
        void ExplodeLivingBomb(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        bool DropMeteors(OwnedWeapon& owned, SpellEntry const* spellInfo, std::vector<Creature*> const& mobs);
        bool RainMeteors(OwnedWeapon& owned, SpellEntry const* spellInfo, std::vector<Creature*> const& mobs);
        void QueueMeteor(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* aim, float share, float radius, bool shower);
        void LandMeteor(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        void EruptBurning(OwnedWeapon& owned, SpellEntry const* spellInfo);
        void TriggerHotStreak(OwnedWeapon const& hotStreak, Creature* target);
        uint32 m_hotStreakCast = 0;
        void RushThrough(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast);
        struct TimeBubble
        {
            float x = 0.0f, y = 0.0f;
            float radius = 0.0f;
            uint32 untilMs = 0;
            ObjectGuid look;
            uint32 lookAura = 0;
        };
        std::vector<TimeBubble> m_timeBubbles;
        void EndTimeBubble(TimeBubble const& bubble);
        void ClearTimeBubbles();
        int32 m_timeBubbleSlowMs = 0;
        void LeaveTimeBubble(OwnedWeapon const& owned);
        void UpdateTimeBubbles(uint32 diff);
        float GetTimeBubbleMultiplier(Unit const* victim) const;
        void QueuePresenceOfMind(OwnedWeapon const& owned);
        void CastPresenceOfMind(OwnedWeapon const& source);
        uint32 m_presenceOfMindMs = 0;
        void BankMissiles(OwnedWeapon const& owned);
        uint32 m_bankedMissiles = 0;
        float GetArcanePowerMultiplier() const;
        uint32 m_arcanePowerUntilMs = 0;
        void PlantHydra(Creature* pet);
        void HoldPet(Creature* pet, bool hold, bool planted);
        static bool IsPlanted(BonkWeapon const& weapon);
        void ReplantHydras();
        float GetMageDamageMultiplier(Unit const* victim) const;
        OwnedWeapon const* FindOwnedWhere(bool (BonkWeapon::*test)() const) const;
        void ShiftElemental(Creature* pet, uint32 entry);
        void UpdateElementalShift();
        uint32 m_shiftFamily = 0;
        bool GetMageCloudKind(OwnedWeapon const& owned, KeywordCloudKind& kind) const;
        void DealExactHit(SpellEntry const* spellInfo, Unit* target, uint32 damage, Unit* attacker = nullptr);
        void ShowFallingAt(SpellEntry const* spellInfo, float x, float y, float z);
        void ShowMissileFrom(Unit* source, SpellEntry const* spellInfo, float x, float y, float z);
        void ShowThrowAtSpot(Unit* source, SpellEntry const* spellInfo, Unit const* target);
        void ShowMissilesAround(SpellEntry const* spellInfo, float radius, uint32 count);
        void PlaceAreaLook(uint32 spellId, float x, float y, float z, float radius, uint32 durationMs);
        struct Bladestorm
        {
            uint32 weaponId = 0;
            int32 leftMs = 0;
            int32 pulseMs = 0;
        };
        Bladestorm m_bladestorm;
        uint32 m_bladestormKitMs = 0;
        struct SteelCyclone
        {
            uint32 weaponId = 0;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            int32 leftMs = 0;
            int32 tickMs = 0;
        };
        std::vector<SteelCyclone> m_steelCyclones;
        void StrikeSteelCyclone(OwnedWeapon& owned, SpellEntry const* spellInfo, SteelCyclone const& storm);
        bool m_aegisPending = false;
        uint32 m_aegisReadyMs = 0;
        int32 m_spikedShieldMs = 0;
        uint32 m_thunderHits = 0;
        uint32 m_vengeanceStored = 0;
        float m_vengeanceShare = 1.0f;
        std::map<uint32, uint32> m_strikeImpactMs;
        uint32 m_swingAnimationMs = 0;
        bool m_swingKnocksBack = false;
        uint32 m_bloodlustCutMs = 0;
        bool m_bloodShieldHeld = false;
        bool m_bloodShieldBurst = false;
        std::vector<uint32> m_bloodPoolTimes;
        std::vector<uint32> m_chainExecutionTimes;
        uint32 m_chainExecutionCast = 0;
        uint32 m_chainExecutionsLeft = 0;
        uint32 m_enrageUntilMs = 0;
        bool m_struck = false;
        uint32 m_struckAtMs = 0;
        std::vector<std::pair<ObjectGuid, uint32>> m_criticalStrikes;
        std::map<ObjectGuid, uint32> m_deepWoundsReadyMs;
        std::vector<uint32> m_deepWoundTimes;
        BonkTelegraphs m_telegraphs;
        BonkSpawnPoints m_spawnPoints;
        BonkObjectives m_objectives;
        uint32 m_shield = 0;
        uint32 m_shieldIdleMs = 0;
        uint32 m_shieldSendTimer = 0;
        uint32 m_shieldSent = 0;
        uint32 m_shieldMaxSent = 0;
        uint32 m_chestsBought = 0;
        uint32 m_lastLevel = 1;
        uint32 m_pendingLevels = 0;
        uint32 m_overcapLevel = 0;
        uint32 m_overcapXp = 0;

        void UpdateKeywords(uint32 diff);
        void SpreadKeywords(Creature const* dead);
        bool Freeze(Creature* mob);
        bool IsFrozen(Unit const* victim) const;
        uint32 CountKeywords(Unit const* victim) const;
        float GetKeywordDamageMultiplier(SpellEntry const* spell, Unit const* victim) const;
        float GetHuntersMarkBonus() const;
        float GetMarkStrength(float damageBonus, float markUpgrade) const;
        float GetSlowedBonus(Unit const* victim) const;
        static float GetSlowedStrength(float damageBonus, float slowedUpgrade);
        SpellAuraHolder* AddKeywordAura(Creature* mob, BonkKeywordInfo const& info, uint32 stacks, uint32 durationMs, int32 points = 0);
        int32 GetKeywordPoints(BonkKeywordInfo const& info) const;
        struct DotStack
        {
            uint32 untilMs = 0;
            int32 points = 0;
            uint32 spellId = 0;
            uint32 appliedMs = 0;
            bool grows = false;
        };
        void AddDotStacks(Creature* mob, BonkKeywordInfo const& info, uint32 stacks, uint32 durationMs, int32 points, uint32 spellId);
        void UpdateDotStacks(uint32 diff);
        void SyncDotAura(Creature* mob, BonkKeywordInfo const& info, std::vector<DotStack> const& stacks);
        void ShiftDotStacks(uint32 ms);
        void MarkKeywordTimer(Creature const* mob, BonkKeyword keyword);
        bool GetKeywordTimer(Creature* mob, BonkKeywordInfo const& info, uint32 now, uint32& leftMs, uint32& fullMs) const;
        void SendKeywordTimers(uint32 diff);
        bool CanKnockBack(Creature const* mob);
        void RebuildWeaponSpells();
        OwnedWeapon const* FindOwnedBySpell(uint32 spellId) const;

        struct PendingKeyword
        {
            ObjectGuid mob;
            BonkKeyword keyword = BONK_KEYWORD_NONE;
            uint8 stacks = 0;
            uint32 durationMs = 0;
            float damageScale = 1.0f;
            uint32 spellId = 0;
        };
        std::vector<PendingKeyword> m_pendingKeywords;
        std::vector<PendingKeyword> m_pendingConsumes;
        bool GetWeaponKeyword(OwnedWeapon const& owned, PendingKeyword& keyword) const;
        bool GetPetAbilityKeyword(OwnedWeapon const& owned, BonkPetAbility const& ability, PendingKeyword& keyword) const;
        void ConsumePendingKeywords();
        float ConsumeDotStacks(Creature* mob, BonkKeywordInfo const& info);
        struct DyingKeyword
        {
            uint32 stacks = 0;
            int32 points = 0;
            uint32 spellId = 0;
            uint32 leftMs = 0;
        };
        std::map<ObjectGuid, std::map<uint8, DyingKeyword>> m_dyingKeywords;
        std::map<ObjectGuid, std::map<uint8, std::vector<DotStack>>> m_dotStacks;
        uint32 m_dotTimer = 0;
        std::set<std::pair<ObjectGuid, uint8>> m_keywordTimers;
        uint32 m_keywordTimerMs = 0;
        uint32 m_pausedAtMs = 0;
        struct CrowdControl
        {
            uint32 frozenImmuneUntil = 0;
            uint32 stunImmuneUntil = 0;
            uint32 knockImmuneUntil = 0;
            uint32 pullImmuneUntil = 0;
            uint32 shatterUntil = 0;
        };
        std::map<ObjectGuid, CrowdControl> m_crowdControl;
        std::map<uint32, uint32> m_weaponSpells;

        std::vector<OwnedWeapon> m_weapons;
        std::map<uint32, uint8> m_passiveRanks;
        std::vector<Offer> m_offer;
        OfferSource m_offerSource = OFFER_LEVEL;

        float m_safeX = 0.0f, m_safeY = 0.0f, m_safeZ = 0.0f;

        struct StressTicks
        {
            uint32 count = 0;
            uint64 totalMs = 0;
            uint32 worstMs = 0;
            uint32 slow = 0;
            uint64 runUs = 0;
            uint32 worstRunUs = 0;
            void Add(uint32 tickMs, uint32 usedUs);
        };
        struct Stress
        {
            bool on = false;
            float bonuses[MAX_BONK_PASSIVE_EFFECT] = {};
            uint32 startMs = 0;
            uint32 reportMs = 0;
            uint32 holdCheckMs = 0;
            StressTicks window;
            StressTicks total;
            uint32 alive = 0;
            uint32 held = 0;
            std::map<ObjectGuid, uint32> heldSince;
            std::set<ObjectGuid> heldReported;
        };
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
        void UpdateStress();
        void CheckStressHolds();
        void ReportStress(StressTicks const& ticks, bool summary) const;
        Stress m_stress;

        BonkMap const* GetSandboxMap() const;
        bool TeleportToSandbox(std::string& error);
        void StartSandbox();
        void SummonSandboxKeeper();
        void UpdateSandbox(uint32 diff);
        void SpawnQueuedSandboxPack();
        void RefillIdleDummies(uint32 elapsedMs);
        void PrepareSandboxMob(Creature* mob);
        void ReviveInSandbox();
        void EndSandbox();
        float GetSandboxFront() const;
        void SpawnSandboxDummies(uint32 count, BonkSpawnKind kind, float ringRadius = 0.0f);
        bool GetSandboxDummySpot(uint32 index, uint32 count, float ringRadius, float& x, float& y) const;
        void QueueSandboxPack(uint32 count, bool meleeOnly);
        BonkMapCreature const* PickSandboxPackCreature() const;
        Creature* SpawnSandboxEnemy(uint32 entry);
        bool RunSandboxBossVerb(std::string const& verb, uint32 first, std::string& error);
        void ListSandboxBosses(BonkSpawnKind kind) const;
        void DescribeSandboxBoss(Creature const* mob) const;
        std::vector<Creature*> GetSandboxBosses() const;
        void ToggleSandbox(uint32 toggle);
        void GiveSandboxLevels(uint32 levels);
        void SetSandboxLevel(uint32 level);
        void SpawnSandboxCrystals(Creature const* keeper);
        OwnedWeapon* FindSandboxSpell(uint32 baseId);
        bool AddSandboxSpell(uint32 baseId, std::string& error);
        void SetSandboxRank(uint32 baseId, uint32 rank, bool epicRoundRobin);
        void SetSandboxPassiveRank(uint32 passiveId, uint32 rank);
        void RemoveSandboxSpells(uint32 baseId);
        void ResetSandbox(uint32 reset);
        void EnterSandboxBench();
        void ClearSandboxEnemies();
        void HealInSandbox();
        void ResetSandboxCooldowns();
        void RefillSandboxDummies();
        void AddSandboxSpellItems();
        void AddSandboxSpellPage(uint32 baseId);
        void AddSandboxPassiveItems(uint32 page);
        void AddSandboxCardItems(uint32 page);
        struct SandboxDummy
        {
            uint32 health = 0;
            uint32 idleMs = 0;
        };
        bool m_sandbox = false;
        bool m_sandboxGodBefore = false;
        bool m_sandboxPassive = false;
        bool m_sandboxUnkillable = false;
        bool m_sandboxDeepDummies = false;
        bool m_sandboxMovesOff = false;
        bool m_sandboxReachReport = false;
        bool m_sandboxQuietLevels = false;
        bool m_sandboxBench = false;
        float m_sandboxMinute = -1.0f;
        ObjectGuid m_sandboxKeeper;
        std::map<ObjectGuid, SandboxDummy> m_sandboxDummies;
        uint32 m_sandboxPackLeft = 0;
        bool m_sandboxPackMeleeOnly = false;
        uint32 m_sandboxTimer = 0;
        uint32 m_sandboxMenu = 0;
};

#endif
