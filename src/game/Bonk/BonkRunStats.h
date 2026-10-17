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

    std::map<uint32, Spell> bySpell;
    std::map<uint32, std::map<uint32, Spell>> dotBySpell;
    uint64 damageTaken = 0;
    uint32 kindKills[MAX_BONK_SPAWN_KIND] = {};
    std::map<ObjectGuid, uint32> killingSpells;
    std::map<ObjectGuid, uint32> killingDots;
};

struct BonkSummaryWeapon
{
    uint32 spellId = 0;
    std::vector<uint32> spellIds;
    std::string name;
    std::string icon;
    uint32 level = 1;
    uint32 maxLevel = 1;
    bool evolved = false;
};

namespace BonkStats
{
    void Reset(ObjectGuid player);
    uint32 CountKill(ObjectGuid player, ObjectGuid enemy, BonkSpawnKind kind);
    BonkRunStats::Spell GetSpellStats(ObjectGuid player, uint32 spellId);
    std::string DescribeDamageTypes(ObjectGuid player, uint32 spellId);
    std::vector<uint32> GetWeaponSpells(BonkWeapon const& weapon);
    void SendSummary(Player* player, std::vector<BonkSummaryWeapon> const& weapons);

    void UpdateBossBar(Player* player, uint32 diff);
}

void BonkOnDamage(Unit* attacker, Unit* victim, uint32 damage, SpellEntry const* spellProto);

#endif
