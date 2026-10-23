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

#include <algorithm>
#include <cstdio>

namespace
{
    uint32 const BONK_GO_LOOT            = 900040;
    uint32 const BONK_LOOT_LIFETIME_SEC  = 600;
    uint32 const BONK_SPELL_LOOT_BEAM    = 25824;
    uint32 const BONK_SPELL_UNCOMMON_LOOT_BEAM  = 33456;
    uint32 const BONK_SPELL_RARE_LOOT_BEAM      = 33457;
    uint32 const BONK_SPELL_EPIC_LOOT_BEAM      = 33458;
    uint32 const BONK_SPELL_LEGENDARY_LOOT_BEAM = 33459;
    float const BONK_LOOT_BEAM_SCALE     = 0.35f;
    uint32 const BONK_GEAR_CHECK_MS      = 250;
    uint32 const BONK_BANKER_ENTRY       = 900009;
    size_t const BONK_MAX_GEAR_DROPS     = 10;
    uint32 const BONK_ITEM_HEARTHSTONE   = 900230;
    uint32 const BONK_GO_HEARTH_RING     = 900023;
    float const BONK_HEARTH_CHANCE_MINIBOSS = 30.0f;
    float const BONK_HEARTH_CHANCE_ELITE    = 3.0f;
    float const BONK_HEARTH_RADIUS       = 5.0f;
    uint32 const BONK_HEARTH_MS          = 15000;
    float const BONK_HEARTH_LORE_STEP    = 20.0f;
    uint32 const BONK_HEARTH_MIN_MS      = 8000;
    uint32 const BONK_HEARTH_SEND_MS     = 250;
    char const* const BONK_HEARTH_LABEL  = "Hearthstone";
    float const BONK_VAULT_REACH         = 8.0f;
    float const BONK_VAULT_RING_OFFSET   = 2.5f;

    std::vector<uint8> GetEquipSlotsFor(uint32 inventoryType)
    {
        switch (inventoryType)
        {
            case INVTYPE_HEAD:           return { EQUIPMENT_SLOT_HEAD };
            case INVTYPE_NECK:           return { EQUIPMENT_SLOT_NECK };
            case INVTYPE_SHOULDERS:      return { EQUIPMENT_SLOT_SHOULDERS };
            case INVTYPE_BODY:           return { EQUIPMENT_SLOT_BODY };
            case INVTYPE_CHEST:
            case INVTYPE_ROBE:           return { EQUIPMENT_SLOT_CHEST };
            case INVTYPE_WAIST:          return { EQUIPMENT_SLOT_WAIST };
            case INVTYPE_LEGS:           return { EQUIPMENT_SLOT_LEGS };
            case INVTYPE_FEET:           return { EQUIPMENT_SLOT_FEET };
            case INVTYPE_WRISTS:         return { EQUIPMENT_SLOT_WRISTS };
            case INVTYPE_HANDS:          return { EQUIPMENT_SLOT_HANDS };
            case INVTYPE_FINGER:         return { EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2 };
            case INVTYPE_TRINKET:        return { EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 };
            case INVTYPE_CLOAK:          return { EQUIPMENT_SLOT_BACK };
            case INVTYPE_WEAPON:
            case INVTYPE_2HWEAPON:
            case INVTYPE_WEAPONMAINHAND: return { EQUIPMENT_SLOT_MAINHAND };
            case INVTYPE_SHIELD:
            case INVTYPE_WEAPONOFFHAND:
            case INVTYPE_HOLDABLE:       return { EQUIPMENT_SLOT_OFFHAND };
            case INVTYPE_RANGED:
            case INVTYPE_THROWN:
            case INVTYPE_RANGEDRIGHT:
            case INVTYPE_RELIC:          return { EQUIPMENT_SLOT_RANGED };
            case INVTYPE_TABARD:         return { EQUIPMENT_SLOT_TABARD };
            default:                     return {};
        }
    }

