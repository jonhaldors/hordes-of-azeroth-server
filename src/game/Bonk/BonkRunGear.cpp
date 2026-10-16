/*
 * Vanilla Bonk: run gear (Core loop v2, phase D1).
 *
 * Elites now and then, and every mini-boss and boss, drop gear the hero's
 * class can wear: a loot sack under a beam where the enemy fell. Walking
 * over it (as far as orbs are pulled) pauses the run like a level-up and
 * offers the item: Equip puts it on for the run (what it replaces goes to
 * the bags), Salvage turns it into Valor. Quality follows the difficulty,
 * the kind of enemy and the stage.
 *
 * Gear found in a run never outlives it. When the run ends, greens and
 * blues turn into gold; epics are kept only by going home through the
 * stage portal, as a permanent copy sent by mail (BonkGear::keepEntry);
 * dying loses them. The gear the hero wore coming in goes back on. A run
 * torn down by a logout takes its gear off the same way, and login clears
 * any run copy a crash left behind (BonkPlayer::OnLogin).
 *
 * Addon: "gearoffer~entry~replacedEntry~valor~quality" opens the choice
 * (replacedEntry 0 for an empty slot), "gearclose" closes it; `.bonk gear
 * equip` and `.bonk gear salvage` answer it. Item tooltips show the Bonk
 * stats from the gear~ lines as for camp gear.
 */

#include "BonkRun.h"
#include "BonkMgr.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "Item.h"
#include "Mail.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Utilities/Random.h"

#include <cstdio>

namespace
{
    uint32 const BONK_GO_LOOT            = 900040;    // the loot sack
    uint32 const BONK_LOOT_LIFETIME_SEC  = 600;
    uint32 const BONK_SPELL_LOOT_BEAM    = 25824;     // Spotlight, as on Darkmoon Tickets
    float const BONK_LOOT_BEAM_SCALE     = 0.5f;
    uint32 const BONK_GEAR_CHECK_MS      = 250;
    uint32 const BONK_BANKER_ENTRY       = 900009;    // Bixi Vaultbolt signs the mail
    size_t const BONK_MAX_GEAR_DROPS     = 10;
    // The run's Hearthstone: mini-bosses and now and then an elite drop one,
    // once a run. Used, it puts a ring at the hero's feet; standing in it
    // this long (leaving pauses it) takes the hero home with the loot.
    uint32 const BONK_ITEM_HEARTHSTONE   = 900230;
    uint32 const BONK_GO_HEARTH_RING     = 900023;    // the Extraction Point model
    float const BONK_HEARTH_CHANCE_MINIBOSS = 30.0f;
    float const BONK_HEARTH_CHANCE_ELITE    = 3.0f;
    float const BONK_HEARTH_RADIUS       = 5.0f;
    uint32 const BONK_HEARTH_MS          = 10000;
    uint32 const BONK_HEARTH_SEND_MS     = 250;
    char const* const BONK_HEARTH_LABEL  = "Hearthstone";

    // While alive, gear goes on and off in combat too (a run hero always is;
    // BonkPlayer::IsEquippingGear).
    struct EquippingGear
    {
        explicit EquippingGear(BonkPlayer* bonk) : m_bonk(bonk) { m_bonk->SetEquippingGear(true); }
        ~EquippingGear() { m_bonk->SetEquippingGear(false); }
        BonkPlayer* m_bonk;
    };

    // An elite drops gear this often (+ a little per affix); mini-bosses and
    // bosses always do.
    float const BONK_GEAR_CHANCE_ELITE     = 20.0f;
    float const BONK_GEAR_CHANCE_PER_AFFIX = 5.0f;
    // Each stage past the first gives a drop this chance a stage to be one
    // quality better.
    float const BONK_GEAR_STAGE_UPGRADE    = 25.0f;
    // A boss on Hard, stage 2 on, drops a legendary this often (a named
    // item with one unique effect, bonk_item_card).
    float const BONK_GEAR_LEGENDARY_CHANCE = 25.0f;

