/*
 * Vanilla Bonk: `.bonk` chat commands. The VanillaBonk addon sends these
 * too, so they are all available to players.
 */

#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Chat.h"
#include "Player.h"
#include "WorldSession.h"

bool ChatHandler::HandleBonkStartCommand(char* args)
{
    Player* player = m_session->GetPlayer();
    char* key = ExtractArg(&args);

    BonkMap const* map = key ? sBonkMgr.FindMap(key) : nullptr;
    if (!map)
    {
        SendSysMessage("Choose a map:");
        player->GetBonkPlayer()->SendMapList();
        SetSentErrorMessage(true);
        return false;
    }

    std::string error;
    if (!player->GetBonkPlayer()->StartRun(*map, error))
    {
        SendSysMessage(error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    return true;
}

bool ChatHandler::HandleBonkStopCommand(char* /*args*/)
{
    BonkPlayer* bonk = m_session->GetPlayer()->GetBonkPlayer();
    if (!bonk->GetRun())
    {
        SendSysMessage("You are not in a run.");
        SetSentErrorMessage(true);
        return false;
    }

    bonk->StopRun();
    return true;
}

bool ChatHandler::HandleBonkPickCommand(char* args)
{
    uint32 choice;
    if (!ExtractUInt32(&args, choice))
        return false;

    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    std::string error = "You are not in a run.";
    if (!run || !run->Pick(choice, error))
    {
        SendSysMessage(error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    return true;
}

bool ChatHandler::HandleBonkHelloCommand(char* /*args*/)
{
    // The addon announces itself; from now on it gets protocol lines.
    BonkPlayer* bonk = m_session->GetPlayer()->GetBonkPlayer();
    bonk->SetAddonReady();
    bonk->SendAddon("hello~1");
    bonk->SendMapList();
    if (BonkRun* run = bonk->GetRun())
        run->SendFullState();
    return true;
}

bool ChatHandler::HandleBonkMapsCommand(char* /*args*/)
{
    SendSysMessage("Maps:");
    m_session->GetPlayer()->GetBonkPlayer()->SendMapList();
    return true;
}

bool ChatHandler::HandleBonkCampCommand(char* /*args*/)
{
    BonkPlayer* bonk = m_session->GetPlayer()->GetBonkPlayer();
    if (bonk->GetRun())
    {
        SendSysMessage("Leave the run first with .bonk stop.");
        SetSentErrorMessage(true);
        return false;
    }

    bonk->TeleportToCamp();
    return true;
}

bool ChatHandler::HandleBonkReloadCommand(char* /*args*/)
{
    // Map threads read these tables without locking, so only reload between runs.
    if (m_session && m_session->GetPlayer()->GetBonkPlayer()->GetRun())
    {
        SendSysMessage("Leave the run first with .bonk stop.");
        SetSentErrorMessage(true);
        return false;
    }

    sBonkMgr.Load();
    SendSysMessage("Bonk tables reloaded.");
    return true;
}
