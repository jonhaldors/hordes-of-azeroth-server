#include "BonkQuests.h"
#include "BonkMastery.h"
#include "BonkPlayer.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Policies/SingletonImp.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "SpellMgr.h"
#include "WorldSession.h"

#include <ctime>
#include <bitset>
#include <vector>

INSTANTIATE_SINGLETON_1(BonkQuests);

namespace
{
    uint32 const BONK_TEXT_QUEST_BOARD = 900005;

    uint32 const BONK_UNLOCK_BASE_PRICE = 2500;
    float const BONK_UNLOCK_CARD_RARITY[MAX_BONK_RARITY] = { 1.0f, 1.25f, 1.5f, 2.0f, 2.0f };
    float const BONK_UNLOCK_GLYPH_RARITY[MAX_BONK_RARITY] = { 1.5f, 2.0f, 3.0f, 4.0f, 6.0f };

    char const* const CATEGORY_KEYS[MAX_BONK_QUEST_CATEGORY] = { "slayer", "victory", "mastery", "challenge", "discovery", "collection" };

    struct ClassLook
    {
        char const* name;
        char const* icon;
    };
    ClassLook const CLASS_LOOKS[] = {
        { "", "" },
        { "Warrior", "Ability_Warrior_BattleShout" },
        { "Paladin", "Spell_Holy_SealOfMight" },
        { "Hunter", "Ability_Hunter_SniperShot" },
        { "Rogue", "Ability_Stealth" },
        { "Priest", "Spell_Holy_PowerWordShield" },
        { "", "" },
        { "Shaman", "Spell_Nature_BloodLust" },
        { "Mage", "Spell_Frost_FrostBolt02" },
        { "Warlock", "Spell_Shadow_SummonImp" },
        { "", "" },
        { "Druid", "Ability_Druid_CatForm" },
    };

    ClassLook const* GetClassLook(uint32 classId)
    {
        if (classId >= sizeof(CLASS_LOOKS) / sizeof(CLASS_LOOKS[0]) || !*CLASS_LOOKS[classId].name)
            return nullptr;
        return &CLASS_LOOKS[classId];
    }

    uint32 CountBits(uint32 value)
    {
        return uint32(std::bitset<32>(value).count());
    }
}

char const* BonkQuests::GetCategoryKey(BonkQuestCategory category)
{
    return category < MAX_BONK_QUEST_CATEGORY ? CATEGORY_KEYS[category] : "";
}

