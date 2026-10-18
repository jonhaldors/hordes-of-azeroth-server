#include "BonkPlayer.h"
#include "BonkMastery.h"
#include "BonkMgr.h"
#include "BonkQuests.h"
#include "BonkRun.h"
#include "BonkRunStats.h"
#include "Chat.h"
#include "Config/Config.h"
#include "Creature.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "MasterPlayer.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellAuras.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
    float const BONK_CAMP_RADIUS = 300.0f;
    uint32 const BONK_CAMP_SIT_DELAY_MS = 500;
    float const BONK_CAMP_CHAIR_RANGE = 3.0f;
    float const BONK_CAMP_CHAIR_TAKEN = 1.0f;

    float const BONK_SHOP_RANGE = 15.0f;

    uint32 const BONK_GEAR_VENDOR = 900004;
    float const BONK_GEAR_VENDOR_RANGE = 10.0f;

    uint32 const BONK_MAILBOX_BEACON = 900129;
    uint32 const BONK_MAILBOX_CHECK_MS = 1000;
    float const BONK_MAILBOX_GLOW_RANGE = 100.0f;

    uint32 const BONK_ITEM_STARTER_BAG = 900225;
    uint32 const BONK_ITEM_RUN_HEARTHSTONE = 900230;
    uint32 const BONK_ITEM_OLD_ARROWS = 900226;
    uint32 const BONK_ITEM_OLD_BULLETS = 900227;

    std::string FormatStat(float value)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%g", value);
        return buffer;
    }

    std::string FormatStatList(float const* stats)
    {
        std::string list;
        for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
            if (stats[effect] != 0.0f)
                list += (list.empty() ? "" : ",") + std::to_string(effect) + ":" + FormatStat(stats[effect]);
        return list;
    }

    BonkQuest const* GetLockingQuest(Player* player, BonkQuestReward type, uint32 id)
    {
        if (!sBonkQuests.IsLocked(player->GetSession()->GetAccountId(), type, id))
            return nullptr;
        return sBonkQuests.GetUnlockingQuest(type, id);
    }

    std::string RequirementText(std::string const& name, uint8 ranks, uint8 maxRank)
    {
        if (maxRank <= 1)
            return "Requires " + name + ".";
        return "Requires " + std::to_string(ranks) + (ranks == 1 ? " rank in " : " ranks in ") + name + ".";
    }
}

BonkPlayer::BonkPlayer(Player* player) : m_player(player)
{
}

BonkPlayer::~BonkPlayer() = default;

void BonkPlayer::OnLogin()
{
    uint32 const accountId = m_player->GetSession()->GetAccountId();
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `gold` FROM `bonk_account` WHERE `account` = %u", accountId));
    if (result)
        m_player->SetMoney(result->Fetch()[0].GetUInt32());
    else
        CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `gold`) VALUES (%u, %u)", accountId, m_player->GetMoney());
    m_goldLoaded = true;

    for (uint8 i = 0; i < PLAYER_EXPLORED_ZONES_SIZE; ++i)
        m_player->SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + i, 0xFFFFFFFF);

    m_bestStages.clear();
    result = CharacterDatabase.PQuery("SELECT `tier`, `best_stage` FROM `bonk_account_tier` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            m_bestStages[fields[0].GetUInt32()] = fields[1].GetUInt32();
        } while (result->NextRow());
    }

    m_record = Record();
    result = CharacterDatabase.PQuery("SELECT `runs`, `best_stage`, `best_time_sec`, `kills`, `gold_earned` FROM `bonk_character_stats` WHERE `guid` = %u",
        m_player->GetGUIDLow());
    if (result)
    {
        Field* fields = result->Fetch();
        m_record.runs = fields[0].GetUInt32();
        m_record.bestStage = fields[1].GetUInt32();
        m_record.bestSeconds = fields[2].GetUInt32();
        m_record.kills = fields[3].GetUInt32();
        m_record.goldEarned = fields[4].GetUInt32();
    }

    LoadPurchases();
    LoadMastery();
    LoadStarter();
    sBonkQuests.OnLogin(m_player);

    ResetCharacter();
    ClearAuras();
    m_campCheckPending = true;
    for (auto const& itr : sBonkMgr.GetGear())
        if (m_player->HasItemCount(itr.first, 1, true))
            m_player->DestroyItemCount(itr.first, 255, true, false, true);
    if (m_player->HasItemCount(BONK_ITEM_RUN_HEARTHSTONE, 1, true))
        m_player->DestroyItemCount(BONK_ITEM_RUN_HEARTHSTONE, 255, true, false, true);
    for (uint32 ammo : { BONK_ITEM_OLD_ARROWS, BONK_ITEM_OLD_BULLETS })
        if (uint32 const count = m_player->GetItemCount(ammo, true))
            m_player->DestroyItemCount(ammo, count, true, false, true);
    if (m_player->GetUInt32Value(PLAYER_AMMO_ID))
        m_player->RemoveAmmo();

    GiveStarterBags();
    TeachWeaponSkills();
}

void BonkPlayer::TeachWeaponSkills()
{
    uint32 const swords = 201, twoHandedSwords = 202, maces = 198, twoHandedMaces = 199, daggers = 1180, staves = 227;
    uint32 const bows = 264, guns = 266, crossbows = 5011, shield = 9116, dualWield = 674;
    std::vector<uint32> spells;
    switch (m_player->GetClass())
    {
        case CLASS_WARRIOR: spells = { swords, twoHandedSwords, shield, dualWield }; break;
        case CLASS_PALADIN: spells = { maces, swords, twoHandedMaces, twoHandedSwords, shield }; break;
        case CLASS_SHAMAN:  spells = { maces, twoHandedMaces, shield }; break;
        case CLASS_ROGUE:   spells = { daggers, swords, dualWield }; break;
        case CLASS_HUNTER:  spells = { bows, guns, crossbows }; break;
        case CLASS_DRUID:   spells = { staves, twoHandedMaces }; break;
        case CLASS_MAGE:
        case CLASS_WARLOCK: spells = { staves, swords, daggers }; break;
        case CLASS_PRIEST:  spells = { staves, maces }; break;
        default: break;
    }
    for (uint32 spell : spells)
        if (!m_player->HasSpell(spell))
            m_player->LearnSpell(spell, false);
}

void BonkPlayer::GiveStarterBags()
{
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        if (!m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            m_player->StoreNewItemInBestSlots(BONK_ITEM_STARTER_BAG, 1);

    uint8 const bankBagSlots = BANK_SLOT_BAG_END - BANK_SLOT_BAG_START;
    if (m_player->GetBankBagSlotCount() < bankBagSlots)
        m_player->SetBankBagSlotCount(bankBagSlots);
}

void BonkPlayer::LoadPurchases()
{
    uint32 const accountId = m_player->GetSession()->GetAccountId();

    m_upgradeRanks.clear();
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery("SELECT `upgrade`, `rank` FROM `bonk_account_upgrade` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            m_upgradeRanks[fields[0].GetUInt32()] = fields[1].GetUInt8();
        } while (result->NextRow());
    }

    m_shards = 0;
    result = CharacterDatabase.PQuery("SELECT `shards` FROM `bonk_account` WHERE `account` = %u", accountId);
    if (result)
        m_shards = result->Fetch()[0].GetUInt32();

    m_talentRanks.clear();
    m_enabledTalents.clear();
    result = CharacterDatabase.PQuery("SELECT `talent`, `rank`, `enabled` FROM `bonk_account_talent` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const talentId = fields[0].GetUInt32();
            m_talentRanks[talentId] = fields[1].GetUInt8();
            if (fields[2].GetUInt8())
                m_enabledTalents.insert(talentId);
        } while (result->NextRow());
    }
}

void BonkPlayer::LoadMastery()
{
    uint32 const accountId = m_player->GetSession()->GetAccountId();
    uint8 const classId = m_player->GetClass();

    m_masteryXp = 0;
    std::unique_ptr<QueryResult> result = CharacterDatabase.PQuery("SELECT `xp` FROM `bonk_account_mastery` WHERE `account` = %u AND `class` = %u",
        accountId, uint32(classId));
    if (result)
        m_masteryXp = result->Fetch()[0].GetUInt32();

    m_masteryRanks.clear();
    result = CharacterDatabase.PQuery("SELECT `node`, `rank` FROM `bonk_account_mastery_node` WHERE `account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BonkMasteryNode const* node = sBonkMastery.GetNode(fields[0].GetUInt32());
            if (node && node->classId == classId && fields[1].GetUInt8())
                m_masteryRanks[node->id] = std::min(fields[1].GetUInt8(), node->maxRank);
        } while (result->NextRow());
    }
}

