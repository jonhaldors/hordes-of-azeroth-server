/*
 * Vanilla Bonk: shrines, BonkRun's share of the places worth walking to
 * (feature plan, Shrines and interactables).
 *
 * A stage places a few at spawn points as they gather (more on open ground:
 * BONK_SHRINE_COUNT by map type), each with a shrine objective, so they show
 * as blue pillars and on the map. Each goes once it is used:
 * - Ley Line: a blue rune ring; standing in it for BONK_LEY_CAPTURE_MS (less
 *   with the Attunement upgrade, which also makes a Moonwell last longer)
 *   (leaving pauses it, the addon shows a capture bar) offers three stat
 *   buffs for the run, rarity rolled with Luck; the run pauses for the pick,
 *   like a level-up, and picks stack.
 * - Greed Totem: clicked, the run takes one more Greed stack: +5% gold and
 *   Darkmoon Tickets each, while the crowd grows by 5% n(n+1)/2 (past the
 *   crowd cap, enemy health instead). No cap.
 * - Challenge Altar: clicked, an elite pack with two affixes guards it; a
 *   free chest with extra choices once they are dead.
 * - Moonwell (one a stage): clicked, it heals the hero inside hard for
 *   BONK_MOONWELL_SECONDS, wherever they go meanwhile, and kills inside give
 *   double experience.
 * - Summoning Stone (exactly one a stage, so no run is restarted to find
 *   one; it comes at a random place in the stage's order): clicked, the
 *   stage's boss comes early.
 *   Beating it pays like any boss but leaves the stage running: the boss
 *   still comes at its time.
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "Chat.h"
#include "Creature.h"
#include "GameObject.h"
#include "Map.h"
#include "Player.h"
#include "Utilities/Random.h"

#include <cstdio>
#include <cstring>

namespace
{
    uint32 const BONK_GO_SHRINE            = 900030;    // + BonkShrineType: its model
    uint32 const BONK_SHRINE_MODEL_SEC     = 3600;      // outlives any stage; taken away with the shrine
    uint32 const BONK_SHRINE_PLACE_MS      = 2000;      // one placed this often while some are left
    float const BONK_SHRINE_MIN_DISTANCE   = 60.0f;     // from the hero, when placed...
    float const BONK_SHRINE_MAX_DISTANCE   = 400.0f;    // ...to this far
    float const BONK_SHRINE_SPREAD         = 150.0f;    // apart from each other; the rest wait for room

    // Shrines a stage places: at least, plus up to `extra` more, by map type.
    struct ShrineCount
    {
        uint32 least;
        uint32 extra;
    };
    ShrineCount const BONK_SHRINE_COUNT[MAX_BONK_MAP_TYPE] = { { 4, 2 }, { 3, 1 }, { 1, 1 } };
    // The Summoning Stone is never rolled: every stage has exactly one.
    uint32 const BONK_SHRINE_WEIGHTS[MAX_BONK_SHRINE] = { 35, 30, 20, 15, 0 };

    float const BONK_LEY_RADIUS            = 7.0f;
    uint32 const BONK_LEY_CAPTURE_MS       = 8000;
    uint32 const BONK_LEY_SEND_MS          = 250;       // the capture bar's updates
    uint32 const BONK_LEY_CHOICES          = 3;
    float const BONK_LEY_CARD_CHANCE       = 35.0f;     // % that one choice is a Darkmoon Card

    float const BONK_GREED_REWARD          = 5.0f;      // % gold and tickets a stack
    float const BONK_GREED_CROWD           = 5.0f;      // % enemies, times n(n+1)/2

    uint32 const BONK_ALTAR_PACK           = 3;         // elites guarding it
    uint32 const BONK_ALTAR_AFFIXES        = 2;
    uint32 const BONK_ALTAR_CHEST_CHOICES  = 5;
    float const BONK_ALTAR_PACK_SPREAD     = 6.0f;      // yards from the altar

    float const BONK_MOONWELL_RADIUS       = 10.0f;
    uint32 const BONK_MOONWELL_SECONDS     = 10;
    float const BONK_MOONWELL_HEAL         = 0.08f;     // of max health a second, inside

    float const BONK_STONE_BOSS_DISTANCE   = 8.0f;      // the early boss appears this far from the stone

    char const* const SHRINE_NAMES[MAX_BONK_SHRINE] = { "leyline", "greed", "altar", "moonwell", "stone" };
    char const* const SHRINE_LABELS[MAX_BONK_SHRINE] = { "Ley Line", "Greed Totem", "Challenge Altar", "Moonwell", "Summoning Stone" };
    // Their pins on the map and minimap.
    char const* const SHRINE_ICONS[MAX_BONK_SHRINE] = {
        "Spell_Arcane_StarFire", "INV_Misc_Coin_02", "Ability_Warrior_Challange", "Spell_Arcane_TeleportMoonglade", "Spell_Shadow_Twilight" };
    // What each does, for its pin's tooltip.
    char const* const SHRINE_DESCRIPTIONS[MAX_BONK_SHRINE] = {
        "Stand in its ring for 8 seconds to choose a blessing for the rest of the run.",
        "Click it for +5% gold and Darkmoon Tickets for the run, but more enemies. Stacks.",
        "Click it to face elite guards with two affixes. Beat them for a free chest.",
        "Click it to heal 8% of your health every second for 10 seconds while you stand in it. Kills inside give double experience.",
        "Click it to summon this stage's boss early. Beating it pays like any boss, and the stage goes on.",
    };

    // A Ley Line buff: its stat, value at Common, name, icon when no passive
    // has that stat, and whether it counts points rather than percent.
    struct LeyStat
    {
        BonkPassiveEffect effect;
        float value;
        char const* name;
        char const* icon;
        bool points;
    };
    LeyStat const LEY_STATS[] = {
        { BONK_PASSIVE_POWER,       15.0f, "Power",          "Spell_Nature_Strength",       false },
        { BONK_PASSIVE_MAX_HEALTH,  15.0f, "Max Health",     "Spell_Holy_WordFortitude",    false },
        { BONK_PASSIVE_ARMOR,       10.0f, "Armor",          "Spell_Holy_DevotionAura",     true },
        { BONK_PASSIVE_DODGE,        6.0f, "Dodge",          "Spell_Shadow_ShadowWard",     false },
        { BONK_PASSIVE_HIT,         10.0f, "Hit",            "Ability_Marksmanship",        false },
        { BONK_PASSIVE_CRIT,         8.0f, "Crit",           "Ability_CriticalStrike",      false },
        { BONK_PASSIVE_CRIT_DAMAGE, 25.0f, "Crit Damage",    "Spell_Holy_PowerInfusion",    false },
        { BONK_PASSIVE_SHIELD,      15.0f, "Shield",         "Spell_Holy_InnerFire",        false },
        { BONK_PASSIVE_HASTE,       10.0f, "Haste",          "Racial_Troll_Berserk",        false },
        { BONK_PASSIVE_AREA,        20.0f, "Area",           "Spell_Holy_ArcaneIntellect",  false },
        { BONK_PASSIVE_MOVE_SPEED,  10.0f, "Movement Speed", "Ability_Mount_JungleTiger",   false },
        { BONK_PASSIVE_PICKUP,      40.0f, "Pickup Range",   "INV_Gizmo_02",                false },
        { BONK_PASSIVE_LUCK,        10.0f, "Luck",           "INV_Misc_Foot_Kodo",          true },
    };
    // A rarity's size, times the Common value.
    float const LEY_RARITY_SCALE[] = { 1.0f, 1.3f, 1.6f, 2.0f };

    LeyStat const* GetLeyStat(BonkPassiveEffect effect)
    {
        for (LeyStat const& stat : LEY_STATS)
            if (stat.effect == effect)
                return &stat;
        return nullptr;
    }

    std::string FormatLeyValue(LeyStat const& stat, float value)
    {
        char text[64];
        std::snprintf(text, sizeof(text), stat.points ? "+%.0f %s" : "+%.0f%% %s", value, stat.name);
        return text;
    }

    float DistanceSq(float x1, float y1, float x2, float y2)
    {
        return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
    }
}

char const* GetBonkShrineName(BonkShrineType type)
{
    return type < MAX_BONK_SHRINE ? SHRINE_NAMES[type] : "";
}

bool ParseBonkShrine(char const* name, BonkShrineType& type)
{
    for (uint8 i = 0; i < MAX_BONK_SHRINE; ++i)
        if (!strcmp(name, SHRINE_NAMES[i]))
        {
            type = BonkShrineType(i);
            return true;
        }
    return false;
}

void BonkRun::UpdateShrines(uint32 diff)
{
    // A few placed early in the stage, as spawn points gather.
    m_shrineTimer += diff;
    if (m_shrinesToPlace && m_shrineTimer >= BONK_SHRINE_PLACE_MS)
    {
        m_shrineTimer = 0;
        BonkShrineType const type = !m_stonePlaced && m_shrinesToPlace <= m_stoneAt ? BONK_SHRINE_STONE : PickShrineType();
        if (PlaceShrine(type))
            --m_shrinesToPlace;
    }

    for (size_t i = 0; i < m_shrines.size(); )
    {
        size_t const count = m_shrines.size();
        switch (m_shrines[i].type)
        {
            case BONK_SHRINE_LEY_LINE: UpdateLeyLine(i, diff); break;
            case BONK_SHRINE_MOONWELL: UpdateMoonwell(i, diff); break;
            case BONK_SHRINE_ALTAR:    UpdateAltar(i); break;
            default: break;
        }
        // Still there: on to the next one.
        if (m_shrines.size() == count)
            ++i;
    }
}

BonkShrineType BonkRun::PickShrineType() const
{
    auto const allowed = [this](uint8 type)
    {
        return type != BONK_SHRINE_STONE && (type != BONK_SHRINE_MOONWELL || !m_moonwellPlaced);
    };
    uint32 total = 0;
    for (uint8 i = 0; i < MAX_BONK_SHRINE; ++i)
        if (allowed(i))
            total += BONK_SHRINE_WEIGHTS[i];

    uint32 roll = urand(1, total);
    for (uint8 i = 0; i < MAX_BONK_SHRINE; ++i)
    {
        if (!allowed(i))
            continue;
        if (roll <= BONK_SHRINE_WEIGHTS[i])
            return BonkShrineType(i);
        roll -= BONK_SHRINE_WEIGHTS[i];
    }
    return BONK_SHRINE_LEY_LINE;
}

bool BonkRun::PlaceShrine(BonkShrineType type)
{
    // One Moonwell and one Summoning Stone a stage, however they come.
    if ((type == BONK_SHRINE_MOONWELL && m_moonwellPlaced) || (type == BONK_SHRINE_STONE && m_stonePlaced))
        return false;

    Shrine shrine;
    shrine.type = type;
    auto const apart = [this](BonkSpot const& spot)
    {
        for (Shrine const& other : m_shrines)
            if (DistanceSq(spot.x, spot.y, other.spot.x, other.spot.y) < BONK_SHRINE_SPREAD * BONK_SHRINE_SPREAD)
                return false;
        return true;
    };
    if (!m_spawnPoints.Take(m_player, BONK_SHRINE_MIN_DISTANCE, BONK_SHRINE_MAX_DISTANCE, shrine.spot, apart))
        return false;

    BonkSpot const& spot = shrine.spot;
    if (GameObject* model = m_player->SummonGameObject(BONK_GO_SHRINE + type, spot.x, spot.y, spot.z, frand(0.0f, 2.0f * M_PI_F),
        0.0f, 0.0f, 0.0f, 0.0f, BONK_SHRINE_MODEL_SEC, false))
        shrine.model = model->GetObjectGuid();
    shrine.objectiveId = m_objectives.Add(m_player, BONK_OBJECTIVE_SHRINE, spot.x, spot.y, spot.z, SHRINE_LABELS[type], 0, SHRINE_ICONS[type],
        SHRINE_DESCRIPTIONS[type]);
    if (type == BONK_SHRINE_MOONWELL)
        m_moonwellPlaced = true;
    if (type == BONK_SHRINE_STONE)
        m_stonePlaced = true;
    m_shrines.push_back(shrine);
    return true;
}

void BonkRun::RemoveShrine(size_t index)
{
    Shrine const& shrine = m_shrines[index];
    if (Map* map = m_player->FindMap())
        if (GameObject* model = map->GetGameObject(shrine.model))
            model->Delete();
    // Its objective gives its spawn point back.
    RemoveObjective(shrine.objectiveId);
    m_shrines.erase(m_shrines.begin() + index);
}

void BonkRun::UpdateLeyLine(size_t index, uint32 diff)
{
    Shrine& shrine = m_shrines[index];
    bool const inside = DistanceSq(m_player->GetPositionX(), m_player->GetPositionY(), shrine.spot.x, shrine.spot.y) <=
        BONK_LEY_RADIUS * BONK_LEY_RADIUS;
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (!inside)
    {
        // Out of the ring: progress waits; the bar goes away (only one Ley
        // Line can hold the hero at a time, rings being far apart).
        if (m_captureShown && shrine.progressMs)
        {
            bonk->SendAddon(std::string("capture~") + SHRINE_LABELS[BONK_SHRINE_LEY_LINE] + "~-1");
            m_captureShown = false;
        }
        return;
    }

    // Attunement: captured sooner.
    uint32 const captureMs = uint32(BONK_LEY_CAPTURE_MS / (1.0f + GetUtility(BONK_UTILITY_SHRINE_SPEED) / 100.0f));
    shrine.progressMs += diff;
    m_captureSendMs += diff;
    if (shrine.progressMs >= captureMs)
    {
        bonk->SendAddon(std::string("capture~") + SHRINE_LABELS[BONK_SHRINE_LEY_LINE] + "~100");
        bonk->SendAddon(std::string("capture~") + SHRINE_LABELS[BONK_SHRINE_LEY_LINE] + "~-1");
        m_captureShown = false;
        bonk->Notify("Ley Line captured!");
        RemoveShrine(index);
        ++m_pendingLeyOffers;
        return;
    }
    if (!m_captureShown || m_captureSendMs >= BONK_LEY_SEND_MS)
    {
        m_captureSendMs = 0;
        m_captureShown = true;
        bonk->SendAddon(std::string("capture~") + SHRINE_LABELS[BONK_SHRINE_LEY_LINE] + "~" +
            std::to_string(shrine.progressMs * 100 / captureMs));
    }
}

void BonkRun::OfferLeyBuffs()
{
    --m_pendingLeyOffers;

    // Three different stats, none already at its cap.
    std::vector<LeyStat const*> pool;
    for (LeyStat const& stat : LEY_STATS)
    {
        if (stat.effect == BONK_PASSIVE_DODGE && GetPassiveTotal(stat.effect) >= BONK_MAX_DODGE)
            continue;
        if (stat.effect == BONK_PASSIVE_HIT && GetPassiveTotal(stat.effect) >= BONK_MAX_HIT)
            continue;
        pool.push_back(&stat);
    }

    // Luck tips the odds toward the bigger rarities.
    float const luck = std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
    float const epic = 4.0f + 0.2f * luck;
    float const rare = epic + 12.0f + 0.4f * luck;
    float const uncommon = rare + 30.0f + 0.4f * luck;

    m_leyOffer.clear();
    for (uint32 i = 0; i < BONK_LEY_CHOICES && !pool.empty(); ++i)
    {
        size_t const pick = urand(0, uint32(pool.size()) - 1);
        LeyStat const* stat = pool[pick];
        pool.erase(pool.begin() + pick);

        float const roll = frand(0.0f, 100.0f);
        LeyBuff buff;
        buff.effect = stat->effect;
        buff.rarity = roll < epic ? BONK_RARITY_EPIC : roll < rare ? BONK_RARITY_RARE : roll < uncommon ? BONK_RARITY_UNCOMMON : BONK_RARITY_COMMON;
        buff.value = stat->value * LEY_RARITY_SCALE[buff.rarity];
        m_leyOffer.push_back(buff);
    }
    if (m_leyOffer.empty())
        return;

    // Now and then the last choice is a Darkmoon Card, drawn as a chest's.
    if (roll_chance_f(BONK_LEY_CARD_CHANCE) && !sBonkMgr.GetCards().empty())
    {
        std::vector<Offer> const none;
        std::vector<Offer> drawn;
        if (OfferCards(1, nullptr, &none, &drawn) && !drawn.empty())
        {
            LeyBuff card;
            card.cardId = drawn.front().id;
            card.rarity = drawn.front().rarity;
            m_leyOffer.back() = card;
        }
    }

    SetPaused(true);
    SendLeyOffer();
}

void BonkRun::SendLeyOffer() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (bonk->IsAddonReady())
    {
        // leyoffer~count, then leyopt~index~name~icon~description~rarity (a
        // Darkmoon Card adds ~c~copy~maxCopies), leyofferend.
        bonk->SendAddon("leyoffer~" + std::to_string(m_leyOffer.size()));
        for (size_t i = 0; i < m_leyOffer.size(); ++i)
        {
            LeyBuff const& buff = m_leyOffer[i];
            if (buff.cardId)
            {
                // ~c~copy~maxCopies: the addon dresses it as a Darkmoon Card.
                if (BonkCard const* card = sBonkMgr.GetCard(buff.cardId))
                    bonk->SendAddon("leyopt~" + std::to_string(i + 1) + "~" + card->name + "~" + card->icon + "~" + card->description + "~" +
                        std::to_string(buff.rarity) + "~c~" + std::to_string(GetCardCopies(card->id) + 1) + "~" + std::to_string(uint32(card->maxCopies)));
                continue;
            }
            LeyStat const* stat = GetLeyStat(buff.effect);
            if (!stat)
                continue;
            std::string icon = stat->icon;
            for (auto const& itr : sBonkMgr.GetPassives())
                if (itr.second.effect == buff.effect && !itr.second.icon.empty())
                {
                    icon = itr.second.icon;
                    break;
                }
            bonk->SendAddon("leyopt~" + std::to_string(i + 1) + "~" + stat->name + "~" + icon + "~" +
                FormatLeyValue(*stat, buff.value) + " for the rest of the run." + "~" + std::to_string(buff.rarity));
        }
        bonk->SendAddon("leyofferend");
        return;
    }

    ChatHandler handler(m_player);
    handler.SendSysMessage("Ley Line! Choose a blessing with .bonk leypick <number>:");
    for (size_t i = 0; i < m_leyOffer.size(); ++i)
    {
        if (m_leyOffer[i].cardId)
        {
            if (BonkCard const* card = sBonkMgr.GetCard(m_leyOffer[i].cardId))
                handler.PSendSysMessage("%u. %s: %s", uint32(i + 1), card->name.c_str(), card->description.c_str());
        }
        else if (LeyStat const* stat = GetLeyStat(m_leyOffer[i].effect))
            handler.PSendSysMessage("%u. %s", uint32(i + 1), FormatLeyValue(*stat, m_leyOffer[i].value).c_str());
    }
}

bool BonkRun::PickLeyBuff(uint32 index, std::string& error)
{
    if (m_leyOffer.empty())
    {
        error = "No Ley Line blessing to choose.";
        return false;
    }
    if (!index || index > m_leyOffer.size())
    {
        error = "Choose 1 to " + std::to_string(m_leyOffer.size()) + ".";
        return false;
    }

    LeyBuff const buff = m_leyOffer[index - 1];
    m_leyOffer.clear();
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("leyofferclose");
    if (buff.cardId)
    {
        if (BonkCard const* card = sBonkMgr.GetCard(buff.cardId))
        {
            AddCard(*card);
            bonk->Notify("Ley Line: " + card->name + "!");
        }
    }
    else
    {
        m_shrineBonuses[buff.effect] += buff.value;
        ApplyPassives();
        if (LeyStat const* stat = GetLeyStat(buff.effect))
            bonk->Notify("Ley Line: " + FormatLeyValue(*stat, buff.value) + "!");
    }
    if (m_offer.empty())
        SetPaused(false);
    SendState();
    return true;
}

uint32 BonkRun::GetGreedRewardPercent() const
{
    return uint32(BONK_GREED_REWARD * m_greedStacks);
}

float BonkRun::GetGreedCrowdPercent() const
{
    return BONK_GREED_CROWD * m_greedStacks * (m_greedStacks + 1) / 2.0f;
}

void BonkRun::SendGreed() const
{
    m_player->GetBonkPlayer()->SendAddon("greed~" + std::to_string(m_greedStacks) + "~" + std::to_string(GetGreedRewardPercent()) + "~" +
        std::to_string(uint32(GetGreedCrowdPercent() + 0.5f)));
}

void BonkRun::UseShrine(GameObject* go)
{
    if (m_state != STATE_RUNNING || m_paused)
        return;

    for (size_t i = 0; i < m_shrines.size(); ++i)
    {
        Shrine& shrine = m_shrines[i];
        if (shrine.model != go->GetObjectGuid())
            continue;

        BonkPlayer* bonk = m_player->GetBonkPlayer();
        switch (shrine.type)
        {
            case BONK_SHRINE_GREED:
            {
                ++m_greedStacks;
                SendGreed();
                bonk->Notify("Greed Totem: +" + std::to_string(GetGreedRewardPercent()) + "% gold and Darkmoon Tickets, +" +
                    std::to_string(uint32(GetGreedCrowdPercent() + 0.5f)) + "% enemies.");
                RemoveShrine(i);
                break;
            }
            case BONK_SHRINE_ALTAR:
            {
                if (shrine.active)
                    break;
                shrine.active = true;
                for (uint32 n = 0; n < BONK_ALTAR_PACK; ++n)
                {
                    BonkMapCreature const* creature = PickCreature(BONK_SPAWN_ELITE);
                    if (!creature)
                        break;
                    float const angle = 2.0f * M_PI_F * n / BONK_ALTAR_PACK;
                    float x = shrine.spot.x + BONK_ALTAR_PACK_SPREAD * std::cos(angle);
                    float y = shrine.spot.y + BONK_ALTAR_PACK_SPREAD * std::sin(angle);
                    float z = m_player->GetMap()->GetHeight(x, y, shrine.spot.z + 5.0f, true, 15.0f);
                    if (z <= INVALID_HEIGHT)
                    {
                        x = shrine.spot.x;
                        y = shrine.spot.y;
                        z = shrine.spot.z;
                    }
                    BonkMobAI::SetNextAffixCount(BONK_ALTAR_AFFIXES);
                    if (Creature* mob = SpawnMobAt(*creature, x, y, z))
                        shrine.pack.push_back(mob->GetObjectGuid());
                    BonkMobAI::SetNextAffixCount(0);
                }
                bonk->Notify("The altar's guardians awaken!");
                break;
            }
            case BONK_SHRINE_MOONWELL:
            {
                if (shrine.active)
                    break;
                shrine.active = true;
                shrine.leftMs = GetMoonwellSeconds() * IN_MILLISECONDS;
                shrine.tickMs = IN_MILLISECONDS;    // the first heal at once
                bonk->Notify("The Moonwell's waters stir!");
                break;
            }
            case BONK_SHRINE_STONE:
            {
                if (m_bossSpawned)
                {
                    bonk->Notify("The boss is already here.");
                    break;
                }
                BonkMapCreature const* creature = PickCreature(BONK_SPAWN_BOSS);
                if (!creature)
                    break;
                float const angle = std::atan2(m_player->GetPositionY() - shrine.spot.y, m_player->GetPositionX() - shrine.spot.x) + M_PI_F;
                float x = shrine.spot.x + BONK_STONE_BOSS_DISTANCE * std::cos(angle);
                float y = shrine.spot.y + BONK_STONE_BOSS_DISTANCE * std::sin(angle);
                float z = m_player->GetMap()->GetHeight(x, y, shrine.spot.z + 5.0f, true, 15.0f);
                if (z <= INVALID_HEIGHT)
                {
                    x = shrine.spot.x;
                    y = shrine.spot.y;
                    z = shrine.spot.z;
                }
                // Not the stage's boss: its death opens no portal.
                ObjectGuid const stageBoss = m_bossGuid;
                Creature* boss = SpawnMobAt(*creature, x, y, z);
                m_bossGuid = stageBoss;
                if (!boss)
                    break;
                bonk->Notify(std::string(boss->GetName()) + " answers the summons!");
                RemoveShrine(i);
                break;
            }
            default:
                break;
        }
        return;
    }
}

void BonkRun::UpdateAltar(size_t index)
{
    Shrine const& shrine = m_shrines[index];
    if (!shrine.active)
        return;

    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : shrine.pack)
        if (Creature* mob = map->GetCreature(guid))
            if (mob->IsAlive())
                return;

    // The guards are down: the altar gives up its chest.
    SpawnChest(true, shrine.spot.x, shrine.spot.y, shrine.spot.z, BONK_ALTAR_CHEST_CHOICES);
    m_player->GetBonkPlayer()->Notify("The Challenge Altar yields its treasure!");
    RemoveShrine(index);
}

void BonkRun::UpdateMoonwell(size_t index, uint32 diff)
{
    Shrine& shrine = m_shrines[index];
    bool const inside = DistanceSq(m_player->GetPositionX(), m_player->GetPositionY(), shrine.spot.x, shrine.spot.y) <=
        BONK_MOONWELL_RADIUS * BONK_MOONWELL_RADIUS;
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (!shrine.active)
        return;

    if (shrine.leftMs <= diff)
    {
        bonk->SendAddon("moonwell~0~0~" + std::to_string(GetMoonwellSeconds()));
        bonk->Notify("The Moonwell runs dry.");
        RemoveShrine(index);
        return;
    }
    shrine.leftMs -= diff;

    // The addon's bar shows only while the hero stands in it.
    if (!inside)
    {
        if (shrine.progressMs)
        {
            shrine.progressMs = 0;
            bonk->SendAddon("moonwell~0~0~" + std::to_string(GetMoonwellSeconds()));
        }
        return;
    }

    // Once a second inside: heal the hero, and tell the addon.
    bool const entered = !shrine.progressMs;
    shrine.progressMs = 1;          // the bar is up
    shrine.tickMs += diff;
    if (!entered && shrine.tickMs < IN_MILLISECONDS)
        return;
    if (shrine.tickMs >= IN_MILLISECONDS)
    {
        shrine.tickMs -= IN_MILLISECONDS;
        if (m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth())
            m_player->ModifyHealth(int32(ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * BONK_MOONWELL_HEAL)))));
    }
    bonk->SendAddon("moonwell~" + std::to_string((shrine.leftMs + IN_MILLISECONDS - 1) / IN_MILLISECONDS) + "~1~" +
        std::to_string(GetMoonwellSeconds()));
}

uint32 BonkRun::GetMoonwellSeconds() const
{
    // Attunement: its waters stir longer.
    return uint32(BONK_MOONWELL_SECONDS * (1.0f + GetUtility(BONK_UTILITY_SHRINE_SPEED) / 100.0f) + 0.5f);
}

bool BonkRun::IsInMoonwell(float x, float y) const
{
    for (Shrine const& shrine : m_shrines)
        if (shrine.type == BONK_SHRINE_MOONWELL && shrine.active &&
            DistanceSq(x, y, shrine.spot.x, shrine.spot.y) <= BONK_MOONWELL_RADIUS * BONK_MOONWELL_RADIUS)
            return true;
    return false;
}

void BonkRun::ResetShrines()
{
    DespawnShrines();
    ShrineCount const& count = BONK_SHRINE_COUNT[m_map.type < MAX_BONK_MAP_TYPE ? m_map.type : BONK_MAP_OPEN];
    m_shrinesToPlace = count.least + urand(0, count.extra);
    m_shrineTimer = 0;
    m_moonwellPlaced = false;
    m_stonePlaced = false;
    m_stoneAt = urand(1, std::max(1u, m_shrinesToPlace));
}

void BonkRun::DespawnShrines()
{
    if (Map* map = m_player->FindMap())
        for (Shrine const& shrine : m_shrines)
            if (GameObject* model = map->GetGameObject(shrine.model))
                model->Delete();
    m_shrines.clear();
    m_shrinesToPlace = 0;
    if (m_captureShown)
    {
        m_player->GetBonkPlayer()->SendAddon(std::string("capture~") + SHRINE_LABELS[BONK_SHRINE_LEY_LINE] + "~-1");
        m_captureShown = false;
    }
}

bool BonkRun::PlaceShrineNow(BonkShrineType type, std::string& error)
{
    if (m_state != STATE_RUNNING)
    {
        error = "Only a running stage can have a shrine.";
        return false;
    }
    if ((type == BONK_SHRINE_MOONWELL && m_moonwellPlaced) || (type == BONK_SHRINE_STONE && m_stonePlaced))
    {
        error = std::string("This stage already had its ") + SHRINE_LABELS[type] + ".";
        return false;
    }
    if (!PlaceShrine(type))
    {
        error = "No spawn point 60-400 yards away yet; try again in a few seconds.";
        return false;
    }
    return true;
}
