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

    // 15 -> "15%": step sizes are already percentages.
    std::string Points(float percent)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%g%%", percent);
        return buffer;
    }

    // "a", "a or b", "a, b or c".
    std::string JoinOr(std::vector<std::string> const& parts)
    {
        std::string text;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i)
                text += i + 1 == parts.size() ? " or " : ", ";
            text += parts[i];
        }
        return text;
    }

    // How a spell grows rank by rank (each card's rarity decides the step,
    // see BonkRun::RollStep), and what it does besides its hit.
    std::string DescribeGrowth(BonkWeapon const& weapon)
    {
        bool const cleave = weapon.behavior == BONK_BEHAVIOR_CLEAVE;
        bool const damageless = weapon.damageEffectIndex < 0 && !weapon.weaponDamage;
        std::string const minor = "+" + Points(BONK_RANK_DAMAGE_MINOR) + " damage and ";

        std::string text = "Ranks have no cap; each card's rarity decides what it adds. Common: +" + Points(BONK_RANK_DAMAGE) + " damage.";

        std::vector<std::string> uncommon = { "fires " + Points(BONK_RANK_HASTE) + " more often" };
        if (weapon.HasArea())
            uncommon.push_back("+" + Points(BONK_RANK_AREA) + (cleave ? " reach" : " area"));
        if (weapon.lasting)
            uncommon.push_back("lasts " + Points(BONK_RANK_DURATION) + " longer");
        text += " Uncommon: " + minor + JoinOr(uncommon) + ".";

        // The same choices as BonkRun::RollStep.
        std::vector<std::string> rare;
        if (weapon.IsAimed())
            rare.push_back("+" + Points(BONK_RANK_RANGE) + " range");
        if (weapon.HasJumps())
        {
            if (cleave)
                rare.push_back("cleaves 1 more enemy");
            else if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
                rare.push_back("spreads to 1 more enemy");
            else
                rare.push_back("hits 1 more enemy");
        }
        if (weapon.IsWide())
            rare.push_back("+" + Points(BONK_RANK_WIDE) + " area");
        if (rare.empty())
            rare.push_back(weapon.lasting ? "lasts " + Points(BONK_RANK_DURATION) + " longer" : "fires " + Points(BONK_RANK_HASTE) + " more often");
        text += " Rare: " + minor + JoinOr(rare) + ".";

        if (!weapon.HasTargets())
            text += " Epic: +" + Points(BONK_RANK_EPIC_DAMAGE) + " damage.";
        else
            text += std::string(" Epic: +1 ") + weapon.GetTargetNoun() + " (up to " + std::to_string(BONK_MAX_EPIC_STEPS) + "), then +" +
                Points(BONK_RANK_EPIC_DAMAGE) + " damage.";

        BonkWeapon const* evolved = weapon.evolvesFrom ? nullptr : sBonkMgr.GetEvolution(weapon.id);
        if (evolved)
            text += " Legendary, from rank " + std::to_string(BONK_EVOLVE_RANK) + " and sure by rank " + std::to_string(BONK_EVOLVE_PITY_RANK) +
                ": evolves into " + evolved->name + ", then +" + Points(BONK_RANK_LEGENDARY_DAMAGE) + " damage.";
        else
            text += " Legendary, from rank " + std::to_string(BONK_EVOLVE_RANK) + ": +" + Points(BONK_RANK_LEGENDARY_DAMAGE) + " damage.";

        if (cleave)
            text += " Cleaves hit enemies within " + std::to_string(uint32(BONK_CLEAVE_RADIUS)) + " yards of the target for " +
                Percent(weapon.behaviorValue) + " damage.";
        if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
            text += (weapon.evolvesFrom ? std::string(" Spreads") : " From rank " + std::to_string(BONK_EVOLVE_RANK) + " it spreads") + " to " +
                std::to_string(uint32(weapon.behaviorValue)) + " enemies within " + std::to_string(uint32(BONK_SPREAD_RADIUS)) +
                " yards when one carrying it dies.";
        // What "damage" means for a spell that deals none of its own.
        switch (weapon.behavior)
        {
            case BONK_BEHAVIOR_BUFF:
                text += " Damage ranks raise the Power it gives.";
                break;
            case BONK_BEHAVIOR_DEMORALIZE:
                text += " Damage ranks make enemies weaker still.";
                break;
            case BONK_BEHAVIOR_MARK:
                if (damageless)
                    text += " Damage ranks make the mark stronger.";
                break;
            case BONK_BEHAVIOR_PET:
                text += " Beasts bite for the spell's damage; faster ranks make them bite more often.";
                break;
            case BONK_BEHAVIOR_REVENGE:
                text += " Gives " + Percent(weapon.behaviorValue) + " Dodge while you have it.";
                break;
            default:
                break;
        }
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

    // cdxw~id~name~icon~classMask~starterMask~evolvesFrom~locked,
    // then cdxwd~id~description and cdxwg~id~growth. Long text has lines of
    // its own to keep each message short.
    for (auto const& itr : sBonkMgr.GetWeapons())
    {
        BonkWeapon const& weapon = itr.second;
        std::string const id = std::to_string(weapon.id);
        bool const locked = sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, weapon.id);
        bonk->SendAddon("cdxw~" + id + "~" + weapon.name + "~" + weapon.icon + "~" + std::to_string(weapon.classMask) + "~" +
            std::to_string(weapon.starterClassMask) + "~" + std::to_string(weapon.evolvesFrom) + "~" + (locked ? "1" : "0"));
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
            std::to_string(passive.maxRank) + "~" + (locked ? "1" : "0") + "~" + sBonkMgr.DescribePassive(passive));
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
