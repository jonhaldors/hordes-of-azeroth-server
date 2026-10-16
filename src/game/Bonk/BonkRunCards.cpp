/*
 * Vanilla Bonk: Darkmoon Cards (Core loop v2, phase C).
 *
 * Run-only items that stack, Megabonk's items in WoW dress: chests offer
 * them (a choice of three, more from an elite with affixes, rarity rolled
 * with Luck), level-ups never do. A card adds up to two stats, which count
 * like a passive's ranks (GetPassiveTotal), and may carry one effect
 * (BonkCardEffect): heals, a moment's immunity, cheating death, procs on the
 * hero's hits, more Valor. Copies add up; chances stack with diminishing
 * returns, cooldowns shorten. Cards last the run and are never extracted.
 *
 * Addon: the chest choice is the level-up's offer with kind c (BonkRun's
 * SendOffer); "cards~id:copies:rarity:icon,..." lists the cards held, and a
 * "cardtip~id~name~description" line each follows. `.bonk card <id>` gives
 * one for testing.
 */

#include "BonkRun.h"
#include "BonkMgr.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "Creature.h"
#include "Item.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Utilities/Random.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace
{
    // At least one card of a chest's choice is this good, when one is left.
    BonkRarity const BONK_CARD_CHEST_MIN_RARITY = BONK_RARITY_UNCOMMON;

    // A card's hits name a spell of their own, so the results screen shows
    // them as their own rows, and they never set off cards again.
    uint32 const BONK_SPELL_CARD_FIRE      = 13342;     // Fire Blast: Hand of Ragnaros
    uint32 const BONK_SPELL_CARD_CHAIN_THREE = 22355;   // Chain Lightning, 3 targets: Darkmoon Card: Maelstrom
    uint32 const BONK_SPELL_CARD_CHAIN_FIVE  = 25021;   // Chain Lightning, 5 targets: Thunderfury
    uint32 const BONK_SPELL_CARD_THORNS    = 467;       // Thorns: Barbed Choker
    uint32 const BONK_SPELL_CARD_NOVA      = 11307;     // Fire Nova: Force Reactive Disk
    uint32 const BONK_SPELL_CARD_SHADOW    = 16583;     // Shadow Shock: Corrupted Ashbringer
    uint32 const BONK_SPELL_HEALTHSTONE    = 11732;     // Major Healthstone: names the heal in combat text
    float const BONK_SHADOW_BURST_HEAL     = 0.1f;      // share of a shadow burst's damage that heals the hero

    // Spell visual kits played where a card strikes (SpellVisual.dbc).
    uint32 const BONK_KIT_FIRE_IMPACT      = 728;       // Fire Blast's impact
    uint32 const BONK_KIT_NOVA             = 984;       // Fire Nova's cast, on the hero
    uint32 const BONK_KIT_DEATH_RAY        = 419;       // an arcane burst on the target
    uint32 const BONK_KIT_SHADOW_IMPACT    = 2350;      // Shadow Shock's impact

    bool IsCardSpell(SpellEntry const* spell)
    {
        if (!spell)
            return false;
        switch (spell->Id)
        {
            case BONK_SPELL_CARD_FIRE:
            case BONK_SPELL_CARD_CHAIN_THREE:
            case BONK_SPELL_CARD_CHAIN_FIVE:
            case BONK_SPELL_CARD_THORNS:
            case BONK_SPELL_CARD_NOVA:
            case BONK_SPELL_CARD_SHADOW:
                return true;
            default:
                return false;
        }
    }

    // Diminishing returns: each copy rolls on its own; any of them is enough.
    float StackChance(float percent, uint32 copies)
    {
        if (!copies || percent <= 0.0f)
            return 0.0f;
        return 100.0f * (1.0f - std::pow(1.0f - std::min(percent, 100.0f) / 100.0f, float(copies)));
    }
}

