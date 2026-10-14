/*
 * Vanilla Bonk: static game-mode data.
 */

#include "BonkMgr.h"
#include "BonkMastery.h"
#include "BonkQuests.h"
#include "Database/DatabaseEnv.h"
#include "DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Policies/SingletonImp.h"
#include "SpellMgr.h"

#include <cmath>
#include <tuple>

INSTANTIATE_SINGLETON_1(BonkMgr);

namespace
{
    // bonk_item_stat: the column for each stat camp gear can carry.
    struct GearColumn
    {
        char const* name;
        BonkPassiveEffect effect;
    };
    GearColumn const BONK_GEAR_COLUMNS[] =
    {
        { "power",        BONK_PASSIVE_POWER },
        { "armor",        BONK_PASSIVE_ARMOR },
        { "dodge",        BONK_PASSIVE_DODGE },
        { "hit",          BONK_PASSIVE_HIT },
        { "crit",         BONK_PASSIVE_CRIT },
        { "crit_damage",  BONK_PASSIVE_CRIT_DAMAGE },
        { "shield",       BONK_PASSIVE_SHIELD },
        { "haste",        BONK_PASSIVE_HASTE },
        { "area",         BONK_PASSIVE_AREA },
        { "pickup_range", BONK_PASSIVE_PICKUP },
        { "luck",         BONK_PASSIVE_LUCK },
    };
    size_t const BONK_GEAR_COLUMN_COUNT = sizeof(BONK_GEAR_COLUMNS) / sizeof(BONK_GEAR_COLUMNS[0]);

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

    // Whether the spell's damage comes from the weapon swing.
    bool HasWeaponDamage(SpellEntry const* spellInfo)
    {
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            switch (spellInfo->Effect[i])
            {
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                    return true;
                default:
                    break;
            }
        return false;
    }

