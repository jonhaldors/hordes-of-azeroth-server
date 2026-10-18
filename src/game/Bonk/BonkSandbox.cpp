#include "BonkSandbox.h"
#include "BonkMgr.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Chat.h"
#include "Creature.h"
#include "GossipDef.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace
{
    uint32 const BONK_SANDBOX_KEEPER = 900123;
    uint32 const BONK_SANDBOX_DUMMY  = 900124;
    uint32 const BONK_SANDBOX_MAP    = 3;
    float const BONK_SANDBOX_X       = 981.3f;
    float const BONK_SANDBOX_Y       = -2912.6f;
    float const BONK_SANDBOX_Z       = 91.7f;
    float const BONK_SANDBOX_O       = 2.94f;

    uint32 const BONK_TEXT_SANDBOX_CAMP   = 900012;
    uint32 const BONK_TEXT_SANDBOX_MENU   = 900013;
    uint32 const BONK_TEXT_SANDBOX_REFUSE = 900014;

    float const BONK_SANDBOX_KEEPER_BEHIND    = 3.0f;
    float const BONK_SANDBOX_FRONT_RANGE      = 15.0f;
    uint32 const BONK_SANDBOX_TICK_MS         = 500;
    uint32 const BONK_SANDBOX_SPAWNS_PER_TICK = 20;
    float const BONK_SANDBOX_ENEMY_DISTANCE   = 20.0f;
    uint32 const BONK_SANDBOX_CRYSTALS        = 100;
    float const BONK_SANDBOX_CRYSTAL_INNER    = 12.0f;
    float const BONK_SANDBOX_CRYSTAL_MARGIN   = 4.0f;
    float const BONK_SANDBOX_CRYSTAL_SPREAD   = 35.0f;
    uint32 const BONK_SANDBOX_PAGE            = 12;
    uint32 const BONK_SANDBOX_RANKS[]         = { 1, 5, 10, 15 };

    uint32 const BONK_DUMMY_REFILL_MS  = 3000;
    float const BONK_DUMMY_DISTANCE    = 12.0f;
    float const BONK_DUMMY_GRID_NEAR   = 10.0f;
    float const BONK_DUMMY_GRID_FAR    = 15.0f;
    float const BONK_DUMMY_SPACING     = 2.5f;
    uint32 const BONK_DUMMY_COLUMNS    = 5;
    float const BONK_DUMMY_ELITE_SCALE = 1.35f;
    float const BONK_DUMMY_BOSS_SCALE  = 1.8f;
    float const BONK_DEEP_DUMMY_HEALTH = 100.0f;
    float const BONK_DEEP_DUMMY_REFILL = 0.5f;
    uint32 const BONK_SANDBOX_DEFAULT_PACK = 30;

    enum SandboxSender : uint32
    {
        SENDER_OPEN = 1,
        SENDER_ENTER,
        SENDER_DUMMY,
        SENDER_PACK,
        SENDER_ENEMY,
        SENDER_TOGGLE,
        SENDER_LEVELS,
        SENDER_SPELL_ADD,
        SENDER_SPELL_REPLACE,
        SENDER_SPELL_RANK,
        SENDER_SPELL_REMOVE,
        SENDER_EVOLVE,
        SENDER_PASSIVE,
        SENDER_CARD,
        SENDER_RESET,
        SENDER_LEAVE,
        SENDER_SET_LEVEL,
    };

    enum SandboxMenu : uint32
    {
        MENU_MAIN,
        MENU_DUMMIES,
        MENU_ENEMIES,
        MENU_ENEMY_LIST,
        MENU_LEVELS,
        MENU_SPELLS,
        MENU_SPELL,
        MENU_PASSIVES,
        MENU_CARDS,
        MENU_RESET,
    };

    enum SandboxToggle : uint32
    {
        TOGGLE_PASSIVE = 1,
        TOGGLE_UNKILLABLE,
        TOGGLE_GOD,
    };

    enum SandboxReset : uint32
    {
        RESET_CLEAR = 1,
        RESET_HEAL,
        RESET_COOLDOWNS,
        RESET_DUMMIES,
        RESET_SPELLS,
    };

    enum SandboxEnemyType : uint32
    {
        ENEMY_CASTER,
        ENEMY_RANGED,
        ENEMY_CHARGER,
        ENEMY_ELITE,
        ENEMY_MINIBOSS,
        ENEMY_BOSS,
        MAX_ENEMY_TYPE
    };

    char const* const ENEMY_TYPE_LABELS[MAX_ENEMY_TYPE] = { "One caster", "One ranged enemy", "One charger", "One elite",
        "One mini-boss", "One boss" };

    struct SandboxEnemy
    {
        uint32 entry = 0;
        std::string label;
    };

    uint32 MenuAction(SandboxMenu menu, uint32 page = 0, uint32 context = 0)
    {
        return uint32(menu) | (page << 8) | (context << 16);
    }

    SandboxMenu MenuOf(uint32 action) { return SandboxMenu(action & 0xFF); }
    uint32 PageOf(uint32 action) { return (action >> 8) & 0xFF; }
    uint32 ContextOf(uint32 action) { return action >> 16; }

    uint32 DummyAction(uint32 count, BonkSpawnKind kind) { return (uint32(kind) << 8) | count; }
    uint32 PairAction(uint32 high, uint32 low) { return (high << 16) | (low & 0xFFFF); }

    void AddItem(Player* player, uint8 icon, std::string const& text, uint32 sender, uint32 action)
    {
        player->PlayerTalkClass->GetGossipMenu().AddMenuItem(icon, text.c_str(), sender, action);
    }

    void AddBack(Player* player, SandboxMenu menu, uint32 page = 0, uint32 context = 0)
    {
        AddItem(player, GOSSIP_ICON_CHAT, "Back", SENDER_OPEN, MenuAction(menu, page, context));
    }

    std::string Toggled(bool on, std::string const& label)
    {
        return std::string(on ? "[On] " : "[Off] ") + label;
    }

    uint32 ClampPage(uint32 page, size_t total)
    {
        uint32 const last = total ? uint32((total - 1) / BONK_SANDBOX_PAGE) : 0;
        return std::min(page, last);
    }

    void AddPaging(Player* player, SandboxMenu menu, uint32 page, uint32 context, size_t total)
    {
        if (page > 0)
            AddItem(player, GOSSIP_ICON_CHAT, "Previous page", SENDER_OPEN, MenuAction(menu, page - 1, context));
        if ((page + 1) * BONK_SANDBOX_PAGE < total)
            AddItem(player, GOSSIP_ICON_CHAT, "Next page", SENDER_OPEN, MenuAction(menu, page + 1, context));
    }

    bool IsEnemyType(BonkMapCreature const& creature, SandboxEnemyType type)
    {
        BonkMob const* mob = sBonkMgr.GetMob(creature.entry);
        bool const hasAbilities = mob && !mob->abilities.empty();
        switch (type)
        {
            case ENEMY_CASTER:
                return creature.kind == BONK_SPAWN_NORMAL && creature.archetype == BONK_ARCHETYPE_CASTER && hasAbilities;
            case ENEMY_RANGED:
                return mob && mob->role == BONK_ROLE_RANGED && hasAbilities;
            case ENEMY_CHARGER:
                return creature.kind == BONK_SPAWN_NORMAL && creature.archetype == BONK_ARCHETYPE_CHARGER;
            case ENEMY_ELITE:
                return creature.kind == BONK_SPAWN_ELITE && hasAbilities;
            case ENEMY_MINIBOSS:
                return creature.kind == BONK_SPAWN_MINIBOSS && hasAbilities;
            case ENEMY_BOSS:
                return creature.kind == BONK_SPAWN_BOSS && hasAbilities;
            default:
                return false;
        }
    }

    std::vector<SandboxEnemy> ListEnemies(SandboxEnemyType type)
    {
        std::vector<SandboxEnemy> list;
        std::set<uint32> seen;
        for (auto const& itr : sBonkMgr.GetMaps())
            for (BonkMapCreature const& creature : itr.second.creatures)
            {
                if (!IsEnemyType(creature, type) || !seen.insert(creature.entry).second)
                    continue;
                if (CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(creature.entry))
                    list.push_back({ creature.entry, info->name + " (" + itr.second.name + ")" });
            }
        return list;
    }

    BonkMapCreature const* FindMapCreature(uint32 entry)
    {
        for (auto const& itr : sBonkMgr.GetMaps())
            for (BonkMapCreature const& creature : itr.second.creatures)
                if (creature.entry == entry)
                    return &creature;
        return nullptr;
    }

    BonkMapCreature const* PickNormalCreature(BonkMap const& map)
    {
        uint32 total = 0;
        for (BonkMapCreature const& creature : map.creatures)
            if (creature.kind == BONK_SPAWN_NORMAL && !creature.flyer)
                total += creature.weight;
        if (!total)
            return nullptr;

        uint32 roll = urand(1, total);
        for (BonkMapCreature const& creature : map.creatures)
        {
            if (creature.kind != BONK_SPAWN_NORMAL || creature.flyer)
                continue;
            if (roll <= creature.weight)
                return &creature;
            roll -= creature.weight;
        }
        return nullptr;
    }

    float GroundHeight(Player const* player, float x, float y, float aboutZ)
    {
        float const z = player->GetMap()->GetHeight(x, y, aboutZ + 5.0f, true, 15.0f);
        return z > INVALID_HEIGHT ? z : aboutZ;
    }

    void AddMainItems(Player* player)
    {
        AddItem(player, GOSSIP_ICON_BATTLE, "Dummies", SENDER_OPEN, MenuAction(MENU_DUMMIES));
        AddItem(player, GOSSIP_ICON_BATTLE, "Enemies", SENDER_OPEN, MenuAction(MENU_ENEMIES));
        AddItem(player, GOSSIP_ICON_TRAINER, "Levels", SENDER_OPEN, MenuAction(MENU_LEVELS));
        AddItem(player, GOSSIP_ICON_TRAINER, "Spells", SENDER_OPEN, MenuAction(MENU_SPELLS));
        AddItem(player, GOSSIP_ICON_INTERACT_1, "Reset", SENDER_OPEN, MenuAction(MENU_RESET));
        AddItem(player, GOSSIP_ICON_TAXI, "Leave the sandbox", SENDER_LEAVE, 0);
    }

    void AddDummyItems(Player* player)
    {
        AddItem(player, GOSSIP_ICON_BATTLE, "1 dummy", SENDER_DUMMY, DummyAction(1, BONK_SPAWN_NORMAL));
        AddItem(player, GOSSIP_ICON_BATTLE, "Row of 5 dummies", SENDER_DUMMY, DummyAction(5, BONK_SPAWN_NORMAL));
        AddItem(player, GOSSIP_ICON_BATTLE, "Grid of 20 dummies", SENDER_DUMMY, DummyAction(20, BONK_SPAWN_NORMAL));
        AddItem(player, GOSSIP_ICON_BATTLE, "1 elite dummy", SENDER_DUMMY, DummyAction(1, BONK_SPAWN_ELITE));
        AddItem(player, GOSSIP_ICON_BATTLE, "1 boss dummy", SENDER_DUMMY, DummyAction(1, BONK_SPAWN_BOSS));
        AddItem(player, GOSSIP_ICON_INTERACT_1, "Refill dummies", SENDER_RESET, RESET_DUMMIES);
        AddBack(player, MENU_MAIN);
    }

    void AddEnemyItems(Player* player, bool passive, bool unkillable)
    {
        for (uint32 count : { 10u, 30u, 100u })
            AddItem(player, GOSSIP_ICON_BATTLE, "Pack of " + std::to_string(count) + " enemies", SENDER_PACK, count);
        for (uint32 type = 0; type < MAX_ENEMY_TYPE; ++type)
        {
            size_t const choices = ListEnemies(SandboxEnemyType(type)).size();
            AddItem(player, GOSSIP_ICON_CHAT, std::string(ENEMY_TYPE_LABELS[type]) + " (" + std::to_string(choices) + ")", SENDER_OPEN,
                MenuAction(MENU_ENEMY_LIST, 0, type));
        }
        AddItem(player, GOSSIP_ICON_INTERACT_1, Toggled(passive, "Enemies passive"), SENDER_TOGGLE, TOGGLE_PASSIVE);
        AddItem(player, GOSSIP_ICON_INTERACT_1, Toggled(unkillable, "Enemies stop at 1 health"), SENDER_TOGGLE, TOGGLE_UNKILLABLE);
        AddBack(player, MENU_MAIN);
    }

    void AddEnemyList(Player* player, SandboxEnemyType type, uint32 page)
    {
        std::vector<SandboxEnemy> const list = ListEnemies(type);
        if (list.empty())
            AddItem(player, GOSSIP_ICON_CHAT, "None on any run map", SENDER_OPEN, MenuAction(MENU_ENEMIES));
        page = ClampPage(page, list.size());
        size_t const end = std::min(list.size(), size_t(page + 1) * BONK_SANDBOX_PAGE);
        for (size_t i = size_t(page) * BONK_SANDBOX_PAGE; i < end; ++i)
            AddItem(player, GOSSIP_ICON_BATTLE, list[i].label, SENDER_ENEMY, list[i].entry);
        AddPaging(player, MENU_ENEMY_LIST, page, type, list.size());
        AddBack(player, MENU_ENEMIES);
    }

    void AddLevelItems(Player* player)
    {
        AddItem(player, GOSSIP_ICON_TRAINER, "+1 level", SENDER_LEVELS, 1);
        AddItem(player, GOSSIP_ICON_TRAINER, "+5 levels", SENDER_LEVELS, 5);
        AddItem(player, GOSSIP_ICON_TRAINER, "+10 levels", SENDER_LEVELS, 10);
        for (uint32 level : { 10u, 20u, 30u, 40u, 50u, 60u })
            AddItem(player, GOSSIP_ICON_TRAINER, "Set level " + std::to_string(level) + " (no level-up choices)", SENDER_SET_LEVEL, level);
        AddItem(player, GOSSIP_ICON_INTERACT_1, "Spawn experience crystals", SENDER_LEVELS, 0);
        AddBack(player, MENU_MAIN);
    }

    void AddResetItems(Player* player)
    {
        AddItem(player, GOSSIP_ICON_INTERACT_1, "Clear all enemies and dummies", SENDER_RESET, RESET_CLEAR);
        AddItem(player, GOSSIP_ICON_INTERACT_1, "Full heal", SENDER_RESET, RESET_HEAL);
        AddItem(player, GOSSIP_ICON_INTERACT_1, "Reset all cooldowns", SENDER_RESET, RESET_COOLDOWNS);
        AddItem(player, GOSSIP_ICON_INTERACT_1, "Refill dummies", SENDER_RESET, RESET_DUMMIES);
        AddItem(player, GOSSIP_ICON_INTERACT_1, Toggled(player->GetBonkPlayer()->IsTestGod(), "God mode"), SENDER_TOGGLE, TOGGLE_GOD);
        AddBack(player, MENU_MAIN);
    }
}

