#include "BonkCodex.h"
#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "BonkRun.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Database/DatabaseEnv.h"
#include "GameObject.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "WorldSession.h"

#include <string>

namespace
{
    uint32 const BONK_TEXT_SPELLBOOK = 900006;

    std::string Number(float value)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), value >= 10.0f ? "%.0f" : "%.2f", value);
        std::string text = buffer;
        if (text.find('.') != std::string::npos)
        {
            while (text.back() == '0')
                text.pop_back();
            if (text.back() == '.')
                text.pop_back();
        }
        return text;
    }

    std::string DescribeStat(BonkPassiveEffect stat, float value)
    {
        switch (stat)
        {
            case BONK_PASSIVE_POWER:        return "+" + Number(value) + "% Power";
            case BONK_PASSIVE_HASTE:        return "+" + Number(value) + "% Haste";
            case BONK_PASSIVE_MOVE_SPEED:   return "+" + Number(value) + "% movement speed";
            case BONK_PASSIVE_MAX_HEALTH:   return "+" + Number(value) + "% maximum health";
            case BONK_PASSIVE_RECOVERY:     return Number(value) + "% health a second";
            case BONK_PASSIVE_GROWTH:       return "+" + Number(value) + "% experience";
            case BONK_PASSIVE_AREA:         return "+" + Number(value) + "% Area";
            case BONK_PASSIVE_ARMOR:        return "+" + Number(value) + " Armor";
            case BONK_PASSIVE_DODGE:        return "+" + Number(value) + "% Dodge";
            case BONK_PASSIVE_CRIT:         return "+" + Number(value) + "% crit";
            case BONK_PASSIVE_CRIT_DAMAGE:  return "+" + Number(value) + "% crit damage";
            case BONK_PASSIVE_SHIELD:       return Number(value) + "% Shield";
            case BONK_PASSIVE_AMOUNT:       return "+" + Number(value) + " Count";
            case BONK_PASSIVE_PICKUP:       return "+" + Number(value) + "% pickup range";
            case BONK_PASSIVE_LUCK:         return "+" + Number(value) + " Luck";
            default:                        return "+" + Number(value);
        }
    }

    std::string DescribeCardStack(BonkCard const& card, uint32 copies)
    {
        float const sum = BonkRun::GetCopySum(copies);
        auto chance = [&card, copies]() { return Number(BonkRun::GetStackedChance(card.values[0], copies)) + "% chance"; };
        auto cooldown = [&card, copies]() { return Number(card.cooldownMs * BonkRun::GetCopyWeight(copies) / 1000.0f) + " sec cooldown"; };
        switch (card.effect)
        {
            case BONK_CARD_HEALTHSTONE:
            case BONK_CARD_HIT_IMMUNE:
            case BONK_CARD_SWIFTNESS:
            case BONK_CARD_MAGNET:
                return cooldown();
            case BONK_CARD_BANDAGE:
            case BONK_CARD_CAMPFIRE:
                return Number(card.values[0] * sum) + "% health a second";
            case BONK_CARD_JERKY:
                return "+" + Number(card.values[0] * sum) + "% turkey legs, healing " + Number(card.values[1] * sum) + "% more";
            case BONK_CARD_CRIT_HEAL:
                return chance() + ", up to " + Number(card.values[2] * sum) + "% health a second";
            case BONK_CARD_OVERHEAL:
                return "Shield up to " + Number(card.values[0] * sum) + "%";
            case BONK_CARD_THORNS:
                return Number(card.values[0] * sum) + "% of a normal enemy's health on Normal";
            case BONK_CARD_FIRE_BURST:
            case BONK_CARD_CHAIN:
            case BONK_CARD_DEATH_RAY:
            case BONK_CARD_ECHO:
            case BONK_CARD_FREE_CHEST:
            case BONK_CARD_SHADOW_BURST:
                return chance();
            case BONK_CARD_HASTE_BURST:
                return chance() + ", +" + Number(card.values[1] * sum) + "% Haste";
            case BONK_CARD_KILL_POWER:
            case BONK_CARD_UNHIT_POWER:
                return "up to +" + Number(card.values[1] * sum) + "% Power";
            case BONK_CARD_POWER_SURGE:
                return "+" + Number(card.values[0] * sum) + "% Power";
            case BONK_CARD_BIG_GAME:
                if (card.values[1] > 0.0f)
                    return "+" + Number(card.values[0] * sum) + "% damage, " +
                        Number(std::min(BonkRun::GetChampionMaxReduction(), card.values[1] * sum)) + "% less damage taken";
                return "+" + Number(card.values[0] * sum) + "% damage";
            case BONK_CARD_VALOR:
                return "+" + Number(card.values[0] * sum) + "% Valor";
            case BONK_CARD_DOT_DAMAGE:
                return "+" + Number(card.values[0] * sum) + "% damage over time";
            default:
                break;
        }
        std::string text;
        for (uint8 i = 0; i < 2; ++i)
            if (card.stats[i] < MAX_BONK_PASSIVE_EFFECT)
            {
                float value = card.statValues[i] * BonkRun::GetStatCopySum(card.stats[i], copies);
                float const cap = BonkRun::GetPassiveCap(card.stats[i]);
                if (cap > 0.0f)
                    value = std::min(value, cap);
                text += (text.empty() ? "" : ", ") + DescribeStat(card.stats[i], value);
            }
        return text;
    }

    BonkKeywordInfo const& KeywordOrBlank(BonkKeyword id)
    {
        static BonkKeywordInfo const blank{};
        BonkKeywordInfo const* info = sBonkMgr.GetKeyword(id);
        return info ? *info : blank;
    }

    std::string Seconds(uint32 ms)
    {
        return Number(ms / 1000.0f) + " sec";
    }

    class RulesPage
    {
    public:
        RulesPage(BonkPlayer* bonk, char const* section) : m_bonk(bonk), m_section(section) {}
        RulesPage& Line(std::string const& text)
        {
            return Term("", text);
        }
        RulesPage& Term(std::string const& term, std::string const& text)
        {
            m_bonk->SendAddon("cdxr~" + m_section + "~" + term + "~" + text);
            return *this;
        }

    private:
        BonkPlayer* m_bonk;
        std::string m_section;
    };

    void SendStatRules(BonkPlayer* bonk)
    {
        RulesPage(bonk, "Stats")
            .Term("Power", "+1% damage per point, for you and your summons. Power on gear gives +0.5% per point and multiplies with your other Power.")
            .Term("Max Health", "+1% maximum health per point.")
            .Term("Critical strike chance", "" + Number(BONK_BASE_CRIT) + "% base. Spells crit for 150% damage, attacks for 200%. Above 100%, a hit can crit twice.")
            .Term("Critical strike damage", "+1% critical strike damage per point.")
            .Term("Haste", "shorter cooldowns and faster damage over time. Diminishing returns, up to " + Number(BONK_MAX_HASTE) + "%. Gear gives at most " + Number(BONK_MAX_GEAR_HASTE) + "%. Short Haste boosts go past the cap.")
            .Term("Area", "bigger AoE spells. Diminishing returns, up to " + Number(BONK_MAX_AREA) + "%. Doesn't affect other spells or summons.")
            .Term("Count", "+1 bolt, shot, swing, pet or totem per point, up to " + Number(BONK_MAX_COUNT) + ". Other AoE spells ignore it.")
            .Term("Dodge", "chance to dodge enemy melee attacks, up to " + Number(BONK_MAX_DODGE) + "%. Evasion and Deterrence go past the cap.")
            .Term("Armor", "20 Armor reduces damage taken by 16%, 100 Armor by 43%, and 300 Armor by 60%, the most it can. Each point above 300 gives +0.5% maximum health.")
            .Term("Shield", "absorbs damage up to a share of maximum health. Refills after " + Seconds(BONK_SHIELD_REGEN_DELAY_MS) + " without taking damage. All shields together hold at most 50% of maximum health, 100% with Juggernaut.")
            .Term("Leech", "your hits have a 1% chance per point to heal 1% of maximum health, up to 0.03% of maximum health per second per point. Summon hits don't count.")
            .Term("Health regeneration", "heals 1% of maximum health per second per point. Healing cuts reduce it.")
            .Term("Luck", "rarer level-up, card, glyph and Ley Line choices. +0.1% elite chest chance and +0.1% evolution chance per point.")
            .Term("Pickup Range", "6 yards base, +1% per point, up to 30 yards. Darkmoon Tickets and glyphs always need 3 yards.")
            .Term("Movement Speed", "+1% per point, up to " + Number(BONK_MAX_MOVE_SPEED) + "%. Short speed boosts go past it.")
            .Term("Gold", "more gold. Doesn't affect Valor or Darkmoon Tickets.")
            .Term("Experience", "more experience.")
            .Term("Determination", "each run that ends in death before your first Normal clear gives +" + Number(BONK_DETERMINATION_PER_STACK) + " Power and " + Number(BONK_DETERMINATION_PER_STACK) + "% less damage taken, up to " + std::to_string(uint32(BONK_DETERMINATION_MAX_STACKS)) + " stacks.");
    }

    void SendStackingRules(BonkPlayer* bonk)
    {
        RulesPage(bonk, "Stacking")
            .Line("Stats from gear, cards, glyphs, passives, talents, Training and Ley Lines add together. Caps apply to the total.")
            .Line("Critical strike chance has no cap.")
            .Line("Talent damage bonuses add to Power. Spell ranks, Hunter's Mark, Judged, keyword payoffs and capstones multiply with it.")
            .Line("Vulnerabilities and Sunder Armor multiply with everything else.")
            .Line("Each passive rank and each extra card copy is worth 85% of the one before, never below 10%. Count doesn't shrink, and Rested has no floor.")
            .Line("Two trinkets with the same effect don't stack it.");
    }

    void SendSpellRules(BonkPlayer* bonk)
    {
        RulesPage(bonk, "Spells")
            .Term("Cooldown", "shortened by Haste, including the spell's own Haste upgrades. Never below 0.2 sec. A spell with no target in range waits instead of starting its cooldown.")
            .Term("Ranks", "Common +" + Number(BONK_RANK_DAMAGE[0]) + "% damage, Uncommon +" + Number(BONK_RANK_DAMAGE[1]) + "%, Rare +" + Number(BONK_RANK_DAMAGE[2]) + "% and 1 upgrade, Epic +" + Number(BONK_RANK_DAMAGE[3]) + "% and 2 upgrades. Spells without damage get upgrades instead. Past rank 20 the damage per rank shrinks: rank 40 adds half, rank 80 a quarter.")
            .Line("Hit Twice, Echo and Targets come only from Epic ranks.")
            .Line("Evolutions can appear from rank 10, earlier with Premonition, and keep your rank, damage and upgrades.")
            .Line("Spell hits vary by 7% either way, pet attacks by 10%. Damage over time doesn't vary.")
            .Line("You start with 3 spell slots and 3 passive slots. Sayge sells more, up to 5 spells and 6 passives.");
    }

    void SendDotRules(BonkPlayer* bonk)
    {
        BonkKeywordInfo const& burning = KeywordOrBlank(BONK_KEYWORD_BURNING);
        BonkKeywordInfo const& agony = KeywordOrBlank(BONK_KEYWORD_AGONY);
        RulesPage(bonk, "Damage over time")
            .Term("Base health", "the health of a normal enemy of the zone's level on Normal. It doesn't grow with difficulty, floors, elites or bosses.")
            .Line("Burning, Bleed, Poison, Decay and Moonfire: each stack deals " + Number(burning.valuePerStack * 100.0f) + "% of base health every tick for " + Seconds(burning.durationMs) + ", up to " + std::to_string(uint32(burning.maxStacks)) + " stacks.")
            .Term("Agony", "each stack deals " + Number(agony.valuePerStack * 100.0f) + "% of base health every tick for " + Seconds(agony.durationMs) + ", growing to 3 times as strong over 8 ticks. It doesn't spread on death.")
            .Line("A tick is 1 sec, faster with Haste.")
            .Line("Damage over time can critically strike only with a talent, for half the usual critical strike bonus.")
            .Line("Each new stack refreshes all stacks on that enemy.")
            .Line("Power counts when stacks are added or expire. Vulnerabilities count on every tick.")
            .Line("When an enemy with 2 or more stacks dies, half its stacks spread to the 2 nearest enemies within 8 yards.")
            .Line("Bosses take the same damage per stack; it's a smaller share of their health.")
            .Line("Ticks can trigger Leech. They don't trigger cards or trinkets.");
    }

    void SendKeywordRules(BonkPlayer* bonk)
    {
        BonkKeywordInfo const& chill = KeywordOrBlank(BONK_KEYWORD_CHILL);
        BonkKeywordInfo const& frozen = KeywordOrBlank(BONK_KEYWORD_FROZEN);
        BonkKeywordInfo const& staticCharge = KeywordOrBlank(BONK_KEYWORD_STATIC);
        BonkKeywordInfo const& mark = KeywordOrBlank(BONK_KEYWORD_HUNTERS_MARK);
        BonkKeywordInfo const& dazed = KeywordOrBlank(BONK_KEYWORD_DAZED);
        RulesPage(bonk, "Keywords")
            .Term("Chill", "" + Number(chill.valuePerStack) + "% slow per stack, up to " + std::to_string(uint32(chill.maxStacks)) + ". At " + std::to_string(uint32(chill.maxStacks)) + " stacks the enemy is Frozen.")
            .Term("Frozen", "normal enemies are rooted for " + Seconds(frozen.durationMs) + "; elites and bosses keep moving but count as Frozen. Your Frost spells always crit Frozen enemies, and your critical strikes on them deal +50% critical strike damage.")
            .Term("Static", "at " + std::to_string(uint32(staticCharge.maxStacks)) + " stacks, lightning strikes the enemy and up to 3 enemies within 6 yards for " + Number(staticCharge.valuePerStack * 100.0f) + "% of base health, raised by Power. It can crit.")
            .Term("Hunter's Mark", "marked enemies take " + Number(mark.valuePerStack) + "% more damage from you and your summons, raised by the marking spell's ranks and Mark upgrades up to 30%. Talents add on top. On death it passes to the 2 nearest unmarked enemies within 10 yards.")
            .Term("Judged", "enemies hit by Judgement of the Crusader take 20% more damage from you for 6 sec.")
            .Term("Dazed", "" + Number(dazed.valuePerStack) + "% slow for " + Seconds(dazed.durationMs) + ". Only the strongest slow counts.")
            .Term("Vulnerabilities", "Fire +" + Number(KeywordOrBlank(BONK_KEYWORD_FIRE_VULNERABILITY).valuePerStack) + "%, Shadow +" + Number(KeywordOrBlank(BONK_KEYWORD_SHADOW_VULNERABILITY).valuePerStack) + "%, Nature +" + Number(KeywordOrBlank(BONK_KEYWORD_NATURE_VULNERABILITY).valuePerStack) + "% and Sunder Armor (Physical) +" + Number(KeywordOrBlank(BONK_KEYWORD_SUNDER_ARMOR).valuePerStack) + "% damage taken per stack, up to 5 stacks.");
    }

    void SendControlRules(BonkPlayer* bonk)
    {
        RulesPage(bonk, "Crowd control")
            .Line("Stuns last at most 2 sec, then the enemy can't be stunned for 6 sec. Bosses and mini-bosses can't be stunned.")
            .Line("Roots work on normal enemies only and last at most 2 sec, then 6 sec immune. Frozen shares this timer.")
            .Line("Knockbacks work on normal enemies only, then 6 sec immune. Pulls work on normal enemies only, then 3 sec immune.")
            .Line("Fear and charm don't work on elites or bosses.");
    }

    void SendSummonRules(BonkPlayer* bonk)
    {
        RulesPage(bonk, "Summons and pets")
            .Line("Summons get your Power, critical strike chance and damage, Haste and Movement Speed. They never die and enemies ignore them.")
            .Line("Summons don't get your Area. Their hits don't trigger Leech, trinkets or on-hit cards, but their kills count.")
            .Line("Every summon hit applies its spell's keyword.")
            .Line("Count adds pets, elementals, treants, totems and wolves.")
            .Line("Summons that leave on their own, like snakes, beasts, Stampede, Spirit Pack wolves and warlock demons, last longer with Duration.");
    }

    void SendProcRules(BonkPlayer* bonk)
    {
        RulesPage(bonk, "Procs")
            .Term("Trinkets", "5% chance when a spell is cast, at most once per cooldown. Damage trinkets deal a share of that spell's next hit, more with Count, and can crit. Area doesn't affect them.")
            .Line("Fire Burst, Shadow Burst, Chain Lightning and Death Ray roll when a spell is cast and fire on that spell's next hit.")
            .Line("Procs never trigger other trinkets or cards. Their hits can trigger Leech.")
            .Line("Glyph damage (Storm, Meteor, Shadowmourne) grows with base health, the run's time and Power, not difficulty. Storm always crits.");
    }

    void SendGearRules(BonkPlayer* bonk)
    {
        RulesPage(bonk, "Gear and tiers")
            .Line("Gear gives its listed stats plus set bonuses.")
            .Term("Mythic tiers", "Power, Max Health and Armor grow 12% per tier up to Tier 20, about 8.6 times Tier 1. Past Tier 20, each tier adds 12% of the Tier 20 value. Other stats stay the same.")
            .Line("Trinket effects don't grow with tier; their stats do.")
            .Term("Scrap", "each item you don't extract from a Mythic run gives Scrap, more for higher tiers and bonus stats. Mythic elites drop a little too. The Mythic Quartermaster spends it to raise gear you found by 1 tier, up to Tier 10 or your best Mythic floor if higher. For gold, it rerolls one bonus stat per item.");
    }

    void SendSurvivalRules(BonkPlayer* bonk)
    {
        RulesPage(bonk, "Survival")
            .Line("Damage you take is reduced by Dodge, damage-taken cuts, Armor and then Shield, in that order.")
            .Line("Dash makes you immune for 0.25 sec.")
            .Line("Healing orbs heal 20% of maximum health. Each level gained heals 5%.")
            .Line("Necrotic, boss wounds and Cursed Ground reduce healing taken, up to 100%.")
            .Term("Revives", "when you die, Glyph of Divine Shield saves you first, then Second Chance, then a Soulstone. A revive restores your health, shields you and destroys normal enemies within 15 yards.");
    }

    void SendRules(BonkPlayer* bonk)
    {
        SendStatRules(bonk);
        SendStackingRules(bonk);
        SendSpellRules(bonk);
        SendDotRules(bonk);
        SendKeywordRules(bonk);
        SendControlRules(bonk);
        SendSummonRules(bonk);
        SendProcRules(bonk);
        SendGearRules(bonk);
        SendSurvivalRules(bonk);
    }

    void SendLock(BonkPlayer* bonk, char const* kind, uint32 id, BonkQuestReward type)
    {
        if (BonkQuest const* quest = sBonkQuests.GetUnlockingQuest(type, id))
            bonk->SendAddon(std::string("cdxq~") + kind + "~" + std::to_string(id) + "~" + quest->name + "~" + quest->description + "~" +
                std::to_string(sBonkQuests.GetProgress(bonk->GetProgressAccount(), *quest)) + "~" + std::to_string(quest->amount));
    }
}

