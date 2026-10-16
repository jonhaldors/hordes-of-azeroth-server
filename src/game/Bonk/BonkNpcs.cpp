/*
 * Vanilla Bonk: gossip and use scripts for camp NPCs and arena objects.
 *
 * At camp, the run master lists the tiers in `bonk_tier` and starts a run
 * on the one the player picks, the quartermaster sells permanent upgrades
 * (`bonk_upgrade`), Sayge sells fortunes for Darkmoon Tickets: run unlocks
 * and switchable Dark Fortunes (`bonk_talent`), and the talent trainer spends
 * a class's talent points, earned with Mastery levels (`bonk_mastery`); the
 * gear vendor is a plain vendor.
 * In the arena, chests open an upgrade choice and the portal that appears
 * after a stage boss leads on or home. The NPCs and objects themselves are
 * world database rows.
 */

#include "BonkMastery.h"
#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Creature.h"
#include "GameObject.h"
#include "ObjectMgr.h"
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
    uint32 const BONK_TEXT_MASTERY       = 900008;
    uint32 const BONK_TEXT_MASTERY_SOON  = 900009;
    uint32 const BONK_TEXT_VAULT_EMPTY   = 900010;
    uint32 const BONK_TEXT_VAULT         = 900011;

    // Talent trainer without the addon: this action takes the points back.
    uint32 const MASTERY_ACTION_RESPEC = GOSSIP_ACTION_INFO_DEF - 1;

    // Sayge's options: what a click does.
    uint32 const SENDER_BUY    = GOSSIP_SENDER_MAIN;
    uint32 const SENDER_TOGGLE = GOSSIP_SENDER_MAIN + 1;

    enum PortalAction
    {
        PORTAL_ACTION_NEXT = GOSSIP_ACTION_INFO_DEF + 1,
        PORTAL_ACTION_HOME = GOSSIP_ACTION_INFO_DEF + 2,
        PORTAL_ACTION_HOME_SURE = GOSSIP_ACTION_INFO_DEF + 3,
        PORTAL_ACTION_BACK = GOSSIP_ACTION_INFO_DEF + 4,
    };

    // The vault without the addon: the sender says which way, the action is
    // the item's guid.
    uint32 const SENDER_VAULT_PUT  = GOSSIP_SENDER_MAIN + 1;
    uint32 const SENDER_VAULT_TAKE = GOSSIP_SENDER_MAIN + 2;
}