namespace BonkSandbox
{
    bool IsAllowed(Player const* player)
    {
        return player && player->GetSession() && player->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
    }

    bool IsHiddenFrom(Creature const* creature, Player const* viewer)
    {
        return creature->GetEntry() == BONK_SANDBOX_KEEPER && !IsAllowed(viewer);
    }
}

bool BonkPlayer::IsInSandbox() const
{
    return m_run && m_run->IsSandbox();
}

bool BonkPlayer::StartSandbox(std::string& error)
{
    if (!BonkSandbox::IsAllowed(m_player))
        error = "The sandbox is for game masters.";
    else if (m_run)
        error = "You are already in a run. Use .bonk stop to leave it.";
    else if (!IsAtCamp())
        error = "Go back to camp to enter the sandbox.";
    else if (m_player->IsInCombat())
        error = "You can't enter the sandbox while in combat.";
    else if (m_player->IsTaxiFlying())
        error = "You can't enter the sandbox while flying.";
    else if (!IsPlayableClass(m_player->GetClass()) || !sBonkMgr.HasStarterSpell(m_player->GetClassMask()))
        error = "The " + GetClassName() + " is being reworked and can't enter the sandbox yet.";
    if (!error.empty())
        return false;

    BonkMap const* map = sBonkMgr.GetMap(BONK_SANDBOX_MAP);
    BonkTier const* tier = map ? sBonkMgr.GetTier(map->tierId) : nullptr;
    if (!tier)
    {
        std::vector<BonkTier const*> const tiers = GetTiersInOrder();
        tier = tiers.empty() ? nullptr : tiers.front();
    }
    if (!map || map->creatures.empty() || !tier)
    {
        error = "The sandbox map is not set up.";
        return false;
    }

    bool const godBefore = m_testGod;
    m_testGod = true;
    m_run.reset(new BonkRun(m_player, *tier));
    m_run->MakeSandbox(godBefore);
    if (m_run->Begin(error))
        return true;

    m_run.reset();
    m_testGod = godBefore;
    m_player->SetCheatGod(godBefore);
    return false;
}

