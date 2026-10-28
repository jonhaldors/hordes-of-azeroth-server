#include "BonkRun.h"
#include "Bag.h"
#include "BonkMgr.h"
#include "BonkMobAI.h"
#include "BonkObjectives.h"
#include "BonkPlayer.h"
#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "Item.h"
#include "Mail.h"
#include "Map.h"
#include "MasterPlayer.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Utilities/Random.h"
#include "WorldSession.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

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
    uint32 const BONK_SPELL_FIRST_LOOT_CORE  = 33642;
    uint32 const BONK_SPELL_FIRST_LOOT_SWIRL = 33646;
    float const BONK_LOOT_SWIRL_SCALE    = 1.0f;
    uint32 const BONK_SPELL_DROP_SPARKLE = 33455;
    uint32 const BONK_SPELL_TICKET_POOL  = 33640;
    uint32 const BONK_SPELL_GLYPH_POOL   = 33641;
    float const BONK_DROP_POOL_SCALE     = 0.3f;
    float const BONK_DROP_LOOKS_DISTANCE = 7.0f;
    float const BONK_DROP_LOOKS_SPACING  = 3.0f;
    uint32 const BONK_DROP_LOOKS_LIFETIME_SEC = 180;
    uint32 const BONK_GEAR_CHECK_MS      = 250;
    uint32 const BONK_BANKER_ENTRY       = 900009;
    size_t const BONK_MAX_GEAR_DROPS     = 10;
    uint32 const BONK_ITEM_HEARTHSTONE   = 900230;
    float const BONK_HEARTH_CHANCE_MINIBOSS = 30.0f;
    float const BONK_HEARTH_CHANCE_ELITE    = 3.0f;
    float const BONK_HEARTH_RADIUS       = 5.0f;
    uint32 const BONK_HEARTH_MS          = 15000;
    float const BONK_HEARTH_LORE_STEP    = 20.0f;
    uint32 const BONK_HEARTH_MIN_MS      = 8000;
    uint32 const BONK_HEARTH_SEND_MS     = 250;
    char const* const BONK_HEARTH_MARK          = "hearth";
    uint32 const BONK_HEARTH_VANISH_MS          = 300;
    uint32 const BONK_HEARTH_SWIRL_DISPLAY      = 18585;
    uint32 const BONK_KIT_ASTRAL_RECALL         = 365;
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
    float const BONK_TIER_DROP_BOSS        = 100.0f;
    float const BONK_TIER_DROP_MINIBOSS    = 25.0f;
    float const BONK_TIER_DROP_MINIBOSS_AHEAD = 50.0f;
    float const BONK_GEAR_CHANCE_PER_AFFIX = 5.0f;
    float const BONK_GEAR_STAGE_UPGRADE    = 25.0f;
    float const BONK_GEAR_MYTHIC_FLOOR_UPGRADE = 10.0f;
    float const BONK_GEAR_LEGENDARY_CHANCE = 1.0f;
    float const BONK_GEAR_LEGENDARY_PER_FLOOR = 0.5f;
    uint32 const BONK_GEAR_LEGENDARY_LAST_FLOOR = 20;
    float const BONK_INVASION_UPGRADE_CHANCE = 50.0f;
    uint32 const BONK_ENDLESS_SCRAP_MULTIPLIER = 2;
    uint32 const BONK_SCRAP_BY_TIERS_AHEAD[] = { 0, 20, 35, 50 };
    uint32 const BONK_GO_PILLAR            = 900070;
    uint32 const BONK_PILLAR_EVERY_OTHER_UNTIL = 10;
    uint32 const BONK_PILLAR_MINI_BOSSES   = 2;
    float const BONK_PILLAR_MIN_DISTANCE   = 120.0f;
    float const BONK_PILLAR_MAX_DISTANCE   = 250.0f;
    float const BONK_PILLAR_NEAR_MIN       = 40.0f;
    float const BONK_PILLAR_REACH          = 8.0f;
    char const* const BONK_PILLAR_LABEL    = "Extraction Pillar";
    char const* const BONK_PILLAR_ICON     = "INV_Misc_Rune_01";
    char const* const BONK_PILLAR_DESCRIPTION = "Sends 1 find home right now. It leaves your character.";

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

    uint32 GetLootCoreSpell(uint8 quality)
    {
        return quality >= ITEM_QUALITY_UNCOMMON && quality <= ITEM_QUALITY_LEGENDARY ? BONK_SPELL_FIRST_LOOT_CORE + quality - ITEM_QUALITY_UNCOMMON : 0;
    }

    uint32 GetLootSwirlSpell(uint8 quality)
    {
        return quality >= ITEM_QUALITY_RARE && quality <= ITEM_QUALITY_LEGENDARY ? BONK_SPELL_FIRST_LOOT_SWIRL + quality - ITEM_QUALITY_RARE : 0;
    }

    std::string GetItemLink(ItemPrototype const* proto)
    {
        char color[16];
        snprintf(color, sizeof(color), "%08x", ItemQualityColors[proto->Quality]);
        return "|c" + std::string(color) + "|Hitem:" + std::to_string(proto->ItemId) + ":0:0:0|h[" + proto->Name1 + "]|h|r";
    }
}

