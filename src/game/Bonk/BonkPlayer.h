#ifndef MANGOS_BONKPLAYER_H
#define MANGOS_BONKPLAYER_H

#include "BonkTelegraph.h"
#include "Common.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class BonkRun;
class Creature;
class Player;
enum BonkShop : uint8;
enum BonkQuestReward : uint8;
enum BonkUpgradeUtility : uint8;
struct BonkMap;
struct BonkMasteryBonuses;
struct BonkRunBonuses;
struct BonkRunChoice;
struct BonkTier;
struct BonkWeapon;

class BonkPlayer
{
    public:
        explicit BonkPlayer(Player* player);
        ~BonkPlayer();

        void OnLogin();
        void OnLogout();
        void OnSave();
        void Update(uint32 diff);

        BonkRun* GetRun() const { return m_run.get(); }
        time_t GetSkyTime() const;
        void PreviewTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs);
        bool StartRun(BonkTier const& tier, std::string& error);
        bool StartRun(BonkTier const& tier, std::string& error, BonkRunChoice const& choice);
        void SendMapInfo() const;
        bool StartSandbox(std::string& error);
        bool IsInSandbox() const;
        void StopRun();

        static std::vector<BonkTier const*> GetTiersInOrder();
        uint32 GetBestStage(uint32 tierId) const;
        bool IsTierUnlocked(BonkTier const& tier) const;
        bool IsTierCleared(BonkTier const& tier) const;
        bool CanBuyGear(uint32 itemEntry) const;
        bool ShowsGearItem(Creature const* vendor, uint32 itemEntry) const;
        void SendGearTabs(Creature const* vendor) const;
        void SelectGearTab(uint32 group);
        std::string GetUnlockHint(BonkTier const& tier) const;
        void RecordStage(uint32 tierId, uint32 stage);

        static bool IsWipeName(std::string const& name);
        static bool IsPlayableClass(uint8 classId);
        static void WipeAccount(uint32 accountId);

        void RecordRun(uint32 stage, uint32 seconds, uint32 kills, uint32 goldEarned);

        struct StarterOption
        {
            BonkWeapon const* weapon = nullptr;
            bool unlocked = false;
            std::string hint;
        };
        std::vector<StarterOption> GetStarterOptions() const;
        BonkWeapon const* GetStarterWeapon() const;
        bool ChooseStarter(uint32 weaponId, std::string& error);
        void SendStarters() const;

        uint32 GetShards() const { return m_shards; }
        void AddShards(uint32 count);
        uint8 GetUpgradeRank(uint32 upgradeId) const;
        float GetUpgradeUtility(BonkUpgradeUtility utility) const;
        uint8 GetTalentRank(uint32 talentId) const;
        bool IsTalentEnabled(uint32 talentId) const { return m_enabledTalents.count(talentId) != 0; }
        uint32 CountDarkFortunes() const { return uint32(m_enabledTalents.size()); }
        bool BuyUpgrade(uint32 upgradeId, std::string& message);
        bool BuyUnlock(BonkQuestReward type, uint32 id, std::string& message);
        void VisitSpellbook(Creature const* keeper);
        bool BuyTalent(uint32 talentId, std::string& message);
        bool ResetUpgrades(std::string& message);
        bool ResetTalents(std::string& message);
        bool RefundUpgrade(uint32 upgradeId, std::string& message);
        bool RefundTalent(uint32 talentId, std::string& message);
        bool ToggleTalent(uint32 talentId, std::string& message);
        BonkRunBonuses GetRunBonuses() const;

        uint32 GetMasteryXp() const { return m_masteryXp; }
        uint32 GetMasteryLevel() const;
        uint32 GetMasteryPoints() const;
        uint32 GetMasterySpent() const;
        uint32 GetMasterySpentInTree(uint32 treeId) const;
        uint8 GetMasteryRank(uint32 nodeId) const;
        void AddMasteryXp(uint32 xp);
        bool LearnMastery(uint32 nodeId, std::string& message);
        bool ResetMastery(std::string& message);
        bool RefundMastery(uint32 nodeId, std::string& message);
        BonkMasteryBonuses GetMasteryBonuses() const;
        void SendMasteryState() const;
        void SendWallet() const;

        void OpenShop(BonkShop shop, Creature const* npc);
        bool IsVisitingShop(BonkShop shop) const { return m_shop == shop && IsAtShop(); }
        void ShopLearn(uint32 id);
        void ShopUnlearn(uint32 id);
        void ShopToggle(uint32 id);
        void ShopRespec();

        void SetFiringWeapon(bool firing) { m_firingWeapon = firing; }
        bool IsFiringWeapon() const { return m_firingWeapon; }
        void SetEquippingGear(bool equipping) { m_equippingGear = equipping; }
        bool IsEquippingGear() const { return m_equippingGear; }
        bool IsSlotUnusedByClass(uint8 slot) const;
        struct SpellTweak
        {
            float areaScale = 1.0f;
            uint32 chainBonus = 0;
            float jumpRadius = CHAIN_SPELL_JUMP_RADIUS;
            uint32 durationMs = 0;
            float durationScale = 1.0f;
            int32 weaponDamage = 0;
            uint32 tickMs = 0;
            float tickScale = 1.0f;
            bool normalEnemiesOnly = false;
            bool oneTargetPerCast = false;
        };
        SpellTweak& TweakSpell(uint32 spellId) { return m_spellTweaks[spellId]; }
        void ClearSpellTweaks() { m_spellTweaks.clear(); }
        float GetAreaScale(uint32 spellId) const
        {
            SpellTweak const* tweak = FindSpellTweak(spellId);
            return tweak ? tweak->areaScale : 1.0f;
        }
        uint32 GetChainBonus(uint32 spellId) const
        {
            SpellTweak const* tweak = FindSpellTweak(spellId);
            return tweak ? tweak->chainBonus : 0;
        }
        float GetChainJumpRadius(uint32 spellId) const
        {
            SpellTweak const* tweak = FindSpellTweak(spellId);
            return tweak ? tweak->jumpRadius : CHAIN_SPELL_JUMP_RADIUS;
        }
        bool StrikesOneTargetPerCast(uint32 spellId) const
        {
            SpellTweak const* tweak = FindSpellTweak(spellId);
            return tweak && tweak->oneTargetPerCast;
        }
        bool HoldsNormalEnemiesOnly(uint32 spellId) const
        {
            SpellTweak const* tweak = FindSpellTweak(spellId);
            return tweak && tweak->normalEnemiesOnly;
        }
        int32 AdjustDuration(uint32 spellId, int32 duration) const
        {
            SpellTweak const* tweak = FindSpellTweak(spellId);
            if (!tweak || duration <= 0)
                return duration;
            return int32((tweak->durationMs ? tweak->durationMs : duration) * tweak->durationScale);
        }
        uint32 GetTickInterval(uint32 spellId, uint32 amplitude) const
        {
            SpellTweak const* tweak = FindSpellTweak(spellId);
            if (!tweak)
                return 0;
            uint32 const tick = tweak->tickMs ? tweak->tickMs : amplitude;
            return std::max(100u, uint32(tick * tweak->tickScale));
        }
        int32 GetWeaponDamage(uint32 spellId) const
        {
            SpellTweak const* tweak = FindSpellTweak(spellId);
            return tweak ? tweak->weaponDamage : 0;
        }

        void SetTestGod(bool on) { m_testGod = on; }
        bool IsTestGod() const { return m_testGod; }

        void SetAddonReady() { m_addonReady = true; m_zoneArtSent = false; }
        bool IsAddonReady() const { return m_addonReady; }

        void SendAddon(std::string const& line) const;
        void Notify(std::string const& text) const;
        void NotifyFind(std::string const& text) const;
        void SendGlyphInfo() const;
        uint32 GetPoolToggleSlots() const;
        void TrimPoolToggles() const;

        bool Dash();
        bool IsDashInvulnerable() const { return m_dashClockMs < m_dashInvulnerableUntilMs; }
        float GetDashBurstSpeed() const { return m_dashBursting ? GetDashSpeed() : 0.0f; }
        void SendDash() const;
        uint32 GetDashCooldownMs() const;
        void SendTierList() const;
        void SendGearInfo() const;
        void SendStats() const;
        void SendStatTotals(float const* totals, bool inRun) const;
        void OpenArena(Creature const* npc) const;

        void ResetCharacter();
        bool IsAtCamp() const;
        static bool IsHiddenFrom(Creature const* creature, Player const* viewer);
        void TeleportToCamp();
        void AnnounceMailAtCamp() { m_campMailNoticeMs = BONK_CAMP_MAIL_NOTICE_DELAY_MS; }
        bool MoveToCampBeforeLogin(bool gameMaster);

        struct PacketCounts
        {
            uint32 packets = 0;
            uint32 bytes = 0;
            std::vector<std::pair<uint16, uint32>> top;
        };
        void NotePacket(uint16 opcode, size_t bytes);
        PacketCounts TakePacketCounts();

    private:
        void ClearAuras();

        struct Record
        {
            uint32 runs = 0;
            uint32 bestStage = 0;
            uint32 bestSeconds = 0;
            uint32 kills = 0;
            uint32 goldEarned = 0;
        };

        void WriteHeroScreenStats() const;
        static void WriteHeroScreenFile(std::string const& lua);

        void LoadPurchases();
        void LoadMastery();
        void LoadStarter();
        void GiveStarterBags();
        void TeachWeaponSkills();
        std::string GetClassName() const;
        bool Spend(uint32 copper, std::string& message);
        bool SpendShards(uint32 count, std::string& message);
        void SaveGold() const;
        void SaveShards() const;
        void SaveTalent(uint32 talentId) const;

        void SendShop(bool open) const;
        void SendArena(std::string const& npc) const;
        bool IsAtShop() const;
        bool IsAtSpellbook() const;

        Player* m_player;
        std::unique_ptr<BonkRun> m_run;
        BonkTelegraphs m_previewTelegraphs;
        bool m_addonReady = false;
        mutable bool m_zoneArtSent = false;
        void SendZoneArt() const;
        bool m_firingWeapon = false;
        bool m_equippingGear = false;
        std::map<uint32, SpellTweak> m_spellTweaks;
        SpellTweak const* FindSpellTweak(uint32 spellId) const
        {
            auto itr = m_spellTweaks.find(spellId);
            return itr != m_spellTweaks.end() ? &itr->second : nullptr;
        }
        bool m_testGod = false;
        bool m_campCheckPending = false;
        uint32 m_sitAtCampMs = 0;
        void UpdateCampSeat(uint32 diff);
        uint32 m_deadOutsideRunMs = 0;
        void UpdateReviveOutsideRun(uint32 diff);
        void SitAtCamp();
        uint32 m_mailboxRefreshMs = 0;
        void UpdateMailboxRefresh(uint32 diff);
        void RefreshMailboxBeacon();
        static constexpr uint32 BONK_CAMP_MAIL_NOTICE_DELAY_MS = 1500;
        uint32 m_campMailNoticeMs = 0;
        void UpdateCampMailNotice(uint32 diff);
        uint32 m_gearTab = 0;
        std::vector<uint32> GetGearTabs(Creature const* vendor) const;
        uint32 GetShownGearTab(std::vector<uint32> const& tabs) const;
        uint32 m_mailboxGlowMs = 0;
        bool m_mailboxGlowShown = false;
        static uint16 const PACKET_KINDS = 1024;
        std::array<std::atomic<uint32>, PACKET_KINDS> m_packetCounts{};
        std::atomic<uint32> m_packetBytes{ 0 };
        bool HasUnreadMail() const;
        void UpdateMailboxGlow(uint32 diff);
        bool m_goldLoaded = false;
        std::map<uint32, uint32> m_bestStages;
        Record m_record;
        std::map<uint32, uint8> m_upgradeRanks;
        std::map<uint32, uint8> m_talentRanks;
        std::set<uint32> m_enabledTalents;
        uint32 m_shards = 0;
        uint32 m_masteryXp = 0;
        std::map<uint32, uint8> m_masteryRanks;
        uint32 m_starterChoice = 0;
        BonkShop m_shop{};
        ObjectGuid m_shopNpc;
        ObjectGuid m_spellbookKeeper;
        std::string m_shopNpcName;

        float GetDashDistance() const;
        uint32 GetDashMaxCharges() const;
        float GetDashSpeed() const;
        bool IsDashPaused() const;
        void UpdateDash(uint32 diff);
        void SetDashBurst(bool on);
        void ThrowHero(float angle, float speed);
        void BlinkHero();
        float GetMoveAngle() const;
        uint32 m_dashClockMs = 0;
        uint32 m_dashSpent = 0;
        uint32 m_dashRechargeMs = 0;
        uint32 m_dashInvulnerableUntilMs = 0;
        uint32 m_dashBurstUntilMs = 0;
        bool m_dashBursting = false;
};

struct FiringWeapon
{
    explicit FiringWeapon(BonkPlayer* bonk) : m_bonk(bonk), m_wasFiring(bonk->IsFiringWeapon()) { m_bonk->SetFiringWeapon(true); }
    ~FiringWeapon() { m_bonk->SetFiringWeapon(m_wasFiring); }
    BonkPlayer* m_bonk;
    bool m_wasFiring;
};

#endif
