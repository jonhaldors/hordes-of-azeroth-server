/*
 * Vanilla Bonk: `.bonk stress`, the GM stress test (docs/Spells.md, Testing).
 *
 * On, for the rest of the run: the upgrades tree at its max ranks, Haste and
 * Area as from their passives at max rank, Count at its cap, and every spell
 * held brought to rank 16 by Epic ranks (evolving where it can). The crowd is
 * kept at the tier's cap, refilled quickly; kills give experience as usual,
 * so level-ups keep coming. Every 10 seconds of running time the player is
 * told, and Perf.log gets, how long server ticks took, how much of them the
 * run's own update took, and what is on the ground. An enemy held (stunned,
 * rooted, feared or confused) for BONK_STRESS_HOLD_MS straight is reported:
 * holds should never be permanent. Off: a summary, and the stat boosts go
 * (spell ranks stay).
 */

#include "BonkRun.h"
#include "BonkPlayer.h"
#include "Chat.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace
{
    uint32 const BONK_STRESS_RANK            = 16;      // spells held are brought to this rank
    uint32 const BONK_STRESS_REPORT_MS       = 10000;   // running time between reports
    uint32 const BONK_STRESS_HOLD_CHECK_MS   = 1000;
    uint32 const BONK_STRESS_HOLD_MS         = 10000;   // held this long straight: reported (holds last at most 2 sec)
    uint32 const BONK_STRESS_SLOW_TICK_MS    = 50;      // twice the 25 ms tick

    uint64 NowUs()
    {
        return uint64(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
}

void BonkRun::StressTicks::Add(uint32 tickMs, uint32 usedUs)
{
    ++count;
    totalMs += tickMs;
    worstMs = std::max(worstMs, tickMs);
    if (tickMs > BONK_STRESS_SLOW_TICK_MS)
        ++slow;
    runUs += usedUs;
    worstRunUs = std::max(worstRunUs, usedUs);
}

BonkRun::StressTick::StressTick(BonkRun& run, uint32 diff) : m_run(run), m_diff(diff), m_startUs(run.m_stress.on ? NowUs() : 0)
{
}

BonkRun::StressTick::~StressTick()
{
    if (!m_run.m_stress.on)
        return;

    // The run is over: the summary, once.
    if (m_run.m_state == STATE_ENDING || m_run.m_state == STATE_FINISHED)
    {
        m_run.StopStress();
        return;
    }

    // Only fighting counts: not a level-up's pause, a portal or a stage change.
    if (m_run.m_state != STATE_RUNNING || m_run.m_paused)
        return;

    uint32 const usedUs = uint32(std::min<uint64>(NowUs() - m_startUs, 0xFFFFFFFFull));
    m_run.m_stress.window.Add(m_diff, usedUs);
    m_run.m_stress.total.Add(m_diff, usedUs);
    m_run.UpdateStress();
}

void BonkRun::ToggleStress()
{
    if (m_stress.on)
        StopStress();
    else
        StartStress();
}

void BonkRun::StartStress()
{
    m_stress = Stress();
    m_stress.on = true;

    // The upgrades tree at its max ranks, on top of what is bought.
    BonkPlayer const* bonk = m_player->GetBonkPlayer();
    for (auto const& itr : sBonkMgr.GetUpgrades())
    {
        BonkUpgrade const& upgrade = itr.second;
        uint8 const owned = std::min(bonk->GetUpgradeRank(upgrade.id), upgrade.maxRank);
        m_stress.bonuses[upgrade.effect] += upgrade.valuePerRank * (upgrade.maxRank - owned);
    }

    // Haste and Area as from their passives at max rank, Count at its cap.
    for (BonkPassiveEffect effect : { BONK_PASSIVE_HASTE, BONK_PASSIVE_AREA })
    {
        float best = 0.0f;
        for (auto const& itr : sBonkMgr.GetPassives())
            if (itr.second.effect == effect)
                best = std::max(best, itr.second.valuePerRank * itr.second.maxRank);
        m_stress.bonuses[effect] += best;
    }
    m_stress.bonuses[BONK_PASSIVE_AMOUNT] += BONK_MAX_COUNT;

    // Spells held to rank 16 by Epic ranks; a spell that can evolve does at
    // its first chance.
    for (OwnedWeapon& owned : m_weapons)
    {
        while (owned.rank < BONK_STRESS_RANK)
        {
            Offer offer;
            offer.id = owned.weapon.id;
            offer.newLevel = owned.rank + 1;
            if (GetLegendaryChance(owned) > 0.0f)
            {
                offer.rarity = BONK_RARITY_LEGENDARY;
                offer.step = BONK_STEP_EVOLVE;
            }
            else
            {
                offer.rarity = BONK_RARITY_EPIC;
                offer.step = BONK_STEP_RANK;
                RollUpgrades(owned, offer);
            }
            ApplyStep(owned, offer);
        }
    }

    ApplyPassives();
    SendLoadout();

    m_stress.startMs = m_stress.reportMs = m_stress.holdCheckMs = m_totalMs;

    ChatHandler(m_player).PSendSysMessage("Stress test on: upgrades tree maxed, +%g%% Haste, +%g%% Area, Count %g, %u spells at rank %u; "
        "the crowd stays at %u. A report every %u seconds (also in Perf.log); .bonk stress again to stop.",
        m_stress.bonuses[BONK_PASSIVE_HASTE], m_stress.bonuses[BONK_PASSIVE_AREA], GetStat(BONK_PASSIVE_AMOUNT),
        uint32(m_weapons.size()), BONK_STRESS_RANK, m_tier.maxAlive, BONK_STRESS_REPORT_MS / IN_MILLISECONDS);
    sLog.Out(LOG_PERFORMANCE, LOG_LVL_BASIC, "Stress test: %s starts on %s %s, stage %u, level %u, class %u",
        m_player->GetName(), m_map.key.c_str(), m_tier.key.c_str(), m_stage, m_player->GetLevel(), uint32(m_player->GetClass()));
}

void BonkRun::StopStress()
{
    if (!m_stress.on)
        return;

    ReportStress(m_stress.total, true);
    m_stress = Stress();
    ApplyPassives();
}

void BonkRun::UpdateStress()
{
    if (m_totalMs - m_stress.holdCheckMs >= BONK_STRESS_HOLD_CHECK_MS)
    {
        m_stress.holdCheckMs = m_totalMs;
        CheckStressHolds();
    }

    if (m_totalMs - m_stress.reportMs >= BONK_STRESS_REPORT_MS)
    {
        m_stress.reportMs = m_totalMs;
        ReportStress(m_stress.window, false);
        m_stress.window = StressTicks();
    }
}

void BonkRun::CheckStressHolds()
{
    // In running time, so a level-up's pause (which freezes auras too)
    // doesn't count.
    std::map<ObjectGuid, uint32> stillHeld;
    m_stress.alive = 0;
    m_stress.held = 0;
    for (ObjectGuid const& guid : m_mobs)
    {
        Creature* mob = m_player->GetMap()->GetCreature(guid);
        if (!mob || !mob->IsAlive())
            continue;
        ++m_stress.alive;

        uint32 spellId = 0;
        for (AuraType type : { SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE })
        {
            Unit::AuraList const& auras = mob->GetAurasByType(type);
            if (!auras.empty())
            {
                spellId = auras.front()->GetId();
                break;
            }
        }
        if (!spellId)
            continue;
        ++m_stress.held;

        auto itr = m_stress.heldSince.find(guid);
        uint32 const since = itr != m_stress.heldSince.end() ? itr->second : m_totalMs;
        stillHeld[guid] = since;
        if (m_totalMs - since < BONK_STRESS_HOLD_MS || !m_stress.heldReported.insert(guid).second)
            continue;

        SpellEntry const* spell = sSpellMgr.GetSpellEntry(spellId);
        std::string const name = spell ? std::string(spell->SpellName[0]) : "?";
        ChatHandler(m_player).PSendSysMessage("Stress: %s has been held for %u seconds straight, now by %s (%u). Holds should never be permanent.",
            mob->GetName(), (m_totalMs - since) / IN_MILLISECONDS, name.c_str(), spellId);
        sLog.Out(LOG_PERFORMANCE, LOG_LVL_BASIC, "Stress test: %s held for %u seconds straight, now by %s (%u)",
            mob->GetName(), (m_totalMs - since) / IN_MILLISECONDS, name.c_str(), spellId);
    }
    m_stress.heldSince.swap(stillHeld);
}

void BonkRun::ReportStress(StressTicks const& ticks, bool summary) const
{
    uint32 effects = uint32(m_pendingCasts.size() + m_projectiles.size() + m_traps.size() + m_weaponOrbs.size());
    for (OwnedWeapon const& owned : m_weapons)
        effects += uint32(owned.pets.size());

    uint32 const seconds = (m_totalMs - m_stress.startMs) / IN_MILLISECONDS;
    float const tickAvg = ticks.count ? ticks.totalMs / float(ticks.count) : 0.0f;
    float const runAvg = ticks.count ? ticks.runUs / 1000.0f / ticks.count : 0.0f;

    char line[512];
    if (summary)
        snprintf(line, sizeof(line), "Stress summary, %u:%02u of fighting: %u ticks, avg %.1f ms, worst %u ms, %u over %u ms; "
            "run code avg %.1f ms, worst %.1f ms; %u enemies held past %u seconds; level %u",
            seconds / MINUTE, seconds % MINUTE, ticks.count, tickAvg, ticks.worstMs, ticks.slow, BONK_STRESS_SLOW_TICK_MS,
            runAvg, ticks.worstRunUs / 1000.0f, uint32(m_stress.heldReported.size()), BONK_STRESS_HOLD_MS / IN_MILLISECONDS,
            m_player->GetLevel());
    else
        snprintf(line, sizeof(line), "Stress %u:%02u: tick avg %.1f ms, worst %u ms, %u over %u ms; run code avg %.1f ms, worst %.1f ms; "
            "%u enemies (%u held), %u orbs, %u spell effects; level %u",
            seconds / MINUTE, seconds % MINUTE, tickAvg, ticks.worstMs, ticks.slow, BONK_STRESS_SLOW_TICK_MS,
            runAvg, ticks.worstRunUs / 1000.0f, m_stress.alive, m_stress.held, uint32(m_orbs.size()), effects, m_player->GetLevel());

    ChatHandler(m_player).SendSysMessage(line);
    sLog.Out(LOG_PERFORMANCE, LOG_LVL_BASIC, "%s: %s", m_player->GetName(), line);
}