    // Green, blue and epic in percent, by difficulty and kind of enemy.
    struct GearOdds
    {
        uint8 green;
        uint8 blue;
        uint8 epic;
    };
    GearOdds const BONK_GEAR_ODDS_EASY[3]   = { { 85, 15, 0 },  { 60, 40, 0 },  { 0, 70, 30 } };     // elite, mini-boss, boss
    GearOdds const BONK_GEAR_ODDS_MEDIUM[3] = { { 50, 45, 5 },  { 0, 70, 30 },  { 0, 30, 70 } };
    GearOdds const BONK_GEAR_ODDS_HARD[3]   = { { 0, 70, 30 },  { 0, 30, 70 },  { 0, 10, 90 } };

    // What a piece is worth: Valor when salvaged, as a share of the chest
    // price; gold when the run ends, as a share of the map's stage gold.
    float GetSalvageShare(uint8 quality)
    {
        switch (quality)
        {
            case ITEM_QUALITY_UNCOMMON: return 0.25f;
            case ITEM_QUALITY_RARE:     return 0.5f;
            default:                    return 1.0f;
        }
    }
    float GetGoldShare(uint8 quality)
    {
        return quality == ITEM_QUALITY_UNCOMMON ? 0.1f : 0.25f;
    }

    std::string GetQualityName(uint8 quality)
    {
        switch (quality)
        {
            case ITEM_QUALITY_UNCOMMON:  return "Uncommon";
            case ITEM_QUALITY_RARE:      return "Rare";
            case ITEM_QUALITY_EPIC:      return "Epic";
            case ITEM_QUALITY_LEGENDARY: return "Legendary";
            default:                     return "Common";
        }
    }

    std::string GetItemLink(ItemPrototype const* proto)
    {
        char color[16];
        snprintf(color, sizeof(color), "%08x", ItemQualityColors[proto->Quality]);
        return "|c" + std::string(color) + "|Hitem:" + std::to_string(proto->ItemId) + ":0:0:0|h[" + proto->Name1 + "]|h|r";
    }
}

uint8 BonkRun::RollGearQuality(BonkSpawnKind kind) const
{
    GearOdds const* odds = BONK_GEAR_ODDS_EASY;
    if (m_tier.key == "medium")
        odds = BONK_GEAR_ODDS_MEDIUM;
    else if (m_tier.key == "hard")
        odds = BONK_GEAR_ODDS_HARD;
    GearOdds const& row = odds[kind == BONK_SPAWN_BOSS ? 2 : kind == BONK_SPAWN_MINIBOSS ? 1 : 0];

    uint32 const roll = urand(1, 100);
    uint8 quality = roll <= row.green ? ITEM_QUALITY_UNCOMMON : roll <= uint32(row.green + row.blue) ? ITEM_QUALITY_RARE : ITEM_QUALITY_EPIC;
    // Later stages: a chance at one quality better.
    if (quality < ITEM_QUALITY_EPIC && m_stage > 1 && roll_chance_f(BONK_GEAR_STAGE_UPGRADE * (m_stage - 1)))
        ++quality;
    // Legendaries: bosses on Hard, from stage 2.
    if (kind == BONK_SPAWN_BOSS && m_tier.key == "hard" && m_stage >= 2 && roll_chance_f(BONK_GEAR_LEGENDARY_CHANCE))
        quality = ITEM_QUALITY_LEGENDARY;
    return quality;
}

void BonkRun::DropGear(Creature const* mob, BonkSpawnKind kind)
{
    MaybeDropHearthstone(kind);
    if (kind == BONK_SPAWN_NORMAL || m_gearDrops.size() >= BONK_MAX_GEAR_DROPS)
        return;
    if (kind == BONK_SPAWN_ELITE)
    {
        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        float const chance = BONK_GEAR_CHANCE_ELITE + BONK_GEAR_CHANCE_PER_AFFIX * (ai ? ai->GetAffixCount() : 0);
        if (!roll_chance_f(chance))
            return;
    }

    // Something the hero's class can wear, of the rolled quality or the
    // nearest one below that has any.
    uint32 const classMask = m_player->GetClassMask();
    std::vector<BonkGear const*> pool;
    for (uint8 quality = RollGearQuality(kind); quality >= ITEM_QUALITY_UNCOMMON && pool.empty(); --quality)
        for (auto const& itr : sBonkMgr.GetGear())
            if (itr.second.quality == quality && (itr.second.allowableClass & classMask))
                pool.push_back(&itr.second);
    if (pool.empty())
        return;
    BonkGear const* gear = pool[urand(0, uint32(pool.size() - 1))];

    // Where the enemy fell, a step to the side of its chest.
    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = mob->GetPositionX() + 2.0f * std::cos(angle);
    float y = mob->GetPositionY() + 2.0f * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, mob->GetPositionZ() + 2.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT)
    {
        x = mob->GetPositionX();
        y = mob->GetPositionY();
        z = mob->GetPositionZ();
    }

    GameObject* go = PlaceObject(BONK_GO_LOOT, x, y, z, BONK_LOOT_LIFETIME_SEC);
    if (!go)
    {
        m_pendingGear.push_back(gear->entry);
        return;
    }
    GearDrop drop;
    drop.guid = go->GetObjectGuid();
    drop.entry = gear->entry;
    drop.marker = PlaceMarker(go, BONK_SPELL_LOOT_BEAM, BONK_LOOT_BEAM_SCALE);
    m_gearDrops.push_back(drop);
    m_player->GetBonkPlayer()->Notify(GetQualityName(gear->quality) + " gear dropped!");
}

