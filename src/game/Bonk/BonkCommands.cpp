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
        player->GetBonkPlayer()->PreviewTelegraph(shape, x, y, z, o, size, durationMs);

    PSendSysMessage("A %s of %.1f yards, %.1f seconds.", GetBonkTelegraphShapeName(shape), size, durationMs / float(IN_MILLISECONDS));
    return true;
}

bool ChatHandler::HandleBonkObjectiveCommand(char* args)
{
    // GM testing aid: an objective marker at one of the stage's spawn points
    // 120-250 yards away (or 30 yards ahead if none is free yet), or all of
    // them taken away.
    // .bonk objective shrine|vendor|event|extract [seconds] | clear
    Player* player = m_session->GetPlayer();
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (!run || !run->IsActive())
    {
        SendSysMessage("Objectives need a run.");
        SetSentErrorMessage(true);
        return false;
    }

    char* kindArg = ExtractArg(&args);
    if (kindArg && !strcmp(kindArg, "clear"))
    {
        std::vector<uint32> ids;
        for (BonkObjective const& objective : run->GetObjectives().GetAll())
            ids.push_back(objective.id);
        for (uint32 id : ids)
            run->RemoveObjective(id);
        PSendSysMessage("%u objectives taken away.", uint32(ids.size()));
        return true;
    }

    BonkObjectiveKind kind = BONK_OBJECTIVE_SHRINE;
    if (!kindArg || !ParseBonkObjectiveKind(kindArg, kind))
    {
        SendSysMessage("Usage: .bonk objective shrine|vendor|event|extract [seconds] | clear");
        SetSentErrorMessage(true);
        return false;
    }
    uint32 seconds = 0;
    ExtractUInt32(&args, seconds);

    char const* const labels[MAX_BONK_OBJECTIVE_KIND] = { "Shrine", "Goblin Merchant", "Event", "Extraction Point" };
    BonkSpot spot;
    bool const atSpot = run->TakeSpawnPoint(120.0f, 250.0f, spot);
    if (!atSpot)
    {
        float const o = player->GetOrientation();
        spot.x = player->GetPositionX() + 30.0f * std::cos(o);
        spot.y = player->GetPositionY() + 30.0f * std::sin(o);
        spot.z = player->GetMap()->GetHeight(spot.x, spot.y, player->GetPositionZ() + 10.0f, true, 30.0f);
        if (spot.z <= INVALID_HEIGHT)
            spot.z = player->GetPositionZ();
    }
    uint32 const id = run->AddObjective(kind, spot.x, spot.y, spot.z, labels[kind], seconds);
    PSendSysMessage("Objective %u (%s) %.0f yards away, %s.", id, GetBonkObjectiveKindName(kind), player->GetDistance2dToCenter(spot.x, spot.y),
        atSpot ? "at a spawn point" : "30 yards ahead (no free spawn point yet)");
    return true;
}