    // Damage over time, on the target or on the ground, other than effect
    // `except`: the effect index, -1 for none.
    int32 FindTickEffect(SpellEntry const* spellInfo, int32 except)
    {
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (int32(i) != except && (spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA || spellInfo->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA) &&
                spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
                return int32(i);
        return -1;
    }
}

void BonkMgr::Load()
{
    LoadTiers();
    LoadArchetypes();
    LoadSpawnCurve();
    LoadMaps();
    LoadMapCreatures();
    LoadMapEvents();
    // Weapons name the keywords they apply and pay off.
    LoadKeywords();
    LoadProjectiles();
    LoadWeapons();
    LoadPassives();
    LoadPowerups();
    LoadShopTrees();
    LoadUpgrades();
    LoadTalents();
    LoadItemStats();
    LoadMobs();
    // Quests check the weapons, passives, talents and upgrades they unlock.
    sBonkQuests.Load();
    sBonkMastery.Load();

    for (uint32 entry : { BONK_GO_CHEST, BONK_GO_CHEST_FREE, BONK_GO_PORTAL, BONK_GO_SHARD })
        if (!sObjectMgr.GetGameObjectTemplate(entry))
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Vanilla Bonk: gameobject template %u is missing; runs will lack chests or portals, and give shards without a drop.", entry);

    for (uint32 entry : { BONK_ORB_SMALL, BONK_ORB_LARGE, BONK_ORB_HUGE })
        if (!sObjectMgr.GetCreatureTemplate(entry))
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Vanilla Bonk: creature template %u is missing; kills will give experience directly.", entry);

    if (!sObjectMgr.GetCreatureTemplate(BONK_SANGUINE_POOL))
        sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Vanilla Bonk: creature template %u is missing; Sanguine pools will heal without a look.", BONK_SANGUINE_POOL);
}

void BonkMgr::LoadTiers()
{
    m_tiers.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `key`, `name`, `health_multiplier`, `damage_multiplier`, `stage_health_growth`, `stage_damage_growth`, "
        "`stage_health_accel`, `chest_cost`, `chest_cost_growth`, `unlock_tier`, `unlock_stage`, "
        "`shard_chance_normal`, `shard_chance_elite`, `shards_miniboss`, `shards_boss`, `description`, `art`, "
        "`mastery_xp_minute`, `mastery_xp_miniboss`, `mastery_xp_boss`, `elite_dodge`, `elite_affixes`, "
        "`stage_damage_accel`, `min_alive_multiplier`, `max_alive`, `max_bruisers`, `first_elite_sec`, `charge_interval_ms` FROM `bonk_tier`"));

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
        tier.stageHealthAccel = fields[7].GetFloat();
        tier.chestCost = fields[8].GetUInt32();
        tier.chestCostGrowth = fields[9].GetFloat();
        tier.unlockTierId = fields[10].GetUInt32();
        tier.unlockStage = fields[11].GetUInt32();
        tier.shardChanceNormal = fields[12].GetFloat();
        tier.shardChanceElite = fields[13].GetFloat();
        tier.shardsMiniboss = fields[14].GetUInt32();
        tier.shardsBoss = fields[15].GetUInt32();
        tier.description = fields[16].GetCppString();
        tier.art = fields[17].GetCppString();
        tier.masteryXpMinute = fields[18].GetUInt32();
        tier.masteryXpMiniboss = fields[19].GetUInt32();
        tier.masteryXpBoss = fields[20].GetUInt32();
        tier.eliteDodge = fields[21].GetFloat();
        tier.eliteAffixes = fields[22].GetUInt8();
        tier.stageDamageAccel = fields[23].GetFloat();
        tier.minAliveMultiplier = fields[24].GetFloat();
        tier.maxAlive = std::max(1u, fields[25].GetUInt32());
        tier.maxBruisers = fields[26].GetUInt32();
        tier.firstEliteSecond = fields[27].GetUInt32();
        tier.chargeIntervalMs = fields[28].GetUInt32();

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
        "`gold_per_kill`, `stage_gold`, `coins_per_kill`, `powerup_interval_sec`, `powerup_max`, "
        "`spawn_min_distance`, `spawn_max_distance`, `icon`, `damage_growth_per_min`, `map_type` FROM `bonk_map`"));

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
        map.goldPerKill = fields[15].GetUInt32();
        map.stageGold = fields[16].GetUInt32();
        map.coinsPerKill = fields[17].GetUInt32();
        map.powerupIntervalSeconds = fields[18].GetUInt32();
        map.powerupMax = fields[19].GetUInt32();
        map.spawnMinDistance = std::max(5.0f, fields[20].GetFloat());
        map.spawnMaxDistance = std::max(map.spawnMinDistance, fields[21].GetFloat());
        map.icon = fields[22].GetCppString();
        map.damageGrowthPerMinute = fields[23].GetFloat();
        map.type = BonkMapType(std::min<uint8>(fields[24].GetUInt8(), MAX_BONK_MAP_TYPE - 1));

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

void BonkMgr::LoadArchetypes()
{
    for (BonkArchetypeInfo& archetype : m_archetypes)
        archetype = BonkArchetypeInfo();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `health_multiplier`, `damage_multiplier`, `experience`, `first_sec`, `first_sec_later`, `orb` FROM `bonk_archetype`"));
    uint32 count = 0;
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 const id = fields[0].GetUInt8();
            if (id >= MAX_BONK_ARCHETYPE)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_archetype`: unknown archetype %u, skipped.", id);
                continue;
            }

            BonkArchetypeInfo& archetype = m_archetypes[id];
            archetype.name = fields[1].GetCppString();
            archetype.healthMultiplier = fields[2].GetFloat();
            archetype.damageMultiplier = fields[3].GetFloat();
            archetype.experience = fields[4].GetFloat();
            archetype.firstSecond = fields[5].GetUInt32();
            archetype.firstSecondLater = fields[6].GetUInt32();
            archetype.orb = BonkOrb(std::min<uint8>(fields[7].GetUInt8(), BONK_ORB_RED));
            ++count;
        } while (result->NextRow());
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk enemy archetypes", count);
}

void BonkMgr::LoadSpawnCurve()
{
    m_spawnCurve.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `second`, `min_alive`, `bruiser_share`, `elite_interval_sec` FROM `bonk_spawn_curve` ORDER BY `second`"));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BonkSpawnPoint point;
            point.second = fields[0].GetUInt32();
            point.minAlive = std::max(0.0f, fields[1].GetFloat());
            point.bruiserShare = std::min(1.0f, std::max(0.0f, fields[2].GetFloat() / 100.0f));
            point.eliteIntervalSeconds = fields[3].GetUInt32();
            m_spawnCurve.push_back(point);
        } while (result->NextRow());
    }

    if (m_spawnCurve.empty())
    {
        sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_spawn_curve` is empty; runs keep a small crowd.");
        BonkSpawnPoint point;
        point.minAlive = 20.0f;
        point.eliteIntervalSeconds = 90;
        m_spawnCurve.push_back(point);
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk spawn curve points", uint32(m_spawnCurve.size()));
}