std::string BonkPlayer::GetClassName() const
{
    if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(m_player->GetClass()))
        if (char const* name = entry->name[sWorld.GetDefaultDbcLocale()])
            return name;
    return "Hero";
}

uint32 BonkPlayer::GetMasteryLevel() const
{
    return BonkMastery::GetLevel(m_masteryXp);
}

uint32 BonkPlayer::GetMasteryPoints() const
{
    return GetMasteryLevel() + sBonkQuests.GetMasteryPoints(m_player->GetSession()->GetAccountId(), m_player->GetClass());
}

uint32 BonkPlayer::GetMasterySpent() const
{
    uint32 spent = 0;
    for (auto const& itr : m_masteryRanks)
        spent += itr.second;
    return spent;
}

uint32 BonkPlayer::GetMasterySpentInTree(uint32 treeId) const
{
    uint32 spent = 0;
    for (auto const& itr : m_masteryRanks)
        if (BonkMasteryNode const* node = sBonkMastery.GetNode(itr.first))
            if (node->tree == treeId)
                spent += itr.second;
    return spent;
}

uint8 BonkPlayer::GetMasteryRank(uint32 nodeId) const
{
    auto itr = m_masteryRanks.find(nodeId);
    return itr != m_masteryRanks.end() ? itr->second : 0;
}

BonkMasteryBonuses BonkPlayer::GetMasteryBonuses() const
{
    return sBonkMastery.Evaluate(m_masteryRanks);
}

void BonkPlayer::SendMasteryState() const
{
    uint32 const level = GetMasteryLevel();
    uint32 const earned = GetMasteryPoints();
    uint32 const spent = GetMasterySpent();
    SendAddon("mastery~" + GetClassName() + "~" + std::to_string(level) + "~" + std::to_string(BonkMastery::GetXpIntoLevel(m_masteryXp)) + "~" +
        std::to_string(level < BONK_MASTERY_MAX_LEVEL ? BonkMastery::GetXpForLevel(level + 1) : 0) + "~" +
        std::to_string(BONK_MASTERY_MAX_LEVEL) + "~" + std::to_string(earned > spent ? earned - spent : 0) + "~" +
        (sBonkMastery.HasTrees(m_player->GetClass()) ? "1" : "0"));
}

void BonkPlayer::AddMasteryXp(uint32 xp)
{
    if (!xp)
        return;

    uint32 const before = GetMasteryLevel();
    m_masteryXp += xp;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_mastery` (`account`, `class`, `xp`) VALUES (%u, %u, %u)",
        m_player->GetSession()->GetAccountId(), uint32(m_player->GetClass()), m_masteryXp);

    uint32 const after = GetMasteryLevel();
    if (after > before)
        sBonkQuests.OnProgress(m_player, BONK_QUEST_MASTERY_LEVEL, m_player->GetClass(), after, true);
    std::string const name = GetClassName() + " Mastery";
    if (after > before)
        Notify("Your " + name + " reached level " + std::to_string(after) + "! You have " + std::to_string(after - before) +
            (after - before == 1 ? " new talent point" : " new talent points") + " to spend at the Talent Trainer.");
    else if (after < BONK_MASTERY_MAX_LEVEL)
        Notify("You gained " + std::to_string(xp) + " " + name + " experience (" + std::to_string(BonkMastery::GetXpIntoLevel(m_masteryXp)) + "/" +
            std::to_string(BonkMastery::GetXpForLevel(after + 1)) + ").");
    SendMasteryState();
}

bool BonkPlayer::LearnMastery(uint32 nodeId, std::string& message)
{
    BonkMasteryNode const* node = sBonkMastery.GetNode(nodeId);
    if (!node || node->classId != m_player->GetClass())
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const rank = GetMasteryRank(nodeId);
    if (rank >= node->maxRank)
    {
        message = node->name + " is already at its highest rank.";
        return false;
    }

    uint32 const treePoints = (node->row - 1) * BONK_MASTERY_POINTS_PER_ROW;
    if (GetMasterySpentInTree(node->tree) < treePoints)
    {
        message = "Requires " + std::to_string(treePoints) + " points in this tree.";
        return false;
    }

    BonkMasteryNode const* required = sBonkMastery.GetNode(node->requiredId);
    if (required && GetMasteryRank(required->id) < node->requiredRank)
    {
        message = RequirementText(required->name, node->requiredRank, required->maxRank);
        return false;
    }

    if (GetMasterySpent() >= GetMasteryPoints())
    {
        message = "You have no talent points left. Level up your Mastery to earn more.";
        return false;
    }

    m_masteryRanks[nodeId] = rank + 1;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_mastery_node` (`account`, `node`, `rank`) VALUES (%u, %u, %u)",
        m_player->GetSession()->GetAccountId(), nodeId, uint32(rank + 1));
    message = "You learned " + node->name + (node->maxRank > 1 ? " (rank " + std::to_string(rank + 1) + ")." : ".");
    SendMasteryState();
    return true;
}

bool BonkPlayer::ResetMastery(std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }
    if (m_masteryRanks.empty())
    {
        message = "You have not spent any talent points.";
        return false;
    }

    std::string nodes;
    for (auto const& itr : m_masteryRanks)
        nodes += (nodes.empty() ? "" : ",") + std::to_string(itr.first);
    CharacterDatabase.PExecute("DELETE FROM `bonk_account_mastery_node` WHERE `account` = %u AND `node` IN (%s)",
        m_player->GetSession()->GetAccountId(), nodes.c_str());

    uint32 const refunded = GetMasterySpent();
    m_masteryRanks.clear();
    message = "You got back " + std::to_string(refunded) + (refunded == 1 ? " talent point." : " talent points.");
    SendMasteryState();
    return true;
}

bool BonkPlayer::RefundMastery(uint32 nodeId, std::string& message)
{
    BonkMasteryNode const* node = sBonkMastery.GetNode(nodeId);
    if (!node || node->classId != m_player->GetClass())
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const rank = GetMasteryRank(nodeId);
    if (!rank)
    {
        message = "You have not learned " + node->name + ".";
        return false;
    }

    for (auto const& itr : m_masteryRanks)
    {
        BonkMasteryNode const* other = sBonkMastery.GetNode(itr.first);
        if (!other || !itr.second || other->id == nodeId)
            continue;

        if (other->requiredId == nodeId && rank - 1 < other->requiredRank)
        {
            message = "Refund " + other->name + " first, because it requires this one.";
            return false;
        }

        if (other->tree == node->tree && other->row > node->row)
        {
            uint32 above = 0;
            for (auto const& inner : m_masteryRanks)
                if (BonkMasteryNode const* placed = sBonkMastery.GetNode(inner.first))
                    if (placed->tree == other->tree && placed->row < other->row)
                        above += inner.second;
            if (above - 1 < uint32(other->row - 1) * BONK_MASTERY_POINTS_PER_ROW)
            {
                message = "Refund " + other->name + " first, because it needs these points spent in this tree.";
                return false;
            }
        }
    }

    if (rank > 1)
    {
        m_masteryRanks[nodeId] = rank - 1;
        CharacterDatabase.PExecute("REPLACE INTO `bonk_account_mastery_node` (`account`, `node`, `rank`) VALUES (%u, %u, %u)",
            m_player->GetSession()->GetAccountId(), nodeId, uint32(rank - 1));
    }
    else
    {
        m_masteryRanks.erase(nodeId);
        CharacterDatabase.PExecute("DELETE FROM `bonk_account_mastery_node` WHERE `account` = %u AND `node` = %u",
            m_player->GetSession()->GetAccountId(), nodeId);
    }
    message = "You refunded " + (node->maxRank > 1 ? "rank " + std::to_string(rank) + " of " : std::string()) + node->name + " and got 1 talent point back.";
    SendMasteryState();
    return true;
}

void BonkPlayer::OnLogout()
{
    m_previewTelegraphs.Dismiss(m_player);
    if (m_run)
    {
        m_run->Abort();
        m_run.reset();
    }
    ClearAuras();
    m_addonReady = false;
    sBonkQuests.Save(m_player->GetSession()->GetAccountId());

    WriteHeroScreenStats();
}

