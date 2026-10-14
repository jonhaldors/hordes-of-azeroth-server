/*
 * Vanilla Bonk: quests and the quest board NPC.
 */

#include "BonkQuests.h"
#include "BonkPlayer.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Policies/SingletonImp.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "WorldSession.h"

#include <vector>

INSTANTIATE_SINGLETON_1(BonkQuests);

namespace
{
    uint32 const BONK_TEXT_QUEST_BOARD = 900005;
}

void BonkQuests::Load()
{
    m_quests.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `description`, `icon`, `type`, `target`, `amount`, `reward_type`, `reward_value`, `class` FROM `bonk_quest`"));
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

        if (type >= MAX_BONK_QUEST_TYPE || rewardType >= MAX_BONK_REWARD)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_quest`: quest %u has unknown type %u or reward type %u, skipped.", quest.id, type, rewardType);
            continue;
        }
        quest.type = BonkQuestType(type);
        quest.rewardType = BonkQuestReward(rewardType);

        bool rewardExists = true;
        switch (quest.rewardType)
        {
            case BONK_REWARD_WEAPON:  rewardExists = sBonkMgr.GetWeapon(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_PASSIVE: rewardExists = sBonkMgr.GetPassive(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_TALENT:  rewardExists = sBonkMgr.GetTalent(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_UPGRADE: rewardExists = sBonkMgr.GetUpgrade(quest.rewardValue) != nullptr; break;
            case BONK_REWARD_MASTERY: rewardExists = quest.classId != 0; break;     // points for a class
            default: break;
        }
        if (!rewardExists)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_quest`: quest %u unlocks %u, which does not exist; skipped.", quest.id, quest.rewardValue);
            continue;
        }

        m_quests[quest.id] = std::move(quest);
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk quests", uint32(m_quests.size()));
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

    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_accounts[account] = std::move(loaded);
    }

    // Stages reached before there were quests count as well.
    for (auto const& itr : sBonkMgr.GetTiers())
        OnStage(player, itr.first, player->GetBonkPlayer()->GetBestStage(itr.first), true);
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

bool BonkQuests::IsLocked(uint32 account, BonkQuestReward type, uint32 id) const
{
    BonkQuest const* quest = GetUnlockingQuest(type, id);
    if (!quest)
        return false;

    std::lock_guard<std::mutex> guard(m_lock);
    auto itr = m_accounts.find(account);
    if (itr == m_accounts.end())
        return true;
    auto progress = itr->second.find(quest->id);
    return progress == itr->second.end() || !progress->second.completed;
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

        // An evolution counts for the weapon it grew from.
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

void BonkQuests::Advance(Player* /*player*/, AccountProgress& progress, BonkQuest const& quest, uint32 value, bool absolute)
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

void BonkQuests::OnKill(Player* player, BonkSpawnKind kind, uint32 killingSpell)
{
    uint32 const account = player->GetSession()->GetAccountId();
    uint32 const weapon = GetRootWeapon(killingSpell);
    uint32 const level = player->GetLevel();

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
            bool const wasDone = progress[q.id].completed;
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
                // Levels only rise during a run, and kills are what raise them.
                case BONK_QUEST_REACH_LEVEL:
                    Advance(player, progress, q, level, true);
                    break;
                default:
                    break;
            }
            if (!wasDone && progress[q.id].completed)
                done.push_back(&q);
        }
    }

    for (BonkQuest const* quest : done)
        Complete(player, *quest);
}

void BonkQuests::OnStage(Player* player, uint32 tierId, uint32 stage, bool backfill)
{
    if (!stage)
        return;

    uint32 const account = player->GetSession()->GetAccountId();
    std::vector<BonkQuest const*> done;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_accounts.find(account);
        if (itr == m_accounts.end())
            return;

        for (auto const& quest : m_quests)
        {
            BonkQuest const& q = quest.second;
            if (q.type != BONK_QUEST_REACH_STAGE || q.target != tierId || !Counts(player, q) || (backfill && q.classId))
                continue;
            bool const wasDone = itr->second[q.id].completed;
            Advance(player, itr->second, q, stage, true);
            if (!wasDone && itr->second[q.id].completed)
                done.push_back(&q);
        }
    }

    for (BonkQuest const* quest : done)
        Complete(player, *quest);
}

void BonkQuests::OnShards(Player* player, uint32 count)
{
    uint32 const account = player->GetSession()->GetAccountId();
    std::vector<BonkQuest const*> done;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto itr = m_accounts.find(account);
        if (itr == m_accounts.end())
            return;

        for (auto const& quest : m_quests)
        {
            BonkQuest const& q = quest.second;
            if (q.type != BONK_QUEST_SHARDS || !Counts(player, q))
                continue;
            bool const wasDone = itr->second[q.id].completed;
            Advance(player, itr->second, q, count, false);
            if (!wasDone && itr->second[q.id].completed)
                done.push_back(&q);
        }
    }

    for (BonkQuest const* quest : done)
        Complete(player, *quest);
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
            bonk->SendMasteryState();       // the points count from now on
            break;
        default:
            break;      // an unlock: being done is all it takes
    }

    std::string icon;
    std::string const reward = DescribeReward(quest, &icon);
    if (bonk->IsAddonReady())
        bonk->SendAddon("questdone~" + std::to_string(quest.id) + "~" + quest.name + "~" + reward + "~" + icon);
    else
        bonk->Notify("Quest complete: " + quest.name + "! " + reward);
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
                return "Unlocks the spell " + weapon->name;
            }
            break;
        case BONK_REWARD_PASSIVE:
            if (BonkPassive const* passive = sBonkMgr.GetPassive(quest.rewardValue))
            {
                *icon = passive->icon;
                return "Unlocks the passive " + passive->name;
            }
            break;
        case BONK_REWARD_TALENT:
            if (BonkTalent const* talent = sBonkMgr.GetTalent(quest.rewardValue))
            {
                *icon = talent->layout.icon;
                return "Unlocks the fortune " + talent->name;
            }
            break;
        case BONK_REWARD_UPGRADE:
            if (BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(quest.rewardValue))
            {
                *icon = upgrade->layout.icon;
                return "Unlocks the upgrade " + upgrade->name;
            }
            break;
        case BONK_REWARD_MASTERY:
            *icon = "Spell_Holy_SealOfWisdom";
            return std::to_string(quest.rewardValue) + (quest.rewardValue == 1 ? " talent point" : " talent points");
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
            std::to_string(entry.completed ? quest.amount : entry.value) + "~" + std::to_string(quest.amount) + "~" +
            (entry.completed ? "1" : "0") + "~" + reward + "~" + rewardIcon + "~" + (quest.classId ? "1" : "0"));
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
        lines.push_back(quest.name + (entry.completed ? " (done): " : " " + std::to_string(entry.value) + "/" + std::to_string(quest.amount) + ": ") +
            quest.description + (reward.empty() ? "" : " - " + reward));
    }
    return lines;
}

// The quest board at camp. With the addon the board opens as a window in
// place of this gossip; without it the gossip lists the quests.
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

bool GossipSelect_npc_bonk_quests(Player* player, Creature* creature, uint32 /*sender*/, uint32 /*action*/)
{
    return GossipHello_npc_bonk_quests(player, creature);
}

// The board has no real quests to accept, but it always shows the yellow "!"
// so players find it at camp.
uint32 DialogStatus_npc_bonk_quests(Player* /*player*/, Creature* /*creature*/)
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
