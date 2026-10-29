#include "BonkEnchant.h"
#include "BonkMgr.h"
#include "BonkPlayer.h"
#include "Database/DatabaseEnv.h"
#include "Player.h"
#include "Policies/SingletonImp.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

INSTANTIATE_SINGLETON_1(BonkEnchantStore);

namespace
{
    uint8 const BONK_ENCHANT_SLOTS[] = { EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_SHOULDERS, EQUIPMENT_SLOT_CHEST, EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_HANDS };
    float const BONK_ENCHANTER_RANGE = 10.0f;
    uint32 const BONK_ENCHANT_BASE_GOLD = 50;
    float const BONK_ENCHANT_GOLD_PER_TIER_SQUARED = 2.5f;
    uint32 const BONK_COPPER_PER_GOLD = 10000;
    float const BONK_ENCHANT_TIER_GROWTH = 1.12f;

    struct BonkEnchantGrade
    {
        char const* name;
        uint32 weight;
        float multiplier;
    };

    BonkEnchantGrade const BONK_ENCHANT_GRADES[] =
    {
        { "Minor",    50, 1.0f },
        { "Lesser",   30, 1.5f },
        { "Greater",  14, 2.2f },
        { "Superior",  5, 3.2f },
        { "Godly",     1, 5.0f },
    };
    uint8 const BONK_ENCHANT_GRADE_COUNT = uint8(sizeof(BONK_ENCHANT_GRADES) / sizeof(BONK_ENCHANT_GRADES[0]));

    struct BonkEnchantStat
    {
        BonkPassiveEffect effect;
        float base;
        bool grows;
    };

    BonkEnchantStat const BONK_ENCHANT_STATS[] =
    {
        { BONK_PASSIVE_POWER,       1.0f,  true },
        { BONK_PASSIVE_MAX_HEALTH,  1.15f, true },
        { BONK_PASSIVE_CRIT,        0.5f,  false },
        { BONK_PASSIVE_CRIT_DAMAGE, 1.0f,  false },
        { BONK_PASSIVE_DODGE,       0.4f,  false },
        { BONK_PASSIVE_SHIELD,      0.9f,  false },
    };

    BonkEnchantStat const* FindStat(uint8 effect)
    {
        for (BonkEnchantStat const& stat : BONK_ENCHANT_STATS)
            if (stat.effect == effect)
                return &stat;
        return nullptr;
    }

    uint8 RollGrade()
    {
        uint32 total = 0;
        for (BonkEnchantGrade const& grade : BONK_ENCHANT_GRADES)
            total += grade.weight;
        uint32 roll = urand(1, total);
        for (uint8 index = 0; index < BONK_ENCHANT_GRADE_COUNT; ++index)
        {
            if (roll <= BONK_ENCHANT_GRADES[index].weight)
                return index;
            roll -= BONK_ENCHANT_GRADES[index].weight;
        }
        return 0;
    }

    uint32 GetEnchantTier(Player const* player)
    {
        return player->GetBonkPlayer()->GetMythicFloorsCleared();
    }

    std::string FormatValue(float value)
    {
        char text[32];
        snprintf(text, sizeof(text), "%.1f", value);
        return text;
    }

    std::string DescribeEnchant(BonkEnchant const& enchant)
    {
        if (!enchant.IsSet())
            return std::string(std::to_string(BONK_ENCHANT_NONE)) + "~0~0";
        return std::to_string(enchant.effect) + "~" + std::to_string(enchant.grade) + "~" + FormatValue(BonkEnchanting::GetValue(enchant));
    }
}

bool BonkEnchanting::IsEnchantSlot(uint8 slot)
{
    return std::find(std::begin(BONK_ENCHANT_SLOTS), std::end(BONK_ENCHANT_SLOTS), slot) != std::end(BONK_ENCHANT_SLOTS);
}

uint32 BonkEnchanting::GetRollCost(Player const* player)
{
    float const tier = float(GetEnchantTier(player));
    return uint32((BONK_ENCHANT_BASE_GOLD + BONK_ENCHANT_GOLD_PER_TIER_SQUARED * tier * tier) * BONK_COPPER_PER_GOLD);
}

float BonkEnchanting::GetValue(BonkEnchant const& enchant)
{
    BonkEnchantStat const* stat = FindStat(enchant.effect);
    if (!stat || enchant.grade >= BONK_ENCHANT_GRADE_COUNT)
        return 0.0f;
    float value = stat->base * BONK_ENCHANT_GRADES[enchant.grade].multiplier;
    if (stat->grows)
        value *= std::pow(BONK_ENCHANT_TIER_GROWTH, float(std::max(1u, enchant.tier) - 1));
    return std::round(value * 10.0f) / 10.0f;
}

