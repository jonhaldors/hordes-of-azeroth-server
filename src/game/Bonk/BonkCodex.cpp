#include "BonkCodex.h"
#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "Creature.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "WorldSession.h"

#include <string>

namespace
{
    uint32 const BONK_TEXT_SPELLBOOK = 900006;

    void SendLock(BonkPlayer* bonk, char const* kind, uint32 id, BonkQuestReward type)
    {
        if (BonkQuest const* quest = sBonkQuests.GetUnlockingQuest(type, id))
            bonk->SendAddon(std::string("cdxq~") + kind + "~" + std::to_string(id) + "~" + quest->name + "~" + quest->description);
    }
}

void BonkCodex::Send(Player* player, std::string const& npc)
{
    BonkPlayer* bonk = player->GetBonkPlayer();
    if (!bonk->IsAddonReady())
        return;

    uint32 const account = player->GetSession()->GetAccountId();
    bonk->SendAddon("cdx~" + npc + "~" + std::to_string(player->GetClassMask()));

    auto lockState = [account](BonkQuestReward type, uint32 id, uint32& price)
    {
        price = 0;
        if (sBonkQuests.IsForSale(account, type, id))
        {
            price = sBonkQuests.GetUnlockPrice(account, type, id);
            return "2";
        }
        return sBonkQuests.IsLocked(account, type, id) ? "1" : "0";
    };

    for (auto const& itr : sBonkMgr.GetWeapons())
    {
        BonkWeapon const& weapon = itr.second;
        std::string const id = std::to_string(weapon.id);
        uint32 price;
        std::string const locked = lockState(BONK_REWARD_WEAPON, weapon.id, price);
        bonk->SendAddon("cdxw~" + id + "~" + weapon.name + "~" + weapon.icon + "~" + std::to_string(weapon.classMask) + "~" +
            std::to_string(weapon.starterClassMask | weapon.starterOptionMask) + "~" + std::to_string(weapon.evolvesFrom) + "~" + locked + "~" + std::to_string(price));
        bonk->SendAddon("cdxwd~" + id + "~" + weapon.description);
        if (locked == "1")
            SendLock(bonk, "w", weapon.id, BONK_REWARD_WEAPON);
    }

    for (auto const& itr : sBonkMgr.GetPassives())
    {
        BonkPassive const& passive = itr.second;
        uint32 price;
        std::string const locked = lockState(BONK_REWARD_PASSIVE, passive.id, price);
        bonk->SendAddon("cdxp~" + std::to_string(passive.id) + "~" + passive.name + "~" + passive.icon + "~" +
            std::to_string(passive.maxRank) + "~" + locked + "~" + sBonkMgr.DescribePassive(passive) + "~" + std::to_string(price));
        if (locked == "1")
            SendLock(bonk, "p", passive.id, BONK_REWARD_PASSIVE);
    }

    for (auto const& itr : sBonkMgr.GetCards())
    {
        BonkCard const& card = itr.second;
        if (!card.inChests)
            continue;
        uint32 price;
        std::string const locked = lockState(BONK_REWARD_CARD, card.id, price);
        std::string const id = std::to_string(card.id);
        bonk->SendAddon("cdxc~" + id + "~" + card.name + "~" + card.icon + "~" + std::to_string(uint32(card.rarity)) + "~" +
            std::to_string(uint32(card.maxCopies)) + "~" + locked + "~" + std::to_string(price));
        bonk->SendAddon("cdxcd~" + id + "~" + card.description);
        if (locked == "1")
            SendLock(bonk, "c", card.id, BONK_REWARD_CARD);
    }

    bonk->TrimPoolToggles();
    uint32 const slots = bonk->GetPoolToggleSlots();
    bonk->SendAddon("cdxpool~" + std::string(slots ? "1" : "0") + "~" + std::to_string(sBonkQuests.GetDisabled(account).size()) + "~" +
        std::to_string(slots));
    for (auto const& off : sBonkQuests.GetDisabled(account))
    {
        char const* kind = off.first == BONK_REWARD_WEAPON ? "w" : off.first == BONK_REWARD_PASSIVE ? "p" : off.first == BONK_REWARD_CARD ? "c" : "g";
        bonk->SendAddon("cdxoff~" + std::string(kind) + "~" + std::to_string(off.second));
    }

    for (auto const& itr : sBonkMgr.GetGlyphs())
    {
        uint32 price;
        std::string const locked = lockState(BONK_REWARD_GLYPH, itr.first, price);
        bonk->SendAddon("cdxg~" + std::to_string(itr.first) + "~" + locked + "~" + std::to_string(price));
        if (locked == "1")
            SendLock(bonk, "g", itr.first, BONK_REWARD_GLYPH);
    }

    bonk->SendAddon("cdxend");
}

bool GossipHello_npc_bonk_codex(Player* player, Creature* creature)
{
    BonkCodex::Send(player, player->GetBonkPlayer()->IsAddonReady() ? creature->GetName() : "");
    player->SEND_GOSSIP_MENU(BONK_TEXT_SPELLBOOK, creature->GetObjectGuid());
    return true;
}

void AddSC_bonk_codex()
{
    Script* newscript = new Script;
    newscript->Name = "npc_bonk_codex";
    newscript->pGossipHello = &GossipHello_npc_bonk_codex;
    newscript->RegisterSelf();
}