void BonkMgr::LoadMapCreatures()
{
    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `bonk_map_id`, `entry`, `kind`, `min_sec`, `max_sec`, `weight`, `health_multiplier`, `damage_multiplier`, `speed`, `scale`, `max_alive`, "
        "`archetype` FROM `bonk_map_creature`"));

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
        creature.maxAlive = fields[10].GetUInt32();
        uint8 const archetype = fields[11].GetUInt8();
        if (archetype < MAX_BONK_ARCHETYPE)
            creature.archetype = BonkArchetype(archetype);
        else
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_map_creature`: creature %u has unknown archetype %u; it is a grunt.", creature.entry, archetype);

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
        // Sorted by second: the first mini-boss event is when it arrives.
        if (event.type == BONK_EVENT_MINIBOSS && !map->second.minibossSecond)
            map->second.minibossSecond = event.second;

        map->second.events.push_back(event);
        ++count;
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk map events", count);
}

void BonkMgr::LoadKeywords()
{
    m_keywords.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `icon`, `spell_id`, `max_stacks`, `value_per_stack`, `tick_ms`, `duration_ms`, `spreads_on_death` FROM `bonk_keyword`"));
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk keywords.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkKeywordInfo keyword;
        uint8 const id = fields[0].GetUInt8();
        keyword.id = BonkKeyword(id);
        keyword.name = fields[1].GetCppString();
        keyword.icon = fields[2].GetCppString();
        keyword.spellId = fields[3].GetUInt32();
        keyword.maxStacks = std::max<uint8>(1, fields[4].GetUInt8());
        keyword.valuePerStack = fields[5].GetFloat();
        keyword.tickMs = fields[6].GetUInt32();
        keyword.durationMs = fields[7].GetUInt32();
        keyword.spreadsOnDeath = fields[8].GetUInt8() != 0;

        if (id == BONK_KEYWORD_NONE || id >= MAX_BONK_KEYWORD)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_keyword`: unknown keyword %u, skipped.", uint32(id));
            continue;
        }
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(keyword.spellId);
        if (!spellInfo || !spellInfo->IsSpellAppliesAura((1 << EFFECT_INDEX_0) | (1 << EFFECT_INDEX_1) | (1 << EFFECT_INDEX_2)))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_keyword`: keyword %u uses spell %u, which is missing or applies no aura; skipped.", uint32(id), keyword.spellId);
            continue;
        }
        if (keyword.IsDamageOverTime() && !keyword.tickMs)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_keyword`: damage over time keyword %u has no tick_ms; it ticks every second.", uint32(id));
            keyword.tickMs = IN_MILLISECONDS;
        }
        m_keywords[id] = std::move(keyword);
    } while (result->NextRow());

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk keywords", uint32(m_keywords.size()));
}

void BonkMgr::LoadProjectiles()
{
    m_projectiles.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `spell_id`, `display_id`, `speed`, `radius`, `scale` FROM `bonk_projectile`"));
    if (!result)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded 0 Bonk projectiles.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        BonkProjectileInfo projectile;
        projectile.spellId = fields[0].GetUInt32();
        projectile.displayId = fields[1].GetUInt32();
        projectile.speed = fields[2].GetFloat();
        projectile.radius = std::max(0.1f, fields[3].GetFloat());
        projectile.scale = fields[4].GetFloat() > 0.0f ? fields[4].GetFloat() : 1.0f;

        // Without the client patch's display (and the server's copy of
        // its tables) the spell still lands on its target.
        if (!sSpellMgr.GetSpellEntry(projectile.spellId) || projectile.speed <= 0.0f)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_projectile`: spell %u is missing or has no speed; skipped.", projectile.spellId);
            continue;
        }
        if (!sCreatureDisplayInfoStore.LookupEntry(projectile.displayId) ||
            !sCreatureDisplayInfoAddonStorage.LookupEntry<CreatureDisplayInfoAddon>(projectile.displayId))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_projectile`: spell %u uses display %u, which the server's tables lack (build the client patch, tools/build-telegraphs.ps1); skipped.",
                projectile.spellId, projectile.displayId);
            continue;
        }
        m_projectiles[projectile.spellId] = projectile;
    } while (result->NextRow());

    if (!sObjectMgr.GetCreatureTemplate(BONK_PROJECTILE))
    {
        sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Vanilla Bonk: creature template %u is missing; spells land on their targets instead of flying.", BONK_PROJECTILE);
        m_projectiles.clear();
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk projectiles", uint32(m_projectiles.size()));
}