void BonkPlayer::RecordRun(uint32 stage, uint32 seconds, uint32 kills, uint32 goldEarned)
{
    if (IsInSandbox())
        return;
    ++m_record.runs;
    m_record.bestStage = std::max(m_record.bestStage, stage);
    m_record.bestSeconds = std::max(m_record.bestSeconds, seconds);
    m_record.kills += kills;
    m_record.goldEarned += goldEarned;

    CharacterDatabase.PExecute("REPLACE INTO `bonk_character_stats` (`guid`, `runs`, `best_stage`, `best_time_sec`, `kills`, `gold_earned`) "
        "VALUES (%u, %u, %u, %u, %u, %u)", m_player->GetGUIDLow(), m_record.runs, m_record.bestStage, m_record.bestSeconds,
        m_record.kills, m_record.goldEarned);
}

void BonkPlayer::WriteHeroScreenStats() const
{
    std::string const path = sConfig.GetStringDefault("Bonk.GlueStatsFile", "");
    if (path.empty() || !m_goldLoaded)
        return;

    struct Hero
    {
        std::string name;
        Record record;
        uint32 items = 0;
        uint32 itemLevels = 0;
        uint32 gearValue = 0;
    };
    std::map<uint32, Hero> heroes;

    auto addItem = [](Hero& hero, uint32 slot, uint32 entry)
    {
        if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
            return;
        if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry))
        {
            ++hero.items;
            hero.itemLevels += proto->ItemLevel;
            hero.gearValue += proto->BuyPrice;
        }
    };

    uint32 const accountId = m_player->GetSession()->GetAccountId();
    uint32 const ownGuid = m_player->GetGUIDLow();

    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `guid`, `name` FROM `characters` WHERE `account` = %u", accountId));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            heroes[fields[0].GetUInt32()].name = fields[1].GetCppString();
        } while (result->NextRow());
    }

    result = CharacterDatabase.PQuery("SELECT s.`guid`, s.`runs`, s.`best_stage`, s.`best_time_sec`, s.`kills`, s.`gold_earned` "
        "FROM `bonk_character_stats` s JOIN `characters` c ON c.`guid` = s.`guid` WHERE c.`account` = %u", accountId);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            auto hero = heroes.find(fields[0].GetUInt32());
            if (hero == heroes.end() || hero->first == ownGuid)
                continue;
            hero->second.record.runs = fields[1].GetUInt32();
            hero->second.record.bestStage = fields[2].GetUInt32();
            hero->second.record.bestSeconds = fields[3].GetUInt32();
            hero->second.record.kills = fields[4].GetUInt32();
            hero->second.record.goldEarned = fields[5].GetUInt32();
        } while (result->NextRow());
    }

    result = CharacterDatabase.PQuery("SELECT i.`guid`, i.`slot`, i.`item_id` FROM `character_inventory` i JOIN `characters` c ON c.`guid` = i.`guid` "
        "WHERE c.`account` = %u AND i.`bag` = 0 AND i.`slot` < %u", accountId, uint32(EQUIPMENT_SLOT_END));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            auto hero = heroes.find(fields[0].GetUInt32());
            if (hero != heroes.end() && hero->first != ownGuid)
                addItem(hero->second, fields[1].GetUInt32(), fields[2].GetUInt32());
        } while (result->NextRow());
    }

    Hero& self = heroes[ownGuid];
    self.name = m_player->GetName();
    self.record = m_record;
    for (uint32 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            addItem(self, slot, item->GetEntry());

    std::ostringstream lua;
    lua << "BONK_STATS = {\n";
    lua << "\tgold = " << m_player->GetMoney() << ",\n";
    lua << "\tlastHero = \"" << m_player->GetName() << "\",\n";
    lua << "\theroes = {\n";
    for (auto const& itr : heroes)
    {
        Hero const& hero = itr.second;
        if (hero.name.empty() || hero.name.find_first_of("\\\"\n") != std::string::npos)
            continue;
        lua << "\t\t[\"" << hero.name << "\"] = { runs = " << hero.record.runs
            << ", bestStage = " << hero.record.bestStage
            << ", bestTime = " << hero.record.bestSeconds
            << ", kills = " << hero.record.kills
            << ", goldEarned = " << hero.record.goldEarned
            << ", items = " << hero.items
            << ", itemLevel = " << (hero.items ? hero.itemLevels / hero.items : 0)
            << ", gearValue = " << hero.gearValue << " },\n";
    }
    lua << "\t},\n};\n";
    WriteHeroScreenFile(lua.str());
}

void BonkPlayer::WriteHeroScreenFile(std::string const& lua)
{
    std::string const path = sConfig.GetStringDefault("Bonk.GlueStatsFile", "");
    if (path.empty())
        return;

    std::string const temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::out | std::ios::trunc);
        if (!file)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Bonk: cannot write hero stats to %s", temporary.c_str());
            return;
        }
        file << lua;
    }
    std::remove(path.c_str());
    if (std::rename(temporary.c_str(), path.c_str()) != 0)
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Bonk: cannot replace hero stats file %s", path.c_str());
}

bool BonkPlayer::IsWipeName(std::string const& name)
{
    return name == "Wipeaccount";
}

bool BonkPlayer::IsPlayableClass(uint8 classId)
{
    return classId == CLASS_WARRIOR || classId == CLASS_HUNTER || classId == CLASS_MAGE;
}

void BonkPlayer::WipeAccount(uint32 accountId)
{
    for (char const* table : { "bonk_account", "bonk_account_mastery", "bonk_account_mastery_node", "bonk_account_quest",
        "bonk_account_starter", "bonk_account_talent", "bonk_account_tier", "bonk_account_upgrade" })
        CharacterDatabase.PExecute("DELETE FROM `%s` WHERE `account` = %u", table, accountId);

    WriteHeroScreenFile("BONK_STATS = {\n\tgold = 0,\n\tlastHero = \"\",\n\theroes = {\n\t},\n};\n");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bonk: account %u wiped from the hero screen", accountId);
}

void BonkPlayer::OnSave()
{
    SaveGold();
    sBonkQuests.Save(m_player->GetSession()->GetAccountId());
}

void BonkPlayer::SaveGold() const
{
    if (!m_goldLoaded)
        return;

    CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `gold`) VALUES (%u, %u) ON DUPLICATE KEY UPDATE `gold` = VALUES(`gold`)",
        m_player->GetSession()->GetAccountId(), m_player->GetMoney());
}

uint8 BonkPlayer::GetUpgradeRank(uint32 upgradeId) const
{
    auto itr = m_upgradeRanks.find(upgradeId);
    return itr != m_upgradeRanks.end() ? itr->second : 0;
}

float BonkPlayer::GetUpgradeUtility(BonkUpgradeUtility utility) const
{
    float total = 0.0f;
    for (auto const& itr : m_upgradeRanks)
        if (BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(itr.first))
            if (upgrade->utility == int8(utility))
                total += upgrade->valuePerRank * std::min(itr.second, upgrade->maxRank);
    return total;
}

uint8 BonkPlayer::GetTalentRank(uint32 talentId) const
{
    auto itr = m_talentRanks.find(talentId);
    return itr != m_talentRanks.end() ? itr->second : 0;
}

bool BonkPlayer::Spend(uint32 copper, std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    if (m_player->GetMoney() < copper)
    {
        message = "You need " + FormatBonkMoney(copper) + ".";
        return false;
    }

    m_player->ModifyMoney(-int32(copper));
    SaveGold();
    return true;
}

bool BonkPlayer::SpendShards(uint32 count, std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    if (m_shards < count)
    {
        message = "You need " + FormatBonkShards(count) + ". You have " + std::to_string(m_shards) + ".";
        return false;
    }

    m_shards -= count;
    SaveShards();
    SendWallet();
    return true;
}

void BonkPlayer::AddShards(uint32 count)
{
    m_shards += count;
    SaveShards();
    SendWallet();
}

void BonkPlayer::SendWallet() const
{
    SendAddon("wallet~" + std::to_string(m_shards));
}

void BonkPlayer::SaveShards() const
{
    CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `shards`) VALUES (%u, %u) ON DUPLICATE KEY UPDATE `shards` = VALUES(`shards`)",
        m_player->GetSession()->GetAccountId(), m_shards);
}