    struct EquippingGear
    {
        explicit EquippingGear(BonkPlayer* bonk) : m_bonk(bonk) { m_bonk->SetEquippingGear(true); }
        ~EquippingGear() { m_bonk->SetEquippingGear(false); }
        BonkPlayer* m_bonk;
    };

    float const BONK_GEAR_CHANCE_ELITE     = 20.0f;
    float const BONK_GEAR_CHANCE_PER_AFFIX = 5.0f;
    float const BONK_GEAR_STAGE_UPGRADE    = 25.0f;
    float const BONK_GEAR_LEGENDARY_CHANCE = 25.0f;
    float const BONK_INVASION_UPGRADE_CHANCE = 50.0f;

    struct GearOdds
    {
        uint8 green;
        uint8 blue;
        uint8 epic;
    };
    GearOdds const BONK_GEAR_ODDS_EASY[3]   = { { 85, 15, 0 },  { 60, 40, 0 },  { 0, 70, 30 } };
    GearOdds const BONK_GEAR_ODDS_MEDIUM[3] = { { 50, 45, 5 },  { 0, 70, 30 },  { 0, 30, 70 } };
    GearOdds const BONK_GEAR_ODDS_HARD[3]   = { { 0, 70, 30 },  { 0, 30, 70 },  { 0, 10, 90 } };

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
        switch (quality)
        {
            case ITEM_QUALITY_UNCOMMON: return 0.1f;
            case ITEM_QUALITY_RARE:     return 0.25f;
            case ITEM_QUALITY_EPIC:     return 0.5f;
            default:                    return 1.0f;
        }
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

