/*
 * Vanilla Bonk: quests.
 *
 * Account-wide goals (bonk_quest) that count across runs: kills (of a kind,
 * with a weapon, of a creature type), how far a tier was pushed, stages
 * cleared with several classes, a level or a number of crits reached in one
 * run, healing taken, a boss killed early, Darkmoon Tickets found, a Mastery
 * level reached. Quests unlock content and currencies buy power: a finished
 * quest pays one reward, gold or tickets, or unlocks a spell, passive, class,
 * Epic spell rank, talent capstone or title (until then that stays out of the
 * game). Progress is kept per account (bonk_account_quest) and shown on the
 * quest board at camp, in tabs by category.
 *
 * A class quest (`class` set) only counts while that class is played and
 * shows on the board's tab for it; class quests pay talent points.
 *
 * Some types count things later content brings (Ley Lines, Greed stacks,
 * hidden encounters, shrines, heirlooms): that content calls OnProgress.
 */

#ifndef MANGOS_BONKQUESTS_H
#define MANGOS_BONKQUESTS_H

#include "Common.h"
#include "BonkMgr.h"
#include "Policies/Singleton.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class Player;

enum BonkQuestType : uint8
{
    BONK_QUEST_KILLS          = 0,    // any enemies
    BONK_QUEST_KILL_KIND      = 1,    // target: spawn kind
    BONK_QUEST_KILL_WEAPON    = 2,    // target: weapon id; its evolutions count too
    BONK_QUEST_REACH_STAGE    = 3,    // target: tier id; amount: stage
    BONK_QUEST_REACH_LEVEL    = 4,    // amount: level in a single run
    BONK_QUEST_SHARDS         = 5,    // Darkmoon Tickets found in runs
    BONK_QUEST_CRITS          = 6,    // amount: crits in a single run
    BONK_QUEST_HEALED         = 7,    // amount: health healed, across runs
    BONK_QUEST_KILL_TYPE      = 8,    // target: creature type (6 undead)
    BONK_QUEST_LEY_LINES      = 9,    // Ley Lines captured
    BONK_QUEST_GREED_CLEAR    = 10,   // amount: Greed Totem stacks held when a stage is cleared
    BONK_QUEST_CLEAR_CLASSES  = 11,   // target: class mask; param: stage; amount: of those classes to clear it with
    BONK_QUEST_BOSS_BEFORE    = 12,   // target: tier id, 0 any; param: second of the stage; amount: bosses killed before it
    BONK_QUEST_DISCOVER       = 13,   // target: hidden encounter id
    BONK_QUEST_SHRINE_TYPES   = 14,   // amount: kinds of shrine activated
    BONK_QUEST_HEIRLOOMS      = 15,   // heirlooms extracted
    BONK_QUEST_MASTERY_LEVEL  = 16,   // target: class id, 0 any; amount: Mastery level
    MAX_BONK_QUEST_TYPE
};

enum BonkQuestReward : uint8
{
    BONK_REWARD_GOLD      = 0,        // copper
    BONK_REWARD_SHARDS    = 1,        // Darkmoon Tickets
    BONK_REWARD_WEAPON    = 2,        // bonk_weapon.id
    BONK_REWARD_PASSIVE   = 3,        // bonk_passive.id
    BONK_REWARD_TALENT    = 4,        // bonk_talent.id
    BONK_REWARD_UPGRADE   = 5,        // bonk_upgrade.id
    BONK_REWARD_MASTERY   = 6,        // talent points for the quest's class
    BONK_REWARD_CLASS     = 7,        // class id: playable once done (class locks come later)
    BONK_REWARD_EPIC_RANK = 8,        // bonk_weapon.id: its Epic rank joins the level-up pool
    BONK_REWARD_CAPSTONE  = 9,        // bonk_mastery.id: a talent capstone
    BONK_REWARD_TITLE     = 10,       // bonk_title.id (shown with the cosmetics, later)
    MAX_BONK_REWARD
};

