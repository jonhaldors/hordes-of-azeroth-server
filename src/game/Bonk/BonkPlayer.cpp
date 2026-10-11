/*
 * Vanilla Bonk: per-player game-mode state.
 */

#include "BonkPlayer.h"
#include "BonkMgr.h"
#include "BonkRun.h"
#include "Chat.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
    // Characters this close to the camp spawn point count as being at camp.
    float const BONK_CAMP_RADIUS = 300.0f;
}

BonkPlayer::BonkPlayer(Player* player) : m_player(player)
{
}

BonkPlayer::~BonkPlayer() = default;

void BonkPlayer::OnLogin()
{
    // Gold is shared by every character on the account.
    uint32 const accountId = m_player->GetSession()->GetAccountId();
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `gold` FROM `bonk_account` WHERE `account` = %u", accountId));
    if (result)
        m_player->SetMoney(result->Fetch()[0].GetUInt32());
    else
        CharacterDatabase.PExecute("INSERT INTO `bonk_account` (`account`, `gold`) VALUES (%u, %u)", accountId, m_player->GetMoney());
    m_goldLoaded = true;

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

    // Runs never survive a logout.
    ResetCharacter();
    m_campCheckPending = true;
}

void BonkPlayer::OnLogout()
{
    if (m_run)
    {
        m_run->Abort();
        m_run.reset();
    }
    m_addonReady = false;

    // The hero screen is loaded again right after this, so it shows this logout.
    WriteHeroScreenStats();
}

void BonkPlayer::RecordRun(uint32 stage, uint32 seconds, uint32 kills, uint32 goldEarned)
{
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

    // Gear: everything equipped except the cosmetic shirt and tabard.
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

    // The other heroes are offline and fully saved; this one is read from
    // memory, since its last saves may still be queued.
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
    lua << "-- Written by mangosd (Bonk.GlueStatsFile) when a character logs out.\n";
    lua << "BONK_STATS = {\n";
    lua << "\tgold = " << m_player->GetMoney() << ",\n";
    lua << "\theroes = {\n";
    for (auto const& itr : heroes)
    {
        // Character names are letters only, but never let one break the Lua.
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

    // Write next to the file and swap it in, so the client never reads half of it.
    std::string const temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::out | std::ios::trunc);
        if (!file)
        {
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Bonk: cannot write hero stats to %s", temporary.c_str());
            return;
        }
        file << lua.str();
    }
    std::remove(path.c_str());
    if (std::rename(temporary.c_str(), path.c_str()) != 0)
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Bonk: cannot replace hero stats file %s", path.c_str());
}

void BonkPlayer::OnSave()
{
    // A character being created has no account gold yet; don't overwrite it.
    if (!m_goldLoaded)
        return;

    CharacterDatabase.PExecute("REPLACE INTO `bonk_account` (`account`, `gold`) VALUES (%u, %u)",
        m_player->GetSession()->GetAccountId(), m_player->GetMoney());
}

void BonkPlayer::Update(uint32 diff)
{
    if (m_campCheckPending && !m_player->IsBeingTeleported())
    {
        m_campCheckPending = false;
        // GMs may roam the world; everyone else lives at camp between runs.
        if (!m_run && !m_player->IsGameMaster() && !IsAtCamp())
            TeleportToCamp();
    }

    if (m_run)
    {
        m_run->Update(diff);
        if (m_run->IsFinished())
            m_run.reset();
    }
}

bool BonkPlayer::StartRun(BonkTier const& tier, std::string& error)
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

    m_run.reset(new BonkRun(m_player, tier));
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
    return "Reach stage " + std::to_string(tier.unlockStage) + " on " + required->name;
}

void BonkPlayer::RecordStage(uint32 tierId, uint32 stage)
{
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
            Notify(tier->name + " unlocked!");
}

void BonkPlayer::SendAddon(std::string const& line) const
{
    if (!m_addonReady)
        return;

    ChatHandler(m_player).SendSysMessage(("BONK~" + line).c_str());
}

void BonkPlayer::Notify(std::string const& text) const
{
    if (m_addonReady)
        SendAddon("msg~" + text);
    else
        ChatHandler(m_player).SendSysMessage(text.c_str());
}

void BonkPlayer::SendTierList() const
{
    for (auto const& itr : sBonkMgr.GetTiers())
    {
        BonkTier const& tier = itr.second;
        bool const unlocked = IsTierUnlocked(tier);
        uint32 const best = GetBestStage(tier.id);
        if (m_addonReady)
            SendAddon("tier~" + std::to_string(tier.id) + "~" + tier.key + "~" + tier.name + "~" + (unlocked ? "1" : "0") + "~" +
                (unlocked ? "" : GetUnlockHint(tier)) + "~" + std::to_string(best));
        else if (unlocked)
            ChatHandler(m_player).PSendSysMessage("  %s (best: stage %u): .bonk start %s", tier.name.c_str(), best, tier.key.c_str());
        else
            ChatHandler(m_player).PSendSysMessage("  %s (locked: %s)", tier.name.c_str(), GetUnlockHint(tier).c_str());
    }
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

bool BonkPlayer::IsAtCamp() const
{
    return m_player->GetMapId() == BONK_CAMP_LOCATION.mapId &&
        m_player->IsWithinDist2d(BONK_CAMP_LOCATION.x, BONK_CAMP_LOCATION.y, BONK_CAMP_RADIUS);
}

void BonkPlayer::TeleportToCamp()
{
    m_player->TeleportTo(BONK_CAMP_LOCATION);
}