    uint32 GetLootBeamSpell(uint8 quality)
    {
        switch (quality)
        {
            case ITEM_QUALITY_UNCOMMON:  return BONK_SPELL_UNCOMMON_LOOT_BEAM;
            case ITEM_QUALITY_RARE:      return BONK_SPELL_RARE_LOOT_BEAM;
            case ITEM_QUALITY_EPIC:      return BONK_SPELL_EPIC_LOOT_BEAM;
            case ITEM_QUALITY_LEGENDARY: return BONK_SPELL_LEGENDARY_LOOT_BEAM;
            default:                     return BONK_SPELL_LOOT_BEAM;
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
    if (m_tier.key == "heroic")
        odds = BONK_GEAR_ODDS_MEDIUM;
    else if (m_tier.key == "mythic")
        odds = BONK_GEAR_ODDS_HARD;
    GearOdds const& row = odds[kind == BONK_SPAWN_BOSS ? 2 : kind == BONK_SPAWN_MINIBOSS ? 1 : 0];

    uint32 const roll = urand(1, 100);
    uint8 quality = roll <= row.green ? ITEM_QUALITY_UNCOMMON : roll <= uint32(row.green + row.blue) ? ITEM_QUALITY_RARE : ITEM_QUALITY_EPIC;
    if (quality < ITEM_QUALITY_EPIC && m_stage > 1 && roll_chance_f(BONK_GEAR_STAGE_UPGRADE * (m_stage - 1)))
        ++quality;
    if (IsChallenge() && quality < ITEM_QUALITY_EPIC && (!IsInvasion() || roll_chance_f(BONK_INVASION_UPGRADE_CHANCE)))
        ++quality;
    if (kind == BONK_SPAWN_BOSS && m_tier.key == "mythic" && m_stage >= 2 && roll_chance_f(BONK_GEAR_LEGENDARY_CHANCE))
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

    uint32 const classMask = m_player->GetClassMask();
    std::vector<BonkGear const*> pool;
    for (uint8 quality = RollGearQuality(kind); quality >= ITEM_QUALITY_UNCOMMON && pool.empty(); --quality)
        for (auto const& itr : sBonkMgr.GetGear())
            if (itr.second.quality == quality && (itr.second.allowableClass & classMask) && itr.second.DropsOn(m_tier.id))
                pool.push_back(&itr.second);
    if (pool.empty())
        return;
    BonkGear const* gear = pool[urand(0, uint32(pool.size() - 1))];

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
    drop.marker = PlaceMarker(go, GetLootBeamSpell(gear->quality), BONK_LOOT_BEAM_SCALE);
    m_gearDrops.push_back(drop);
    m_player->GetBonkPlayer()->Notify(GetQualityName(gear->quality) + " gear dropped!");
}

void BonkRun::UpdateGearDrops(uint32 diff)
{
    m_gearTimer += diff;
    if (m_gearTimer < BONK_GEAR_CHECK_MS || m_gearDrops.empty())
        return;
    m_gearTimer = 0;

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
    float const scrapper = 1.0f + GetUtility(BONK_UTILITY_SCRAPPER) / 100.0f;
    return std::max(1u, uint32(GetChestCost() * GetSalvageShare(gear ? gear->quality : ITEM_QUALITY_UNCOMMON) * scrapper + 0.5f));
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

    auto wornIn = [this](uint8 slot)
    {
        Item const* current = slot != NULL_SLOT ? m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) : nullptr;
        return current ? current->GetEntry() : 0u;
    };
    std::vector<uint8> const choices = GetGearSlotChoices(m_gearOfferEntry);
    bool const twoSlots = choices.size() >= 2;
    uint8 const slot = twoSlots ? choices[0] : m_player->FindEquipSlot(proto, NULL_SLOT, true);
    bool const blockedByTwoHander = slot == EQUIPMENT_SLOT_OFFHAND && m_player->IsTwoHandUsed();
    uint32 const replaced = wornIn(blockedByTwoHander ? uint8(EQUIPMENT_SLOT_MAINHAND) : slot);
    std::string const slotChoice = twoSlots ? "~" + std::to_string(choices[0]) + "~" + std::to_string(choices[1]) + "~" +
        std::to_string(wornIn(choices[1])) : std::string();

    uint32 const valor = GetGearSalvage(m_gearOfferEntry);
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (bonk->IsAddonReady())
    {
        bonk->SendAddon("gearoffer~" + std::to_string(m_gearOfferEntry) + "~" + std::to_string(replaced) + "~" + std::to_string(valor) + "~" +
            std::to_string(proto->Quality) + slotChoice);
        return;
    }

    ChatHandler(m_player).PSendSysMessage("Loot: %s. Type .bonk gear equip to wear it for this run, .bonk gear loot to carry it in your bags, or .bonk gear salvage for %u Valor.",
        GetItemLink(proto).c_str(), valor);
}

void BonkRun::CloseGearOffer()
{
    if (!m_gearOfferEntry)
        return;
    m_gearOfferEntry = 0;
    m_player->GetBonkPlayer()->SendAddon("gearclose");
}

bool BonkRun::ChooseGear(GearChoice choice, uint8 slot, std::string& error)
{
    if (!m_gearOfferEntry)
    {
        error = "There is no gear to choose right now.";
        return false;
    }

    uint32 const entry = m_gearOfferEntry;
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    std::string const name = proto ? proto->Name1 : "the gear";
    if (choice == GEAR_CHOICE_EQUIP && slot != NULL_SLOT)
    {
        std::vector<uint8> const choices = GetGearSlotChoices(entry);
        if (std::find(choices.begin(), choices.end(), slot) == choices.end())
        {
            error = name + " doesn't go in that slot.";
            return false;
        }
    }

    if (choice == GEAR_CHOICE_LOOT)
    {
        Item* item = StoreRunGear(entry, error);
        if (!item)
        {
            error += ".";
            return false;
        }
        m_runGear.push_back(item->GetObjectGuid());
        bonk->Notify("You put " + name + " in your bags. Put it in the Extraction Vault to keep it.");
    }
    else
    {
        std::string why;
        if (choice == GEAR_CHOICE_EQUIP && EquipRunGear(entry, slot, why))
            bonk->Notify("You equipped " + name + ".");
        else
        {
            uint32 const valor = GetGearSalvage(entry);
            m_coins += valor;
            bonk->Notify((why.empty() ? "You salvaged " + name : why + ", so you salvaged it") + " for " + std::to_string(valor) + " Valor.");
            SendState();
        }
    }

    CloseGearOffer();
    SetPaused(false);
    UpdateOffers();
    return true;
}

Item* BonkRun::StoreRunGear(uint32 entry, std::string& error)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    std::string const name = proto ? proto->Name1 : "the gear";
    ItemPosCountVec dest;
    if (m_player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, 1) != EQUIP_ERR_OK)
    {
        error = "You have no room in your bags for " + name;
        return nullptr;
    }
    Item* item = m_player->StoreNewItem(dest, entry, true);
    if (!item)
        error = name + " couldn't be created";
    return item;
}