BonkProjectileInfo const* BonkMgr::GetProjectile(uint32 spellId) const
{
    auto itr = m_projectiles.find(spellId);
    return itr != m_projectiles.end() ? &itr->second : nullptr;
}

BonkKeywordInfo const* BonkMgr::GetKeyword(BonkKeyword id) const
{
    auto itr = m_keywords.find(uint8(id));
    return itr != m_keywords.end() ? &itr->second : nullptr;
}

BonkKeywordInfo const* BonkMgr::GetKeywordBySpell(uint32 spellId) const
{
    for (auto const& itr : m_keywords)
        if (itr.second.spellId == spellId)
            return &itr.second;
    return nullptr;
}

void BonkMgr::LoadWeapons()
{
    m_weapons.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `icon`, `description`, `spell_id`, `target`, `cooldown_ms`, `range`, `damage_fraction`, "
        "`class_mask`, `starter_class_mask`, `evolves_from`, `heal_pct`, `duration_ms`, "
        "`chains`, `behavior`, `behavior_value`, `tick_ms`, `base_targets`, `alt_spell_id`, `alt_every`, `alt_damage`, `base_jumps`, `kill_below_pct`, `flags`, "
        "`applies`, `apply_stacks`, `uses`, `uses_pct` "
        "FROM `bonk_weapon`"));

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
        weapon.target = BonkWeaponTarget(std::min<uint8>(fields[5].GetUInt8(), MAX_BONK_TARGET - 1));
        weapon.cooldownMs = std::max(200u, fields[6].GetUInt32());
        weapon.range = fields[7].GetFloat();
        weapon.damageFraction = fields[8].GetFloat();
        weapon.classMask = fields[9].GetUInt32();
        weapon.starterClassMask = fields[10].GetUInt32();
        weapon.evolvesFrom = fields[11].GetUInt32();
        weapon.healPct = fields[12].GetFloat();
        weapon.durationMs = fields[13].GetUInt32();
        weapon.chains = fields[14].GetUInt8() != 0;
        weapon.behavior = BonkWeaponBehavior(std::min<uint8>(fields[15].GetUInt8(), MAX_BONK_BEHAVIOR - 1));
        weapon.behaviorValue = fields[16].GetFloat();
        weapon.tickMs = fields[17].GetUInt32();
        weapon.baseTargets = std::max<uint8>(1, fields[18].GetUInt8());
        weapon.altSpellId = fields[19].GetUInt32();
        weapon.altEvery = fields[20].GetUInt8();
        weapon.altDamage = fields[21].GetFloat();
        weapon.baseJumps = fields[22].GetUInt8();
        weapon.killBelowPct = fields[23].GetFloat();
        weapon.flags = fields[24].GetUInt32();
        weapon.applies = BonkKeyword(fields[25].GetUInt8());
        weapon.applyStacks = fields[26].GetUInt8();
        weapon.uses = BonkKeyword(fields[27].GetUInt8());
        weapon.usesPct = fields[28].GetFloat();

        if (weapon.applies != BONK_KEYWORD_NONE && !GetKeyword(weapon.applies))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: weapon %u applies unknown keyword %u; it applies none.", weapon.id, uint32(weapon.applies));
            weapon.applies = BONK_KEYWORD_NONE;
        }
        if (weapon.uses != BONK_KEYWORD_NONE && weapon.uses != BONK_KEYWORD_VARIETY && !GetKeyword(weapon.uses))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: weapon %u pays off unknown keyword %u; it pays off none.", weapon.id, uint32(weapon.uses));
            weapon.uses = BONK_KEYWORD_NONE;
        }

        if (weapon.altSpellId && !sSpellMgr.GetSpellEntry(weapon.altSpellId))
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: weapon %u has unknown alt spell %u; it is left out.", weapon.id, weapon.altSpellId);
            weapon.altSpellId = 0;
        }

        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
        if (!spellInfo)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: weapon %u uses unknown spell %u, skipped.", weapon.id, weapon.spellId);
            continue;
        }

        weapon.damageEffectIndex = FindDamageEffect(spellInfo);
        weapon.weaponDamage = weapon.damageEffectIndex < 0 && HasWeaponDamage(spellInfo);
        // Shields, marks, shouts, buffs and beasts deal no damage of their
        // own (their ranks make them stronger); the rest need a damage effect.
        bool damageless = false;
        switch (weapon.behavior)
        {
            case BONK_BEHAVIOR_SHIELD:
            case BONK_BEHAVIOR_WARD:
            case BONK_BEHAVIOR_MARK:
            case BONK_BEHAVIOR_DEMORALIZE:
            case BONK_BEHAVIOR_BUFF:
            case BONK_BEHAVIOR_PET:
                damageless = true;
                break;
            default:
                break;
        }
        if (weapon.damageEffectIndex < 0 && !weapon.weaponDamage && !damageless)
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: spell %u of weapon %u has no damage effect; it will use its own values.", weapon.spellId, weapon.id);

        weapon.tickEffectIndex = FindTickEffect(spellInfo, weapon.damageEffectIndex);
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA && spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
            {
                weapon.groundRadius = Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(spellInfo->EffectRadiusIndex[i]));
                break;
            }
        // A trap's radius is its range, whatever its spell's own.
        if (weapon.behavior == BONK_BEHAVIOR_TRAP)
        {
            weapon.groundRadius = weapon.range;
            if (!sObjectMgr.GetGameObjectTemplate(uint32(weapon.behaviorValue)))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: trap %u uses unknown gameobject %u; skipped.", weapon.id, uint32(weapon.behaviorValue));
                continue;
            }
        }
        if (weapon.target == BONK_TARGET_GROUND && weapon.groundRadius <= 0.0f)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_weapon`: ground weapon %u uses spell %u, which has no damaging ground effect; skipped.", weapon.id, weapon.spellId);
            continue;
        }

        weapon.lasting = weapon.target == BONK_TARGET_GROUND || weapon.tickEffectIndex >= 0 || weapon.durationMs ||
            (weapon.damageEffectIndex >= 0 && spellInfo->EffectApplyAuraName[weapon.damageEffectIndex] == SPELL_AURA_PERIODIC_DAMAGE);

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
        "SELECT `id`, `name`, `icon`, `description`, `effect`, `value_per_rank`, `max_rank`, `excludes` FROM `bonk_passive`"));

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
        passive.excludes = fields[7].GetUInt32();

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

uint32 GetBonkMapRewardPercent(BonkMapType type)
{
    switch (type)
    {
        case BONK_MAP_ENCLOSED: return 125;
        case BONK_MAP_ARENA:    return 150;
        default:                return 100;
    }
}

std::string FormatBonkShards(uint32 count)
{
    return std::to_string(count) + (count == 1 ? " Darkmoon Ticket" : " Darkmoon Tickets");
}

char const* GetBonkRarityName(BonkRarity rarity)
{
    switch (rarity)
    {
        case BONK_RARITY_UNCOMMON:  return "Uncommon";
        case BONK_RARITY_RARE:      return "Rare";
        case BONK_RARITY_EPIC:      return "Epic";
        case BONK_RARITY_LEGENDARY: return "Legendary";
        default:                    return "Common";
    }
}

uint32 BonkUpgrade::GetCost(uint8 ownedRanks) const
{
    // Each rank costs costGrowth times the one before, to the nearest silver.
    uint32 const copper = uint32(cost * std::pow(costGrowth, float(ownedRanks)) + 0.5f);
    return copper >= 100 ? (copper + 50) / 100 * 100 : copper;
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

void BonkMgr::LoadItemStats()
{
    m_itemStats.clear();
    m_itemSets.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `id`, `name` FROM `bonk_item_set`"));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BonkItemSet set;
            set.id = fields[0].GetUInt32();
            set.name = fields[1].GetCppString();
            m_itemSets[set.id] = std::move(set);
        } while (result->NextRow());
    }

    std::string columns;
    for (auto const& column : BONK_GEAR_COLUMNS)
        columns += ", `" + std::string(column.name) + "`";
    result = std::unique_ptr<QueryResult>(WorldDatabase.Query(("SELECT `entry`, `set_id`" + columns + " FROM `bonk_item_stat` ORDER BY `entry`").c_str()));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const entry = fields[0].GetUInt32();
            if (!sObjectMgr.GetItemPrototype(entry))
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_item_stat`: item %u does not exist, skipped.", entry);
                continue;
            }

            BonkItemStat stat;
            stat.setId = fields[1].GetUInt32();
            for (size_t i = 0; i < BONK_GEAR_COLUMN_COUNT; ++i)
                stat.stats[BONK_GEAR_COLUMNS[i].effect] = fields[2 + i].GetFloat();
            if (stat.setId)
            {
                auto set = m_itemSets.find(stat.setId);
                if (set == m_itemSets.end())
                {
                    sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_item_stat`: item %u is in unknown set %u, set ignored.", entry, stat.setId);
                    stat.setId = 0;
                }
                else
                    set->second.items.push_back(entry);
            }
            m_itemStats[entry] = stat;
        } while (result->NextRow());
    }

    result = std::unique_ptr<QueryResult>(WorldDatabase.Query("SELECT `set_id`, `pieces`, `power`, `armor`, `passive_effect`, `passive_value`, `description` "
        "FROM `bonk_item_set_bonus` ORDER BY `set_id`, `pieces`"));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const setId = fields[0].GetUInt32();
            auto set = m_itemSets.find(setId);
            if (set == m_itemSets.end())
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_item_set_bonus`: unknown set %u, skipped.", setId);
                continue;
            }

            BonkItemSetBonus bonus;
            bonus.pieces = std::max<uint8>(1, fields[1].GetUInt8());
            bonus.stats[BONK_PASSIVE_POWER] += fields[2].GetFloat();
            bonus.stats[BONK_PASSIVE_ARMOR] += fields[3].GetFloat();
            uint8 const effect = fields[4].GetUInt8();
            if (effect < MAX_BONK_PASSIVE_EFFECT)
                bonus.stats[effect] += fields[5].GetFloat();
            else if (effect != 255)
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_item_set_bonus`: set %u has unknown passive effect %u, ignored.", setId, effect);
            bonus.description = fields[6].GetCppString();
            set->second.bonuses.push_back(std::move(bonus));
        } while (result->NextRow());
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk gear items in %u sets", uint32(m_itemStats.size()), uint32(m_itemSets.size()));
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

BonkItemStat const* BonkMgr::GetItemStat(uint32 itemEntry) const
{
    auto itr = m_itemStats.find(itemEntry);
    return itr != m_itemStats.end() ? &itr->second : nullptr;
}

BonkItemSet const* BonkMgr::GetItemSet(uint32 id) const
{
    auto itr = m_itemSets.find(id);
    return itr != m_itemSets.end() ? &itr->second : nullptr;
}

BonkGearTotals BonkMgr::EvaluateGear(Player const* player) const
{
    BonkGearTotals totals;
    std::map<uint32, uint32> setPieces;

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (BonkItemStat const* stat = GetItemStat(item->GetEntry()))
            {
                for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
                    totals.passives[effect] += stat->stats[effect];
                if (stat->setId)
                    ++setPieces[stat->setId];
            }

    for (auto const& itr : setPieces)
        if (BonkItemSet const* set = GetItemSet(itr.first))
            for (BonkItemSetBonus const& bonus : set->bonuses)
            {
                if (itr.second < bonus.pieces)
                    continue;
                for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
                    totals.passives[effect] += bonus.stats[effect];
            }

    return totals;
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

std::string BonkMgr::DescribePassive(BonkPassive const& passive) const
{
    for (auto const& itr : m_passives)
        if (itr.first != passive.id && (itr.first == passive.excludes || itr.second.excludes == passive.id))
            return passive.description + " Can't be taken with " + itr.second.name + ".";
    return passive.description;
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