BonkMap const* BonkRun::GetSandboxMap() const
{
    BonkMap const* map = sBonkMgr.GetMap(BONK_SANDBOX_MAP);
    return map && !map->creatures.empty() ? map : nullptr;
}

bool BonkRun::TeleportToSandbox(std::string& error)
{
    if (!m_player->TeleportTo(m_map.mapId, BONK_SANDBOX_X, BONK_SANDBOX_Y, BONK_SANDBOX_Z + 0.5f, BONK_SANDBOX_O))
    {
        error = "You couldn't travel to the sandbox.";
        return false;
    }
    m_state = STATE_ENTERING;
    m_enterTimer = 0;
    return true;
}

void BonkRun::StartSandbox()
{
    SendFullState();
    SummonSandboxKeeper();
    m_player->GetBonkPlayer()->Notify("Welcome to the sandbox. Talk to the Sandbox Keeper behind you for dummies, enemies, levels and spells. "
        "Nothing here counts.");
}

void BonkRun::SummonSandboxKeeper()
{
    float const x = BONK_SANDBOX_X - BONK_SANDBOX_KEEPER_BEHIND * std::cos(BONK_SANDBOX_O);
    float const y = BONK_SANDBOX_Y - BONK_SANDBOX_KEEPER_BEHIND * std::sin(BONK_SANDBOX_O);
    float const z = GroundHeight(m_player, x, y, BONK_SANDBOX_Z);
    Creature* keeper = m_player->SummonCreature(BONK_SANDBOX_KEEPER, x, y, z, BONK_SANDBOX_O, TEMPSUMMON_MANUAL_DESPAWN, 0);
    m_sandboxKeeper = keeper ? keeper->GetObjectGuid() : ObjectGuid();
}