bool BonkRun::EquipRunGear(uint32 entry, uint8 slot, std::string& error)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    std::string const name = proto ? proto->Name1 : "the gear";
    Item* item = StoreRunGear(entry, error);
    if (!item)
        return false;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    EquippingGear const equipping(bonk);
    uint16 equipDest;
    InventoryResult result = m_player->CanEquipItem(slot, equipDest, item, true);
    if (result == EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED)
    {
        if (!bonk->StowTwoHander())
        {
            m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            error = "You have no room in your bags for your two-handed weapon";
            return false;
        }
        result = m_player->CanEquipItem(slot, equipDest, item, true);
    }
    if (result != EQUIP_ERR_OK)
    {
        m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        error = "You can't use " + name;
        return false;
    }
    m_player->SwapItem(item->GetPos(), equipDest);
    if (!item->IsEquipped())
    {
        m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        error = "You couldn't equip " + name;
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
    EquippingGear const equipping(m_player->GetBonkPlayer());
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "RESTOREPROBE start %u slots", uint32(m_broughtGear.size()));
    for (auto const& itr : m_broughtGear)
    {
        if (m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, itr.first))
            continue;
        Item* item = m_player->GetItemByGuid(itr.second);
        if (!item || item->IsEquipped())
            continue;
        uint16 dest;
        InventoryResult const restoreProbe = m_player->CanEquipItem(itr.first, dest, item, false);
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "RESTOREPROBE slot %u item %u result %u alive %u combat %u", uint32(itr.first), item->GetEntry(), uint32(restoreProbe), uint32(m_player->IsAlive()), uint32(m_player->IsInCombat()));
        if (restoreProbe == EQUIP_ERR_OK)
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
        if (BonkGear const* gear = sBonkMgr.GetGear(item->GetEntry()))
        {
            if (extract && gear->keepEntry && IsInVault(guid))
                kept.push_back(gear->keepEntry);
            else if (extract || gear->quality <= ITEM_QUALITY_RARE)
            {
                gold += uint32(m_map.stageGold * GetGoldShare(gear->quality));
                ++sold;
            }
        }
        m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    }
    m_runGear.clear();
    m_vault.clear();
    if (m_player->IsAlive())
    {
        RestoreBroughtGear();
        m_broughtGear.clear();
    }
    EndHearth();
    if (m_player->HasItemCount(BONK_ITEM_HEARTHSTONE, 1, true))
        m_player->DestroyItemCount(BONK_ITEM_HEARTHSTONE, 255, true, false, true);

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (gold)
    {
        uint32 const paid = uint32(gold * GetGoldMultiplier());
        GiveGold(gold);
        bonk->Notify("You sold " + std::to_string(sold) + (sold == 1 ? " piece" : " pieces") + " of run gear for " + FormatBonkMoney(paid) + ".");
    }

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
        bonk->Notify("You extracted " + std::string(proto->Name1) + "! It's waiting in your mailbox.");
        bonk->AnnounceMailAtCamp();
    }
}