bool BonkPlayer::BuyUpgrade(uint32 upgradeId, std::string& message)
{
    BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(upgradeId);
    if (!upgrade)
        return false;

    uint8 const rank = GetUpgradeRank(upgradeId);
    if (rank >= upgrade->maxRank)
    {
        message = upgrade->name + " is already at its highest rank.";
        return false;
    }

    if (BonkQuest const* quest = GetLockingQuest(m_player, BONK_REWARD_UPGRADE, upgradeId))
    {
        message = "Finish the quest \"" + quest->name + "\" to unlock " + upgrade->name + ".";
        return false;
    }

    BonkUpgrade const* required = sBonkMgr.GetUpgrade(upgrade->layout.requiredId);
    if (required && GetUpgradeRank(required->id) < upgrade->layout.requiredRank)
    {
        message = RequirementText(required->name, upgrade->layout.requiredRank, required->maxRank);
        return false;
    }

    if (!Spend(upgrade->GetCost(rank), message))
        return false;

    m_upgradeRanks[upgradeId] = rank + 1;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_upgrade` (`account`, `upgrade`, `rank`) VALUES (%u, %u, %u)",
        m_player->GetSession()->GetAccountId(), upgradeId, uint32(rank + 1));
    message = upgrade->name + " is now rank " + std::to_string(rank + 1) + ".";
    return true;
}

bool BonkPlayer::BuyUnlock(BonkQuestReward type, uint32 id, std::string& message)
{
    std::string name;
    switch (type)
    {
        case BONK_REWARD_WEAPON:
            if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(id))
                name = weapon->name;
            break;
        case BONK_REWARD_PASSIVE:
            if (BonkPassive const* passive = sBonkMgr.GetPassive(id))
                name = passive->name;
            break;
        case BONK_REWARD_CARD:
            if (BonkCard const* card = sBonkMgr.GetCard(id))
                name = card->name;
            break;
        case BONK_REWARD_GLYPH:
            if (BonkGlyph const* glyph = sBonkMgr.GetGlyph(id))
                name = glyph->name;
            break;
        default:
            break;
    }
    if (name.empty())
    {
        message = "You can't unlock that.";
        return false;
    }
    if (type == BONK_REWARD_CARD && !IsVisitingShop(BONK_SHOP_TALENTS))
    {
        message = "Visit Sayge at camp to unlock Darkmoon Cards.";
        return false;
    }
    if (type != BONK_REWARD_CARD && !IsAtSpellbook())
    {
        message = "Visit the Spellbook Keeper at camp to unlock " + name + ".";
        return false;
    }

    uint32 const account = m_player->GetSession()->GetAccountId();
    if (!sBonkQuests.IsForSale(account, type, id))
    {
        if (!sBonkQuests.IsLocked(account, type, id))
            message = "You already have " + name + ".";
        else if (BonkQuest const* quest = sBonkQuests.GetUnlockingQuest(type, id))
            message = "Finish the quest \"" + quest->name + "\" to unlock " + name + ".";
        return false;
    }

    uint32 const price = sBonkQuests.GetUnlockPrice(account, type, id);
    bool const paid = BonkQuests::IsTicketUnlock(type) ? SpendShards(price, message) : Spend(price, message);
    if (!paid)
        return false;
    sBonkQuests.Buy(account, type, id);
    message = name + " unlocked!";
    if (BonkQuests::IsTicketUnlock(type))
        SendShop(false);
    return true;
}

bool BonkPlayer::BuyTalent(uint32 talentId, std::string& message)
{
    BonkTalent const* talent = sBonkMgr.GetTalent(talentId);
    if (!talent)
        return false;

    uint8 const rank = GetTalentRank(talentId);
    if (rank >= talent->maxRank)
    {
        message = talent->name + " is already learned.";
        return false;
    }

    if (BonkQuest const* quest = GetLockingQuest(m_player, BONK_REWARD_TALENT, talentId))
    {
        message = "Finish the quest \"" + quest->name + "\" to unlock " + talent->name + ".";
        return false;
    }

    BonkTalent const* required = sBonkMgr.GetTalent(talent->layout.requiredId);
    if (required && GetTalentRank(required->id) < talent->layout.requiredRank)
    {
        message = RequirementText(required->name, talent->layout.requiredRank, required->maxRank);
        return false;
    }

    if (rank < talent->rankQuests.size())
    {
        uint32 const needed = talent->rankQuests[rank];
        uint32 const done = sBonkQuests.GetCompletedCount(m_player->GetSession()->GetAccountId());
        if (done < needed)
        {
            message = "You must complete " + std::to_string(needed) + " quests to buy rank " + std::to_string(rank + 1) + " of " + talent->name +
                ". You have completed " + std::to_string(done) + ".";
            return false;
        }
    }

    if (!SpendShards(talent->GetCost(rank), message))
        return false;

    m_talentRanks[talentId] = rank + 1;
    if (talent->IsToggle())
        m_enabledTalents.insert(talentId);
    SaveTalent(talentId);
    message = talent->IsToggle() ? "You learned " + talent->name + " and switched it on." : talent->name + " is now rank " + std::to_string(rank + 1) + ".";
    return true;
}

bool BonkPlayer::ResetUpgrades(std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    if (m_upgradeRanks.empty())
    {
        message = "You have not bought any upgrades.";
        return false;
    }

    uint32 refunded = 0;
    for (auto const& itr : m_upgradeRanks)
        if (BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(itr.first))
            for (uint8 rank = 0; rank < itr.second; ++rank)
                refunded += upgrade->GetCost(rank);

    m_upgradeRanks.clear();
    CharacterDatabase.PExecute("DELETE FROM `bonk_account_upgrade` WHERE `account` = %u", m_player->GetSession()->GetAccountId());
    m_player->ModifyMoney(int32(refunded));
    SaveGold();
    message = "You refunded all your upgrades and got " + FormatBonkMoney(refunded) + " back.";
    return true;
}

bool BonkPlayer::ResetTalents(std::string& message)
{
    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }
    if (m_talentRanks.empty())
    {
        message = "You have not bought any fortunes.";
        return false;
    }

    uint32 refunded = 0;
    for (auto const& itr : m_talentRanks)
        if (BonkTalent const* talent = sBonkMgr.GetTalent(itr.first))
            for (uint8 rank = 0; rank < itr.second; ++rank)
                refunded += talent->GetCost(rank);

    m_talentRanks.clear();
    m_enabledTalents.clear();
    CharacterDatabase.PExecute("DELETE FROM `bonk_account_talent` WHERE `account` = %u", m_player->GetSession()->GetAccountId());
    AddShards(refunded);
    message = "You refunded all your fortunes and got " + FormatBonkShards(refunded) + " back.";
    return true;
}

bool BonkPlayer::RefundUpgrade(uint32 upgradeId, std::string& message)
{
    BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(upgradeId);
    if (!upgrade)
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const rank = GetUpgradeRank(upgradeId);
    if (!rank)
    {
        message = "You have not bought " + upgrade->name + ".";
        return false;
    }

    for (auto const& itr : m_upgradeRanks)
        if (BonkUpgrade const* other = sBonkMgr.GetUpgrade(itr.first))
            if (itr.second && other->layout.requiredId == upgradeId && rank - 1 < other->layout.requiredRank)
            {
                message = "Refund " + other->name + " first, because it requires this one.";
                return false;
            }

    uint32 const refunded = upgrade->GetCost(rank - 1);
    struct TrimAfter
    {
        BonkPlayer const* bonk;
        ~TrimAfter() { bonk->TrimPoolToggles(); }
    } const trimAfter{ this };
    if (rank > 1)
    {
        m_upgradeRanks[upgradeId] = rank - 1;
        CharacterDatabase.PExecute("REPLACE INTO `bonk_account_upgrade` (`account`, `upgrade`, `rank`) VALUES (%u, %u, %u)",
            m_player->GetSession()->GetAccountId(), upgradeId, uint32(rank - 1));
    }
    else
    {
        m_upgradeRanks.erase(upgradeId);
        CharacterDatabase.PExecute("DELETE FROM `bonk_account_upgrade` WHERE `account` = %u AND `upgrade` = %u",
            m_player->GetSession()->GetAccountId(), upgradeId);
    }
    m_player->ModifyMoney(int32(refunded));
    SaveGold();
    message = "You refunded " + (upgrade->maxRank > 1 ? "rank " + std::to_string(rank) + " of " : std::string()) + upgrade->name + " and got " +
        FormatBonkMoney(refunded) + " back.";
    return true;
}

bool BonkPlayer::RefundTalent(uint32 talentId, std::string& message)
{
    BonkTalent const* talent = sBonkMgr.GetTalent(talentId);
    if (!talent)
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    uint8 const rank = GetTalentRank(talentId);
    if (!rank)
    {
        message = "You have not bought " + talent->name + ".";
        return false;
    }

    for (auto const& itr : m_talentRanks)
        if (BonkTalent const* other = sBonkMgr.GetTalent(itr.first))
            if (itr.second && other->layout.requiredId == talentId && rank - 1 < other->layout.requiredRank)
            {
                message = "Refund " + other->name + " first, because it requires this one.";
                return false;
            }

    uint32 const refunded = talent->GetCost(rank - 1);
    if (rank > 1)
    {
        m_talentRanks[talentId] = rank - 1;
        SaveTalent(talentId);
    }
    else
    {
        m_talentRanks.erase(talentId);
        m_enabledTalents.erase(talentId);
        CharacterDatabase.PExecute("DELETE FROM `bonk_account_talent` WHERE `account` = %u AND `talent` = %u",
            m_player->GetSession()->GetAccountId(), talentId);
    }
    AddShards(refunded);
    message = "You refunded " + (talent->maxRank > 1 ? "rank " + std::to_string(rank) + " of " : std::string()) + talent->name + " and got " +
        FormatBonkShards(refunded) + " back.";
    return true;
}

bool BonkPlayer::ToggleTalent(uint32 talentId, std::string& message)
{
    BonkTalent const* talent = sBonkMgr.GetTalent(talentId);
    if (!talent || !talent->IsToggle() || !GetTalentRank(talentId))
        return false;

    if (m_run)
    {
        message = "You can't do that during a run.";
        return false;
    }

    bool const enable = !IsTalentEnabled(talentId);
    if (enable)
        m_enabledTalents.insert(talentId);
    else
        m_enabledTalents.erase(talentId);
    SaveTalent(talentId);
    message = talent->name + (enable ? " is now on." : " is now off.");
    return true;
}

void BonkPlayer::SaveTalent(uint32 talentId) const
{
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_talent` (`account`, `talent`, `rank`, `enabled`) VALUES (%u, %u, %u, %u)",
        m_player->GetSession()->GetAccountId(), talentId, uint32(GetTalentRank(talentId)), IsTalentEnabled(talentId) ? 1u : 0u);
}