BonkEnchantStore::SlotEnchants& BonkEnchantStore::LoadLocked(uint32 guid)
{
    auto itr = m_enchants.find(guid);
    if (itr != m_enchants.end())
        return itr->second;
    SlotEnchants& slots = m_enchants[guid];
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery(
        "SELECT `slot`, `effect`, `grade`, `tier`, `rolls` FROM `bonk_character_enchant` WHERE `guid` = %u", guid));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BonkEnchant enchant;
            enchant.effect = fields[1].GetUInt8();
            enchant.grade = fields[2].GetUInt8();
            enchant.tier = fields[3].GetUInt32();
            enchant.rolls = fields[4].GetUInt32();
            if (!FindStat(enchant.effect))
                enchant.effect = BONK_ENCHANT_NONE;
            slots[fields[0].GetUInt8()] = enchant;
        } while (result->NextRow());
    }
    return slots;
}

void BonkEnchantStore::SaveLocked(uint32 guid, uint8 slot, BonkEnchant const& enchant)
{
    CharacterDatabase.PExecute("REPLACE INTO `bonk_character_enchant` (`guid`, `slot`, `effect`, `grade`, `tier`, `rolls`) VALUES (%u, %u, %u, %u, %u, %u)",
        guid, uint32(slot), uint32(enchant.effect), uint32(enchant.grade), enchant.tier, enchant.rolls);
}

void BonkEnchantStore::Open(Player* player)
{
    Send(player);
    player->GetBonkPlayer()->SendAddon("enchantopen");
}

bool BonkEnchantStore::Roll(Player* player, uint8 slot, std::string& error)
{
    if (!player->FindNearestCreature(BONK_ENCHANTER_ENTRY, BONK_ENCHANTER_RANGE))
    {
        error = "Talk to the enchanter at the Inn to enchant your gear.";
        return false;
    }
    if (!BonkEnchanting::IsEnchantSlot(slot))
    {
        error = "That slot can't be enchanted.";
        return false;
    }
    uint32 const cost = BonkEnchanting::GetRollCost(player);
    {
        std::lock_guard<std::mutex> guard(m_lock);
        uint32 const guid = player->GetGUIDLow();
        if (m_pending.count(guid))
        {
            error = "Keep or discard your last enchant first.";
            return false;
        }
        BonkEnchant& current = LoadLocked(guid)[slot];
        bool const free = current.rolls == 0;
        if (!free && player->GetMoney() < cost)
        {
            error = "An enchant costs " + FormatBonkMoney(cost) + ".";
            return false;
        }
        if (!free)
            player->ModifyMoney(-int32(cost));
        ++current.rolls;
        SaveLocked(guid, slot, current);

        PendingRoll pending;
        pending.slot = slot;
        pending.enchant.effect = BONK_ENCHANT_STATS[urand(0, uint32(sizeof(BONK_ENCHANT_STATS) / sizeof(BONK_ENCHANT_STATS[0]) - 1))].effect;
        pending.enchant.grade = RollGrade();
        pending.enchant.tier = std::max(1u, GetEnchantTier(player));
        pending.enchant.rolls = current.rolls;
        m_pending[guid] = pending;
    }
    Send(player);
    return true;
}

bool BonkEnchantStore::Keep(Player* player, std::string& error)
{
    {
        std::lock_guard<std::mutex> guard(m_lock);
        uint32 const guid = player->GetGUIDLow();
        auto itr = m_pending.find(guid);
        if (itr == m_pending.end())
        {
            error = "There is no new enchant to keep.";
            return false;
        }
        BonkEnchant& current = LoadLocked(guid)[itr->second.slot];
        uint32 const rolls = current.rolls;
        current = itr->second.enchant;
        current.rolls = rolls;
        SaveLocked(guid, itr->second.slot, current);
        player->GetBonkPlayer()->Notify(std::string(BONK_ENCHANT_GRADES[current.grade].name) + " enchant kept.");
        m_pending.erase(itr);
    }
    Send(player);
    return true;
}

void BonkEnchantStore::Discard(Player* player)
{
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_pending.erase(player->GetGUIDLow());
    }
    Send(player);
}

void BonkEnchantStore::AddStats(Player const* player, float* passives)
{
    std::lock_guard<std::mutex> guard(m_lock);
    for (auto const& itr : LoadLocked(player->GetGUIDLow()))
        if (itr.second.IsSet())
            passives[itr.second.effect] += BonkEnchanting::GetValue(itr.second);
}

void BonkEnchantStore::Send(Player const* player)
{
    BonkPlayer* bonk = player->GetBonkPlayer();
    if (!bonk || !bonk->IsAddonReady())
        return;
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        uint32 const guid = player->GetGUIDLow();
        SlotEnchants& slots = LoadLocked(guid);
        lines.push_back("enchantinfo~" + std::to_string(BonkEnchanting::GetRollCost(player)));
        for (uint8 slot : BONK_ENCHANT_SLOTS)
        {
            BonkEnchant const& enchant = slots[slot];
            lines.push_back("enchantslot~" + std::to_string(slot) + "~" + DescribeEnchant(enchant) + "~" + (enchant.rolls ? "0" : "1"));
        }
        auto pending = m_pending.find(guid);
        if (pending != m_pending.end())
            lines.push_back("enchantpending~" + std::to_string(pending->second.slot) + "~" + DescribeEnchant(pending->second.enchant));
        lines.push_back("enchantend");
    }
    bonk->SendAddon("enchantclear");
    for (std::string const& line : lines)
        bonk->SendAddon(line);
}
