/*
 * Vanilla Bonk: the mini-event scheduler, BonkRun's share of the in-run
 * events (feature plan, Mini-events).
 *
 * One mini-event every BONK_MINI_EVENT_INTERVAL_MS of a stage, picked at
 * random but never the same twice running (while there is more than one),
 * at most one under way, held while a mini-boss or the boss is near
 * (IsQuietTime) and none once the boss is up.
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
 * A goblin or a bounty is under way until it dies or gets away
 * (m_miniEventActive); the addon shows its time left
 * ("eventtimer~label~seconds~total", 0 seconds hides it).
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "Utilities/Random.h"

#include <cstring>

namespace
{
    uint32 const BONK_MINI_EVENT_INTERVAL_MS = 2 * MINUTE * IN_MILLISECONDS;
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
    char const* const BONK_GOBLIN_ICON       = "INV_Misc_Coin_01";

    uint32 const BONK_BOUNTY_SECONDS         = 60;
    float const BONK_BOUNTY_MIN_DISTANCE     = 35.0f;
    float const BONK_BOUNTY_MAX_DISTANCE     = 50.0f;
    float const BONK_BOUNTY_HEALTH           = 2.0f;    // times its elite's
    float const BONK_BOUNTY_SCALE            = 1.25f;
    char const* const BONK_BOUNTY_ICON       = "INV_Misc_Bone_HumanSkull_01";

    uint32 const BONK_SPELL_SPAWN_SMOKE      = 10389;   // a goblin or bounty getting away

    char const* const MINI_EVENT_NAMES[MAX_BONK_MINI_EVENT] = { "horde", "goblin", "bounty" };

    // The difficulty's step, 0 to 2, from the elites' affixes (1 to 3).
    uint32 GetDifficultyStep(BonkTier const& tier)
    {
        return std::min(2u, tier.eliteAffixes ? tier.eliteAffixes - 1 : 0u);
    }
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
    // A goblin or a bounty runs until it dies or gets away.
    if (m_miniEventActive)
    {
        UpdateMiniEventTarget(diff);
        return;
    }

    if (!m_nextMiniEventMs)
        m_nextMiniEventMs = BONK_MINI_EVENT_INTERVAL_MS;
    if (m_elapsedMs < m_nextMiniEventMs || m_bossSpawned || IsQuietTime())
        return;

    if (StartMiniEvent(PickMiniEvent()))
        m_nextMiniEventMs = m_elapsedMs + BONK_MINI_EVENT_INTERVAL_MS;
    else
        m_nextMiniEventMs = m_elapsedMs + BONK_MINI_EVENT_RETRY_MS;
}

BonkMiniEventType BonkRun::PickMiniEvent() const
{
    std::vector<BonkMiniEventType> choices;
    for (uint8 i = 0; i < MAX_BONK_MINI_EVENT; ++i)
        if (BonkMiniEventType(i) != m_lastMiniEvent || MAX_BONK_MINI_EVENT == 1)
            choices.push_back(BonkMiniEventType(i));
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
    uint32 const step = GetDifficultyStep(m_tier);
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

    uint32 const step = GetDifficultyStep(m_tier);
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
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->SetFleeing(step >= 1);

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

    StartMiniEventTarget(mob, "Treasure Goblin", BONK_GOBLIN_SECONDS);
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
    BonkMobAI::SetNextAffixCount(int32(GetDifficultyStep(m_tier)));
    BonkMobAI::SetNextPlateIcon(BONK_BOUNTY_ICON);
    Creature* mob = SpawnMobAt(bounty, x, y, z);
    if (!mob)
        return false;

    std::string const name = mob->GetName();
    StartMiniEventTarget(mob, "Bounty: " + name, BONK_BOUNTY_SECONDS);
    m_player->GetBonkPlayer()->Notify("Bounty: " + name + "! Kill it within " + std::to_string(BONK_BOUNTY_SECONDS) + " seconds for a chest.");
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
    if (!m_miniEventActive || mob->GetObjectGuid() != m_miniEvent.target)
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
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
    bool const timed = !m_miniEvent.target.IsEmpty();
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
        EndMiniEvent();
    }
    if (!StartMiniEvent(type))
    {
        error = "It could not start here (no room for more enemies, or no ground around you).";
        return false;
    }
    m_nextMiniEventMs = m_elapsedMs + BONK_MINI_EVENT_INTERVAL_MS;
    return true;
}