float BonkRun::BossLegendaryChance(BonkTier const& tier, uint32 stage)
{
    if (tier.key != "mythic" || !stage)
        return 0.0f;
    return BONK_GEAR_LEGENDARY_CHANCE + BONK_GEAR_LEGENDARY_PER_FLOOR * float(std::min(stage, BONK_GEAR_LEGENDARY_LAST_FLOOR) - 1);
}

uint32 BonkRun::GetDropTier() const
{
    return std::min(BONK_TIER_GEAR_TIERS, m_stage + 1);
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
    float const upgradePerStage = IsMythic() ? BONK_GEAR_MYTHIC_FLOOR_UPGRADE : BONK_GEAR_STAGE_UPGRADE;
    if (quality < ITEM_QUALITY_EPIC && m_stage > 1 && roll_chance_f(upgradePerStage * (m_stage - 1)))
        ++quality;
    if (IsChallenge() && quality < ITEM_QUALITY_EPIC && (!IsInvasion() || roll_chance_f(BONK_INVASION_UPGRADE_CHANCE)))
        ++quality;
    if (kind == BONK_SPAWN_BOSS && roll_chance_f(BossLegendaryChance(m_tier, m_stage)))
        quality = ITEM_QUALITY_LEGENDARY;
    return quality;
}

void BonkRun::DropTierPiece(Creature const* mob, BonkSpawnKind kind)
{
    if (!IsMythic() || m_sandbox || m_gearDrops.size() >= BONK_MAX_GEAR_DROPS)
        return;
    bool const ahead = m_stage >= m_player->GetBonkPlayer()->GetStockTier();
    float const miniBoss = ahead ? BONK_TIER_DROP_MINIBOSS_AHEAD : BONK_TIER_DROP_MINIBOSS;
    float const chance = kind == BONK_SPAWN_BOSS ? BONK_TIER_DROP_BOSS : kind == BONK_SPAWN_MINIBOSS ? miniBoss : 0.0f;
    if (!roll_chance_f(chance))
        return;
    uint32 const tier = GetDropTier();
    std::vector<uint32> choices;
    for (uint32 slot = 0; slot < BONK_TIER_GEAR_SLOTS; ++slot)
        if (uint32 const entry = BonkMgr::GetTierGearEntry(m_player->GetClass(), tier, slot))
            choices.push_back(entry);
    std::vector<uint32> weapons, trinkets;
    for (uint32 entry : BonkMgr::GetMythicGearFor(m_player->GetClassMask(), tier))
    {
        uint32 kind, entryTier;
        BonkMgr::ReadMythicGearEntry(entry, kind, entryTier);
        if (kind >= BONK_MYTHIC_GEAR_FIRST_TRINKET)
            trinkets.push_back(entry);
        else if (kind < BONK_MYTHIC_GEAR_FIRST_ACCESSORY)
            weapons.push_back(entry);
        else
            choices.push_back(entry);
    }
    if (!weapons.empty())
        choices.push_back(weapons[urand(0, uint32(weapons.size() - 1))]);
    if (!trinkets.empty())
        choices.push_back(trinkets[urand(0, uint32(trinkets.size() - 1))]);
    if (choices.empty())
        return;
    uint32 const entry = choices[urand(0, uint32(choices.size() - 1))];
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    if (proto && PlaceGearDrop(mob, entry, ITEM_QUALITY_EPIC))
        m_player->GetBonkPlayer()->Notify(std::string(proto->Name1) + " (Tier " + std::to_string(tier) + ") dropped!");
}