std::pair<bool, uint32> BonkRun::GetBanishKey(Offer const& offer)
{
    // Card ids live apart from passive ids.
    uint32 const BONK_CARD_BANISH_OFFSET = 1000000;
    return { offer.isWeapon, offer.isCard ? BONK_CARD_BANISH_OFFSET + offer.id : offer.id };
}

uint32 BonkRun::GetCardCopies(uint32 cardId) const
{
    for (OwnedCard const& owned : m_cards)
        if (owned.card->id == cardId)
            return owned.copies;
    return 0;
}

float BonkRun::GetCardTotal(BonkCardEffect effect, uint8 index) const
{
    // Every card with the effect adds its own amount (a chest card and a
    // worn legendary can share one).
    float total = 0.0f;
    if (index < 3 && m_cardCopies[effect])
        for (OwnedCard const& owned : m_cards)
            if (owned.card->effect == effect)
                total += owned.card->values[index] * owned.copies;
    return total;
}

float BonkRun::GetCardChance(BonkCardEffect effect) const
{
    // Each copy of each card rolls on its own; any of them is enough.
    if (!m_cardCopies[effect])
        return 0.0f;
    float miss = 1.0f;
    for (OwnedCard const& owned : m_cards)
        if (owned.card->effect == effect)
            miss *= 1.0f - StackChance(owned.card->values[0], owned.copies) / 100.0f;
    return 100.0f * (1.0f - miss);
}

uint32 BonkRun::GetCardCooldown(BonkCardEffect effect) const
{
    BonkCard const* card = m_cardByEffect[effect];
    if (!card)
        return 0;
    // Each copy past the first takes value3 % off what is left.
    float const step = std::max(0.0f, std::min(card->values[2], 90.0f)) / 100.0f;
    return uint32(card->cooldownMs * std::pow(1.0f - step, float(m_cardCopies[effect] - 1)));
}

float BonkRun::GetCardPower(Unit const* victim) const
{
    float power = 0.0f;

    // Bloodlust Brooch: kills since it was taken, up to its cap a copy.
    if (BonkCard const* brooch = m_cardByEffect[BONK_CARD_KILL_POWER])
        power += std::min(brooch->values[1], brooch->values[0] * m_cardKills) * m_cardCopies[BONK_CARD_KILL_POWER];

    // Zandalarian Hero Charm: a surge now and then.
    if (m_cardSurgeUntilMs > m_totalMs)
        power += GetCardTotal(BONK_CARD_POWER_SURGE, 0);

    // Mark of the Champion: elites, mini-bosses and bosses.
    if (HasCard(BONK_CARD_BIG_GAME) && victim && victim->IsCreature())
        if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(static_cast<Creature const*>(victim)->AI()))
            if (ai->GetKind() != BONK_SPAWN_NORMAL)
                power += GetCardTotal(BONK_CARD_BIG_GAME, 0);

    return power;
}

bool BonkRun::IsCardInPool(BonkCard const& card) const
{
    // A legendary's own card never comes from chests; the rest once the
    // account has it (the starter set, or bought after its quest).
    return card.inChests && !sBonkQuests.IsLocked(m_player->GetSession()->GetAccountId(), BONK_REWARD_CARD, card.id);
}

void BonkRun::GiveStartCard()
{
    // Lucky Draw: a common card, an uncommon one from its second rank.
    float const draw = GetUtility(BONK_UTILITY_START_CARD);
    if (draw < 1.0f)
        return;
    BonkRarity const rarity = draw >= 2.0f ? BONK_RARITY_UNCOMMON : BONK_RARITY_COMMON;
    std::vector<BonkCard const*> pool;
    for (auto const& itr : sBonkMgr.GetCards())
        if (itr.second.rarity == rarity && IsCardInPool(itr.second) && itr.second.effect != BONK_CARD_MAGNET)
            pool.push_back(&itr.second);
    if (pool.empty())
        return;
    BonkCard const& card = *pool[urand(0, uint32(pool.size()) - 1)];
    AddCard(card);
    m_player->GetBonkPlayer()->Notify("Lucky Draw: " + card.name + "!");
}

