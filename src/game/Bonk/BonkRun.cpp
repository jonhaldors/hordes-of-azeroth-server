/*
 * Vanilla Bonk: the run loop.
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "NullCreatureAI.h"
#include "ObjectMgr.h"
#include "PathFinder.h"
#include "Player.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
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
    // An enemy must be able to walk to the player, without a long way round.
    float const BONK_SPAWN_MAX_DETOUR      = 2.5f;     // times the straight distance
    float const BONK_SPAWN_SNAP_DISTANCE   = 3.0f;     // spot may move this far onto the path mesh

    // The arena belongs to the run: the zone's own hostile creatures near the
    // player are removed, and come back long after the run has left.
    uint32 const BONK_NATIVE_SWEEP_MS      = 500;
    float const BONK_NATIVE_SWEEP_RANGE    = 90.0f;
    uint32 const BONK_NATIVE_RESPAWN_SEC   = 30 * MINUTE;

    float const BONK_CHEST_MIN_DISTANCE    = 15.0f;
    float const BONK_CHEST_MAX_DISTANCE    = 45.0f;
    float const BONK_PORTAL_DISTANCE       = 6.0f;
    float const BONK_POWERUP_MIN_DISTANCE  = 20.0f;
    float const BONK_POWERUP_MAX_DISTANCE  = 50.0f;
    float const BONK_POWERUP_PICKUP_RANGE  = 3.0f;
    uint32 const BONK_POWERUP_LIFETIME_SEC = 90;
    uint32 const BONK_POWERUP_CHECK_MS     = 250;
    float const BONK_SHARD_PICKUP_RANGE    = 3.0f;
    float const BONK_SHARD_SCATTER         = 2.0f;     // lands this far from where the enemy died
    uint32 const BONK_SHARD_CHECK_MS       = 250;
    // A swarm may push the enemy count this far past max_alive.
    float const BONK_SWARM_OVERFLOW        = 1.5f;
    float const BONK_RING_DISTANCE         = 22.0f;    // a ring starts this close
    float const BONK_RUSH_WIDTH            = 0.6f;     // radians either side of a rush's direction

    // Experience orbs.
    size_t const BONK_MAX_ORBS             = 150;      // past this, orbs grow instead
    uint32 const BONK_ORB_CHECK_MS         = 100;
    float const BONK_ORB_PICKUP_RANGE      = 1.5f;
    float const BONK_ORB_MAGNET_RANGE      = 6.0f;     // before the Magnet passive
    float const BONK_ORB_SPEED             = 2.5f;     // run speed rate once pulled in
    uint32 const BONK_ORB_FACTION          = 35;       // friendly: enemies and area spells leave it be
    // Orbs the run lost track of (cleanup off the map) still go away eventually.
    uint32 const BONK_ORB_LIFETIME_MS      = 30 * MINUTE * IN_MILLISECONDS;

    size_t const BONK_MAX_WEAPONS  = 6;
    size_t const BONK_MAX_PASSIVES = 6;
    size_t const BONK_OFFER_SIZE   = 3;

    // Passive upgrades that need a real aura on the player.
    uint32 const BONK_SPELL_MOVE_SPEED = 17498;     // Speed: SPELL_AURA_MOD_INCREASE_SPEED
    uint32 const BONK_SPELL_MAX_HEALTH = 19255;     // Survivalist: SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT
    // A few seconds safe from melee after a revive.
    uint32 const BONK_SPELL_REVIVE_SHIELD = 1022;   // Blessing of Protection, rank 1
    float const BONK_REVIVE_CLEAR_RADIUS  = 15.0f;

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

    void InstallOrbAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
    }

    // Weapon casts made while this is alive take no ammo (Spell::TakeAmmo).
    struct FiringWeapon
    {
        explicit FiringWeapon(BonkPlayer* bonk) : m_bonk(bonk) { m_bonk->SetFiringWeapon(true); }
        ~FiringWeapon() { m_bonk->SetFiringWeapon(false); }
        BonkPlayer* m_bonk;
    };

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

BonkRun::BonkRun(Player* player, BonkTier const& tier) : m_player(player), m_tier(tier),
    m_bonuses(player->GetBonkPlayer()->GetRunBonuses())
{
    m_rerolls = m_bonuses.rerolls;
    m_banishes = m_bonuses.banishes;
    m_revives = m_bonuses.revives;
    m_coins = m_bonuses.startCoins;
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

    // GM mode makes the player untargetable, and GM accounts log in with god
    // mode (GM.CheatGod), which stops all damage at 1 health; either would
    // make the run unlosable.
    if (m_player->IsGameMaster())
        m_player->SetGameMaster(false);
    m_player->SetCheatGod(false);

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
                if (m_revives)
                    Revive();
                else
                    Finish(false);
                return;
            }

            UpdateOffers();
            UpdateBoundary(diff);
            UpdateNatives(diff);
            UpdatePowerups(diff, false);
            UpdateOrbs(diff);
            UpdateShards(diff);

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
    m_eliteTimer = GetEliteIntervalMs();
    m_bossSpawned = false;
    m_bossGuid.Clear();
    m_powerupSpawnTimer = m_map.powerupIntervalSeconds * IN_MILLISECONDS / 2;
    m_nativeTimer = BONK_NATIVE_SWEEP_MS;     // clear the arrival spot right away
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
                if (!itr.second.evolvesFrom && (!itr.second.classMask || (itr.second.classMask & classMask)))
                {
                    AddWeapon(itr.second);
                    break;
                }

        // Permanent upgrades may already need their auras.
        ApplyPassives();
    }
    else
    {
        // A fresh stage starts at full health.
        m_player->SetHealth(m_player->GetMaxHealth());
        m_player->SetPower(POWER_MANA, m_player->GetMaxPower(POWER_MANA));
    }

    // Worn gear adds weapon power; checked again every second.
    m_gearPower = GetGearPower();

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
        if (m_revives)
            Revive();
        else
            Finish(false);
        return;
    }

    UpdateOffers();
    // The zone's own creatures do not pause with the run.
    UpdateNatives(diff);
    if (m_paused)
        return;

    m_elapsedMs += diff;
    m_totalMs += diff;

    UpdateBoundary(diff);
    UpdateRecovery(diff);
    UpdatePowerups(diff, true);
    UpdateOrbs(diff);
    UpdateShards(diff);
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
        // Gear can be swapped from the bags mid-run.
        m_gearPower = GetGearPower();
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

void BonkRun::UpdateNatives(uint32 diff)
{
    m_nativeTimer += diff;
    if (m_nativeTimer < BONK_NATIVE_SWEEP_MS)
        return;
    m_nativeTimer = 0;

    if (m_player->IsBeingTeleported() || !m_player->IsInWorld())
        return;

    std::list<Creature*> creatures;
    MaNGOS::AnyUnitInObjectRangeCheck check(m_player, BONK_NATIVE_SWEEP_RANGE);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
    Cell::VisitGridObjects(m_player, searcher, BONK_NATIVE_SWEEP_RANGE);

    ObjectGuid const playerGuid = m_player->GetObjectGuid();
    for (Creature* creature : creatures)
    {
        // Run enemies and orbs are the player's summons; pets and guardians
        // belong to someone. Friendly townsfolk can stay.
        if (creature->IsTemporarySummon() && static_cast<TemporarySummon*>(creature)->GetSummonerGuid() == playerGuid)
            continue;
        if (creature->IsPet() || creature->GetCharmerOrOwnerGuid() || creature->IsFriendlyTo(m_player))
            continue;
        if (creature->HasCreatureState(CSTATE_DESPAWNING))
            continue;

        creature->DespawnOrUnsummon(0, BONK_NATIVE_RESPAWN_SEC);
    }
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
        case BONK_EVENT_RING:
        case BONK_EVENT_RUSH:
        {
            if (SpawnFormation(event))
                bonk->Notify(event.type == BONK_EVENT_RING ? "You are surrounded!" : "Something is charging in!");
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

uint32 BonkRun::SpawnFormation(BonkMapEvent const& event)
{
    PruneMobs();
    uint32 const cap = uint32(m_map.maxAlive * BONK_SWARM_OVERFLOW);
    uint32 const alive = m_mobs.size();
    uint32 const count = std::min(event.count, cap > alive ? cap - alive : 0u);

    // A ring surrounds the player evenly and close; a rush comes as a wedge
    // from one side at the usual distance.
    float const side = frand(0.0f, 2.0f * M_PI_F);
    uint32 spawned = 0;
    for (uint32 i = 0; i < count; ++i)
    {
        BonkMapCreature const* creature = PickCreature(BONK_SPAWN_NORMAL);
        if (!creature)
            break;

        float angle, distance;
        if (event.type == BONK_EVENT_RING)
        {
            angle = side + 2.0f * M_PI_F * i / count;
            distance = BONK_RING_DISTANCE;
        }
        else
        {
            angle = side + frand(-BONK_RUSH_WIDTH, BONK_RUSH_WIDTH);
            distance = frand(m_map.spawnMinDistance, m_map.spawnMaxDistance);
        }

        float x, y, z;
        if (FindSpawnPositionAt(angle, distance, x, y, z) && SpawnMobAt(*creature, x, y, z))
            ++spawned;
    }
    return spawned;
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
        m_eliteTimer += GetEliteIntervalMs();
        if (m_map.eliteIntervalSeconds && alive < m_map.maxAlive)
            if (BonkMapCreature const* elite = PickCreature(BONK_SPAWN_ELITE))
                SpawnMob(*elite);
    }

    if (m_spawnTimer <= 0)
    {
        m_spawnTimer += m_map.spawnIntervalMs;

        float const stageBonus = (1.0f + m_tier.stageSpawnGrowth * (m_stage - 1)) * (1.0f + m_bonuses.spawnRate / 100.0f);
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
    for (uint32 attempt = 0; attempt < 10; ++attempt)
        if (FindSpawnPositionAt(frand(0.0f, 2.0f * M_PI_F), frand(minDistance, maxDistance), x, y, z))
            return true;
    return false;
}

bool BonkRun::FindSpawnPositionAt(float angle, float distance, float& x, float& y, float& z) const
{
    Map const* map = m_player->GetMap();
    float const playerZ = m_player->GetPositionZ();

    x = m_player->GetPositionX() + distance * std::cos(angle);
    y = m_player->GetPositionY() + distance * std::sin(angle);
    z = map->GetHeight(x, y, playerZ + BONK_SPAWN_MAX_HEIGHT_DIFF, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);

    if (z <= INVALID_HEIGHT || std::fabs(z - playerZ) > BONK_SPAWN_MAX_HEIGHT_DIFF)
        return false;

    if (m_map.zoneId && map->GetTerrain()->GetZoneId(x, y, z) != m_map.zoneId)
        return false;

    // Ground behind a wall, up a cliff or inside rock has a height too; only
    // spots with a path to the player will do.
    PathFinder path(m_player);
    path.ExcludeSteepSlopes();
    path.calculate(x, y, z);
    if (path.getPathType() & PATHFIND_NOT_USING_PATH)
        return true;    // no path mesh for this map
    if (!(path.getPathType() & PATHFIND_NORMAL) || path.Length() > distance * BONK_SPAWN_MAX_DETOUR)
        return false;

    Vector3 const end = path.getActualEndPosition();
    if ((end.x - x) * (end.x - x) + (end.y - y) * (end.y - y) > BONK_SPAWN_SNAP_DISTANCE * BONK_SPAWN_SNAP_DISTANCE)
        return false;

    x = end.x;
    y = end.y;
    z = end.z;
    return true;
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
    return m_tier.healthMultiplier * (1.0f + m_tier.stageHealthGrowth * (m_stage - 1)) * (1.0f + m_bonuses.enemyHealth / 100.0f);
}

float BonkRun::GetStageDamageMultiplier() const
{
    return m_tier.damageMultiplier * (1.0f + m_tier.stageDamageGrowth * (m_stage - 1)) * (1.0f + m_bonuses.enemyDamage / 100.0f);
}

uint32 BonkRun::GetEliteIntervalMs() const
{
    return uint32(m_map.eliteIntervalSeconds * IN_MILLISECONDS / (1.0f + m_bonuses.eliteRate / 100.0f));
}

float BonkRun::GetGearPower() const
{
    float power = 0.0f;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            power += sBonkMgr.GetItemPower(item->GetEntry());
    return power;
}

void BonkRun::Revive()
{
    --m_revives;
    m_player->ResurrectPlayer(0.5f);
    m_player->SpawnCorpseBones();
    m_player->CastSpell(m_player, BONK_SPELL_REVIVE_SHIELD, true);

    // Room to breathe: normal enemies close by are swept away; bosses and
    // elites stay.
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : m_mobs)
        if (Creature* mob = map->GetCreature(guid))
            if (mob->IsAlive() && m_player->IsWithinDistInMap(mob, BONK_REVIVE_CLEAR_RADIUS))
                if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                    if (ai->GetKind() == BONK_SPAWN_NORMAL)
                        mob->DespawnOrUnsummon();
    m_player->GetBonkPlayer()->Notify(m_revives ? "Second wind! " + std::to_string(m_revives) + " left." : "Second wind!");
    SendState();
}

Creature* BonkRun::SpawnMob(BonkMapCreature const& creature)
{
    float x, y, z;
    if (!FindSpawnPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
        return nullptr;
    return SpawnMobAt(creature, x, y, z);
}

Creature* BonkRun::SpawnMobAt(BonkMapCreature const& creature, float x, float y, float z)
{
    float const facing = std::atan2(m_player->GetPositionY() - y, m_player->GetPositionX() - x);
    Creature* mob = m_player->SummonCreature(creature.entry, x, y, z, facing, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2000, false, 0, &BonkMobAI::Install);
    if (!mob)
        return nullptr;

    // Only a template's looks are used: no auras from it (Double Attack,
    // stealth), and no invulnerability or immunity from its static flags.
    mob->RemoveAllAuras();
    mob->SetInvincibilityHpThreshold(0);
    mob->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_NOT_SELECTABLE);

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

    // Last, so abilities and affixes build on the final stats.
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->Setup(creature.kind);

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
        for (ShardDrop const& drop : m_shardDrops)
            if (GameObject* shard = map->GetGameObject(drop.guid))
                shard->Delete();
        if (GameObject* portal = map->GetGameObject(m_portalGuid))
            portal->Delete();
    }
    m_chests.clear();
    m_powerups.clear();
    m_shardDrops.clear();
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
    CollectAllOrbs();

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

    // Shards still lying around go with the player.
    CollectAllShards();
    DespawnObjects();
    DespawnMobs();
    DespawnOrbs();

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
    float total = m_bonuses.passives[effect];
    for (auto const& itr : m_passiveRanks)
        if (BonkPassive const* passive = sBonkMgr.GetPassive(itr.first))
            if (passive->effect == effect)
                total += passive->valuePerRank * itr.second;
    return total;
}

float BonkRun::GetWeaponDamage(OwnedWeapon const& owned) const
{
    float const levelBonus = 1.0f + 0.25f * (owned.level - 1);
    float const might = 1.0f + (GetPassiveTotal(BONK_PASSIVE_MIGHT) + m_gearPower) / 100.0f;
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

    FiringWeapon firing(m_player->GetBonkPlayer());
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

void BonkRun::DropOrb(Creature const* mob, uint32 experience, BonkSpawnKind kind)
{
    if (!experience)
        return;

    // Past the cap, the experience joins the orb nearest to where it dropped.
    Map* map = m_player->GetMap();
    if (m_orbs.size() >= BONK_MAX_ORBS)
    {
        Orb* nearest = nullptr;
        float nearestDistance = 0.0f;
        for (Orb& orb : m_orbs)
            if (Creature const* creature = map->GetCreature(orb.guid))
            {
                float const distance = mob->GetDistance(creature);
                if (!nearest || distance < nearestDistance)
                {
                    nearest = &orb;
                    nearestDistance = distance;
                }
            }
        if (nearest)
        {
            nearest->experience += experience;
            return;
        }
    }

    uint32 const entry = kind == BONK_SPAWN_NORMAL ? BONK_ORB_SMALL : kind == BONK_SPAWN_ELITE ? BONK_ORB_LARGE : BONK_ORB_HUGE;
    Creature* orb = m_player->SummonCreature(entry, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), 0.0f,
        TEMPSUMMON_TIMED_DESPAWN, BONK_ORB_LIFETIME_MS, false, 0, &InstallOrbAI);
    if (!orb)
    {
        GiveExperience(experience);
        return;
    }

    orb->SetFactionTemplateId(BONK_ORB_FACTION);
    orb->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);

    Orb dropped;
    dropped.guid = orb->GetObjectGuid();
    dropped.experience = experience;
    m_orbs.push_back(dropped);
}

void BonkRun::UpdateOrbs(uint32 diff)
{
    m_orbTimer += diff;
    if (m_orbTimer < BONK_ORB_CHECK_MS || m_orbs.empty())
        return;
    m_orbTimer = 0;

    float const magnet = BONK_ORB_MAGNET_RANGE * (1.0f + GetPassiveTotal(BONK_PASSIVE_MAGNET) / 100.0f);
    Map* map = m_player->GetMap();
    uint32 collected = 0;
    for (auto itr = m_orbs.begin(); itr != m_orbs.end();)
    {
        Creature* orb = map->GetCreature(itr->guid);
        float const distance = orb ? m_player->GetDistance(orb) : 0.0f;

        // Touching an orb takes it; one that vanished is not lost either.
        if (!orb || distance <= BONK_ORB_PICKUP_RANGE)
        {
            collected += itr->experience;
            if (orb)
                orb->DespawnOrUnsummon();
            itr = m_orbs.erase(itr);
            continue;
        }

        if (!itr->pulled && distance <= magnet)
        {
            itr->pulled = true;
            orb->SetWalk(false);
            orb->SetSpeedRate(MOVE_RUN, BONK_ORB_SPEED);
            orb->GetMotionMaster()->MoveFollow(m_player, 0.0f, 0.0f);
        }
        ++itr;
    }

    if (collected)
        GiveExperience(collected);
}

void BonkRun::CollectAllOrbs()
{
    uint32 collected = 0;
    for (Orb const& orb : m_orbs)
        collected += orb.experience;
    DespawnOrbs();
    if (collected)
        GiveExperience(collected);
}

void BonkRun::DespawnOrbs()
{
    if (Map* map = m_player->FindMap())
        for (Orb const& orb : m_orbs)
            if (Creature* creature = map->GetCreature(orb.guid))
                creature->DespawnOrUnsummon();
    m_orbs.clear();
}

void BonkRun::DropShards(Creature const* mob, BonkSpawnKind kind)
{
    // Rare on purpose: talents are meant to take many runs.
    uint32 count = 0;
    switch (kind)
    {
        case BONK_SPAWN_NORMAL:   count = roll_chance_f(m_tier.shardChanceNormal) ? 1 : 0; break;
        case BONK_SPAWN_ELITE:    count = roll_chance_f(m_tier.shardChanceElite) ? 1 : 0; break;
        case BONK_SPAWN_MINIBOSS: count = m_tier.shardsMiniboss; break;
        case BONK_SPAWN_BOSS:     count = m_tier.shardsBoss; break;
        default: break;
    }
    if (!count)
        return;

    // Beside the spot, so it doesn't hide under the chest that drops there too.
    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = mob->GetPositionX() + BONK_SHARD_SCATTER * std::cos(angle);
    float y = mob->GetPositionY() + BONK_SHARD_SCATTER * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, mob->GetPositionZ() + 2.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT)
    {
        x = mob->GetPositionX();
        y = mob->GetPositionY();
        z = mob->GetPositionZ();
    }

    GameObject* go = PlaceObject(BONK_GO_SHARD, x, y, z, BONK_OBJECT_LIFETIME_SEC);
    if (!go)
    {
        GiveShards(count);
        return;
    }

    ShardDrop drop;
    drop.guid = go->GetObjectGuid();
    drop.count = count;
    m_shardDrops.push_back(drop);
    m_player->GetBonkPlayer()->Notify(count == 1 ? "A Talent Shard dropped!" : FormatBonkShards(count) + " dropped!");
}

void BonkRun::UpdateShards(uint32 diff)
{
    m_shardTimer += diff;
    if (m_shardTimer < BONK_SHARD_CHECK_MS || m_shardDrops.empty())
        return;
    m_shardTimer = 0;

    Map* map = m_player->GetMap();
    uint32 taken = 0;
    for (auto itr = m_shardDrops.begin(); itr != m_shardDrops.end();)
    {
        GameObject* go = map->GetGameObject(itr->guid);
        if (go && go->isSpawned() && m_player->GetDistance(go) > BONK_SHARD_PICKUP_RANGE)
        {
            ++itr;
            continue;
        }

        // Touched, or gone some other way: the shards are not lost.
        taken += itr->count;
        if (go)
            go->Delete();
        itr = m_shardDrops.erase(itr);
    }

    if (taken)
        GiveShards(taken);
}

void BonkRun::CollectAllShards()
{
    uint32 taken = 0;
    for (ShardDrop const& drop : m_shardDrops)
        taken += drop.count;

    if (Map* map = m_player->FindMap())
        for (ShardDrop const& drop : m_shardDrops)
            if (GameObject* go = map->GetGameObject(drop.guid))
                go->Delete();
    m_shardDrops.clear();

    if (taken)
        GiveShards(taken);
}

void BonkRun::GiveShards(uint32 count)
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->AddShards(count);
    m_shardsEarned += count;
    bonk->SendAddon("shard~" + std::to_string(count) + "~" + std::to_string(bonk->GetShards()));
    bonk->Notify("+" + FormatBonkShards(count) + "!");
    SendState();
}

void BonkRun::GiveGold(uint32 copper)
{
    copper = uint32(copper * (1.0f + (GetPassiveTotal(BONK_PASSIVE_GREED) + m_bonuses.goldBonus) / 100.0f));
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
        DropOrb(mob, xp, kind);
    }

    // Anything tougher than a normal enemy drops a free chest.
    if (kind != BONK_SPAWN_NORMAL)
        SpawnChest(true, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ());

    DropShards(mob, kind);

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

bool BonkRun::OfferUpgrades(OfferSource source, std::vector<Offer> const* avoid)
{
    struct Candidate
    {
        Offer offer;
        uint32 weight;
    };
    std::vector<Candidate> candidates;

    // A maxed weapon whose paired passive is owned can evolve. Chests always
    // offer that; level ups only favor it.
    std::vector<Offer> evolutions;
    for (auto const& owned : m_weapons)
    {
        if (owned.weapon.evolvesFrom || owned.level < owned.weapon.maxLevel)
            continue;
        BonkWeapon const* evolved = sBonkMgr.GetEvolution(owned.weapon.id);
        if (!evolved || m_banished.count({ true, evolved->id }))
            continue;
        auto rank = m_passiveRanks.find(evolved->evolvePassive);
        if (rank == m_passiveRanks.end() || !rank->second)
            continue;

        Offer offer;
        offer.id = evolved->id;
        offer.newLevel = owned.level;
        offer.isEvolution = true;
        if (source == OFFER_LEVEL)
            candidates.push_back({ offer, 6 });
        else
            evolutions.push_back(offer);
    }

    uint32 const classMask = m_player->GetClassMask();
    for (auto const& owned : m_weapons)
        if (owned.level < owned.weapon.maxLevel && !m_banished.count({ true, owned.weapon.id }))
            candidates.push_back({ { true, owned.weapon.id, uint8(owned.level + 1) }, 3 });

    if (m_weapons.size() < BONK_MAX_WEAPONS)
        for (auto const& itr : sBonkMgr.GetWeapons())
        {
            BonkWeapon const& weapon = itr.second;
            if (weapon.evolvesFrom || (weapon.classMask && !(weapon.classMask & classMask)))
                continue;
            if (m_banished.count({ true, weapon.id }))
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
        if (m_banished.count({ false, itr.first }))
            continue;
        candidates.push_back({ { false, itr.first, uint8(current + 1) }, 2 });
    }

    // Cards to avoid are only drawn once the fresh ones run out.
    std::vector<Candidate> repeats;
    if (avoid)
    {
        auto shown = [avoid](Candidate const& candidate)
        {
            return std::any_of(avoid->begin(), avoid->end(), [&](Offer const& offer)
            {
                return offer.isWeapon == candidate.offer.isWeapon && offer.id == candidate.offer.id;
            });
        };
        for (auto itr = candidates.begin(); itr != candidates.end();)
        {
            if (shown(*itr))
            {
                repeats.push_back(*itr);
                itr = candidates.erase(itr);
            }
            else
                ++itr;
        }
    }

    // A reroll can turn down an evolution: it then only comes back when
    // nothing new is left.
    m_offer.clear();
    for (Offer const& offer : evolutions)
    {
        bool const shown = avoid && std::any_of(avoid->begin(), avoid->end(), [&](Offer const& previous)
        {
            return previous.isWeapon && previous.id == offer.id;
        });
        if (shown)
            repeats.push_back({ offer, 6 });
        else if (m_offer.size() < BONK_OFFER_SIZE)
            m_offer.push_back(offer);
    }
    for (std::vector<Candidate>* pool : { &candidates, &repeats })
    {
        while (m_offer.size() < BONK_OFFER_SIZE && !pool->empty())
        {
            uint32 totalWeight = 0;
            for (auto const& candidate : *pool)
                totalWeight += candidate.weight;

            uint32 roll = urand(1, totalWeight);
            for (auto itr = pool->begin(); itr != pool->end(); ++itr)
            {
                if (roll > itr->weight)
                {
                    roll -= itr->weight;
                    continue;
                }
                m_offer.push_back(itr->offer);
                pool->erase(itr);
                break;
            }
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
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (choice < 1 || choice > m_offer.size())
    {
        error = "Choose a number from 1 to " + std::to_string(m_offer.size()) + ".";
        SendOffer();
        return false;
    }

    Offer const offer = m_offer[choice - 1];
    if (offer.isEvolution)
    {
        BonkWeapon const* evolved = sBonkMgr.GetWeapon(offer.id);
        auto owned = std::find_if(m_weapons.begin(), m_weapons.end(), [&](OwnedWeapon const& w)
        {
            return evolved && w.weapon.id == evolved->evolvesFrom;
        });
        if (owned != m_weapons.end())
        {
            std::string const from = owned->weapon.name;
            owned->weapon = *evolved;
            owned->timer = 500;
            m_player->GetBonkPlayer()->Notify(from + " evolved into " + evolved->name + "!");
        }
    }
    else if (offer.isWeapon)
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

bool BonkRun::Reroll(std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = "There is nothing to reroll right now.";
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (!m_rerolls)
    {
        error = "No rerolls left.";
        SendOffer();
        return false;
    }

    --m_rerolls;
    std::vector<Offer> const previous = m_offer;
    // Nothing left to offer turns into gold; close the choice then.
    if (!OfferUpgrades(m_offerSource, &previous))
    {
        m_paused = false;
        m_player->GetBonkPlayer()->SendAddon("picked");
    }
    return true;
}

bool BonkRun::Banish(uint32 choice, std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = "There is nothing to banish right now.";
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (!m_banishes || choice < 1 || choice > m_offer.size())
    {
        error = m_banishes ? "Choose a number from 1 to " + std::to_string(m_offer.size()) + "." : "No banishes left.";
        SendOffer();
        return false;
    }

    Offer const& offer = m_offer[choice - 1];
    m_banished.insert({ offer.isWeapon, offer.id });
    m_player->GetBonkPlayer()->Notify(DescribeOffer(offer, nullptr, nullptr) + " is banished for this run.");
    --m_banishes;

    // Nothing left to offer turns into gold; close the choice then.
    if (!OfferUpgrades(m_offerSource))
    {
        m_paused = false;
        m_player->GetBonkPlayer()->SendAddon("picked");
    }
    return true;
}

void BonkRun::SendState() const
{
    m_player->GetBonkPlayer()->SendAddon("state~" + std::to_string(m_elapsedMs / IN_MILLISECONDS) + "~" + std::to_string(m_map.durationSeconds) + "~" +
        std::to_string(m_kills) + "~" + std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + (m_bossSpawned ? "1" : "0") + "~" +
        std::to_string(m_coins) + "~" + std::to_string(GetChestCost()) + "~" + std::to_string(m_stage) + "~" + (m_state == STATE_PORTAL ? "1" : "0") + "~" +
        std::to_string(m_shardsEarned));
}

void BonkRun::SendLoadout() const
{
    // w:<icon>:<level>:<max>,... then p:<icon>:<rank>:<max>,...
    std::string line = "loadout~";
    for (auto const& owned : m_weapons)
        line += std::string(owned.weapon.evolvesFrom ? "e:" : "w:") + owned.weapon.icon + ":" + std::to_string(owned.level) + ":" + std::to_string(owned.weapon.maxLevel) + ",";
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
            (m_offerSource == OFFER_CHEST ? "chest" : "level") + "~" + std::to_string(m_rerolls) + "~" + std::to_string(m_banishes));
        for (size_t i = 0; i < m_offer.size(); ++i)
        {
            Offer const& offer = m_offer[i];
            std::string icon;
            uint8 maxLevel = 1;
            std::string const name = DescribeOffer(offer, &icon, &maxLevel);
            std::string description;
            if (offer.isEvolution)
            {
                if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id))
                {
                    BonkWeapon const* from = sBonkMgr.GetWeapon(weapon->evolvesFrom);
                    description = weapon->description + (from ? " Replaces " + from->name + "." : "");
                }
            }
            else if (offer.isWeapon)
            {
                if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id))
                    description = offer.newLevel == 1 ? weapon->description : "+25% damage, faster" + std::string(offer.newLevel % 3 == 1 ? ", +1 projectile" : "");
            }
            else if (BonkPassive const* passive = sBonkMgr.GetPassive(offer.id))
                description = passive->description;

            bonk->SendAddon("opt~" + std::to_string(i + 1) + "~" + (offer.isEvolution ? "e" : offer.isWeapon ? "w" : "p") + "~" + std::to_string(offer.newLevel) + "~" +
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
        handler.PSendSysMessage("  %u) %s %s", uint32(i + 1), name.c_str(), m_offer[i].isEvolution ? "(evolution)" :
            m_offer[i].newLevel == 1 ? "(new)" : ("(level " + std::to_string(m_offer[i].newLevel) + ")").c_str());
    }
    if (m_rerolls)
        handler.PSendSysMessage("  .bonk reroll for new choices (%u left)", m_rerolls);
    if (m_banishes)
        handler.PSendSysMessage("  .bonk banish <number> to remove one for this run (%u left)", m_banishes);
}

void BonkRun::Finish(bool wentHome)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;

    m_state = STATE_ENDING;
    m_wentHome = wentHome;
    m_paused = false;

    // Going home takes the shards still lying around; dying leaves them.
    if (wentHome)
        CollectAllShards();

    m_player->GetBonkPlayer()->RecordRun(m_stage, m_totalMs / IN_MILLISECONDS, m_kills, m_goldEarned);
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
            std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + std::to_string(m_stage) + "~" + std::to_string(m_shardsEarned));
    else
        ChatHandler(m_player).PSendSysMessage("%s Reached stage %u in %s, %u kills, level %u, earned %s and %s.", wentHome ? "Run complete!" : "Defeated.",
            m_stage, FormatTime(seconds).c_str(), m_kills, m_player->GetLevel(), FormatGold(m_goldEarned).c_str(), FormatBonkShards(m_shardsEarned).c_str());
}

void BonkRun::CleanUp()
{
    DespawnMobs();
    DespawnObjects();
    DespawnOrbs();
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

    // A run that ended before the logout was recorded by Finish already.
    if (m_state == STATE_RUNNING || m_state == STATE_PORTAL || (m_state == STATE_ENTERING && m_stage > 1))
        m_player->GetBonkPlayer()->RecordRun(m_stage, m_totalMs / IN_MILLISECONDS, m_kills, m_goldEarned);

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