bool BonkRun::DropLegendary(Creature const* mob)
{
    std::vector<uint32> const legendaries = BonkMgr::GetLegendariesFor(m_player->GetClassMask(), GetDropTier());
    if (legendaries.empty())
        return false;
    uint32 const entry = legendaries[urand(0, uint32(legendaries.size() - 1))];
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    if (!proto || !PlaceGearDrop(mob, entry, ITEM_QUALITY_LEGENDARY))
        return true;
    m_player->GetBonkPlayer()->Notify(std::string(proto->Name1) + " (Tier " + std::to_string(GetDropTier()) + ") dropped!");
    return true;
}

bool BonkRun::GetKeepInfo(uint32 entry, uint32& keepEntry, uint8& quality) const
{
    if (BonkGear const* gear = sBonkMgr.GetGear(entry))
    {
        keepEntry = gear->keepEntry;
        quality = gear->quality;
        return keepEntry != 0;
    }
    ItemPrototype const* proto = BonkMgr::GetGearTier(entry) ? sObjectMgr.GetItemPrototype(entry) : nullptr;
    if (!proto)
        return false;
    keepEntry = entry;
    quality = uint8(proto->Quality);
    return true;
}

void BonkRun::MailKeptPiece(uint32 entry, std::string const& subject, std::string const& body)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    Item* keep = proto ? Item::CreateItem(entry, 1, m_player->GetObjectGuid()) : nullptr;
    if (!keep)
        return;
    keep->SaveToDB();
    MailDraft(subject + proto->Name1, body)
        .AddItem(keep)
        .SendMailTo(MailReceiver(m_player), MailSender(MAIL_CREATURE, BONK_BANKER_ENTRY));
}

uint32 BonkRun::GetScrapPoints(uint32 entry) const
{
    if (!IsMythic() || m_sandbox)
        return 0;
    uint32 const tier = BonkMgr::GetGearTier(entry);
    uint32 const stock = m_player->GetBonkPlayer()->GetStockTier();
    if (tier <= stock)
        return 0;
    size_t const steps = std::size(BONK_SCRAP_BY_TIERS_AHEAD);
    uint32 const points = BONK_SCRAP_BY_TIERS_AHEAD[std::min<size_t>(tier - stock, steps - 1)];
    return m_stage > BONK_TIER_GEAR_TIERS ? points * BONK_ENDLESS_SCRAP_MULTIPLIER : points;
}

void BonkRun::GetOwnedTiers(uint32 entry, uint32& bestTier, bool& owned) const
{
    bestTier = 0;
    owned = false;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    std::vector<uint8> const slots = proto ? GetEquipSlotsFor(proto->InventoryType) : std::vector<uint8>();
    if (slots.empty())
        return;
    auto consider = [&](uint32 ownedEntry)
    {
        ItemPrototype const* ownedProto = sObjectMgr.GetItemPrototype(ownedEntry);
        if (!ownedProto || GetEquipSlotsFor(ownedProto->InventoryType) != slots)
            return;
        owned |= ownedEntry == entry;
        bestTier = std::max(bestTier, BonkMgr::GetGearTier(ownedEntry));
    };
    auto considerItem = [&](Item const* item)
    {
        if (item && std::find(m_runGear.begin(), m_runGear.end(), item->GetObjectGuid()) == m_runGear.end())
            consider(item->GetEntry());
    };
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < BANK_SLOT_ITEM_END; ++slot)
        considerItem(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    auto considerBags = [&](uint8 first, uint8 end)
    {
        for (uint8 bag = first; bag < end; ++bag)
            if (Bag const* container = dynamic_cast<Bag const*>(m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, bag)))
                for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                    considerItem(container->GetItemByPos(uint8(slot)));
    };
    considerBags(INVENTORY_SLOT_BAG_START, INVENTORY_SLOT_BAG_END);
    considerBags(BANK_SLOT_BAG_START, BANK_SLOT_BAG_END);
    if (MasterPlayer* master = m_player->GetSession()->GetMasterPlayer())
        for (auto itr = master->GetMailBegin(); itr != master->GetMailEnd(); ++itr)
            if ((*itr)->state != MAIL_STATE_DELETED)
                for (MailItemInfo const& mailed : (*itr)->items)
                    consider(mailed.itemId);
}