bool BonkRun::OfferCards(size_t offerSize, std::vector<Offer> const* avoid, std::vector<Offer> const* kept, std::vector<Offer>* drawn)
{
    // Locked cards stay: from this choice on a reroll or banish, or from the
    // last chest.
    std::vector<Offer> cards;
    std::vector<Offer> carried;
    if (kept)
        carried = *kept;
    else
    {
        carried = m_lockedOffers;
        m_lockedOffers.clear();
    }
    for (Offer offer : carried)
        if (cards.size() < offerSize && RefreshKeptOffer(offer))
        {
            offer.locked = true;
            cards.push_back(offer);
        }

    auto makeOffer = [this](BonkCard const& card)
    {
        Offer offer;
        offer.isWeapon = false;
        offer.isCard = true;
        offer.id = card.id;
        offer.rarity = card.rarity;
        offer.newLevel = GetCardCopies(card.id) + 1;
        return offer;
    };
    auto shown = [&cards](uint32 id)
    {
        return std::any_of(cards.begin(), cards.end(), [id](Offer const& card) { return card.isCard && card.id == id; });
    };

    // Fresh cards first; ones just shown (a reroll) only when those run out.
    std::vector<BonkCard const*> fresh;
    std::vector<BonkCard const*> repeats;
    for (auto const& itr : sBonkMgr.GetCards())
    {
        BonkCard const& card = itr.second;
        Offer probe;
        probe.isWeapon = false;
        probe.isCard = true;
        probe.id = card.id;
        if (!IsCardInPool(card) || m_banished.count(GetBanishKey(probe)) || shown(card.id) || (card.maxCopies && GetCardCopies(card.id) >= card.maxCopies))
            continue;
        bool const avoided = avoid && std::any_of(avoid->begin(), avoid->end(), [&](Offer const& offer) { return offer.isCard && offer.id == card.id; });
        (avoided ? repeats : fresh).push_back(&card);
    }

    // Each slot rolls a rarity with Luck, then a card of it, or the nearest
    // rarity that has one left.
    for (std::vector<BonkCard const*>* pool : { &fresh, &repeats })
        while (cards.size() < offerSize && !pool->empty())
        {
            BonkRarity const rolled = RollRarity(BONK_RARITY_COMMON);
            std::vector<size_t> picks;
            for (int32 distance = 0; distance < MAX_BONK_RARITY && picks.empty(); ++distance)
                for (size_t i = 0; i < pool->size(); ++i)
                    if (std::abs(int32((*pool)[i]->rarity) - int32(rolled)) == distance)
                        picks.push_back(i);
            size_t const chosen = picks[urand(0, uint32(picks.size() - 1))];
            cards.push_back(makeOffer(*(*pool)[chosen]));
            pool->erase(pool->begin() + chosen);
        }

    // Never only Commons, when something better is left.
    if (std::none_of(cards.begin(), cards.end(), [](Offer const& card) { return card.rarity >= BONK_CARD_CHEST_MIN_RARITY; }))
    {
        std::vector<BonkCard const*> better;
        for (std::vector<BonkCard const*> const* pool : { &fresh, &repeats })
            for (BonkCard const* card : *pool)
                if (card->rarity >= BONK_CARD_CHEST_MIN_RARITY)
                    better.push_back(card);
        auto replaced = std::find_if(cards.rbegin(), cards.rend(), [](Offer const& card) { return !card.locked; });
        if (!better.empty() && replaced != cards.rend())
            *replaced = makeOffer(*better[urand(0, uint32(better.size() - 1))]);
    }

    if (drawn)
    {
        *drawn = cards;
        return !cards.empty();
    }

    m_offer = cards;
    if (m_offer.empty())
    {
        // Every card banished: a bag of gold instead.
        if (!m_pendingChestChoices.empty())
            m_pendingChestChoices.erase(m_pendingChestChoices.begin());
        GiveGold(m_map.goldPerKill * 25);
        m_player->GetBonkPlayer()->Notify("No cards left in the deck - have some gold!");
        return false;
    }

    m_offerSource = OFFER_CHEST;
    SetPaused(true);
    SendOffer();
    return true;
}