bool ChatHandler::HandleBonkSpotsCommand(char* /*args*/)
{
    // GM testing aid: an event pillar on every spawn point the stage has
    // gathered so far, for 20 seconds (seen only within the usual 100 yards).
    Player* player = m_session->GetPlayer();
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (!run || !run->IsActive())
    {
        SendSysMessage("Spawn points need a run.");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 taken = 0;
    for (BonkSpot const& spot : run->GetSpawnPoints().GetSpots())
    {
        player->SummonGameObject(900020 + BONK_OBJECTIVE_EVENT, spot.x, spot.y, spot.z, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 20, false);
        taken += spot.taken ? 1 : 0;
    }
    PSendSysMessage("%u spawn points (%u taken), shown for 20 seconds.", uint32(run->GetSpawnPoints().GetSpots().size()), taken);
    return true;
}

bool ChatHandler::HandleBonkEventCommand(char* args)
{
    // GM testing aid: a mini-event right now, whatever the scheduler says.
    // .bonk event horde
    char* nameArg = ExtractArg(&args);
    BonkMiniEventType type = BONK_MINI_EVENT_HORDE;
    if (!nameArg || !ParseBonkMiniEvent(nameArg, type))
    {
        SendSysMessage("Usage: .bonk event horde");
        SetSentErrorMessage(true);
        return false;
    }

    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    std::string error;
    if (!run)
        error = "Mini-events need a run.";
    else if (run->StartMiniEventNow(type, error))
    {
        PSendSysMessage("Mini-event %s started.", GetBonkMiniEventName(type));
        return true;
    }
    SendSysMessage(error.c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleBonkShrineCommand(char* args)
{
    // GM testing aid: a shrine at one of the stage's spawn points now.
    // .bonk shrine leyline|greed|altar|moonwell|stone
    char* nameArg = ExtractArg(&args);
    BonkShrineType type = BONK_SHRINE_LEY_LINE;
    if (!nameArg || !ParseBonkShrine(nameArg, type))
    {
        SendSysMessage("Usage: .bonk shrine leyline|greed|altar|moonwell|stone");
        SetSentErrorMessage(true);
        return false;
    }

    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    std::string error;
    if (!run)
        error = "Shrines need a run.";
    else if (run->PlaceShrineNow(type, error))
    {
        PSendSysMessage("Shrine %s placed.", GetBonkShrineName(type));
        return true;
    }
    SendSysMessage(error.c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleBonkLeyPickCommand(char* args)
{
    // A Ley Line blessing: .bonk leypick <number> (the addon's cards send it).
    uint32 index = 0;
    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    std::string error;
    if (!run)
        error = "No run.";
    else if (!ExtractUInt32(&args, index))
        error = "Usage: .bonk leypick <number>";
    else if (run->PickLeyBuff(index, error))
        return true;
    SendSysMessage(error.c_str());
    SetSentErrorMessage(true);
    return false;
}

bool ChatHandler::HandleBonkKeywordCommand(char* args)
{
    // GM testing aid: stacks of a keyword on the selected enemy of your run.
    // .bonk keyword <name, or its start> [stacks]
    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    if (!run || !run->IsActive())
    {
        SendSysMessage("You are not in a running stage.");
        SetSentErrorMessage(true);
        return false;
    }

    char* nameArg = ExtractArg(&args);
    uint32 stacks = 1;
    if (!nameArg || (*args && !ExtractUInt32(&args, stacks)))
        return false;

    std::string wanted = nameArg;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), ::tolower);
    BonkKeywordInfo const* keyword = nullptr;
    std::string names;
    for (auto const& itr : sBonkMgr.GetKeywords())
    {
        std::string name = itr.second.name;
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (!keyword && name.compare(0, wanted.size(), wanted) == 0)
            keyword = &itr.second;
        names += (names.empty() ? "" : ", ") + itr.second.name;
    }
    if (!keyword)
    {
        PSendSysMessage("No keyword starts with \"%s\". Keywords: %s.", nameArg, names.c_str());
        SetSentErrorMessage(true);
        return false;
    }

    Creature* mob = GetSelectedCreature();
    if (!mob || !run->ApplyKeyword(mob, keyword->id, stacks))
    {
        SendSysMessage("Select a living enemy of your run (one still immune after a hold can't be held again yet).");
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage("%s: %u %s.", mob->GetName(), run->GetKeywordStacks(mob, keyword->id), keyword->name.c_str());
    return true;
}

bool ChatHandler::HandleBonkScalingCommand(char* /*args*/)
{
    // GM tuning aid: enemy health right now against each spell's hit.
    BonkRun* run = m_session->GetPlayer()->GetBonkPlayer()->GetRun();
    if (!run || !run->IsActive())
    {
        SendSysMessage("You are not in a running stage.");
        SetSentErrorMessage(true);
        return false;
    }

    for (std::string const& line : run->DescribeScaling())
        SendSysMessage(line.c_str());
    return true;
}
