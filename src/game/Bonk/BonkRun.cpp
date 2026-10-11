/*
 * Vanilla Bonk: the run loop.
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "Chat.h"
#include "Creature.h"
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

    float const BONK_SPAWN_MIN_DISTANCE    = 26.0f;
    float const BONK_SPAWN_MAX_DISTANCE    = 36.0f;
    float const BONK_SPAWN_MAX_HEIGHT_DIFF = 20.0f;

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
            case BONK_SPAWN_ELITE: return 10.0f;
            case BONK_SPAWN_BOSS:  return 150.0f;
            default:               return 1.0f;
        }
    }

    float GetKindDamage(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE: return 2.0f;
            case BONK_SPAWN_BOSS:  return 4.0f;
            default:               return 1.0f;
        }
    }

    uint32 GetKindReward(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE: return 5;
            case BONK_SPAWN_BOSS:  return 50;
            default:               return 1;
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

BonkRun::BonkRun(Player* player, BonkMap const& map) : m_player(player), m_map(map)
{
}

bool BonkRun::Begin(std::string& error)
{
    // GM mode makes the player untargetable, which would make the run trivial.
    if (m_player->IsGameMaster())
        m_player->SetGameMaster(false);

    m_player->GetBonkPlayer()->ResetCharacter();
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);

    if (!m_player->TeleportTo(m_map.mapId, m_map.x, m_map.y, m_map.z, m_map.o))
    {
        error = "Could not enter " + m_map.name + ".";
        return false;
    }

    m_state = STATE_ENTERING;
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
                StartRunning();
                return;
            }

            m_enterTimer += diff;
            if (m_enterTimer >= BONK_ENTER_TIMEOUT_MS)
            {
                m_player->GetBonkPlayer()->Notify("Could not enter " + m_map.name + ".");
                m_state = STATE_FINISHED;
            }
            break;
        }
        case STATE_RUNNING:
            UpdateRunning(diff);
            break;
        case STATE_ENDING:
            m_endTimer -= int32(diff);
            if (m_endTimer <= 0)
                Exit();
            break;
        case STATE_FINISHED:
            break;
    }
}

void BonkRun::StartRunning()
{
    m_state = STATE_RUNNING;
    m_spawnTimer = BONK_FIRST_WAVE_MS;
    m_eliteTimer = m_map.eliteIntervalSeconds * IN_MILLISECONDS;
    m_lastLevel = m_player->GetLevel();
    m_safeX = m_player->GetPositionX();
    m_safeY = m_player->GetPositionY();
    m_safeZ = m_player->GetPositionZ();

    // The summon limit is stored per map, so set it once we are in the arena.
    m_player->SetCreatureSummonLimit(BONK_SUMMON_LIMIT);

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

    SendFullState();
    m_player->GetBonkPlayer()->Notify(m_map.name + " - survive " + FormatTime(m_map.durationSeconds) + "!");
}

void BonkRun::SendFullState() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("start~" + m_map.name + "~" + m_map.GetDifficultyName() + "~" + std::to_string(m_map.durationSeconds));
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

    uint32 const level = m_player->GetLevel();
    if (level > m_lastLevel)
    {
        m_pendingLevels += level - m_lastLevel;
        m_lastLevel = level;
    }

    if (m_pendingLevels && m_offer.empty())
        OfferUpgrades();

    if (m_paused)
        return;

    m_elapsedMs += diff;

    UpdateBoundary(diff);
    UpdateRecovery(diff);
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

    if (!m_bossSpawned && elapsedSeconds >= m_map.durationSeconds)
    {
        m_bossSpawned = true;
        if (BonkMapCreature const* boss = PickCreature(BONK_SPAWN_BOSS))
        {
            SpawnMob(*boss);
            m_player->GetBonkPlayer()->Notify("The boss has arrived!");
        }
        else
        {
            Finish(true);
            return;
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

        float const batch = m_map.spawnBatch + m_map.spawnBatchGrowthPerMinute * (m_elapsedMs / float(MINUTE * IN_MILLISECONDS));
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

bool BonkRun::FindSpawnPosition(float& x, float& y, float& z) const
{
    Map const* map = m_player->GetMap();
    float const playerZ = m_player->GetPositionZ();

    for (uint32 attempt = 0; attempt < 10; ++attempt)
    {
        float const angle = frand(0.0f, 2.0f * M_PI_F);
        float const distance = frand(BONK_SPAWN_MIN_DISTANCE, BONK_SPAWN_MAX_DISTANCE);
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

void BonkRun::SpawnMob(BonkMapCreature const& creature)
{
    float x, y, z;
    if (!FindSpawnPosition(x, y, z))
        return;

    float const facing = std::atan2(m_player->GetPositionY() - y, m_player->GetPositionX() - x);
    Creature* mob = m_player->SummonCreature(creature.entry, x, y, z, facing, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2000, false, 0, &BonkMobAI::Install);
    if (!mob)
        return;

    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->SetKind(creature.kind);

    uint32 level = GetMobLevel();
    if (creature.kind == BONK_SPAWN_ELITE)
        level += 1;
    else if (creature.kind == BONK_SPAWN_BOSS)
        level += 2;
    level = std::min(level, 63u);

    mob->SetLevel(level);
    mob->InitStatsForLevel();

    float const minutes = m_elapsedMs / float(MINUTE * IN_MILLISECONDS);
    uint32 const health = std::max(1u, uint32(GetMobHealth(level) * m_map.healthMultiplier * creature.healthMultiplier *
        GetKindHealth(creature.kind) * (1.0f + m_map.healthGrowthPerMinute * minutes)));
    mob->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
    mob->SetMaxHealth(health);
    mob->SetHealth(health);

    if (CreatureClassLevelStats const* stats = sObjectMgr.GetCreatureClassLevelStats(CLASS_WARRIOR, level))
    {
        float const damage = stats->melee_damage * m_map.damageMultiplier * creature.damageMultiplier * GetKindDamage(creature.kind);
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

    // Same as Player::GiveXP without the chat line for every kill.
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
    GiveGold(m_map.goldPerKill * GetKindReward(kind));

    if (kind != BONK_SPAWN_BOSS)
    {
        // Early levels take a handful of kills, later ones a few dozen.
        uint32 const level = m_player->GetLevel();
        uint32 const killsForLevel = 4 + 2 * level;
        float const growth = 1.0f + GetPassiveTotal(BONK_PASSIVE_GROWTH) / 100.0f;
        uint32 const xp = uint32(std::ceil(m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP) / float(killsForLevel) * GetKindReward(kind) * growth));
        GiveExperience(xp);
    }

    if (mob->GetObjectGuid() == m_bossGuid)
        Finish(true);
}

void BonkRun::OnMobStrayed(Creature* mob, BonkSpawnKind kind)
{
    // A boss is never recycled: bring it back next to the player instead.
    if (kind != BONK_SPAWN_BOSS || m_state != STATE_RUNNING)
        return;

    float x, y, z;
    if (FindSpawnPosition(x, y, z))
        mob->NearTeleportTo(x, y, z, mob->GetOrientation());
}

void BonkRun::OfferUpgrades()
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
        --m_pendingLevels;
        GiveGold(m_map.goldPerKill * 25);
        m_player->GetBonkPlayer()->Notify("Everything is maxed - have some gold!");
        return;
    }

    m_paused = true;
    SendOffer();
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
    if (m_state != STATE_RUNNING || m_offer.empty())
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
    if (m_pendingLevels)
        --m_pendingLevels;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("picked");
    SendLoadout();

    if (m_pendingLevels)
        OfferUpgrades();
    else
        m_paused = false;
    return true;
}

void BonkRun::SendState() const
{
    m_player->GetBonkPlayer()->SendAddon("state~" + std::to_string(m_elapsedMs / IN_MILLISECONDS) + "~" + std::to_string(m_map.durationSeconds) + "~" +
        std::to_string(m_kills) + "~" + std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + (m_bossSpawned ? "1" : "0"));
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
        bonk->SendAddon("offer~" + std::to_string(m_player->GetLevel()) + "~" + std::to_string(m_offer.size()));
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
    handler.PSendSysMessage("Level %u! Choose an upgrade with .bonk pick <number>:", m_player->GetLevel());
    for (size_t i = 0; i < m_offer.size(); ++i)
    {
        std::string const name = DescribeOffer(m_offer[i], nullptr, nullptr);
        handler.PSendSysMessage("  %u) %s %s", uint32(i + 1), name.c_str(),
            m_offer[i].newLevel == 1 ? "(new)" : ("(level " + std::to_string(m_offer[i].newLevel) + ")").c_str());
    }
}

void BonkRun::Finish(bool victory)
{
    if (m_state != STATE_RUNNING)
        return;

    m_state = STATE_ENDING;
    m_victory = victory;
    m_paused = false;
    m_offer.clear();
    m_endTimer = BONK_END_DELAY_MS;

    // Enemies despawn themselves once the run is no longer active.
    if (victory)
        GiveGold(m_map.victoryGold);

    if (m_player->IsAlive())
        m_player->CombatStop(true);

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    uint32 const seconds = m_elapsedMs / IN_MILLISECONDS;
    if (bonk->IsAddonReady())
        bonk->SendAddon("end~" + std::string(victory ? "1" : "0") + "~" + std::to_string(seconds) + "~" + std::to_string(m_kills) + "~" +
            std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()));
    else
        ChatHandler(m_player).PSendSysMessage("%s Survived %s, %u kills, level %u, earned %s.", victory ? "Victory!" : "Defeated.",
            FormatTime(seconds).c_str(), m_kills, m_player->GetLevel(), FormatGold(m_goldEarned).c_str());
}

void BonkRun::Exit()
{
    DespawnMobs();
    m_state = STATE_FINISHED;
    m_player->SetCreatureSummonLimit(sWorld.getConfig(CONFIG_UINT32_CREATURE_SUMMON_LIMIT));
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->ResetCharacter();
    bonk->TeleportToCamp();
}

void BonkRun::Abort()
{
    if (m_state == STATE_FINISHED)
        return;

    DespawnMobs();
    m_state = STATE_FINISHED;
    m_player->SetCreatureSummonLimit(sWorld.getConfig(CONFIG_UINT32_CREATURE_SUMMON_LIMIT));
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    m_player->GetBonkPlayer()->ResetCharacter();
}

void BonkRun::Surrender()
{
    if (m_state == STATE_RUNNING)
        Finish(false);
    else if (m_state == STATE_ENTERING)
    {
        m_state = STATE_FINISHED;
        m_player->GetBonkPlayer()->TeleportToCamp();
    }
}
