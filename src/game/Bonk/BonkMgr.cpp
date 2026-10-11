/*
 * Vanilla Bonk: static game-mode data.
 */

#include "BonkMgr.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Policies/SingletonImp.h"
#include "SpellMgr.h"

#include <tuple>

INSTANTIATE_SINGLETON_1(BonkMgr);

namespace
{
    // The effect whose base points carry a spell's damage: direct damage
    // first, damage over time otherwise, -1 for none.
    int32 FindDamageEffect(SpellEntry const* spellInfo)
    {
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                return int32(i);

        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if ((spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA || spellInfo->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA) &&
                spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
                return int32(i);

        return -1;
    }
}

void BonkMgr::Load()
{
    LoadTiers();
    LoadMaps();
    LoadMapCreatures();
    LoadMapEvents();
    LoadWeapons();
    LoadPassives();
    LoadPowerups();
    LoadShopTrees();
    LoadUpgrades();
    LoadTalents();
    LoadItemPower();
    LoadMobs();

    for (uint32 entry : { BONK_GO_CHEST, BONK_GO_CHEST_FREE, BONK_GO_PORTAL, BONK_GO_SHARD })
        if (!sObjectMgr.GetGameObjectTemplate(entry))
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Vanilla Bonk: gameobject template %u is missing; runs will lack chests or portals, and give shards without a drop.", entry);

    for (uint32 entry : { BONK_ORB_SMALL, BONK_ORB_LARGE, BONK_ORB_HUGE })
        if (!sObjectMgr.GetCreatureTemplate(entry))
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Vanilla Bonk: creature template %u is missing; kills will give experience directly.", entry);
}

void BonkMgr::LoadTiers()
{
    m_tiers.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `key`, `name`, `health_multiplier`, `damage_multiplier`, `stage_health_growth`, `stage_damage_growth`, "
        "`stage_spawn_growth`, `chest_cost`, `chest_cost_growth`, `unlock_tier`, `unlock_stage`, "
        "`shard_chance_normal`, `shard_chance_elite`, `shards_miniboss`, `shards_boss` FROM `bonk_tier`"));

    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk tiers. Table `bonk_tier` is empty or missing.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkTier tier;
        tier.id = fields[0].GetUInt32();
        tier.key = fields[1].GetCppString();
        tier.name = fields[2].GetCppString();
        tier.healthMultiplier = fields[3].GetFloat();
        tier.damageMultiplier = fields[4].GetFloat();
        tier.stageHealthGrowth = fields[5].GetFloat();
        tier.stageDamageGrowth = fields[6].GetFloat();
        tier.stageSpawnGrowth = fields[7].GetFloat();
        tier.chestCost = fields[8].GetUInt32();
        tier.chestCostGrowth = fields[9].GetFloat();
        tier.unlockTierId = fields[10].GetUInt32();
        tier.unlockStage = fields[11].GetUInt32();
        tier.shardChanceNormal = fields[12].GetFloat();
        tier.shardChanceElite = fields[13].GetFloat();
        tier.shardsMiniboss = fields[14].GetUInt32();
        tier.shardsBoss = fields[15].GetUInt32();

        m_tiers[tier.id] = std::move(tier);
    } while (result->NextRow());

    for (auto& itr : m_tiers)
    {
        BonkTier& tier = itr.second;
        if (tier.unlockTierId && !m_tiers.count(tier.unlockTierId))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_tier`: tier %u is unlocked by unknown tier %u; it is always open.", tier.id, tier.unlockTierId);
            tier.unlockTierId = 0;
        }
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk tiers", uint32(m_tiers.size()));
}

void BonkMgr::LoadMaps()
{
    m_maps.clear();
    m_arenaZonesByMap.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `key`, `name`, `tier`, `map_id`, `zone_id`, `position_x`, `position_y`, `position_z`, `orientation`, "
        "`duration_sec`, `level_offset`, `health_multiplier`, `damage_multiplier`, `health_growth_per_min`, "
        "`spawn_interval_ms`, `spawn_batch`, `spawn_batch_growth_per_min`, `max_alive`, `elite_interval_sec`, "
        "`gold_per_kill`, `stage_gold`, `coins_per_kill`, `powerup_interval_sec`, `powerup_max`, "
        "`spawn_min_distance`, `spawn_max_distance` FROM `bonk_map`"));

    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk maps. Table `bonk_map` is empty or missing.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkMap map;
        map.id = fields[0].GetUInt32();
        map.key = fields[1].GetCppString();
        map.name = fields[2].GetCppString();
        map.tierId = fields[3].GetUInt32();
        map.mapId = fields[4].GetUInt32();
        map.zoneId = fields[5].GetUInt32();
        map.x = fields[6].GetFloat();
        map.y = fields[7].GetFloat();
        map.z = fields[8].GetFloat();
        map.o = fields[9].GetFloat();
        map.durationSeconds = fields[10].GetUInt32();
        map.levelOffset = fields[11].GetInt32();
        map.healthMultiplier = fields[12].GetFloat();
        map.damageMultiplier = fields[13].GetFloat();
        map.healthGrowthPerMinute = fields[14].GetFloat();
        map.spawnIntervalMs = std::max(250u, fields[15].GetUInt32());
        map.spawnBatch = fields[16].GetFloat();
        map.spawnBatchGrowthPerMinute = fields[17].GetFloat();
        map.maxAlive = fields[18].GetUInt32();
        map.eliteIntervalSeconds = fields[19].GetUInt32();
        map.goldPerKill = fields[20].GetUInt32();
        map.stageGold = fields[21].GetUInt32();
        map.coinsPerKill = fields[22].GetUInt32();
        map.powerupIntervalSeconds = fields[23].GetUInt32();
        map.powerupMax = fields[24].GetUInt32();
        map.spawnMinDistance = std::max(5.0f, fields[25].GetFloat());
        map.spawnMaxDistance = std::max(map.spawnMinDistance, fields[26].GetFloat());

        if (!sMapStorage.LookupEntry<MapEntry>(map.mapId))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map`: map %u (%s) does not exist, skipped.", map.mapId, map.name.c_str());
            continue;
        }

        auto tier = m_tiers.find(map.tierId);
        if (tier == m_tiers.end())
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map`: map %u (%s) belongs to unknown tier %u; no run will use it.", map.id, map.name.c_str(), map.tierId);
        else
            tier->second.mapIds.push_back(map.id);

        m_arenaZonesByMap[map.mapId].insert(map.zoneId);
        m_maps[map.id] = std::move(map);
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk maps", uint32(m_maps.size()));
}

void BonkMgr::LoadMapCreatures()
{
    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `bonk_map_id`, `entry`, `kind`, `min_sec`, `max_sec`, `weight`, `health_multiplier`, `damage_multiplier`, `speed`, `scale` "
        "FROM `bonk_map_creature`"));

    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk map creatures.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 const bonkMapId = fields[0].GetUInt32();
        auto map = m_maps.find(bonkMapId);
        if (map == m_maps.end())
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map_creature`: unknown bonk map %u, skipped.", bonkMapId);
            continue;
        }

        BonkMapCreature creature;
        creature.entry = fields[1].GetUInt32();
        uint8 const kind = fields[2].GetUInt8();
        creature.minSecond = fields[3].GetUInt32();
        creature.maxSecond = fields[4].GetUInt32();
        creature.weight = std::max(1u, fields[5].GetUInt32());
        creature.healthMultiplier = fields[6].GetFloat();
        creature.damageMultiplier = fields[7].GetFloat();
        creature.speed = fields[8].GetFloat();
        creature.scale = fields[9].GetFloat();

        if (kind >= MAX_BONK_SPAWN_KIND)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map_creature`: creature %u has unknown kind %u, skipped.", creature.entry, kind);
            continue;
        }
        creature.kind = BonkSpawnKind(kind);

        if (!sObjectMgr.GetCreatureTemplate(creature.entry))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map_creature`: creature %u does not exist, skipped.", creature.entry);
            continue;
        }

        map->second.creatures.push_back(creature);
        ++count;
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk map creatures", count);
}

