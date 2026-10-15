/*
 * Vanilla Bonk: the wandering vendor (feature plan, Phase 3).
 *
 * Zixil, a goblin merchant, stops by once or twice a stage (never in an
 * arena) at a random time between 2:00 and 8:00, never while a boss or
 * mini-boss is up. He stands at a spawn point some way off, under a vendor
 * objective whose pin counts down the minute he stays. Clicked, he pauses
 * the run like a level-up and shows his shelf: three cards from the
 * level-up pool (spell ranks roll their rarity with Luck) and a special
 * slot, a Reroll, Banish or Skip charge. Everything costs Valor, about 1.5x
 * the current chest price; rarer spell ranks cost more. Any number may be
 * bought while the Valor lasts.
 *
 * Rerolling the shelf costs Valor too, 50% more each time, back to the start
 * at the next visit. Locking a slot is free; locked slots not bought come
 * back at the next visit, even a later stage's, at the same price.
 *
 * Addon: "vendor~valor~rerollCost~secondsLeft~name", then for each slot
 * "vslot~index~kind~rank~maxRank~name~icon~description~rarity~locked~price~state"
 * (kind, rank, maxRank and rarity as the level-up's opt, kind c for a
 * charge; state 0 for sale, 1 bought, 2 no longer fits the build), then
 * "vendorend"; "vendorclose" when it closes. `.bonk vendor buy <slot>`,
 * `.bonk vendor lock <slot>`, `.bonk vendor reroll` and `.bonk vendor close`
 * answer it.
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "Chat.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Utilities/Random.h"

namespace
{
    uint32 const BONK_VENDOR_ENTRY         = 900010;    // Zixil
    uint32 const BONK_VENDOR_SECONDS       = 60;        // he stays this long
    uint32 const BONK_VENDOR_EARLIEST      = 120;       // stage seconds: he comes from 2:00...
    uint32 const BONK_VENDOR_LATEST        = 480;       // ...to 8:00...
    uint32 const BONK_VENDOR_BOSS_MARGIN   = 60;        // ...and gone before the boss
    uint32 const BONK_VENDOR_GAP           = 60;        // two visits a stage at least this far apart
    uint32 const BONK_VENDOR_CHECK_MS      = 1000;      // a due visit looks for its chance this often
    float const BONK_VENDOR_MIN_DISTANCE   = 100.0f;    // from the hero...
    float const BONK_VENDOR_MAX_DISTANCE   = 250.0f;    // ...to this far: a walk, not a trek

    // Prices, times the current chest price.
    float const BONK_VENDOR_CARD_PRICE     = 1.5f;
    float const BONK_VENDOR_RARITY_PRICE[MAX_BONK_RARITY] = { 0.8f, 1.0f, 1.25f, 1.6f, 2.0f };    // spell ranks
    float const BONK_VENDOR_CHARGE_PRICE   = 1.0f;
    float const BONK_VENDOR_REROLL_PRICE   = 0.5f;      // the first reroll of a visit...
    float const BONK_VENDOR_REROLL_GROWTH  = 0.5f;      // ...and 50% of that more for each one since

    char const* const BONK_VENDOR_NAME        = "Zixil";
    char const* const BONK_VENDOR_LABEL       = "Wandering Merchant";
    char const* const BONK_VENDOR_ICON        = "INV_Misc_Bag_10";
    char const* const BONK_VENDOR_DESCRIPTION = "Zixil sells spells and upgrades for Valor. Click him before he moves on.";

    // The special slot.
    enum BonkVendorCharge : uint8
    {
        BONK_VENDOR_CHARGE_REROLL = 0,
        BONK_VENDOR_CHARGE_BANISH = 1,
        BONK_VENDOR_CHARGE_SKIP   = 2,
        MAX_BONK_VENDOR_CHARGE
    };
    char const* const CHARGE_NAMES[MAX_BONK_VENDOR_CHARGE] = { "Reroll", "Banish", "Skip" };
    char const* const CHARGE_ICONS[MAX_BONK_VENDOR_CHARGE] = { "Spell_Nature_Reincarnation", "Spell_Shadow_Cripple", "Ability_Rogue_Sprint" };
    char const* const CHARGE_DESCRIPTIONS[MAX_BONK_VENDOR_CHARGE] = {
        "One more Reroll for your level-ups and chests: new cards to choose from.",
        "One more Banish: take a card out of this run for good.",
        "One more Skip: pass on a choice for a little Valor.",
    };
}

void BonkRun::ResetVendor()
{
    DespawnVendor();
    m_vendorVisits.clear();
    m_vendorCheckMs = 0;
    if (m_map.type == BONK_MAP_ARENA)
        return;

    uint32 const latest = std::min(BONK_VENDOR_LATEST,
        m_map.durationSeconds > BONK_VENDOR_BOSS_MARGIN + BONK_VENDOR_SECONDS ? m_map.durationSeconds - BONK_VENDOR_BOSS_MARGIN - BONK_VENDOR_SECONDS : 0u);
    if (latest < BONK_VENDOR_EARLIEST)
        return;

    // Once or twice; twice splits the window, so the two never come together.
    if (urand(0, 1) && latest >= BONK_VENDOR_EARLIEST + 2 * BONK_VENDOR_GAP)
    {
        uint32 const middle = (BONK_VENDOR_EARLIEST + latest) / 2;
        m_vendorVisits.push_back(urand(BONK_VENDOR_EARLIEST, middle - BONK_VENDOR_GAP / 2));
        m_vendorVisits.push_back(urand(middle + BONK_VENDOR_GAP / 2, latest));
    }
    else
        m_vendorVisits.push_back(urand(BONK_VENDOR_EARLIEST, latest));
}

void BonkRun::UpdateVendor(uint32 diff)
{
    if (!m_vendorGuid.IsEmpty())
    {
        Creature* vendor = m_player->GetMap()->GetCreature(m_vendorGuid);
        if (vendor && vendor->IsAlive() && m_vendorLeftMs > diff)
        {
            m_vendorLeftMs -= diff;
            return;
        }
        if (vendor)
            m_player->GetBonkPlayer()->Notify(std::string(BONK_VENDOR_NAME) + " packs up and moves on.");
        DespawnVendor();
        return;
    }

    if (m_vendorVisits.empty() || m_elapsedMs < m_vendorVisits.front() * IN_MILLISECONDS)
        return;
    m_vendorCheckMs += diff;
    if (m_vendorCheckMs < BONK_VENDOR_CHECK_MS)
        return;
    m_vendorCheckMs = 0;

    // A visit held back by a boss until too close to the stage's own lapses.
    if (m_elapsedMs / IN_MILLISECONDS + BONK_VENDOR_BOSS_MARGIN + BONK_VENDOR_SECONDS >= m_map.durationSeconds)
    {
        m_vendorVisits.clear();
        return;
    }
    if (m_bossSpawned || IsBossUp())
        return;
    if (SpawnVendor())
        m_vendorVisits.erase(m_vendorVisits.begin());
}

bool BonkRun::IsBossUp() const
{
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : m_mobs)
        if (Creature* mob = map->GetCreature(guid))
            if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                if (mob->IsAlive() && (ai->GetKind() == BONK_SPAWN_BOSS || ai->GetKind() == BONK_SPAWN_MINIBOSS))
                    return true;
    return false;
}

bool BonkRun::SpawnVendor()
{
    BonkSpot spot;
    if (!m_spawnPoints.Take(m_player, BONK_VENDOR_MIN_DISTANCE, BONK_VENDOR_MAX_DISTANCE, spot))
        return false;

    float const facing = std::atan2(m_player->GetPositionY() - spot.y, m_player->GetPositionX() - spot.x);
    Creature* vendor = m_player->SummonCreature(BONK_VENDOR_ENTRY, spot.x, spot.y, spot.z, facing, TEMPSUMMON_MANUAL_DESPAWN, 0);
    if (!vendor)
    {
        m_spawnPoints.Release(spot.x, spot.y);
        return false;
    }

    m_vendorGuid = vendor->GetObjectGuid();
    m_vendorLeftMs = BONK_VENDOR_SECONDS * IN_MILLISECONDS;
    m_vendorRerolls = 0;
    m_vendorObjective = m_objectives.Add(m_player, BONK_OBJECTIVE_VENDOR, spot.x, spot.y, spot.z, BONK_VENDOR_LABEL, BONK_VENDOR_SECONDS,
        BONK_VENDOR_ICON, BONK_VENDOR_DESCRIPTION);
    StockVendor(false);
    m_player->GetBonkPlayer()->Notify("A goblin merchant has set up shop nearby - for one minute!");
    return true;
}

void BonkRun::StockVendor(bool reroll)
{
    // Locked slots stay: through a reroll, or from the last visit.
    std::vector<VendorSlot> locked;
    if (reroll)
    {
        for (VendorSlot const& slot : m_vendorStock)
            if (slot.locked && slot.state == VENDOR_FOR_SALE)
                locked.push_back(slot);
    }
    else
    {
        locked = m_vendorKept;
        m_vendorKept.clear();
    }

    std::vector<Offer> kept, shown;
    for (VendorSlot const& slot : locked)
        if (!slot.isCharge)
            kept.push_back(slot.card);
    if (reroll)
        for (VendorSlot const& slot : m_vendorStock)
            if (!slot.isCharge)
                shown.push_back(slot.card);

    // The level-up's draw: locked cards first, as they are now, then new
    // ones, a reroll's last ones only once fresh ones run out.
    std::vector<Offer> cards;
    OfferUpgrades(OFFER_LEVEL, reroll ? &shown : nullptr, &kept, &cards);

    m_vendorStock.clear();
    for (Offer const& card : cards)
    {
        VendorSlot slot;
        slot.card = card;
        slot.price = GetVendorPrice(&card);
        for (VendorSlot const& old : locked)
            if (!old.isCharge && old.card.isWeapon == card.isWeapon && old.card.id == card.id)
            {
                slot.price = old.price;
                slot.locked = true;
            }
        m_vendorStock.push_back(slot);
    }

    VendorSlot special;
    special.isCharge = true;
    special.charge = uint8(urand(0, MAX_BONK_VENDOR_CHARGE - 1));
    special.price = GetVendorPrice(nullptr);
    for (VendorSlot const& old : locked)
        if (old.isCharge)
            special = old;
    m_vendorStock.push_back(special);
}

void BonkRun::CheckVendorStock()
{
    // What the build took since (a level-up, another card) may leave a card
    // no longer fitting, or its spell a rank further on: shown as it is now.
    for (VendorSlot& slot : m_vendorStock)
    {
        if (slot.isCharge || slot.state != VENDOR_FOR_SALE)
            continue;
        Offer card = slot.card;
        if (!RefreshKeptOffer(card))
        {
            slot.state = VENDOR_GONE;
            slot.locked = false;
        }
        else if (card.newLevel != slot.card.newLevel)
            slot.card = card;
    }
}

uint32 BonkRun::GetVendorPrice(Offer const* card) const
{
    float scale = BONK_VENDOR_CHARGE_PRICE;
    if (card)
    {
        scale = BONK_VENDOR_CARD_PRICE;
        if (card->isWeapon && card->step != BONK_STEP_NEW && card->rarity < MAX_BONK_RARITY)
            scale *= BONK_VENDOR_RARITY_PRICE[card->rarity];
    }
    return std::max(1u, uint32(GetChestCost() * scale + 0.5f));
}

uint32 BonkRun::GetVendorRerollCost() const
{
    return std::max(1u, uint32(GetChestCost() * BONK_VENDOR_REROLL_PRICE * (1.0f + BONK_VENDOR_REROLL_GROWTH * m_vendorRerolls) + 0.5f));
}

void BonkRun::OpenVendor(Creature* vendor)
{
    if (m_state != STATE_RUNNING || !vendor || vendor->GetObjectGuid() != m_vendorGuid)
        return;
    if (!m_offer.empty() || !m_leyOffer.empty())
    {
        m_player->GetBonkPlayer()->Notify("Make your choice first.");
        return;
    }

    if (!m_vendorOpen)
    {
        m_vendorOpen = true;
        CheckVendorStock();
        SetPaused(true);
    }
    SendVendor();
}

void BonkRun::CloseVendor()
{
    if (!m_vendorOpen)
        return;
    m_vendorOpen = false;
    m_player->GetBonkPlayer()->SendAddon("vendorclose");
    // A level-up that came meanwhile pauses again right away.
    SetPaused(false);
    UpdateOffers();
}

bool BonkRun::BuyFromVendor(uint32 index, std::string& error)
{
    if (!m_vendorOpen)
    {
        error = "The merchant isn't showing his wares.";
        return false;
    }
    if (index < 1 || index > m_vendorStock.size())
    {
        error = "Choose a number from 1 to " + std::to_string(m_vendorStock.size()) + ".";
        return false;
    }

    VendorSlot& slot = m_vendorStock[index - 1];
    if (slot.state != VENDOR_FOR_SALE)
    {
        error = slot.state == VENDOR_SOLD ? "You already bought that." : "That no longer fits your build.";
        SendVendor();
        return false;
    }
    if (m_coins < slot.price)
    {
        error = "That costs " + std::to_string(slot.price) + " Valor. You have " + std::to_string(m_coins) + ".";
        SendVendor();
        return false;
    }

    m_coins -= slot.price;
    slot.state = VENDOR_SOLD;
    sBonkQuests.OnProgress(m_player, BONK_QUEST_VENDOR_BOUGHT, 0, 1);
    slot.locked = false;
    std::string name;
    if (slot.isCharge)
    {
        switch (slot.charge)
        {
            case BONK_VENDOR_CHARGE_REROLL: ++m_rerolls; break;
            case BONK_VENDOR_CHARGE_BANISH: ++m_banishes; break;
            default:                        ++m_skips; break;
        }
        name = std::string(CHARGE_NAMES[slot.charge < MAX_BONK_VENDOR_CHARGE ? slot.charge : BONK_VENDOR_CHARGE_SKIP]) + " charge";
    }
    else
    {
        name = DescribeOffer(slot.card, nullptr);
        sLog.Player(m_player->GetSession(), LOG_LEVELUP, LOG_LVL_BASIC,
            "Bonk buy: %s buys %s rank %u for %u Valor at run minute %.2f, level %u",
            m_player->GetName(), name.c_str(), slot.card.newLevel, slot.price, GetRunMinutes(), m_player->GetLevel());
        ApplyVendorCard(slot.card);
        SendLoadout();
        CheckVendorStock();
    }

    m_player->GetBonkPlayer()->Notify("Bought " + name + ".");
    SendState();
    SendVendor();
    return true;
}

void BonkRun::ApplyVendorCard(Offer const& card)
{
    if (card.isWeapon)
    {
        if (OwnedWeapon* owned = FindOwned(card.id))
            ApplyStep(*owned, card);
        else if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(card.id))
            AddWeapon(*weapon);
    }
    else
    {
        m_passiveRanks[card.id] = uint8(card.newLevel);
        ApplyPassives();
    }
}

bool BonkRun::LockVendorSlot(uint32 index, std::string& error)
{
    if (!m_vendorOpen)
    {
        error = "The merchant isn't showing his wares.";
        return false;
    }
    if (index < 1 || index > m_vendorStock.size() || m_vendorStock[index - 1].state != VENDOR_FOR_SALE)
    {
        error = "Choose something still for sale.";
        return false;
    }

    m_vendorStock[index - 1].locked = !m_vendorStock[index - 1].locked;
    SendVendor();
    return true;
}

bool BonkRun::RerollVendor(std::string& error)
{
    if (!m_vendorOpen)
    {
        error = "The merchant isn't showing his wares.";
        return false;
    }
    uint32 const cost = GetVendorRerollCost();
    if (m_coins < cost)
    {
        error = "A reroll costs " + std::to_string(cost) + " Valor. You have " + std::to_string(m_coins) + ".";
        SendVendor();
        return false;
    }

    m_coins -= cost;
    ++m_vendorRerolls;
    StockVendor(true);
    SendState();
    SendVendor();
    return true;
}

bool BonkRun::SpawnVendorNow(std::string& error)
{
    if (m_state != STATE_RUNNING)
    {
        error = "Only a running stage can have a merchant.";
        return false;
    }
    if (!m_vendorGuid.IsEmpty())
    {
        error = "The merchant is already here.";
        return false;
    }
    if (!SpawnVendor())
    {
        error = "No spawn point 100-250 yards away yet; try again in a few seconds.";
        return false;
    }
    return true;
}

void BonkRun::DespawnVendor()
{
    if (m_vendorOpen)
    {
        m_vendorOpen = false;
        m_player->GetBonkPlayer()->SendAddon("vendorclose");
    }

    // Locked slots not bought wait for the next visit.
    for (VendorSlot const& slot : m_vendorStock)
        if (slot.locked && slot.state == VENDOR_FOR_SALE)
            m_vendorKept.push_back(slot);
    m_vendorStock.clear();

    if (Map* map = m_player->FindMap())
    {
        if (Creature* vendor = map->GetCreature(m_vendorGuid))
            vendor->DespawnOrUnsummon();
        if (m_vendorObjective)
            RemoveObjective(m_vendorObjective);
    }
    m_vendorGuid.Clear();
    m_vendorObjective = 0;
}

void BonkRun::SendVendor() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    uint32 const rerollCost = GetVendorRerollCost();
    if (bonk->IsAddonReady())
    {
        bonk->SendAddon("vendor~" + std::to_string(m_coins) + "~" + std::to_string(rerollCost) + "~" +
            std::to_string((m_vendorLeftMs + IN_MILLISECONDS - 1) / IN_MILLISECONDS) + "~" + BONK_VENDOR_NAME);
        for (size_t i = 0; i < m_vendorStock.size(); ++i)
        {
            VendorSlot const& slot = m_vendorStock[i];
            std::string kind = "c", name, icon, description, rarity;
            uint32 rank = 0, maxRank = 0;
            if (slot.isCharge)
            {
                uint8 const charge = slot.charge < MAX_BONK_VENDOR_CHARGE ? slot.charge : BONK_VENDOR_CHARGE_SKIP;
                name = std::string(CHARGE_NAMES[charge]) + " charge";
                icon = CHARGE_ICONS[charge];
                description = CHARGE_DESCRIPTIONS[charge];
            }
            else
            {
                Offer const& card = slot.card;
                name = DescribeOffer(card, &icon);
                kind = card.step == BONK_STEP_EVOLVE ? "e" : card.isWeapon ? "w" : "p";
                rank = card.newLevel;
                if (!card.isWeapon)
                    if (BonkPassive const* passive = sBonkMgr.GetPassive(card.id))
                        maxRank = passive->maxRank;
                description = DescribeStep(card);
                if (card.isWeapon && card.step != BONK_STEP_NEW)
                    rarity = std::to_string(card.rarity);
            }
            bonk->SendAddon("vslot~" + std::to_string(i + 1) + "~" + kind + "~" + std::to_string(rank) + "~" + std::to_string(maxRank) + "~" +
                name + "~" + icon + "~" + description + "~" + rarity + "~" + (slot.locked ? "1" : "0") + "~" + std::to_string(slot.price) + "~" +
                std::to_string(slot.state));
        }
        bonk->SendAddon("vendorend");
        return;
    }

    ChatHandler handler(m_player);
    handler.PSendSysMessage("%s's wares (you have %u Valor): .bonk vendor buy <number>, .bonk vendor lock <number>, "
        ".bonk vendor reroll (%u Valor), .bonk vendor close", BONK_VENDOR_NAME, m_coins, rerollCost);
    for (size_t i = 0; i < m_vendorStock.size(); ++i)
    {
        VendorSlot const& slot = m_vendorStock[i];
        std::string name;
        if (slot.isCharge)
            name = std::string(CHARGE_NAMES[slot.charge < MAX_BONK_VENDOR_CHARGE ? slot.charge : BONK_VENDOR_CHARGE_SKIP]) + " charge";
        else
        {
            name = DescribeOffer(slot.card, nullptr) + " rank " + std::to_string(slot.card.newLevel);
            if (slot.card.isWeapon && slot.card.step != BONK_STEP_NEW)
                name += std::string(" (") + GetBonkRarityName(slot.card.rarity) + ")";
            name += ": " + DescribeStep(slot.card);
        }
        char const* state = slot.state == VENDOR_SOLD ? " - bought" : slot.state == VENDOR_GONE ? " - no longer fits" : slot.locked ? " - locked" : "";
        handler.PSendSysMessage("  %u) %s - %u Valor%s", uint32(i + 1), name.c_str(), slot.price, state);
    }
}
