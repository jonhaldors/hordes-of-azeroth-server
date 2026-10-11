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

    BonkTier const* tier = key ? sBonkMgr.FindTier(key) : nullptr;
    if (!tier)
    {
        SendSysMessage("Choose a difficulty:");
        player->GetBonkPlayer()->SendTierList();
        SetSentErrorMessage(true);
        return false;
    }

    std::string error;
    if (!player->GetBonkPlayer()->StartRun(*tier, error))
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

bool ChatHandler::HandleBonkRerollCommand(char* /*args*/)
{
    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    std::string error = "You are not in a run.";
    if (!run || !run->Reroll(error))
    {
        SendSysMessage(error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    return true;
}

bool ChatHandler::HandleBonkBanishCommand(char* args)
{
    uint32 choice;
    if (!ExtractUInt32(&args, choice))
        return false;

    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    std::string error = "You are not in a run.";
    if (!run || !run->Banish(choice, error))
    {
        SendSysMessage(error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    return true;
}

bool ChatHandler::HandleBonkLearnCommand(char* args)
{
    uint32 id;
    if (!ExtractUInt32(&args, id))
        return false;

    m_session->GetPlayer()->GetBonkPlayer()->ShopLearn(id);
    return true;
}

bool ChatHandler::HandleBonkToggleCommand(char* args)
{
    uint32 id;
    if (!ExtractUInt32(&args, id))
        return false;

    m_session->GetPlayer()->GetBonkPlayer()->ShopToggle(id);
    return true;
}

bool ChatHandler::HandleBonkShardsCommand(char* args)
{
    // GM testing aid: give the selected character (or yourself) Talent Shards.
    uint32 count;
    if (!ExtractUInt32(&args, count) || !count)
        return false;

    Player* target = GetSelectedPlayer();
    if (!target)
        target = m_session->GetPlayer();

    target->GetBonkPlayer()->AddShards(count);
    PSendSysMessage("%s now has %s.", target->GetName(), FormatBonkShards(target->GetBonkPlayer()->GetShards()).c_str());
    return true;
}

bool ChatHandler::HandleBonkHelloCommand(char* /*args*/)
{
    // The addon announces itself; from now on it gets protocol lines.
    BonkPlayer* bonk = m_session->GetPlayer()->GetBonkPlayer();
    bonk->SetAddonReady();
    bonk->SendAddon("hello~1");
    bonk->SendTierList();
    if (BonkRun* run = bonk->GetRun())
        run->SendFullState();
    return true;
}

bool ChatHandler::HandleBonkMapsCommand(char* /*args*/)
{
    SendSysMessage("Difficulties:");
    m_session->GetPlayer()->GetBonkPlayer()->SendTierList();
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