void BonkRun::MaybeDropHearthstone(BonkSpawnKind kind)
{
    if (m_hearthDropped || m_hearthActive)
        return;
    float const chance = (kind == BONK_SPAWN_MINIBOSS ? BONK_HEARTH_CHANCE_MINIBOSS : kind == BONK_SPAWN_ELITE ? BONK_HEARTH_CHANCE_ELITE : 0.0f) *
        (1.0f + GetUtility(BONK_UTILITY_HEARTHSTONE) / 100.0f);
    if (!roll_chance_f(chance))
        return;

    ItemPosCountVec dest;
    if (m_player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, BONK_ITEM_HEARTHSTONE, 1) != EQUIP_ERR_OK)
        return;
    if (!m_player->StoreNewItem(dest, BONK_ITEM_HEARTHSTONE, true))
        return;
    m_hearthDropped = true;
    m_player->GetBonkPlayer()->NotifyFind("You found a Hearthstone! Save it for an emergency: it lets you escape the run early and takes you back to the Inn with 1 item from your Extraction Vault.");
}

bool BonkRun::UseItem(Item* item)
{
    if (!item || item->GetEntry() != BONK_ITEM_HEARTHSTONE)
        return false;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (m_state != STATE_RUNNING || m_paused || m_hearthActive)
    {
        bonk->Notify(m_hearthActive ? "Your Hearthstone's ring is already open." : "You can't use that right now.");
        return true;
    }

    m_player->DestroyItemCount(BONK_ITEM_HEARTHSTONE, 1, true);
    m_hearthX = m_player->GetPositionX();
    m_hearthY = m_player->GetPositionY();
    m_hearthZ = m_player->GetPositionZ();
    if (GameObject* ring = PlaceObject(BONK_GO_HEARTH_RING, m_hearthX, m_hearthY, m_hearthZ, BONK_LOOT_LIFETIME_SEC))
        m_hearthRing = ring->GetObjectGuid();
    float const facing = m_player->GetOrientation();
    PlaceVault(m_hearthX + BONK_VAULT_RING_OFFSET * std::cos(facing), m_hearthY + BONK_VAULT_RING_OFFSET * std::sin(facing), m_hearthZ);
    m_hearthActive = true;
    FitVaultToSlots();
    m_hearthMs = 0;
    m_hearthSendMs = 0;
    m_hearthShown = false;
    bonk->Notify("Stand in the ring for " + std::to_string(GetHearthMs() / IN_MILLISECONDS) + " seconds to return to the Inn" +
        (IsVaultEmptyWithFinds() ? ". Put your best find in the Extraction Vault first!" : " with your loot!"));
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
        if (m_hearthShown)
        {
            bonk->SendAddon(std::string("capture~") + BONK_HEARTH_LABEL + "~-1");
            m_hearthShown = false;
        }
        return;
    }

    m_hearthMs += diff;
    m_hearthSendMs += diff;
    uint32 const needed = GetHearthMs();
    if (m_hearthMs >= needed)
    {
        bonk->SendAddon(std::string("capture~") + BONK_HEARTH_LABEL + "~100");
        EndHearth();
        bonk->Notify("Returning to the Inn!");
        Finish(true);
        return;
    }
    if (!m_hearthShown || m_hearthSendMs >= BONK_HEARTH_SEND_MS)
    {
        m_hearthSendMs = 0;
        m_hearthShown = true;
        bonk->SendAddon(std::string("capture~") + BONK_HEARTH_LABEL + "~" + std::to_string(m_hearthMs * 100 / needed));
    }
}

