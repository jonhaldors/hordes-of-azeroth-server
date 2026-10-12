/*
 * Vanilla Bonk: the spellbook and its NPC.
 */

#include "BonkCodex.h"
#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "Creature.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "WorldSession.h"

#include <map>
#include <vector>

namespace
{
    uint32 const BONK_TEXT_SPELLBOOK = 900006;

    std::string Percent(float value)
    {
        return std::to_string(uint32(value * 100.0f + 0.5f)) + "%";
    }

    // "+1 target per rank.", "+1 target at ranks 3, 5 and 7.": one more of
    // `what` every `every` ranks; with more than one entry the gains add up
    // ("+2 targets at rank 7").
    std::string DescribeGains(std::string const& what, std::vector<uint32> const& everies, uint8 maxLevel)
    {
        std::map<uint32, uint32> gains;         // rank -> how many more
        for (uint32 every : everies)
            if (every)
                for (uint32 rank = 1 + every; rank <= maxLevel; rank += every)
                    ++gains[rank];
        if (gains.empty())
            return "";

        // Group the ranks by how much they add.
        std::map<uint32, std::vector<uint32>> byAmount;
        for (auto const& itr : gains)
            byAmount[itr.second].push_back(itr.first);

        // The same gain at every rank after the first.
        if (byAmount.size() == 1 && gains.size() + 1 == maxLevel)
        {
            uint32 const amount = byAmount.begin()->first;
            return " +" + std::to_string(amount) + " " + what + (amount > 1 ? "s" : "") + " per rank.";
        }

        std::string text;
        for (auto const& itr : byAmount)
        {
            std::string ranks;
            for (size_t i = 0; i < itr.second.size(); ++i)
            {
                if (i)
                    ranks += i + 1 == itr.second.size() ? " and " : ", ";
                ranks += std::to_string(itr.second[i]);
            }
            text += " +" + std::to_string(itr.first) + " " + what + (itr.first > 1 ? "s" : "") +
                (itr.second.size() == 1 ? " at rank " : " at ranks ") + ranks + ".";
        }
        return text;
    }

    // How a spell grows rank by rank, and what it does besides its hit.
    std::string DescribeGrowth(BonkWeapon const& weapon)
    {
        bool const cleave = weapon.behavior == BONK_BEHAVIOR_CLEAVE;
        std::string text = "Each rank: +" + Percent(BONK_DAMAGE_PER_LEVEL) + " damage, " + Percent(BONK_HASTE_PER_LEVEL) + " faster";
        if (weapon.target == BONK_TARGET_SELF_AREA || weapon.target == BONK_TARGET_GROUND)
            text += ", +" + Percent(BONK_AREA_GROWTH) + " area";
        else if (cleave)
            text += ", +" + Percent(BONK_AREA_GROWTH) + " reach";
        text += ".";

        // More targets: more enemies fired at, and more jumps for spells that
        // leap from one to the next. A cleave's extra victims are their own.
        uint32 const targetEvery = (weapon.target == BONK_TARGET_NEAREST || weapon.target == BONK_TARGET_RANDOM) ? weapon.targetEvery : 0;
        if (cleave)
        {
            text += DescribeGains("target", { targetEvery }, weapon.maxLevel);
            text += DescribeGains("cleaved enemy", { weapon.chainEvery }, weapon.maxLevel);
        }
        else
            text += DescribeGains("target", { targetEvery, weapon.chainEvery }, weapon.maxLevel);

        if (cleave)
            text += " Cleaves hit enemies within " + std::to_string(uint32(BONK_CLEAVE_RADIUS)) + " yards of the target for " +
                Percent(weapon.behaviorValue) + " damage.";
        if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
            text += std::string(weapon.evolvesFrom ? " Spreads" : " At max rank it spreads") + " to " +
                std::to_string(uint32(weapon.behaviorValue)) + " enemies within " + std::to_string(uint32(BONK_SPREAD_RADIUS)) +
                " yards when one carrying it dies.";
        if (weapon.target == BONK_TARGET_GROUND)
            text += " Lands on the biggest pack of enemies.";
        if (weapon.durationMs)
            text += " Lasts " + std::to_string(weapon.durationMs / IN_MILLISECONDS) + " seconds.";
        if (weapon.healPct > 0.0f)
        {
            char heal[16];
            snprintf(heal, sizeof(heal), "%g", weapon.healPct);
            text += std::string(" Heals you ") + heal + "% of your health each time.";
        }
        return text;
    }

    // "cdxq~kind~id~quest name~quest description" for something still locked.
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

    // cdxw~id~name~icon~classMask~starterMask~maxLevel~evolvesFrom~evolvePassive~locked,
    // then cdxwd~id~description and cdxwg~id~growth. Long text has lines of
    // its own to keep each message short.
    for (auto const& itr : sBonkMgr.GetWeapons())
    {
        BonkWeapon const& weapon = itr.second;
        std::string const id = std::to_string(weapon.id);
        bool const locked = sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, weapon.id);
        bonk->SendAddon("cdxw~" + id + "~" + weapon.name + "~" + weapon.icon + "~" + std::to_string(weapon.classMask) + "~" +
            std::to_string(weapon.starterClassMask) + "~" + std::to_string(weapon.maxLevel) + "~" + std::to_string(weapon.evolvesFrom) + "~" +
            std::to_string(weapon.evolvePassive) + "~" + (locked ? "1" : "0"));
        bonk->SendAddon("cdxwd~" + id + "~" + weapon.description);
        bonk->SendAddon("cdxwg~" + id + "~" + DescribeGrowth(weapon));
        if (locked)
            SendLock(bonk, "w", weapon.id, BONK_REWARD_WEAPON);
    }

    // cdxp~id~name~icon~maxRank~locked~description
    for (auto const& itr : sBonkMgr.GetPassives())
    {
        BonkPassive const& passive = itr.second;
        bool const locked = sBonkQuests.IsLocked(account, BONK_REWARD_PASSIVE, passive.id);
        bonk->SendAddon("cdxp~" + std::to_string(passive.id) + "~" + passive.name + "~" + passive.icon + "~" +
            std::to_string(passive.maxRank) + "~" + (locked ? "1" : "0") + "~" + passive.description);
        if (locked)
            SendLock(bonk, "p", passive.id, BONK_REWARD_PASSIVE);
    }

    bonk->SendAddon("cdxend");
}

// The spellbook at camp. With the addon the book opens as a window in place
// of this gossip; without it there is only the greeting.
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