void BonkRun::UpdateGearDrops(uint32 diff)
{
    m_gearTimer += diff;
    if (m_gearTimer < BONK_GEAR_CHECK_MS || m_gearDrops.empty())
        return;
    m_gearTimer = 0;

    // Taken from as far as orbs are pulled (the range is wide enough not to
    // mind the server's position trailing a running hero).
    Map* map = m_player->GetMap();
    float const range = GetMagnetRange();
    bool taken = false;
    for (auto itr = m_gearDrops.begin(); itr != m_gearDrops.end();)
    {
        GameObject* go = map->GetGameObject(itr->guid);
        if (go && go->isSpawned() && m_player->GetDistance(go) > range)
        {
            ++itr;
            continue;
        }
        // Touched, or gone some other way: the item is not lost.
        m_pendingGear.push_back(itr->entry);
        RemovePickup(itr->guid, itr->marker);
        itr = m_gearDrops.erase(itr);
        taken = true;
    }
    if (taken)
        UpdateOffers();
}

void BonkRun::CollectAllGear()
{
    for (GearDrop const& drop : m_gearDrops)
    {
        m_pendingGear.push_back(drop.entry);
        RemovePickup(drop.guid, drop.marker);
    }
    m_gearDrops.clear();
    UpdateOffers();
}

uint32 BonkRun::GetGearSalvage(uint32 entry) const
{
    BonkGear const* gear = sBonkMgr.GetGear(entry);
    return std::max(1u, uint32(GetChestCost() * GetSalvageShare(gear ? gear->quality : ITEM_QUALITY_UNCOMMON) + 0.5f));
}

void BonkRun::OfferGear()
{
    if (m_pendingGear.empty() || m_gearOfferEntry)
        return;
    m_gearOfferEntry = m_pendingGear.front();
    m_pendingGear.erase(m_pendingGear.begin());
    SetPaused(true);
    SendGearOffer();
}

void BonkRun::SendGearOffer() const
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(m_gearOfferEntry);
    if (!proto)
        return;

    // What it would replace: the item in the slot it would go to.
    uint32 replaced = 0;
    uint8 const slot = m_player->FindEquipSlot(proto, NULL_SLOT, true);
    if (slot != NULL_SLOT)
        if (Item const* current = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            replaced = current->GetEntry();

    uint32 const valor = GetGearSalvage(m_gearOfferEntry);
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (bonk->IsAddonReady())
    {
        bonk->SendAddon("gearoffer~" + std::to_string(m_gearOfferEntry) + "~" + std::to_string(replaced) + "~" + std::to_string(valor) + "~" +
            std::to_string(proto->Quality));
        return;
    }

    // Without the addon: in chat, with the commands that answer it.
    ChatHandler(m_player).PSendSysMessage("Loot: %s. Type .bonk gear equip to wear it for this run, or .bonk gear salvage for %u Valor.",
        GetItemLink(proto).c_str(), valor);
}

void BonkRun::CloseGearOffer()
{
    if (!m_gearOfferEntry)
        return;
    m_gearOfferEntry = 0;
    m_player->GetBonkPlayer()->SendAddon("gearclose");
}