void BonkRun::UpdateSandbox(uint32 diff)
{
    m_sandboxTimer += diff;
    if (m_sandboxTimer < BONK_SANDBOX_TICK_MS)
        return;
    uint32 const elapsed = m_sandboxTimer;
    m_sandboxTimer = 0;

    PruneMobs();
    SpawnQueuedSandboxPack();
    RefillIdleDummies(elapsed);
    if (m_sandboxKeeper.IsEmpty() || !m_player->GetMap()->GetCreature(m_sandboxKeeper))
        SummonSandboxKeeper();
}

void BonkRun::SpawnQueuedSandboxPack()
{
    for (uint32 i = 0; i < BONK_SANDBOX_SPAWNS_PER_TICK && m_sandboxPackLeft; ++i)
    {
        --m_sandboxPackLeft;
        if (BonkMapCreature const* creature = PickNormalCreature(m_map))
            SpawnMob(*creature);
    }
}

void BonkRun::RefillIdleDummies(uint32 elapsedMs)
{
    Map* map = m_player->GetMap();
    for (auto itr = m_sandboxDummies.begin(); itr != m_sandboxDummies.end();)
    {
        Creature* dummy = map->GetCreature(itr->first);
        if (!dummy || !dummy->IsAlive())
        {
            itr = m_sandboxDummies.erase(itr);
            continue;
        }

        SandboxDummy& state = itr->second;
        uint32 const health = dummy->GetHealth();
        if (m_sandboxDeepDummies && health < uint32(dummy->GetMaxHealth() * BONK_DEEP_DUMMY_REFILL))
        {
            dummy->SetHealth(dummy->GetMaxHealth());
            state.health = dummy->GetHealth();
            state.idleMs = 0;
        }
        else if (health != state.health)
        {
            state.health = health;
            state.idleMs = 0;
        }
        else if (health < dummy->GetMaxHealth())
        {
            state.idleMs += elapsedMs;
            if (state.idleMs >= BONK_DUMMY_REFILL_MS)
            {
                dummy->SetHealth(dummy->GetMaxHealth());
                state.health = dummy->GetHealth();
                state.idleMs = 0;
            }
        }
        ++itr;
    }
}

void BonkRun::PrepareSandboxMob(Creature* mob)
{
    BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI());
    if (mob->GetEntry() == BONK_SANDBOX_DUMMY)
    {
        mob->SetInvincibilityHpThreshold(1);
        if (ai)
            ai->SetStationary();
        m_sandboxDummies[mob->GetObjectGuid()].health = mob->GetHealth();
        return;
    }

    if (m_sandboxUnkillable)
        mob->SetInvincibilityHpThreshold(1);
    if (m_sandboxPassive && ai)
        ai->SetPassive(true);
}

void BonkRun::ReviveInSandbox()
{
    m_player->ResurrectPlayer(1.0f);
    m_player->SpawnCorpseBones();
    m_player->GetBonkPlayer()->Notify("You fell, so the sandbox brought you back.");
}

void BonkRun::EndSandbox()
{
    if (Map* map = m_player->FindMap())
        if (Creature* keeper = map->GetCreature(m_sandboxKeeper))
            keeper->DespawnOrUnsummon();
    m_sandboxKeeper.Clear();
    m_sandboxDummies.clear();
    m_sandboxPackLeft = 0;

    m_player->GetBonkPlayer()->SetTestGod(m_sandboxGodBefore);
    m_player->SetCheatGod(m_sandboxGodBefore);
}

void BonkRun::LeaveSandbox()
{
    if (m_state == STATE_FINISHED || m_state == STATE_ENDING)
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (m_state == STATE_RUNNING)
    {
        if (bonk->IsAddonReady())
            bonk->SendAddon("end~1~" + std::to_string(m_totalMs / IN_MILLISECONDS) + "~" + std::to_string(m_kills) + "~0~" +
                std::to_string(m_player->GetLevel()) + "~" + std::to_string(m_stage) + "~0~0");
        else
            ChatHandler(m_player).SendSysMessage("You left the sandbox.");
    }
    Exit();
}

float BonkRun::GetSandboxFront() const
{
    Creature const* keeper = m_sandboxKeeper.IsEmpty() ? nullptr : m_player->GetMap()->GetCreature(m_sandboxKeeper);
    if (keeper && keeper->IsWithinDistInMap(m_player, BONK_SANDBOX_FRONT_RANGE) && keeper->GetDistance2d(m_player) > 1.0f)
        return keeper->GetAngle(m_player);
    return m_player->GetOrientation();
}

void BonkRun::SpawnSandboxDummies(uint32 count, BonkSpawnKind kind)
{
    if (!count)
        return;

    float const front = GetSandboxFront();
    float const heroX = m_player->GetPositionX();
    float const heroY = m_player->GetPositionY();
    float const heroZ = m_player->GetPositionZ();
    uint32 const columns = std::min(count, BONK_DUMMY_COLUMNS);
    uint32 const rows = (count + columns - 1) / columns;

    BonkMapCreature dummy;
    dummy.entry = BONK_SANDBOX_DUMMY;
    dummy.kind = kind;
    dummy.archetype = BONK_ARCHETYPE_GRUNT;
    dummy.scale = kind == BONK_SPAWN_BOSS ? BONK_DUMMY_BOSS_SCALE : kind == BONK_SPAWN_ELITE ? BONK_DUMMY_ELITE_SCALE : 1.0f;
    dummy.healthMultiplier = m_sandboxDeepDummies ? BONK_DEEP_DUMMY_HEALTH : 1.0f;

    uint32 spawned = 0;
    for (uint32 i = 0; i < count; ++i)
    {
        uint32 const row = i / columns;
        uint32 const column = i % columns;
        float const ahead = rows > 1 ? BONK_DUMMY_GRID_NEAR + (BONK_DUMMY_GRID_FAR - BONK_DUMMY_GRID_NEAR) * row / float(rows - 1) : BONK_DUMMY_DISTANCE;
        float const side = (float(column) - float(columns - 1) / 2.0f) * BONK_DUMMY_SPACING;
        float const x = heroX + ahead * std::cos(front) - side * std::sin(front);
        float const y = heroY + ahead * std::sin(front) + side * std::cos(front);
        BonkMobAI::SetNextAffixCount(0);
        if (SpawnMobAt(dummy, x, y, GroundHeight(m_player, x, y, heroZ)))
            ++spawned;
        BonkMobAI::SetNextAffixCount(-1);
    }

    std::string const kindName = kind == BONK_SPAWN_BOSS ? "boss " : kind == BONK_SPAWN_ELITE ? "elite " : "";
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (!spawned)
        bonk->Notify("There is no room for dummies here.");
    else if (spawned == 1)
        bonk->Notify((kind == BONK_SPAWN_ELITE ? "An " : "A ") + kindName + "training dummy stands ready.");
    else
        bonk->Notify(std::to_string(spawned) + " " + kindName + "training dummies stand ready.");
}

