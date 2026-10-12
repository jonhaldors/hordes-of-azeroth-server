/*
 * Vanilla Bonk: quests.
 *
 * Account-wide goals (bonk_quest) that count across runs: kills, kills of a
 * kind or with a weapon, how far a tier was pushed, a level reached in one
 * run, Talent Shards found. A finished quest pays gold or shards, or unlocks
 * a weapon, passive, talent or upgrade; until then that stays out of the
 * game. Progress is kept per account (bonk_account_quest) and shown on the
 * quest board at camp.
 *
 * A class quest (`class` set) only counts while that class is played and
 * shows on the board's tab for it; class quests pay Mastery Points.
 */

#ifndef MANGOS_BONKQUESTS_H
#define MANGOS_BONKQUESTS_H

#include "Common.h"
#include "BonkMgr.h"
#include "Policies/Singleton.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

class Player;

enum BonkQuestType : uint8
{
    BONK_QUEST_KILLS        = 0,    // any enemies
    BONK_QUEST_KILL_KIND    = 1,    // target: spawn kind
    BONK_QUEST_KILL_WEAPON  = 2,    // target: weapon id; its evolutions count too
    BONK_QUEST_REACH_STAGE  = 3,    // target: tier id; amount: stage
    BONK_QUEST_REACH_LEVEL  = 4,    // amount: level in a single run
    BONK_QUEST_SHARDS       = 5,    // Talent Shards found in runs
    MAX_BONK_QUEST_TYPE
};

enum BonkQuestReward : uint8
{
    BONK_REWARD_GOLD    = 0,        // copper
    BONK_REWARD_SHARDS  = 1,
    BONK_REWARD_WEAPON  = 2,        // bonk_weapon.id
    BONK_REWARD_PASSIVE = 3,        // bonk_passive.id
    BONK_REWARD_TALENT  = 4,        // bonk_talent.id
    BONK_REWARD_UPGRADE = 5,        // bonk_upgrade.id
    BONK_REWARD_MASTERY = 6,        // Mastery Points for the quest's class
    MAX_BONK_REWARD
};

struct BonkQuest
{
    uint32 id = 0;
    std::string name;
    std::string description;
    std::string icon;
    BonkQuestType type = BONK_QUEST_KILLS;
    uint32 target = 0;
    uint32 amount = 1;
    BonkQuestReward rewardType = BONK_REWARD_GOLD;
    uint32 rewardValue = 0;
    uint8 classId = 0;              // 0 = any hero
};

class BonkQuests
{
    public:
        void Load();

        // A character of `account` logged in: load the account's progress and
        // count what it already did (stages reached before quests existed).
        void OnLogin(Player* player);
        void Save(uint32 account);

        // Something a quest still has to unlock stays out of the game.
        bool IsLocked(uint32 account, BonkQuestReward type, uint32 id) const;
        BonkQuest const* GetUnlockingQuest(BonkQuestReward type, uint32 id) const;

        void OnKill(Player* player, BonkSpawnKind kind, uint32 killingSpell);
        // `backfill`: the account's best stage at login, which says nothing
        // about which class reached it.
        void OnStage(Player* player, uint32 tierId, uint32 stage, bool backfill = false);
        void OnShards(Player* player, uint32 count);

        // Mastery Points `account` earned with quests of `classId`.
        uint32 GetMasteryPoints(uint32 account, uint8 classId) const;

        // qlog~npc (empty: a refresh), q~id~name~description~icon~progress~amount~done~reward~rewardIcon~class..., qlogend.
        // Only the account's quests and those of the player's class; `class` is 1 for the latter.
        void SendLog(Player* player, std::string const& npc);
        // One line per quest, for players without the addon.
        std::vector<std::string> DescribeAll(Player* player) const;

    private:
        struct Progress
        {
            uint32 value = 0;
            bool completed = false;
            bool dirty = false;
        };
        typedef std::map<uint32, Progress> AccountProgress;

        // `value` replaces the progress when `absolute`, else adds to it.
        void Advance(Player* player, AccountProgress& progress, BonkQuest const& quest, uint32 value, bool absolute);
        void Complete(Player* player, BonkQuest const& quest);
        static bool Counts(Player const* player, BonkQuest const& quest);
        std::string DescribeReward(BonkQuest const& quest, std::string* icon) const;
        uint32 GetRootWeapon(uint32 spellId) const;

        std::map<uint32, BonkQuest> m_quests;

        mutable std::mutex m_lock;
        std::map<uint32, AccountProgress> m_accounts;
};

#define sBonkQuests MaNGOS::Singleton<BonkQuests>::Instance()

#endif
