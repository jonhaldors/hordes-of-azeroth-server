/*
 * Vanilla Bonk: run statistics and the boss bar.
 */

#include "BonkRunStats.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "SpellEntry.h"
#include "SpellMgr.h"
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
    auto dot = stats.killingDots.find(enemy);
    if (dot != stats.killingDots.end())
    {
        ++stats.dotBySpell[spellId][dot->second].kills;
        stats.killingDots.erase(dot);
    }
    return spellId;
}

BonkRunStats::Spell BonkStats::GetSpellStats(ObjectGuid player, uint32 spellId)
{
    std::lock_guard<std::mutex> guard(s_lock);
    auto stats = s_stats.find(player);
    if (stats == s_stats.end())
        return BonkRunStats::Spell();
    auto spell = stats->second.bySpell.find(spellId);
    return spell != stats->second.bySpell.end() ? spell->second : BonkRunStats::Spell();
}

std::vector<uint32> BonkStats::GetWeaponSpells(BonkWeapon const& weapon)
{
    std::vector<uint32> spellIds;
    for (uint32 spellId : { weapon.spellId, weapon.altSpellId })
    {
        SpellEntry const* spellInfo = spellId ? sSpellMgr.GetSpellEntry(spellId) : nullptr;
        if (!spellInfo)
            continue;
        spellIds.push_back(spellId);
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->EffectTriggerSpell[i])
                spellIds.push_back(spellInfo->EffectTriggerSpell[i]);
    }
    return spellIds;
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

    // Each weapon's row holds every spell it deals damage with; weapons
    // sharing a spell (an evolution and its base) share one row.
    std::set<uint32> sent;
    for (BonkSummaryWeapon const& weapon : weapons)
    {
        if (sent.count(weapon.spellId))
            continue;
        uint64 damage = 0;
        uint32 kills = 0;
        std::vector<uint32> const spellIds = weapon.spellIds.empty() ? std::vector<uint32>{ weapon.spellId } : weapon.spellIds;
        for (uint32 spellId : spellIds)
        {
            if (!sent.insert(spellId).second)
                continue;
            auto itr = stats.bySpell.find(spellId);
            if (itr == stats.bySpell.end())
                continue;
            damage += itr->second.damage;
            kills += itr->second.kills;
        }
        bonk->SendAddon("sumw~" + weapon.icon + "~" + weapon.name + "~" + std::to_string(weapon.level) + "~" + std::to_string(weapon.maxLevel) + "~" +
            (weapon.evolved ? "1" : "0") + "~" + std::to_string(damage) + "~" + std::to_string(kills));

        // The damage over time it put on, part of the row above.
        std::map<uint32, BonkRunStats::Spell> dots;
        for (uint32 spellId : spellIds)
        {
            auto itr = stats.dotBySpell.find(spellId);
            if (itr == stats.dotBySpell.end())
                continue;
            for (auto const& dot : itr->second)
            {
                dots[dot.first].damage += dot.second.damage;
                dots[dot.first].kills += dot.second.kills;
            }
        }
        for (auto const& dot : dots)
        {
            BonkKeywordInfo const* keyword = sBonkMgr.GetKeywordBySpell(dot.first);
            SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(dot.first);
            std::string const icon = keyword && !keyword->icon.empty() ? keyword->icon : "INV_Misc_QuestionMark";
            std::string const name = keyword ? keyword->name : spellInfo ? spellInfo->SpellName[0] : "Spell " + std::to_string(dot.first);
            bonk->SendAddon("sums~" + icon + "~" + name + "~" + std::to_string(dot.second.damage) + "~" + std::to_string(dot.second.kills));
        }
    }

    // The hero's own weapon swings, keywords (Burning, Bleed, Poison) by
    // their names, then any other spell by its name.
    for (auto const& itr : stats.bySpell)
    {
        if (sent.count(itr.first) || (!itr.second.damage && !itr.second.kills))
            continue;
        std::string icon = "INV_Misc_QuestionMark";
        std::string name = "Spell " + std::to_string(itr.first);
        if (!itr.first)
        {
            icon = "INV_Sword_04";
            name = "Melee";
        }
        else if (BonkKeywordInfo const* keyword = sBonkMgr.GetKeywordBySpell(itr.first))
        {
            icon = keyword->icon.empty() ? icon : keyword->icon;
            name = keyword->name;
        }
        else if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(itr.first))
            name = spellInfo->SpellName[0];
        bonk->SendAddon("sumw~" + icon + "~" + name + "~0~0~0~" + std::to_string(itr.second.damage) + "~" + std::to_string(itr.second.kills));
    }

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
            (ai->GetKind() == BONK_SPAWN_BOSS ? "boss" : "mini") + "~" + ai->GetAffixNames();
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

    // A hero's beast counts for the spell that called it.
    bool const swing = !spellProto;
    Player* owner = nullptr;
    if (attacker->IsCreature() && !victim->IsPlayer())
        if (Player* hero = attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
            if (uint32 const createdBy = attacker->GetUInt32Value(UNIT_CREATED_BY_SPELL))
            {
                owner = hero;
                spellProto = sSpellMgr.GetSpellEntry(createdBy);
            }

    if (Player* player = owner ? owner : attacker->ToPlayer())
    {
        if (!victim->IsCreature() || !InRun(player))
            return;

        // Damage over time counts for the spells that put its stacks on
        // (Serpent Sting's Poison, the cat's Bleed), shared by their damage;
        // a kill for the largest share.
        BonkRun* run = player->GetBonkPlayer()->GetRun();
        std::vector<std::pair<uint32, uint32>> credits;
        if (spellProto)
            run->CreditDamageOverTime(victim, spellProto->Id, dealt, credits);
        // The keyword's spell, when this was damage over time a spell put on.
        uint32 const dotSpellId = !credits.empty() ? spellProto->Id : 0;
        if (credits.empty())
            credits.emplace_back(spellProto ? spellProto->Id : 0, dealt);

        // Keywords: the spell's goes on the enemy; a killing blow notes its
        // damage over time stacks, to spread when it dies.
        if (damage >= health)
            run->OnKillingBlow(victim);
        else
            run->OnWeaponHit(victim, spellProto);
        // A frenzied pet's bite (a swing, never its own cleave) strikes on.
        if (owner && swing)
            run->OnPetBite(attacker->ToCreature(), victim);
        // The hero's own hits and spells (their projectiles too) set off
        // Darkmoon Cards; summons' bites and abilities never do.
        if (!owner || attacker->GetEntry() == BONK_PROJECTILE)
            run->OnHeroHit(victim, dealt, spellProto);

        std::lock_guard<std::mutex> guard(s_lock);
        BonkRunStats& stats = s_stats[player->GetObjectGuid()];
        for (auto const& credit : credits)
        {
            stats.bySpell[credit.first].damage += credit.second;
            if (dotSpellId && credit.first != dotSpellId)
                stats.dotBySpell[credit.first][dotSpellId].damage += credit.second;
        }
        if (damage >= health)
        {
            stats.killingSpells[victim->GetObjectGuid()] = credits.front().first;
            if (dotSpellId && credits.front().first != dotSpellId)
                stats.killingDots[victim->GetObjectGuid()] = dotSpellId;
        }
    }
    else if (Player* player = victim->ToPlayer())
    {
        if (!InRun(player))
            return;

        std::lock_guard<std::mutex> guard(s_lock);
        s_stats[player->GetObjectGuid()].damageTaken += dealt;
    }
}