void BonkRun::QueueSandboxPack(uint32 count)
{
    PruneMobs();
    uint32 const busy = uint32(m_mobs.size()) + m_sandboxPackLeft;
    uint32 const room = m_tier.maxAlive > busy ? m_tier.maxAlive - busy : 0;
    uint32 const queued = std::min(count, room);
    m_sandboxPackLeft += queued;
    m_player->GetBonkPlayer()->Notify(queued ? std::to_string(queued) + " enemies are on their way." :
        "The sandbox is full. Clear some enemies first.");
}

void BonkRun::SpawnSandboxEnemy(uint32 entry)
{
    BonkMapCreature const* creature = FindMapCreature(entry);
    if (!creature)
    {
        m_player->GetBonkPlayer()->Notify("That enemy is not on any run map.");
        return;
    }

    float x, y, z;
    if (!FindSpawnPositionAt(GetSandboxFront(), BONK_SANDBOX_ENEMY_DISTANCE, x, y, z) &&
        !FindMobPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
    {
        m_player->GetBonkPlayer()->Notify("There is no room for it here. Step into the open and try again.");
        return;
    }
    if (Creature* mob = SpawnMobAt(*creature, x, y, z))
        m_player->GetBonkPlayer()->Notify(std::string(mob->GetName()) + " has arrived!");
}

void BonkRun::ToggleSandbox(uint32 toggle)
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (toggle == TOGGLE_GOD)
    {
        bool const on = !bonk->IsTestGod();
        bonk->SetTestGod(on);
        m_player->SetCheatGod(on);
        bonk->Notify(on ? "God mode is on: damage stops at 1 health." : "God mode is off.");
        return;
    }
    if (toggle != TOGGLE_PASSIVE && toggle != TOGGLE_UNKILLABLE)
        return;

    bool& setting = toggle == TOGGLE_PASSIVE ? m_sandboxPassive : m_sandboxUnkillable;
    setting = !setting;
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : m_mobs)
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || mob->GetEntry() == BONK_SANDBOX_DUMMY)
            continue;
        if (toggle == TOGGLE_UNKILLABLE)
            mob->SetInvincibilityHpThreshold(setting ? 1 : 0);
        else if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
            ai->SetPassive(setting);
    }

    if (toggle == TOGGLE_PASSIVE)
        bonk->Notify(setting ? "Enemies are passive now: they don't move, attack or cast." : "Enemies fight again.");
    else
        bonk->Notify(setting ? "Enemies stop at 1 health now." : "Enemies die normally again.");
}

void BonkRun::GiveSandboxLevels(uint32 levels)
{
    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    uint32 const before = m_player->GetLevel() + m_overcapLevel;
    for (uint32 i = 0; i < levels; ++i)
    {
        uint32 const next = GetNextLevelXp();
        uint32 const have = m_player->GetLevel() >= maxLevel ? m_overcapXp : m_player->GetUInt32Value(PLAYER_XP);
        if (!next || next <= have)
            break;
        GiveExperience(next - have);
    }

    uint32 const after = m_player->GetLevel() + m_overcapLevel;
    m_player->GetBonkPlayer()->Notify(after > before ? "You are now level " + std::to_string(after) + "." : std::string("You can't gain more levels."));
}

void BonkRun::SetSandboxLevel(uint32 level)
{
    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    level = std::max(1u, std::min(level, maxLevel));
    m_overcapLevel = 0;
    m_overcapXp = 0;
    m_player->GiveLevel(level);
    m_player->SetUInt32Value(PLAYER_XP, 0);
    m_lastLevel = m_player->GetLevel();
    m_player->SetHealth(m_player->GetMaxHealth());
    SendState();
    m_player->GetBonkPlayer()->Notify("You are now level " + std::to_string(m_player->GetLevel()) + ".");
}

void BonkRun::SpawnSandboxCrystals(Creature const* keeper)
{
    uint32 const level = m_player->GetLevel() + m_overcapLevel;
    float const killsForLevel = 4.0f + 1.5f * level + 0.05f * level * level;
    float const growth = 1.0f + GetPassiveTotal(BONK_PASSIVE_GROWTH) / 100.0f;
    BonkArchetypeInfo const& grunt = sBonkMgr.GetArchetype(BONK_ARCHETYPE_GRUNT);
    uint32 const xp = std::max(1u, uint32(std::ceil(GetNextLevelXp() / killsForLevel * grunt.experience * growth)));

    WorldObject const* center = keeper ? static_cast<WorldObject const*>(keeper) : m_player;
    float const centerX = center->GetPositionX();
    float const centerY = center->GetPositionY();
    float const centerZ = center->GetPositionZ();
    float const inner = std::max(BONK_SANDBOX_CRYSTAL_INNER,
        GetMagnetRange() + m_player->GetDistance2d(centerX, centerY) + BONK_SANDBOX_CRYSTAL_MARGIN);
    float const outer = inner + BONK_SANDBOX_CRYSTAL_SPREAD;
    for (uint32 i = 0; i < BONK_SANDBOX_CRYSTALS; ++i)
    {
        float const angle = frand(0.0f, 2.0f * M_PI_F);
        float const distance = std::sqrt(frand(inner * inner, outer * outer));
        float const x = centerX + distance * std::cos(angle);
        float const y = centerY + distance * std::sin(angle);
        SpawnOrb(x, y, GroundHeight(m_player, x, y, centerZ), xp, grunt.orb);
    }
    m_player->GetBonkPlayer()->Notify(keeper ? "Experience crystals appear around " + std::string(keeper->GetName()) + "." :
        "Experience crystals surround you.");
}

BonkRun::OwnedWeapon* BonkRun::FindSandboxSpell(uint32 baseId)
{
    if (!baseId)
        return nullptr;
    auto itr = std::find_if(m_weapons.begin(), m_weapons.end(), [baseId](OwnedWeapon const& owned)
    {
        return owned.weapon.id == baseId || owned.weapon.evolvesFrom == baseId;
    });
    return itr != m_weapons.end() ? &*itr : nullptr;
}