void BonkPlayer::OpenShop(BonkShop shop, Creature const* npc)
{
    m_shop = shop;
    m_shopNpc = npc->GetObjectGuid();
    m_shopNpcName = npc->GetName();
    SendShop(true);
}

void BonkPlayer::VisitSpellbook(Creature const* keeper)
{
    m_spellbookKeeper = keeper->GetObjectGuid();
}

bool BonkPlayer::IsAtSpellbook() const
{
    if (m_spellbookKeeper.IsEmpty() || !m_player->IsInWorld())
        return false;

    Creature const* keeper = m_player->GetMap()->GetCreature(m_spellbookKeeper);
    return keeper && keeper->IsAlive() && m_player->IsWithinDistInMap(keeper, BONK_SHOP_RANGE);
}

bool BonkPlayer::IsAtShop() const
{
    if (m_shop == BONK_SHOP_NONE || !m_player->IsInWorld())
        return false;

    Creature const* npc = m_player->GetMap()->GetCreature(m_shopNpc);
    return npc && npc->IsAlive() && m_player->IsWithinDistInMap(npc, BONK_SHOP_RANGE);
}

void BonkPlayer::ShopLearn(uint32 id)
{
    if (!IsAtShop())
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    if (m_shop == BONK_SHOP_UPGRADES)
        BuyUpgrade(id, message);
    else if (m_shop == BONK_SHOP_MASTERY)
        LearnMastery(id, message);
    else
        BuyTalent(id, message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
    SendStats();
}

void BonkPlayer::ShopUnlearn(uint32 id)
{
    if (!IsAtShop())
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    if (m_shop == BONK_SHOP_UPGRADES)
        RefundUpgrade(id, message);
    else if (m_shop == BONK_SHOP_MASTERY)
        RefundMastery(id, message);
    else
        RefundTalent(id, message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
    SendStats();
}

void BonkPlayer::ShopToggle(uint32 id)
{
    if (!IsAtShop() || m_shop != BONK_SHOP_TALENTS)
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    ToggleTalent(id, message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
}

void BonkPlayer::ShopRespec()
{
    if (!IsAtShop())
    {
        Notify("You are too far away from " + (m_shopNpcName.empty() ? std::string("the shop") : m_shopNpcName) + ".");
        SendAddon("shopclose");
        return;
    }

    std::string message;
    if (m_shop == BONK_SHOP_UPGRADES)
        ResetUpgrades(message);
    else if (m_shop == BONK_SHOP_MASTERY)
        ResetMastery(message);
    else
        ResetTalents(message);
    if (!message.empty())
        Notify(message);
    SendShop(false);
    SendStats();
}

void BonkPlayer::SendShop(bool open) const
{
    if (m_shop == BONK_SHOP_MASTERY)
    {
        uint8 const classId = m_player->GetClass();
        uint32 const level = GetMasteryLevel();
        uint32 const earned = GetMasteryPoints();
        uint32 const spent = GetMasterySpent();
        SendAddon("shop~" + std::to_string(uint32(m_shop)) + "~" + (open ? "1" : "0") + "~" + m_shopNpcName + "~points~" +
            std::to_string(earned > spent ? earned - spent : 0) + "~" + std::to_string(level) + "~" +
            std::to_string(BonkMastery::GetXpIntoLevel(m_masteryXp)) + "~" +
            std::to_string(level < BONK_MASTERY_MAX_LEVEL ? BonkMastery::GetXpForLevel(level + 1) : 0) + "~" +
            std::to_string(BONK_MASTERY_MAX_LEVEL) + "~" + std::to_string(earned) + "~" + std::to_string(earned - level) + "~" +
            GetClassName() + "~" + std::to_string(BONK_MASTERY_POINTS_PER_ROW));

        for (BonkMasteryTree const* tree : sBonkMastery.GetTrees(classId))
            SendAddon("tree~" + std::to_string(tree->id) + "~" + tree->name + "~" + tree->background);

        for (auto const& itr : sBonkMastery.GetNodes())
        {
            BonkMasteryNode const& node = itr.second;
            if (node.classId != classId)
                continue;
            uint8 const rank = GetMasteryRank(node.id);
            std::ostringstream line;
            line << "node~" << node.tree << '~' << node.id << '~' << uint32(node.row) << '~' << uint32(node.column) << '~' << node.icon
                 << '~' << uint32(rank) << '~' << uint32(node.maxRank) << '~' << (rank < node.maxRank ? 1 : 0)
                 << '~' << node.requiredId << '~' << uint32(node.requiredRank) << "~0~0~" << node.name << '~' << node.description << "~0~~" << FormatStat(node.valuePerRank) << '~' << (rank ? 1 : 0);
            SendAddon(line.str());
        }

        SendAddon("shopend");
        return;
    }

    bool const shards = m_shop == BONK_SHOP_TALENTS;
    SendAddon("shop~" + std::to_string(uint32(m_shop)) + "~" + (open ? "1" : "0") + "~" + m_shopNpcName + "~" +
        (shards ? "shards~" + std::to_string(m_shards) : "gold~" + std::to_string(m_player->GetMoney())));

    for (auto const& itr : sBonkMgr.GetShopTrees())
        if (itr.second.shop == m_shop)
            SendAddon("tree~" + std::to_string(itr.first) + "~" + itr.second.name + "~" + itr.second.background);

    BonkQuestReward const lockType = m_shop == BONK_SHOP_UPGRADES ? BONK_REWARD_UPGRADE : BONK_REWARD_TALENT;
    auto sendNode = [this, lockType](uint32 id, BonkShopNode const& layout, uint8 rank, uint8 maxRank, uint32 nextCost, uint32 refund, bool toggle,
        bool enabled, std::string const& name, std::string const& description, float valuePerRank)
    {
        if (!layout.tree)
            return;

        BonkQuest const* lock = GetLockingQuest(m_player, lockType, id);

        rank = std::min(rank, maxRank);
        std::ostringstream line;
        line << "node~" << layout.tree << '~' << id << '~' << uint32(layout.row) << '~' << uint32(layout.column) << '~' << layout.icon
             << '~' << uint32(rank) << '~' << uint32(maxRank) << '~' << (rank < maxRank ? nextCost : 0)
             << '~' << layout.requiredId << '~' << uint32(layout.requiredRank) << '~' << (toggle ? 1 : 0) << '~' << (enabled ? 1 : 0)
             << '~' << name << '~' << description << '~' << (lock ? 1 : 0) << '~' << (lock ? lock->name : "") << '~' << FormatStat(valuePerRank)
             << '~' << (rank ? refund : 0) << '~' << (lock ? lock->description : "");
        SendAddon(line.str());
    };

    if (m_shop == BONK_SHOP_UPGRADES)
    {
        for (auto const& itr : sBonkMgr.GetUpgrades())
        {
            BonkUpgrade const& upgrade = itr.second;
            uint8 const rank = GetUpgradeRank(upgrade.id);
            sendNode(upgrade.id, upgrade.layout, rank, upgrade.maxRank, upgrade.GetCost(rank), rank ? upgrade.GetCost(rank - 1) : 0, false, false,
                upgrade.name, upgrade.description, upgrade.valuePerRank);
        }
    }
    else if (m_shop == BONK_SHOP_TALENTS)
    {
        for (auto const& itr : sBonkMgr.GetTalents())
        {
            BonkTalent const& talent = itr.second;
            uint8 const rank = GetTalentRank(talent.id);
            std::string description = talent.description;
            if (rank < talent.maxRank && rank < talent.rankQuests.size() &&
                sBonkQuests.GetCompletedCount(m_player->GetSession()->GetAccountId()) < talent.rankQuests[rank])
                description += " The next rank unlocks after you complete " + std::to_string(talent.rankQuests[rank]) + " quests.";
            sendNode(talent.id, talent.layout, rank, talent.maxRank, talent.GetCost(rank), rank ? talent.GetCost(rank - 1) : 0, talent.IsToggle(),
                IsTalentEnabled(talent.id), talent.name, description, talent.valuePerRank);
        }
    }

    SendAddon("shopend");
}

BonkRunBonuses BonkPlayer::GetRunBonuses() const
{
    BonkRunBonuses bonuses;

    for (auto const& itr : m_upgradeRanks)
        if (BonkUpgrade const* upgrade = sBonkMgr.GetUpgrade(itr.first))
        {
            float const value = upgrade->valuePerRank * std::min(itr.second, upgrade->maxRank);
            if (upgrade->utility >= 0)
                bonuses.utility[upgrade->utility] += value;
            else
                bonuses.passives[upgrade->effect] += value;
        }

    BonkMasteryBonuses const mastery = GetMasteryBonuses();
    for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
        bonuses.passives[effect] += mastery.passives[effect];

    for (auto const& itr : m_talentRanks)
    {
        BonkTalent const* talent = sBonkMgr.GetTalent(itr.first);
        if (!talent || !itr.second)
            continue;

        float const value = talent->valuePerRank * std::min(itr.second, talent->maxRank);
        if (talent->IsToggle())
        {
            if (!IsTalentEnabled(talent->id))
                continue;
            bonuses.goldBonus += talent->goldBonus;
        }

        switch (talent->effect)
        {
            case BONK_TALENT_ENEMY_HEALTH: bonuses.enemyHealth += value; break;
            case BONK_TALENT_ENEMY_DAMAGE: bonuses.enemyDamage += value; break;
            case BONK_TALENT_SPAWN_RATE:   bonuses.spawnRate += value; break;
            case BONK_TALENT_ELITE_RATE:   bonuses.eliteRate += value; break;
            case BONK_TALENT_REROLL:       bonuses.rerolls += uint32(value); break;
            case BONK_TALENT_BANISH:       bonuses.banishes += uint32(value); break;
            case BONK_TALENT_SKIP:         bonuses.skips += uint32(value); break;
            case BONK_TALENT_REVIVE:       bonuses.revives += uint32(value); break;
            case BONK_TALENT_START_COINS:  bonuses.startCoins += uint32(value); break;
            case BONK_TALENT_SPELL_SLOT:   bonuses.spellSlots += uint32(value); break;
            case BONK_TALENT_PASSIVE_SLOT: bonuses.passiveSlots += uint32(value); break;
            case BONK_TALENT_SECOND_SIGHT: bonuses.secondSight = bonuses.secondSight || value > 0.0f; break;
            case BONK_TALENT_EXPERIENCE:   bonuses.passives[BONK_PASSIVE_GROWTH] += value; break;
            case BONK_TALENT_PREMONITION:  bonuses.earlierEvolutions += uint32(value); break;
            case BONK_TALENT_CURSED_GROUND: bonuses.healingCut += value; break;
            case BONK_TALENT_DOOM_CLOCK:   bonuses.earlierBossSeconds += uint32(value); break;
            case BONK_TALENT_ELITE_AFFIX:  bonuses.eliteAffixes += uint32(value); break;
            default: break;
        }
    }

    return bonuses;
}

void BonkPlayer::PreviewTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs)
{
    m_previewTelegraphs.SetPatient(true);
    m_previewTelegraphs.Warm();
    m_previewTelegraphs.Add(m_player, shape, x, y, z, orientation, size, durationMs);
}

void BonkPlayer::Update(uint32 diff)
{
    if (m_campCheckPending && !m_player->IsBeingTeleported())
    {
        m_campCheckPending = false;
        if (!m_run && !m_player->IsGameMaster() && !IsAtCamp())
            TeleportToCamp();
    }

    m_previewTelegraphs.Update(m_player, diff);
    UpdateCampSeat(diff);
    UpdateDash(diff);
    UpdateCampMailNotice(diff);
    UpdateMailboxGlow(diff);

    if (m_run)
    {
        m_run->Update(diff);
        if (m_run->IsFinished())
        {
            m_run.reset();
            SendStats();
            SendDash();
        }
        else
            BonkStats::UpdateBossBar(m_player, diff);
    }

    if (m_player->GetPowerType() == POWER_MANA)
    {
        uint32 const maxMana = m_player->GetMaxPower(POWER_MANA);
        if (m_player->GetPower(POWER_MANA) < maxMana)
            m_player->SetPower(POWER_MANA, maxMana);
    }
}

bool BonkPlayer::IsTierCleared(BonkTier const& tier) const
{
    return tier.stages && GetBestStage(tier.id) > tier.stages;
}

bool BonkPlayer::IsSlotUnusedByClass(uint8 slot) const
{
    uint8 const playerClass = m_player->GetClass();
    switch (slot)
    {
        case EQUIPMENT_SLOT_MAINHAND: return playerClass == CLASS_HUNTER;
        case EQUIPMENT_SLOT_OFFHAND:  return playerClass == CLASS_HUNTER || playerClass == CLASS_DRUID;
        case EQUIPMENT_SLOT_RANGED:   return playerClass != CLASS_HUNTER;
        default:                      return false;
    }
}

bool BonkPlayer::CanBuyGear(uint32 itemEntry) const
{
    BonkItemStat const* stat = sBonkMgr.GetItemStat(itemEntry);
    if (!stat || !stat->unlockTier)
        return true;
    BonkTier const* tier = sBonkMgr.GetTier(stat->unlockTier);
    return tier && IsTierCleared(*tier);
}

namespace
{
    uint32 GetGearGroup(uint32 itemEntry)
    {
        BonkItemStat const* stat = sBonkMgr.GetItemStat(itemEntry);
        return stat ? stat->unlockTier : 0;
    }

    BonkTier const* GetGearGroupTier(uint32 group, size_t* order = nullptr)
    {
        std::vector<BonkTier const*> const tiers = BonkPlayer::GetTiersInOrder();
        for (size_t i = 0; i < tiers.size(); ++i)
            if (tiers[i]->unlockTierId == group)
            {
                if (order)
                    *order = i;
                return tiers[i];
            }
        if (order)
            *order = tiers.size();
        return nullptr;
    }
}

std::vector<uint32> BonkPlayer::GetGearTabs(Creature const* vendor) const
{
    std::vector<uint32> tabs;
    VendorItemData const* items = vendor->GetVendorItems();
    if (!items)
        return tabs;
    for (uint32 i = 0; i < items->GetItemCount(); ++i)
    {
        VendorItem const* item = items->GetItem(i);
        if (!item || !CanBuyGear(item->item))
            continue;
        uint32 const group = GetGearGroup(item->item);
        if (std::find(tabs.begin(), tabs.end(), group) == tabs.end())
            tabs.push_back(group);
    }
    std::sort(tabs.begin(), tabs.end(), [](uint32 a, uint32 b)
    {
        size_t orderA, orderB;
        GetGearGroupTier(a, &orderA);
        GetGearGroupTier(b, &orderB);
        return orderA != orderB ? orderA < orderB : a < b;
    });
    return tabs;
}

uint32 BonkPlayer::GetShownGearTab(std::vector<uint32> const& tabs) const
{
    if (std::find(tabs.begin(), tabs.end(), m_gearTab) != tabs.end())
        return m_gearTab;
    return tabs.empty() ? 0 : tabs.front();
}

bool BonkPlayer::ShowsGearItem(Creature const* vendor, uint32 itemEntry) const
{
    if (vendor->GetEntry() != BONK_GEAR_VENDOR)
        return true;
    std::vector<uint32> const tabs = GetGearTabs(vendor);
    return tabs.size() < 2 || GetGearGroup(itemEntry) == GetShownGearTab(tabs);
}

void BonkPlayer::SendGearTabs(Creature const* vendor) const
{
    if (vendor->GetEntry() != BONK_GEAR_VENDOR)
        return;
    std::vector<uint32> const tabs = GetGearTabs(vendor);
    std::string line = "geartabs~" + std::to_string(GetShownGearTab(tabs));
    if (tabs.size() >= 2)
        for (uint32 group : tabs)
        {
            BonkTier const* tier = GetGearGroupTier(group);
            line += "~" + std::to_string(group) + "~" + (tier ? tier->name : std::string("Gear"));
        }
    SendAddon(line);
}

void BonkPlayer::SelectGearTab(uint32 group)
{
    Creature* vendor = m_player->FindNearestCreature(BONK_GEAR_VENDOR, BONK_GEAR_VENDOR_RANGE);
    if (!vendor)
        return;
    std::vector<uint32> const tabs = GetGearTabs(vendor);
    if (std::find(tabs.begin(), tabs.end(), group) == tabs.end())
        return;
    m_gearTab = group;
    m_player->GetSession()->SendListInventory(vendor->GetObjectGuid());
}

bool BonkPlayer::StartRun(BonkTier const& tier, std::string& error, BonkMap const* dungeon)
{
    if (m_run)
    {
        error = "You are already in a run. Use .bonk stop to leave it.";
        return false;
    }

    if (!IsTierUnlocked(tier))
    {
        error = tier.name + " is locked. " + GetUnlockHint(tier) + ".";
        return false;
    }

    if (dungeon && !IsTierCleared(tier))
    {
        error = "Clear " + tier.name + " first to unlock its dungeons.";
        return false;
    }

    if (m_player->IsInCombat())
    {
        error = "You can't start a run while in combat.";
        return false;
    }

    if (m_player->IsTaxiFlying())
    {
        error = "You can't start a run while flying.";
        return false;
    }

    if (!IsPlayableClass(m_player->GetClass()) || !sBonkMgr.HasStarterSpell(m_player->GetClassMask()))
    {
        error = "The " + GetClassName() + " is being reworked and can't start a run yet.";
        return false;
    }

    TrimPoolToggles();
    m_run.reset(new BonkRun(m_player, tier, dungeon));
    if (!m_run->Begin(error))
    {
        m_run.reset();
        return false;
    }
    return true;
}

void BonkPlayer::StopRun()
{
    if (m_run)
        m_run->Surrender();
}

std::vector<BonkTier const*> BonkPlayer::GetTiersInOrder()
{
    std::map<uint32, BonkTier> const& tiers = sBonkMgr.GetTiers();
    std::vector<BonkTier const*> ordered;
    std::set<uint32> placed;

    for (bool added = true; added;)
    {
        added = false;
        for (auto const& itr : tiers)
        {
            BonkTier const& tier = itr.second;
            if (placed.count(tier.id) || (tier.unlockTierId && !placed.count(tier.unlockTierId)))
                continue;
            ordered.push_back(&tier);
            placed.insert(tier.id);
            added = true;
        }
    }

    for (auto const& itr : tiers)
        if (!placed.count(itr.first))
            ordered.push_back(&itr.second);
    return ordered;
}

uint32 BonkPlayer::GetBestStage(uint32 tierId) const
{
    auto itr = m_bestStages.find(tierId);
    return itr != m_bestStages.end() ? itr->second : 0;
}

bool BonkPlayer::IsTierUnlocked(BonkTier const& tier) const
{
    return !tier.unlockTierId || GetBestStage(tier.unlockTierId) >= tier.unlockStage;
}

std::string BonkPlayer::GetUnlockHint(BonkTier const& tier) const
{
    BonkTier const* required = sBonkMgr.GetTier(tier.unlockTierId);
    if (!required)
        return "";
    if (required->stages && tier.unlockStage > required->stages)
        return "Clear " + required->name;
    return "Reach stage " + std::to_string(tier.unlockStage) + " on " + required->name;
}

void BonkPlayer::RecordStage(uint32 tierId, uint32 stage)
{
    sBonkQuests.OnStage(m_player, tierId, stage);

    if (stage <= GetBestStage(tierId))
        return;

    std::vector<BonkTier const*> locked;
    for (auto const& itr : sBonkMgr.GetTiers())
        if (!IsTierUnlocked(itr.second))
            locked.push_back(&itr.second);

    m_bestStages[tierId] = stage;
    CharacterDatabase.PExecute("REPLACE INTO `bonk_account_tier` (`account`, `tier`, `best_stage`) VALUES (%u, %u, %u)",
        m_player->GetSession()->GetAccountId(), tierId, stage);

    for (BonkTier const* tier : locked)
        if (IsTierUnlocked(*tier))
            Notify("You unlocked " + tier->name + " difficulty!");
}

void BonkPlayer::SendAddon(std::string const& line) const
{
    if (!m_addonReady)
        return;

    ChatHandler(m_player).SendSysMessage(("BONK~" + line).c_str());
}

uint32 BonkPlayer::GetPoolToggleSlots() const
{
    return uint32(GetUpgradeUtility(BONK_UTILITY_POOL_TOGGLES));
}

void BonkPlayer::TrimPoolToggles() const
{
    sBonkQuests.TrimDisabled(m_player->GetSession()->GetAccountId(), GetPoolToggleSlots());
}

void BonkPlayer::SendGlyphInfo() const
{
    for (auto const& itr : sBonkMgr.GetGlyphs())
    {
        BonkGlyph const& glyph = itr.second;
        SendAddon("glyphinfo~" + std::to_string(glyph.id) + "~" + glyph.name + "~" + glyph.icon + "~" + std::to_string(uint32(glyph.rarity)) + "~" +
            glyph.description + "~" + glyph.drawback);
    }
}

void BonkPlayer::Notify(std::string const& text) const
{
    if (m_addonReady)
        SendAddon("msg~" + text);
    else
        ChatHandler(m_player).SendSysMessage(text.c_str());
}

void BonkPlayer::SendGearInfo() const
{
    for (auto const& itr : sBonkMgr.GetItemStats())
        SendAddon("gear~" + std::to_string(itr.first) + "~" + std::to_string(itr.second.setId) + "~" + FormatStatList(itr.second.stats));

    for (auto const& itr : sBonkMgr.GetItemSets())
    {
        BonkItemSet const& set = itr.second;
        std::string pieces;
        for (uint32 entry : set.items)
            if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry))
                pieces += (pieces.empty() ? "" : ",") + std::to_string(entry) + ":" + proto->Name1;
        SendAddon("set~" + std::to_string(set.id) + "~" + set.name + "~" + pieces);

        for (BonkItemSetBonus const& bonus : set.bonuses)
            SendAddon("setbonus~" + std::to_string(set.id) + "~" + std::to_string(bonus.pieces) + "~" + FormatStatList(bonus.stats) + "~" +
                bonus.description);
    }

    SendAddon("gearend");
    SendStats();
}

