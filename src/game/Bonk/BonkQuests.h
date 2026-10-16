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
#include <set>
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
    BONK_QUEST_QUESTS_DONE    = 17,   // amount: quests completed (milestones that unlock Sayge's run charges)
    // Megabonk-style quests (2026-10-15). "In a run" ones take the best run;
    // the rest add up over runs. Kills as a class are BONK_QUEST_KILLS with
    // the quest's class set.
    BONK_QUEST_KILL_SCHOOL    = 18,   // target: spell school + 1 (1 physical, 2 holy, 3 fire, 4 nature, 5 frost, 6 shadow, 7 arcane)
    BONK_QUEST_KILLS_STANDING = 19,   // kills while the hero stands still
    BONK_QUEST_PASSIVE_RANK   = 20,   // target: passive id, 0 any; amount: its rank in a run
    BONK_QUEST_SPELL_RANK     = 21,   // target: weapon id, 0 any; amount: its rank in a run
    BONK_QUEST_MAX_HEALTH     = 22,   // amount: maximum health in a run
    BONK_QUEST_BOSS_FAST      = 23,   // target: spawn kind (2 boss, 3 mini-boss); param: seconds from its arrival
    BONK_QUEST_BOSS_UNHURT    = 24,   // target: spawn kind; killed without an enemy hit on the hero since it arrived
    BONK_QUEST_BOSS_LOW_HEALTH = 25,  // target: spawn kind; param: the hero's health below this %
    BONK_QUEST_UNHURT_TIME    = 26,   // amount: seconds in a row without an enemy hit, in a run
    BONK_QUEST_OVERTIME       = 27,   // amount: seconds of overtime survived, in a run
    BONK_QUEST_LEECH_HEALED   = 28,   // amount: health healed by Leech
    BONK_QUEST_DODGES         = 29,   // amount: enemy hits dodged
    BONK_QUEST_ARMOR_ABSORBED = 30,   // amount: damage Armor took off enemy hits
    BONK_QUEST_CHESTS_BOUGHT  = 31,   // amount: chests bought with Valor
    BONK_QUEST_VENDOR_BOUGHT  = 32,   // amount: things bought from the wandering vendor
    BONK_QUEST_MINI_EVENTS    = 33,   // target: BonkMiniEventType (1 goblin, 2 bounty); amount: won
    BONK_QUEST_KILL_CREATURE  = 34,   // target: creature entry (a named boss or mini-boss)
    BONK_QUEST_DARK_FORTUNES  = 35,   // amount: Dark Fortunes switched on when a stage is cleared
    BONK_QUEST_PROJECTILES    = 36,   // amount: bolts fired
    BONK_QUEST_DEATHS         = 37,   // amount: runs ended by dying
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
    BONK_REWARD_CARD      = 11,       // bonk_card.id: chests may offer it
    BONK_REWARD_GLYPH     = 12,       // bonk_glyph.id: runs may offer it
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

        // Something a quest still has to unlock stays out of the game. A
        // spell, passive or card (Core loop v2 phase E) also has to be bought
        // at the Spellbook once its quest is done; until then it is for sale.
        bool IsLocked(uint32 account, BonkQuestReward type, uint32 id) const;
        // The pool toggle (Phase 4, codex): an unlocked Darkmoon Card or Glyph
        // the player switched off never comes up in a run (user, 2026-10-16:
        // cards and Glyphs only, as many as Stacked Deck's ranks, `slots`).
        bool IsDisabled(uint32 account, BonkQuestReward type, uint32 id) const;
        bool IsInPool(uint32 account, BonkQuestReward type, uint32 id) const { return !IsLocked(account, type, id) && !IsDisabled(account, type, id); }
        static bool IsTogglable(BonkQuestReward type) { return type == BONK_REWARD_CARD || type == BONK_REWARD_GLYPH; }
        bool SetDisabled(uint32 account, BonkQuestReward type, uint32 id, bool disabled, uint32 slots, std::string& error);
        // Past `slots` (ranks refunded): the latest switched back on.
        void TrimDisabled(uint32 account, uint32 slots);
        std::vector<std::pair<BonkQuestReward, uint32>> GetDisabled(uint32 account) const;
        // The bestiary (Phase 4, codex): kills per enemy, and the first.
        struct BestiaryEntry
        {
            uint32 kills = 0;
            uint64 firstKill = 0;       // unix time
            bool dirty = false;
        };
        std::map<uint32, BestiaryEntry> GetBestiary(uint32 account) const;
        bool IsForSale(uint32 account, BonkQuestReward type, uint32 id) const;
        BonkQuest const* GetUnlockingQuest(BonkQuestReward type, uint32 id) const;
        // The kinds of unlock bought with gold after their quest.
        // Quests the account has finished.
        uint32 GetCompletedCount(uint32 account) const { return CountCompleted(account); }
        static bool IsBought(BonkQuestReward type)
        {
            return type == BONK_REWARD_WEAPON || type == BONK_REWARD_PASSIVE || type == BONK_REWARD_CARD || type == BONK_REWARD_GLYPH;
        }
        // Its price in copper for `account` (0 for something that is not for
        // sale): each unlock paid for raises the next (Phase F).
        uint32 GetUnlockPrice(uint32 account, BonkQuestReward type, uint32 id) const;
        // Records the purchase (the caller took the gold).
        void Buy(uint32 account, BonkQuestReward type, uint32 id);

        // `creatureType`: the enemy's (CREATURE_TYPE_*).
        // `standing`: the hero stood still. `entry`: the enemy's creature.
        void OnKill(Player* player, BonkSpawnKind kind, uint32 killingSpell, uint32 creatureType, uint32 entry, bool standing);
        // A boss or mini-boss killed `seconds` after it arrived; `unhurt`: no
        // enemy hit the hero since; `healthPercent`: the hero's health then.
        void OnBossFight(Player* player, BonkSpawnKind kind, uint32 seconds, bool unhurt, uint32 healthPercent);
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
        uint32 CountCompleted(uint32 account) const;
        static bool Counts(Player const* player, BonkQuest const& quest);
        // What the board shows as done so far.
        static uint32 GetShownProgress(BonkQuest const& quest, Progress const& entry);
        std::string DescribeReward(BonkQuest const& quest, std::string* icon) const;
        uint32 GetRootWeapon(uint32 spellId) const;

        std::map<uint32, BonkQuest> m_quests;
        std::map<uint32, std::string> m_titles;     // bonk_title

        static uint64 UnlockKey(BonkQuestReward type, uint32 id) { return (uint64(type) << 32) | id; }
        bool IsOwned(uint32 account, BonkQuestReward type, uint32 id) const;

        mutable std::mutex m_lock;
        std::map<uint32, AccountProgress> m_accounts;
        std::map<uint32, std::set<uint64>> m_owned;     // bought unlocks by account (bonk_account_unlock)
        std::map<uint32, uint32> m_paid;                // of them, those paid for with gold (not kept from before)
        std::map<uint32, std::set<uint64>> m_disabled;  // switched off by account (bonk_account_disabled)
        std::map<uint32, std::map<uint32, BestiaryEntry>> m_bestiary;  // by account, creature entry (bonk_account_bestiary)
};

#define sBonkQuests MaNGOS::Singleton<BonkQuests>::Instance()

#endif
