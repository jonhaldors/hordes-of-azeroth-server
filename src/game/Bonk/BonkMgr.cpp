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

INSTANTIATE_SINGLETON_1(BonkMgr);

char const* BonkMap::GetDifficultyName() const
{
    switch (difficulty)
    {
        case 0: return "Easy";
        case 1: return "Normal";
        default: return "Hard";
    }
}

void BonkMgr::Load()
{
    LoadMaps();
    LoadMapCreatures();
    LoadWeapons();
    LoadPassives();
}

void BonkMgr::LoadMaps()
{
    m_maps.clear();
    m_arenaZonesByMap.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `key`, `name`, `difficulty`, `map_id`, `zone_id`, `position_x`, `position_y`, `position_z`, `orientation`, "
        "`duration_sec`, `level_offset`, `health_multiplier`, `damage_multiplier`, `health_growth_per_min`, "
        "`spawn_interval_ms`, `spawn_batch`, `spawn_batch_growth_per_min`, `max_alive`, `elite_interval_sec`, "
        "`gold_per_kill`, `victory_gold` FROM `bonk_map`"));

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
        map.difficulty = fields[3].GetUInt8();
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
        map.victoryGold = fields[21].GetUInt32();

        if (!sMapStorage.LookupEntry<MapEntry>(map.mapId))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map`: map %u (%s) does not exist, skipped.", map.mapId, map.name.c_str());
            continue;
        }

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
        creature.kind = BonkSpawnKind(std::min<uint8>(fields[2].GetUInt8(), BONK_SPAWN_BOSS));
        creature.minSecond = fields[3].GetUInt32();
        creature.maxSecond = fields[4].GetUInt32();
        creature.weight = std::max(1u, fields[5].GetUInt32());
        creature.healthMultiplier = fields[6].GetFloat();
        creature.damageMultiplier = fields[7].GetFloat();
        creature.speed = fields[8].GetFloat();
        creature.scale = fields[9].GetFloat();

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

void BonkMgr::LoadWeapons()
{
    m_weapons.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `icon`, `description`, `spell_id`, `target`, `cooldown_ms`, `range`, `damage_fraction`, "
        "`class_mask`, `starter_class_mask`, `max_level` FROM `bonk_weapon`"));

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

        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
        if (!spellInfo)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: weapon %u uses unknown spell %u, skipped.", weapon.id, weapon.spellId);
            continue;
        }

        // Direct damage wins over damage over time.
        for (uint32 i = 0; i < MAX_EFFECT_INDEX && weapon.damageEffectIndex < 0; ++i)
            if (spellInfo->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                weapon.damageEffectIndex = i;

        for (uint32 i = 0; i < MAX_EFFECT_INDEX && weapon.damageEffectIndex < 0; ++i)
            if ((spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA || spellInfo->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA) &&
                spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
                weapon.damageEffectIndex = i;

        if (weapon.damageEffectIndex < 0)
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: spell %u of weapon %u has no damage effect; it will use its own values.", weapon.spellId, weapon.id);

        m_weapons[weapon.id] = std::move(weapon);
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk weapons", uint32(m_weapons.size()));
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

BonkMap const* BonkMgr::GetMap(uint32 id) const
{
    auto itr = m_maps.find(id);
    return itr != m_maps.end() ? &itr->second : nullptr;
}

BonkMap const* BonkMgr::FindMap(std::string const& keyOrId) const
{
    for (auto const& itr : m_maps)
        if (itr.second.key == keyOrId || std::to_string(itr.first) == keyOrId)
            return &itr.second;
    return nullptr;
}

BonkWeapon const* BonkMgr::GetWeapon(uint32 id) const
{
    auto itr = m_weapons.find(id);
    return itr != m_weapons.end() ? &itr->second : nullptr;
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
