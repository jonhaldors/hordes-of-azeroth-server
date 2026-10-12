/*
 * Vanilla Bonk: per-player game-mode state. Every Player owns one.
 *
 * Talks to the VanillaBonk client addon with system messages that start
 * with "BONK~" (the addon hides them from chat). The addon answers with
 * `.bonk` chat commands.
 */

#ifndef MANGOS_BONKPLAYER_H
#define MANGOS_BONKPLAYER_H

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
        bool StartRun(BonkTier const& tier, std::string& error);
        void StopRun();

        // Tier progress is shared by every character on the account.
        // Tiers in play order: the open ones first, then each one after the
        // tier that unlocks it.
        static std::vector<BonkTier const*> GetTiersInOrder();
        uint32 GetBestStage(uint32 tierId) const;
        bool IsTierUnlocked(BonkTier const& tier) const;
        std::string GetUnlockHint(BonkTier const& tier) const;
        void RecordStage(uint32 tierId, uint32 stage);

        // This character's record over all its runs, shown on the hero screen.
        void RecordRun(uint32 stage, uint32 seconds, uint32 kills, uint32 goldEarned);

        // Camp purchases, gold and Talent Shards belong to the account: every
        // hero shares them. Talents cost Talent Shards.
        uint32 GetShards() const { return m_shards; }
        void AddShards(uint32 count);
        uint8 GetUpgradeRank(uint32 upgradeId) const;
        uint8 GetTalentRank(uint32 talentId) const;
        bool IsTalentEnabled(uint32 talentId) const { return m_enabledTalents.count(talentId) != 0; }
        bool BuyUpgrade(uint32 upgradeId, std::string& message);
        bool BuyTalent(uint32 talentId, std::string& message);
        // Free full refunds: every upgrade's gold, every talent's shards.
        bool ResetUpgrades(std::string& message);
        bool ResetTalents(std::string& message);
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
        BonkMasteryBonuses GetMasteryBonuses() const;
        // mastery~class~level~xpIntoLevel~xpForNext~maxLevel~unspent~hasTrees,
        // for the addon's Mastery bar.
        void SendMasteryState() const;

        // Shop window (addon only): `npc` sells `shop`; the addon shows its
        // trees and buys with `.bonk learn` and `.bonk toggle`, and refunds
        // the whole shop with `.bonk respec`, while the character stays near
        // the NPC.
        void OpenShop(BonkShop shop, Creature const* npc);
        void ShopLearn(uint32 id);
        void ShopToggle(uint32 id);
        void ShopRespec();

        // Set while a run fires an auto-weapon, so the cast takes no ammo.
        void SetFiringWeapon(bool firing) { m_firingWeapon = firing; }
        bool IsFiringWeapon() const { return m_firingWeapon; }
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

        void LoadPurchases();
        void LoadMastery();
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
        bool m_addonReady = false;
        bool m_firingWeapon = false;
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
};

#endif