uint32 BonkRun::GetHearthMs() const
{
    uint32 const off = uint32(GetUtility(BONK_UTILITY_HEARTHSTONE) / BONK_HEARTH_LORE_STEP) * IN_MILLISECONDS;
    return std::max(BONK_HEARTH_MIN_MS, BONK_HEARTH_MS > off ? BONK_HEARTH_MS - off : 0u);
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
    if (m_state != STATE_PORTAL)
        DespawnVault();
}

uint32 BonkRun::GetVaultSlots() const
{
    if (m_hearthActive)
        return 1;
    return 1 + uint32(GetUtility(BONK_UTILITY_PACK_MULE));
}

void BonkRun::FitVaultToSlots()
{
    if (m_vault.size() <= GetVaultSlots())
        return;
    m_vault.erase(m_vault.begin(), m_vault.end() - GetVaultSlots());
    if (Item const* item = m_player->GetItemByGuid(m_vault.back()))
        m_player->GetBonkPlayer()->Notify("The Hearthstone's vault holds only 1 item. " + std::string(item->GetProto()->Name1) + " will go home with you.");
}

uint32 BonkRun::GetGearGold(uint8 quality) const
{
    return uint32(uint32(m_map.stageGold * GetGoldShare(quality)) * GetGoldMultiplier());
}

void BonkRun::PlaceVault(float x, float y, float baseZ)
{
    DespawnVault();
    float z = m_player->GetMap()->GetHeight(x, y, baseZ + 5.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT || !m_player->IsWithinLOS(x, y, z + 1.0f))
    {
        x = m_player->GetPositionX();
        y = m_player->GetPositionY();
        z = m_player->GetPositionZ();
    }
    if (GameObject* vault = PlaceObject(BONK_GO_VAULT, x, y, z, BONK_LOOT_LIFETIME_SEC))
        m_vaultGuid = vault->GetObjectGuid();
}

void BonkRun::DespawnVault()
{
    if (m_vaultGuid.IsEmpty())
        return;
    if (Map* map = m_player->FindMap())
        if (GameObject* vault = map->GetGameObject(m_vaultGuid))
            vault->Delete();
    m_vaultGuid.Clear();
    m_vaultOpen = false;
    if (m_player->IsInWorld())
        m_player->GetBonkPlayer()->SendAddon("vaultclose");
}

bool BonkRun::IsOwnVault(GameObject const* vault) const
{
    return (m_state == STATE_RUNNING || m_state == STATE_PORTAL) && vault->GetObjectGuid() == m_vaultGuid;
}

bool BonkRun::IsInVault(ObjectGuid const& guid) const
{
    return std::find(m_vault.begin(), m_vault.end(), guid) != m_vault.end();
}

ObjectGuid BonkRun::FindRunGear(uint32 guidLow) const
{
    for (ObjectGuid const& guid : m_runGear)
        if (guid.GetCounter() == guidLow)
            if (Item const* item = m_player->GetItemByGuid(guid))
                if (BonkGear const* gear = sBonkMgr.GetGear(item->GetEntry()))
                    if (gear->keepEntry)
                        return guid;
    return ObjectGuid();
}

std::vector<BonkRun::VaultFind> BonkRun::GetVaultFinds() const
{
    std::vector<VaultFind> finds;
    for (ObjectGuid const& guid : m_runGear)
    {
        Item const* item = m_player->GetItemByGuid(guid);
        BonkGear const* gear = item ? sBonkMgr.GetGear(item->GetEntry()) : nullptr;
        if (!gear || !gear->keepEntry)
            continue;
        VaultFind find;
        find.guid = guid;
        find.entry = item->GetEntry();
        find.quality = gear->quality;
        find.worn = item->IsEquipped();
        find.sellCopper = GetGearGold(gear->quality);
        find.inVault = IsInVault(guid);
        finds.push_back(find);
    }
    return finds;
}