void BonkRun::AddCard(BonkCard const& card)
{
    auto owned = std::find_if(m_cards.begin(), m_cards.end(), [&card](OwnedCard const& held) { return held.card->id == card.id; });
    if (owned == m_cards.end())
        m_cards.push_back({ &card, 1 });
    else
        ++owned->copies;

    for (uint8 i = 0; i < 2; ++i)
        if (card.stats[i] < MAX_BONK_PASSIVE_EFFECT)
            m_cardStats[card.stats[i]] += card.statValues[i];

    if (card.effect != BONK_CARD_NONE)
    {
        ++m_cardCopies[card.effect];
        // The rarest card with the effect sets its size and reach.
        if (!m_cardByEffect[card.effect] || card.rarity >= m_cardByEffect[card.effect]->rarity)
            m_cardByEffect[card.effect] = &card;
    }
    switch (card.effect)
    {
        case BONK_CARD_SOULSTONE:
            ++m_soulstones;
            break;
        case BONK_CARD_CRIT_HEAL:
            // Only crits from now on.
            if (m_cardCopies[card.effect] == 1)
                m_cardCritsSeen = m_crits;
            break;
        default:
            break;
    }

    // New stats show at once (health, speed, dodge and the rest).
    ApplyPassives();
    SendCards();
}

void BonkRun::RemoveCard(BonkCard const& card)
{
    auto owned = std::find_if(m_cards.begin(), m_cards.end(), [&card](OwnedCard const& held) { return held.card->id == card.id; });
    if (owned == m_cards.end())
        return;
    if (--owned->copies == 0)
        m_cards.erase(owned);

    for (uint8 i = 0; i < 2; ++i)
        if (card.stats[i] < MAX_BONK_PASSIVE_EFFECT)
            m_cardStats[card.stats[i]] -= card.statValues[i];

    if (card.effect != BONK_CARD_NONE && m_cardCopies[card.effect])
    {
        --m_cardCopies[card.effect];
        // The rarest card left with the effect, if any.
        m_cardByEffect[card.effect] = nullptr;
        for (OwnedCard const& held : m_cards)
            if (held.card->effect == card.effect && (!m_cardByEffect[card.effect] || held.card->rarity >= m_cardByEffect[card.effect]->rarity))
                m_cardByEffect[card.effect] = held.card;
    }

    ApplyPassives();
    SendCards();
}

void BonkRun::SyncGearCards()
{
    std::vector<uint32> worn;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (uint32 const cardId = sBonkMgr.GetItemCard(item->GetEntry()))
                worn.push_back(cardId);
    std::sort(worn.begin(), worn.end());
    if (worn == m_gearCards)
        return;

    std::vector<uint32> gone, added;
    std::set_difference(m_gearCards.begin(), m_gearCards.end(), worn.begin(), worn.end(), std::back_inserter(gone));
    std::set_difference(worn.begin(), worn.end(), m_gearCards.begin(), m_gearCards.end(), std::back_inserter(added));
    m_gearCards = worn;
    for (uint32 cardId : gone)
        if (BonkCard const* card = sBonkMgr.GetCard(cardId))
            RemoveCard(*card);
    for (uint32 cardId : added)
        if (BonkCard const* card = sBonkMgr.GetCard(cardId))
            AddCard(*card);
}

