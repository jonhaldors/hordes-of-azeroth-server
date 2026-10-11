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

#include <map>
#include <memory>
#include <set>
#include <string>

class BonkRun;
class Creature;
class Player;
enum BonkShop : uint8;
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
        uint32 GetBestStage(uint32 tierId) const;
        bool IsTierUnlocked(BonkTier const& tier) const;
        std::string GetUnlockHint(BonkTier const& tier) const;
        void RecordStage(uint32 tierId, uint32 stage);

        // This character's record over all its runs, shown on the hero screen.
        void RecordRun(uint32 stage, uint32 seconds, uint32 kills, uint32 goldEarned);

        // Camp purchases belong to this character; gold is the account's.
        // Talents cost Talent Shards, which belong to the character too.
        uint32 GetShards() const { return m_shards; }
        void AddShards(uint32 count);
        uint8 GetUpgradeRank(uint32 upgradeId) const;
        uint8 GetTalentRank(uint32 talentId) const;
        bool IsTalentEnabled(uint32 talentId) const { return m_enabledTalents.count(talentId) != 0; }
        bool BuyUpgrade(uint32 upgradeId, std::string& message);
        bool BuyTalent(uint32 talentId, std::string& message);
        bool ToggleTalent(uint32 talentId, std::string& message);
        BonkRunBonuses GetRunBonuses() const;

        // Shop window (addon only): `npc` sells `shop`; the addon shows its
        // trees and buys with `.bonk learn` and `.bonk toggle` while the
        // character stays near the NPC.
        void OpenShop(BonkShop shop, Creature const* npc);
        void ShopLearn(uint32 id);
        void ShopToggle(uint32 id);

        // Set while a run fires an auto-weapon, so the cast takes no ammo.
        void SetFiringWeapon(bool firing) { m_firingWeapon = firing; }
        bool IsFiringWeapon() const { return m_firingWeapon; }

        void SetAddonReady() { m_addonReady = true; }
        bool IsAddonReady() const { return m_addonReady; }

        // Protocol line for the addon; dropped when the addon is not loaded.
        void SendAddon(std::string const& line) const;
        // Short message: a center-screen notice with the addon, chat without it.
        void Notify(std::string const& text) const;
        void SendTierList() const;

        // Level 1, alive, full health: the state a character has outside a run.
        void ResetCharacter();
        bool IsAtCamp() const;
        void TeleportToCamp();

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
        bool Spend(uint32 copper, std::string& message);
        bool SpendShards(uint32 count, std::string& message);
        void SaveGold() const;
        void SaveShards() const;
        void SaveTalent(uint32 talentId) const;

        // `open`: the NPC was just spoken to, rather than a purchase made.
        void SendShop(bool open) const;
        bool IsAtShop() const;

        Player* m_player;
        std::unique_ptr<BonkRun> m_run;
        bool m_addonReady = false;
        bool m_firingWeapon = false;
        bool m_campCheckPending = false;
        bool m_goldLoaded = false;
        std::map<uint32, uint32> m_bestStages;  // tier id -> furthest stage reached
        Record m_record;
        std::map<uint32, uint8> m_upgradeRanks;
        std::map<uint32, uint8> m_talentRanks;
        std::set<uint32> m_enabledTalents;      // difficulty toggles switched on
        uint32 m_shards = 0;
        BonkShop m_shop{};                      // last shop spoken to; none at first
        ObjectGuid m_shopNpc;
        std::string m_shopNpcName;
};

#endif