void BonkMgr::LoadMapEvents()
{
    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `bonk_map_id`, `second`, `event`, `count` FROM `bonk_map_event` ORDER BY `second`"));

    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk map events.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 const bonkMapId = fields[0].GetUInt32();
        auto map = m_maps.find(bonkMapId);
        if (map == m_maps.end())
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map_event`: unknown bonk map %u, skipped.", bonkMapId);
            continue;
        }

        BonkMapEvent event;
        event.second = fields[1].GetUInt32();
        uint8 const type = fields[2].GetUInt8();
        event.count = std::max(1u, fields[3].GetUInt32());

        if (type >= MAX_BONK_EVENT)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map_event`: map %u has unknown event %u, skipped.", bonkMapId, type);
            continue;
        }
        event.type = BonkEventType(type);

        map->second.events.push_back(event);
        ++count;
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk map events", count);
}

void BonkMgr::LoadWeapons()
{
    m_weapons.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `icon`, `description`, `spell_id`, `target`, `cooldown_ms`, `range`, `damage_fraction`, "
        "`class_mask`, `starter_class_mask`, `max_level`, `evolves_from`, `evolve_passive` FROM `bonk_weapon`"));

    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk weapons.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkWeapon weapon;
        weapon.id = fields[0].GetUInt32();
        weapon.name = fields[1].GetCppString();
        weapon.icon = fields[2].GetCppString();
        weapon.description = fields[3].GetCppString();
        weapon.spellId = fields[4].GetUInt32();
        weapon.target = BonkWeaponTarget(std::min<uint8>(fields[5].GetUInt8(), BONK_TARGET_RANDOM));
        weapon.cooldownMs = std::max(200u, fields[6].GetUInt32());
        weapon.range = fields[7].GetFloat();
        weapon.damageFraction = fields[8].GetFloat();
        weapon.classMask = fields[9].GetUInt32();
        weapon.starterClassMask = fields[10].GetUInt32();
        weapon.maxLevel = std::max<uint8>(1, fields[11].GetUInt8());
        weapon.evolvesFrom = fields[12].GetUInt32();
        weapon.evolvePassive = fields[13].GetUInt32();

        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
        if (!spellInfo)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: weapon %u uses unknown spell %u, skipped.", weapon.id, weapon.spellId);
            continue;
        }

        weapon.damageEffectIndex = FindDamageEffect(spellInfo);
        if (weapon.damageEffectIndex < 0)
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: spell %u of weapon %u has no damage effect; it will use its own values.", weapon.spellId, weapon.id);

        m_weapons[weapon.id] = std::move(weapon);
    } while (result->NextRow());

    for (auto const& itr : m_weapons)
        if (itr.second.evolvesFrom && !m_weapons.count(itr.second.evolvesFrom))
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: weapon %u evolves from unknown weapon %u.", itr.first, itr.second.evolvesFrom);

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk weapons", uint32(m_weapons.size()));
}

