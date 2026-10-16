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

    // "a", "a or b", "a, b or c"; "and" in place of "or" for JoinAnd.
    std::string Join(std::vector<std::string> const& parts, char const* last)
    {
        std::string text;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i)
                text += i + 1 == parts.size() ? last : ", ";
            text += parts[i];
        }
        return text;
    }

    std::string JoinOr(std::vector<std::string> const& parts) { return Join(parts, " or "); }
    std::string JoinAnd(std::vector<std::string> const& parts) { return Join(parts, " and "); }

    // How a spell grows rank by rank (each card's rarity decides what it
    // adds, see BonkRun::RollUpgrades), and what it does besides its hit.
    std::string DescribeGrowth(BonkWeapon const& weapon)
    {
        bool const cleave = weapon.behavior == BONK_BEHAVIOR_CLEAVE;
        bool const damageless = weapon.damageEffectIndex < 0 && !weapon.weaponDamage;

        std::string text = "Ranks have no cap; each card's rarity decides what it adds. Common: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_COMMON]) +
            " damage. Uncommon: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_UNCOMMON]) + " damage.";

        // Rare and Epic: damage and steps from the spell's own list.
        std::vector<std::string> upgrades;
        std::vector<std::string> caps;
        for (BonkWeaponUpgrade const& upgrade : weapon.upgrades)
        {
            upgrades.push_back(DescribeBonkSpellUpgrade(weapon, upgrade.upgrade, upgrade.step));
            float const cap = BONK_SPELL_UPGRADE_CAP[upgrade.upgrade];
            if (cap > 0.0f)
                caps.push_back(DescribeBonkSpellUpgrade(weapon, upgrade.upgrade, cap));
        }
        if (upgrades.empty())
            text += " Rare: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_RARE] + BONK_RANK_SPARE_DAMAGE) + " damage. Epic: +" +
                Points(BONK_RANK_DAMAGE[BONK_RARITY_EPIC] + 2 * BONK_RANK_SPARE_DAMAGE) + " damage.";
        else
        {
            text += " Rare: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_RARE]) + " damage and " + JoinOr(upgrades) + ". Epic: +" +
                Points(BONK_RANK_DAMAGE[BONK_RARITY_EPIC]) + " damage and two of these, maybe the same one twice.";
            if (!caps.empty())
                text += " At most " + JoinAnd(caps) + ".";
        }

        // Legendary ranks are evolutions only.
        BonkWeapon const* evolved = weapon.evolvesFrom ? nullptr : sBonkMgr.GetEvolution(weapon.id);
        if (evolved)
            text += " Legendary, from rank " + std::to_string(BONK_EVOLVE_RANK) + " and sure by rank " + std::to_string(BONK_EVOLVE_PITY_RANK) +
                ": evolves into " + evolved->name + ".";

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
                text += " Beasts bite for the spell's damage; Haste makes them bite more often.";
                break;
            case BONK_BEHAVIOR_REVENGE:
                text += " Gives " + Points(weapon.behaviorValue) + " Dodge while you have it.";
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

    // `locked`: 0 owned (or never locked), 1 waiting on its quest, 2 for
    // sale at `price` copper (its quest done, Core loop v2 phase E).
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

    // cdxw~id~name~icon~classMask~starterMask~evolvesFrom~locked~price,
    // then cdxwd~id~description and cdxwg~id~growth. Long text has lines of
    // its own to keep each message short.
    for (auto const& itr : sBonkMgr.GetWeapons())
    {
        BonkWeapon const& weapon = itr.second;
        std::string const id = std::to_string(weapon.id);
        uint32 price;
        std::string const locked = lockState(BONK_REWARD_WEAPON, weapon.id, price);
        bonk->SendAddon("cdxw~" + id + "~" + weapon.name + "~" + weapon.icon + "~" + std::to_string(weapon.classMask) + "~" +
            std::to_string(weapon.starterClassMask) + "~" + std::to_string(weapon.evolvesFrom) + "~" + locked + "~" + std::to_string(price));
        bonk->SendAddon("cdxwd~" + id + "~" + weapon.description);
        bonk->SendAddon("cdxwg~" + id + "~" + DescribeGrowth(weapon));
        if (locked == "1")
            SendLock(bonk, "w", weapon.id, BONK_REWARD_WEAPON);
    }

    // cdxp~id~name~icon~maxRank~locked~description~price
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

    // cdxc~id~name~icon~rarity~maxCopies~locked~price, then
    // cdxcd~id~description: the Darkmoon Cards chests can offer.
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

    // cdxg~id~locked~price: the Glyphs (their text came with glyphinfo).
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