bool BonkRun::PlaceGearDrop(Creature const* mob, uint32 entry, uint8 quality)
{
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
        m_pendingGear.push_back(entry);
        return false;
    }
    ShowFromFar(go);
    GearDrop drop;
    drop.guid = go->GetObjectGuid();
    drop.entry = entry;
    drop.marker = PlaceLootBeam(go, quality, drop.core, drop.swirl);
    m_gearDrops.push_back(drop);
    return true;
}

uint32 BonkRun::PlaceLootBeam(GameObject const* go, uint8 quality, uint32& core, uint32& swirl)
{
    uint32 const beam = PlaceMarker(go, GetLootBeamSpell(quality), BONK_LOOT_BEAM_SCALE);
    if (uint32 const coreSpell = GetLootCoreSpell(quality))
        core = PlaceMarker(go, coreSpell, BONK_LOOT_BEAM_SCALE);
    if (uint32 const swirlSpell = GetLootSwirlSpell(quality))
        swirl = PlaceMarker(go, swirlSpell, BONK_LOOT_SWIRL_SCALE);
    return beam;
}

void BonkRun::ClearDropLooks()
{
    for (uint32 key : m_dropLookKeys)
        SendBonkAuraGone(m_player, key);
    m_dropLookKeys.clear();
    if (Map* map = m_player->FindMap())
        for (ObjectGuid const& guid : m_dropLooks)
            if (GameObject* go = map->GetGameObject(guid))
                go->Delete();
    m_dropLooks.clear();
}

bool BonkRun::PlaceDropLooksNow(std::string& error)
{
    if (!IsActive())
        return false;

    ClearDropLooks();

    struct DropLook
    {
        uint32 entry;
        uint8 quality;
        uint32 pool;
    };
    DropLook const looks[] =
    {
        { BONK_GO_LOOT, ITEM_QUALITY_UNCOMMON, 0 },
        { BONK_GO_LOOT, ITEM_QUALITY_RARE, 0 },
        { BONK_GO_LOOT, ITEM_QUALITY_EPIC, 0 },
        { BONK_GO_LOOT, ITEM_QUALITY_LEGENDARY, 0 },
        { BONK_GO_SHARD, 0, BONK_SPELL_TICKET_POOL },
        { BONK_GO_GLYPH, 0, BONK_SPELL_GLYPH_POOL },
    };
    size_t const count = sizeof(looks) / sizeof(looks[0]);
    float const facing = m_player->GetOrientation();
    float const centerX = m_player->GetPositionX() + BONK_DROP_LOOKS_DISTANCE * std::cos(facing);
    float const centerY = m_player->GetPositionY() + BONK_DROP_LOOKS_DISTANCE * std::sin(facing);
    for (size_t i = 0; i < count; ++i)
    {
        float const offset = (float(i) - float(count - 1) / 2.0f) * BONK_DROP_LOOKS_SPACING;
        float const x = centerX - offset * std::sin(facing), y = centerY + offset * std::cos(facing);
        float const z = m_player->GetMap()->GetHeight(x, y, m_player->GetPositionZ() + 5.0f, true, 15.0f);
        if (z <= INVALID_HEIGHT)
            continue;
        GameObject* go = PlaceObject(looks[i].entry, x, y, z, BONK_DROP_LOOKS_LIFETIME_SEC);
        if (!go)
            continue;
        ShowFromFar(go);
        m_dropLooks.push_back(go->GetObjectGuid());
        if (looks[i].pool)
        {
            m_dropLookKeys.push_back(PlaceMarker(go, looks[i].pool, BONK_DROP_POOL_SCALE));
            m_dropLookKeys.push_back(PlaceMarker(go, BONK_SPELL_DROP_SPARKLE, 1.0f));
        }
        else
        {
            uint32 core = 0, swirl = 0;
            m_dropLookKeys.push_back(PlaceLootBeam(go, looks[i].quality, core, swirl));
            m_dropLookKeys.push_back(core);
            m_dropLookKeys.push_back(swirl);
        }
    }
    return true;
}

