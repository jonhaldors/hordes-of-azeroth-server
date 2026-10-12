/*
 * Vanilla Bonk: Class Mastery.
 */

#include "BonkMastery.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Policies/SingletonImp.h"

#include <algorithm>

INSTANTIATE_SINGLETON_1(BonkMastery);

void BonkMastery::Load()
{
    m_trees.clear();
    m_nodes.clear();

    std::unique_ptr<QueryResult> result(WorldDatabase.Query("SELECT `id`, `class`, `name`, `background`, `order` FROM `bonk_mastery_tree`"));
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BonkMasteryTree tree;
            tree.id = fields[0].GetUInt32();
            tree.classId = fields[1].GetUInt8();
            tree.name = fields[2].GetCppString();
            tree.background = fields[3].GetCppString();
            tree.order = fields[4].GetUInt8();
            m_trees[tree.id] = std::move(tree);
        } while (result->NextRow());
    }

    result = WorldDatabase.Query("SELECT `id`, `class`, `tree`, `row`, `column`, `icon`, `name`, `description`, `max_rank`, "
        "`required_id`, `required_rank`, `effect`, `target`, `value_per_rank` FROM `bonk_mastery`");
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            BonkMasteryNode node;
            node.id = fields[0].GetUInt32();
            node.classId = fields[1].GetUInt8();
            node.tree = fields[2].GetUInt32();
            node.row = fields[3].GetUInt8();
            node.column = fields[4].GetUInt8();
            node.icon = fields[5].GetCppString();
            node.name = fields[6].GetCppString();
            node.description = fields[7].GetCppString();
            node.maxRank = std::max<uint8>(1, fields[8].GetUInt8());
            node.requiredId = fields[9].GetUInt32();
            node.requiredRank = fields[10].GetUInt8();
            uint8 const effect = fields[11].GetUInt8();
            node.target = fields[12].GetUInt32();
            node.valuePerRank = fields[13].GetFloat();

            auto tree = m_trees.find(node.tree);
            if (tree == m_trees.end() || tree->second.classId != node.classId)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_mastery`: node %u is in tree %u, which is missing or of another class; skipped.", node.id, node.tree);
                continue;
            }
            if (effect >= MAX_BONK_MASTERY_EFFECT)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_mastery`: node %u has unknown effect %u, skipped.", node.id, effect);
                continue;
            }
            node.effect = BonkMasteryEffect(effect);

            bool targetOk = true;
            switch (node.effect)
            {
                case BONK_MASTERY_PASSIVE:
                    targetOk = node.target < MAX_BONK_PASSIVE_EFFECT;
                    break;
                case BONK_MASTERY_SPELL_DAMAGE:
                case BONK_MASTERY_SPELL_COOLDOWN:
                case BONK_MASTERY_SPELL_TARGETS:
                case BONK_MASTERY_START_SPELL:
                    targetOk = sBonkMgr.GetWeapon(node.target) != nullptr;
                    break;
                default:
                    break;
            }
            if (!targetOk)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_mastery`: node %u has unknown target %u, skipped.", node.id, node.target);
                continue;
            }

            if (node.row < 1 || node.row > BONK_SHOP_ROWS || node.column < 1 || node.column > BONK_SHOP_COLUMNS)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_mastery`: node %u sits outside its tree (row %u, column %u), skipped.",
                    node.id, uint32(node.row), uint32(node.column));
                continue;
            }

            m_nodes[node.id] = std::move(node);
        } while (result->NextRow());
    }

    // Requirements must be another node of the same class.
    for (auto& itr : m_nodes)
    {
        BonkMasteryNode& node = itr.second;
        if (!node.requiredId)
            continue;
        auto required = m_nodes.find(node.requiredId);
        if (required == m_nodes.end() || required->second.classId != node.classId)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_mastery`: node %u requires %u, which is missing or of another class; no requirement.",
                node.id, node.requiredId);
            node.requiredId = 0;
            node.requiredRank = 0;
        }
        else
            node.requiredRank = std::min(std::max<uint8>(1, node.requiredRank), required->second.maxRank);
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk mastery nodes in %u trees", uint32(m_nodes.size()), uint32(m_trees.size()));
}

std::vector<BonkMasteryTree const*> BonkMastery::GetTrees(uint8 classId) const
{
    std::vector<BonkMasteryTree const*> trees;
    for (auto const& itr : m_trees)
        if (itr.second.classId == classId)
            trees.push_back(&itr.second);
    std::sort(trees.begin(), trees.end(), [](BonkMasteryTree const* a, BonkMasteryTree const* b)
    {
        return a->order != b->order ? a->order < b->order : a->id < b->id;
    });
    return trees;
}

bool BonkMastery::HasTrees(uint8 classId) const
{
    for (auto const& itr : m_trees)
        if (itr.second.classId == classId)
            return true;
    return false;
}

BonkMasteryNode const* BonkMastery::GetNode(uint32 id) const
{
    auto itr = m_nodes.find(id);
    return itr != m_nodes.end() ? &itr->second : nullptr;
}

uint32 BonkMastery::GetXpForLevel(uint32 level)
{
    return BONK_MASTERY_XP_BASE + BONK_MASTERY_XP_STEP * (std::max(1u, level) - 1);
}

uint32 BonkMastery::GetLevel(uint32 xp)
{
    uint32 level = 0;
    while (level < BONK_MASTERY_MAX_LEVEL && xp >= GetXpForLevel(level + 1))
    {
        xp -= GetXpForLevel(level + 1);
        ++level;
    }
    return level;
}

uint32 BonkMastery::GetXpIntoLevel(uint32 xp)
{
    uint32 level = 0;
    while (level < BONK_MASTERY_MAX_LEVEL && xp >= GetXpForLevel(level + 1))
    {
        xp -= GetXpForLevel(level + 1);
        ++level;
    }
    return level < BONK_MASTERY_MAX_LEVEL ? xp : 0;
}

uint32 BonkMastery::GetRootWeapon(uint32 weaponId)
{
    BonkWeapon const* weapon = sBonkMgr.GetWeapon(weaponId);
    for (uint32 steps = 0; weapon && weapon->evolvesFrom && steps < 8; ++steps)
    {
        BonkWeapon const* from = sBonkMgr.GetWeapon(weapon->evolvesFrom);
        if (!from)
            break;
        weapon = from;
    }
    return weapon ? weapon->id : weaponId;
}

BonkMasteryBonuses BonkMastery::Evaluate(std::map<uint32, uint8> const& ranks) const
{
    BonkMasteryBonuses bonuses;
    for (auto const& itr : ranks)
    {
        BonkMasteryNode const* node = GetNode(itr.first);
        if (!node || !itr.second)
            continue;

        float const value = node->valuePerRank * std::min(itr.second, node->maxRank);
        switch (node->effect)
        {
            case BONK_MASTERY_PASSIVE:        bonuses.passives[node->target] += value; break;
            case BONK_MASTERY_SPELL_DAMAGE:   bonuses.spellDamage[node->target] += value; break;
            case BONK_MASTERY_SCHOOL_DAMAGE:  bonuses.schoolDamage.emplace_back(node->target, value); break;
            case BONK_MASTERY_SPELL_COOLDOWN: bonuses.spellCooldown[node->target] += value; break;
            case BONK_MASTERY_SPELL_TARGETS:  bonuses.spellTargets[node->target] += uint32(value); break;
            case BONK_MASTERY_START_SPELL:    bonuses.startSpells.push_back(node->target); break;
            case BONK_MASTERY_SHATTER:        bonuses.shatter += value; break;
            case BONK_MASTERY_OPENER:         bonuses.opener.emplace_back(node->target, value); break;
            case BONK_MASTERY_EXECUTE:        bonuses.execute.emplace_back(node->target, value); break;
            case BONK_MASTERY_CLEARCASTING:   bonuses.clearcasting += value; break;
            case BONK_MASTERY_SOUL_SIPHON:    bonuses.soulSiphon.emplace_back(node->target, value); break;
            default: break;
        }
    }
    return bonuses;
}
