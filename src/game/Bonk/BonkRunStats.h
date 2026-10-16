/*
 * Vanilla Bonk: what a run did, for the results screen and quests.
 *
 * Unit::DealDamage reports every hit a player in a run deals or takes
 * (BonkOnDamage); the run asks for the killing blow when an enemy dies and
 * sends the summary when it ends. The boss bar lives here too: the run's
 * stage boss or nearest mini-boss, sent to the addon a few times a second.
 */

#ifndef MANGOS_BONKRUNSTATS_H
#define MANGOS_BONKRUNSTATS_H

#include "Common.h"
#include "BonkMgr.h"
#include "ObjectGuid.h"

#include <map>
#include <string>
#include <vector>

class Player;
class Unit;
class SpellEntry;

struct BonkRunStats
{
    struct Spell
    {
        uint64 damage = 0;
        uint32 kills = 0;
    };

    std::map<uint32, Spell> bySpell;            // spell id (0 = melee) -> damage and killing blows
    // spell id -> the damage over time it put on (the keyword's spell id) ->
    // that part of its damage and killing blows (already in bySpell too)
    std::map<uint32, std::map<uint32, Spell>> dotBySpell;
    uint64 damageTaken = 0;
    uint32 kindKills[MAX_BONK_SPAWN_KIND] = {};
    std::map<ObjectGuid, uint32> killingSpells; // enemy -> spell of the blow that killed it, until counted
    std::map<ObjectGuid, uint32> killingDots;   // enemy -> keyword spell, when that blow was its damage over time
};

// A weapon the run owned, for the results screen.
struct BonkSummaryWeapon
{
    uint32 spellId = 0;
    std::vector<uint32> spellIds;   // every spell its damage comes from (BonkStats::GetWeaponSpells)
    std::string name;
    std::string icon;
    uint32 level = 1;
    uint32 maxLevel = 1;
    bool evolved = false;
};

namespace BonkStats
{
    void Reset(ObjectGuid player);
    // Counts an enemy's death; returns the spell that dealt the killing blow (0 = unknown or melee).
    uint32 CountKill(ObjectGuid player, ObjectGuid enemy, BonkSpawnKind kind);
    // What one spell has done this run so far.
    BonkRunStats::Spell GetSpellStats(ObjectGuid player, uint32 spellId);
    // The spells a weapon deals damage with: its own, its alt spell and the
    // spells either of them triggers.
    std::vector<uint32> GetWeaponSpells(BonkWeapon const& weapon);
    // sum~damageTaken~elites~minibosses~bosses, sumw~icon~name~level~max~evolved~damage~kills
    // (each followed by sums~icon~name~damage~kills for the damage over time
    // in it)..., sumend
    void SendSummary(Player* player, std::vector<BonkSummaryWeapon> const& weapons);

    // boss~name~healthPercent~kind, or boss~ when no boss or mini-boss is up.
    void UpdateBossBar(Player* player, uint32 diff);
}

// Called by Unit::DealDamage for every hit.
void BonkOnDamage(Unit* attacker, Unit* victim, uint32 damage, SpellEntry const* spellProto);

#endif