bool BonkRun::AddSandboxSpell(uint32 baseId, std::string& error)
{
    BonkWeapon const* base = sBonkMgr.GetWeapon(baseId);
    if (!base || !ForceEvolve(baseId, error))
        return false;
    if (UsesShotSources())
        WarmShotSources();
    m_player->GetBonkPlayer()->Notify("You learned " + base->name + ".");
    return true;
}

void BonkRun::SetSandboxRank(uint32 baseId, uint32 rank)
{
    OwnedWeapon* owned = FindSandboxSpell(baseId);
    if (!owned || !rank)
        return;

    owned->rank = 1;
    owned->damageBonus = 0.0f;
    std::fill(std::begin(owned->upgrades), std::end(owned->upgrades), 0.0f);
    owned->rarities.clear();
    while (owned->rank < rank)
    {
        Offer step;
        step.id = owned->weapon.id;
        step.newLevel = owned->rank + 1;
        step.rarity = RollRarity(BONK_RARITY_COMMON);
        step.step = BONK_STEP_RANK;
        RollUpgrades(*owned, step);
        ApplyStep(*owned, step);
    }
    SendLoadout();
    m_player->GetBonkPlayer()->Notify(owned->weapon.name + " is now rank " + std::to_string(owned->rank) + ".");
}

void BonkRun::RemoveSandboxSpells(uint32 baseId)
{
    DespawnPets();
    DespawnTraps();
    DespawnWeaponOrbs();
    DespawnProjectiles();
    DespawnShotSources();
    ClearHunterEvolutions();
    ResetWarriorState();
    ResetMageState();
    m_pendingCasts.clear();
    m_marks.clear();
    m_buffs.clear();

    m_weapons.erase(std::remove_if(m_weapons.begin(), m_weapons.end(), [baseId](OwnedWeapon const& owned)
    {
        return !baseId || owned.weapon.id == baseId || owned.weapon.evolvesFrom == baseId;
    }), m_weapons.end());
    RebuildWeaponSpells();
    if (UsesShotSources())
        WarmShotSources();
    ApplyPassives();
    SendLoadout();
}

void BonkRun::ResetSandbox(uint32 reset)
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    switch (reset)
    {
        case RESET_CLEAR:
            ClearSandboxEnemies();
            bonk->Notify("All enemies and dummies are gone.");
            break;
        case RESET_HEAL:
            HealInSandbox();
            bonk->Notify("You are fully healed.");
            break;
        case RESET_COOLDOWNS:
            ResetSandboxCooldowns();
            bonk->Notify("All your cooldowns are ready.");
            break;
        case RESET_DUMMIES:
            RefillSandboxDummies();
            bonk->Notify("The training dummies are whole again.");
            break;
        case RESET_SPELLS:
            RemoveSandboxSpells(0);
            bonk->Notify("You forgot all your spells.");
            break;
        default:
            break;
    }
}

void BonkRun::ClearSandboxEnemies()
{
    m_sandboxPackLeft = 0;
    DespawnMobs();
    m_sandboxDummies.clear();
    m_telegraphs.Clear(m_player);
    m_voidZones.clear();
    DespawnPools();
    m_marks.clear();
    m_pendingKeywords.clear();
    m_pendingConsumes.clear();
    m_dyingKeywords.clear();
    m_dotStacks.clear();
    m_crowdControl.clear();
    SetNecroticStacks(0);
    m_player->GetBonkPlayer()->SendAddon("mobclear");
}

void BonkRun::HealInSandbox()
{
    if (!m_player->IsAlive())
        ReviveInSandbox();
    m_player->SetHealth(m_player->GetMaxHealth());
    m_player->SetPower(POWER_MANA, m_player->GetMaxPower(POWER_MANA));
    m_shield = GetMaxShield();
    m_shieldIdleMs = 0;
    SetNecroticStacks(0);
}

void BonkRun::ResetSandboxCooldowns()
{
    for (OwnedWeapon& owned : m_weapons)
        owned.timer = 0;
    std::fill(std::begin(m_cardCooldowns), std::end(m_cardCooldowns), 0);
    m_player->RemoveAllCooldowns();
    SendCardCooldowns();
    SendLoadout();
}

void BonkRun::RefillSandboxDummies()
{
    Map* map = m_player->GetMap();
    for (auto& itr : m_sandboxDummies)
        if (Creature* dummy = map->GetCreature(itr.first))
            if (dummy->IsAlive())
            {
                dummy->SetHealth(dummy->GetMaxHealth());
                itr.second.health = dummy->GetHealth();
                itr.second.idleMs = 0;
            }
}

void BonkRun::ShowSandboxMenu(Creature* keeper, uint32 menu)
{
    m_sandboxMenu = menu;
    PlayerMenu* talk = m_player->PlayerTalkClass;
    talk->ClearMenus();
    switch (MenuOf(menu))
    {
        case MENU_DUMMIES:
            AddDummyItems(m_player);
            break;
        case MENU_ENEMIES:
            AddEnemyItems(m_player, m_sandboxPassive, m_sandboxUnkillable);
            break;
        case MENU_ENEMY_LIST:
            AddEnemyList(m_player, SandboxEnemyType(ContextOf(menu)), PageOf(menu));
            break;
        case MENU_LEVELS:
            AddLevelItems(m_player);
            break;
        case MENU_SPELLS:
            AddSandboxSpellItems();
            break;
        case MENU_SPELL:
            AddSandboxSpellPage(ContextOf(menu));
            break;
        case MENU_PASSIVES:
            AddSandboxPassiveItems(PageOf(menu));
            break;
        case MENU_CARDS:
            AddSandboxCardItems(PageOf(menu));
            break;
        case MENU_RESET:
            AddResetItems(m_player);
            break;
        default:
            AddMainItems(m_player);
            break;
    }
    talk->SendGossipMenu(BONK_TEXT_SANDBOX_MENU, keeper->GetObjectGuid());
}

