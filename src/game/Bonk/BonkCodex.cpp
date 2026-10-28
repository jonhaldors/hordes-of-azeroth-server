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
                return "+" + Number(card.values[0] * sum) + "% turkey legs";
            case BONK_CARD_CRIT_HEAL:
                return "up to " + Number(card.values[2] * sum) + "% health a second";
            case BONK_CARD_OVERHEAL:
                return "Shield up to " + Number(card.values[0] * sum) + "%";
            case BONK_CARD_THORNS:
                return Number(card.values[0] * sum) + "% of a normal enemy's health on Normal";
            case BONK_CARD_FIRE_BURST:
            case BONK_CARD_CHAIN:
            case BONK_CARD_DEATH_RAY:
            case BONK_CARD_ECHO:
            case BONK_CARD_FREE_CHEST:
                return chance();
            case BONK_CARD_HASTE_BURST:
                return chance() + ", +" + Number(card.values[1] * sum) + "% Haste";
            case BONK_CARD_KILL_POWER:
            case BONK_CARD_UNHIT_POWER:
                return "up to +" + Number(card.values[1] * sum) + "% Power";
            case BONK_CARD_POWER_SURGE:
                return "+" + Number(card.values[0] * sum) + "% Power";
            case BONK_CARD_BIG_GAME:
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
                text += (text.empty() ? "" : ", ") + DescribeStat(card.stats[i], card.statValues[i] * sum);
        return text;
    }

    void SendLock(BonkPlayer* bonk, char const* kind, uint32 id, BonkQuestReward type)
    {
        if (BonkQuest const* quest = sBonkQuests.GetUnlockingQuest(type, id))
            bonk->SendAddon(std::string("cdxq~") + kind + "~" + std::to_string(id) + "~" + quest->name + "~" + quest->description + "~" +
                std::to_string(sBonkQuests.GetProgress(bonk->GetProgressAccount(), *quest)) + "~" + std::to_string(quest->amount));
    }
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
