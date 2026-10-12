/*
 * Vanilla Bonk: `.bonk` chat commands. The VanillaBonk addon sends these
 * too, so they are all available to players.
 */

#include "BonkCodex.h"
#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "BonkRun.h"
#include "Chat.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>

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

bool ChatHandler::HandleBonkPassCommand(char* /*args*/)
{
    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    std::string error = "You are not in a run.";
    if (!run || !run->Skip(error))
    {
        SendSysMessage(error.c_str());
        SetSentErrorMessage(true);
        return false;
    }
    return true;
}

bool ChatHandler::HandleBonkLockCommand(char* args)
{
    uint32 choice;
    if (!ExtractUInt32(&args, choice))
        return false;

    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    std::string error = "You are not in a run.";
    if (!run || !run->Lock(choice, error))
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

bool ChatHandler::HandleBonkUnlearnCommand(char* args)
{
    uint32 id;
    if (!ExtractUInt32(&args, id))
        return false;

    m_session->GetPlayer()->GetBonkPlayer()->ShopUnlearn(id);
    return true;
}

bool ChatHandler::HandleBonkRespecCommand(char* /*args*/)
{
    m_session->GetPlayer()->GetBonkPlayer()->ShopRespec();
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

bool ChatHandler::HandleBonkSkipCommand(char* args)
{
    // GM testing aid: jump the stage clock forward, by default straight to
    // the stage boss.
    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    if (!run)
    {
        SendSysMessage("You are not in a run.");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 seconds = 0;
    if (*args && !ExtractUInt32(&args, seconds))
        return false;

    std::string error;
    if (!run->SkipTime(seconds, error))
    {
        SendSysMessage(error.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    SendSysMessage(seconds ? "Skipped ahead." : "Skipped to the boss.");
    return true;
}

bool ChatHandler::HandleBonkGodCommand(char* /*args*/)
{
    // GM testing aid: toggle god mode (damage stops at 1 health). It stays on
    // through run starts until toggled off or the character logs out.
    Player* player = m_session->GetPlayer();
    BonkPlayer* bonk = player->GetBonkPlayer();
    bool const on = !bonk->IsTestGod();
    bonk->SetTestGod(on);
    player->SetCheatGod(on);
    SendSysMessage(on ? "God mode on: damage stops at 1 health." : "God mode off.");
    return true;
}

bool ChatHandler::HandleBonkQuestsCommand(char* /*args*/)
{
    Player* player = m_session->GetPlayer();
    if (player->GetBonkPlayer()->IsAddonReady())
    {
        sBonkQuests.SendLog(player, "");
        return true;
    }

    SendSysMessage("Quests:");
    for (std::string const& line : sBonkQuests.DescribeAll(player))
        PSendSysMessage("  %s", line.c_str());
    return true;
}

bool ChatHandler::HandleBonkCodexCommand(char* /*args*/)
{
    // The spellbook is an addon window only.
    Player* player = m_session->GetPlayer();
    if (!player->GetBonkPlayer()->IsAddonReady())
    {
        SendSysMessage("The spellbook needs the Vanilla Bonk addon.");
        return true;
    }
    BonkCodex::Send(player, "");
    return true;
}

bool ChatHandler::HandleBonkHelloCommand(char* /*args*/)
{
    // The addon announces itself; from now on it gets protocol lines.
    BonkPlayer* bonk = m_session->GetPlayer()->GetBonkPlayer();
    bonk->SetAddonReady();
    bonk->SendAddon("hello~1");
    bonk->SendTierList();
    bonk->SendGearInfo();
    bonk->SendMasteryState();
    bonk->SendWallet();
    sBonkQuests.SendLog(m_session->GetPlayer(), "");
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

bool ChatHandler::HandleBonkTelegraphCommand(char* args)
{
    // GM testing aid: a ground warning in front of you. In a run it fires
    // (a burst of fire, no damage); anywhere else it just fills and goes.
    // .bonk telegraph circle|cone|line|ring [size in yards] [seconds]
    char* shapeArg = ExtractArg(&args);
    BonkTelegraphShape shape = BONK_TELEGRAPH_CIRCLE;
    if (!shapeArg || !ParseBonkTelegraphShape(shapeArg, shape))
    {
        SendSysMessage("Usage: .bonk telegraph circle|cone|line|ring [size] [seconds]");
        SetSentErrorMessage(true);
        return false;
    }

    float size = shape == BONK_TELEGRAPH_LINE ? 20.0f : shape == BONK_TELEGRAPH_CONE ? 10.0f : 5.0f;
    float seconds = 2.0f;
    ExtractFloat(&args, size);
    ExtractFloat(&args, seconds);
    size = std::max(0.5f, std::min(size, 60.0f));
    uint32 const durationMs = uint32(std::max(0.2f, std::min(seconds, 30.0f)) * IN_MILLISECONDS);

    // Circles and rings a little ahead; cones and lines from your feet, the way you face.
    Player* player = m_session->GetPlayer();
    float const o = player->GetOrientation();
    float x = player->GetPositionX(), y = player->GetPositionY();
    if (shape == BONK_TELEGRAPH_CIRCLE || shape == BONK_TELEGRAPH_RING)
    {
        x += (size + 2.0f) * std::cos(o);
        y += (size + 2.0f) * std::sin(o);
    }
    float const z = player->GetPositionZ();

    if (BonkRun* run = player->GetBonkPlayer()->GetRun())
    {
        if (!run->IsActive() || run->IsPaused())
        {
            SendSysMessage("Not while the run is paused or over.");
            SetSentErrorMessage(true);
            return false;
        }
        run->AddTelegraph(shape, x, y, z, o, size, durationMs, [run, x, y, z]() { run->Erupt(ObjectGuid(), x, y, z, 0.0f, 0); });
    }
    else
        BonkTelegraphs::Preview(player->GetMap(), shape, x, y, z, o, size, durationMs);

    PSendSysMessage("A %s of %.1f yards, %.1f seconds.", GetBonkTelegraphShapeName(shape), size, durationMs / float(IN_MILLISECONDS));
    return true;
}
