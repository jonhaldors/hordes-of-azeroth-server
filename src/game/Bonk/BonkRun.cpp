/*
 * Vanilla Bonk: the run loop.
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "BonkRunStats.h"
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
#include "Spell.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "World.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <iterator>
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
    // Random-target weapons (damage over time) leave an enemy alone this
    // long after a cast, so the cast lands and its ticks are not restarted.
    uint32 const BONK_DOT_RETARGET_MS      = 4000;
    // Leech heals at most this % of max health a second for every 1% of Leech.
    float const BONK_LEECH_CAP_PER_PERCENT = 0.4f;
    // A stuck enemy is moved to somewhere this far from the player first.
    float const BONK_STUCK_MIN_DISTANCE    = 15.0f;
    float const BONK_STUCK_MAX_DISTANCE    = 30.0f;
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
    // A pulled orb flies straight at the player this fast (yards per second),
    // turning toward the player again once they have moved this far.
    float const BONK_ORB_FLY_SPEED         = 28.0f;
    float const BONK_ORB_REAIM_DISTANCE    = 1.0f;
    uint32 const BONK_ORB_FACTION          = 35;       // friendly: enemies and area spells leave it be
    // Orbs the run lost track of (cleanup off the map) still go away eventually.
    uint32 const BONK_ORB_LIFETIME_MS      = 30 * MINUTE * IN_MILLISECONDS;

    size_t const BONK_MAX_WEAPONS  = 6;
    size_t const BONK_MAX_PASSIVES = 6;
    size_t const BONK_OFFER_SIZE   = 3;

    // Passive upgrades that need a real aura on the player.
    uint32 const BONK_SPELL_MOVE_SPEED = 17498;     // Speed: SPELL_AURA_MOD_INCREASE_SPEED
    uint32 const BONK_SPELL_MAX_HEALTH = 19255;     // Survivalist: SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT
    uint32 const BONK_SPELL_DODGE      = 13669;     // Increased Dodge: SPELL_AURA_MOD_DODGE_PERCENT
    uint32 const BONK_SPELL_HEAL_TEXT  = 439;       // Healing Potion: names the turkey leg's heal in combat text

    // Turkey legs: drop chances (%), heal (share of max health) and reach.
    float const BONK_HEAL_CHANCE_NORMAL  = 1.0f;
    float const BONK_HEAL_CHANCE_ELITE   = 35.0f;
    float const BONK_HEAL_AMOUNT         = 0.20f;
    float const BONK_HEAL_PICKUP_RANGE   = 3.0f;
    uint32 const BONK_HEAL_CHECK_MS      = 250;
    size_t const BONK_MAX_HEAL_DROPS     = 20;
    // Each level gained heals this share of max health.
    float const BONK_LEVEL_UP_HEAL       = 0.10f;
    // Armor never takes a hit below this share of itself.
    float const BONK_MIN_HIT_AFTER_ARMOR = 0.25f;
    // A few seconds safe from melee after a revive.
    uint32 const BONK_SPELL_REVIVE_SHIELD = 1022;   // Blessing of Protection, rank 1
    float const BONK_REVIVE_CLEAR_RADIUS  = 15.0f;

    // Gear Defense never takes off more than this share of a hit, %.
    float const BONK_MAX_DEFENSE = 60.0f;

    // Class Mastery: what counts as an enemy at full and at low health, the
    // most Soul Siphon heals per second (% of max health), and how soon a
    // Clearcasting spell fires again.
    float const BONK_MASTERY_OPENER_HEALTH  = 80.0f;
    float const BONK_MASTERY_EXECUTE_HEALTH = 30.0f;
    float const BONK_MASTERY_SIPHON_CAP     = 3.0f;
    int32 const BONK_CLEARCAST_DELAY_MS     = 300;

    // Times a normal enemy's health at the same moment. Tuned so that, with a
    // typical build, an elite takes a few seconds, a mini-boss about ten and
    // a stage boss 30-50 seconds of the player's single-target damage.
    float GetKindHealth(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 4.0f;
            case BONK_SPAWN_MINIBOSS: return 10.0f;
            case BONK_SPAWN_BOSS:     return 30.0f;
            default:                  return 1.0f;
        }
    }

    // Enemy health grows with the player's level as well as with time, the
    // way Megabonk scales with XP gained and time: every level is one more
    // upgrade, and a run's damage grows about one starting weapon per upgrade.
    float const BONK_HEALTH_PER_LEVEL = 0.10f;

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

    // Enemies may hurt the player but never slow, root, stun, fear or
    // confuse them: getting out of the swarm is the game.
    void SetMovementImmunity(Player* player, bool apply)
    {
        for (AuraType aura : { SPELL_AURA_MOD_DECREASE_SPEED, SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE })
        {
            player->ApplySpellImmune(0, IMMUNITY_STATE, aura, apply);
            if (apply)
                player->RemoveSpellsCausingAura(aura);
        }
    }

    void InstallOrbAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
    }

    // Weapon casts made while this is alive take no ammo (Spell::TakeAmmo).
    // A kill during a volley can spread a spell, nesting another one.
    struct FiringWeapon
    {
        explicit FiringWeapon(BonkPlayer* bonk) : m_bonk(bonk), m_wasFiring(bonk->IsFiringWeapon()) { m_bonk->SetFiringWeapon(true); }
        ~FiringWeapon() { m_bonk->SetFiringWeapon(m_wasFiring); }
        BonkPlayer* m_bonk;
        bool m_wasFiring;
    };

    // What a weapon gains at `level`, for the level-up card.
    std::string DescribeWeaponLevel(BonkWeapon const& weapon, uint8 level)
    {
        std::string text = "+25% damage, faster";
        std::string const growth = std::to_string(uint32(BONK_AREA_GROWTH * 100.0f + 0.5f)) + "%";
        bool const cleave = weapon.behavior == BONK_BEHAVIOR_CLEAVE;

        if (weapon.target == BONK_TARGET_SELF_AREA || weapon.target == BONK_TARGET_GROUND)
            text += ", +" + growth + " area";
        else if (cleave)
            text += ", +" + growth + " reach";

        // More enemies fired at and more jumps both read as targets; a
        // cleave's extra victims are their own.
        bool const moreTargets = (weapon.target == BONK_TARGET_NEAREST || weapon.target == BONK_TARGET_RANDOM) &&
            weapon.targetEvery && level > 1 && (level - 1) % weapon.targetEvery == 0;
        bool const moreChain = weapon.chainEvery && level > 1 && (level - 1) % weapon.chainEvery == 0;
        uint32 const targets = (moreTargets ? 1 : 0) + (moreChain && !cleave ? 1 : 0);
        if (targets)
            text += ", +" + std::to_string(targets) + (targets > 1 ? " targets" : " target");
        if (moreChain && cleave)
            text += ", +1 cleaved enemy";

        if (weapon.behavior == BONK_BEHAVIOR_SPREAD && level >= weapon.maxLevel)
            text += ". Now spreads to nearby enemies when one carrying it dies";
        return text;
    }

    // "Evolves with <passive> at max level." for a weapon that can evolve.
    std::string DescribeEvolution(BonkWeapon const& weapon)
    {
        BonkWeapon const* evolved = sBonkMgr.GetEvolution(weapon.id);
        BonkPassive const* passive = evolved ? sBonkMgr.GetPassive(evolved->evolvePassive) : nullptr;
        if (!passive)
            return "";
        return " Max level + " + passive->name + ": " + evolved->name + ".";
    }

    // CastCustomSpell for a spot on the ground.
    void CastAtGround(Player* player, SpellEntry const* spellInfo, optional<int32> const* basePoints, float x, float y, float z)
    {
        Spell* spell = new Spell(player, spellInfo, true);
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (basePoints[i])
                spell->m_currentBasePoints[i] = *basePoints[i];

        SpellCastTargets targets;
        targets.setDestination(x, y, z);
        spell->prepare(std::move(targets));
    }

    // Up to `count` spots for a ground effect of `radius`, each centred on the
    // enemy with the most others around it that no earlier spot covers.
    std::vector<Creature*> PickGroundSpots(std::vector<Creature*> const& mobs, float radius, uint32 count)
    {
        std::vector<Creature*> spots;
        std::vector<bool> covered(mobs.size(), false);
        while (spots.size() < count)
        {
            size_t best = mobs.size();
            uint32 bestCount = 0;
            for (size_t i = 0; i < mobs.size(); ++i)
            {
                if (covered[i])
                    continue;
                uint32 around = 0;
                for (size_t j = 0; j < mobs.size(); ++j)
                    if (!covered[j] && mobs[i]->GetDistance2d(mobs[j]) <= radius)
                        ++around;
                if (around > bestCount)
                {
                    best = i;
                    bestCount = around;
                }
            }
            if (best == mobs.size())
                break;

            for (size_t j = 0; j < mobs.size(); ++j)
                if (mobs[best]->GetDistance2d(mobs[j]) <= radius)
                    covered[j] = true;
            spots.push_back(mobs[best]);
        }
        return spots;
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

BonkRun::BonkRun(Player* player, BonkTier const& tier) : m_player(player), m_tier(tier),
    m_bonuses(player->GetBonkPlayer()->GetRunBonuses()), m_mastery(player->GetBonkPlayer()->GetMasteryBonuses())
{
    // Mastery bonuses are by weapon; damage comes in by spell.
    for (auto const& itr : sBonkMgr.GetWeapons())
        m_spellRoots.emplace(itr.second.spellId, BonkMastery::GetRootWeapon(itr.first));

    m_rerolls = m_bonuses.rerolls;
    m_banishes = m_bonuses.banishes;
    m_revives = m_bonuses.revives;
    m_coins = m_bonuses.startCoins;
}

bool BonkRun::Begin(std::string& error)
{
    BonkStats::Reset(m_player->GetObjectGuid());

    BonkMap const* first = ChooseNextMap();
    if (!first)
    {
        error = "No maps are set up for " + m_tier.name + ".";
        return false;
    }
    m_map = *first;

    // GM mode makes the player untargetable, and GM accounts log in with god
    // mode (GM.CheatGod), which stops all damage at 1 health; either would
    // make the run unlosable. Only `.bonk god` keeps god mode on, for testing.
    if (m_player->IsGameMaster())
        m_player->SetGameMaster(false);
    m_player->SetCheatGod(m_player->GetBonkPlayer()->IsTestGod());
    SetMovementImmunity(m_player, true);

    m_player->GetBonkPlayer()->ResetCharacter();
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);

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
            UpdateHeals(diff);

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

        // Class Mastery: spells every run starts with, once a quest has
        // unlocked them.
        uint32 const account = m_player->GetSession()->GetAccountId();
        for (uint32 weaponId : m_mastery.startSpells)
        {
            BonkWeapon const* weapon = sBonkMgr.GetWeapon(weaponId);
            if (!weapon || m_weapons.size() >= BONK_MAX_WEAPONS || sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, weaponId))
                continue;
            if (std::none_of(m_weapons.begin(), m_weapons.end(), [weaponId](OwnedWeapon const& owned) { return owned.weapon.id == weaponId; }))
                AddWeapon(*weapon);
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

    // Worn gear adds Power, Defense and set bonuses; checked again every second.
    UpdateGear();

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
    UpdateHeals(diff);
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
        UpdateGear();
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

    // Leech gathered over the last second, up to its cap; the rest is lost.
    uint32 heal = uint32(m_leechCarry);
    m_leechCarry = 0.0f;

    float const percent = GetPassiveTotal(BONK_PASSIVE_RECOVERY);
    if (percent > 0.0f)
        heal += std::max(1u, uint32(m_player->GetMaxHealth() * percent / 100.0f));

    if (heal && m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth())
        m_player->ModifyHealth(int32(heal));
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
            uint32 const cap = uint32(GetAliveCap() * BONK_SWARM_OVERFLOW);
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

uint32 BonkRun::GetAliveCap() const
{
    if (!m_map.maxAliveRampSeconds || m_map.maxAliveStart >= m_map.maxAlive)
        return m_map.maxAlive;

    // Run time, not stage time: a new stage keeps the crowd the last one had.
    float const progress = std::min(1.0f, m_totalMs / float(m_map.maxAliveRampSeconds * IN_MILLISECONDS));
    return m_map.maxAliveStart + uint32((m_map.maxAlive - m_map.maxAliveStart) * progress);
}

uint32 BonkRun::SpawnFormation(BonkMapEvent const& event)
{
    PruneMobs();
    uint32 const cap = uint32(GetAliveCap() * BONK_SWARM_OVERFLOW);
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
        if (m_map.eliteIntervalSeconds && alive < GetAliveCap())
            if (BonkMapCreature const* elite = PickCreature(BONK_SPAWN_ELITE))
                SpawnMob(*elite);
    }

    if (m_spawnTimer <= 0)
    {
        m_spawnTimer += m_map.spawnIntervalMs;

        // Spawns keep growing over the whole run, so a new stage picks up
        // where the last one left off.
        float const stageBonus = (1.0f + m_tier.stageSpawnGrowth * (m_stage - 1)) * (1.0f + m_bonuses.spawnRate / 100.0f);
        float const batch = (m_map.spawnBatch + m_map.spawnBatchGrowthPerMinute * GetRunMinutes()) * stageBonus;
        uint32 count = uint32(batch);
        if (frand(0.0f, 1.0f) < batch - float(count))
            ++count;

        uint32 const cap = GetAliveCap();
        count = std::min(count, cap > alive ? cap - alive : 0u);
        for (uint32 i = 0; i < count; ++i)
            if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_NORMAL))
                SpawnMob(*creature);
    }
}

BonkMapCreature const* BonkRun::PickCreature(BonkSpawnKind kind) const
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;

    // Only the enemies whose window is open, and none past their cap: a few
    // chargers or casters in the crowd, never a whole pack of them.
    std::vector<BonkMapCreature const*> candidates;
    uint32 totalWeight = 0;
    for (auto const& creature : m_map.creatures)
    {
        if (creature.kind != kind || elapsedSeconds < creature.minSecond || (creature.maxSecond && elapsedSeconds > creature.maxSecond))
            continue;
        if (creature.maxAlive && CountAlive(creature.entry) >= creature.maxAlive)
            continue;
        candidates.push_back(&creature);
        totalWeight += creature.weight;
    }

    if (!totalWeight)
        return nullptr;

    uint32 roll = urand(1, totalWeight);
    for (BonkMapCreature const* creature : candidates)
    {
        if (roll <= creature->weight)
            return creature;
        roll -= creature->weight;
    }
    return nullptr;
}

uint32 BonkRun::CountAlive(uint32 entry) const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (guid.GetEntry() == entry)
            if (Creature const* mob = map->GetCreature(guid))
                if (mob->IsAlive() && !mob->HasCreatureState(CSTATE_DESPAWNING))
                    ++count;
    return count;
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

void BonkRun::UpdateGear()
{
    BonkGearTotals const gear = sBonkMgr.EvaluateGear(m_player);
    bool const passivesChanged = !std::equal(std::begin(gear.passives), std::end(gear.passives), std::begin(m_gear.passives));
    m_gear = gear;

    // Set bonuses can raise passives that live on auras (speed, health).
    if (passivesChanged)
        ApplyPassives();
}

uint32 BonkRun::ApplyArmor(uint32 damage) const
{
    float const armor = GetPassiveTotal(BONK_PASSIVE_ARMOR);
    if (armor <= 0.0f || !damage)
        return damage;

    float const reduced = damage - GetNormalHit() * armor / 100.0f;
    return std::max(1u, uint32(std::max(reduced, damage * BONK_MIN_HIT_AFTER_ARMOR)));
}

float BonkRun::GetNormalHit() const
{
    CreatureClassLevelStats const* stats = sObjectMgr.GetCreatureClassLevelStats(CLASS_WARRIOR, GetMobLevel());
    if (!stats)
        return 0.0f;
    return stats->melee_damage * GetStageDamageMultiplier() * m_map.damageMultiplier * (1.0f + m_map.damageGrowthPerMinute * GetRunMinutes());
}

float BonkRun::GetDefense() const
{
    return std::min(m_gear.defense + GetPassiveTotal(BONK_PASSIVE_DEFENSE), BONK_MAX_DEFENSE);
}

float BonkRun::GetDamageMultiplier(SpellEntry const* spell, Unit const* victim) const
{
    float const might = 1.0f + (GetPassiveTotal(BONK_PASSIVE_MIGHT) + m_gear.power) / 100.0f;
    if (!spell)
        return might;

    // Class Mastery: the spell itself, its school, and the state of the enemy.
    uint32 const school = spell->GetSpellSchoolMask();
    float percent = 0.0f;
    auto root = m_spellRoots.find(spell->Id);
    if (root != m_spellRoots.end())
    {
        auto bonus = m_mastery.spellDamage.find(root->second);
        if (bonus != m_mastery.spellDamage.end())
            percent += bonus->second;
    }
    for (auto const& bonus : m_mastery.schoolDamage)
        if (bonus.first & school)
            percent += bonus.second;

    if (victim)
    {
        if (m_mastery.shatter > 0.0f && (victim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) || victim->HasAuraType(SPELL_AURA_MOD_ROOT)))
            percent += m_mastery.shatter;

        float const health = victim->GetHealthPercent();
        if (health > BONK_MASTERY_OPENER_HEALTH)
            for (auto const& bonus : m_mastery.opener)
                if (!bonus.first || (bonus.first & school))
                    percent += bonus.second;
        if (health < BONK_MASTERY_EXECUTE_HEALTH)
            for (auto const& bonus : m_mastery.execute)
                if (!bonus.first || (bonus.first & school))
                    percent += bonus.second;
    }

    return might * (1.0f + percent / 100.0f);
}

uint32 BonkRun::GetRootWeapon(BonkWeapon const& weapon) const
{
    auto itr = m_spellRoots.find(weapon.spellId);
    return itr != m_spellRoots.end() ? itr->second : weapon.id;
}

void BonkRun::SoulSiphon(uint32 killingSpell)
{
    if (m_mastery.soulSiphon.empty() || !killingSpell || !m_player->IsAlive())
        return;
    SpellEntry const* spell = sSpellMgr.GetSpellEntry(killingSpell);
    if (!spell)
        return;

    float percent = 0.0f;
    for (auto const& siphon : m_mastery.soulSiphon)
        if (siphon.first & spell->GetSpellSchoolMask())
            percent += siphon.second;

    // A swarm dying at once must not fill the bar: capped per second.
    uint32 const now = WorldTimer::getMSTime();
    if (WorldTimer::getMSTimeDiff(m_siphonWindow, now) >= IN_MILLISECONDS)
    {
        m_siphonWindow = now;
        m_siphonHealed = 0.0f;
    }
    percent = std::min(percent, BONK_MASTERY_SIPHON_CAP - m_siphonHealed);
    if (percent <= 0.0f)
        return;
    m_siphonHealed += percent;
    m_player->ModifyHealth(int32(std::max(1.0f, m_player->GetMaxHealth() * percent / 100.0f)));
}

void BonkRun::AwardMastery()
{
    if (m_masteryAwarded)
        return;
    m_masteryAwarded = true;
    m_player->GetBonkPlayer()->AddMasteryXp(m_totalMs / (MINUTE * IN_MILLISECONDS) * m_tier.masteryXpMinute + m_masteryXp);
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

    // Stats are set once, at spawn, from the run's clock (not the stage's)
    // and the player's level; spells deal a share of GetMobHealth, so only
    // these factors make enemies tougher relative to the player.
    float const minutes = GetRunMinutes();
    float const levelGrowth = 1.0f + BONK_HEALTH_PER_LEVEL * (m_player->GetLevel() - 1);
    uint32 const health = std::max(1u, uint32(GetMobHealth(level) * GetStageHealthMultiplier() * m_map.healthMultiplier *
        creature.healthMultiplier * GetKindHealth(creature.kind) * levelGrowth * (1.0f + m_map.healthGrowthPerMinute * minutes)));
    mob->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
    mob->SetMaxHealth(health);
    mob->SetHealth(health);

    if (CreatureClassLevelStats const* stats = sObjectMgr.GetCreatureClassLevelStats(CLASS_WARRIOR, level))
    {
        // Gentle at the start of a run, harder the longer it goes.
        float const damage = stats->melee_damage * GetStageDamageMultiplier() * m_map.damageMultiplier * creature.damageMultiplier *
            GetKindDamage(creature.kind) * (1.0f + m_map.damageGrowthPerMinute * minutes);
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

std::vector<Creature*> BonkRun::GetMobsInRange(float range, WorldObject const* center) const
{
    if (!center)
        center = m_player;

    Map* map = m_player->GetMap();
    std::vector<std::pair<float, Creature*>> found;
    for (ObjectGuid const& guid : m_mobs)
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING))
            continue;

        if (mob == center)
            continue;
        float const distance = center->GetDistance(mob);
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
            {
                // A delayed despawn waits on the mob's own clock.
                mob->SetTimeFrozen(false);
                mob->DespawnOrUnsummon();
            }
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
        for (ObjectGuid const& guid : m_healDrops)
            if (GameObject* heal = map->GetGameObject(guid))
                heal->Delete();
        if (GameObject* portal = map->GetGameObject(m_portalGuid))
            portal->Delete();
    }
    m_chests.clear();
    m_powerups.clear();
    m_shardDrops.clear();
    m_healDrops.clear();
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
        // Stay at the portal (it comes back within a second) with another
        // map to try, rather than ending the run as if the player went home.
        bonk->Notify(error);
        BonkMap const* other = ChooseNextMap();
        m_nextMapId = other ? other->id : 0;
        return;
    }

    ++m_stage;
    m_map = *next;
    m_nextMapId = 0;
}

bool BonkRun::SkipTime(uint32 seconds, std::string& error)
{
    if (m_state != STATE_RUNNING)
    {
        error = "Only a running stage can be skipped.";
        return false;
    }
    if (m_bossSpawned)
    {
        error = "The boss is already here.";
        return false;
    }

    // 0 = straight to the boss.
    uint32 const bossMs = m_map.durationSeconds * IN_MILLISECONDS;
    uint32 const target = seconds ? std::min(m_elapsedMs + seconds * IN_MILLISECONDS, bossMs) : bossMs;
    if (target <= m_elapsedMs)
    {
        error = "The boss is already due.";
        return false;
    }

    m_totalMs += target - m_elapsedMs;
    m_elapsedMs = target;

    // Events in the skipped stretch are dropped, not all fired at once.
    while (m_nextEvent < m_map.events.size() && m_map.events[m_nextEvent].second * IN_MILLISECONDS < target)
        ++m_nextEvent;

    SendState();
    return true;
}

void BonkRun::GoHome()
{
    if (m_state == STATE_PORTAL)
        Finish(true);
}

float BonkRun::GetPassiveTotal(BonkPassiveEffect effect) const
{
    float total = m_bonuses.passives[effect] + m_gear.passives[effect];
    for (auto const& itr : m_passiveRanks)
        if (BonkPassive const* passive = sBonkMgr.GetPassive(itr.first))
            if (passive->effect == effect)
                total += passive->valuePerRank * itr.second;
    return total;
}

float BonkRun::GetWeaponDamage(OwnedWeapon const& owned) const
{
    // Might is not in here: GetDamageMultiplier applies it to every hit.
    float const levelBonus = 1.0f + BONK_DAMAGE_PER_LEVEL * (owned.level - 1);
    return GetMobHealth(GetMobLevel()) * owned.weapon.damageFraction * levelBonus;
}

uint32 BonkRun::GetWeaponCooldown(OwnedWeapon const& owned) const
{
    float const levelBonus = 1.0f - BONK_HASTE_PER_LEVEL * (owned.level - 1);
    float mastery = 0.0f;
    auto itr = m_mastery.spellCooldown.find(GetRootWeapon(owned.weapon));
    if (itr != m_mastery.spellCooldown.end())
        mastery = itr->second;
    float const passive = std::max(0.3f, 1.0f - (GetPassiveTotal(BONK_PASSIVE_COOLDOWN) + mastery) / 100.0f);
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

float BonkRun::GetWeaponArea(OwnedWeapon const& owned) const
{
    BonkWeapon const& weapon = owned.weapon;
    bool const grows = weapon.target == BONK_TARGET_SELF_AREA || weapon.target == BONK_TARGET_GROUND ||
        weapon.behavior == BONK_BEHAVIOR_CLEAVE;
    float const level = grows ? 1.0f + BONK_AREA_GROWTH * (owned.level - 1) : 1.0f;
    return level * (1.0f + GetPassiveTotal(BONK_PASSIVE_AREA) / 100.0f);
}

uint32 BonkRun::GetWeaponTargets(OwnedWeapon const& owned) const
{
    uint32 const every = owned.weapon.targetEvery;
    uint32 mastery = 0;
    auto itr = m_mastery.spellTargets.find(GetRootWeapon(owned.weapon));
    if (itr != m_mastery.spellTargets.end())
        mastery = itr->second;
    return 1 + (every ? (owned.level - 1) / every : 0) + uint32(GetPassiveTotal(BONK_PASSIVE_AMOUNT)) + mastery;
}

uint32 BonkRun::GetWeaponChainBonus(OwnedWeapon const& owned) const
{
    uint32 const every = owned.weapon.chainEvery;
    return every ? (owned.level - 1) / every : 0;
}

void BonkRun::GetBasePoints(OwnedWeapon const& owned, SpellEntry const* spellInfo, float damageShare, optional<int32>* basePoints) const
{
    BonkWeapon const& weapon = owned.weapon;
    float const damage = GetWeaponDamage(owned) * damageShare;
    if (weapon.damageEffectIndex >= 0)
    {
        // A weapon with its own tick rate deals its damage per second.
        float share = 1.0f;
        if (weapon.tickMs && spellInfo->EffectApplyAuraName[weapon.damageEffectIndex] == SPELL_AURA_PERIODIC_DAMAGE)
            share = weapon.tickMs / float(IN_MILLISECONDS);
        basePoints[weapon.damageEffectIndex] = std::max(1, int32(damage * share));
    }

    if (weapon.tickEffectIndex >= 0)
    {
        float share = 1.0f;
        if (spellInfo->Effect[weapon.tickEffectIndex] == SPELL_EFFECT_APPLY_AURA)
        {
            int32 const duration = m_player->GetBonkPlayer()->AdjustDuration(weapon.spellId, spellInfo->GetDuration());
            uint32 const amplitude = weapon.tickMs ? weapon.tickMs : spellInfo->EffectAmplitude[weapon.tickEffectIndex];
            if (duration > 0 && amplitude)
                share = std::min(1.0f, float(amplitude) / duration);
        }
        basePoints[weapon.tickEffectIndex] = std::max(1, int32(damage * share));
    }
}

void BonkRun::StrikeWith(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float damageShare)
{
    BonkWeapon const& weapon = owned.weapon;
    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, spellInfo, damageShare, basePoints);
    if (weapon.weaponDamage)
        m_player->GetBonkPlayer()->TweakSpell(weapon.spellId).weaponDamage = std::max(1, int32(GetWeaponDamage(owned) * damageShare));

    m_player->CastCustomSpell(target, spellInfo, basePoints[0], basePoints[1], basePoints[2], true);

    if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
    {
        int32 const duration = m_player->GetBonkPlayer()->AdjustDuration(weapon.spellId, spellInfo->GetDuration());
        owned.carriers[target->GetObjectGuid()] = WorldTimer::getMSTime() + uint32(std::max(0, duration));
    }
}

void BonkRun::SpreadFrom(Creature const* dead)
{
    uint32 const now = WorldTimer::getMSTime();
    FiringWeapon firing(m_player->GetBonkPlayer());
    for (auto& owned : m_weapons)
    {
        BonkWeapon const& weapon = owned.weapon;
        if (weapon.behavior != BONK_BEHAVIOR_SPREAD)
            continue;

        auto carrier = owned.carriers.find(dead->GetObjectGuid());
        if (carrier == owned.carriers.end())
            continue;
        bool const carried = int32(carrier->second - now) > 0;
        owned.carriers.erase(carrier);

        // Only at max level, or once evolved.
        if (!carried || (owned.level < weapon.maxLevel && !weapon.evolvesFrom))
            continue;
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
        if (!spellInfo)
            continue;

        uint32 const jumps = uint32(weapon.behaviorValue) + uint32(GetPassiveTotal(BONK_PASSIVE_AMOUNT));
        uint32 spread = 0;
        for (Creature* next : GetMobsInRange(BONK_SPREAD_RADIUS * GetWeaponArea(owned), dead))
        {
            if (spread >= jumps)
                break;
            auto itr = owned.carriers.find(next->GetObjectGuid());
            if (itr != owned.carriers.end() && int32(itr->second - now) > 0)
                continue;
            StrikeWith(owned, spellInfo, next, 1.0f);
            ++spread;
        }
    }
}

void BonkRun::FireWeapon(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
    if (!spellInfo)
        return;

    // What the weapon's level and the passives do to its spell (Spell reads
    // these): a bigger area, more jumps, a longer or shorter duration.
    float const areaScale = GetWeaponArea(owned);
    BonkPlayer::SpellTweak& tweak = m_player->GetBonkPlayer()->TweakSpell(weapon.spellId);
    tweak.areaScale = areaScale;
    tweak.chainBonus = weapon.behavior == BONK_BEHAVIOR_CLEAVE ? 0 : GetWeaponChainBonus(owned);
    tweak.durationMs = weapon.durationMs;
    tweak.durationScale = 1.0f + GetPassiveTotal(BONK_PASSIVE_DURATION) / 100.0f;
    tweak.weaponDamage = weapon.weaponDamage ? std::max(1, int32(GetWeaponDamage(owned))) : 0;
    tweak.tickMs = weapon.tickMs;

    // Area weapons and melee reach grow; a ground weapon's range is where it
    // can aim, not its area.
    bool const reachGrows = weapon.target == BONK_TARGET_SELF_AREA || weapon.behavior == BONK_BEHAVIOR_CLEAVE;
    std::vector<Creature*> mobs = GetMobsInRange(reachGrows ? weapon.range * areaScale : weapon.range);

    // Damage over time wants a fresh enemy each cast: casting again on one
    // that has it restarts its ticks, so with few enemies around they never
    // came.
    uint32 const now = WorldTimer::getMSTime();
    if (weapon.target == BONK_TARGET_RANDOM)
    {
        for (auto itr = owned.recentTargets.begin(); itr != owned.recentTargets.end();)
        {
            if (now - itr->second > BONK_DOT_RETARGET_MS)
                itr = owned.recentTargets.erase(itr);
            else
                ++itr;
        }
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
        {
            return mob->HasAura(weapon.spellId) || owned.recentTargets.count(mob->GetObjectGuid());
        }), mobs.end());
    }

    if (mobs.empty())
    {
        owned.timer = 250;      // look again soon
        return;
    }

    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, spellInfo, 1.0f, basePoints);

    FiringWeapon firing(m_player->GetBonkPlayer());
    if (weapon.target == BONK_TARGET_SELF_AREA)
        m_player->CastCustomSpell(m_player, spellInfo, basePoints[0], basePoints[1], basePoints[2], true);
    else if (weapon.target == BONK_TARGET_GROUND)
    {
        // Triggered casts skip the line of sight check, so do it here.
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
        {
            return !m_player->IsWithinLOSInMap(mob);
        }), mobs.end());

        uint32 const drops = 1 + uint32(GetPassiveTotal(BONK_PASSIVE_AMOUNT));
        std::vector<Creature*> const spots = PickGroundSpots(mobs, weapon.groundRadius * areaScale, drops);
        if (spots.empty())
        {
            owned.timer = 250;
            return;
        }
        for (Creature* spot : spots)
            CastAtGround(m_player, spellInfo, basePoints, spot->GetPositionX(), spot->GetPositionY(), spot->GetPositionZ());
    }
    else
    {
        if (weapon.target == BONK_TARGET_RANDOM)
            for (size_t i = mobs.size(); i > 1; --i)
                std::swap(mobs[i - 1], mobs[urand(0, uint32(i - 1))]);

        // Spreading spells forget enemies they have worn off.
        if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
            for (auto itr = owned.carriers.begin(); itr != owned.carriers.end();)
            {
                if (int32(itr->second - now) <= 0)
                    itr = owned.carriers.erase(itr);
                else
                    ++itr;
            }

        // Triggered casts skip the line of sight check, so do it here.
        uint32 const targets = GetWeaponTargets(owned);
        std::vector<Creature*> struck;
        for (Creature* mob : mobs)
        {
            if (struck.size() >= targets)
                break;
            if (!m_player->IsWithinLOSInMap(mob))
                continue;
            StrikeWith(owned, spellInfo, mob, 1.0f);
            if (weapon.target == BONK_TARGET_RANDOM)
                owned.recentTargets[mob->GetObjectGuid()] = now;
            struck.push_back(mob);
        }
        uint32 const fired = uint32(struck.size());

        // A cleave also hits the enemies next to each target, for a share
        // of the damage; every enemy is hit once.
        if (weapon.behavior == BONK_BEHAVIOR_CLEAVE)
        {
            uint32 const victims = 1 + GetWeaponChainBonus(owned);
            for (size_t i = 0; i < fired; ++i)
            {
                uint32 cleaved = 0;
                for (Creature* next : GetMobsInRange(BONK_CLEAVE_RADIUS * areaScale, struck[i]))
                {
                    if (cleaved >= victims)
                        break;
                    if (std::find(struck.begin(), struck.end(), next) != struck.end())
                        continue;
                    StrikeWith(owned, spellInfo, next, weapon.behaviorValue);
                    struck.push_back(next);
                    ++cleaved;
                }
            }
            // The next swing hits for full damage again.
            if (weapon.weaponDamage)
                tweak.weaponDamage = std::max(1, int32(GetWeaponDamage(owned)));
        }

        if (!fired)
        {
            owned.timer = 250;
            return;
        }
    }

    // A share of max health, so it keeps pace with the player's level.
    if (weapon.healPct > 0.0f && m_player->IsAlive())
        m_player->DealHeal(m_player, std::max(1u, uint32(m_player->GetMaxHealth() * weapon.healPct / 100.0f)), spellInfo);

    owned.timer = GetWeaponCooldown(owned);

    // Class Mastery: now and then a spell is ready again at once.
    if (m_mastery.clearcasting > 0.0f && roll_chance_f(m_mastery.clearcasting))
        owned.timer = std::min<int32>(owned.timer, BONK_CLEARCAST_DELAY_MS);
}

void BonkRun::OnWeaponDamage(uint32 damage)
{
    if (m_state != STATE_RUNNING || !m_player->IsAlive())
        return;

    float const percent = GetPassiveTotal(BONK_PASSIVE_LEECH);
    if (percent <= 0.0f)
        return;

    // Gathered here and handed out once a second (UpdateRecovery), never
    // more than the cap: hitting a whole swarm at once must not make the
    // hero unkillable.
    float const cap = m_player->GetMaxHealth() * percent * BONK_LEECH_CAP_PER_PERCENT / 100.0f;
    m_leechCarry = std::min(cap, m_leechCarry + damage * percent / 100.0f);
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

    // A real dodge chance, so enemy swings show "Dodge".
    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);
    if (int32 const dodge = int32(GetPassiveTotal(BONK_PASSIVE_DODGE) + 0.5f))
        m_player->CastCustomSpell(m_player, BONK_SPELL_DODGE, dodge, {}, {}, true);
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
    {
        // Not the full heal a level normally gives: a share per level.
        uint32 const heal = uint32(m_player->GetMaxHealth() * BONK_LEVEL_UP_HEAL * (level - startLevel));
        m_player->SetHealth(std::min(health + heal, m_player->GetMaxHealth()));
    }
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
            {
                orb->DestroyForNearbyPlayers();     // vanish at once, not the out-of-range fade
                orb->DespawnOrUnsummon();
            }
            itr = m_orbs.erase(itr);
            continue;
        }

        // Pulled orbs fly in a straight line, not along a follow path: that
        // slows to a walk at the end and aims ahead of a running player, so
        // orbs would hover beside the player before being taken.
        float const x = m_player->GetPositionX();
        float const y = m_player->GetPositionY();
        float const z = m_player->GetPositionZ() + 0.5f;
        if (!itr->pulled ? distance <= magnet :
            (x - itr->aimX) * (x - itr->aimX) + (y - itr->aimY) * (y - itr->aimY) + (z - itr->aimZ) * (z - itr->aimZ) >
                BONK_ORB_REAIM_DISTANCE * BONK_ORB_REAIM_DISTANCE)
        {
            itr->pulled = true;
            itr->aimX = x;
            itr->aimY = y;
            itr->aimZ = z;
            orb->MonsterMoveWithSpeed(x, y, z, -10.0f, BONK_ORB_FLY_SPEED, MOVE_FLY_MODE);
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
            {
                creature->DestroyForNearbyPlayers();
                creature->DespawnOrUnsummon();
            }
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

void BonkRun::DropHeal(Creature const* mob, BonkSpawnKind kind)
{
    float chance = 100.0f;
    if (kind == BONK_SPAWN_NORMAL)
        chance = BONK_HEAL_CHANCE_NORMAL;
    else if (kind == BONK_SPAWN_ELITE)
        chance = BONK_HEAL_CHANCE_ELITE;
    if (m_healDrops.size() >= BONK_MAX_HEAL_DROPS || !roll_chance_f(chance))
        return;

    // The other side of the spot from a shard.
    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = mob->GetPositionX() - BONK_SHARD_SCATTER * std::cos(angle);
    float y = mob->GetPositionY() - BONK_SHARD_SCATTER * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, mob->GetPositionZ() + 2.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT)
    {
        x = mob->GetPositionX();
        y = mob->GetPositionY();
        z = mob->GetPositionZ();
    }

    if (GameObject* go = PlaceObject(BONK_GO_HEAL, x, y, z, BONK_OBJECT_LIFETIME_SEC))
        m_healDrops.push_back(go->GetObjectGuid());
}

void BonkRun::UpdateHeals(uint32 diff)
{
    m_healTimer += diff;
    if (m_healTimer < BONK_HEAL_CHECK_MS || m_healDrops.empty())
        return;
    m_healTimer = 0;

    // Left lying while the hero is unhurt: one may be needed later.
    bool hurt = m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth();
    float const range = BONK_HEAL_PICKUP_RANGE * (1.0f + GetPassiveTotal(BONK_PASSIVE_MAGNET) / 100.0f);
    Map* map = m_player->GetMap();
    for (auto itr = m_healDrops.begin(); itr != m_healDrops.end();)
    {
        GameObject* go = map->GetGameObject(*itr);
        if (!go || !go->isSpawned())
        {
            itr = m_healDrops.erase(itr);
            continue;
        }
        if (!hurt || m_player->GetDistance(go) > range)
        {
            ++itr;
            continue;
        }

        uint32 const heal = std::max(1u, uint32(m_player->GetMaxHealth() * BONK_HEAL_AMOUNT));
        if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_HEAL_TEXT))
            m_player->DealHeal(m_player, heal, spellInfo);
        else
            m_player->ModifyHealth(int32(heal));
        go->Delete();
        itr = m_healDrops.erase(itr);
        hurt = m_player->GetHealth() < m_player->GetMaxHealth();
    }
}

void BonkRun::GiveShards(uint32 count)
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->AddShards(count);
    sBonkQuests.OnShards(m_player, count);
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
    SpreadFrom(mob);
    uint32 const killingSpell = BonkStats::CountKill(m_player->GetObjectGuid(), mob->GetObjectGuid(), kind);
    sBonkQuests.OnKill(m_player, kind, killingSpell);
    SoulSiphon(killingSpell);
    if (kind == BONK_SPAWN_MINIBOSS)
        m_masteryXp += m_tier.masteryXpMiniboss;
    else if (kind == BONK_SPAWN_BOSS)
        m_masteryXp += m_tier.masteryXpBoss;
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
    DropHeal(mob, kind);

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
            if (m_banished.count({ true, weapon.id }) || sBonkQuests.IsLocked(m_player->GetSession()->GetAccountId(), BONK_REWARD_WEAPON, weapon.id))
                continue;
            if (std::any_of(m_weapons.begin(), m_weapons.end(), [&](OwnedWeapon const& owned) { return owned.weapon.id == weapon.id; }))
                continue;
            candidates.push_back({ { true, weapon.id, 1 }, 2 });
        }

    uint32 const account = m_player->GetSession()->GetAccountId();
    size_t const ownedPassives = std::count_if(m_passiveRanks.begin(), m_passiveRanks.end(), [](auto const& itr) { return itr.second > 0; });
    for (auto const& itr : sBonkMgr.GetPassives())
    {
        auto rank = m_passiveRanks.find(itr.first);
        uint8 const current = rank != m_passiveRanks.end() ? rank->second : 0;
        if (current >= itr.second.maxRank || (!current && ownedPassives >= BONK_MAX_PASSIVES))
            continue;
        if (m_banished.count({ false, itr.first }) || sBonkQuests.IsLocked(account, BONK_REWARD_PASSIVE, itr.first))
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
    SetPaused(true);
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
    SetPaused(false);
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
        SetPaused(false);
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
        SetPaused(false);
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
                {
                    if (offer.newLevel == 1)
                        description = weapon->description + DescribeEvolution(*weapon);
                    else
                        description = DescribeWeaponLevel(*weapon, offer.newLevel) + ".";
                }
            }
            else if (BonkPassive const* passive = sBonkMgr.GetPassive(offer.id))
            {
                description = passive->description;
                // Which of the hero's spells it would let evolve.
                for (auto const& owned : m_weapons)
                    if (BonkWeapon const* evolved = sBonkMgr.GetEvolution(owned.weapon.id))
                        if (evolved->evolvePassive == passive->id && !owned.weapon.evolvesFrom)
                            description += " Evolves " + owned.weapon.name + " at max level.";
            }

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
    SetPaused(false);

    // Going home takes the shards still lying around; dying leaves them.
    if (wentHome)
        CollectAllShards();

    m_player->GetBonkPlayer()->RecordRun(m_stage, m_totalMs / IN_MILLISECONDS, m_kills, m_goldEarned);
    AwardMastery();
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

    // The results screen: what each weapon did.
    if (bonk->IsAddonReady())
    {
        std::vector<BonkSummaryWeapon> weapons;
        for (OwnedWeapon const& owned : m_weapons)
        {
            BonkSummaryWeapon weapon;
            weapon.spellId = owned.weapon.spellId;
            weapon.name = owned.weapon.name;
            weapon.icon = owned.weapon.icon;
            weapon.level = owned.level;
            weapon.maxLevel = owned.weapon.maxLevel;
            weapon.evolved = owned.weapon.evolvesFrom != 0;
            weapons.push_back(weapon);
        }
        BonkStats::SendSummary(m_player, weapons);
    }

    sBonkQuests.Save(m_player->GetSession()->GetAccountId());
    sBonkQuests.SendLog(m_player, "");
}

void BonkRun::SetPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;

    // Everything stops at once, rather than each enemy on its own next
    // update. A root, not an aura: runs make the player immune to those.
    // Auras, casts and ground spells stop too (Unit::SetTimeFrozen).
    if (m_player->IsInWorld())
        m_player->SetRooted(paused);
    m_player->SetTimeFrozen(paused);

    if (Map* map = m_player->FindMap())
        for (ObjectGuid const& guid : m_mobs)
            if (Creature* mob = map->GetCreature(guid))
                if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                    ai->SetFrozen(paused);
}

void BonkRun::OnMobStuck(Creature* mob)
{
    if (m_state != STATE_RUNNING || m_paused)
        return;

    // Close by if there is room, else where new enemies appear.
    float x, y, z;
    if (FindSpawnPosition(x, y, z, BONK_STUCK_MIN_DISTANCE, BONK_STUCK_MAX_DISTANCE) ||
        FindSpawnPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
        mob->NearTeleportTo(x, y, z, mob->GetOrientation());
}

void BonkRun::CleanUp()
{
    SetPaused(false);
    m_player->SetTimeFrozen(false);
    m_player->GetBonkPlayer()->ClearSpellTweaks();
    DespawnMobs();
    DespawnObjects();
    DespawnOrbs();
    m_player->SetCreatureSummonLimit(sWorld.getConfig(CONFIG_UINT32_CREATURE_SUMMON_LIMIT));
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);
    SetMovementImmunity(m_player, false);
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
    {
        m_player->GetBonkPlayer()->RecordRun(m_stage, m_totalMs / IN_MILLISECONDS, m_kills, m_goldEarned);
        AwardMastery();
    }

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