bool BonkRun::GiveCard(uint32 cardId, std::string& error)
{
    BonkCard const* card = sBonkMgr.GetCard(cardId);
    if (!card)
    {
        error = "No card " + std::to_string(cardId) + ".";
        return false;
    }
    AddCard(*card);
    m_player->GetBonkPlayer()->Notify(card->name + "!");
    return true;
}

void BonkRun::UpdateCards(uint32 diff)
{
    if (m_cards.empty() || !m_player->IsAlive())
        return;

    for (int32& cooldown : m_cardCooldowns)
        if (cooldown > 0)
            cooldown -= int32(diff);

    float const health = m_player->GetHealthPercent();
    uint32 const maxHealth = m_player->GetMaxHealth();

    // Major Healthstone: a big heal when health runs low, on a cooldown.
    if (BonkCard const* stone = m_cardByEffect[BONK_CARD_HEALTHSTONE])
        if (m_cardCooldowns[BONK_CARD_HEALTHSTONE] <= 0 && health < stone->values[0])
        {
            HealHero(ReduceHealing(uint32(maxHealth * stone->values[1] / 100.0f)), BONK_SPELL_HEALTHSTONE);
            StartCardCooldown(BONK_CARD_HEALTHSTONE, GetCardCooldown(BONK_CARD_HEALTHSTONE));
        }

    // Swiftness Potion: running speed when health runs low; the speed rides
    // on the card stats until it wears off.
    if (BonkCard const* potion = m_cardByEffect[BONK_CARD_SWIFTNESS])
    {
        if (!m_cardSwiftUntilMs && m_cardCooldowns[BONK_CARD_SWIFTNESS] <= 0 && health < potion->values[0])
        {
            m_cardSwiftUntilMs = m_totalMs + uint32(potion->values[2]);
            StartCardCooldown(BONK_CARD_SWIFTNESS, potion->cooldownMs);
            m_cardStats[BONK_PASSIVE_MOVE_SPEED] += potion->values[1];
            ApplyPassives();
        }
        else if (m_cardSwiftUntilMs && m_totalMs >= m_cardSwiftUntilMs)
        {
            m_cardSwiftUntilMs = 0;
            m_cardStats[BONK_PASSIVE_MOVE_SPEED] -= potion->values[1];
            ApplyPassives();
        }
    }

    // Zandalarian Hero Charm: a surge of Power every so often.
    if (BonkCard const* charm = m_cardByEffect[BONK_CARD_POWER_SURGE])
        if (m_cardCooldowns[BONK_CARD_POWER_SURGE] <= 0)
        {
            m_cardSurgeUntilMs = m_totalMs + uint32(charm->values[1]);
            StartCardCooldown(BONK_CARD_POWER_SURGE, charm->cooldownMs);
            m_player->GetBonkPlayer()->Notify(charm->name + "!");
        }

    // Gnomish Magnet: every XP orb flies in.
    if (HasCard(BONK_CARD_MAGNET) && m_cardCooldowns[BONK_CARD_MAGNET] <= 0)
    {
        PullAllOrbs();
        StartCardCooldown(BONK_CARD_MAGNET, GetCardCooldown(BONK_CARD_MAGNET));
    }

    // Basic Campfire watches for the hero standing still.
    if (m_player->IsMoving())
        m_stillSinceMs = m_totalMs;

    // Once a second: the heals that tick.
    m_cardTickMs += diff;
    if (m_cardTickMs < IN_MILLISECONDS)
        return;
    m_cardTickMs -= IN_MILLISECONDS;

    float percent = 0.0f;
    // Heavy Runecloth Bandage: unhit for a while.
    if (BonkCard const* bandage = m_cardByEffect[BONK_CARD_BANDAGE])
        if (!m_hurt || m_totalMs - m_lastHurtMs >= uint32(bandage->values[1] * IN_MILLISECONDS))
            percent += GetCardTotal(BONK_CARD_BANDAGE, 0);
    // Basic Campfire: standing still for a moment.
    if (BonkCard const* campfire = m_cardByEffect[BONK_CARD_CAMPFIRE])
        if (m_totalMs - m_stillSinceMs >= uint32(campfire->values[1] * IN_MILLISECONDS))
            percent += GetCardTotal(BONK_CARD_CAMPFIRE, 0);
    // Darkmoon Card: Heroism: last second's crits, each with its chance to
    // heal, together up to the cap.
    if (BonkCard const* heroism = m_cardByEffect[BONK_CARD_CRIT_HEAL])
    {
        uint32 const crits = m_crits - m_cardCritsSeen;
        m_cardCritsSeen = m_crits;
        percent += std::min(crits * GetCardChance(BONK_CARD_CRIT_HEAL) / 100.0f * heroism->values[1], heroism->values[2]);
    }
    if (percent > 0.0f)
        HealHero(ReduceHealing(std::max(1u, uint32(maxHealth * percent / 100.0f))));

    SendCardStatus(false);
}

