/*
 * Vanilla Bonk: per-player game-mode state.
 */

#include "BonkPlayer.h"
#include "BonkMgr.h"
#include "BonkRun.h"
#include "Chat.h"
#include "Database/DatabaseEnv.h"
#include "Player.h"
#include "World.h"
#include "WorldSession.h"

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

bool BonkPlayer::StartRun(BonkMap const& map, std::string& error)
{
    if (m_run)
    {
        error = "You are already in a run. Use .bonk stop to leave it.";
        return false;
    }

    if (map.creatures.empty())
    {
        error = "That map has no enemies set up.";
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

    m_run.reset(new BonkRun(m_player, map));
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

void BonkPlayer::SendMapList() const
{
    for (auto const& itr : sBonkMgr.GetMaps())
    {
        BonkMap const& map = itr.second;
        if (m_addonReady)
            SendAddon("map~" + std::to_string(map.id) + "~" + map.key + "~" + map.name + "~" + map.GetDifficultyName() + "~" + std::to_string(map.durationSeconds));
        else
            ChatHandler(m_player).PSendSysMessage("  %s (%s, %u min): .bonk start %s", map.name.c_str(), map.GetDifficultyName(), map.durationSeconds / 60, map.key.c_str());
    }
    SendAddon("mapsend");
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
