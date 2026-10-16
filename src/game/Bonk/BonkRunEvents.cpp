/*
 * Vanilla Bonk: the mini-event scheduler, BonkRun's share of the in-run
 * events (feature plan, Mini-events).
 *
 * The stage timeline's blocks have mini-event slots (bonk_wave): a block
 * with one starts one of its allowed events as soon as it can, picked at
 * random but never the same twice running (while there is another), at
 * most one under way and none once the boss is up.
 *
 * They happen to the hero wherever they are, unmarked:
 * - Encircling horde: a ring of enemies closes in, growing through the
 *   stage and with the difficulty's crowd; denser on harder difficulties,
 *   with elites mixed in.
 * - Treasure goblin: a goblin appears close by and runs from the hero for
 *   BONK_GOBLIN_SECONDS; caught, it pays a chest's price in Valor and drops
 *   a free chest. Faster and blinking away when hit on harder difficulties,
 *   with two elites at its side on the hardest.
 * - Bounty: a tougher elite with a skull on its nameplate comes for the hero;
 *   killed within BONK_BOUNTY_SECONDS, it drops a free chest with a choice
 *   more for each affix. It rolls affixes on harder difficulties.
 * - Kobold thieves (Phase 3): kobolds turn up by the orbs lying about, take
 *   them and run; each killed drops what it took and a bonus.
 * - Scourge Invasion: Necrotic Shards a walk away, each pinned, raise undead
 *   round them until destroyed; Valor and experience a shard. One more
 *   shard on harder difficulties, an elite guarding each on the hardest.
 * - Demon portal: a portal a walk away sends demons until destroyed; a
 *   chest. More health and felguard elites on harder difficulties.
 * - Kodo caravan: a caravan kodo walks about 120 yards past the hero while
 *   raiders come up round it every few seconds and go for it; every raider
 *   at its side lands a blow a second. It arrives (a chest with more
 *   choices) or falls; less to fall on harder difficulties, more raiders.
 * - Stampede: a wall of beasts (the map's chargers, or kodos) charges across
 *   where the hero stands after a red warning; a second wall crosses it on
 *   the hardest. A hazard, no reward.
 * A goblin, a bounty, kobolds, shards or a portal are under way until they
 * die or the time is up (m_miniEventActive); the addon shows the time left
 * ("eventtimer~label~seconds~total", 0 seconds hides it).
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    uint32 const BONK_MINI_EVENT_RETRY_MS    = 10 * IN_MILLISECONDS;   // it could not start yet

    float const BONK_HORDE_BASE              = 8.0f;    // enemies in the ring at 0:00...
    float const BONK_HORDE_PER_MINUTE        = 2.2f;    // ...and more each minute, times the tier's crowd
    float const BONK_HORDE_DENSER            = 0.25f;   // more again for each step of difficulty
    float const BONK_HORDE_ELITE_DISTANCE    = 22.0f;   // elites in the ring: one a step of difficulty

    uint32 const BONK_GOBLIN_ENTRY           = 900011;
    uint32 const BONK_GOBLIN_SECONDS         = 20;
    float const BONK_GOBLIN_MIN_DISTANCE     = 18.0f;   // close by, in sight
    float const BONK_GOBLIN_MAX_DISTANCE     = 28.0f;
    float const BONK_GOBLIN_HEALTH           = 0.5f;    // of an elite's
    float const BONK_GOBLIN_SPEED[3]         = { 0.85f, 0.95f, 1.05f };     // by difficulty; the hero runs at 1
    float const BONK_GOBLIN_SCALE            = 1.2f;
    uint32 const BONK_GOBLIN_GUARDS          = 2;       // on the hardest difficulty
    float const BONK_GOBLIN_GUARD_SPREAD     = 4.0f;
    uint32 const BONK_GOBLIN_HEAD_START_MS   = 2000;    // it stands this long before it runs
    float const BONK_EVENT_HEAD_START_SHARE  = 0.2f;    // of Bounty Hunter's extra seconds, more head start
    // A skull on its nameplate and its minimap pin (user, 2026-10-16: hard to find).
    char const* const BONK_GOBLIN_ICON       = "INV_Misc_Bone_HumanSkull_01";

    uint32 const BONK_BOUNTY_SECONDS         = 60;
    float const BONK_BOUNTY_MIN_DISTANCE     = 35.0f;
    float const BONK_BOUNTY_MAX_DISTANCE     = 50.0f;
    float const BONK_BOUNTY_HEALTH           = 2.0f;    // times its elite's
    float const BONK_BOUNTY_SCALE            = 1.25f;
    char const* const BONK_BOUNTY_ICON       = "INV_Misc_Bone_HumanSkull_01";

    uint32 const BONK_SPELL_SPAWN_SMOKE      = 10389;   // a goblin or bounty getting away

    // Kobold thieves: one more a step of difficulty, a little faster too.
    uint32 const BONK_KOBOLD_ENTRY           = 475;     // Kobold Tunneler, candle and all
    uint32 const BONK_KOBOLD_COUNT           = 3;
    uint32 const BONK_KOBOLD_SECONDS         = 20;
    float const BONK_KOBOLD_MIN_DISTANCE     = 12.0f;
    float const BONK_KOBOLD_MAX_DISTANCE     = 35.0f;
    float const BONK_KOBOLD_GRAB_RANGE       = 12.0f;   // the orbs round where one turns up
    float const BONK_KOBOLD_HEALTH           = 0.3f;    // of an elite's
    float const BONK_KOBOLD_SPEED[3]         = { 0.8f, 0.9f, 1.0f };
    uint32 const BONK_KOBOLD_HEAD_START_MS   = 1000;
    float const BONK_KOBOLD_BONUS            = 0.1f;    // of a level's experience each, on top of what it took
    char const* const BONK_KOBOLD_SHOUT      = "You no take candle!";

    // Scourge Invasion.
    uint32 const BONK_SHARD_ENTRY            = 16136;   // Necrotic Shard (the 1.11 invasion)
    uint32 const BONK_SHARD_COUNT            = 3;       // one more from the second step
    uint32 const BONK_SHARD_SECONDS          = 90;
    float const BONK_SHARD_MIN_DISTANCE      = 45.0f;
    float const BONK_SHARD_MAX_DISTANCE      = 90.0f;
    float const BONK_SHARD_SPREAD            = 35.0f;   // apart from one another
    float const BONK_SHARD_HEALTH            = 1.5f;    // of an elite's
    int32 const BONK_SHARD_SPAWN_MS          = 5000;    // each raises undead this often
    uint32 const BONK_SHARD_ADDS             = 2;       // and one more a step
    float const BONK_SHARD_VALOR             = 0.5f;    // of a chest's price, a shard
    float const BONK_SHARD_EXPERIENCE        = 0.25f;   // of a level's, a shard
    char const* const BONK_SHARD_ICON        = "Spell_Shadow_AnimateDead";
    uint32 const BONK_SCOURGE_ADDS[3]        = { 16299, 16141, 16298 };    // Skeletal Shocktrooper, Ghoul Berserker, Spectral Soldier

    // Demon portal.
    uint32 const BONK_PORTAL_ENTRY           = 9707;    // Scarshield Portal
    uint32 const BONK_PORTAL_SECONDS         = 75;      // then it closes on its own, the chest with it
    float const BONK_PORTAL_MIN_DISTANCE     = 40.0f;
    float const BONK_PORTAL_MAX_DISTANCE     = 70.0f;
    float const BONK_PORTAL_HEALTH[3]        = { 3.0f, 4.0f, 5.0f };   // of an elite's
    int32 const BONK_PORTAL_SPAWN_MS         = 6000;
    uint32 const BONK_PORTAL_ADDS            = 3;       // and one more a step
    uint32 const BONK_PORTAL_ELITE_WAVE      = 3;       // from the second step, every third wave a felguard elite
    uint32 const BONK_PORTAL_CHEST_CHOICES   = 4;
    char const* const BONK_PORTAL_ICON       = "Spell_Shadow_SummonInfernal";
    uint32 const BONK_DEMON_ADDS[3]          = { 12922, 6010, 6011 };      // Imp Minion, Felhound, Felguard Sentry

    // Adds come up this far round their shard or portal.
    float const BONK_EVENT_ADD_MIN           = 3.0f;
    float const BONK_EVENT_ADD_MAX           = 7.0f;

    // Stampede.
    uint32 const BONK_STAMPEDE_BEAST         = 3236;    // Barrens Kodo, where the map has no chargers of its own
    uint32 const BONK_STAMPEDE_BEASTS        = 7;       // two more a step
    float const BONK_STAMPEDE_SPACING        = 3.5f;    // between the lanes
    float const BONK_STAMPEDE_BEHIND         = 35.0f;   // they start this far off to one side of the hero
    float const BONK_STAMPEDE_LENGTH         = 70.0f;
    uint32 const BONK_STAMPEDE_WARN_MS       = 2000;
    uint32 const BONK_STAMPEDE_SECOND_MS     = 1200;    // the crossing wall on the hardest, this much later
    float const BONK_STAMPEDE_DAMAGE         = 2.0f;    // in melee hits, as a charger's

    // Kodo caravan.
    uint32 const BONK_CARAVAN_ENTRY          = 11564;   // Gizelton Caravan Kodo
    float const BONK_CARAVAN_START           = 25.0f;   // it turns up this far from the hero...
    float const BONK_CARAVAN_LENGTH          = 120.0f;  // ...and walks this far past them
    float const BONK_CARAVAN_LEG             = 20.0f;   // in legs this long
    float const BONK_CARAVAN_SPEED           = 3.5f;    // yards a second, a kodo's amble
    uint32 const BONK_CARAVAN_SPARE_SECONDS  = 20;      // on top of the walk, then it is lost
    uint32 const BONK_CARAVAN_BLOWS[3]       = { 70, 55, 45 };     // it falls at this many
    float const BONK_CARAVAN_REACH           = 5.0f;    // a raider this close lands a blow a second
    int32 const BONK_CARAVAN_WAVE_MS         = 7000;
    uint32 const BONK_CARAVAN_RAIDERS        = 2;       // a wave, one more a step of difficulty
    float const BONK_CARAVAN_RAID_DISTANCE   = 22.0f;
    uint32 const BONK_CARAVAN_CHEST_CHOICES  = 4;       // and one more a step
    char const* const BONK_CARAVAN_ICON      = "Ability_Mount_Kodo_01";

    char const* const MINI_EVENT_NAMES[MAX_BONK_MINI_EVENT] = { "horde", "goblin", "bounty", "kobolds", "scourge", "portal", "stampede", "caravan" };

}

char const* GetBonkMiniEventName(BonkMiniEventType type)
{
    return type < MAX_BONK_MINI_EVENT ? MINI_EVENT_NAMES[type] : "";
}

bool ParseBonkMiniEvent(char const* name, BonkMiniEventType& type)
{
    for (uint8 i = 0; i < MAX_BONK_MINI_EVENT; ++i)
        if (!strcmp(name, MINI_EVENT_NAMES[i]))
        {
            type = BonkMiniEventType(i);
            return true;
        }
    return false;
}

void BonkRun::UpdateMiniEvents(uint32 diff)
{
    // A goblin or a bounty runs until it dies or gets away; kobolds, shards
    // and a portal until they are all dead or the time is up.
    if (m_miniEventActive)
    {
        if (m_miniEvent.targets.empty())
            UpdateMiniEventTarget(diff);
        else
            UpdateMiniEventGroup(diff);
        return;
    }

    // This block's slot, while it lasts.
    if (!m_miniEventSlots || m_elapsedMs < m_nextMiniEventMs || m_bossSpawned)
        return;

    if (StartMiniEvent(PickMiniEvent(m_miniEventSlots)))
        m_miniEventSlots = 0;
    else
        m_nextMiniEventMs = m_elapsedMs + BONK_MINI_EVENT_RETRY_MS;
}

BonkMiniEventType BonkRun::PickMiniEvent(uint32 allowed) const
{
    std::vector<BonkMiniEventType> choices, repeats;
    for (uint8 i = 0; i < MAX_BONK_MINI_EVENT; ++i)
        if (allowed & (1u << i))
            (BonkMiniEventType(i) != m_lastMiniEvent ? choices : repeats).push_back(BonkMiniEventType(i));
    if (choices.empty())
        choices = repeats;
    if (choices.empty())
        return BONK_MINI_EVENT_HORDE;
    return choices[urand(0, uint32(choices.size()) - 1)];
}

bool BonkRun::StartMiniEvent(BonkMiniEventType type)
{
    m_miniEvent = MiniEvent();
    m_miniEvent.type = type;
    bool started = false;
    switch (type)
    {
        case BONK_MINI_EVENT_HORDE:  started = SpawnHorde(); break;
        case BONK_MINI_EVENT_GOBLIN: started = SpawnTreasureGoblin(); break;
        case BONK_MINI_EVENT_BOUNTY: started = SpawnBounty(); break;
        case BONK_MINI_EVENT_KOBOLDS:  started = SpawnKoboldThieves(); break;
        case BONK_MINI_EVENT_SCOURGE:  started = SpawnScourgeInvasion(); break;
        case BONK_MINI_EVENT_PORTAL:   started = SpawnDemonPortal(); break;
        case BONK_MINI_EVENT_STAMPEDE: started = SpawnStampede(); break;
        case BONK_MINI_EVENT_CARAVAN:  started = SpawnCaravan(); break;
        default:                     break;
    }
    // Whatever was set up for a mob that never came is not left for the next.
    BonkMobAI::SetNextAffixCount(-1);
    BonkMobAI::SetNextPlateIcon(nullptr);
    if (started)
        m_lastMiniEvent = type;
    return started;
}

bool BonkRun::SpawnHorde()
{
    uint32 const step = GetDifficultyStep();
    float const minutes = m_elapsedMs / float(MINUTE * IN_MILLISECONDS);
    BonkMapEvent ring;
    ring.second = m_elapsedMs / IN_MILLISECONDS;
    ring.type = BONK_EVENT_RING;
    ring.count = uint32((BONK_HORDE_BASE + BONK_HORDE_PER_MINUTE * minutes) * m_tier.minAliveMultiplier * (1.0f + BONK_HORDE_DENSER * step));
    if (!SpawnFormation(ring))
        return false;

    // Elites in the ring on harder difficulties.
    for (uint32 i = 0; i < step; ++i)
        if (BonkMapCreature const* elite = PickCreature(BONK_SPAWN_ELITE))
        {
            float x, y, z;
            if (FindSpawnPositionAt(frand(0.0f, 2.0f * M_PI_F), BONK_HORDE_ELITE_DISTANCE, x, y, z))
                SpawnMobAt(*elite, x, y, z);
        }

    m_player->GetBonkPlayer()->Notify("You are surrounded!");
    return true;
}

bool BonkRun::SpawnTreasureGoblin()
{
    float x, y, z;
    if (!FindSpawnPosition(x, y, z, BONK_GOBLIN_MIN_DISTANCE, BONK_GOBLIN_MAX_DISTANCE))
        return false;

    uint32 const step = GetDifficultyStep();
    BonkMapCreature goblin;
    goblin.entry = BONK_GOBLIN_ENTRY;
    goblin.kind = BONK_SPAWN_ELITE;         // a named nameplate, an elite's rewards
    goblin.healthMultiplier = BONK_GOBLIN_HEALTH;
    goblin.speed = BONK_GOBLIN_SPEED[step];
    goblin.scale = BONK_GOBLIN_SCALE;
    BonkMobAI::SetNextAffixCount(0);
    BonkMobAI::SetNextPlateIcon(BONK_GOBLIN_ICON);
    Creature* mob = SpawnMobAt(goblin, x, y, z);
    if (!mob)
        return false;
    // Bounty Hunter: more time to catch it, a fifth of it as head start.
    float const extra = GetUtility(BONK_UTILITY_EVENT_TIME);
    uint32 const seconds = BONK_GOBLIN_SECONDS + uint32(extra);
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->SetFleeing(step >= 1, BONK_GOBLIN_HEAD_START_MS + uint32(extra * BONK_EVENT_HEAD_START_SHARE * IN_MILLISECONDS));

    // The hardest difficulty: elites at its side.
    if (step >= 2)
        for (uint32 i = 0; i < BONK_GOBLIN_GUARDS; ++i)
            if (BonkMapCreature const* guard = PickCreature(BONK_SPAWN_ELITE))
            {
                float const guardX = x + frand(-BONK_GOBLIN_GUARD_SPREAD, BONK_GOBLIN_GUARD_SPREAD);
                float const guardY = y + frand(-BONK_GOBLIN_GUARD_SPREAD, BONK_GOBLIN_GUARD_SPREAD);
                float const guardZ = m_player->GetMap()->GetHeight(guardX, guardY, z + 5.0f, true, 15.0f);
                if (guardZ > INVALID_HEIGHT)
                    SpawnMobAt(*guard, guardX, guardY, guardZ);
            }

    StartMiniEventTarget(mob, "Treasure Goblin", seconds);
    // Pinned on the minimap wherever it runs, and the minimap pinged on it.
    m_miniEvent.objectiveId = m_objectives.Follow(m_player, BONK_OBJECTIVE_EVENT, mob, "Treasure Goblin", seconds,
        BONK_GOBLIN_ICON, "Catch it before it gets away: Valor and a chest.");
    m_player->GetBonkPlayer()->Notify("A treasure goblin! Catch it before it gets away!");
    return true;
}

bool BonkRun::SpawnBounty()
{
    BonkMapCreature const* pick = PickCreature(BONK_SPAWN_ELITE);
    float x, y, z;
    if (!pick || !FindSpawnPosition(x, y, z, BONK_BOUNTY_MIN_DISTANCE, BONK_BOUNTY_MAX_DISTANCE))
        return false;

    BonkMapCreature bounty = *pick;
    bounty.healthMultiplier *= BONK_BOUNTY_HEALTH;
    bounty.scale = (bounty.scale > 0.0f ? bounty.scale : 1.0f) * BONK_BOUNTY_SCALE;
    BonkMobAI::SetNextAffixCount(int32(GetDifficultyStep()));
    BonkMobAI::SetNextPlateIcon(BONK_BOUNTY_ICON);
    Creature* mob = SpawnMobAt(bounty, x, y, z);
    if (!mob)
        return false;

    std::string const name = mob->GetName();
    // Bounty Hunter: more time.
    uint32 const seconds = BONK_BOUNTY_SECONDS + uint32(GetUtility(BONK_UTILITY_EVENT_TIME));
    StartMiniEventTarget(mob, "Bounty: " + name, seconds);
    m_player->GetBonkPlayer()->Notify("Bounty: " + name + "! Kill it within " + std::to_string(seconds) + " seconds for a chest.");
    return true;
}

void BonkRun::StartMiniEventTarget(Creature* target, std::string const& label, uint32 seconds)
{
    m_miniEvent.target = target->GetObjectGuid();
    m_miniEvent.label = label;
    m_miniEvent.leftMs = seconds * IN_MILLISECONDS;
    m_miniEvent.totalSeconds = seconds;
    m_miniEventActive = true;
    SendMiniEventTimer();
}

void BonkRun::UpdateMiniEventTarget(uint32 diff)
{
    Creature* target = m_player->GetMap()->GetCreature(m_miniEvent.target);
    if (!target || !target->IsAlive())
    {
        // Killed (OnMiniEventTargetKilled paid for it), or gone some other way.
        EndMiniEvent();
        return;
    }

    if (m_miniEvent.leftMs > diff)
    {
        m_miniEvent.leftMs -= diff;
        SendMiniEventTimer();
        return;
    }

    // Time's up: it gets away.
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->Notify(m_miniEvent.type == BONK_MINI_EVENT_GOBLIN ? std::string("The treasure goblin got away!") :
        std::string(target->GetName()) + " slipped away.");
    bonk->SendAddon("mobdead~" + std::string(target->GetName()) + "~" + std::to_string(target->GetPlateId()) + "~" +
        BonkMobAI::GetUnitGuidText(target));
    target->CastSpell(target, BONK_SPELL_SPAWN_SMOKE, true);
    target->DespawnOrUnsummon(500);
    EndMiniEvent();
}

void BonkRun::OnMiniEventTargetKilled(Creature* mob)
{
    if (m_miniEventActive && !m_miniEvent.targets.empty())
    {
        OnMiniEventGroupKilled(mob);
        return;
    }
    if (!m_miniEventActive || mob->GetObjectGuid() != m_miniEvent.target)
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    sBonkQuests.OnProgress(m_player, BONK_QUEST_MINI_EVENTS, m_miniEvent.type, 1);
    float const x = mob->GetPositionX(), y = mob->GetPositionY(), z = mob->GetPositionZ();
    if (m_miniEvent.type == BONK_MINI_EVENT_GOBLIN)
    {
        // A chest's worth of Valor, and the chest.
        uint32 const valor = GetChestCost();
        m_coins += valor;
        SpawnChest(true, x, y, z);
        bonk->Notify("Treasure goblin caught: +" + std::to_string(valor) + " Valor and a chest!");
    }
    else
    {
        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        SpawnChest(true, x, y, z, 3 + (ai ? ai->GetAffixCount() : 0));
        bonk->Notify("Bounty claimed: a chest!");
    }
    EndMiniEvent();
    SendState();
}

void BonkRun::SendMiniEventTimer(bool force) const
{
    uint32 const seconds = m_miniEventActive ? (m_miniEvent.leftMs + IN_MILLISECONDS - 1) / IN_MILLISECONDS : 0;
    if (!force && seconds == m_miniEvent.shownSeconds)
        return;
    m_miniEvent.shownSeconds = seconds;
    m_player->GetBonkPlayer()->SendAddon("eventtimer~" + m_miniEvent.label + "~" + std::to_string(seconds) + "~" +
        std::to_string(m_miniEvent.totalSeconds));
}

void BonkRun::EndMiniEvent()
{
    bool const timed = !m_miniEvent.target.IsEmpty() || m_miniEvent.total;
    for (auto const& itr : m_miniEvent.objectives)
        RemoveObjective(itr.second);
    if (m_miniEvent.objectiveId)
        RemoveObjective(m_miniEvent.objectiveId);
    else
        m_spawnPoints.Release(m_miniEvent.spot.x, m_miniEvent.spot.y);
    m_miniEvent = MiniEvent();
    m_miniEventActive = false;
    if (timed)
        m_player->GetBonkPlayer()->SendAddon("eventtimer~~0~0");
}

bool BonkRun::StartMiniEventNow(BonkMiniEventType type, std::string& error)
{
    if (m_state != STATE_RUNNING || m_paused)
    {
        error = "Only a running stage can have a mini-event.";
        return false;
    }
    if (m_miniEventActive)
    {
        if (Creature* target = m_player->GetMap()->GetCreature(m_miniEvent.target))
            target->DespawnOrUnsummon();
        for (ObjectGuid const& guid : m_miniEvent.targets)
            if (Creature* target = m_player->GetMap()->GetCreature(guid))
                target->DespawnOrUnsummon();
        EndMiniEvent();
    }
    if (!StartMiniEvent(type))
    {
        error = "It could not start here (no room for more enemies, or no ground around you).";
        return false;
    }
    return true;
}

bool BonkRun::IsMiniEventTarget(ObjectGuid const& guid) const
{
    if (!m_miniEventActive)
        return false;
    // A caravan's raiders too: they stay with the caravan.
    return guid == m_miniEvent.target || std::find(m_miniEvent.targets.begin(), m_miniEvent.targets.end(), guid) != m_miniEvent.targets.end() ||
        std::find(m_miniEvent.raiders.begin(), m_miniEvent.raiders.end(), guid) != m_miniEvent.raiders.end();
}

bool BonkRun::SpawnKoboldThieves()
{
    uint32 const step = GetDifficultyStep();
    uint32 const count = BONK_KOBOLD_COUNT + step;
    uint32 const bonus = uint32(GetNextLevelXp() * BONK_KOBOLD_BONUS);
    // Bounty Hunter: more time to catch them, and a longer head start.
    float const extra = GetUtility(BONK_UTILITY_EVENT_TIME);
    uint32 const seconds = BONK_KOBOLD_SECONDS + uint32(extra);
    uint32 const headStart = BONK_KOBOLD_HEAD_START_MS + uint32(extra * BONK_EVENT_HEAD_START_SHARE * IN_MILLISECONDS);

    m_miniEvent.targets.clear();
    m_miniEvent.stolen.clear();
    for (uint32 i = 0; i < count; ++i)
    {
        // By an orb lying about if there is one, close by anyway.
        float x, y, z;
        bool byOrb = FindLyingOrb(BONK_KOBOLD_MIN_DISTANCE, BONK_KOBOLD_MAX_DISTANCE, x, y, z);
        if (byOrb)
        {
            float const angle = frand(0.0f, 2.0f * M_PI_F);
            float const kx = x + 2.0f * std::cos(angle), ky = y + 2.0f * std::sin(angle);
            float const kz = m_player->GetMap()->GetHeight(kx, ky, z + 5.0f, true, 10.0f);
            if (kz > INVALID_HEIGHT)
            {
                x = kx;
                y = ky;
                z = kz;
            }
        }
        else if (!FindSpawnPosition(x, y, z, BONK_KOBOLD_MIN_DISTANCE, BONK_KOBOLD_MAX_DISTANCE))
            continue;

        BonkMapCreature kobold;
        kobold.entry = BONK_KOBOLD_ENTRY;
        kobold.kind = BONK_SPAWN_ELITE;         // a named nameplate; it pays through the event
        kobold.healthMultiplier = BONK_KOBOLD_HEALTH;
        kobold.speed = BONK_KOBOLD_SPEED[step];
        BonkMobAI::SetNextAffixCount(0);
        BonkMobAI::SetNextPlateIcon(BONK_GOBLIN_ICON);
        Creature* mob = SpawnMobAt(kobold, x, y, z);
        if (!mob)
            continue;
        if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
            ai->SetFleeing(false, headStart);
        if (m_miniEvent.targets.empty())
            mob->MonsterSay(BONK_KOBOLD_SHOUT);
        m_miniEvent.stolen[mob->GetObjectGuid()] = StealOrbs(x, y, BONK_KOBOLD_GRAB_RANGE) + bonus;
        m_miniEvent.targets.push_back(mob->GetObjectGuid());
        m_miniEvent.objectives[mob->GetObjectGuid()] = m_objectives.Follow(m_player, BONK_OBJECTIVE_EVENT, mob, "Kobold Thief", seconds,
            BONK_GOBLIN_ICON, "It took your experience: kill it to get it back, and more.");
    }
    if (m_miniEvent.targets.empty())
        return false;

    StartMiniEventGroup("Kobold Thieves", seconds);
    m_player->GetBonkPlayer()->Notify("Kobold thieves! Kill them to get your experience back.");
    return true;
}

bool BonkRun::SpawnScourgeInvasion()
{
    uint32 const step = GetDifficultyStep();
    uint32 const count = BONK_SHARD_COUNT + (step >= 1 ? 1 : 0);
    uint32 const seconds = BONK_SHARD_SECONDS + uint32(GetUtility(BONK_UTILITY_EVENT_TIME));

    m_miniEvent.targets.clear();
    std::vector<std::pair<float, float>> placed;
    for (uint32 i = 0; i < count; ++i)
    {
        // A walk away, and apart from the others.
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool found = false;
        for (uint32 attempt = 0; attempt < 8 && !found; ++attempt)
        {
            if (!FindSpawnPosition(x, y, z, BONK_SHARD_MIN_DISTANCE, BONK_SHARD_MAX_DISTANCE))
                break;
            found = std::none_of(placed.begin(), placed.end(), [x, y](std::pair<float, float> const& other)
                { return (other.first - x) * (other.first - x) + (other.second - y) * (other.second - y) < BONK_SHARD_SPREAD * BONK_SHARD_SPREAD; });
        }
        if (!found)
            continue;

        BonkMapCreature shard;
        shard.entry = BONK_SHARD_ENTRY;
        shard.kind = BONK_SPAWN_ELITE;
        shard.healthMultiplier = BONK_SHARD_HEALTH;
        BonkMobAI::SetNextAffixCount(0);
        BonkMobAI::SetNextPlateIcon(BONK_SHARD_ICON);
        Creature* mob = SpawnMobAt(shard, x, y, z);
        if (!mob)
            continue;
        if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
            ai->SetStationary();
        placed.emplace_back(x, y);
        m_miniEvent.targets.push_back(mob->GetObjectGuid());
        m_miniEvent.objectives[mob->GetObjectGuid()] = m_objectives.Follow(m_player, BONK_OBJECTIVE_EVENT, mob, "Necrotic Shard", seconds,
            BONK_SHARD_ICON, "It raises undead until destroyed: Valor and experience.");
        // The hardest: an elite stands guard.
        if (step >= 2)
            SpawnEventAdds(mob, 0, 1);
    }
    if (m_miniEvent.targets.empty())
        return false;

    m_miniEvent.spawnMs = BONK_SHARD_SPAWN_MS / 2;
    StartMiniEventGroup("Scourge Invasion", seconds);
    m_player->GetBonkPlayer()->Notify("The Scourge invades! Destroy the Necrotic Shards.");
    return true;
}

bool BonkRun::SpawnDemonPortal()
{
    uint32 const step = GetDifficultyStep();
    float x, y, z;
    if (!FindSpawnPosition(x, y, z, BONK_PORTAL_MIN_DISTANCE, BONK_PORTAL_MAX_DISTANCE))
        return false;

    BonkMapCreature portal;
    portal.entry = BONK_PORTAL_ENTRY;
    portal.kind = BONK_SPAWN_ELITE;
    portal.healthMultiplier = BONK_PORTAL_HEALTH[step];
    BonkMobAI::SetNextAffixCount(0);
    BonkMobAI::SetNextPlateIcon(BONK_PORTAL_ICON);
    Creature* mob = SpawnMobAt(portal, x, y, z);
    if (!mob)
        return false;
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->SetStationary();

    uint32 const seconds = BONK_PORTAL_SECONDS + uint32(GetUtility(BONK_UTILITY_EVENT_TIME));
    m_miniEvent.targets.assign(1, mob->GetObjectGuid());
    m_miniEvent.objectives[mob->GetObjectGuid()] = m_objectives.Follow(m_player, BONK_OBJECTIVE_EVENT, mob, "Demon Portal", seconds,
        BONK_PORTAL_ICON, "Demons pour out until it is destroyed: a chest.");
    m_miniEvent.spawnMs = 0;
    StartMiniEventGroup("Demon Portal", seconds);
    m_player->GetBonkPlayer()->Notify("A demon portal opens! Destroy it before it closes.");
    return true;
}

bool BonkRun::SpawnStampede()
{
    // The map's own chargers if it has any, kodos otherwise.
    BonkMapCreature beast;
    beast.entry = BONK_STAMPEDE_BEAST;
    for (BonkMapCreature const& creature : m_map.creatures)
        if (creature.kind == BONK_SPAWN_NORMAL && creature.archetype == BONK_ARCHETYPE_CHARGER)
        {
            beast = creature;
            break;
        }
    beast.kind = BONK_SPAWN_NORMAL;
    beast.archetype = BONK_ARCHETYPE_GRUNT;     // never a charger's own timer

    uint32 const step = GetDifficultyStep();
    uint32 const count = BONK_STAMPEDE_BEASTS + 2 * step;
    // A wall wide enough that a few yards of the server trailing the hero
    // makes no odds.
    float heroX, heroY, heroZ;
    m_player->GetPosition(heroX, heroY, heroZ);
    float const first = frand(0.0f, 2.0f * M_PI_F);
    uint32 spawned = 0;
    // A wall across where the hero stands; on the hardest a second crosses it.
    for (uint32 wall = 0; wall < (step >= 2 ? 2u : 1u); ++wall)
    {
        float const angle = first + wall * M_PI_F / 2.0f;
        float const sideX = -std::sin(angle), sideY = std::cos(angle);
        uint32 const warnMs = BONK_STAMPEDE_WARN_MS + wall * BONK_STAMPEDE_SECOND_MS;
        for (uint32 i = 0; i < count; ++i)
        {
            float const offset = (float(i) - (count - 1) / 2.0f) * BONK_STAMPEDE_SPACING;
            float const x = heroX - BONK_STAMPEDE_BEHIND * std::cos(angle) + offset * sideX;
            float const y = heroY - BONK_STAMPEDE_BEHIND * std::sin(angle) + offset * sideY;
            float const z = m_player->GetMap()->GetHeight(x, y, heroZ + 10.0f, true, 30.0f);
            if (z <= INVALID_HEIGHT)
                continue;
            Creature* mob = SpawnMobAt(beast, x, y, z);
            if (!mob)
                continue;
            if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                ai->StartStampede(m_player, angle, BONK_STAMPEDE_LENGTH, warnMs, BONK_STAMPEDE_DAMAGE);
            ++spawned;
        }
    }
    if (!spawned)
        return false;
    m_player->GetBonkPlayer()->Notify("Stampede! Get out of the way!");
    return true;
}

void BonkRun::SpawnEventAdds(Creature const* source, uint32 count, uint32 eliteCount)
{
    bool const scourge = m_miniEvent.type == BONK_MINI_EVENT_SCOURGE;
    uint32 const* entries = scourge ? BONK_SCOURGE_ADDS : BONK_DEMON_ADDS;
    for (uint32 i = 0; i < count + eliteCount; ++i)
    {
        if (m_mobs.size() >= m_tier.maxAlive)
            return;
        float const angle = frand(0.0f, 2.0f * M_PI_F);
        float const distance = frand(BONK_EVENT_ADD_MIN, BONK_EVENT_ADD_MAX);
        float const x = source->GetPositionX() + distance * std::cos(angle);
        float const y = source->GetPositionY() + distance * std::sin(angle);
        float const z = m_player->GetMap()->GetHeight(x, y, source->GetPositionZ() + 5.0f, true, 15.0f);
        if (z <= INVALID_HEIGHT)
            continue;

        BonkMapCreature add;
        bool const elite = i >= count;
        if (elite)
        {
            // A shard's guard is one of the map's elites; a portal's a felguard.
            BonkMapCreature const* pick = scourge ? PickCreature(BONK_SPAWN_ELITE) : nullptr;
            if (pick)
                add = *pick;
            else
            {
                add.entry = BONK_DEMON_ADDS[2];
                add.kind = BONK_SPAWN_ELITE;
            }
        }
        else
        {
            // Swarmers mostly, a grunt now and then, a bruiser seldom.
            uint32 const roll = urand(0, 9);
            uint32 const which = roll < 6 ? 0 : roll < 9 ? 1 : 2;
            add.entry = entries[which];
            add.kind = BONK_SPAWN_NORMAL;
            add.archetype = which == 0 ? BONK_ARCHETYPE_SWARMER : which == 1 ? BONK_ARCHETYPE_GRUNT : BONK_ARCHETYPE_BRUISER;
        }
        if (elite)
            BonkMobAI::SetNextAffixCount(-1);
        SpawnMobAt(add, x, y, z);
    }
}

void BonkRun::StartMiniEventGroup(std::string const& label, uint32 seconds)
{
    m_miniEvent.label = label;
    m_miniEvent.leftMs = seconds * IN_MILLISECONDS;
    m_miniEvent.totalSeconds = seconds;
    m_miniEvent.total = uint32(m_miniEvent.targets.size());
    m_miniEventActive = true;
    SendMiniEventTimer(true);
}

void BonkRun::UpdateMiniEventGroup(uint32 diff)
{
    // A caravan runs its own course.
    if (m_miniEvent.type == BONK_MINI_EVENT_CARAVAN)
    {
        if (UpdateCaravan(diff))
            return;
        if (m_miniEvent.leftMs > diff)
        {
            m_miniEvent.leftMs -= diff;
            SendMiniEventTimer();
        }
        else
            EndCaravan(false);
        return;
    }

    // Gone some other way (a despawn): no longer waited for.
    Map* map = m_player->GetMap();
    for (auto itr = m_miniEvent.targets.begin(); itr != m_miniEvent.targets.end();)
    {
        Creature* target = map->GetCreature(*itr);
        if (target && target->IsAlive())
        {
            ++itr;
            continue;
        }
        auto const objective = m_miniEvent.objectives.find(*itr);
        if (objective != m_miniEvent.objectives.end())
        {
            RemoveObjective(objective->second);
            m_miniEvent.objectives.erase(objective);
        }
        itr = m_miniEvent.targets.erase(itr);
    }
    if (m_miniEvent.targets.empty())
    {
        EndMiniEvent();
        return;
    }

    // Shards and a portal send out more.
    if (m_miniEvent.type == BONK_MINI_EVENT_SCOURGE || m_miniEvent.type == BONK_MINI_EVENT_PORTAL)
    {
        m_miniEvent.spawnMs -= int32(diff);
        if (m_miniEvent.spawnMs <= 0)
        {
            uint32 const step = GetDifficultyStep();
            bool const scourge = m_miniEvent.type == BONK_MINI_EVENT_SCOURGE;
            m_miniEvent.spawnMs += scourge ? BONK_SHARD_SPAWN_MS : BONK_PORTAL_SPAWN_MS;
            ++m_miniEvent.waves;
            uint32 const adds = (scourge ? BONK_SHARD_ADDS : BONK_PORTAL_ADDS) + step;
            uint32 const elites = !scourge && step >= 1 && m_miniEvent.waves % BONK_PORTAL_ELITE_WAVE == 0 ? 1 : 0;
            for (ObjectGuid const& guid : m_miniEvent.targets)
                if (Creature const* source = map->GetCreature(guid))
                    SpawnEventAdds(source, adds, elites);
        }
    }

    if (m_miniEvent.leftMs > diff)
    {
        m_miniEvent.leftMs -= diff;
        SendMiniEventTimer();
        return;
    }

    // Time's up: what is left goes.
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    switch (m_miniEvent.type)
    {
        case BONK_MINI_EVENT_KOBOLDS: bonk->Notify("The kobolds got away with your experience!"); break;
        case BONK_MINI_EVENT_SCOURGE: bonk->Notify("The invasion moves on."); break;
        case BONK_MINI_EVENT_PORTAL:  bonk->Notify("The portal closes."); break;
        default: break;
    }
    for (ObjectGuid const& guid : m_miniEvent.targets)
        if (Creature* target = map->GetCreature(guid))
        {
            bonk->SendAddon("mobdead~" + std::string(target->GetName()) + "~" + std::to_string(target->GetPlateId()) + "~" +
                BonkMobAI::GetUnitGuidText(target));
            target->CastSpell(target, BONK_SPELL_SPAWN_SMOKE, true);
            target->DespawnOrUnsummon(500);
        }
    EndMiniEvent();
}

void BonkRun::OnMiniEventGroupKilled(Creature* mob)
{
    auto const itr = std::find(m_miniEvent.targets.begin(), m_miniEvent.targets.end(), mob->GetObjectGuid());
    if (itr == m_miniEvent.targets.end())
        return;
    m_miniEvent.targets.erase(itr);
    auto const objective = m_miniEvent.objectives.find(mob->GetObjectGuid());
    if (objective != m_miniEvent.objectives.end())
    {
        RemoveObjective(objective->second);
        m_miniEvent.objectives.erase(objective);
    }

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    float const x = mob->GetPositionX(), y = mob->GetPositionY(), z = mob->GetPositionZ();
    uint32 const done = m_miniEvent.total - uint32(m_miniEvent.targets.size());
    switch (m_miniEvent.type)
    {
        case BONK_MINI_EVENT_KOBOLDS:
        {
            // What it took, and a bonus.
            auto const loot = m_miniEvent.stolen.find(mob->GetObjectGuid());
            if (loot != m_miniEvent.stolen.end() && loot->second)
                SpawnOrb(x, y, z, loot->second, BONK_ORB_RED);
            bonk->Notify("Kobold thief caught (" + std::to_string(done) + "/" + std::to_string(m_miniEvent.total) + ")!");
            break;
        }
        case BONK_MINI_EVENT_SCOURGE:
        {
            uint32 const valor = std::max(1u, uint32(GetChestCost() * BONK_SHARD_VALOR + 0.5f));
            m_coins += valor;
            SpawnOrb(x, y, z, uint32(GetNextLevelXp() * BONK_SHARD_EXPERIENCE), BONK_ORB_RED);
            bonk->Notify("Necrotic Shard destroyed (" + std::to_string(done) + "/" + std::to_string(m_miniEvent.total) + "): +" +
                std::to_string(valor) + " Valor.");
            SendState();
            break;
        }
        case BONK_MINI_EVENT_PORTAL:
            SpawnChest(true, x, y, z, BONK_PORTAL_CHEST_CHOICES);
            bonk->Notify("The demon portal is destroyed: a chest!");
            break;
        default:
            break;
    }

    if (!m_miniEvent.targets.empty())
        return;
    sBonkQuests.OnProgress(m_player, BONK_QUEST_MINI_EVENTS, m_miniEvent.type, 1);
    if (m_miniEvent.type == BONK_MINI_EVENT_SCOURGE)
        bonk->Notify("The invasion is turned back!");
    else if (m_miniEvent.type == BONK_MINI_EVENT_KOBOLDS)
        bonk->Notify("Every kobold thief caught!");
    EndMiniEvent();
}

bool BonkRun::SpawnCaravan()
{
    // It turns up to one side of the hero and walks past them, as far as
    // the ground lets it.
    uint32 const step = GetDifficultyStep();
    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x, y, z;
    if (!FindSpawnPositionAt(angle, BONK_CARAVAN_START, x, y, z))
        return false;
    float const heading = angle + M_PI_F + frand(-0.4f, 0.4f);
    std::vector<BonkSpot> path;
    float px = x, py = y, pz = z;
    for (float walked = BONK_CARAVAN_LEG; walked <= BONK_CARAVAN_LENGTH; walked += BONK_CARAVAN_LEG)
    {
        float nx = x + walked * std::cos(heading), ny = y + walked * std::sin(heading), nz = pz;
        m_player->GetMap()->GetWalkHitPosition(nullptr, px, py, pz, nx, ny, nz);
        if (std::hypot(nx - px, ny - py) < BONK_CARAVAN_LEG / 2.0f)
            break;      // a wall or a cliff: it stops there
        BonkSpot spot;
        spot.x = nx;
        spot.y = ny;
        spot.z = nz;
        path.push_back(spot);
        px = nx;
        py = ny;
        pz = nz;
    }
    if (path.size() < 3)
        return false;

    Creature* caravan = SummonCaravan(BONK_CARAVAN_ENTRY, x, y, z, heading);
    if (!caravan)
        return false;

    m_miniEvent.targets.assign(1, caravan->GetObjectGuid());
    m_miniEvent.path = path;
    m_miniEvent.pathIndex = 0;
    m_miniEvent.raiders.clear();
    m_miniEvent.tickMs = 0;
    m_miniEvent.blows = 0;
    m_miniEvent.maxBlows = BONK_CARAVAN_BLOWS[step];
    m_miniEvent.warned = 0;
    m_miniEvent.spawnMs = BONK_CARAVAN_WAVE_MS / 2;
    m_miniEvent.waves = 0;
    caravan->GetMotionMaster()->MovePoint(1, path[0].x, path[0].y, path[0].z, MOVE_PATHFINDING | MOVE_WALK_MODE, BONK_CARAVAN_SPEED);

    uint32 const seconds = uint32(BONK_CARAVAN_LEG * path.size() / BONK_CARAVAN_SPEED) + BONK_CARAVAN_SPARE_SECONDS;
    m_miniEvent.objectives[caravan->GetObjectGuid()] = m_objectives.Follow(m_player, BONK_OBJECTIVE_EVENT, caravan, "Kodo Caravan", seconds,
        BONK_CARAVAN_ICON, "Keep the raiders off it until it gets through: a chest with more choices.");
    StartMiniEventGroup("Kodo Caravan", seconds);
    m_player->GetBonkPlayer()->Notify("A caravan! Keep the raiders off it until it gets through.");
    return true;
}

bool BonkRun::UpdateCaravan(uint32 diff)
{
    Map* map = m_player->GetMap();
    Creature* caravan = m_miniEvent.targets.empty() ? nullptr : map->GetCreature(m_miniEvent.targets.front());
    if (!caravan || !caravan->IsAlive())
    {
        EndCaravan(false);
        return true;
    }

    // On to the next leg once it gets to this one; through, it arrives.
    BonkSpot const& leg = m_miniEvent.path[m_miniEvent.pathIndex];
    if (caravan->GetDistance2d(leg.x, leg.y) < 2.0f)
    {
        if (++m_miniEvent.pathIndex >= m_miniEvent.path.size())
        {
            EndCaravan(true);
            return true;
        }
        BonkSpot const& next = m_miniEvent.path[m_miniEvent.pathIndex];
        caravan->GetMotionMaster()->MovePoint(1, next.x, next.y, next.z, MOVE_PATHFINDING | MOVE_WALK_MODE, BONK_CARAVAN_SPEED);
    }
    else if (!caravan->IsMoving())
        caravan->GetMotionMaster()->MovePoint(1, leg.x, leg.y, leg.z, MOVE_PATHFINDING | MOVE_WALK_MODE, BONK_CARAVAN_SPEED);

    // Raiders come up round it.
    uint32 const step = GetDifficultyStep();
    m_miniEvent.spawnMs -= int32(diff);
    if (m_miniEvent.spawnMs <= 0)
    {
        m_miniEvent.spawnMs += BONK_CARAVAN_WAVE_MS;
        ++m_miniEvent.waves;
        uint32 const count = BONK_CARAVAN_RAIDERS + step;
        bool const elite = step >= 2 && m_miniEvent.waves % 2 == 0;
        for (uint32 i = 0; i < count + (elite ? 1 : 0); ++i)
        {
            if (m_mobs.size() >= m_tier.maxAlive)
                break;
            BonkMapCreature const* pick = PickCreature(i < count ? BONK_SPAWN_NORMAL : BONK_SPAWN_ELITE);
            if (!pick)
                continue;
            float const angle = frand(0.0f, 2.0f * M_PI_F);
            float const rx = caravan->GetPositionX() + BONK_CARAVAN_RAID_DISTANCE * std::cos(angle);
            float const ry = caravan->GetPositionY() + BONK_CARAVAN_RAID_DISTANCE * std::sin(angle);
            float const rz = map->GetHeight(rx, ry, caravan->GetPositionZ() + 10.0f, true, 30.0f);
            if (rz <= INVALID_HEIGHT)
                continue;
            if (Creature* raider = SpawnMobAt(*pick, rx, ry, rz))
                if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(raider->AI()))
                {
                    ai->SetRaidTarget(caravan->GetObjectGuid());
                    m_miniEvent.raiders.push_back(raider->GetObjectGuid());
                }
        }
    }

    // Every raider at its side lands a blow a second.
    m_miniEvent.tickMs += diff;
    if (m_miniEvent.tickMs < IN_MILLISECONDS)
        return false;
    m_miniEvent.tickMs -= IN_MILLISECONDS;
    uint32 blows = 0;
    for (auto itr = m_miniEvent.raiders.begin(); itr != m_miniEvent.raiders.end();)
    {
        Creature* raider = map->GetCreature(*itr);
        if (!raider || !raider->IsAlive())
        {
            itr = m_miniEvent.raiders.erase(itr);
            continue;
        }
        if (raider->IsWithinDistInMap(caravan, BONK_CARAVAN_REACH))
        {
            raider->SetFacingToObject(caravan);
            raider->HandleEmote(EMOTE_ONESHOT_ATTACK1H);
            ++blows;
        }
        ++itr;
    }
    if (!blows)
        return false;
    m_miniEvent.blows += blows;
    uint32 const left = m_miniEvent.blows >= m_miniEvent.maxBlows ? 0 : m_miniEvent.maxBlows - m_miniEvent.blows;
    caravan->SetHealth(std::max(1u, uint32(uint64(caravan->GetMaxHealth()) * left / m_miniEvent.maxBlows)));
    if (!left)
    {
        EndCaravan(false);
        return true;
    }
    // Warnings at half and a quarter.
    uint8 const mark = left * 4 <= m_miniEvent.maxBlows ? 2 : left * 2 <= m_miniEvent.maxBlows ? 1 : 0;
    if (mark > m_miniEvent.warned)
    {
        m_miniEvent.warned = mark;
        m_player->GetBonkPlayer()->Notify(mark == 2 ? "The caravan is about to fall!" : "The caravan is under attack!");
    }
    return false;
}

void BonkRun::PauseCaravan(bool paused)
{
    Creature* caravan = m_miniEvent.targets.empty() ? nullptr : m_player->GetMap()->GetCreature(m_miniEvent.targets.front());
    if (!caravan)
        return;
    if (paused)
    {
        caravan->StopMoving();
        caravan->GetMotionMaster()->Clear(false);
        caravan->GetMotionMaster()->MoveIdle();
    }
    // Going on: UpdateCaravan sends it to its leg again.
}

void BonkRun::EndCaravan(bool arrived)
{
    Map* map = m_player->GetMap();
    Creature* caravan = m_miniEvent.targets.empty() ? nullptr : map->GetCreature(m_miniEvent.targets.front());
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    // Its raiders turn on the hero.
    for (ObjectGuid const& guid : m_miniEvent.raiders)
        if (Creature* raider = map->GetCreature(guid))
            if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(raider->AI()))
                ai->ClearRaidTarget();
    if (caravan)
    {
        if (arrived)
        {
            SpawnChest(true, caravan->GetPositionX(), caravan->GetPositionY(), caravan->GetPositionZ(), BONK_CARAVAN_CHEST_CHOICES + GetDifficultyStep());
            sBonkQuests.OnProgress(m_player, BONK_QUEST_MINI_EVENTS, BONK_MINI_EVENT_CARAVAN, 1);
            bonk->Notify("The caravan got through: a chest!");
        }
        else
        {
            caravan->CastSpell(caravan, BONK_SPELL_SPAWN_SMOKE, true);
            bonk->Notify("The caravan is lost.");
        }
        caravan->DespawnOrUnsummon(arrived ? 3000 : 500);
    }
    else
        bonk->Notify("The caravan is lost.");
    EndMiniEvent();
}