void BonkRun::AddSandboxSpellItems()
{
    uint32 const classMask = m_player->GetClassMask();
    for (auto const& itr : sBonkMgr.GetWeapons())
    {
        BonkWeapon const& base = itr.second;
        if (base.evolvesFrom || (base.classMask && !(base.classMask & classMask)))
            continue;
        if (m_player->PlayerTalkClass->GetGossipMenu().MenuItemCount() >= GOSSIP_MAX_MENU_ITEMS - 4)
            break;
        OwnedWeapon const* owned = FindSandboxSpell(base.id);
        std::string const text = owned ? owned->weapon.name + " (rank " + std::to_string(owned->rank) + ")" : base.name;
        AddItem(m_player, owned ? GOSSIP_ICON_TRAINER : GOSSIP_ICON_CHAT, text, SENDER_OPEN, MenuAction(MENU_SPELL, 0, base.id));
    }
    AddItem(m_player, GOSSIP_ICON_TRAINER, "Give a passive", SENDER_OPEN, MenuAction(MENU_PASSIVES));
    AddItem(m_player, GOSSIP_ICON_TRAINER, "Give a Darkmoon Card", SENDER_OPEN, MenuAction(MENU_CARDS));
    AddItem(m_player, GOSSIP_ICON_INTERACT_1, "Remove all spells", SENDER_RESET, RESET_SPELLS);
    AddBack(m_player, MENU_MAIN);
}

void BonkRun::AddSandboxSpellPage(uint32 baseId)
{
    BonkWeapon const* base = sBonkMgr.GetWeapon(baseId);
    if (!base)
    {
        AddBack(m_player, MENU_SPELLS);
        return;
    }

    OwnedWeapon const* owned = FindSandboxSpell(baseId);
    std::string const status = (owned ? owned->weapon.name + ", rank " + std::to_string(owned->rank) : base->name + ", not learned") +
        ". Spell slots: " + std::to_string(m_weapons.size()) + " of " + std::to_string(GetSpellSlots()) + ".";
    AddItem(m_player, GOSSIP_ICON_CHAT, status, SENDER_OPEN, MenuAction(MENU_SPELL, 0, baseId));

    std::vector<BonkWeapon const*> const evolutions = sBonkMgr.GetEvolutions(baseId);
    if (owned)
    {
        for (uint32 rank : BONK_SANDBOX_RANKS)
            AddItem(m_player, GOSSIP_ICON_TRAINER, "Set rank " + std::to_string(rank), SENDER_SPELL_RANK, PairAction(rank, baseId));
        for (BonkWeapon const* evolved : evolutions)
        {
            bool const held = owned->weapon.id == evolved->id;
            AddItem(m_player, held ? GOSSIP_ICON_CHAT : GOSSIP_ICON_BATTLE, held ? evolved->name + " (held)" : "Evolve into " + evolved->name,
                SENDER_EVOLVE, evolved->id);
        }
        AddItem(m_player, GOSSIP_ICON_INTERACT_1, "Remove " + owned->weapon.name, SENDER_SPELL_REMOVE, baseId);
    }
    else
    {
        if (m_weapons.size() < GetSpellSlots())
            AddItem(m_player, GOSSIP_ICON_BATTLE, "Add " + base->name, SENDER_SPELL_ADD, baseId);
        else
            for (OwnedWeapon const& other : m_weapons)
                AddItem(m_player, GOSSIP_ICON_INTERACT_1, "Replace " + other.weapon.name, SENDER_SPELL_REPLACE,
                    PairAction(baseId, other.weapon.evolvesFrom ? other.weapon.evolvesFrom : other.weapon.id));
        for (BonkWeapon const* evolved : evolutions)
            AddItem(m_player, GOSSIP_ICON_BATTLE, "Add as " + evolved->name, SENDER_EVOLVE, evolved->id);
    }
    AddBack(m_player, MENU_SPELLS);
}

void BonkRun::AddSandboxPassiveItems(uint32 page)
{
    std::vector<BonkPassive const*> passives;
    for (auto const& itr : sBonkMgr.GetPassives())
        passives.push_back(&itr.second);

    page = ClampPage(page, passives.size());
    size_t const end = std::min(passives.size(), size_t(page + 1) * BONK_SANDBOX_PAGE);
    for (size_t i = size_t(page) * BONK_SANDBOX_PAGE; i < end; ++i)
    {
        BonkPassive const& passive = *passives[i];
        auto const rank = m_passiveRanks.find(passive.id);
        uint32 const current = rank != m_passiveRanks.end() ? rank->second : 0;
        AddItem(m_player, current < passive.maxRank ? GOSSIP_ICON_TRAINER : GOSSIP_ICON_CHAT,
            passive.name + " " + std::to_string(current) + "/" + std::to_string(passive.maxRank), SENDER_PASSIVE, passive.id);
    }
    AddPaging(m_player, MENU_PASSIVES, page, 0, passives.size());
    AddBack(m_player, MENU_SPELLS);
}

void BonkRun::AddSandboxCardItems(uint32 page)
{
    std::vector<BonkCard const*> cards;
    for (auto const& itr : sBonkMgr.GetCards())
        cards.push_back(&itr.second);

    page = ClampPage(page, cards.size());
    size_t const end = std::min(cards.size(), size_t(page + 1) * BONK_SANDBOX_PAGE);
    for (size_t i = size_t(page) * BONK_SANDBOX_PAGE; i < end; ++i)
    {
        BonkCard const& card = *cards[i];
        uint32 const copies = GetCardCopies(card.id);
        AddItem(m_player, GOSSIP_ICON_VENDOR, copies ? card.name + " (" + std::to_string(copies) + " held)" : card.name, SENDER_CARD, card.id);
    }
    AddPaging(m_player, MENU_CARDS, page, 0, cards.size());
    AddBack(m_player, MENU_SPELLS);
}