void BonkQuests::Load()
{
    m_quests.clear();
    m_titles.clear();

    if (std::unique_ptr<QueryResult> titles{ WorldDatabase.Query("SELECT `id`, `name` FROM `bonk_title`") })
    {
        do
        {
            Field* fields = titles->Fetch();
            m_titles[fields[0].GetUInt32()] = fields[1].GetCppString();
        } while (titles->NextRow());
    }

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `description`, `icon`, `type`, `target`, `amount`, `reward_type`, `reward_value`, `class`, `param`, `category` FROM `bonk_quest`"));
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk quests.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkQuest quest;
        quest.id = fields[0].GetUInt32();
        quest.name = fields[1].GetCppString();
        quest.description = fields[2].GetCppString();
        quest.icon = fields[3].GetCppString();
        uint8 const type = fields[4].GetUInt8();
        quest.target = fields[5].GetUInt32();
        quest.amount = std::max(1u, fields[6].GetUInt32());
        uint8 const rewardType = fields[7].GetUInt8();
        quest.rewardValue = fields[8].GetUInt32();
        quest.classId = fields[9].GetUInt8();
        quest.param = fields[10].GetUInt32();
        uint8 const category = fields[11].GetUInt8();

        if (type >= MAX_BONK_QUEST_TYPE || rewardType >= MAX_BONK_REWARD)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_quest`: quest %u has unknown type %u or reward type %u, skipped.", quest.id, type, rewardType);
            continue;
        }
        quest.type = BonkQuestType(type);
        quest.rewardType = BonkQuestReward(rewardType);
        if (category >= MAX_BONK_QUEST_CATEGORY)
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_quest`: quest %u has unknown category %u; shown under Slayer.", quest.id, category);
        else
            quest.category = BonkQuestCategory(category);

        bool rewardExists = true;
        switch (quest.rewardType)
        {
            case BONK_REWARD_WEAPON:
            case BONK_REWARD_EPIC_RANK: rewardExists = sBonkMgr.GetWeapon(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_PASSIVE:   rewardExists = sBonkMgr.GetPassive(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_TALENT:    rewardExists = sBonkMgr.GetTalent(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_UPGRADE:   rewardExists = sBonkMgr.GetUpgrade(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_MASTERY:   rewardExists = quest.classId != 0; break;
            case BONK_REWARD_CLASS:     rewardExists = GetClassLook(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_CAPSTONE:  rewardExists = sBonkMastery.GetNode(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_TITLE:     rewardExists = m_titles.count(quest.rewardValue) != 0; break;
            case BONK_REWARD_CARD:      rewardExists = sBonkMgr.GetCard(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_GLYPH:     rewardExists = sBonkMgr.GetGlyph(quest.rewardValue) != nullptr; break;
            default: break;
        }
        if (!rewardExists)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_quest`: quest %u unlocks %u, which does not exist; skipped.", quest.id, quest.rewardValue);
            continue;
        }

        m_quests[quest.id] = std::move(quest);
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk quests and %u titles", uint32(m_quests.size()), uint32(m_titles.size()));
}

void BonkQuests::OnLogin(Player* player)
{
    uint32 const account = player->GetSession()->GetAccountId();

    AccountProgress loaded;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `quest`, `progress`, `completed` FROM `bonk_account_quest` WHERE `account` = %u", account));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            Progress& progress = loaded[fields[0].GetUInt32()];
            progress.value = fields[1].GetUInt32();
            progress.completed = fields[2].GetUInt8() != 0;
        } while (result->NextRow());
    }

    std::set<uint64> owned;
    uint32 paid = 0;
    result = CharacterDatabase.PQuery("SELECT `type`, `id`, `paid` FROM `bonk_account_unlock` WHERE `account` = %u", account);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            owned.insert(UnlockKey(BonkQuestReward(fields[0].GetUInt8()), fields[1].GetUInt32()));
            if (fields[2].GetUInt8())
                ++paid;
        } while (result->NextRow());
    }

    std::set<uint64> disabled;
    result = CharacterDatabase.PQuery("SELECT `type`, `id` FROM `bonk_account_disabled` WHERE `account` = %u", account);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            disabled.insert(UnlockKey(BonkQuestReward(fields[0].GetUInt8()), fields[1].GetUInt32()));
        } while (result->NextRow());
    }

    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_accounts[account] = std::move(loaded);
        m_owned[account] = std::move(owned);
        m_paid[account] = paid;
        m_disabled[account] = std::move(disabled);
    }

    for (auto const& itr : sBonkMgr.GetTiers())
        OnStage(player, itr.first, player->GetBonkPlayer()->GetBestStage(itr.first), true);
    OnProgress(player, BONK_QUEST_MASTERY_LEVEL, player->GetClass(), player->GetBonkPlayer()->GetMasteryLevel(), true);
    OnProgress(player, BONK_QUEST_QUESTS_DONE, 0, CountCompleted(account), true);
}

void BonkQuests::Save(uint32 account)
{
    std::vector<std::pair<uint32, uint32>> rows;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_accounts.find(account);
        if (itr == m_accounts.end())
            return;
        for (auto& progress : itr->second)
        {
            if (!progress.second.dirty || progress.second.completed)
                continue;
            progress.second.dirty = false;
            rows.emplace_back(progress.first, progress.second.value);
        }
    }

    for (auto const& row : rows)
        CharacterDatabase.PExecute("REPLACE INTO `bonk_account_quest` (`account`, `quest`, `progress`, `completed`) VALUES (%u, %u, %u, 0)",
            account, row.first, row.second);
}

bool BonkQuests::IsDisabled(uint32 account, BonkQuestReward type, uint32 id) const
{
    std::lock_guard<std::mutex> guard(m_lock);
    auto itr = m_disabled.find(account);
    return itr != m_disabled.end() && itr->second.count(UnlockKey(type, id)) != 0;
}

bool BonkQuests::SetDisabled(uint32 account, BonkQuestReward type, uint32 id, bool disabled, uint32 slots, std::string& error)
{
    if (disabled && !IsTogglable(type))
    {
        error = "Only Darkmoon Cards and Glyphs can be switched off.";
        return false;
    }
    if (disabled && IsLocked(account, type, id))
    {
        error = "You can only switch off Darkmoon Cards and Glyphs you have unlocked.";
        return false;
    }
    if (disabled && GetDisabled(account).size() >= slots)
    {
        error = slots ? "You can only switch off " + std::to_string(slots) + (slots == 1 ? " Darkmoon Card or Glyph" : " Darkmoon Cards or Glyphs") +
            ". More ranks of Stacked Deck at the Upgrades vendor let you switch off more." :
            "Buy Stacked Deck at the Upgrades vendor to switch Darkmoon Cards and Glyphs off.";
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(m_lock);
        if (disabled)
            m_disabled[account].insert(UnlockKey(type, id));
        else
            m_disabled[account].erase(UnlockKey(type, id));
    }
    if (disabled)
        CharacterDatabase.PExecute("REPLACE INTO `bonk_account_disabled` (`account`, `type`, `id`) VALUES (%u, %u, %u)", account, uint32(type), id);
    else
        CharacterDatabase.PExecute("DELETE FROM `bonk_account_disabled` WHERE `account` = %u AND `type` = %u AND `id` = %u", account, uint32(type), id);
    return true;
}

void BonkQuests::TrimDisabled(uint32 account, uint32 slots)
{
    std::vector<std::pair<BonkQuestReward, uint32>> const disabled = GetDisabled(account);
    std::string error;
    for (size_t i = slots; i < disabled.size(); ++i)
        SetDisabled(account, disabled[i].first, disabled[i].second, false, slots, error);
}

std::vector<std::pair<BonkQuestReward, uint32>> BonkQuests::GetDisabled(uint32 account) const
{
    std::vector<std::pair<BonkQuestReward, uint32>> list;
    std::lock_guard<std::mutex> guard(m_lock);
    auto itr = m_disabled.find(account);
    if (itr != m_disabled.end())
        for (uint64 key : itr->second)
            list.emplace_back(BonkQuestReward(key >> 32), uint32(key & 0xFFFFFFFF));
    return list;
}

bool BonkQuests::IsLocked(uint32 account, BonkQuestReward type, uint32 id) const
{
    if (type == BONK_REWARD_GLYPH)
        if (BonkGlyph const* glyph = sBonkMgr.GetGlyph(id))
            if (glyph->starter)
                return false;
    BonkQuest const* quest = GetUnlockingQuest(type, id);
    if (!quest)
        return false;

    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_accounts.find(account);
        if (itr == m_accounts.end())
            return true;
        auto progress = itr->second.find(quest->id);
        if (progress == itr->second.end() || !progress->second.completed)
            return true;
    }
    return IsBought(type) && !IsOwned(account, type, id);
}

bool BonkQuests::IsForSale(uint32 account, BonkQuestReward type, uint32 id) const
{
    if (!IsBought(type) || !GetUnlockingQuest(type, id) || !IsLocked(account, type, id))
        return false;
    BonkQuest const* quest = GetUnlockingQuest(type, id);
    std::lock_guard<std::mutex> guard(m_lock);
    auto itr = m_accounts.find(account);
    if (itr == m_accounts.end())
        return false;
    auto progress = itr->second.find(quest->id);
    return progress != itr->second.end() && progress->second.completed;
}

bool BonkQuests::IsOwned(uint32 account, BonkQuestReward type, uint32 id) const
{
    std::lock_guard<std::mutex> guard(m_lock);
    auto itr = m_owned.find(account);
    return itr != m_owned.end() && itr->second.count(UnlockKey(type, id)) != 0;
}

uint32 BonkQuests::GetUnlockPrice(uint32 account, BonkQuestReward type, uint32 id) const
{
    float scale = 1.0f;
    switch (type)
    {
        case BONK_REWARD_WEAPON:
        case BONK_REWARD_PASSIVE:
            break;
        case BONK_REWARD_CARD:
            if (BonkCard const* card = sBonkMgr.GetCard(id))
                scale = BONK_UNLOCK_CARD_RARITY[std::min<uint32>(card->rarity, MAX_BONK_RARITY - 1)];
            break;
        case BONK_REWARD_GLYPH:
            if (BonkGlyph const* glyph = sBonkMgr.GetGlyph(id))
                scale = BONK_UNLOCK_GLYPH_RARITY[std::min<uint32>(glyph->rarity, MAX_BONK_RARITY - 1)];
            break;
        default:
            return 0;
    }

    float paid = 0.0f;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_paid.find(account);
        if (itr != m_paid.end())
            paid = float(itr->second);
    }
    float const copper = BONK_UNLOCK_BASE_PRICE * scale * (1.0f + 0.5f * paid + 0.05f * paid * paid);
    return std::max(100u, uint32(copper / 100.0f + 0.5f) * 100);
}

void BonkQuests::Buy(uint32 account, BonkQuestReward type, uint32 id)
{
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_owned[account].insert(UnlockKey(type, id));
        ++m_paid[account];
    }
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_unlock` (`account`, `type`, `id`, `paid`) VALUES (%u, %u, %u, 1)", account, uint32(type), id);
}

BonkQuest const* BonkQuests::GetUnlockingQuest(BonkQuestReward type, uint32 id) const
{
    for (auto const& itr : m_quests)
        if (itr.second.rewardType == type && itr.second.rewardValue == id)
            return &itr.second;
    return nullptr;
}

bool BonkQuests::Counts(Player const* player, BonkQuest const& quest)
{
    return !quest.classId || player->GetClass() == quest.classId;
}

uint32 BonkQuests::GetMasteryPoints(uint32 account, uint8 classId) const
{
    std::lock_guard<std::mutex> guard(m_lock);
    auto itr = m_accounts.find(account);
    if (itr == m_accounts.end())
        return 0;

    uint32 points = 0;
    for (auto const& quest : m_quests)
    {
        BonkQuest const& q = quest.second;
        if (q.rewardType != BONK_REWARD_MASTERY || q.classId != classId)
            continue;
        auto progress = itr->second.find(q.id);
        if (progress != itr->second.end() && progress->second.completed)
            points += q.rewardValue;
    }
    return points;
}

uint32 BonkQuests::GetRootWeapon(uint32 spellId) const
{
    if (!spellId)
        return 0;

    for (auto const& itr : sBonkMgr.GetWeapons())
    {
        if (itr.second.spellId != spellId)
            continue;

        BonkWeapon const* weapon = &itr.second;
        for (uint32 steps = 0; weapon->evolvesFrom && steps < 8; ++steps)
            if (BonkWeapon const* from = sBonkMgr.GetWeapon(weapon->evolvesFrom))
                weapon = from;
            else
                break;
        return weapon->id;
    }
    return 0;
}

void BonkQuests::Track(Player* player, std::function<void(BonkQuest const&, AccountProgress&)> const& step)
{
    if (player->GetBonkPlayer()->IsInSandbox())
        return;
    uint32 const account = player->GetSession()->GetAccountId();
    std::vector<BonkQuest const*> done;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_accounts.find(account);
        if (itr == m_accounts.end())
            return;
        AccountProgress& progress = itr->second;

        for (auto const& quest : m_quests)
        {
            BonkQuest const& q = quest.second;
            if (!Counts(player, q))
                continue;
            auto const entry = progress.find(q.id);
            if (entry != progress.end() && entry->second.completed)
                continue;
            step(q, progress);
            auto const after = progress.find(q.id);
            if (after != progress.end() && after->second.completed)
                done.push_back(&q);
        }
    }

    for (BonkQuest const* quest : done)
        Complete(player, *quest);
}

void BonkQuests::Advance(Player* , AccountProgress& progress, BonkQuest const& quest, uint32 value, bool absolute)
{
    Progress& entry = progress[quest.id];
    if (entry.completed)
        return;

    uint32 const next = std::min(quest.amount, absolute ? std::max(entry.value, value) : entry.value + value);
    if (next == entry.value)
        return;

    entry.value = next;
    entry.dirty = true;
    if (entry.value >= quest.amount)
        entry.completed = true;
}

void BonkQuests::AdvanceBit(AccountProgress& progress, BonkQuest const& quest, uint32 bit)
{
    if (bit >= 32)
        return;

    Progress& entry = progress[quest.id];
    if (entry.completed || (entry.value & (1u << bit)))
        return;

    entry.value |= 1u << bit;
    entry.dirty = true;
    if (CountBits(entry.value) >= quest.amount)
        entry.completed = true;
}

uint32 BonkQuests::GetShownProgress(BonkQuest const& quest, Progress const& entry)
{
    if (entry.completed)
        return quest.amount;
    return quest.CountsBits() ? CountBits(entry.value) : entry.value;
}

void BonkQuests::OnKill(Player* player, BonkSpawnKind kind, uint32 killingSpell, uint32 creatureType, uint32 entry, bool standing)
{
    if (player->GetBonkPlayer()->IsInSandbox())
        return;

    uint32 const weapon = GetRootWeapon(killingSpell);
    uint32 const level = player->GetLevel();
    SpellEntry const* spellInfo = killingSpell ? sSpellMgr.GetSpellEntry(killingSpell) : nullptr;
    uint32 const school = spellInfo ? spellInfo->School + 1 : 0;

    Track(player, [&](BonkQuest const& q, AccountProgress& progress)
    {
        switch (q.type)
        {
            case BONK_QUEST_KILLS:
                Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_KILL_KIND:
                if (q.target == uint32(kind))
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_KILL_WEAPON:
                if (weapon && q.target == weapon)
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_KILL_TYPE:
                if (q.target == creatureType)
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_KILL_SCHOOL:
                if (school && q.target == school)
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_KILLS_STANDING:
                if (standing)
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_KILL_CREATURE:
                if (q.target == entry)
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_REACH_LEVEL:
                Advance(player, progress, q, level, true);
                break;
            default:
                break;
        }
    });
}

void BonkQuests::OnStage(Player* player, uint32 tierId, uint32 stage, bool backfill)
{
    if (!stage)
        return;

    Track(player, [&](BonkQuest const& q, AccountProgress& progress)
    {
        if (q.type == BONK_QUEST_REACH_STAGE && q.target == tierId && !(backfill && q.classId))
            Advance(player, progress, q, stage, true);
    });
}

void BonkQuests::OnBossKilled(Player* player, uint32 tierId, uint32 stage, uint32 second)
{
    Track(player, [&](BonkQuest const& q, AccountProgress& progress)
    {
        switch (q.type)
        {
            case BONK_QUEST_BOSS_BEFORE:
                if ((!q.target || q.target == tierId) && second < q.param)
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_CLEAR_CLASSES:
                if (q.param == stage && (q.target & player->GetClassMask()))
                    AdvanceBit(progress, q, player->GetClass() - 1);
                break;
            default:
                break;
        }
    });
}

void BonkQuests::OnBossFight(Player* player, BonkSpawnKind kind, uint32 seconds, bool unhurt, uint32 healthPercent)
{
    Track(player, [&](BonkQuest const& q, AccountProgress& progress)
    {
        if (q.target != uint32(kind))
            return;
        switch (q.type)
        {
            case BONK_QUEST_BOSS_FAST:
                if (seconds <= q.param)
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_BOSS_UNHURT:
                if (unhurt)
                    Advance(player, progress, q, 1, false);
                break;
            case BONK_QUEST_BOSS_LOW_HEALTH:
                if (healthPercent < q.param)
                    Advance(player, progress, q, 1, false);
                break;
            default:
                break;
        }
    });
}

void BonkQuests::OnShards(Player* player, uint32 count)
{
    OnProgress(player, BONK_QUEST_SHARDS, 0, count);
}

void BonkQuests::OnProgress(Player* player, BonkQuestType type, uint32 target, uint32 value, bool absolute)
{
    Track(player, [&](BonkQuest const& q, AccountProgress& progress)
    {
        if (q.type != type || (q.target && q.target != target))
            return;
        if (q.CountsBits())
            AdvanceBit(progress, q, value);
        else
            Advance(player, progress, q, value, absolute);
    });
}

void BonkQuests::Complete(Player* player, BonkQuest const& quest)
{
    uint32 const account = player->GetSession()->GetAccountId();
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_quest` (`account`, `quest`, `progress`, `completed`) VALUES (%u, %u, %u, 1)",
        account, quest.id, quest.amount);
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_accounts[account][quest.id].dirty = false;
    }

    BonkPlayer* bonk = player->GetBonkPlayer();
    switch (quest.rewardType)
    {
        case BONK_REWARD_GOLD:
            player->ModifyMoney(int32(quest.rewardValue));
            break;
        case BONK_REWARD_SHARDS:
            bonk->AddShards(quest.rewardValue);
            break;
        case BONK_REWARD_MASTERY:
            bonk->SendMasteryState();
            break;
        default:
            break;
    }

    std::string icon;
    std::string const reward = DescribeReward(quest, &icon);
    if (bonk->IsAddonReady())
        bonk->SendAddon("questdone~" + std::to_string(quest.id) + "~" + quest.name + "~" + reward + "~" + icon);
    else
        bonk->Notify("Quest complete: " + quest.name + "! " + reward);

    OnProgress(player, BONK_QUEST_QUESTS_DONE, 0, CountCompleted(account), true);
}

uint32 BonkQuests::CountCompleted(uint32 account) const
{
    std::lock_guard<std::mutex> guard(m_lock);
    auto itr = m_accounts.find(account);
    if (itr == m_accounts.end())
        return 0;
    uint32 count = 0;
    for (auto const& progress : itr->second)
        if (progress.second.completed && m_quests.count(progress.first))
            ++count;
    return count;
}

std::string BonkQuests::DescribeReward(BonkQuest const& quest, std::string* icon) const
{
    switch (quest.rewardType)
    {
        case BONK_REWARD_GOLD:
            *icon = "INV_Misc_Coin_02";
            return FormatBonkMoney(quest.rewardValue);
        case BONK_REWARD_SHARDS:
            *icon = "INV_Misc_Ticket_Darkmoon_01";
            return FormatBonkShards(quest.rewardValue);
        case BONK_REWARD_WEAPON:
            if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(quest.rewardValue))
            {
                *icon = weapon->icon;
                return "Unlocks " + weapon->name + " for purchase at the Spellbook";
            }
            break;
        case BONK_REWARD_PASSIVE:
            if (BonkPassive const* passive = sBonkMgr.GetPassive(quest.rewardValue))
            {
                *icon = passive->icon;
                return "Unlocks " + passive->name + " for purchase at the Spellbook";
            }
            break;
        case BONK_REWARD_TALENT:
            if (BonkTalent const* talent = sBonkMgr.GetTalent(quest.rewardValue))
            {
                *icon = talent->layout.icon;
                return "Unlocks the fortune " + talent->name + " at Sayge";
            }
            break;
        case BONK_REWARD_UPGRADE:
            if (BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(quest.rewardValue))
            {
                *icon = upgrade->layout.icon;
                return "Unlocks " + upgrade->name + " at the Upgrades vendor";
            }
            break;
        case BONK_REWARD_MASTERY:
            *icon = "Spell_Holy_SealOfWisdom";
            return std::to_string(quest.rewardValue) + (quest.rewardValue == 1 ? " talent point" : " talent points");
        case BONK_REWARD_CLASS:
            if (ClassLook const* look = GetClassLook(quest.rewardValue))
            {
                *icon = look->icon;
                return std::string("Unlocks the ") + look->name + " class";
            }
            break;
        case BONK_REWARD_EPIC_RANK:
            if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(quest.rewardValue))
            {
                *icon = weapon->icon;
                return "An Epic rank for " + weapon->name;
            }
            break;
        case BONK_REWARD_CAPSTONE:
            if (BonkMasteryNode const* node = sBonkMastery.GetNode(quest.rewardValue))
            {
                *icon = node->icon;
                return "Unlocks " + node->name + ", the final talent in its tree";
            }
            break;
        case BONK_REWARD_CARD:
            if (BonkCard const* card = sBonkMgr.GetCard(quest.rewardValue))
            {
                *icon = card->icon;
                return "Unlocks the Darkmoon Card " + card->name + " for purchase at the Spellbook";
            }
            break;
        case BONK_REWARD_GLYPH:
            if (BonkGlyph const* glyph = sBonkMgr.GetGlyph(quest.rewardValue))
            {
                *icon = glyph->icon;
                return "Unlocks " + glyph->name + " for purchase at the Spellbook";
            }
            break;
        case BONK_REWARD_TITLE:
        {
            auto const title = m_titles.find(quest.rewardValue);
            if (title != m_titles.end())
            {
                *icon = "INV_Misc_Note_02";
                return "The title \"" + title->second + "\"";
            }
            break;
        }
        default:
            break;
    }
    *icon = "INV_Misc_QuestionMark";
    return "";
}

void BonkQuests::SendLog(Player* player, std::string const& npc)
{
    BonkPlayer* bonk = player->GetBonkPlayer();
    if (!bonk->IsAddonReady())
        return;

    AccountProgress progress;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_accounts.find(player->GetSession()->GetAccountId());
        if (itr != m_accounts.end())
            progress = itr->second;
    }

    bonk->SendAddon("qlog~" + npc);
    for (auto const& itr : m_quests)
    {
        BonkQuest const& quest = itr.second;
        if (!Counts(player, quest))
            continue;
        Progress const& entry = progress[quest.id];
        std::string rewardIcon;
        std::string const reward = DescribeReward(quest, &rewardIcon);
        bonk->SendAddon("q~" + std::to_string(quest.id) + "~" + quest.name + "~" + quest.description + "~" + quest.icon + "~" +
            std::to_string(GetShownProgress(quest, entry)) + "~" + std::to_string(quest.amount) + "~" +
            (entry.completed ? "1" : "0") + "~" + reward + "~" + rewardIcon + "~" + (quest.classId ? "1" : "0") + "~" +
            GetCategoryKey(quest.category));
    }
    bonk->SendAddon("qlogend");
}

std::vector<std::string> BonkQuests::DescribeAll(Player* player) const
{
    AccountProgress progress;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_accounts.find(player->GetSession()->GetAccountId());
        if (itr != m_accounts.end())
            progress = itr->second;
    }

    std::vector<std::string> lines;
    for (auto const& itr : m_quests)
    {
        BonkQuest const& quest = itr.second;
        if (!Counts(player, quest))
            continue;
        Progress const& entry = progress[quest.id];
        std::string icon;
        std::string const reward = DescribeReward(quest, &icon);
        lines.push_back(quest.name + (entry.completed ? " (done): " : " " + std::to_string(GetShownProgress(quest, entry)) + "/" +
            std::to_string(quest.amount) + ": ") + quest.description + (reward.empty() ? "" : " Reward: " + reward + "."));
    }
    return lines;
}

bool GossipHello_npc_bonk_quests(Player* player, Creature* creature)
{
    if (player->GetBonkPlayer()->IsAddonReady())
    {
        sBonkQuests.SendLog(player, creature->GetName());
        player->SEND_GOSSIP_MENU(BONK_TEXT_QUEST_BOARD, creature->GetObjectGuid());
        return true;
    }

    for (std::string const& line : sBonkQuests.DescribeAll(player))
    {
        if (player->PlayerTalkClass->GetGossipMenu().MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            break;
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, line.c_str(), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
    }
    player->SEND_GOSSIP_MENU(BONK_TEXT_QUEST_BOARD, creature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_bonk_quests(Player* player, Creature* creature, uint32 , uint32 )
{
    return GossipHello_npc_bonk_quests(player, creature);
}

uint32 DialogStatus_npc_bonk_quests(Player* , Creature*)
{
    return DIALOG_STATUS_AVAILABLE;
}

void AddSC_bonk_quests()
{
    Script* newscript = new Script;
    newscript->Name = "npc_bonk_quests";
    newscript->pGossipHello = &GossipHello_npc_bonk_quests;
    newscript->pGossipSelect = &GossipSelect_npc_bonk_quests;
    newscript->pNPCDialogStatus = &DialogStatus_npc_bonk_quests;
    newscript->RegisterSelf();
}