std::string BonkRun::DescribeCardNow(BonkCard const& card) const
{
    auto percent = [](float value)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%.2f", value);
        std::string text = buffer;
        while (!text.empty() && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.')
            text.pop_back();
        return text;
    };

    switch (card.effect)
    {
        case BONK_CARD_KILL_POWER:
        {
            float const power = std::min(card.values[1], card.values[0] * m_cardKills) * GetCardCopies(card.id);
            return "Now: +" + percent(power) + "% Power";
        }
        case BONK_CARD_POWER_SURGE:
            if (m_cardSurgeUntilMs > m_totalMs)
                return "Active: +" + percent(GetCardTotal(BONK_CARD_POWER_SURGE, 0)) + "% Power, " +
                    std::to_string((m_cardSurgeUntilMs - m_totalMs + 999) / IN_MILLISECONDS) + " sec left";
            return "";
        case BONK_CARD_SOULSTONE:
            return std::to_string(m_soulstones) + (m_soulstones == 1 ? " charge left" : " charges left");
        default:
            return "";
    }
}

void BonkRun::SendCardStatus(bool force) const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    for (OwnedCard const& owned : m_cards)
    {
        std::string const text = DescribeCardNow(*owned.card);
        auto sent = m_cardNowSent.find(owned.card->id);
        if (!force && (sent != m_cardNowSent.end() ? sent->second == text : text.empty()))
            continue;
        m_cardNowSent[owned.card->id] = text;
        bonk->SendAddon("cardnow~" + std::to_string(owned.card->id) + "~" + text);
    }
}

void BonkRun::HealHero(uint32 amount, uint32 textSpellId)
{
    if (!amount || !m_player->IsAlive())
        return;

    uint32 const maxHealth = m_player->GetMaxHealth();
    uint32 const healed = std::min(amount, maxHealth - std::min(maxHealth, m_player->GetHealth()));
    if (healed)
    {
        if (SpellEntry const* text = textSpellId ? sSpellMgr.GetSpellEntry(textSpellId) : nullptr)
            m_player->DealHeal(m_player, healed, text);
        else
            m_player->ModifyHealth(int32(healed));
    }

    // Lifegiving Gem: what overflows tops up a Shield, to its cap.
    if (amount > healed && HasCard(BONK_CARD_OVERHEAL))
    {
        uint32 const cap = uint32(maxHealth * GetCardTotal(BONK_CARD_OVERHEAL, 0) / 100.0f);
        if (m_spellShield < cap)
            AddSpellShield(std::min(amount - healed, cap - m_spellShield));
    }
}

void BonkRun::OnCardHitTaken()
{
    // Insignia of the Alliance: a moment immune after a hit, on a cooldown.
    if (BonkCard const* insignia = m_cardByEffect[BONK_CARD_HIT_IMMUNE])
        if (m_cardCooldowns[BONK_CARD_HIT_IMMUNE] <= 0)
        {
            m_cardImmuneUntilMs = m_totalMs + uint32(insignia->values[0]);
            StartCardCooldown(BONK_CARD_HIT_IMMUNE, GetCardCooldown(BONK_CARD_HIT_IMMUNE));
        }
}