void BonkRun::SelectSandboxOption(Creature* keeper, uint32 sender, uint32 action)
{
    if (m_state != STATE_RUNNING)
    {
        m_player->PlayerTalkClass->CloseGossip();
        return;
    }

    std::string error;
    uint32 next = m_sandboxMenu;
    switch (sender)
    {
        case SENDER_OPEN:
            next = action;
            break;
        case SENDER_DUMMY:
            SpawnSandboxDummies(action & 0xFF, BonkSpawnKind(action >> 8));
            break;
        case SENDER_PACK:
            QueueSandboxPack(action);
            break;
        case SENDER_ENEMY:
            SpawnSandboxEnemy(action);
            break;
        case SENDER_TOGGLE:
            ToggleSandbox(action);
            break;
        case SENDER_LEVELS:
            if (action)
                GiveSandboxLevels(action);
            else
                SpawnSandboxCrystals(keeper);
            break;
        case SENDER_SET_LEVEL:
            SetSandboxLevel(action);
            break;
        case SENDER_SPELL_ADD:
            AddSandboxSpell(action, error);
            break;
        case SENDER_SPELL_REPLACE:
            if (FindSandboxSpell(action & 0xFFFF))
                RemoveSandboxSpells(action & 0xFFFF);
            AddSandboxSpell(action >> 16, error);
            next = MenuAction(MENU_SPELL, 0, action >> 16);
            break;
        case SENDER_SPELL_RANK:
            SetSandboxRank(action & 0xFFFF, action >> 16);
            break;
        case SENDER_SPELL_REMOVE:
            if (OwnedWeapon const* owned = FindSandboxSpell(action))
            {
                std::string const name = owned->weapon.name;
                RemoveSandboxSpells(action);
                m_player->GetBonkPlayer()->Notify("You forgot " + name + ".");
            }
            break;
        case SENDER_EVOLVE:
            ForceEvolve(action, error);
            break;
        case SENDER_PASSIVE:
            GivePassive(action, error);
            break;
        case SENDER_CARD:
        {
            BonkCard const* card = sBonkMgr.GetCard(action);
            if (card && card->maxCopies && GetCardCopies(action) >= card->maxCopies)
                error = "You already hold every copy of " + card->name + ".";
            else
                GiveCard(action, error);
            break;
        }
        case SENDER_RESET:
            ResetSandbox(action);
            break;
        case SENDER_LEAVE:
            m_player->PlayerTalkClass->CloseGossip();
            LeaveSandbox();
            return;
        default:
            break;
    }

    if (!error.empty())
        m_player->GetBonkPlayer()->Notify(error);
    ShowSandboxMenu(keeper, next);
}

bool GossipHello_npc_bonk_sandbox(Player* player, Creature* creature)
{
    if (!BonkSandbox::IsAllowed(player))
    {
        player->PlayerTalkClass->SendGossipMenu(BONK_TEXT_SANDBOX_REFUSE, creature->GetObjectGuid());
        return true;
    }

    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (run && run->IsSandbox())
    {
        run->ShowSandboxMenu(creature, MenuAction(MENU_MAIN));
        return true;
    }

    if (!run)
        AddItem(player, GOSSIP_ICON_BATTLE, "Enter the sandbox", SENDER_ENTER, 0);
    player->PlayerTalkClass->SendGossipMenu(BONK_TEXT_SANDBOX_CAMP, creature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_bonk_sandbox(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    bool const allowed = BonkSandbox::IsAllowed(player);
    BonkRun* run = player->GetBonkPlayer()->GetRun();
    if (allowed && run && run->IsSandbox())
    {
        run->SelectSandboxOption(creature, sender, action);
        return true;
    }

    player->PlayerTalkClass->CloseGossip();
    if (!allowed || run || sender != SENDER_ENTER)
        return true;

    std::string error;
    if (!player->GetBonkPlayer()->StartSandbox(error))
        player->GetBonkPlayer()->Notify(error);
    return true;
}

bool BonkRun::RunSandboxVerb(std::string const& verb, uint32 first, uint32 second, std::string& error)
{
    if (m_state != STATE_RUNNING)
    {
        error = "The sandbox is not ready yet.";
        return false;
    }

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (verb == "pack")
        QueueSandboxPack(first ? first : BONK_SANDBOX_DEFAULT_PACK);
    else if (verb == "dummies")
        SpawnSandboxDummies(std::max(1u, std::min(first, 20u)), BONK_SPAWN_NORMAL);
    else if (verb == "spell")
        return AddSandboxSpell(first, error);
    else if (verb == "evolve")
        return ForceEvolve(first, error);
    else if (verb == "rank")
        SetSandboxRank(first, second);
    else if (verb == "forget")
        RemoveSandboxSpells(first);
    else if (verb == "level")
        SetSandboxLevel(first);
    else if (verb == "clear")
        ResetSandbox(RESET_CLEAR);
    else if (verb == "heal")
        ResetSandbox(RESET_HEAL);
    else if (verb == "cooldowns")
        ResetSandbox(RESET_COOLDOWNS);
    else if (verb == "god")
        ToggleSandbox(TOGGLE_GOD);
    else if (verb == "passive")
        ToggleSandbox(TOGGLE_PASSIVE);
    else if (verb == "unkillable")
        ToggleSandbox(TOGGLE_UNKILLABLE);
    else if (verb == "deep")
    {
        m_sandboxDeepDummies = !m_sandboxDeepDummies;
        bonk->Notify(m_sandboxDeepDummies ? "New dummies have 100 times the health and refill below half, so every hit counts on the meter." :
            "New dummies are normal again.");
    }
    else if (verb == "minute")
    {
        m_sandboxMinute = first ? float(first) : -1.0f;
        bonk->Notify(first ? "New enemies are as tough as at minute " + std::to_string(first) + " of a run." :
            "Enemy toughness follows your time in the sandbox again.");
    }
    else if (verb == "quiet")
    {
        m_sandboxQuietLevels = !m_sandboxQuietLevels;
        bonk->Notify(m_sandboxQuietLevels ? "Level-ups no longer offer choices." : "Level-ups offer choices again.");
    }
    else
    {
        error = "Use .bonk sandbox pack|dummies|spell|evolve|rank|forget|level|clear|heal|cooldowns|god|passive|unkillable|deep|quiet|minute.";
        return false;
    }
    return true;
}

bool ChatHandler::HandleBonkSandboxCommand(char* args)
{
    BonkPlayer* bonk = m_session->GetPlayer()->GetBonkPlayer();
    std::string error;
    char* verb = ExtractLiteralArg(&args);
    if (!verb)
    {
        if (bonk->StartSandbox(error))
            return true;
    }
    else if (!bonk->IsInSandbox())
        error = "You are not in the sandbox. Use .bonk sandbox to enter it.";
    else
    {
        uint32 first = 0, second = 0;
        ExtractUInt32(&args, first);
        ExtractUInt32(&args, second);
        if (bonk->GetRun()->RunSandboxVerb(verb, first, second, error))
            return true;
    }

    SendSysMessage(error.c_str());
    SetSentErrorMessage(true);
    return false;
}

void AddSC_bonk_sandbox()
{
    Script* newscript = new Script;
    newscript->Name = "npc_bonk_sandbox";
    newscript->pGossipHello = &GossipHello_npc_bonk_sandbox;
    newscript->pGossipSelect = &GossipSelect_npc_bonk_sandbox;
    newscript->RegisterSelf();
}