bool GossipHello_npc_bonk_run_master(Player* player, Creature* creature)
{
    BonkPlayer* bonk = player->GetBonkPlayer();

    // The addon hides this menu and shows the arena window instead.
    if (bonk->IsAddonReady())
    {
        bonk->OpenArena(creature);
        player->SEND_GOSSIP_MENU(BONK_TEXT_RUN_MASTER, creature->GetObjectGuid());
        return true;
    }

    for (BonkTier const* tierPtr : BonkPlayer::GetTiersInOrder())
    {
        if (player->PlayerTalkClass->GetGossipMenu().MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
            break;

        BonkTier const& tier = *tierPtr;
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

    // The addon hides this menu and shows the fortunes as talent trees.
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

bool GossipHello_npc_bonk_mastery(Player* player, Creature* creature)
{
    BonkPlayer* bonk = player->GetBonkPlayer();

    // Classes whose trees are not out yet still bank their experience.
    if (!sBonkMastery.HasTrees(player->GetClass()))
    {
        player->SEND_GOSSIP_MENU(BONK_TEXT_MASTERY_SOON, creature->GetObjectGuid());
        return true;
    }

    // The addon hides this menu and shows the class's trees.
    if (bonk->IsAddonReady())
    {
        bonk->OpenShop(BONK_SHOP_MASTERY, creature);
        player->SEND_GOSSIP_MENU(BONK_TEXT_MASTERY, creature->GetObjectGuid());
        return true;
    }

    uint32 const points = bonk->GetMasteryPoints();
    uint32 const spent = bonk->GetMasterySpent();
    std::string const header = "Mastery level " + std::to_string(bonk->GetMasteryLevel()) + ", " +
        std::to_string(points > spent ? points - spent : 0) + " talent points to spend";
    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, header.c_str(), GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);
    for (auto const& itr : sBonkMastery.GetNodes())
    {
        BonkMasteryNode const& node = itr.second;
        if (node.classId != player->GetClass())
            continue;
        if (player->PlayerTalkClass->GetGossipMenu().MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS - 1)
            break;
        uint8 const rank = bonk->GetMasteryRank(node.id);
        std::string const text = node.name + " " + std::to_string(rank) + "/" + std::to_string(node.maxRank) + ": " + node.description;
        player->ADD_GOSSIP_ITEM(rank < node.maxRank ? GOSSIP_ICON_TRAINER : GOSSIP_ICON_CHAT, text.c_str(), SENDER_BUY,
            GOSSIP_ACTION_INFO_DEF + node.id);
    }
    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Take my points back (free)", SENDER_BUY, MASTERY_ACTION_RESPEC);

    player->SEND_GOSSIP_MENU(BONK_TEXT_MASTERY, creature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_bonk_mastery(Player* player, Creature* creature, uint32 /*sender*/, uint32 action)
{
    std::string message;
    if (action == MASTERY_ACTION_RESPEC)
        player->GetBonkPlayer()->ResetMastery(message);
    else if (action > GOSSIP_ACTION_INFO_DEF)
        player->GetBonkPlayer()->LearnMastery(action - GOSSIP_ACTION_INFO_DEF, message);
    if (!message.empty())
        player->GetBonkPlayer()->Notify(message);

    return GossipHello_npc_bonk_mastery(player, creature);
}

bool GOHello_go_bonk_chest(Player* player, GameObject* go)
{
    if (BonkRun* run = player->GetBonkPlayer()->GetRun())
        run->UseChest(go);
    return true;
}

// The wandering vendor in a run: his shelf, no gossip menu.
bool GossipHello_npc_bonk_vendor(Player* player, Creature* creature)
{
    if (BonkRun* run = player->GetBonkPlayer()->GetRun())
        run->OpenVendor(creature);
    return true;
}

// A shrine's model at a run's spot: Greed Totem, Challenge Altar.
bool GOHello_go_bonk_shrine(Player* player, GameObject* go)
{
    if (BonkRun* run = player->GetBonkPlayer()->GetRun())
        run->UseShrine(go);
    return true;
}

bool GOHello_go_bonk_portal(Player* player, GameObject* go)
{
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (!run || !run->IsOwnPortal(go))
        return true;

    if (BonkMap const* next = run->GetNextMap())
    {
        // Past an endless difficulty's last stage: Endless.
        BonkTier const& tier = run->GetTier();
        bool const endless = tier.stages && run->GetStage() + 1 > tier.stages;
        std::string const text = std::string(endless ? "Endless - " : "") + "Stage " + std::to_string(run->GetStage() + 1) + ": " + next->name;
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, text.c_str(), GOSSIP_SENDER_MAIN, PORTAL_ACTION_NEXT);
    }
    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TAXI, "Return to camp (end the run)", GOSSIP_SENDER_MAIN, PORTAL_ACTION_HOME);
    player->SEND_GOSSIP_MENU(BONK_TEXT_PORTAL, go->GetObjectGuid());
    return true;
}

bool GOGossipSelect_go_bonk_portal(Player* player, GameObject* go, uint32 /*sender*/, uint32 action)
{
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (!run || !run->IsOwnPortal(go))
    {
        player->CLOSE_GOSSIP_MENU();
        return true;
    }

    // Home with the Extraction Vault empty though a find could come: ask.
    if (action == PORTAL_ACTION_HOME && run->IsVaultEmptyWithFinds())
    {
        player->PlayerTalkClass->ClearMenus();
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TAXI, "Go home anyway", GOSSIP_SENDER_MAIN, PORTAL_ACTION_HOME_SURE);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Back", GOSSIP_SENDER_MAIN, PORTAL_ACTION_BACK);
        player->SEND_GOSSIP_MENU(BONK_TEXT_VAULT_EMPTY, go->GetObjectGuid());
        return true;
    }
    if (action == PORTAL_ACTION_BACK)
    {
        player->PlayerTalkClass->ClearMenus();
        return GOHello_go_bonk_portal(player, go);
    }

    player->CLOSE_GOSSIP_MENU();
    if (action == PORTAL_ACTION_NEXT)
        run->EnterNextStage();
    else if (action == PORTAL_ACTION_HOME || action == PORTAL_ACTION_HOME_SURE)
        run->GoHome();
    return true;
}

bool GOHello_go_bonk_vault(Player* player, GameObject* go)
{
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (!run || !run->IsOwnVault(go))
        return true;

    if (player->GetBonkPlayer()->IsAddonReady())
    {
        run->OpenVault();
        return true;
    }

    // Without the addon: the finds as gossip lines.
    for (BonkRun::VaultFind const& find : run->GetVaultFinds())
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(find.entry);
        if (!proto)
            continue;
        std::string const text = find.inVault ? std::string("In the vault: ") + proto->Name1 + " (take it out)" :
            std::string("Put in the vault: ") + proto->Name1 + " (else sold for " + std::to_string(find.sellCopper / 100) + "s)";
        player->ADD_GOSSIP_ITEM(find.inVault ? GOSSIP_ICON_INTERACT_1 : GOSSIP_ICON_VENDOR, text.c_str(),
            find.inVault ? SENDER_VAULT_TAKE : SENDER_VAULT_PUT, find.guid.GetCounter());
    }
    player->SEND_GOSSIP_MENU(BONK_TEXT_VAULT, go->GetObjectGuid());
    return true;
}

bool GOGossipSelect_go_bonk_vault(Player* player, GameObject* go, uint32 sender, uint32 action)
{
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (!run || !run->IsOwnVault(go))
    {
        player->CLOSE_GOSSIP_MENU();
        return true;
    }

    std::string error;
    if (!(sender == SENDER_VAULT_TAKE ? run->TakeFromVault(action, error) : run->PutInVault(action, error)))
        player->GetBonkPlayer()->Notify(error);
    player->PlayerTalkClass->ClearMenus();
    return GOHello_go_bonk_vault(player, go);
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
    newscript->Name = "npc_bonk_mastery";
    newscript->pGossipHello = &GossipHello_npc_bonk_mastery;
    newscript->pGossipSelect = &GossipSelect_npc_bonk_mastery;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "go_bonk_chest";
    newscript->pGOHello = &GOHello_go_bonk_chest;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "npc_bonk_vendor";
    newscript->pGossipHello = &GossipHello_npc_bonk_vendor;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "go_bonk_shrine";
    newscript->pGOHello = &GOHello_go_bonk_shrine;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "go_bonk_portal";
    newscript->pGOHello = &GOHello_go_bonk_portal;
    newscript->pGOGossipSelect = &GOGossipSelect_go_bonk_portal;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "go_bonk_vault";
    newscript->pGOHello = &GOHello_go_bonk_vault;
    newscript->pGOGossipSelect = &GOGossipSelect_go_bonk_vault;
    newscript->RegisterSelf();
}