void BonkPlayer::SendStats() const
{
    if (m_run)
    {
        m_run->SendStats();
        return;
    }

    BonkRunBonuses const bonuses = GetRunBonuses();
    SendStatTotals(bonuses.passives, false);
}

void BonkPlayer::SendStatTotals(float const* totals, bool inRun) const
{
    std::string line = "stats~" + std::string(inRun ? "1" : "0") + "~";
    for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
    {
        float value = totals[effect];
        if (effect == BONK_PASSIVE_CRIT)
            value += BONK_BASE_CRIT;
        line += (effect ? "," : "") + FormatStat(value);
    }
    SendAddon(line);
}

void BonkPlayer::SendTierList() const
{
    SendArena("");
}

void BonkPlayer::OpenArena(Creature const* npc) const
{
    SendArena(npc->GetName());
}

void BonkPlayer::SendArena(std::string const& npc) const
{
    if (m_addonReady)
    {
        BonkRunBonuses const bonuses = GetRunBonuses();

        std::string challenges;
        for (auto const& itr : sBonkMgr.GetTalents())
            if (itr.second.IsToggle() && GetTalentRank(itr.first) && IsTalentEnabled(itr.first))
                challenges += (challenges.empty() ? "" : ", ") + itr.second.name;

        std::vector<std::string> perks;
        if (bonuses.rerolls)
            perks.push_back(std::to_string(bonuses.rerolls) + (bonuses.rerolls == 1 ? " reroll" : " rerolls"));
        if (bonuses.banishes)
            perks.push_back(std::to_string(bonuses.banishes) + (bonuses.banishes == 1 ? " banish" : " banishes"));
        if (bonuses.revives)
            perks.push_back(std::to_string(bonuses.revives) + (bonuses.revives == 1 ? " revive" : " revives"));
        if (bonuses.startCoins)
            perks.push_back("+" + std::to_string(bonuses.startCoins) + " Valor");
        std::string perkText;
        for (std::string const& perk : perks)
            perkText += (perkText.empty() ? "" : ", ") + perk;

        SendAddon("arena~" + std::string(npc.empty() ? "0" : "1") + "~" + npc + "~" + std::to_string(uint32(bonuses.goldBonus + 0.5f)) + "~" +
            challenges + "~" + perkText);
    }

    for (BonkTier const* tierPtr : GetTiersInOrder())
    {
        BonkTier const& tier = *tierPtr;
        bool const unlocked = IsTierUnlocked(tier);
        uint32 const best = GetBestStage(tier.id);
        if (m_addonReady)
        {
            std::string maps;
            for (uint32 mapId : tier.mapIds)
                if (BonkMap const* map = sBonkMgr.GetMap(mapId))
                    maps += (maps.empty() ? "" : ",") + map->icon + ":" + map->name + ":" + std::to_string(uint32(map->type)) + ":" +
                        std::to_string(GetBonkMapRewardPercent(map->type));
            std::string dungeons;
            bool const cleared = IsTierCleared(tier);
            for (BonkMap const* dungeon : sBonkMgr.GetDungeons())
                dungeons += (dungeons.empty() ? "" : ",") + dungeon->key + ":" + dungeon->icon + ":" + dungeon->name + ":" + (cleared ? "1" : "0");
            SendAddon("tier~" + std::to_string(tier.id) + "~" + tier.key + "~" + tier.name + "~" + (unlocked ? "1" : "0") + "~" +
                (unlocked ? "" : GetUnlockHint(tier)) + "~" + std::to_string(best) + "~" + tier.description + "~" + tier.art + "~" + maps + "~" +
                std::to_string(tier.stages) + "~" + (tier.endless ? "1" : "0") + "~" + dungeons);
        }
        else if (unlocked)
            ChatHandler(m_player).PSendSysMessage("  %s (best: stage %u): .bonk start %s", tier.name.c_str(), best, tier.key.c_str());
        else
        {
            std::string hint = GetUnlockHint(tier);
            if (!hint.empty())
                hint[0] = char(tolower(hint[0]));
            ChatHandler(m_player).PSendSysMessage("  %s (locked: %s)", tier.name.c_str(), hint.c_str());
        }
    }
    if (m_addonReady)
        SendStarters();
    SendAddon("tierend");
}

