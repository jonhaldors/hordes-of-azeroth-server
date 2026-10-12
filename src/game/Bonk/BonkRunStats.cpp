/*
 * Vanilla Bonk: run statistics and the boss bar.
 */

#include "BonkRunStats.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "SpellEntry.h"
#include "TemporarySummon.h"

#include <list>
#include <mutex>
#include <set>

namespace
{
    uint32 const BONK_BOSS_BAR_MS    = 250;
    float const BONK_BOSS_BAR_RANGE  = 150.0f;

    struct BossBar
    {
        uint32 timer = 0;
        std::string lastLine;
    };

    // Map threads update players on different maps at once.
    std::mutex s_lock;
    std::map<ObjectGuid, BonkRunStats> s_stats;
    std::map<ObjectGuid, BossBar> s_bossBars;

    bool InRun(Player const* player)
    {
        return player->GetBonkPlayer()->GetRun() != nullptr;
    }
}

void BonkStats::Reset(ObjectGuid player)
{
    std::lock_guard<std::mutex> guard(s_lock);
    s_stats[player] = BonkRunStats();
    s_bossBars.erase(player);
}

uint32 BonkStats::CountKill(ObjectGuid player, ObjectGuid enemy, BonkSpawnKind kind)
{
    std::lock_guard<std::mutex> guard(s_lock);
    BonkRunStats& stats = s_stats[player];
    if (kind < MAX_BONK_SPAWN_KIND)
        ++stats.kindKills[kind];

    uint32 spellId = 0;
    auto itr = stats.killingSpells.find(enemy);
    if (itr != stats.killingSpells.end())
    {
        spellId = itr->second;
        stats.killingSpells.erase(itr);
    }
    ++stats.bySpell[spellId].kills;
    return spellId;
}

void BonkStats::SendSummary(Player* player, std::vector<BonkSummaryWeapon> const& weapons)
{
    BonkRunStats stats;
    {
        std::lock_guard<std::mutex> guard(s_lock);
        stats = s_stats[player->GetObjectGuid()];
    }

    BonkPlayer* bonk = player->GetBonkPlayer();
    bonk->SendAddon("sum~" + std::to_string(stats.damageTaken) + "~" + std::to_string(stats.kindKills[BONK_SPAWN_ELITE]) + "~" +
        std::to_string(stats.kindKills[BONK_SPAWN_MINIBOSS]) + "~" + std::to_string(stats.kindKills[BONK_SPAWN_BOSS]));

    // Weapons sharing a spell (an evolution and its base) share one row.
    std::set<uint32> sent;
    for (BonkSummaryWeapon const& weapon : weapons)
    {
        if (!sent.insert(weapon.spellId).second)
            continue;
        auto itr = stats.bySpell.find(weapon.spellId);
        uint64 const damage = itr != stats.bySpell.end() ? itr->second.damage : 0;
        uint32 const kills = itr != stats.bySpell.end() ? itr->second.kills : 0;
        bonk->SendAddon("sumw~" + weapon.icon + "~" + weapon.name + "~" + std::to_string(weapon.level) + "~" + std::to_string(weapon.maxLevel) + "~" +
            (weapon.evolved ? "1" : "0") + "~" + std::to_string(damage) + "~" + std::to_string(kills));
    }

    // Swings and anything else that was not a weapon.
    uint64 otherDamage = 0;
    uint32 otherKills = 0;
    for (auto const& itr : stats.bySpell)
    {
        if (sent.count(itr.first))
            continue;
        otherDamage += itr.second.damage;
        otherKills += itr.second.kills;
    }
    if (otherDamage || otherKills)
        bonk->SendAddon("sumw~INV_Sword_04~Other~0~0~0~" + std::to_string(otherDamage) + "~" + std::to_string(otherKills));

    bonk->SendAddon("sumend");
}

void BonkStats::UpdateBossBar(Player* player, uint32 diff)
{
    BossBar* bar;
    {
        std::lock_guard<std::mutex> guard(s_lock);
        bar = &s_bossBars[player->GetObjectGuid()];
    }
    bar->timer += diff;
    if (bar->timer < BONK_BOSS_BAR_MS)
        return;
    bar->timer = 0;

    // The stage boss first, else the nearest mini-boss.
    Creature* shown = nullptr;
    if (player->IsInWorld() && !player->IsBeingTeleported())
    {
        std::list<Creature*> creatures;
        MaNGOS::AnyUnitInObjectRangeCheck check(player, BONK_BOSS_BAR_RANGE);
        MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
        Cell::VisitGridObjects(player, searcher, BONK_BOSS_BAR_RANGE);

        float shownDistance = 0.0f;
        bool shownIsBoss = false;
        for (Creature* creature : creatures)
        {
            if (!creature->IsAlive() || !creature->IsTemporarySummon() ||
                static_cast<TemporarySummon*>(creature)->GetSummonerGuid() != player->GetObjectGuid())
                continue;
            BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(creature->AI());
            if (!ai || (ai->GetKind() != BONK_SPAWN_BOSS && ai->GetKind() != BONK_SPAWN_MINIBOSS))
                continue;

            bool const isBoss = ai->GetKind() == BONK_SPAWN_BOSS;
            float const distance = player->GetDistance(creature);
            if (!shown || (isBoss && !shownIsBoss) || (isBoss == shownIsBoss && distance < shownDistance))
            {
                shown = creature;
                shownDistance = distance;
                shownIsBoss = isBoss;
            }
        }
    }

    std::string line = "boss~";
    if (shown)
    {
        BonkMobAI const* ai = static_cast<BonkMobAI const*>(shown->AI());
        line += std::string(shown->GetName()) + "~" + std::to_string(shown->GetHealth()) + "~" + std::to_string(shown->GetMaxHealth()) + "~" +
            (ai->GetKind() == BONK_SPAWN_BOSS ? "boss" : "mini");
    }
    if (line == bar->lastLine)
        return;
    bar->lastLine = line;
    player->GetBonkPlayer()->SendAddon(line);
}

void BonkOnDamage(Unit* attacker, Unit* victim, uint32 damage, SpellEntry const* spellProto)
{
    if (!damage || attacker == victim)
        return;

    uint32 const health = victim->GetHealth();
    uint32 const dealt = std::min(damage, health);

    if (Player* player = attacker->ToPlayer())
    {
        if (!victim->IsCreature() || !InRun(player))
            return;

        uint32 const spellId = spellProto ? spellProto->Id : 0;
        std::lock_guard<std::mutex> guard(s_lock);
        BonkRunStats& stats = s_stats[player->GetObjectGuid()];
        stats.bySpell[spellId].damage += dealt;
        if (damage >= health)
            stats.killingSpells[victim->GetObjectGuid()] = spellId;
    }
    else if (Player* player = victim->ToPlayer())
    {
        if (!InRun(player))
            return;

        std::lock_guard<std::mutex> guard(s_lock);
        s_stats[player->GetObjectGuid()].damageTaken += dealt;
    }
}
