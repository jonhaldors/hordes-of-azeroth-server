/*
 * Vanilla Bonk: per-player game-mode state. Every Player owns one.
 *
 * Talks to the VanillaBonk client addon with system messages that start
 * with "BONK~" (the addon hides them from chat). The addon answers with
 * `.bonk` chat commands.
 */

#ifndef MANGOS_BONKPLAYER_H
#define MANGOS_BONKPLAYER_H

#include "BonkTelegraph.h"
#include "Common.h"
#include "ObjectGuid.h"

#include <algorithm>
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
struct BonkTier;

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
        // GM testing aid outside a run: a ground warning that fills and goes
        // just as an ability's does, with nothing after it.
        void PreviewTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs);
        // `dungeon`: a dungeon mode, open once the difficulty is cleared.
        bool StartRun(BonkTier const& tier, std::string& error, BonkMap const* dungeon = nullptr);
        void StopRun();

        // Tier progress is shared by every character on the account.
        // Tiers in play order: the open ones first, then each one after the
        // tier that unlocks it.
        static std::vector<BonkTier const*> GetTiersInOrder();
        uint32 GetBestStage(uint32 tierId) const;
        bool IsTierUnlocked(BonkTier const& tier) const;
        // Its last stage's boss beaten at least once.
        bool IsTierCleared(BonkTier const& tier) const;
        // The gear vendor sells Heroic and Mythic gear once Normal and Heroic
        // are cleared (Phase F6).
        bool CanBuyGear(uint32 itemEntry) const;
        std::string GetUnlockHint(BonkTier const& tier) const;
        void RecordStage(uint32 tierId, uint32 stage);

        // The hero screen's "Wipe account" (BonkGlue): it deletes every hero,
        // then asks for a hero with this reserved name, which is never made.
        static bool IsWipeName(std::string const& name);
        // Every hero already deleted: the account's progress goes too (gold,
        // Darkmoon Tickets, upgrades, Sayge's Fortunes, quests, Mastery, tier
        // records), and the hero screen's stats file starts over.
        static void WipeAccount(uint32 accountId);

        // This character's record over all its runs, shown on the hero screen.
        void RecordRun(uint32 stage, uint32 seconds, uint32 kills, uint32 goldEarned);

        // Camp purchases, gold and Talent Shards belong to the account: every
        // hero shares them. Talents cost Talent Shards.
        uint32 GetShards() const { return m_shards; }
        void AddShards(uint32 count);
        uint8 GetUpgradeRank(uint32 upgradeId) const;
        // What the camp upgrades of one utility add up to (dash range and
        // the like), at camp and in runs.
        float GetUpgradeUtility(BonkUpgradeUtility utility) const;
        uint8 GetTalentRank(uint32 talentId) const;
        bool IsTalentEnabled(uint32 talentId) const { return m_enabledTalents.count(talentId) != 0; }
        // Dark Fortunes (difficulty toggles) switched on.
        uint32 CountDarkFortunes() const { return uint32(m_enabledTalents.size()); }
        bool BuyUpgrade(uint32 upgradeId, std::string& message);
        // A spell, passive or card whose quest is done, bought with gold at
        // the Spellbook (Core loop v2 phase E).
        bool BuyUnlock(BonkQuestReward type, uint32 id, std::string& message);
        bool BuyTalent(uint32 talentId, std::string& message);
        // Free full refunds: every upgrade's gold, every talent's shards.
        bool ResetUpgrades(std::string& message);
        bool ResetTalents(std::string& message);
        // One rank back at a time, for what it costs now; refused while
        // another learned purchase still needs that rank.
        bool RefundUpgrade(uint32 upgradeId, std::string& message);
        bool RefundTalent(uint32 talentId, std::string& message);
        bool ToggleTalent(uint32 talentId, std::string& message);
        BonkRunBonuses GetRunBonuses() const;

        // Class Mastery (BonkMastery) of this hero's class, shared by every
        // hero of that class on the account. Points come from Mastery levels
        // and class quests; a respec is free.
        uint32 GetMasteryXp() const { return m_masteryXp; }
        uint32 GetMasteryLevel() const;
        uint32 GetMasteryPoints() const;        // earned
        uint32 GetMasterySpent() const;
        uint32 GetMasterySpentInTree(uint32 treeId) const;
        uint8 GetMasteryRank(uint32 nodeId) const;
        void AddMasteryXp(uint32 xp);
        bool LearnMastery(uint32 nodeId, std::string& message);
        bool ResetMastery(std::string& message);
        // One point back; refused while a learned node needs the rank, or
        // a deeper row needs the points in its tree.
        bool RefundMastery(uint32 nodeId, std::string& message);
        BonkMasteryBonuses GetMasteryBonuses() const;
        // mastery~class~level~xpIntoLevel~xpForNext~maxLevel~unspent~hasTrees,
        // for the addon's Mastery bar.
        void SendMasteryState() const;
        // wallet~shards: the account's Talent Shards, for the camp's wallet.
        void SendWallet() const;

        // Shop window (addon only): `npc` sells `shop`; the addon shows its
        // trees and buys with `.bonk learn` and `.bonk toggle`, refunds one
        // rank with `.bonk unlearn` and the whole shop with `.bonk respec`,
        // while the character stays near the NPC.
        void OpenShop(BonkShop shop, Creature const* npc);
        void ShopLearn(uint32 id);
        void ShopUnlearn(uint32 id);
        void ShopToggle(uint32 id);
        void ShopRespec();

        // Set while a run fires an auto-weapon, so the cast takes no ammo.
        void SetFiringWeapon(bool firing) { m_firingWeapon = firing; }
        bool IsFiringWeapon() const { return m_firingWeapon; }
        // Set while a run puts gear on or back: a run hero is always in
        // combat, where 1.12 allows no armor changes (Player::CanEquipItem).
        void SetEquippingGear(bool equipping) { m_equippingGear = equipping; }
        bool IsEquippingGear() const { return m_equippingGear; }
        // How a run's weapons change their spells, by spell id. Spell reads
        // these whenever the player casts one; players cast no spells of
        // their own.
        struct SpellTweak
        {
            float areaScale = 1.0f;     // radius multiplier
            uint32 chainBonus = 0;      // extra targets for spells that jump
            uint32 durationMs = 0;      // replaces the spell's duration, 0 = keep it
            float durationScale = 1.0f; // then multiplies it
            int32 weaponDamage = 0;     // replaces a weapon-damage spell's swing, 0 = keep it
            uint32 tickMs = 0;          // damage over time ticks this often, 0 = the spell's own rate
            float tickScale = 1.0f;     // then multiplies the tick interval (Haste)
            bool normalEnemiesOnly = false;     // its ground area holds normal enemies only (DynamicObject)
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
        // How often a periodic aura of the spell ticks instead of `amplitude`,
        // 0 = unchanged.
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

        // `.bonk god`, GM testing: kept through run starts, which otherwise
        // turn god mode off.
        void SetTestGod(bool on) { m_testGod = on; }
        bool IsTestGod() const { return m_testGod; }

        void SetAddonReady() { m_addonReady = true; }
        bool IsAddonReady() const { return m_addonReady; }

        // Protocol line for the addon; dropped when the addon is not loaded.
        void SendAddon(std::string const& line) const;
        // Short message: a center-screen notice with the addon, chat without it.
        void Notify(std::string const& text) const;

        // The Shift dash (BonkPlayerDash.cpp, `.bonk dash`), at camp and in
        // runs: false if the hero can't dash now (a paused run, stunned,
        // swimming, no charge).
        bool Dash();
        // Enemy hits do nothing for a moment after a dash.
        bool IsDashInvulnerable() const { return m_dashClockMs < m_dashInvulnerableUntilMs; }
        // The ground dash's speed while its burst lasts, else 0 (Unit::UpdateSpeed).
        float GetDashBurstSpeed() const { return m_dashBursting ? GetDashSpeed() : 0.0f; }
        // The addon's "dash~" line: charges and cooldown.
        void SendDash() const;
        // The difficulties, with what this character brings into a run.
        void SendTierList() const;
        // Item stats and sets, for the addon's tooltips and character sheet.
        void SendGearInfo() const;
        // stats~inRun~total,...: every stat without worn gear (the addon adds
        // gear itself), from the run when in one and from camp otherwise.
        void SendStats() const;
        void SendStatTotals(float const* totals, bool inRun) const;
        // Arena window (addon only), shown in place of the run master's gossip.
        void OpenArena(Creature const* npc) const;

        // Level 1, alive, full health: the state a character has outside a run.
        void ResetCharacter();
        bool IsAtCamp() const;
        void TeleportToCamp();
        // While the character loads, before it enters the world: moves it to
        // camp unless it is there already or a game master. True if it moved.
        bool MoveToCampBeforeLogin(bool gameMaster);

    private:
        struct Record
        {
            uint32 runs = 0;
            uint32 bestStage = 0;
            uint32 bestSeconds = 0;
            uint32 kills = 0;
            uint32 goldEarned = 0;
        };

        // Writes every hero's record for the client's hero screen (BonkStats.lua).
        void WriteHeroScreenStats() const;
        static void WriteHeroScreenFile(std::string const& lua);

        void LoadPurchases();
        void LoadMastery();
        // Fills empty bag slots with Darkmoon Satchels and opens every bank
        // bag slot, for characters made before the starter kit carried them.
        void GiveStarterBags();
        // The weapon skills for the class's weapons (Feature Plan, Weapons),
        // which vanilla only partly teaches at level 1.
        void TeachWeaponSkills();
        std::string GetClassName() const;
        bool Spend(uint32 copper, std::string& message);
        bool SpendShards(uint32 count, std::string& message);
        void SaveGold() const;
        void SaveShards() const;
        void SaveTalent(uint32 talentId) const;

        // `open`: the NPC was just spoken to, rather than a purchase made.
        void SendShop(bool open) const;
        // `npc`: the run master just spoken to, empty for a refresh.
        void SendArena(std::string const& npc) const;
        bool IsAtShop() const;

        Player* m_player;
        std::unique_ptr<BonkRun> m_run;
        BonkTelegraphs m_previewTelegraphs;     // PreviewTelegraph
        bool m_addonReady = false;
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
        bool m_goldLoaded = false;
        std::map<uint32, uint32> m_bestStages;  // tier id -> furthest stage reached
        Record m_record;
        std::map<uint32, uint8> m_upgradeRanks;
        std::map<uint32, uint8> m_talentRanks;
        std::set<uint32> m_enabledTalents;      // difficulty toggles switched on
        uint32 m_shards = 0;
        uint32 m_masteryXp = 0;
        std::map<uint32, uint8> m_masteryRanks;  // this class's Mastery nodes
        BonkShop m_shop{};                      // last shop spoken to; none at first
        ObjectGuid m_shopNpc;
        std::string m_shopNpcName;

        // The Shift dash (BonkPlayerDash.cpp).
        float GetDashDistance() const;
        uint32 GetDashCooldownMs() const;
        uint32 GetDashMaxCharges() const;
        float GetDashSpeed() const;             // yards a second
        bool IsDashPaused() const;
        void UpdateDash(uint32 diff);
        void SetDashBurst(bool on);
        // Unit::KnockBack with no lift, less its interrupt, which would cut a channel.
        void ThrowHero(float angle, float speed);
        uint32 m_dashClockMs = 0;               // stops while a run is paused
        uint32 m_dashSpent = 0;                 // charges used and not back yet
        uint32 m_dashRechargeMs = 0;            // until the next one is back
        uint32 m_dashInvulnerableUntilMs = 0;
        uint32 m_dashBurstUntilMs = 0;
        bool m_dashBursting = false;            // the ground dash's speed is on
};

#endif
