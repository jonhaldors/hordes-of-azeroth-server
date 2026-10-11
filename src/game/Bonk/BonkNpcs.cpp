/*
 * Vanilla Bonk: gossip and use scripts for camp NPCs and arena objects.
 *
 * The run master at camp lists the tiers in `bonk_tier` and starts a run
 * on the one the player picks. In the arena, chests open an upgrade choice
 * and the portal that appears after a stage boss leads on or home. The
 * NPCs and objects themselves are world database rows.
 */

#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Creature.h"
#include "GameObject.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"

namespace
{
    // npc_text greetings.
    uint32 const BONK_TEXT_RUN_MASTER = 900001;
    uint32 const BONK_TEXT_PORTAL     = 900002;

    enum PortalAction
    {
        PORTAL_ACTION_NEXT = GOSSIP_ACTION_INFO_DEF + 1,
        PORTAL_ACTION_HOME = GOSSIP_ACTION_INFO_DEF + 2,
    };
}

bool GossipHello_npc_bonk_run_master(Player* player, Creature* creature)
{
    BonkPlayer* bonk = player->GetBonkPlayer();
    for (auto const& itr : sBonkMgr.GetTiers())
    {
        if (player->PlayerTalkClass->GetGossipMenu().MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            break;

        BonkTier const& tier = itr.second;
        std::string text = tier.name;
        if (!bonk->IsTierUnlocked(tier))
            text += " (locked: " + bonk->GetUnlockHint(tier) + ")";
        else if (uint32 const best = bonk->GetBestStage(tier.id))
            text += " (best: stage " + std::to_string(best) + ")";
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, text.c_str(), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + tier.id);
    }

    player->SEND_GOSSIP_MENU(BONK_TEXT_RUN_MASTER, creature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_bonk_run_master(Player* player, Creature* /*creature*/, uint32 /*sender*/, uint32 action)
{
    player->CLOSE_GOSSIP_MENU();

    // The tables may have been reloaded while the menu was open.
    BonkTier const* tier = action >= GOSSIP_ACTION_INFO_DEF ? sBonkMgr.GetTier(action - GOSSIP_ACTION_INFO_DEF) : nullptr;
    if (!tier)
        return true;

    std::string error;
    if (!player->GetBonkPlayer()->StartRun(*tier, error))
        player->GetBonkPlayer()->Notify(error);
    return true;
}

bool GOHello_go_bonk_chest(Player* player, GameObject* go)
{
    if (BonkRun* run = player->GetBonkPlayer()->GetRun())
        run->UseChest(go);
    return true;
}

bool GOHello_go_bonk_portal(Player* player, GameObject* go)
{
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (!run || !run->IsOwnPortal(go))
        return true;

    if (BonkMap const* next = run->GetNextMap())
    {
        std::string const text = "Stage " + std::to_string(run->GetStage() + 1) + ": " + next->name;
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, text.c_str(), GOSSIP_SENDER_MAIN, PORTAL_ACTION_NEXT);
    }
    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TAXI, "Return to camp (end the run)", GOSSIP_SENDER_MAIN, PORTAL_ACTION_HOME);
    player->SEND_GOSSIP_MENU(BONK_TEXT_PORTAL, go->GetObjectGuid());
    return true;
}

bool GOGossipSelect_go_bonk_portal(Player* player, GameObject* go, uint32 /*sender*/, uint32 action)
{
    player->CLOSE_GOSSIP_MENU();

    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (!run || !run->IsOwnPortal(go))
        return true;

    if (action == PORTAL_ACTION_NEXT)
        run->EnterNextStage();
    else if (action == PORTAL_ACTION_HOME)
        run->GoHome();
    return true;
}

void AddSC_bonk_npcs()
{
    Script* newscript;

    newscript = new Script;
    newscript->Name = "npc_bonk_run_master";
    newscript->pGossipHello = &GossipHello_npc_bonk_run_master;
    newscript->pGossipSelect = &GossipSelect_npc_bonk_run_master;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "go_bonk_chest";
    newscript->pGOHello = &GOHello_go_bonk_chest;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "go_bonk_portal";
    newscript->pGOHello = &GOHello_go_bonk_portal;
    newscript->pGOGossipSelect = &GOGossipSelect_go_bonk_portal;
    newscript->RegisterSelf();
}