std::vector<uint8> BonkRun::GetGearSlotChoices(uint32 entry) const
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    if (!proto)
        return {};
    std::vector<uint8> slots = GetEquipSlotsFor(proto->InventoryType);
    if (proto->InventoryType == INVTYPE_WEAPON && m_player->CanDualWield())
    {
        Item const* mainHand = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        if (!mainHand || mainHand->GetProto()->InventoryType != INVTYPE_2HWEAPON)
            slots.push_back(EQUIPMENT_SLOT_OFFHAND);
    }
    return slots;
}

std::string BonkRun::DescribeBroughtGearFor(uint32 entry) const
{
    std::string entries;
    for (uint8 slot : GetGearSlotChoices(entry))
    {
        auto itr = m_broughtGear.find(slot);
        if (Item const* brought = itr != m_broughtGear.end() ? m_player->GetItemByGuid(itr->second) : nullptr)
            entries += (entries.empty() ? "" : ",") + std::to_string(brought->GetEntry());
    }
    return entries;
}

bool BonkRun::IsVaultEmptyWithFinds() const
{
    return m_vault.empty() && !GetVaultFinds().empty();
}

bool BonkRun::CheckVaultReach(std::string& error) const
{
    GameObject const* vault = m_vaultGuid.IsEmpty() ? nullptr : m_player->GetMap()->GetGameObject(m_vaultGuid);
    if (!vault || (m_state != STATE_RUNNING && m_state != STATE_PORTAL))
    {
        error = "There is no Extraction Vault here.";
        return false;
    }
    if (!m_player->IsWithinDistInMap(vault, BONK_VAULT_REACH))
    {
        error = "Get closer to the Extraction Vault.";
        return false;
    }
    return true;
}

void BonkRun::OpenVault()
{
    if (!m_vaultOpen && m_state == STATE_RUNNING)
    {
        m_vaultOpen = true;
        SetPaused(true);
    }
    SendVault();
}

void BonkRun::CloseVault()
{
    if (!m_vaultOpen)
        return;
    m_vaultOpen = false;
    SetPaused(false);
    UpdateOffers();
}

void BonkRun::SendVault() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (!bonk->IsAddonReady())
        return;
    bonk->SendAddon("vaultclear");
    std::string chosen;
    for (VaultFind const& find : GetVaultFinds())
    {
        bonk->SendAddon("vaultitem~" + std::to_string(find.guid.GetCounter()) + "~" + std::to_string(find.entry) + "~" +
            std::to_string(find.quality) + "~" + (find.worn ? "1" : "0") + "~" + std::to_string(find.sellCopper) + "~" +
            DescribeBroughtGearFor(find.entry));
        if (find.inVault)
            chosen += (chosen.empty() ? "" : ",") + std::to_string(find.guid.GetCounter());
    }
    bonk->SendAddon("vaultopen~" + std::to_string(GetVaultSlots()) + "~" + chosen);
}

bool BonkRun::PutInVault(uint32 guidLow, std::string& error)
{
    if (!CheckVaultReach(error))
        return false;
    ObjectGuid const guid = FindRunGear(guidLow);
    if (guid.IsEmpty())
    {
        error = "That isn't something you found in this run.";
        return false;
    }
    if (!IsInVault(guid))
    {
        while (m_vault.size() >= GetVaultSlots())
            m_vault.erase(m_vault.begin());
        m_vault.push_back(guid);
        if (Item const* item = m_player->GetItemByGuid(guid))
            m_player->GetBonkPlayer()->Notify(std::string(item->GetProto()->Name1) + " will go home with you.");
    }
    SendVault();
    return true;
}

bool BonkRun::TakeFromVault(uint32 guidLow, std::string& error)
{
    if (!CheckVaultReach(error))
        return false;
    auto itr = std::find_if(m_vault.begin(), m_vault.end(), [guidLow](ObjectGuid const& guid) { return guid.GetCounter() == guidLow; });
    if (itr == m_vault.end())
    {
        error = "That isn't in the vault.";
        return false;
    }
    m_vault.erase(itr);
    SendVault();
    return true;
}