void BonkRun::DropGear(Creature const* mob, BonkSpawnKind kind)
{
    MaybeDropHearthstone(kind);
    DropTierPiece(mob, kind);
    if (kind == BONK_SPAWN_NORMAL || m_gearDrops.size() >= BONK_MAX_GEAR_DROPS)
        return;
    if (kind == BONK_SPAWN_ELITE)
    {
        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        float const chance = BONK_GEAR_CHANCE_ELITE + BONK_GEAR_CHANCE_PER_AFFIX * (ai ? ai->GetAffixCount() : 0);
        if (!roll_chance_f(chance))
            return;
    }

    uint8 const rolled = RollGearQuality(kind);
    if (rolled == ITEM_QUALITY_LEGENDARY && IsMythic() && DropLegendary(mob))
        return;
    uint32 const classMask = m_player->GetClassMask();
    std::vector<BonkGear const*> pool;
    for (uint8 quality = rolled; quality >= ITEM_QUALITY_UNCOMMON && pool.empty(); --quality)
        for (auto const& itr : sBonkMgr.GetGear())
            if (itr.second.quality == quality && (itr.second.allowableClass & classMask) && itr.second.DropsOn(m_tier.id))
                pool.push_back(&itr.second);
    if (pool.empty())
        return;
    BonkGear const* gear = pool[urand(0, uint32(pool.size() - 1))];
    if (PlaceGearDrop(mob, gear->entry, gear->quality))
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
        RemovePickup(itr->guid, itr->marker, itr->swirl, itr->core);
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
        RemovePickup(drop.guid, drop.marker, drop.swirl, drop.core);
    }
    m_gearDrops.clear();
    UpdateOffers();
}

uint32 BonkRun::GetGearSalvage(uint32 entry) const
{
    uint32 keepEntry = 0;
    uint8 quality = ITEM_QUALITY_UNCOMMON;
    GetKeepInfo(entry, keepEntry, quality);
    float const scrapper = 1.0f + GetUtility(BONK_UTILITY_SCRAPPER) / 100.0f;
    return std::max(1u, uint32(GetChestCost() * GetSalvageShare(quality) * scrapper + 0.5f));
}

void BonkRun::SettlePendingGear()
{
    if (m_stress.on)
        return;
    for (auto itr = m_pendingGear.begin(); itr != m_pendingGear.end();)
    {
        NoteGearFound(*itr);
        if (!IsWorseThanWorn(*itr))
        {
            ++itr;
            continue;
        }
        AutoSalvageGear(*itr);
        itr = m_pendingGear.erase(itr);
    }
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

void BonkRun::NoteGearFound(uint32 entry)
{
    BonkGear const* gear = m_sandbox ? nullptr : sBonkMgr.GetGear(entry);
    if (!gear || !gear->keepEntry || gear->quality > ITEM_QUALITY_RARE)
        return;
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(gear->keepEntry);
    if (!proto || !m_player->GetBonkPlayer()->RecordGearFound(gear->keepEntry))
        return;
    m_player->GetBonkPlayer()->Notify("New find! " + std::string(proto->Name1) + " is now on Durgan's shelf at half price.");
}

bool BonkRun::IsWorseThanWorn(uint32 entry) const
{
    BonkGear const* gear = m_sandbox ? nullptr : sBonkMgr.GetGear(entry);
    if (!gear || gear->quality > ITEM_QUALITY_RARE)
        return false;
    std::vector<uint8> const slots = GetGearSlotChoices(entry);
    if (slots.empty())
        return false;
    for (uint8 slot : slots)
    {
        Item const* worn = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!worn || worn->GetProto()->Quality <= gear->quality)
            return false;
    }
    return true;
}