// The quest board's tabs.
enum BonkQuestCategory : uint8
{
    BONK_QUEST_SLAYER     = 0,
    BONK_QUEST_VICTORY    = 1,
    BONK_QUEST_MASTERY    = 2,
    BONK_QUEST_CHALLENGE  = 3,
    BONK_QUEST_DISCOVERY  = 4,
    BONK_QUEST_COLLECTION = 5,
    MAX_BONK_QUEST_CATEGORY
};

struct BonkQuest
{
    uint32 id = 0;
    std::string name;
    std::string description;
    std::string icon;
    BonkQuestType type = BONK_QUEST_KILLS;
    uint32 target = 0;
    uint32 param = 0;
    uint32 amount = 1;
    BonkQuestReward rewardType = BONK_REWARD_GOLD;
    uint32 rewardValue = 0;
    uint8 classId = 0;              // 0 = any hero
    BonkQuestCategory category = BONK_QUEST_SLAYER;

    // Progress is a set of bits (classes, shrine kinds), done when enough are set.
    bool CountsBits() const { return type == BONK_QUEST_CLEAR_CLASSES || type == BONK_QUEST_SHRINE_TYPES; }
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

        // `creatureType`: the enemy's (CREATURE_TYPE_*).
        void OnKill(Player* player, BonkSpawnKind kind, uint32 killingSpell, uint32 creatureType);
        // `backfill`: the account's best stage at login, which says nothing
        // about which class reached it.
        void OnStage(Player* player, uint32 tierId, uint32 stage, bool backfill = false);
        // A stage boss died `second` seconds into stage `stage`.
        void OnBossKilled(Player* player, uint32 tierId, uint32 stage, uint32 second);
        void OnShards(Player* player, uint32 count);
        // Anything else a quest counts: `value` adds to the progress, or
        // replaces it when `absolute` and higher (a best in one run). `target`
        // must match the quest's, if it has one. A bit-counting type sets
        // bit `value` instead.
        void OnProgress(Player* player, BonkQuestType type, uint32 target, uint32 value, bool absolute = false);

        // Talent points `account` earned with quests of `classId`.
        uint32 GetMasteryPoints(uint32 account, uint8 classId) const;

        // qlog~npc (empty: a refresh), q~id~name~description~icon~progress~amount~done~reward~rewardIcon~class~category..., qlogend.
        // Only the account's quests and those of the player's class; `class` is 1 for the latter.
        void SendLog(Player* player, std::string const& npc);
        // One line per quest, for players without the addon.
        std::vector<std::string> DescribeAll(Player* player) const;

        static char const* GetCategoryKey(BonkQuestCategory category);

    private:
        struct Progress
        {
            uint32 value = 0;
            bool completed = false;
            bool dirty = false;
        };
        typedef std::map<uint32, Progress> AccountProgress;

        // Runs `step` over every quest that counts for the player, then pays
        // out those it finished.
        void Track(Player* player, std::function<void(BonkQuest const&, AccountProgress&)> const& step);
        // `value` replaces the progress when `absolute`, else adds to it.
        void Advance(Player* player, AccountProgress& progress, BonkQuest const& quest, uint32 value, bool absolute);
        // Sets bit `bit` of a bit-counting quest's progress.
        void AdvanceBit(AccountProgress& progress, BonkQuest const& quest, uint32 bit);
        void Complete(Player* player, BonkQuest const& quest);
        static bool Counts(Player const* player, BonkQuest const& quest);
        // What the board shows as done so far.
        static uint32 GetShownProgress(BonkQuest const& quest, Progress const& entry);
        std::string DescribeReward(BonkQuest const& quest, std::string* icon) const;
        uint32 GetRootWeapon(uint32 spellId) const;

        std::map<uint32, BonkQuest> m_quests;
        std::map<uint32, std::string> m_titles;     // bonk_title

        mutable std::mutex m_lock;
        std::map<uint32, AccountProgress> m_accounts;
};

#define sBonkQuests MaNGOS::Singleton<BonkQuests>::Instance()

#endif