void BonkPlayer::ResetCharacter()
{
    if (!m_player->IsAlive())
    {
        m_player->ResurrectPlayer(1.0f);
        m_player->SpawnCorpseBones();
    }

    m_player->CombatStop();

    if (m_player->GetLevel() != 1)
        m_player->GiveLevel(1);
    m_player->SetUInt32Value(PLAYER_XP, 0);
    m_player->InitTalentForLevel();

    m_player->SetHealth(m_player->GetMaxHealth());
    m_player->SetPower(POWER_MANA, m_player->GetMaxPower(POWER_MANA));
}

void BonkPlayer::ClearAuras()
{
    std::set<uint32> spells;
    for (auto const& itr : m_player->GetSpellAuraHolderMap())
        if (!itr.second->IsPassive() && itr.second->GetCastItemGuid().IsEmpty())
            spells.insert(itr.second->GetId());
    for (uint32 spellId : spells)
        m_player->RemoveAurasDueToSpell(spellId);
}

bool BonkPlayer::IsAtCamp() const
{
    return m_player->GetMapId() == BONK_CAMP_LOCATION.mapId &&
        m_player->IsWithinDist2d(BONK_CAMP_LOCATION.x, BONK_CAMP_LOCATION.y, BONK_CAMP_RADIUS);
}