void BonkRun::AutoSalvageGear(uint32 entry)
{
    ItemPrototype const* proto = sObjectMgr.GetItemPrototype(entry);
    uint32 const valor = GetGearSalvage(entry);
    m_coins += valor;
    m_player->GetBonkPlayer()->Notify("You salvaged " + std::string(proto ? proto->Name1 : "the gear") + " for " + std::to_string(valor) +
        " Valor. You already wear better.");
    SendState();
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
    ResumeUnlessChoiceOpen();
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
    uint32 scrap = 0;
    std::vector<uint32> kept;
    for (ObjectGuid const& guid : m_runGear)
    {
        Item* item = m_player->GetItemByGuid(guid);
        if (!item)
            continue;
        uint32 keepEntry = 0;
        uint8 quality = ITEM_QUALITY_UNCOMMON;
        if (GetKeepInfo(item->GetEntry(), keepEntry, quality) || sBonkMgr.GetGear(item->GetEntry()))
        {
            bool const goesHome = extract && keepEntry && IsInVault(guid);
            if (goesHome)
                kept.push_back(keepEntry);
            else
            {
                scrap += GetScrapPoints(item->GetEntry());
                if (extract || quality <= ITEM_QUALITY_RARE)
                {
                    gold += uint32(m_map.stageGold * GetGoldShare(quality));
                    ++sold;
                }
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
        if (!proto)
            continue;
        MailKeptPiece(entry, "Extracted: ", "You brought this home from " + m_map.name + ". It is yours to keep.");
        bonk->Notify("You extracted " + std::string(proto->Name1) + "! It's waiting in your mailbox.");
        bonk->AnnounceMailAtCamp();
    }

    if (scrap)
    {
        bonk->AddScrap(scrap);
        bonk->Notify("Your unextracted finds became " + std::to_string(scrap) + " Scrap (" + std::to_string(bonk->GetScrap()) + "/" +
            std::to_string(BONK_SCRAP_PER_UPGRADE) + "). Spend it at the Quartermaster.");
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
    m_hearthZ = GetBonkTelegraphGround(m_player->GetMap(), m_hearthX, m_hearthY, m_player->GetPositionZ(), BONK_HEARTH_RADIUS);
    float const facing = m_player->GetOrientation();
    PlaceVault(m_hearthX + BONK_VAULT_RING_OFFSET * std::cos(facing), m_hearthY + BONK_VAULT_RING_OFFSET * std::sin(facing), m_player->GetPositionZ());
    m_hearthActive = true;
    FitVaultToSlots();
    m_hearthMs = 0;
    m_hearthSendMs = 0;
    m_hearthLeavingMs = 0;
    m_hearthShown = false;
    m_hearthSwirl = m_telegraphs.Borrow(m_player, m_hearthX, m_hearthY, m_hearthZ, BONK_HEARTH_SWIRL_DISPLAY);
    ShowHearthRing();
    SendHearthBar(true);
    bonk->Notify("The ring fills in " + std::to_string(GetHearthMs() / IN_MILLISECONDS) + " seconds. Be inside it when it does to return to the Inn" +
        (IsVaultEmptyWithFinds() ? ". Put your best find in the Extraction Vault first!" : " with your loot!"));
    return true;
}

void BonkRun::UpdateHearth(uint32 diff)
{
    if (!m_hearthActive)
        return;

    if (m_hearthLeavingMs)
    {
        if (m_hearthLeavingMs > diff)
            m_hearthLeavingMs -= diff;
        else
            EndHearth();
        return;
    }

    if (m_hearthSwirl.IsEmpty())
        m_hearthSwirl = m_telegraphs.Borrow(m_player, m_hearthX, m_hearthY, m_hearthZ, BONK_HEARTH_SWIRL_DISPLAY);

    m_hearthMs += diff;
    m_hearthSendMs += diff;
    bool const inside = IsHeroInHearthRing();
    if (m_hearthMs >= GetHearthMs())
    {
        if (!inside)
        {
            LeaveHeroBehind();
            return;
        }
        BonkPlayer* bonk = m_player->GetBonkPlayer();
        bonk->SendAddon("hearth~100~1~0");
        EndHearth();
        bonk->Notify("Returning to the Inn!");
        Finish(true);
        return;
    }
    if (!m_hearthShown || inside != m_hearthInside || m_hearthSendMs >= BONK_HEARTH_SEND_MS)
        SendHearthBar(inside);
}

void BonkRun::PauseHearth(bool paused)
{
    if (!m_hearthActive || m_hearthLeavingMs)
        return;
    if (paused)
        SendBonkUnmark(m_player, BONK_HEARTH_MARK);
    else
        ShowHearthRing();
}

void BonkRun::ShowHearthRing()
{
    SendBonkMark(m_player, BONK_HEARTH_MARK, BONK_TELEGRAPH_CIRCLE, m_hearthX, m_hearthY, m_hearthZ, 0.0f, BONK_HEARTH_RADIUS, GetHearthMs(),
        false, BONK_TELEGRAPH_STYLE_FROST, ObjectGuid(), "", m_hearthMs);
}

void BonkRun::SendHearthBar(bool inside)
{
    uint32 const needed = GetHearthMs();
    uint32 const left = needed > m_hearthMs ? needed - m_hearthMs : 0;
    m_hearthSendMs = 0;
    m_hearthShown = true;
    m_hearthInside = inside;
    m_player->GetBonkPlayer()->SendAddon("hearth~" + std::to_string(std::min(m_hearthMs, needed) * 100 / needed) + "~" + (inside ? "1" : "0") + "~" +
        std::to_string((left + IN_MILLISECONDS - 1) / IN_MILLISECONDS));
}

void BonkRun::LeaveHeroBehind()
{
    ShowKitAt(BONK_KIT_ASTRAL_RECALL, m_hearthX, m_hearthY, m_hearthZ);
    if (Map* map = m_player->FindMap())
        if (GameObject* vault = map->GetGameObject(m_vaultGuid))
            ShowKitAt(BONK_KIT_ASTRAL_RECALL, vault->GetPositionX(), vault->GetPositionY(), vault->GetPositionZ());
    m_hearthLeavingMs = BONK_HEARTH_VANISH_MS;
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (m_hearthShown)
        bonk->SendAddon("hearth~-1");
    m_hearthShown = false;
    bonk->Notify("You weren't in the ring when it filled, so the Hearthstone left without you.");
}

bool BonkRun::IsHeroInHearthRing() const
{
    float const dx = m_player->GetPositionX() - m_hearthX, dy = m_player->GetPositionY() - m_hearthY;
    return dx * dx + dy * dy <= BONK_HEARTH_RADIUS * BONK_HEARTH_RADIUS;
}

uint32 BonkRun::GetHearthMs() const
{
    uint32 const off = uint32(GetUtility(BONK_UTILITY_HEARTHSTONE) / BONK_HEARTH_LORE_STEP) * IN_MILLISECONDS;
    return std::max(BONK_HEARTH_MIN_MS, BONK_HEARTH_MS > off ? BONK_HEARTH_MS - off : 0u);
}

void BonkRun::EndHearth()
{
    if (m_hearthShown)
        m_player->GetBonkPlayer()->SendAddon("hearth~-1");
    m_hearthShown = false;
    m_hearthActive = false;
    m_hearthLeavingMs = 0;
    SendBonkUnmark(m_player, BONK_HEARTH_MARK);
    m_telegraphs.Release(m_player, m_hearthSwirl);
    m_hearthSwirl.Clear();
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
    uint32 keepEntry = 0;
    uint8 quality = 0;
    for (ObjectGuid const& guid : m_runGear)
        if (guid.GetCounter() == guidLow)
            if (Item const* item = m_player->GetItemByGuid(guid))
                if (GetKeepInfo(item->GetEntry(), keepEntry, quality))
                    return guid;
    return ObjectGuid();
}

std::vector<BonkRun::VaultFind> BonkRun::GetVaultFinds() const
{
    std::vector<VaultFind> finds;
    for (ObjectGuid const& guid : m_runGear)
    {
        Item const* item = m_player->GetItemByGuid(guid);
        uint32 keepEntry = 0;
        uint8 quality = 0;
        if (!item || !GetKeepInfo(item->GetEntry(), keepEntry, quality))
            continue;
        VaultFind find;
        find.guid = guid;
        find.entry = item->GetEntry();
        find.quality = quality;
        find.worn = item->IsEquipped();
        find.sellCopper = GetGearGold(quality);
        find.inVault = IsInVault(guid);
        find.tier = BonkMgr::GetGearTier(keepEntry);
        GetOwnedTiers(keepEntry, find.bestOwnedTier, find.owned);
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
    m_pillarMode = false;
    if (!m_vaultOpen)
        return;
    m_vaultOpen = false;
    ResumeUnlessChoiceOpen();
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
            DescribeBroughtGearFor(find.entry) + "~" + std::to_string(find.tier) + "~" + std::to_string(find.bestOwnedTier) + "~" +
            (find.owned ? "1" : "0"));
        if (find.inVault && !m_pillarMode)
            chosen += (chosen.empty() ? "" : ",") + std::to_string(find.guid.GetCounter());
    }
    bonk->SendAddon("vaultopen~" + std::to_string(m_pillarMode ? 1u : GetVaultSlots()) + "~" + chosen + "~" + (m_pillarMode ? "pillar" : "vault"));
}

bool BonkRun::IsOwnPillar(GameObject const* pillar) const
{
    return m_state == STATE_RUNNING && !m_pillarGuid.IsEmpty() && pillar->GetObjectGuid() == m_pillarGuid;
}

void BonkRun::OpenPillar()
{
    m_pillarMode = true;
    OpenVault();
}

bool BonkRun::CheckPillarReach(std::string& error) const
{
    GameObject const* pillar = m_pillarGuid.IsEmpty() ? nullptr : m_player->GetMap()->GetGameObject(m_pillarGuid);
    if (!pillar || m_state != STATE_RUNNING)
    {
        error = "There is no Extraction Pillar here.";
        return false;
    }
    if (!m_player->IsWithinDistInMap(pillar, BONK_PILLAR_REACH))
    {
        error = "Get closer to the Extraction Pillar.";
        return false;
    }
    return true;
}

bool BonkRun::SendHomeFromPillar(uint32 guidLow, std::string& error)
{
    if (!CheckPillarReach(error))
        return false;
    ObjectGuid const guid = FindRunGear(guidLow);
    Item* item = guid.IsEmpty() ? nullptr : m_player->GetItemByGuid(guid);
    uint32 keepEntry = 0;
    uint8 quality = 0;
    if (!item || !GetKeepInfo(item->GetEntry(), keepEntry, quality))
    {
        error = "That isn't something you found in this run.";
        return false;
    }

    std::string const name = item->GetProto()->Name1;
    bool const worn = item->IsEquipped();
    m_runGear.erase(std::remove(m_runGear.begin(), m_runGear.end(), guid), m_runGear.end());
    m_vault.erase(std::remove(m_vault.begin(), m_vault.end(), guid), m_vault.end());
    m_player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
    if (worn)
        UpdateGear();
    MailKeptPiece(keepEntry, "Extracted: ", "The Extraction Pillar sent this home from " + m_map.name + ". It is yours to keep.");

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->Notify("The pillar sent " + name + " home. It's waiting in your mailbox.");
    bonk->AnnounceMailAtCamp();
    DespawnPillar();
    CloseVault();
    bonk->SendAddon("vaultclose");
    return true;
}

bool BonkRun::IsPillarFloor() const
{
    if (!IsMythic() || m_sandbox || m_stage < m_player->GetBonkPlayer()->GetStockTier())
        return false;
    return m_stage > BONK_PILLAR_EVERY_OTHER_UNTIL || m_stage % 2 == 0;
}

void BonkRun::CountFloorMiniBoss()
{
    if (++m_floorMiniBosses == BONK_PILLAR_MINI_BOSSES)
        MaybeSpawnPillar();
}

void BonkRun::MaybeSpawnPillar()
{
    if (m_pillarRaised || !IsPillarFloor())
        return;

    BonkSpot spot;
    if (!m_spawnPoints.Take(m_player, BONK_PILLAR_MIN_DISTANCE, BONK_PILLAR_MAX_DISTANCE, spot) &&
        !m_spawnPoints.Take(m_player, BONK_PILLAR_NEAR_MIN, BONK_PILLAR_MIN_DISTANCE, spot))
        return;
    GameObject* pillar = PlaceObject(BONK_GO_PILLAR, spot.x, spot.y, spot.z, BONK_LOOT_LIFETIME_SEC);
    if (!pillar)
    {
        m_spawnPoints.Release(spot.x, spot.y);
        return;
    }
    ShowFromFar(pillar);
    m_pillarRaised = true;
    m_pillarGuid = pillar->GetObjectGuid();
    m_pillarObjective = m_objectives.Add(m_player, BONK_OBJECTIVE_EXTRACT, spot.x, spot.y, spot.z, BONK_PILLAR_LABEL, 0, BONK_PILLAR_ICON,
        BONK_PILLAR_DESCRIPTION);
    m_player->GetBonkPlayer()->Notify("An Extraction Pillar has risen nearby. Use it to send 1 find home right now.");
}

void BonkRun::DespawnPillar()
{
    if (m_pillarObjective)
        m_objectives.Remove(m_player, m_pillarObjective);
    m_pillarObjective = 0;
    if (!m_pillarGuid.IsEmpty())
        if (Map* map = m_player->FindMap())
            if (GameObject* pillar = map->GetGameObject(m_pillarGuid))
                pillar->Delete();
    m_pillarGuid.Clear();
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