void BonkMgr::LoadMobs()
{
    m_mobs.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `entry`, `role`, `range` FROM `bonk_mob`"));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BonkMob& mob = m_mobs[fields[0].GetUInt32()];
            mob.role = BonkMobRole(std::min<uint8>(fields[1].GetUInt8(), MAX_BONK_MOB_ROLE - 1));
            mob.range = fields[2].GetFloat();
        } while (result->NextRow());
    }

    uint32 abilities = 0;
    result = WorldDatabase.Query("SELECT `entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage` "
        "FROM `bonk_mob_ability` ORDER BY `entry`, `spell_id`");
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            BonkMobAbility ability;
            uint32 const entry = fields[0].GetUInt32();
            ability.spellId = fields[1].GetUInt32();
            ability.target = fields[2].GetUInt8() ? BONK_ABILITY_SELF : BONK_ABILITY_PLAYER;
            ability.cooldownMs = std::max(500u, fields[3].GetUInt32());
            ability.firstMs = fields[4].GetUInt32();
            ability.minRange = fields[5].GetFloat();
            ability.maxRange = fields[6].GetFloat();
            ability.damage = fields[7].GetFloat();

            SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(ability.spellId);
            if (!spellInfo)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_mob_ability`: creature %u uses unknown spell %u, skipped.", entry, ability.spellId);
                continue;
            }
            ability.damageEffectIndex = FindDamageEffect(spellInfo);

            // An ability without a bonk_mob row belongs to a plain melee enemy.
            m_mobs[entry].abilities.push_back(ability);
            ++abilities;
        } while (result->NextRow());
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk enemy types with %u abilities", uint32(m_mobs.size()), abilities);
}

void BonkMgr::LoadPassives()
{
    m_passives.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `icon`, `description`, `effect`, `value_per_rank`, `max_rank` FROM `bonk_passive`"));

    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk passives.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkPassive passive;
        passive.id = fields[0].GetUInt32();
        passive.name = fields[1].GetCppString();
        passive.icon = fields[2].GetCppString();
        passive.description = fields[3].GetCppString();
        uint8 const effect = fields[4].GetUInt8();
        passive.valuePerRank = fields[5].GetFloat();
        passive.maxRank = std::max<uint8>(1, fields[6].GetUInt8());

        if (effect >= MAX_BONK_PASSIVE_EFFECT)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_passive`: passive %u has unknown effect %u, skipped.", passive.id, effect);
            continue;
        }
        passive.effect = BonkPassiveEffect(effect);

        m_passives[passive.id] = std::move(passive);
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk passives", uint32(m_passives.size()));
}