std::string BonkCodex::DescribeCardTotal(BonkCard const& card, uint32_t copies)
{
    return DescribeCardStack(card, copies);
}

void BonkCodex::Send(Player* player, std::string const& npc, bool deck)
{
    BonkPlayer* bonk = player->GetBonkPlayer();
    if (!bonk->IsAddonReady())
        return;

    uint32 const account = player->GetBonkPlayer()->GetProgressAccount();
    bonk->SendAddon("cdx~" + npc + "~" + std::to_string(player->GetClassMask()) + "~" + (deck ? "1" : "0"));

    auto lockState = [account](BonkQuestReward type, uint32 id, uint32& price)
    {
        price = 0;
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
        bonk->SendAddon("cdxwd~" + id + "~" + weapon.description + "~" + DescribeBonkWeaponTags(weapon, GetBonkMagicSchoolName(weapon.spellId)));
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
        std::string const one = DescribeCardStack(card, 1);
        if (card.maxCopies != 1 && !one.empty())
            bonk->SendAddon("cdxcs~" + id + "~" + one + "~" + DescribeCardStack(card, 5));
        if (locked == "1")
            SendLock(bonk, "c", card.id, BONK_REWARD_CARD);
    }

    bonk->TrimPoolToggles();
    for (BonkQuestReward type : { BONK_REWARD_CARD, BONK_REWARD_GLYPH })
    {
        char const* kind = type == BONK_REWARD_CARD ? "c" : "g";
        bonk->SendAddon("cdxpool~" + std::string(kind) + "~" + std::to_string(sBonkQuests.CountDisabled(account, type)) + "~" +
            std::to_string(bonk->GetBanSlots(type)));
        for (BonkPlayer::BanSource const& source : bonk->GetBanSources(type))
            bonk->SendAddon("cdxban~" + std::string(kind) + "~" + (source.earned ? "1" : "0") + "~" + std::to_string(source.tickets) + "~" +
                source.label + "~" + source.detail);
    }
    bonk->SendAddon("cdxtickets~" + std::to_string(bonk->GetShards()));
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

    for (auto const& itr : sBonkMgr.GetKeywords())
    {
        BonkKeywordInfo const& keyword = itr.second;
        if (!keyword.description.empty())
            bonk->SendAddon("cdxk~" + std::to_string(uint32(keyword.id)) + "~" + keyword.name + "~" + keyword.icon + "~" +
                std::to_string(uint32(keyword.maxStacks)) + "~" + std::to_string(keyword.durationMs) + "~" + keyword.description);
    }

    SendRules(bonk);
    bonk->SendAddon("cdxend");
}

