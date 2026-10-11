/*
 * Vanilla Bonk: the run loop.
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellMgr.h"
#include "World.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <cmath>

namespace
{
    uint32 const BONK_ENTER_TIMEOUT_MS     = 30000;
    uint32 const BONK_END_DELAY_MS         = 4000;
    uint32 const BONK_STATE_INTERVAL_MS    = 1000;
    uint32 const BONK_BOUNDARY_INTERVAL_MS = 500;
    uint32 const BONK_FIRST_WAVE_MS        = 1500;
    uint32 const BONK_ENEMY_FACTION        = 14;    // Monster: hostile to everyone
    // Summons only count down when they despawn on the summoner's map, so the
    // count drifts a little every run; the run enforces max_alive itself.
    uint32 const BONK_SUMMON_LIMIT         = 1000;
    // Chests and portals despawn on their own after this long if the run
    // could not remove them (logout during a teleport).
    uint32 const BONK_OBJECT_LIFETIME_SEC  = 3600;

    float const BONK_SPAWN_MAX_HEIGHT_DIFF = 20.0f;
    float const BONK_CHEST_MIN_DISTANCE    = 15.0f;
    float const BONK_CHEST_MAX_DISTANCE    = 45.0f;
    float const BONK_PORTAL_DISTANCE       = 6.0f;
    float const BONK_POWERUP_MIN_DISTANCE  = 20.0f;
    float const BONK_POWERUP_MAX_DISTANCE  = 50.0f;
    float const BONK_POWERUP_PICKUP_RANGE  = 3.0f;
    uint32 const BONK_POWERUP_LIFETIME_SEC = 90;
    uint32 const BONK_POWERUP_CHECK_MS     = 250;
    // A swarm may push the enemy count this far past max_alive.
    float const BONK_SWARM_OVERFLOW        = 1.5f;

    size_t const BONK_MAX_WEAPONS  = 6;
    size_t const BONK_MAX_PASSIVES = 6;
    size_t const BONK_OFFER_SIZE   = 3;

    // Passive upgrades that need a real aura on the player.
    uint32 const BONK_SPELL_MOVE_SPEED = 17498;     // Speed: SPELL_AURA_MOD_INCREASE_SPEED
    uint32 const BONK_SPELL_MAX_HEALTH = 19255;     // Survivalist: SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT

    float GetKindHealth(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 10.0f;
            case BONK_SPAWN_MINIBOSS: return 40.0f;
            case BONK_SPAWN_BOSS:     return 150.0f;
            default:                  return 1.0f;
        }
    }

    float GetKindDamage(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 2.0f;
            case BONK_SPAWN_MINIBOSS: return 3.0f;
            case BONK_SPAWN_BOSS:     return 4.0f;
            default:                  return 1.0f;
        }
    }

    // Gold, coins and experience, in normal kills.
    uint32 GetKindReward(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 5;
            case BONK_SPAWN_MINIBOSS: return 20;
            case BONK_SPAWN_BOSS:     return 50;
            default:                  return 1;
        }
    }

    uint32 GetKindLevelBonus(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 1;
            case BONK_SPAWN_MINIBOSS: return 2;
            case BONK_SPAWN_BOSS:     return 2;
            default:                  return 0;
        }
    }

    std::string FormatTime(uint32 seconds)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%u:%02u", seconds / 60, seconds % 60);
        return buffer;
    }

    std::string FormatGold(uint32 copper)
    {
        char buffer[48];
        snprintf(buffer, sizeof(buffer), "%ug %us %uc", copper / 10000, (copper / 100) % 100, copper % 100);
        return buffer;
    }
}

BonkRun::BonkRun(Player* player, BonkTier const& tier) : m_player(player), m_tier(tier)
{
}

bool BonkRun::Begin(std::string& error)
{
    BonkMap const* first = ChooseNextMap();
    if (!first)
    {
        error = "No maps are set up for " + m_tier.name + ".";
        return false;
    }
    m_map = *first;

    // GM mode makes the player untargetable, which would make the run trivial.
    if (m_player->IsGameMaster())
        m_player->SetGameMaster(false);

    m_player->GetBonkPlayer()->ResetCharacter();
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);

    return TeleportToStage(m_map, error);
}

BonkMap const* BonkRun::ChooseNextMap()
{
    // Each lap plays every map of the pool once in random order, and the
    // same map never comes twice in a row unless it is the only one.
    std::vector<BonkMap const*> pool;
    for (uint32 id : m_tier.mapIds)
        if (BonkMap const* map = sBonkMgr.GetMap(id))
            if (!map->creatures.empty())
                pool.push_back(map);

    if (pool.empty())
        return nullptr;

    std::vector<BonkMap const*> candidates;
    for (BonkMap const* map : pool)
        if (!m_playedMaps.count(map->id))
            candidates.push_back(map);

    if (candidates.empty())
    {
        m_playedMaps.clear();
        for (BonkMap const* map : pool)
            if (pool.size() == 1 || map->id != m_map.id)
                candidates.push_back(map);
    }

    BonkMap const* map = candidates[urand(0, uint32(candidates.size() - 1))];
    m_playedMaps.insert(map->id);
    return map;
}

bool BonkRun::TeleportToStage(BonkMap const& map, std::string& error)
{
    if (!m_player->TeleportTo(map.mapId, map.x, map.y, map.z, map.o))
    {
        error = "Could not enter " + map.name + ".";
        return false;
    }

    m_state = STATE_ENTERING;
    m_enterTimer = 0;
    return true;
}

void BonkRun::Update(uint32 diff)
{
    switch (m_state)
    {
        case STATE_ENTERING:
        {
            if (m_player->GetMapId() == m_map.mapId && !m_player->IsBeingTeleported())
            {
                StartStage();
                return;
            }

            m_enterTimer += diff;
            if (m_enterTimer >= BONK_ENTER_TIMEOUT_MS)
            {
                m_player->GetBonkPlayer()->Notify("Could not enter " + m_map.name + ".");
                Exit();
            }
            break;
        }
        case STATE_RUNNING:
            UpdateRunning(diff);
            break;
        case STATE_PORTAL:
        {
            if (!m_player->IsAlive())
            {
                Finish(false);
                return;
            }

            UpdateOffers();
            UpdateBoundary(diff);
            UpdatePowerups(diff, false);

            m_stateTimer += diff;
            if (m_stateTimer >= BONK_STATE_INTERVAL_MS)
            {
                m_stateTimer = 0;
                SendState();

                // Never leave the player without a way out.
                if (!m_player->GetMap()->GetGameObject(m_portalGuid) && !m_player->IsBeingTeleported())
                {
                    float const angle = m_player->GetOrientation();
                    PlacePortal(m_player->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
                        m_player->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), m_player->GetPositionZ());
                }
            }
            break;
        }
        case STATE_ENDING:
            m_endTimer -= int32(diff);
            if (m_endTimer <= 0)
                Exit();
            break;
        case STATE_FINISHED:
            break;
    }
}

void BonkRun::StartStage()
{
    m_state = STATE_RUNNING;
    m_elapsedMs = 0;
    m_nextEvent = 0;
    m_spawnTimer = BONK_FIRST_WAVE_MS;
    m_eliteTimer = m_map.eliteIntervalSeconds * IN_MILLISECONDS;
    m_bossSpawned = false;
    m_bossGuid.Clear();
    m_powerupSpawnTimer = m_map.powerupIntervalSeconds * IN_MILLISECONDS / 2;
    m_lastLevel = m_player->GetLevel();
    m_safeX = m_player->GetPositionX();
    m_safeY = m_player->GetPositionY();
    m_safeZ = m_player->GetPositionZ();

    // The summon limit is stored per map, so set it once we are in the arena.
    m_player->SetCreatureSummonLimit(BONK_SUMMON_LIMIT);

    if (m_weapons.empty())
    {
        uint32 const classMask = m_player->GetClassMask();
        for (auto const& itr : sBonkMgr.GetWeapons())
            if (itr.second.starterClassMask & classMask)
                AddWeapon(itr.second);

        // Every class needs something that fires on its own.
        if (m_weapons.empty())
            for (auto const& itr : sBonkMgr.GetWeapons())
                if (!itr.second.classMask || (itr.second.classMask & classMask))
                {
                    AddWeapon(itr.second);
                    break;
                }
    }
    else
    {
        // A fresh stage starts at full health.
        m_player->SetHealth(m_player->GetMaxHealth());
        m_player->SetPower(POWER_MANA, m_player->GetMaxPower(POWER_MANA));
    }

    m_player->GetBonkPlayer()->RecordStage(m_tier.id, m_stage);

    SendFullState();
    m_player->GetBonkPlayer()->Notify("Stage " + std::to_string(m_stage) + ": " + m_map.name + " - the boss comes in " + FormatTime(m_map.durationSeconds) + "!");
}

void BonkRun::SendFullState() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("start~" + m_map.name + "~" + m_tier.name + "~" + std::to_string(m_map.durationSeconds) + "~" + std::to_string(m_stage));
    SendLoadout();
    SendState();
    if (!m_offer.empty())
        SendOffer();
}

void BonkRun::UpdateRunning(uint32 diff)
{
    if (!m_player->IsAlive())
    {
        Finish(false);
        return;
    }

    UpdateOffers();
    if (m_paused)
        return;

    m_elapsedMs += diff;
    m_totalMs += diff;

    UpdateBoundary(diff);
    UpdateRecovery(diff);
    UpdatePowerups(diff, true);
    UpdateEvents();
    if (m_state != STATE_RUNNING)
        return;

    UpdateSpawns(diff);
    if (m_state != STATE_RUNNING)
        return;

    UpdateWeapons(diff);
    if (m_state != STATE_RUNNING)
        return;

    m_stateTimer += diff;
    if (m_stateTimer >= BONK_STATE_INTERVAL_MS)
    {
        m_stateTimer = 0;
        SendState();
    }
}

void BonkRun::UpdateOffers()
{
    uint32 const level = m_player->GetLevel();
    if (level > m_lastLevel)
    {
        m_pendingLevels += level - m_lastLevel;
        m_lastLevel = level;
    }

    if (!m_offer.empty())
        return;

    // Chests first: the player just walked up to one.
    if (m_pendingChests)
        OfferUpgrades(OFFER_CHEST);
    else if (m_pendingLevels)
        OfferUpgrades(OFFER_LEVEL);
}

void BonkRun::UpdateBoundary(uint32 diff)
{
    m_boundaryTimer += diff;
    if (m_boundaryTimer < BONK_BOUNDARY_INTERVAL_MS)
        return;
    m_boundaryTimer = 0;

    if (!m_map.zoneId || m_player->IsBeingTeleported())
        return;

    if (m_player->GetZoneId() == m_map.zoneId)
    {
        m_safeX = m_player->GetPositionX();
        m_safeY = m_player->GetPositionY();
        m_safeZ = m_player->GetPositionZ();
        return;
    }

    m_player->NearTeleportTo(m_safeX, m_safeY, m_safeZ, m_player->GetOrientation());
    m_player->GetBonkPlayer()->Notify("You can't leave " + m_map.name + ".");
}

void BonkRun::UpdateRecovery(uint32 diff)
{
    m_recoveryTimer += diff;
    if (m_recoveryTimer < IN_MILLISECONDS)
        return;
    m_recoveryTimer -= IN_MILLISECONDS;

    float const percent = GetPassiveTotal(BONK_PASSIVE_RECOVERY);
    if (percent <= 0.0f || m_player->GetHealth() >= m_player->GetMaxHealth())
        return;

    m_player->ModifyHealth(std::max(1, int32(m_player->GetMaxHealth() * percent / 100.0f)));
}

void BonkRun::UpdateEvents()
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;
    while (m_nextEvent < m_map.events.size() && m_map.events[m_nextEvent].second <= elapsedSeconds)
    {
        RunEvent(m_map.events[m_nextEvent++]);
        if (m_state != STATE_RUNNING)
            return;
    }
}

void BonkRun::RunEvent(BonkMapEvent const& event)
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();

    switch (event.type)
    {
        case BONK_EVENT_MINIBOSS:
        {
            std::string name;
            for (uint32 i = 0; i < event.count; ++i)
            {
                BonkMapCreature const* creature = PickCreature(BONK_SPAWN_MINIBOSS);
                if (!creature)
                    creature = PickCreature(BONK_SPAWN_ELITE);
                if (creature)
                    if (Creature* mob = SpawnMob(*creature))
                        name = mob->GetName();
            }
            if (!name.empty())
                bonk->Notify(name + " has arrived!");
            break;
        }
        case BONK_EVENT_ELITES:
        {
            bool spawned = false;
            for (uint32 i = 0; i < event.count; ++i)
                if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_ELITE))
                    spawned |= SpawnMob(*creature) != nullptr;
            if (spawned)
                bonk->Notify("Elites are closing in!");
            break;
        }
        case BONK_EVENT_SWARM:
        {
            PruneMobs();
            uint32 const cap = uint32(m_map.maxAlive * BONK_SWARM_OVERFLOW);
            uint32 const alive = m_mobs.size();
            uint32 const count = std::min(event.count, cap > alive ? cap - alive : 0u);
            for (uint32 i = 0; i < count; ++i)
                if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_NORMAL))
                    SpawnMob(*creature);
            if (count)
                bonk->Notify("A swarm is coming!");
            break;
        }
        case BONK_EVENT_CHESTS:
        {
            uint32 placed = 0;
            for (uint32 i = 0; i < event.count; ++i)
                placed += SpawnChestNearby() ? 1 : 0;
            if (placed)
                bonk->Notify(placed == 1 ? "A chest appeared nearby." : "Chests appeared nearby.");
            break;
        }
        default:
            break;
    }
}

void BonkRun::PruneMobs()
{
    Map* map = m_player->GetMap();
    m_mobs.erase(std::remove_if(m_mobs.begin(), m_mobs.end(), [map](ObjectGuid const& guid)
    {
        Creature* mob = map->GetCreature(guid);
        return !mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING);
    }), m_mobs.end());
}

void BonkRun::UpdateSpawns(uint32 diff)
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;

    // A boss that left without dying (despawned, removed by a GM) comes back.
    if (m_bossSpawned)
    {
        Creature* boss = m_player->GetMap()->GetCreature(m_bossGuid);
        if (!boss || (boss->IsAlive() && boss->HasCreatureState(CSTATE_DESPAWNING)))
            m_bossSpawned = false;
    }

    if (!m_bossSpawned && elapsedSeconds >= m_map.durationSeconds)
    {
        BonkMapCreature const* boss = PickCreature(BONK_SPAWN_BOSS);
        if (!boss)
        {
            OpenPortal(nullptr);
            return;
        }

        // Without room around the player, try again next update.
        if (Creature* mob = SpawnMob(*boss))
        {
            m_bossSpawned = true;
            m_player->GetBonkPlayer()->Notify(std::string(mob->GetName()) + " has arrived!");
        }
    }

    m_spawnTimer -= int32(diff);
    m_eliteTimer -= int32(diff);
    if (m_spawnTimer > 0 && m_eliteTimer > 0)
        return;

    PruneMobs();
    uint32 const alive = m_mobs.size();

    if (m_eliteTimer <= 0)
    {
        m_eliteTimer += m_map.eliteIntervalSeconds * IN_MILLISECONDS;
        if (m_map.eliteIntervalSeconds && alive < m_map.maxAlive)
            if (BonkMapCreature const* elite = PickCreature(BONK_SPAWN_ELITE))
                SpawnMob(*elite);
    }

    if (m_spawnTimer <= 0)
    {
        m_spawnTimer += m_map.spawnIntervalMs;

        float const stageBonus = 1.0f + m_tier.stageSpawnGrowth * (m_stage - 1);
        float const batch = (m_map.spawnBatch + m_map.spawnBatchGrowthPerMinute * (m_elapsedMs / float(MINUTE * IN_MILLISECONDS))) * stageBonus;
        uint32 count = uint32(batch);
        if (frand(0.0f, 1.0f) < batch - float(count))
            ++count;

        count = std::min(count, m_map.maxAlive > alive ? m_map.maxAlive - alive : 0u);
        for (uint32 i = 0; i < count; ++i)
            if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_NORMAL))
                SpawnMob(*creature);
    }
}

BonkMapCreature const* BonkRun::PickCreature(BonkSpawnKind kind) const
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;

    uint32 totalWeight = 0;
    for (auto const& creature : m_map.creatures)
        if (creature.kind == kind && elapsedSeconds >= creature.minSecond && (!creature.maxSecond || elapsedSeconds <= creature.maxSecond))
            totalWeight += creature.weight;

    if (!totalWeight)
        return nullptr;

    uint32 roll = urand(1, totalWeight);
    for (auto const& creature : m_map.creatures)
    {
        if (creature.kind != kind || elapsedSeconds < creature.minSecond || (creature.maxSecond && elapsedSeconds > creature.maxSecond))
            continue;
        if (roll <= creature.weight)
            return &creature;
        roll -= creature.weight;
    }
    return nullptr;
}

bool BonkRun::FindSpawnPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const
{
    Map const* map = m_player->GetMap();
    float const playerZ = m_player->GetPositionZ();

    for (uint32 attempt = 0; attempt < 10; ++attempt)
    {
        float const angle = frand(0.0f, 2.0f * M_PI_F);
        float const distance = frand(minDistance, maxDistance);
        x = m_player->GetPositionX() + distance * std::cos(angle);
        y = m_player->GetPositionY() + distance * std::sin(angle);
        z = map->GetHeight(x, y, playerZ + BONK_SPAWN_MAX_HEIGHT_DIFF, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);

        if (z <= INVALID_HEIGHT || std::fabs(z - playerZ) > BONK_SPAWN_MAX_HEIGHT_DIFF)
            continue;

        if (m_map.zoneId && map->GetTerrain()->GetZoneId(x, y, z) != m_map.zoneId)
            continue;

        return true;
    }
    return false;
}

uint32 BonkRun::GetMobLevel() const
{
    int32 const level = int32(m_player->GetLevel()) + m_map.levelOffset;
    return uint32(std::max(1, std::min(level, 63)));
}

float BonkRun::GetMobHealth(uint32 level) const
{
    if (CreatureClassLevelStats const* stats = sObjectMgr.GetCreatureClassLevelStats(CLASS_WARRIOR, level))
        return float(stats->health);
    return 40.0f * level;
}

float BonkRun::GetStageHealthMultiplier() const
{
    return m_tier.healthMultiplier * (1.0f + m_tier.stageHealthGrowth * (m_stage - 1));
}

float BonkRun::GetStageDamageMultiplier() const
{
    return m_tier.damageMultiplier * (1.0f + m_tier.stageDamageGrowth * (m_stage - 1));
}

Creature* BonkRun::SpawnMob(BonkMapCreature const& creature)
{
    float x, y, z;
    if (!FindSpawnPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
        return nullptr;

    float const facing = std::atan2(m_player->GetPositionY() - y, m_player->GetPositionX() - x);
    Creature* mob = m_player->SummonCreature(creature.entry, x, y, z, facing, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2000, false, 0, &BonkMobAI::Install);
    if (!mob)
        return nullptr;

    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->SetKind(creature.kind);

    uint32 const level = std::min(GetMobLevel() + GetKindLevelBonus(creature.kind), 63u);
    mob->SetLevel(level);
    mob->InitStatsForLevel();

    float const minutes = m_elapsedMs / float(MINUTE * IN_MILLISECONDS);
    uint32 const health = std::max(1u, uint32(GetMobHealth(level) * GetStageHealthMultiplier() * m_map.healthMultiplier *
        creature.healthMultiplier * GetKindHealth(creature.kind) * (1.0f + m_map.healthGrowthPerMinute * minutes)));
    mob->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
    mob->SetMaxHealth(health);
    mob->SetHealth(health);

    if (CreatureClassLevelStats const* stats = sObjectMgr.GetCreatureClassLevelStats(CLASS_WARRIOR, level))
    {
        float const damage = stats->melee_damage * GetStageDamageMultiplier() * m_map.damageMultiplier * creature.damageMultiplier *
            GetKindDamage(creature.kind);
        mob->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, damage * 0.85f);
        mob->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, damage * 1.15f);
        mob->UpdateDamagePhysical(BASE_ATTACK);
    }

    mob->SetFactionTemplateId(BONK_ENEMY_FACTION);
    mob->SetSpeedRate(MOVE_WALK, creature.speed);
    mob->SetSpeedRate(MOVE_RUN, creature.speed);
    if (creature.scale > 0.0f && creature.scale != 1.0f)
        mob->SetObjectScale(mob->GetObjectScale() * creature.scale);
    mob->SetNoXP();

    m_mobs.push_back(mob->GetObjectGuid());
    if (creature.kind == BONK_SPAWN_BOSS)
        m_bossGuid = mob->GetObjectGuid();
    return mob;
}

std::vector<Creature*> BonkRun::GetMobsInRange(float range) const
{
    Map* map = m_player->GetMap();
    std::vector<std::pair<float, Creature*>> found;
    for (ObjectGuid const& guid : m_mobs)
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING))
            continue;

        float const distance = m_player->GetDistance(mob);
        if (distance <= range)
            found.emplace_back(distance, mob);
    }

    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

    std::vector<Creature*> mobs;
    mobs.reserve(found.size());
    for (auto const& itr : found)
        mobs.push_back(itr.second);
    return mobs;
}

void BonkRun::DespawnMobs()
{
    Map* map = m_player->FindMap();
    if (map)
        for (ObjectGuid const& guid : m_mobs)
            if (Creature* mob = map->GetCreature(guid))
                mob->DespawnOrUnsummon();
    m_mobs.clear();
}

GameObject* BonkRun::PlaceObject(uint32 entry, float x, float y, float z, uint32 lifetimeSeconds)
{
    return m_player->SummonGameObject(entry, x, y, z, frand(0.0f, 2.0f * M_PI_F), 0.0f, 0.0f, 0.0f, 0.0f, lifetimeSeconds, false);
}

bool BonkRun::SpawnChest(bool free, float x, float y, float z)
{
    GameObject* chest = PlaceObject(free ? BONK_GO_CHEST_FREE : BONK_GO_CHEST, x, y, z, BONK_OBJECT_LIFETIME_SEC);
    if (!chest)
        return false;
    m_chests.push_back(chest->GetObjectGuid());
    return true;
}

bool BonkRun::SpawnChestNearby()
{
    // Somewhere the player can see, so it is not buried in a wall.
    for (uint32 attempt = 0; attempt < 5; ++attempt)
    {
        float x, y, z;
        if (!FindSpawnPosition(x, y, z, BONK_CHEST_MIN_DISTANCE, BONK_CHEST_MAX_DISTANCE))
            return false;
        if (!m_player->IsWithinLOS(x, y, z))
            continue;
        return SpawnChest(false, x, y, z);
    }
    return false;
}

uint32 BonkRun::GetChestCost() const
{
    return uint32(m_tier.chestCost * (1.0f + m_tier.chestCostGrowth * m_chestsBought) + 0.5f);
}

void BonkRun::SpawnPowerup()
{
    std::vector<BonkPowerup> const& powerups = sBonkMgr.GetPowerups();
    uint32 totalWeight = 0;
    for (BonkPowerup const& powerup : powerups)
        totalWeight += powerup.weight;
    if (!totalWeight)
        return;

    uint32 roll = urand(1, totalWeight);
    BonkPowerup const* chosen = nullptr;
    for (BonkPowerup const& powerup : powerups)
    {
        if (roll <= powerup.weight)
        {
            chosen = &powerup;
            break;
        }
        roll -= powerup.weight;
    }
    if (!chosen)
        return;

    for (uint32 attempt = 0; attempt < 5; ++attempt)
    {
        float x, y, z;
        if (!FindSpawnPosition(x, y, z, BONK_POWERUP_MIN_DISTANCE, BONK_POWERUP_MAX_DISTANCE))
            return;
        if (!m_player->IsWithinLOS(x, y, z))
            continue;
        if (GameObject* go = PlaceObject(chosen->entry, x, y, z, BONK_POWERUP_LIFETIME_SEC))
            m_powerups.push_back(go->GetObjectGuid());
        return;
    }
}

void BonkRun::UpdatePowerups(uint32 diff, bool spawn)
{
    m_powerupCheckTimer += diff;
    if (m_powerupCheckTimer < BONK_POWERUP_CHECK_MS)
        return;
    uint32 const elapsed = m_powerupCheckTimer;
    m_powerupCheckTimer = 0;

    // Walking over a power-up takes it; unclaimed ones expire on their own.
    Map* map = m_player->GetMap();
    for (auto itr = m_powerups.begin(); itr != m_powerups.end();)
    {
        GameObject* go = map->GetGameObject(*itr);
        if (!go || !go->isSpawned())
        {
            itr = m_powerups.erase(itr);
            continue;
        }

        if (m_player->GetDistance(go) > BONK_POWERUP_PICKUP_RANGE)
        {
            ++itr;
            continue;
        }

        auto powerup = std::find_if(sBonkMgr.GetPowerups().begin(), sBonkMgr.GetPowerups().end(),
            [go](BonkPowerup const& p) { return p.entry == go->GetEntry(); });
        if (powerup != sBonkMgr.GetPowerups().end())
        {
            m_player->CastSpell(m_player, powerup->spellId, true);
            m_player->GetBonkPlayer()->Notify(std::string(go->GetGOInfo()->name) + "!");
        }
        go->Delete();
        itr = m_powerups.erase(itr);
    }

    if (!spawn || !m_map.powerupIntervalSeconds)
        return;

    m_powerupSpawnTimer -= int32(elapsed);
    if (m_powerupSpawnTimer > 0)
        return;
    m_powerupSpawnTimer += m_map.powerupIntervalSeconds * IN_MILLISECONDS;

    if (m_powerups.size() < m_map.powerupMax)
        SpawnPowerup();
}

void BonkRun::DespawnObjects()
{
    if (Map* map = m_player->FindMap())
    {
        for (ObjectGuid const& guid : m_chests)
            if (GameObject* chest = map->GetGameObject(guid))
                chest->Delete();
        for (ObjectGuid const& guid : m_powerups)
            if (GameObject* powerup = map->GetGameObject(guid))
                powerup->Delete();
        if (GameObject* portal = map->GetGameObject(m_portalGuid))
            portal->Delete();
    }
    m_chests.clear();
    m_powerups.clear();
    m_portalGuid.Clear();
}

void BonkRun::UseChest(GameObject* chest)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;

    auto itr = std::find(m_chests.begin(), m_chests.end(), chest->GetObjectGuid());
    if (itr == m_chests.end())
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (chest->GetEntry() != BONK_GO_CHEST_FREE)
    {
        uint32 const cost = GetChestCost();
        if (m_coins < cost)
        {
            bonk->Notify("This chest costs " + std::to_string(cost) + " coins. You have " + std::to_string(m_coins) + ".");
            return;
        }
        m_coins -= cost;
        ++m_chestsBought;
    }

    m_chests.erase(itr);
    chest->Delete();

    ++m_pendingChests;
    UpdateOffers();
    SendState();
}

bool BonkRun::IsOwnPortal(GameObject const* portal) const
{
    return m_state == STATE_PORTAL && portal->GetObjectGuid() == m_portalGuid;
}

BonkMap const* BonkRun::GetNextMap() const
{
    return sBonkMgr.GetMap(m_nextMapId);
}

void BonkRun::OpenPortal(Creature* boss)
{
    // Enemies despawn themselves once the run is no longer active.
    m_state = STATE_PORTAL;
    m_bossGuid.Clear();
    GiveGold(m_map.stageGold * m_stage);

    BonkMap const* next = ChooseNextMap();
    m_nextMapId = next ? next->id : 0;

    // Just past where the boss died (its chest lands on the spot), or in
    // front of the player.
    if (boss)
    {
        float const angle = m_player->GetAngle(boss);
        PlacePortal(boss->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
            boss->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), boss->GetPositionZ());
    }
    else
    {
        float const angle = m_player->GetOrientation();
        PlacePortal(m_player->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
            m_player->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), m_player->GetPositionZ());
    }

    m_player->GetBonkPlayer()->Notify("Stage " + std::to_string(m_stage) + " cleared! Take the portal onward, or home.");
    SendState();
}

void BonkRun::PlacePortal(float x, float y, float baseZ)
{
    float z = m_player->GetMap()->GetHeight(x, y, baseZ + 5.0f, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);
    if (z <= INVALID_HEIGHT || !m_player->IsWithinLOS(x, y, z))
    {
        x = m_player->GetPositionX();
        y = m_player->GetPositionY();
        z = m_player->GetPositionZ();
    }

    if (GameObject* portal = PlaceObject(BONK_GO_PORTAL, x, y, z, BONK_OBJECT_LIFETIME_SEC))
        m_portalGuid = portal->GetObjectGuid();
}

void BonkRun::EnterNextStage()
{
    if (m_state != STATE_PORTAL)
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (!m_offer.empty())
    {
        bonk->Notify("Choose your upgrade first.");
        return;
    }

    BonkMap const* next = GetNextMap();
    if (!next)
        next = ChooseNextMap();
    if (!next)
    {
        bonk->Notify("The portal leads nowhere.");
        return;
    }

    DespawnObjects();
    DespawnMobs();

    std::string error;
    if (!TeleportToStage(*next, error))
    {
        bonk->Notify(error);
        Finish(true);
        return;
    }

    ++m_stage;
    m_map = *next;
    m_nextMapId = 0;
}

void BonkRun::GoHome()
{
    if (m_state == STATE_PORTAL)
        Finish(true);
}

float BonkRun::GetPassiveTotal(BonkPassiveEffect effect) const
{
    float total = 0.0f;
    for (auto const& itr : m_passiveRanks)
        if (BonkPassive const* passive = sBonkMgr.GetPassive(itr.first))
            if (passive->effect == effect)
                total += passive->valuePerRank * itr.second;
    return total;
}

float BonkRun::GetWeaponDamage(OwnedWeapon const& owned) const
{
    float const levelBonus = 1.0f + 0.25f * (owned.level - 1);
    float const might = 1.0f + GetPassiveTotal(BONK_PASSIVE_MIGHT) / 100.0f;
    return GetMobHealth(GetMobLevel()) * owned.weapon.damageFraction * levelBonus * might;
}

uint32 BonkRun::GetWeaponCooldown(OwnedWeapon const& owned) const
{
    float const levelBonus = 1.0f - 0.05f * (owned.level - 1);
    float const passive = std::max(0.3f, 1.0f - GetPassiveTotal(BONK_PASSIVE_COOLDOWN) / 100.0f);
    return std::max(200u, uint32(owned.weapon.cooldownMs * levelBonus * passive));
}

void BonkRun::UpdateWeapons(uint32 diff)
{
    for (auto& owned : m_weapons)
    {
        owned.timer -= int32(diff);
        if (owned.timer <= 0)
            FireWeapon(owned);

        if (m_state != STATE_RUNNING)
            return;
    }
}

void BonkRun::FireWeapon(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
    if (!spellInfo)
        return;

    std::vector<Creature*> mobs = GetMobsInRange(weapon.range);
    if (mobs.empty())
    {
        owned.timer = 250;      // look again soon
        return;
    }

    optional<int32> basePoints[MAX_EFFECT_INDEX];
    if (weapon.damageEffectIndex >= 0)
        basePoints[weapon.damageEffectIndex] = std::max(1, int32(GetWeaponDamage(owned)));

    if (weapon.target == BONK_TARGET_SELF_AREA)
        m_player->CastCustomSpell(m_player, spellInfo, basePoints[0], basePoints[1], basePoints[2], true);
    else
    {
        if (weapon.target == BONK_TARGET_RANDOM)
            for (size_t i = mobs.size(); i > 1; --i)
                std::swap(mobs[i - 1], mobs[urand(0, uint32(i - 1))]);

        // Triggered casts skip the line of sight check, so do it here.
        uint32 const projectiles = 1 + (owned.level - 1) / 3 + uint32(GetPassiveTotal(BONK_PASSIVE_AMOUNT));
        uint32 fired = 0;
        for (Creature* mob : mobs)
        {
            if (fired >= projectiles)
                break;
            if (!m_player->IsWithinLOSInMap(mob))
                continue;
            m_player->CastCustomSpell(mob, spellInfo, basePoints[0], basePoints[1], basePoints[2], true);
            ++fired;
        }

        if (!fired)
        {
            owned.timer = 250;
            return;
        }
    }

    owned.timer = GetWeaponCooldown(owned);
}

void BonkRun::AddWeapon(BonkWeapon const& weapon)
{
    OwnedWeapon owned;
    owned.weapon = weapon;
    owned.level = 1;
    owned.timer = 500;
    m_weapons.push_back(owned);
}

void BonkRun::ApplyPassives()
{
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    if (int32 const speed = int32(GetPassiveTotal(BONK_PASSIVE_MOVE_SPEED)))
        m_player->CastCustomSpell(m_player, BONK_SPELL_MOVE_SPEED, speed, {}, {}, true);

    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    if (int32 const health = int32(GetPassiveTotal(BONK_PASSIVE_MAX_HEALTH)))
        m_player->CastCustomSpell(m_player, BONK_SPELL_MAX_HEALTH, health, {}, {}, true);
}

void BonkRun::GiveExperience(uint32 xp)
{
    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    uint32 level = m_player->GetLevel();
    if (!xp || level >= maxLevel)
        return;

    // Same as Player::GiveXP without the chat line for every kill, and
    // without the full heal a level up normally gives.
    uint32 const health = m_player->GetHealth();
    uint32 const startLevel = level;
    uint32 current = m_player->GetUInt32Value(PLAYER_XP) + xp;
    uint32 next = m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    while (current >= next && level < maxLevel)
    {
        current -= next;
        m_player->GiveLevel(level + 1);
        level = m_player->GetLevel();
        next = m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    }
    m_player->SetUInt32Value(PLAYER_XP, level >= maxLevel ? 0 : current);
    if (level != startLevel && m_player->IsAlive())
        m_player->SetHealth(std::min(health, m_player->GetMaxHealth()));
}

void BonkRun::GiveGold(uint32 copper)
{
    copper = uint32(copper * (1.0f + GetPassiveTotal(BONK_PASSIVE_GREED) / 100.0f));
    if (!copper)
        return;
    m_player->ModifyMoney(int32(copper));
    m_goldEarned += copper;
}

void BonkRun::OnMobKilled(Creature* mob, BonkSpawnKind kind)
{
    if (m_state != STATE_RUNNING)
        return;

    ++m_kills;
    uint32 const reward = GetKindReward(kind);
    GiveGold(m_map.goldPerKill * reward);
    m_coins += m_map.coinsPerKill * reward;

    if (kind != BONK_SPAWN_BOSS)
    {
        // Early levels take a handful of kills, later ones a few dozen.
        uint32 const level = m_player->GetLevel();
        uint32 const killsForLevel = 4 + 2 * level;
        float const growth = 1.0f + GetPassiveTotal(BONK_PASSIVE_GROWTH) / 100.0f;
        uint32 const xp = uint32(std::ceil(m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP) / float(killsForLevel) * reward * growth));
        GiveExperience(xp);
    }

    // Anything tougher than a normal enemy drops a free chest.
    if (kind != BONK_SPAWN_NORMAL)
        SpawnChest(true, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ());

    if (mob->GetObjectGuid() == m_bossGuid)
        OpenPortal(mob);
}

void BonkRun::OnMobStrayed(Creature* mob, BonkSpawnKind kind)
{
    // Bosses are never recycled: bring them back next to the player instead.
    if ((kind != BONK_SPAWN_BOSS && kind != BONK_SPAWN_MINIBOSS) || m_state != STATE_RUNNING)
        return;

    float x, y, z;
    if (FindSpawnPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
        mob->NearTeleportTo(x, y, z, mob->GetOrientation());
}

bool BonkRun::OfferUpgrades(OfferSource source)
{
    struct Candidate
    {
        Offer offer;
        uint32 weight;
    };
    std::vector<Candidate> candidates;

    uint32 const classMask = m_player->GetClassMask();
    for (auto const& owned : m_weapons)
        if (owned.level < owned.weapon.maxLevel)
            candidates.push_back({ { true, owned.weapon.id, uint8(owned.level + 1) }, 3 });

    if (m_weapons.size() < BONK_MAX_WEAPONS)
        for (auto const& itr : sBonkMgr.GetWeapons())
        {
            BonkWeapon const& weapon = itr.second;
            if (weapon.classMask && !(weapon.classMask & classMask))
                continue;
            if (std::any_of(m_weapons.begin(), m_weapons.end(), [&](OwnedWeapon const& owned) { return owned.weapon.id == weapon.id; }))
                continue;
            candidates.push_back({ { true, weapon.id, 1 }, 2 });
        }

    size_t const ownedPassives = std::count_if(m_passiveRanks.begin(), m_passiveRanks.end(), [](auto const& itr) { return itr.second > 0; });
    for (auto const& itr : sBonkMgr.GetPassives())
    {
        auto rank = m_passiveRanks.find(itr.first);
        uint8 const current = rank != m_passiveRanks.end() ? rank->second : 0;
        if (current >= itr.second.maxRank || (!current && ownedPassives >= BONK_MAX_PASSIVES))
            continue;
        candidates.push_back({ { false, itr.first, uint8(current + 1) }, 2 });
    }

    m_offer.clear();
    while (m_offer.size() < BONK_OFFER_SIZE && !candidates.empty())
    {
        uint32 totalWeight = 0;
        for (auto const& candidate : candidates)
            totalWeight += candidate.weight;

        uint32 roll = urand(1, totalWeight);
        for (auto itr = candidates.begin(); itr != candidates.end(); ++itr)
        {
            if (roll > itr->weight)
            {
                roll -= itr->weight;
                continue;
            }
            m_offer.push_back(itr->offer);
            candidates.erase(itr);
            break;
        }
    }

    if (m_offer.empty())
    {
        // Everything is maxed out: a bag of gold instead.
        if (source == OFFER_CHEST)
            --m_pendingChests;
        else
            --m_pendingLevels;
        GiveGold(m_map.goldPerKill * 25);
        m_player->GetBonkPlayer()->Notify("Everything is maxed - have some gold!");
        return false;
    }

    m_offerSource = source;
    m_paused = true;
    SendOffer();
    return true;
}

std::string BonkRun::DescribeOffer(Offer const& offer, std::string* icon, uint8* maxLevel) const
{
    if (offer.isWeapon)
    {
        BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id);
        if (!weapon)
            return "?";
        if (icon)
            *icon = weapon->icon;
        if (maxLevel)
            *maxLevel = weapon->maxLevel;
        return weapon->name;
    }

    BonkPassive const* passive = sBonkMgr.GetPassive(offer.id);
    if (!passive)
        return "?";
    if (icon)
        *icon = passive->icon;
    if (maxLevel)
        *maxLevel = passive->maxRank;
    return passive->name;
}

bool BonkRun::Pick(uint32 choice, std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = "There is nothing to choose right now.";
        return false;
    }

    if (choice < 1 || choice > m_offer.size())
    {
        error = "Choose a number from 1 to " + std::to_string(m_offer.size()) + ".";
        return false;
    }

    Offer const offer = m_offer[choice - 1];
    if (offer.isWeapon)
    {
        auto owned = std::find_if(m_weapons.begin(), m_weapons.end(), [&](OwnedWeapon const& w) { return w.weapon.id == offer.id; });
        if (owned != m_weapons.end())
            owned->level = offer.newLevel;
        else if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id))
            AddWeapon(*weapon);
    }
    else
    {
        m_passiveRanks[offer.id] = offer.newLevel;
        ApplyPassives();
    }

    m_offer.clear();
    if (m_offerSource == OFFER_CHEST)
    {
        if (m_pendingChests)
            --m_pendingChests;
    }
    else if (m_pendingLevels)
        --m_pendingLevels;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("picked");
    SendLoadout();

    // The next pending choice pauses again right away.
    m_paused = false;
    UpdateOffers();
    return true;
}

void BonkRun::SendState() const
{
    m_player->GetBonkPlayer()->SendAddon("state~" + std::to_string(m_elapsedMs / IN_MILLISECONDS) + "~" + std::to_string(m_map.durationSeconds) + "~" +
        std::to_string(m_kills) + "~" + std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + (m_bossSpawned ? "1" : "0") + "~" +
        std::to_string(m_coins) + "~" + std::to_string(GetChestCost()) + "~" + std::to_string(m_stage) + "~" + (m_state == STATE_PORTAL ? "1" : "0"));
}

void BonkRun::SendLoadout() const
{
    // w:<icon>:<level>:<max>,... then p:<icon>:<rank>:<max>,...
    std::string line = "loadout~";
    for (auto const& owned : m_weapons)
        line += "w:" + owned.weapon.icon + ":" + std::to_string(owned.level) + ":" + std::to_string(owned.weapon.maxLevel) + ",";
    for (auto const& itr : m_passiveRanks)
        if (BonkPassive const* passive = sBonkMgr.GetPassive(itr.first))
            line += "p:" + passive->icon + ":" + std::to_string(itr.second) + ":" + std::to_string(passive->maxRank) + ",";
    m_player->GetBonkPlayer()->SendAddon(line);
}

void BonkRun::SendOffer() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (bonk->IsAddonReady())
    {
        bonk->SendAddon("offer~" + std::to_string(m_player->GetLevel()) + "~" + std::to_string(m_offer.size()) + "~" +
            (m_offerSource == OFFER_CHEST ? "chest" : "level"));
        for (size_t i = 0; i < m_offer.size(); ++i)
        {
            Offer const& offer = m_offer[i];
            std::string icon;
            uint8 maxLevel = 1;
            std::string const name = DescribeOffer(offer, &icon, &maxLevel);
            std::string description;
            if (offer.isWeapon)
            {
                if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id))
                    description = offer.newLevel == 1 ? weapon->description : "+25% damage, faster" + std::string(offer.newLevel % 3 == 1 ? ", +1 projectile" : "");
            }
            else if (BonkPassive const* passive = sBonkMgr.GetPassive(offer.id))
                description = passive->description;

            bonk->SendAddon("opt~" + std::to_string(i + 1) + "~" + (offer.isWeapon ? "w" : "p") + "~" + std::to_string(offer.newLevel) + "~" +
                std::to_string(maxLevel) + "~" + name + "~" + icon + "~" + description);
        }
        bonk->SendAddon("offerend");
        return;
    }

    ChatHandler handler(m_player);
    if (m_offerSource == OFFER_CHEST)
        handler.SendSysMessage("Chest! Choose an upgrade with .bonk pick <number>:");
    else
        handler.PSendSysMessage("Level %u! Choose an upgrade with .bonk pick <number>:", m_player->GetLevel());
    for (size_t i = 0; i < m_offer.size(); ++i)
    {
        std::string const name = DescribeOffer(m_offer[i], nullptr, nullptr);
        handler.PSendSysMessage("  %u) %s %s", uint32(i + 1), name.c_str(),
            m_offer[i].newLevel == 1 ? "(new)" : ("(level " + std::to_string(m_offer[i].newLevel) + ")").c_str());
    }
}

void BonkRun::Finish(bool wentHome)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;

    m_state = STATE_ENDING;
    m_wentHome = wentHome;
    m_paused = false;
    m_offer.clear();
    m_pendingLevels = 0;
    m_pendingChests = 0;
    m_endTimer = BONK_END_DELAY_MS;

    // Enemies despawn themselves once the run is no longer active.
    if (m_player->IsAlive())
        m_player->CombatStop(true);

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    uint32 const seconds = m_totalMs / IN_MILLISECONDS;
    if (bonk->IsAddonReady())
        bonk->SendAddon("end~" + std::string(wentHome ? "1" : "0") + "~" + std::to_string(seconds) + "~" + std::to_string(m_kills) + "~" +
            std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + std::to_string(m_stage));
    else
        ChatHandler(m_player).PSendSysMessage("%s Reached stage %u in %s, %u kills, level %u, earned %s.", wentHome ? "Run complete!" : "Defeated.",
            m_stage, FormatTime(seconds).c_str(), m_kills, m_player->GetLevel(), FormatGold(m_goldEarned).c_str());
}

void BonkRun::CleanUp()
{
    DespawnMobs();
    DespawnObjects();
    m_player->SetCreatureSummonLimit(sWorld.getConfig(CONFIG_UINT32_CREATURE_SUMMON_LIMIT));
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
}

void BonkRun::Exit()
{
    CleanUp();
    m_state = STATE_FINISHED;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->ResetCharacter();
    bonk->TeleportToCamp();
}

void BonkRun::Abort()
{
    if (m_state == STATE_FINISHED)
        return;

    CleanUp();
    m_state = STATE_FINISHED;
    m_player->GetBonkPlayer()->ResetCharacter();
}

void BonkRun::Surrender()
{
    if (m_state == STATE_RUNNING)
        Finish(false);
    else if (m_state == STATE_PORTAL)
        Finish(true);
    else if (m_state == STATE_ENTERING)
        Exit();
}