bool BonkRun::ChooseGear(bool equip, std::string& error)
{
    if (!m_gearOfferEntry)
    {
        error = "There is no gear to choose right now.";
        return false;
    }

    uint32 const entry = m_gearOfferEntry;
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    std::string why;
    if (equip && EquipRunGear(entry, why))
    {
        if (ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry))
            bonk->Notify("Equipped " + std::string(proto->Name1) + ".");
    }
    else
    {
        // Salvaged, or it couldn't go on: Valor either way.
        uint32 const valor = GetGearSalvage(entry);
        m_coins += valor;
        bonk->Notify((why.empty() ? "" : why + " ") + "Salvaged for " + std::to_string(valor) + " Valor.");
        SendState();
    }

    CloseGearOffer();
    SetPaused(false);
    UpdateOffers();
    return true;
}

bool BonkRun::EquipRunGear(uint32 entry, std::string& error)
{
    ItemPosCountVec dest;
    if (m_player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, 1) != EQUIP_ERR_OK)
    {
        error = "Your bags are full.";
        return false;
    }
    Item* item = m_player->StoreNewItem(dest, entry, true);
    if (!item)
    {
        error = "It couldn't be made.";
        return false;
    }

    // Worn at once; what was in the slot (and an off-hand under a
    // two-hander) goes to the bags.
    EquippingGear const equipping(m_player->GetBonkPlayer());
    uint16 equipDest;
    if (m_player->CanEquipItem(NULL_SLOT, equipDest, item, true) != EQUIP_ERR_OK)
    {
        m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        error = "You can't use that.";
        return false;
    }
    m_player->SwapItem(item->GetPos(), equipDest);
    // The swap can still be refused (the old item could not come off).
    if (!item->IsEquipped())
    {
        m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        error = "It couldn't go on.";
        return false;
    }
    m_runGear.push_back(item->GetObjectGuid());
    UpdateGear();
    return true;
}

void BonkRun::SnapshotBroughtGear()
{
    m_broughtGear.clear();
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            m_broughtGear[slot] = item->GetObjectGuid();
}

void BonkRun::RestoreBroughtGear()
{
    // Main hand before off-hand: the map is in slot order.
    EquippingGear const equipping(m_player->GetBonkPlayer());
    for (auto const& itr : m_broughtGear)
    {
        if (m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, itr.first))
            continue;
        Item* item = m_player->GetItemByGuid(itr.second);
        if (!item || item->IsEquipped())
            continue;
        uint16 dest;
        if (m_player->CanEquipItem(itr.first, dest, item, false) == EQUIP_ERR_OK)
            m_player->SwapItem(item->GetPos(), dest);
    }
}

void BonkRun::ResolveRunGear(bool extract)
{
    uint32 gold = 0;
    uint32 sold = 0;
    std::vector<uint32> kept;
    for (ObjectGuid const& guid : m_runGear)
    {
        Item* item = m_player->GetItemByGuid(guid);
        if (!item)
            continue;
        BonkGear const* gear = sBonkMgr.GetGear(item->GetEntry());
        if (gear && gear->keepEntry)
        {
            if (extract)
                kept.push_back(gear->keepEntry);
        }
        else if (gear)
        {
            gold += uint32(m_map.stageGold * GetGoldShare(gear->quality));
            ++sold;
        }
        m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    }
    m_runGear.clear();
    // A dead hero can't put anything on (the core refuses): BonkRun::Exit
    // does it once they are alive again at camp.
    if (m_player->IsAlive())
    {
        RestoreBroughtGear();
        m_broughtGear.clear();
    }
    // The Hearthstone is the run's too.
    EndHearth();
    if (m_player->HasItemCount(BONK_ITEM_HEARTHSTONE, 1, true))
        m_player->DestroyItemCount(BONK_ITEM_HEARTHSTONE, 255, true, false, true);

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (gold)
    {
        GiveGold(gold);
        bonk->Notify("Your run gear sold: " + std::to_string(sold) + (sold == 1 ? " piece." : " pieces."));
    }

    // Extracted: a permanent copy each, by mail from the camp's banker.
    for (uint32 entry : kept)
    {
        ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
        Item* keep = proto ? Item::CreateItem(entry, 1, m_player->GetObjectGuid()) : nullptr;
        if (!keep)
            continue;
        keep->SaveToDB();
        MailDraft(std::string("Extracted: ") + proto->Name1, "You brought this home from " + m_map.name + ". It is yours to keep.")
            .AddItem(keep)
            .SendMailTo(MailReceiver(m_player), MailSender(MAIL_CREATURE, BONK_BANKER_ENTRY));
        bonk->Notify("Extracted " + std::string(proto->Name1) + "! It waits in your mailbox.");
    }
}