void BonkCodex::SendTermGlossary(BonkPlayer* bonk)
{
    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `term`, `words`, `keyword`, `description` FROM `bonk_glossary` ORDER BY `sort`, `term`"));
    if (!result)
        return;
    do
    {
        Field* fields = result->Fetch();
        std::string description = fields[3].GetCppString();
        if (uint32 const keywordId = fields[2].GetUInt32())
            if (BonkKeywordInfo const* keyword = sBonkMgr.GetKeyword(BonkKeyword(keywordId)))
                description = keyword->description;
        if (!description.empty())
            bonk->SendAddon("gls~" + fields[0].GetCppString() + "~" + fields[1].GetCppString() + "~" + description);
    } while (result->NextRow());
}

void BonkCodex::SendKeywordGlossary(BonkPlayer* bonk)
{
    for (auto const& itr : sBonkMgr.GetKeywords())
    {
        BonkKeywordInfo const& keyword = itr.second;
        std::string const id = std::to_string(uint32(keyword.id));
        if (!keyword.description.empty())
            bonk->SendAddon("kwd~" + id + "~" + keyword.name + "~" + keyword.icon + "~" + keyword.description);
        bonk->SendAddon("kwdaura~" + id + "~" + keyword.name + "~" + keyword.icon + "~" + std::to_string(keyword.spellId) + "~" +
            std::to_string(uint32(keyword.maxStacks)));
    }
    SendTermGlossary(bonk);
}