void BonkRun::OnShieldBroken()
{
    // Force Reactive Disk: the Shield breaking scorches everything close.
    BonkCard const* disk = m_cardByEffect[BONK_CARD_SHIELD_NOVA];
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_CARD_NOVA);
    if (!disk || !spellInfo || m_cardProc)
        return;

    float const damage = GetSpawnHealth(BONK_SPAWN_NORMAL, BONK_ARCHETYPE_GRUNT, 1.0f) * GetCardTotal(BONK_CARD_SHIELD_NOVA, 0);
    m_player->SendPlaySpellVisualKit(BONK_KIT_NOVA);
    m_cardProc = true;
    for (Creature* mob : GetMobsInRange(disk->values[1]))
        DealSpellHit(spellInfo, mob, damage);
    m_cardProc = false;
}

bool BonkRun::RollFreeChest() const
{
    return HasCard(BONK_CARD_FREE_CHEST) && roll_chance_f(GetCardChance(BONK_CARD_FREE_CHEST));
}

void BonkRun::OnHeroHitBy(Creature* attacker, uint32 damage)
{
    // Barbed Choker: a share of the hit goes back.
    if (!damage || !attacker || !attacker->IsAlive() || !HasCard(BONK_CARD_THORNS) || m_cardProc)
        return;
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_CARD_THORNS);
    if (!spellInfo)
        return;
    m_cardProc = true;
    DealSpellHit(spellInfo, attacker, damage * GetCardTotal(BONK_CARD_THORNS, 0) / 100.0f);
    m_cardProc = false;
}

void BonkRun::ModifyHeroHit(Unit* victim, uint32& damage)
{
    // The Glyph of Soul Harvest: an empowered hit.
    if (!m_cardProc)
        ModifyGlyphHit(victim, damage);
    // Gnomish Death Ray: now and then a hit lands many times over.
    if (!damage || m_cardProc || !HasCard(BONK_CARD_DEATH_RAY) || !roll_chance_f(GetCardChance(BONK_CARD_DEATH_RAY)))
        return;
    damage = uint32(damage * m_cardByEffect[BONK_CARD_DEATH_RAY]->values[1]);
    victim->SendPlaySpellVisualKit(BONK_KIT_DEATH_RAY);
}