void BonkRun::MaybeDropHearthstone(BonkSpawnKind kind)
{
    if (m_hearthDropped || m_hearthActive)
        return;
    float const chance = kind == BONK_SPAWN_MINIBOSS ? BONK_HEARTH_CHANCE_MINIBOSS : kind == BONK_SPAWN_ELITE ? BONK_HEARTH_CHANCE_ELITE : 0.0f;
    if (!roll_chance_f(chance))
        return;

    // Straight into the bags: it is used from there.
    ItemPosCountVec dest;
    if (m_player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, BONK_ITEM_HEARTHSTONE, 1) != EQUIP_ERR_OK)
        return;
    if (!m_player->StoreNewItem(dest, BONK_ITEM_HEARTHSTONE, true))
        return;
    m_hearthDropped = true;
    m_player->GetBonkPlayer()->Notify("A Hearthstone! Use it to go home with your loot: stand in its ring for 10 sec.");
}

bool BonkRun::UseItem(Item* item)
{
    if (!item || item->GetEntry() != BONK_ITEM_HEARTHSTONE)
        return false;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (m_state != STATE_RUNNING || m_paused || m_hearthActive)
    {
        bonk->Notify(m_hearthActive ? "Your Hearthstone's ring is already open." : "Not now.");
        return true;
    }

    // The ring at the hero's feet; the Hearthstone is spent.
    m_player->DestroyItemCount(BONK_ITEM_HEARTHSTONE, 1, true);
    m_hearthX = m_player->GetPositionX();
    m_hearthY = m_player->GetPositionY();
    m_hearthZ = m_player->GetPositionZ();
    if (GameObject* ring = PlaceObject(BONK_GO_HEARTH_RING, m_hearthX, m_hearthY, m_hearthZ, BONK_LOOT_LIFETIME_SEC))
        m_hearthRing = ring->GetObjectGuid();
    m_hearthActive = true;
    m_hearthMs = 0;
    m_hearthSendMs = 0;
    m_hearthShown = false;
    bonk->Notify("Stand in the ring for 10 sec to go home with your loot!");
    return true;
}

void BonkRun::UpdateHearth(uint32 diff)
{
    if (!m_hearthActive)
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    float const dx = m_player->GetPositionX() - m_hearthX, dy = m_player->GetPositionY() - m_hearthY;
    if (dx * dx + dy * dy > BONK_HEARTH_RADIUS * BONK_HEARTH_RADIUS)
    {
        // Out of the ring: it waits; the bar goes away.
        if (m_hearthShown)
        {
            bonk->SendAddon(std::string("capture~") + BONK_HEARTH_LABEL + "~-1");
            m_hearthShown = false;
        }
        return;
    }

    m_hearthMs += diff;
    m_hearthSendMs += diff;
    if (m_hearthMs >= BONK_HEARTH_MS)
    {
        bonk->SendAddon(std::string("capture~") + BONK_HEARTH_LABEL + "~100");
        EndHearth();
        bonk->Notify("Going home!");
        Finish(true);
        return;
    }
    if (!m_hearthShown || m_hearthSendMs >= BONK_HEARTH_SEND_MS)
    {
        m_hearthSendMs = 0;
        m_hearthShown = true;
        bonk->SendAddon(std::string("capture~") + BONK_HEARTH_LABEL + "~" + std::to_string(m_hearthMs * 100 / BONK_HEARTH_MS));
    }
}

void BonkRun::EndHearth()
{
    if (m_hearthShown)
        m_player->GetBonkPlayer()->SendAddon(std::string("capture~") + BONK_HEARTH_LABEL + "~-1");
    m_hearthShown = false;
    m_hearthActive = false;
    if (Map* map = m_player->FindMap())
        if (GameObject* ring = map->GetGameObject(m_hearthRing))
            ring->Delete();
    m_hearthRing.Clear();
}