namespace
{
    uint32 const BONK_SHELF_BROWSE_MS = 10000;
    uint32 const BONK_SHELF_KNEEL_MS  = 4000;
    uint32 const BONK_SHELF_PONDER_MS = 3000;

    class BonkShelfKeeperAI : public CreatureAI
    {
        public:
            explicit BonkShelfKeeperAI(Creature* creature) : CreatureAI(creature)
            {
                Browse();
            }

            void MoveInLineOfSight(Unit*) override {}
            void AttackStart(Unit*) override {}
            void EnterEvadeMode() override {}

            void UpdateAI(uint32 const diff) override
            {
                if (m_timer > diff)
                {
                    m_timer -= diff;
                    return;
                }
                switch (m_step)
                {
                    case STEP_BROWSE:
                        Kneel();
                        break;
                    case STEP_KNEEL:
                        Ponder();
                        break;
                    default:
                        Browse();
                        break;
                }
            }

        private:
            enum Step
            {
                STEP_BROWSE,
                STEP_KNEEL,
                STEP_PONDER
            };

            void Browse()
            {
                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                m_creature->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_USESTANDING_NOSHEATHE);
                m_step = STEP_BROWSE;
                m_timer = BONK_SHELF_BROWSE_MS;
            }

            void Kneel()
            {
                m_creature->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
                m_creature->SetStandState(UNIT_STAND_STATE_KNEEL);
                m_step = STEP_KNEEL;
                m_timer = BONK_SHELF_KNEEL_MS;
            }