void BonkRun::OnHeroHit(Unit* victim, uint32 damage, SpellEntry const* spell)
{
    if (!damage || m_cardProc || IsCardSpell(spell) || m_cards.empty())
        return;

    // A card's damage is worked out from the hit with the hero's Power taken
    // back out: DealSpellHit puts it in again.
    float const multiplier = std::max(0.1f, GetDamageMultiplier(nullptr, victim));

    // A burst strikes its target too (GetMobsInRange leaves the centre out).
    auto burstTargets = [this, victim](float radius)
    {
        std::vector<Creature*> mobs = GetMobsInRange(radius, victim);
        if (victim->IsAlive() && victim->IsCreature())
            mobs.push_back(static_cast<Creature*>(victim));
        return mobs;
    };

    // Hand of Ragnaros: the hit erupts around its target.
    if (BonkCard const* hand = m_cardByEffect[BONK_CARD_FIRE_BURST])
        if (roll_chance_f(GetCardChance(BONK_CARD_FIRE_BURST)))
            if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_CARD_FIRE))
            {
                float const burst = damage * hand->values[1] / multiplier;
                m_cardProc = true;
                for (Creature* mob : burstTargets(hand->values[2]))
                {
                    mob->SendPlaySpellVisualKit(BONK_KIT_FIRE_IMPACT);
                    DealSpellHit(spellInfo, mob, burst);
                }
                m_cardProc = false;
            }

    // Corrupted Ashbringer: shadow round the target, a tenth of it back as health.
    if (BonkCard const* blade = m_cardByEffect[BONK_CARD_SHADOW_BURST])
        if (roll_chance_f(GetCardChance(BONK_CARD_SHADOW_BURST)))
            if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_CARD_SHADOW))
            {
                float const burst = damage * blade->values[1] / multiplier;
                uint32 struck = 0;
                m_cardProc = true;
                for (Creature* mob : burstTargets(blade->values[2]))
                {
                    mob->SendPlaySpellVisualKit(BONK_KIT_SHADOW_IMPACT);
                    DealSpellHit(spellInfo, mob, burst);
                    ++struck;
                }
                m_cardProc = false;
                if (struck)
                    HealHero(ReduceHealing(uint32(burst * multiplier * struck * BONK_SHADOW_BURST_HEAL)));
            }

    // Darkmoon Card: Maelstrom, Thunderfury: a real Chain Lightning from the
    // hero, so its bolt and jumps show; three targets, or five for a card
    // with more than three (Thunderfury). On the target, or the enemy
    // nearest it if the hit killed it.
    if (BonkCard const* maelstrom = m_cardByEffect[BONK_CARD_CHAIN])
        if (roll_chance_f(GetCardChance(BONK_CARD_CHAIN)))
        {
            uint32 const spellId = maelstrom->values[2] > 3.0f ? BONK_SPELL_CARD_CHAIN_FIVE : BONK_SPELL_CARD_CHAIN_THREE;
            Unit* target = victim->IsAlive() ? victim : nullptr;
            if (!target)
            {
                std::vector<Creature*> nearby = GetMobsInRange(15.0f, victim);
                target = nearby.empty() ? nullptr : nearby.front();
            }
            if (target)
            {
                int32 const strike = std::max(1, int32(damage * maelstrom->values[1] / multiplier));
                m_cardProc = true;
                m_player->CastCustomSpell(target, spellId, strike, {}, {}, true);
                m_cardProc = false;
            }
        }
}

void BonkRun::SendCards() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    // A Glyph's card is the Glyph's: the sockets show it, not the row.
    std::string line = "cards~";
    for (OwnedCard const& owned : m_cards)
        if (!m_glyphCardIds.count(owned.card->id))
            line += std::to_string(owned.card->id) + ":" + std::to_string(owned.copies) + ":" + std::to_string(owned.card->rarity) + ":" +
                owned.card->icon + ":" + std::to_string(owned.card->maxCopies) + ",";
    bonk->SendAddon(line);
    for (OwnedCard const& owned : m_cards)
        if (!m_glyphCardIds.count(owned.card->id))
            bonk->SendAddon("cardtip~" + std::to_string(owned.card->id) + "~" + owned.card->name + "~" + owned.card->description);
    SendCardCooldowns();
    SendCardStatus(true);
}

void BonkRun::StartCardCooldown(BonkCardEffect effect, uint32 ms)
{
    m_cardCooldowns[effect] = int32(ms);
    m_cardCooldownTotals[effect] = ms;
    SendCardCooldown(effect);
}

void BonkRun::SendCardCooldown(BonkCardEffect effect) const
{
    BonkCard const* card = m_cardByEffect[effect];
    if (!card)
        return;
    m_player->GetBonkPlayer()->SendAddon("cardcd~" + std::to_string(card->id) + "~" + std::to_string(std::max(0, m_cardCooldowns[effect])) + "~" +
        std::to_string(m_cardCooldownTotals[effect]));
}

void BonkRun::SendCardCooldowns() const
{
    for (uint8 effect = 0; effect < MAX_BONK_CARD_EFFECT; ++effect)
        if (m_cardByEffect[effect] && m_cardCooldowns[effect] > 0)
            SendCardCooldown(BonkCardEffect(effect));
}