void BonkMgr::LoadPowerups()
{
    m_powerups.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `entry`, `spell_id`, `weight` FROM `bonk_powerup`"));
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk power-ups.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkPowerup powerup;
        powerup.entry = fields[0].GetUInt32();
        powerup.spellId = fields[1].GetUInt32();
        powerup.weight = std::max(1u, fields[2].GetUInt32());

        if (!sObjectMgr.GetGameObjectTemplate(powerup.entry))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_powerup`: gameobject %u does not exist, skipped.", powerup.entry);
            continue;
        }

        if (!sSpellMgr.GetSpellEntry(powerup.spellId))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_powerup`: power-up %u uses unknown spell %u, skipped.", powerup.entry, powerup.spellId);
            continue;
        }

        m_powerups.push_back(powerup);
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk power-ups", uint32(m_powerups.size()));
}

std::string FormatBonkMoney(uint32 copper)
{
    std::string text;
    if (uint32 const gold = copper / 10000)
        text += std::to_string(gold) + "g";
    if (uint32 const silver = (copper / 100) % 100)
        text += (text.empty() ? "" : " ") + std::to_string(silver) + "s";
    if (uint32 const rest = copper % 100)
        text += (text.empty() ? "" : " ") + std::to_string(rest) + "c";
    return text.empty() ? "0c" : text;
}

std::string FormatBonkShards(uint32 count)
{
    return std::to_string(count) + (count == 1 ? " Talent Shard" : " Talent Shards");
}

uint32 BonkUpgrade::GetCost(uint8 ownedRanks) const
{
    return uint32(cost * (1.0f + costGrowth * ownedRanks) + 0.5f);
}

uint32 BonkTalent::GetCost(uint8 ownedRanks) const
{
    return uint32(cost * (1.0f + costGrowth * ownedRanks) + 0.5f);
}