            void Ponder()
            {
                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                m_creature->HandleEmote(EMOTE_ONESHOT_QUESTION);
                m_step = STEP_PONDER;
                m_timer = BONK_SHELF_PONDER_MS;
            }

            Step m_step = STEP_BROWSE;
            uint32 m_timer = 0;
    };
}

CreatureAI* GetAI_npc_bonk_codex(Creature* creature)
{
    return new BonkShelfKeeperAI(creature);
}

bool GossipHello_npc_bonk_codex(Player* player, Creature* creature)
{
    player->GetBonkPlayer()->VisitSpellbook(creature);
    BonkCodex::Send(player, player->GetBonkPlayer()->IsAddonReady() ? creature->GetName() : "");
    player->SEND_GOSSIP_MENU(BONK_TEXT_SPELLBOOK, creature->GetObjectGuid());
    return true;
}

bool GOHello_go_bonk_deck(Player* player, GameObject* go)
{
    BonkPlayer* bonk = player->GetBonkPlayer();
    if (bonk->GetRun())
        return true;
    bonk->VisitDeck(go);
    BonkCodex::Send(player, go->GetName(), true);
    return true;
}

void AddSC_bonk_codex()
{
    Script* newscript = new Script;
    newscript->Name = "npc_bonk_codex";
    newscript->pGossipHello = &GossipHello_npc_bonk_codex;
    newscript->GetAI = &GetAI_npc_bonk_codex;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name = "go_bonk_deck";
    newscript->pGOHello = &GOHello_go_bonk_deck;
    newscript->RegisterSelf();
}
