/*
 * Vanilla Bonk: gossip and use scripts for camp NPCs and arena objects.
 *
 * At camp, the run master lists the tiers in `bonk_tier` and starts a run
 * on the one the player picks, the quartermaster sells permanent upgrades
 * (`bonk_upgrade`) and the talent trainer sells run unlocks and switches
 * difficulty toggles (`bonk_talent`); the gear vendor is a plain vendor.
 * In the arena, chests open an upgrade choice and the portal that appears
 * after a stage boss leads on or home. The NPCs and objects themselves are
 * world database rows.
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
    uint32 const BONK_TEXT_RUN_MASTER    = 900001;
    uint32 const BONK_TEXT_PORTAL        = 900002;
    uint32 const BONK_TEXT_QUARTERMASTER = 900003;
    uint32 const BONK_TEXT_TALENTS       = 900004;

    // Talent trainer options: what a click does.
    uint32 const SENDER_BUY    = GOSSIP_SENDER_MAIN;
    uint32 const SENDER_TOGGLE = GOSSIP_SENDER_MAIN + 1;

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

bool GossipHello_npc_bonk_quartermaster(Player* player, Creature* creature)
{
    BonkPlayer* bonk = player->GetBonkPlayer();

    // The addon hides this menu and shows the upgrades as talent trees.
    if (bonk->IsAddonReady())
    {
        bonk->OpenShop(BONK_SHOP_UPGRADES, creature);
        player->SEND_GOSSIP_MENU(BONK_TEXT_QUARTERMASTER, creature->GetObjectGuid());
        return true;
    }

    for (auto const& itr : sBonkMgr.GetUpgrades())
    {
        if (player->PlayerTalkClass->GetGossipMenu().MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            break;

        BonkUpgrade const& upgrade = itr.second;
        uint8 const rank = bonk->GetUpgradeRank(upgrade.id);
        std::string text = upgrade.name + " " + std::to_string(rank) + "/" + std::to_string(upgrade.maxRank) + ": " + upgrade.description;
        text += rank < upgrade.maxRank ? " - " + FormatBonkMoney(upgrade.GetCost(rank)) : " (max)";
        player->ADD_GOSSIP_ITEM(rank < upgrade.maxRank ? GOSSIP_ICON_TRAINER : GOSSIP_ICON_CHAT, text.c_str(), SENDER_BUY,
            GOSSIP_ACTION_INFO_DEF + upgrade.id);
    }

    player->SEND_GOSSIP_MENU(BONK_TEXT_QUARTERMASTER, creature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_bonk_quartermaster(Player* player, Creature* creature, uint32 /*sender*/, uint32 action)
{
    if (action >= GOSSIP_ACTION_INFO_DEF)
    {
        std::string message;
        player->GetBonkPlayer()->BuyUpgrade(action - GOSSIP_ACTION_INFO_DEF, message);
        if (!message.empty())
            player->GetBonkPlayer()->Notify(message);
    }

    // Show the new ranks and prices.
    return GossipHello_npc_bonk_quartermaster(player, creature);
}

bool GossipHello_npc_bonk_talents(Player* player, Creature* creature)
{
    BonkPlayer* bonk = player->GetBonkPlayer();

    // The addon hides this menu and shows the talents as talent trees.
    if (bonk->IsAddonReady())
    {
        bonk->OpenShop(BONK_SHOP_TALENTS, creature);
        player->SEND_GOSSIP_MENU(BONK_TEXT_TALENTS, creature->GetObjectGuid());
        return true;
    }

    for (auto const& itr : sBonkMgr.GetTalents())
    {
        if (player->PlayerTalkClass->GetGossipMenu().MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            break;

        BonkTalent const& talent = itr.second;
        uint8 const rank = bonk->GetTalentRank(talent.id);
        uint32 const action = GOSSIP_ACTION_INFO_DEF + talent.id;

        if (talent.IsToggle() && rank)
        {
            std::string const text = std::string(bonk->IsTalentEnabled(talent.id) ? "[On] " : "[Off] ") + talent.name + ": " + talent.description;
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, text.c_str(), SENDER_TOGGLE, action);
            continue;
        }

        std::string text = talent.name;
        if (!talent.IsToggle())
            text += " " + std::to_string(rank) + "/" + std::to_string(talent.maxRank);
        text += ": " + talent.description;
        text += rank < talent.maxRank ? " - " + FormatBonkShards(talent.GetCost(rank)) : " (max)";
        player->ADD_GOSSIP_ITEM(rank < talent.maxRank ? GOSSIP_ICON_TRAINER : GOSSIP_ICON_CHAT, text.c_str(), SENDER_BUY, action);
    }

    player->SEND_GOSSIP_MENU(BONK_TEXT_TALENTS, creature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_bonk_talents(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (action >= GOSSIP_ACTION_INFO_DEF)
    {
        uint32 const talentId = action - GOSSIP_ACTION_INFO_DEF;
        std::string message;
        if (sender == SENDER_TOGGLE)
            player->GetBonkPlayer()->ToggleTalent(talentId, message);
        else
            player->GetBonkPlayer()->BuyTalent(talentId, message);
        if (!message.empty())
            player->GetBonkPlayer()->Notify(message);
    }

    return GossipHello_npc_bonk_talents(player, creature);
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
    newscript->Name = "npc_bonk_quartermaster";
    newscript->pGossipHello = &GossipHello_npc_bonk_quartermaster;
    newscript->pGossipSelect = &GossipSelect_npc_bonk_quartermaster;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_bonk_talents";
    newscript->pGossipHello = &GossipHello_npc_bonk_talents;
    newscript->pGossipSelect = &GossipSelect_npc_bonk_talents;
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