void BonkMgr::LoadShopTrees()
{
    m_shopTrees.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `id`, `shop`, `name`, `background` FROM `bonk_shop_tree`"));
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk shop trees.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkShopTree tree;
        tree.id = fields[0].GetUInt32();
        uint8 const shop = fields[1].GetUInt8();
        tree.name = fields[2].GetCppString();
        tree.background = fields[3].GetCppString();

        if (shop != BONK_SHOP_UPGRADES && shop != BONK_SHOP_TALENTS)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_shop_tree`: tree %u has unknown shop %u, skipped.", tree.id, shop);
            continue;
        }
        tree.shop = BonkShop(shop);

        m_shopTrees[tree.id] = std::move(tree);
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk shop trees", uint32(m_shopTrees.size()));
}

template <class Entry>
void BonkMgr::CheckShopLayout(char const* table, BonkShop shop, std::map<uint32, Entry>& entries) const
{
    std::set<std::tuple<uint32, uint8, uint8>> taken;
    for (auto& itr : entries)
    {
        Entry& entry = itr.second;
        BonkShopNode& node = entry.layout;

        auto tree = m_shopTrees.find(node.tree);
        if (tree == m_shopTrees.end() || tree->second.shop != shop)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `%s`: %u is in tree %u, which this shop doesn't have; it won't show in the trees.",
                table, entry.id, node.tree);
            node.tree = 0;
        }
        else if (node.row < 1 || node.row > BONK_SHOP_ROWS || node.column < 1 || node.column > BONK_SHOP_COLUMNS)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `%s`: %u sits at row %u, column %u, outside the %ux%u tree; it won't show in the trees.",
                table, entry.id, node.row, node.column, BONK_SHOP_ROWS, BONK_SHOP_COLUMNS);
            node.tree = 0;
        }
        else if (!taken.insert(std::make_tuple(node.tree, node.row, node.column)).second)
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `%s`: %u shares row %u, column %u of tree %u with another entry.",
                table, entry.id, node.row, node.column, node.tree);

        if (!node.requiredId)
        {
            node.requiredRank = 0;
            continue;
        }

        auto required = entries.find(node.requiredId);
        if (required == entries.end() || node.requiredId == entry.id)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `%s`: %u requires %u, which doesn't exist; requirement dropped.",
                table, entry.id, node.requiredId);
            node.requiredId = 0;
            node.requiredRank = 0;
            continue;
        }
        node.requiredRank = std::min(std::max<uint8>(1, node.requiredRank), required->second.maxRank);
    }
}

void BonkMgr::LoadUpgrades()
{
    m_upgrades.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `description`, `effect`, `value_per_rank`, `max_rank`, `cost`, `cost_growth`, "
        "`icon`, `tree`, `tree_row`, `tree_column`, `required_id`, `required_rank` FROM `bonk_upgrade`"));
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk upgrades.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkUpgrade upgrade;
        upgrade.id = fields[0].GetUInt32();
        upgrade.name = fields[1].GetCppString();
        upgrade.description = fields[2].GetCppString();
        uint8 const effect = fields[3].GetUInt8();
        upgrade.valuePerRank = fields[4].GetFloat();
        upgrade.maxRank = std::max<uint8>(1, fields[5].GetUInt8());
        upgrade.cost = fields[6].GetUInt32();
        upgrade.costGrowth = fields[7].GetFloat();
        upgrade.layout.icon = fields[8].GetCppString();
        upgrade.layout.tree = fields[9].GetUInt32();
        upgrade.layout.row = fields[10].GetUInt8();
        upgrade.layout.column = fields[11].GetUInt8();
        upgrade.layout.requiredId = fields[12].GetUInt32();
        upgrade.layout.requiredRank = fields[13].GetUInt8();

        if (effect >= MAX_BONK_PASSIVE_EFFECT)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_upgrade`: upgrade %u has unknown effect %u, skipped.", upgrade.id, effect);
            continue;
        }
        upgrade.effect = BonkPassiveEffect(effect);

        m_upgrades[upgrade.id] = std::move(upgrade);
    } while (result->NextRow());

    CheckShopLayout("bonk_upgrade", BONK_SHOP_UPGRADES, m_upgrades);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk upgrades", uint32(m_upgrades.size()));
}