void BonkPlayer::TeleportToCamp()
{
    m_player->TeleportTo(BONK_CAMP_ARRIVAL);
    m_sitAtCampMs = BONK_CAMP_SIT_DELAY_MS;
}

void BonkPlayer::UpdateCampSeat(uint32 diff)
{
    if (!m_sitAtCampMs || !m_player->IsInWorld() || m_player->IsBeingTeleported())
        return;
    if (diff < m_sitAtCampMs)
    {
        m_sitAtCampMs -= diff;
        return;
    }
    m_sitAtCampMs = 0;
    SitAtCamp();
}

void BonkPlayer::SitAtCamp()
{
    if (m_run || !m_player->IsAlive() || !IsAtCamp())
        return;

    GameObject* chair = m_player->FindNearestGameObject(BONK_CAMP_CHAIR, BONK_CAMP_CHAIR_RANGE);
    if (!chair)
        return;
    for (auto const& ref : m_player->GetMap()->GetPlayers())
    {
        Player const* other = ref.getSource();
        if (other && other != m_player && other->GetStandState() != UNIT_STAND_STATE_STAND &&
            other->GetDistance2d(chair) <= BONK_CAMP_CHAIR_TAKEN)
            return;
    }
    chair->Use(m_player);
}

void BonkPlayer::UpdateCampMailNotice(uint32 diff)
{
    if (!m_campMailNoticeMs || m_run || m_player->IsBeingTeleported() || !IsAtCamp())
        return;

    if (diff < m_campMailNoticeMs)
    {
        m_campMailNoticeMs -= diff;
        return;
    }
    m_campMailNoticeMs = 0;
    Notify("You got mail!");
}

bool BonkPlayer::HasUnreadMail() const
{
    MasterPlayer const* master = m_player->GetSession()->GetMasterPlayer();
    return master && master->HasUnreadMail();
}

bool BonkPlayer::IsHiddenFrom(Creature const* creature, Player const* viewer)
{
    return creature->GetEntry() == BONK_MAILBOX_BEACON && !viewer->GetBonkPlayer()->HasUnreadMail();
}

void BonkPlayer::UpdateMailboxGlow(uint32 diff)
{
    m_mailboxGlowMs += diff;
    if (m_mailboxGlowMs < BONK_MAILBOX_CHECK_MS)
        return;
    m_mailboxGlowMs = 0;

    bool const unread = HasUnreadMail();
    if (unread == m_mailboxGlowShown || m_run || m_player->IsBeingTeleported() || !IsAtCamp())
        return;
    m_mailboxGlowShown = unread;
    if (Creature* beacon = m_player->FindNearestCreature(BONK_MAILBOX_BEACON, BONK_MAILBOX_GLOW_RANGE))
        m_player->UpdateVisibilityOf(m_player, beacon);
}

bool BonkPlayer::MoveToCampBeforeLogin(bool gameMaster)
{
    if (gameMaster || IsAtCamp())
        return false;

    m_player->SetLocationMapId(BONK_CAMP_ARRIVAL.mapId);
    m_player->Relocate(BONK_CAMP_ARRIVAL.x, BONK_CAMP_ARRIVAL.y, BONK_CAMP_ARRIVAL.z, BONK_CAMP_ARRIVAL.o);
    m_sitAtCampMs = BONK_CAMP_SIT_DELAY_MS;
    return true;
}
