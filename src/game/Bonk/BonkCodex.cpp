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

#include <map>
#include <vector>

namespace
{
    uint32 const BONK_TEXT_SPELLBOOK = 900006;

    std::string Percent(float value)
    {
        return std::to_string(uint32(value * 100.0f + 0.5f)) + "%";
    }

    std::string Points(float percent)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%g%%", percent);
        return buffer;
    }

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

    std::string DescribeLegendary(BonkWeapon const& weapon)
    {
        std::vector<BonkWeapon const*> const evolutions = sBonkMgr.GetEvolutions(weapon.id);
        if (evolutions.empty())
            return "";
        std::string const rank = std::to_string(BONK_EVOLVE_RANK);
        return " Legendary: from rank " + rank + ", each card of this spell has a " + Points(BONK_LEGENDARY_BASE_CHANCE) +
            " chance to be its evolution, +" + Points(BONK_LEGENDARY_CHANCE_PER_RANK) + " for each rank above " + rank + ", at most " +
            Points(BONK_LEGENDARY_MAX_RANK_CHANCE) + ", plus " + Points(BONK_LEGENDARY_CHANCE_PER_LUCK) + " per point of Luck. It evolves the spell into " +
            sBonkMgr.DescribeEvolutionNames(weapon.id) + (evolutions.size() > 1 ? ", picked at random." : ".");
    }

    std::string DescribeGrowth(BonkWeapon const& weapon)
    {
        bool const cleave = weapon.behavior == BONK_BEHAVIOR_CLEAVE;
        bool const damageless = weapon.damageEffectIndex < 0 && !weapon.weaponDamage;

        std::string text = "Ranks have no limit. Each card's rarity decides what it adds. Common: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_COMMON]) +
            " damage. Uncommon: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_UNCOMMON]) + " damage.";

        std::vector<std::string> upgrades;
        std::string hitTwice;
        std::vector<std::string> caps;
        for (BonkWeaponUpgrade const& upgrade : weapon.upgrades)
        {
            std::string const described = DescribeBonkSpellUpgrade(weapon, upgrade.upgrade, upgrade.step);
            if (upgrade.upgrade == BONK_SPELL_UPGRADE_HIT_TWICE)
                hitTwice = described;
            else
                upgrades.push_back(described);
            float const cap = BONK_SPELL_UPGRADE_CAP[upgrade.upgrade];
            if (cap > 0.0f)
                caps.push_back(DescribeBonkSpellUpgrade(weapon, upgrade.upgrade, cap));
        }
        if (upgrades.empty() && hitTwice.empty())
            text += " Rare: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_RARE] + BONK_RANK_SPARE_DAMAGE) + " damage. Epic: +" +
                Points(BONK_RANK_DAMAGE[BONK_RARITY_EPIC] + 2 * BONK_RANK_SPARE_DAMAGE) + " damage.";
        else
        {
            if (upgrades.empty())
                text += " Rare: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_RARE] + BONK_RANK_SPARE_DAMAGE) + " damage.";
            else
                text += " Rare: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_RARE]) + " damage, plus " +
                    (upgrades.size() == 1 ? upgrades[0] : "one of these: " + JoinOr(upgrades)) + ".";
            text += " Epic: +" + Points(BONK_RANK_DAMAGE[BONK_RARITY_EPIC]) + " damage, plus ";
            if (upgrades.size() >= 2)
                text += "2 of these" + (hitTwice.empty() ? std::string() : " or " + hitTwice) + " (the same one can come up twice).";
            else if (hitTwice.empty())
                text += "that bonus twice.";
            else if (upgrades.empty())
                text += "2 bonuses of " + hitTwice + ".";
            else
                text += "2 bonuses, each " + upgrades[0] + " or " + hitTwice + ".";
            if (!caps.empty())
                text += " These bonuses top out at " + JoinAnd(caps) + ".";
        }

        if (!weapon.evolvesFrom)
            text += DescribeLegendary(weapon);

        if (cleave)
            text += " Its cleave hits enemies within " + std::to_string(uint32(BONK_CLEAVE_RADIUS)) + " yards of the target for " +
                Percent(weapon.behaviorValue) + " of its damage.";
        if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
            text += (weapon.evolvesFrom ? std::string(" When") : " From rank " + std::to_string(BONK_SPREAD_RANK) + ", when") +
                " an affected enemy dies, it spreads to " + std::to_string(uint32(weapon.behaviorValue)) + " enemies within " +
                std::to_string(uint32(BONK_SPREAD_RADIUS)) + " yards.";
        switch (weapon.behavior)
        {
            case BONK_BEHAVIOR_BUFF:
                text += " Damage ranks raise the Power it gives.";
                break;
            case BONK_BEHAVIOR_DEMORALIZE:
                text += " Damage ranks weaken enemies further.";
                break;
            case BONK_BEHAVIOR_MARK:
                if (damageless)
                    text += " Damage ranks make the mark stronger.";
                break;
            case BONK_BEHAVIOR_PET:
                text += " Beasts bite for the spell's damage; Haste makes them bite more often.";
                break;
            case BONK_BEHAVIOR_SHIELD:
            case BONK_BEHAVIOR_AEGIS:
            case BONK_BEHAVIOR_SPIKED_SHIELD:
                text += " Damage ranks make the Shield bigger.";
                break;
            case BONK_BEHAVIOR_DEEP_WOUNDS:
                text += " Count applies its Bleed again on each critical strike.";
                break;
            default:
                break;
        }
        if (damageless)
            if (BonkKeywordInfo const* keyword = sBonkMgr.GetKeyword(weapon.applies))
                if (keyword->IsDamageOverTime())
                    text += " Damage ranks make its " + keyword->name + " deal more damage.";
        if (weapon.target == BONK_TARGET_GROUND)
            text += " Lands on the largest group of enemies.";
        if (weapon.durationMs)
            text += " Lasts " + std::to_string(weapon.durationMs / IN_MILLISECONDS) + " seconds.";
        if (weapon.healPct > 0.0f)
        {
            char heal[16];
            snprintf(heal, sizeof(heal), "%g", weapon.healPct);
            text += std::string(" Heals you for ") + heal + "% of your maximum health " +
                (weapon.behavior == BONK_BEHAVIOR_BLOODTHIRST || weapon.behavior == BONK_BEHAVIOR_DEMORALIZING_SHOUT ? "for each enemy it hits." :
                "each time it is cast.");
        }
        return text;
    }

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
        bonk->SendAddon("cdxwg~" + id + "~" + DescribeGrowth(weapon));
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