void BonkMgr::LoadTalents()
{
    m_talents.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `description`, `effect`, `value_per_rank`, `gold_bonus`, `max_rank`, `cost`, `cost_growth`, "
        "`icon`, `tree`, `tree_row`, `tree_column`, `required_id`, `required_rank` FROM `bonk_talent`"));
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk talents.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkTalent talent;
        talent.id = fields[0].GetUInt32();
        talent.name = fields[1].GetCppString();
        talent.description = fields[2].GetCppString();
        uint8 const effect = fields[3].GetUInt8();
        talent.valuePerRank = fields[4].GetFloat();
        talent.goldBonus = fields[5].GetFloat();
        talent.maxRank = std::max<uint8>(1, fields[6].GetUInt8());
        talent.cost = fields[7].GetUInt32();
        talent.costGrowth = fields[8].GetFloat();
        talent.layout.icon = fields[9].GetCppString();
        talent.layout.tree = fields[10].GetUInt32();
        talent.layout.row = fields[11].GetUInt8();
        talent.layout.column = fields[12].GetUInt8();
        talent.layout.requiredId = fields[13].GetUInt32();
        talent.layout.requiredRank = fields[14].GetUInt8();

        if (effect >= MAX_BONK_TALENT_EFFECT)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_talent`: talent %u has unknown effect %u, skipped.", talent.id, effect);
            continue;
        }
        talent.effect = BonkTalentEffect(effect);

        // A toggle is either owned or not.
        if (talent.IsToggle())
            talent.maxRank = 1;

        m_talents[talent.id] = std::move(talent);
    } while (result->NextRow());

    CheckShopLayout("bonk_talent", BONK_SHOP_TALENTS, m_talents);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk talents", uint32(m_talents.size()));
}

void BonkMgr::LoadItemPower()
{
    m_itemPower.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `entry`, `power` FROM `bonk_item_power`"));
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk gear items.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 const entry = fields[0].GetUInt32();
        if (!sObjectMgr.GetItemPrototype(entry))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_item_power`: item %u does not exist, skipped.", entry);
            continue;
        }
        m_itemPower[entry] = fields[1].GetFloat();
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk gear items", uint32(m_itemPower.size()));
}

BonkUpgrade const* BonkMgr::GetUpgrade(uint32 id) const
{
    auto itr = m_upgrades.find(id);
    return itr != m_upgrades.end() ? &itr->second : nullptr;
}

BonkTalent const* BonkMgr::GetTalent(uint32 id) const
{
    auto itr = m_talents.find(id);
    return itr != m_talents.end() ? &itr->second : nullptr;
}

float BonkMgr::GetItemPower(uint32 itemEntry) const
{
    auto itr = m_itemPower.find(itemEntry);
    return itr != m_itemPower.end() ? itr->second : 0.0f;
}

BonkTier const* BonkMgr::GetTier(uint32 id) const
{
    auto itr = m_tiers.find(id);
    return itr != m_tiers.end() ? &itr->second : nullptr;
}

BonkTier const* BonkMgr::FindTier(std::string const& keyOrId) const
{
    for (auto const& itr : m_tiers)
        if (itr.second.key == keyOrId || std::to_string(itr.first) == keyOrId)
            return &itr.second;
    return nullptr;
}

BonkMap const* BonkMgr::GetMap(uint32 id) const
{
    auto itr = m_maps.find(id);
    return itr != m_maps.end() ? &itr->second : nullptr;
}

BonkWeapon const* BonkMgr::GetWeapon(uint32 id) const
{
    auto itr = m_weapons.find(id);
    return itr != m_weapons.end() ? &itr->second : nullptr;
}

BonkWeapon const* BonkMgr::GetEvolution(uint32 weaponId) const
{
    for (auto const& itr : m_weapons)
        if (itr.second.evolvesFrom == weaponId)
            return &itr.second;
    return nullptr;
}

BonkMob const* BonkMgr::GetMob(uint32 entry) const
{
    auto itr = m_mobs.find(entry);
    return itr != m_mobs.end() ? &itr->second : nullptr;
}

BonkPassive const* BonkMgr::GetPassive(uint32 id) const
{
    auto itr = m_passives.find(id);
    return itr != m_passives.end() ? &itr->second : nullptr;
}

bool BonkMgr::IsArenaLocation(uint32 mapId, uint32 zoneId) const
{
    auto itr = m_arenaZonesByMap.find(mapId);
    if (itr == m_arenaZonesByMap.end())
        return false;
    return itr->second.count(0) || itr->second.count(zoneId);
}

bool BonkMgr::IsStaticCreatureSuppressed(Map const* map, uint32 dbGuid) const
{
    auto itr = m_arenaZonesByMap.find(map->GetId());
    if (itr == m_arenaZonesByMap.end())
        return false;

    if (itr->second.count(0))
        return true;

    CreatureData const* data = sObjectMgr.GetCreatureData(dbGuid);
    if (!data)
        return false;

    uint32 const zoneId = map->GetTerrain()->GetZoneId(data->position.x, data->position.y, data->position.z);
    return itr->second.count(zoneId) != 0;
}
