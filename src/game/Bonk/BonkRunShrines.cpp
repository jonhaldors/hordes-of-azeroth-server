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
    uint32 const BONK_GO_SHRINE            = 900030;
    uint32 const BONK_SHRINE_MODEL_SEC     = 3600;
    uint32 const BONK_SHRINE_PLACE_MS      = 2000;
    float const BONK_SHRINE_MIN_DISTANCE   = 60.0f;
    float const BONK_SHRINE_MAX_DISTANCE   = 400.0f;
    float const BONK_SHRINE_SPREAD         = 150.0f;

    struct ShrineCount
    {
        uint32 least;
        uint32 extra;
    };
    ShrineCount const BONK_SHRINE_COUNT[MAX_BONK_MAP_TYPE] = { { 4, 2 }, { 3, 1 }, { 1, 1 } };
    uint32 const BONK_SHRINE_WEIGHTS[MAX_BONK_SHRINE] = { 35, 30, 20, 15, 0 };
    uint32 const BONK_MAX_LEY_LINES = 2;

    float const BONK_LEY_RADIUS            = 7.0f;
    char const* const BONK_LEY_LINE_TEXTURE = "World\\Goober\\RuneGroundBlue01.blp";

    std::string GetShrineMarkKey(uint32 markKey)
    {
        return "shrine" + std::to_string(markKey);
    }

    uint32 const BONK_LEY_CAPTURE_MS       = 8000;
    uint32 const BONK_LEY_SEND_MS          = 250;
    uint32 const BONK_LEY_CHOICES          = 3;
    float const BONK_LEY_CARD_CHANCE       = 35.0f;

    float const BONK_GREED_REWARD          = 5.0f;
    float const BONK_GREED_CROWD           = 5.0f;

    uint32 const BONK_ALTAR_PACK           = 3;
    uint32 const BONK_ALTAR_AFFIXES        = 2;
    uint32 const BONK_ALTAR_CHEST_CHOICES  = 5;
    float const BONK_ALTAR_PACK_SPREAD     = 6.0f;
    int32 const BONK_SUMMON_DELAY_MS       = 3000;
    int32 const BONK_SUMMON_GLOW_MS        = 1000;
    uint32 const BONK_KIT_SUMMON_CIRCLE    = 1485;
    uint32 const BONK_SPELL_SUMMON_SMOKE   = 10389;

    float const BONK_MOONWELL_RADIUS       = 10.0f;
    uint32 const BONK_MOONWELL_SECONDS     = 10;
    float const BONK_MOONWELL_HEAL         = 0.08f;

    float const BONK_STONE_BOSS_DISTANCE   = 8.0f;

    char const* const SHRINE_NAMES[MAX_BONK_SHRINE] = { "leyline", "greed", "altar", "moonwell", "stone" };
    char const* const SHRINE_LABELS[MAX_BONK_SHRINE] = { "Ley Line", "Greed Totem", "Challenge Altar", "Moonwell", "Summoning Stone" };
    char const* const SHRINE_ICONS[MAX_BONK_SHRINE] = {
        "Spell_Arcane_StarFire", "INV_Misc_Coin_02", "Ability_Warrior_Challange", "Spell_Arcane_TeleportMoonglade", "Spell_Shadow_Twilight" };
    char const* const SHRINE_DESCRIPTIONS[MAX_BONK_SHRINE] = {
        "Stand in its ring for 8 seconds to choose a blessing for the rest of the run.",
        "Click it to earn 5% more gold and Darkmoon Tickets for the rest of the run, but more enemies will come. Each totem you use adds more.",
        "Click it to face elite guards with two affixes. Beat them for a free chest.",
        "Click it to heal 8% of your health every second for 10 seconds while you stand in it. Enemies that die inside it give double experience.",
        "Click it to summon this floor's boss early. Defeat it to open the portal and skip the rest of the floor.",
    };

    struct LeyStat
    {
        BonkPassiveEffect effect;
        float value;
        char const* name;
        char const* phrase;
        char const* icon;
        bool points;
    };
    LeyStat const LEY_STATS[] = {
        { BONK_PASSIVE_POWER,       15.0f, "Power",                  "Power",                                   "Spell_Nature_Strength",       false },
        { BONK_PASSIVE_MAX_HEALTH,  15.0f, "Maximum Health",         "maximum health",                          "Spell_Holy_WordFortitude",    false },
        { BONK_PASSIVE_ARMOR,       10.0f, "Armor",                  "Armor",                                   "Spell_Holy_DevotionAura",     true },
        { BONK_PASSIVE_DODGE,        6.0f, "Dodge Chance",           "chance to dodge enemy swings",            "Spell_Shadow_ShadowWard",     false },
        { BONK_PASSIVE_CRIT,         8.0f, "Critical Strike Chance", "critical strike chance",                  "Ability_CriticalStrike",      false },
        { BONK_PASSIVE_CRIT_DAMAGE, 25.0f, "Critical Strike Damage", "critical strike damage",                  "Spell_Holy_PowerInfusion",    false },
        { BONK_PASSIVE_SHIELD,      15.0f, "Shield",                 "of your maximum health as a Shield",      "Spell_Holy_InnerFire",        false },
        { BONK_PASSIVE_HASTE,       10.0f, "Haste",                  "Haste",                                   "Racial_Troll_Berserk",        false },
        { BONK_PASSIVE_AREA,        20.0f, "Area",                   "Area: bigger AoE spells", "Spell_Holy_ArcaneIntellect", false },
        { BONK_PASSIVE_MOVE_SPEED,  10.0f, "Movement Speed",         "movement speed",                          "Ability_Mount_JungleTiger",   false },
        { BONK_PASSIVE_PICKUP,      40.0f, "Pickup Range",           "pickup range",                            "INV_Gizmo_02",                false },
        { BONK_PASSIVE_LUCK,        10.0f, "Luck",                   "Luck",                                    "INV_Misc_Foot_Kodo",          true },
    };
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
        char text[96];
        std::snprintf(text, sizeof(text), stat.points ? "+%.0f %s" : "+%.0f%% %s", value, stat.phrase);
        return text;
    }

    std::string LeyStatIcon(LeyStat const& stat)
    {
        for (auto const& itr : sBonkMgr.GetPassives())
            if (itr.second.effect == stat.effect && !itr.second.icon.empty())
                return itr.second.icon;
        return stat.icon;
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
    m_shrineTimer += diff;
    if (m_shrinesToPlace && m_shrineTimer >= BONK_SHRINE_PLACE_MS)
    {
        m_shrineTimer = 0;
        BonkShrineType const type = !m_stonePlaced && m_shrinesToPlace <= m_stoneAt ? BONK_SHRINE_STONE : PickShrineType();
        if (PlaceShrine(type))
            --m_shrinesToPlace;
    }

    UpdatePendingSummons(diff);

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
        if (m_shrines.size() == count)
            ++i;
    }
}

BonkShrineType BonkRun::PickShrineType() const
{
    auto const allowed = [this](uint8 type)
    {
        return type != BONK_SHRINE_STONE && (type != BONK_SHRINE_MOONWELL || !m_moonwellPlaced) &&
            (type != BONK_SHRINE_LEY_LINE || m_leyLinesPlaced < BONK_MAX_LEY_LINES);
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
    return BONK_SHRINE_GREED;
}

bool BonkRun::PlaceShrine(BonkShrineType type)
{
    if ((type == BONK_SHRINE_MOONWELL && m_moonwellPlaced) || (type == BONK_SHRINE_STONE && m_stonePlaced) ||
        (type == BONK_SHRINE_LEY_LINE && m_leyLinesPlaced >= BONK_MAX_LEY_LINES))
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
    if (type == BONK_SHRINE_LEY_LINE)
    {
        shrine.markKey = NextBonkAuraKey();
        SendBonkMark(m_player, GetShrineMarkKey(shrine.markKey), BONK_TELEGRAPH_CIRCLE, spot.x, spot.y, spot.z, frand(0.0f, 2.0f * M_PI_F), BONK_LEY_RADIUS, 0,
            true, BONK_TELEGRAPH_STYLE_ARCANE, ObjectGuid(), BONK_LEY_LINE_TEXTURE);
    }
    else if (GameObject* model = m_player->SummonGameObject(BONK_GO_SHRINE + type, spot.x, spot.y, spot.z, frand(0.0f, 2.0f * M_PI_F),
        0.0f, 0.0f, 0.0f, 0.0f, BONK_SHRINE_MODEL_SEC, false))
        shrine.model = model->GetObjectGuid();
    shrine.objectiveId = m_objectives.Add(m_player, BONK_OBJECTIVE_SHRINE, spot.x, spot.y, spot.z, SHRINE_LABELS[type], 0, SHRINE_ICONS[type],
        SHRINE_DESCRIPTIONS[type]);
    if (type == BONK_SHRINE_MOONWELL)
        m_moonwellPlaced = true;
    if (type == BONK_SHRINE_STONE)
        m_stonePlaced = true;
    if (type == BONK_SHRINE_LEY_LINE)
        ++m_leyLinesPlaced;
    m_shrines.push_back(shrine);
    return true;
}

void BonkRun::RemoveShrine(size_t index)
{
    Shrine const& shrine = m_shrines[index];
    if (shrine.markKey)
        SendBonkUnmark(m_player, GetShrineMarkKey(shrine.markKey));
    if (Map* map = m_player->FindMap())
        if (GameObject* model = map->GetGameObject(shrine.model))
            model->Delete();
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
        if (m_captureShown && shrine.progressMs)
        {
            bonk->SendAddon(std::string("capture~") + SHRINE_LABELS[BONK_SHRINE_LEY_LINE] + "~-1");
            m_captureShown = false;
        }
        return;
    }

    uint32 const captureMs = uint32(BONK_LEY_CAPTURE_MS / (1.0f + (GetUtility(BONK_UTILITY_SHRINE_SPEED) + GetGlyphValue(BONK_GLYPH_LEY_LINE, 0)) / 100.0f));
    shrine.progressMs += diff;
    m_captureSendMs += diff;
    if (shrine.progressMs >= captureMs)
    {
        bonk->SendAddon(std::string("capture~") + SHRINE_LABELS[BONK_SHRINE_LEY_LINE] + "~100");
        bonk->SendAddon(std::string("capture~") + SHRINE_LABELS[BONK_SHRINE_LEY_LINE] + "~-1");
        m_captureShown = false;
        bonk->Notify("You captured the Ley Line!");
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

    std::vector<LeyStat const*> pool;
    for (LeyStat const& stat : LEY_STATS)
    {
        if (IsStatCapped(stat.effect))
            continue;
        pool.push_back(&stat);
    }

    float const luck = std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
    float const epic = 4.0f + 0.2f * luck;
    float const rare = epic + 12.0f + 0.4f * luck;
    float const uncommon = rare + 30.0f + 0.4f * luck;

    m_leyOffer.clear();
    uint32 const choices = BONK_LEY_CHOICES + uint32(GetGlyphValue(BONK_GLYPH_LEY_LINE, 1));
    for (uint32 i = 0; i < choices && !pool.empty(); ++i)
    {
        size_t const pick = urand(0, uint32(pool.size()) - 1);
        LeyStat const* stat = pool[pick];
        pool.erase(pool.begin() + pick);

        float const roll = frand(0.0f, 100.0f);
        LeyBuff buff;
        buff.effect = stat->effect;
        buff.rarity = roll < epic ? BONK_RARITY_EPIC : roll < rare ? BONK_RARITY_RARE : roll < uncommon ? BONK_RARITY_UNCOMMON : BONK_RARITY_COMMON;
        buff.value = stat->value * LEY_RARITY_SCALE[buff.rarity] * GetCopyWeight(m_shrinePicks[stat->effect] + 1);
        m_leyOffer.push_back(buff);
    }
    if (m_leyOffer.empty())
        return;

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
        bonk->SendAddon("leyoffer~" + std::to_string(m_leyOffer.size()));
        for (size_t i = 0; i < m_leyOffer.size(); ++i)
        {
            LeyBuff const& buff = m_leyOffer[i];
            if (buff.cardId)
            {
                if (BonkCard const* card = sBonkMgr.GetCard(buff.cardId))
                    bonk->SendAddon("leyopt~" + std::to_string(i + 1) + "~" + card->name + "~" + card->icon + "~" + card->description + "~" +
                        std::to_string(buff.rarity) + "~c~" + std::to_string(GetCardCopies(card->id) + 1) + "~" + std::to_string(uint32(card->maxCopies)));
                continue;
            }
            LeyStat const* stat = GetLeyStat(buff.effect);
            if (!stat)
                continue;
            bonk->SendAddon("leyopt~" + std::to_string(i + 1) + "~" + stat->name + "~" + LeyStatIcon(*stat) + "~" +
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

void BonkRun::SendLeyBlessings() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("leyblessings");
    for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
    {
        LeyStat const* stat = GetLeyStat(BonkPassiveEffect(effect));
        if (stat && m_shrineBonuses[effect] > 0.0f)
            bonk->SendAddon("leybless~" + std::string(stat->name) + "~" + LeyStatIcon(*stat) + "~" + FormatLeyValue(*stat, m_shrineBonuses[effect]));
    }
    bonk->SendAddon("leyblessend");
}

bool BonkRun::PickLeyBuff(uint32 index, std::string& error)
{
    if (m_leyOffer.empty())
    {
        error = "There is no Ley Line blessing to choose right now.";
        return false;
    }
    if (!index || index > m_leyOffer.size())
    {
        error = "Choose a number from 1 to " + std::to_string(m_leyOffer.size()) + ".";
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
            bonk->Notify("The Ley Line grants you " + card->name + "!");
        }
    }
    else
    {
        m_shrineBonuses[buff.effect] += buff.value;
        ++m_shrinePicks[buff.effect];
        ApplyPassives();
        if (LeyStat const* stat = GetLeyStat(buff.effect))
            bonk->Notify("The Ley Line grants you " + FormatLeyValue(*stat, buff.value) + "!");
        SendLeyBlessings();
    }
    ResumeUnlessChoiceOpen();
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
                bonk->Notify("You now earn " + std::to_string(GetGreedRewardPercent()) + "% more gold and Darkmoon Tickets, but face " +
                    std::to_string(uint32(GetGreedCrowdPercent() + 0.5f)) + "% more enemies.");
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
                    QueueSummon(creature, x, y, z, BONK_ALTAR_AFFIXES, shrine.model, false, n ? "" : "The altar's guardians arrive");
                }
                break;
            }
            case BONK_SHRINE_MOONWELL:
            {
                if (shrine.active)
                    break;
                shrine.active = true;
                shrine.leftMs = GetMoonwellSeconds() * IN_MILLISECONDS;
                shrine.tickMs = IN_MILLISECONDS;
                bonk->Notify("The Moonwell's waters stir! Stand in them to heal.");
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
                QueueSummon(creature, x, y, z, 0, ObjectGuid(), true, "Something answers the summons. It arrives");
                RemoveShrine(i);
                break;
            }
            default:
                break;
        }
        return;
    }
}

void BonkRun::QueueSummon(BonkMapCreature const* creature, float x, float y, float z, uint32 affixes, ObjectGuid const& altar, bool boss,
    std::string const& countdown)
{
    PendingSummon summon;
    summon.creature = creature;
    summon.x = x;
    summon.y = y;
    summon.z = z;
    summon.affixes = affixes;
    summon.altar = altar;
    summon.boss = boss;
    summon.leftMs = BONK_SUMMON_DELAY_MS;
    summon.countdown = countdown;
    m_pendingSummons.push_back(summon);
}

bool BonkRun::IsAltarWaking(ObjectGuid const& altar) const
{
    return std::any_of(m_pendingSummons.begin(), m_pendingSummons.end(), [&altar](PendingSummon const& summon) { return summon.altar == altar; });
}

void BonkRun::UpdatePendingSummons(uint32 diff)
{
    std::vector<PendingSummon> ready;
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    for (auto itr = m_pendingSummons.begin(); itr != m_pendingSummons.end();)
    {
        itr->glowMs -= int32(diff);
        if (itr->glowMs <= 0 && itr->leftMs > 0)
        {
            itr->glowMs = BONK_SUMMON_GLOW_MS;
            PlayKitAt(ObjectGuid(), itr->x, itr->y, itr->z, BONK_KIT_SUMMON_CIRCLE, true);
            int32 const seconds = (itr->leftMs + IN_MILLISECONDS - 1) / IN_MILLISECONDS;
            if (!itr->countdown.empty())
                bonk->Notify(itr->countdown + " in " + std::to_string(seconds) + (seconds == 1 ? " second." : " seconds."));
        }
        itr->leftMs -= int32(diff);
        if (itr->leftMs > 0)
        {
            ++itr;
            continue;
        }
        ready.push_back(*itr);
        itr = m_pendingSummons.erase(itr);
    }

    for (PendingSummon const& summon : ready)
    {
        BonkMobAI::SetNextAffixCount(summon.affixes);
        if (summon.boss)
            m_scalingMinuteOverride = std::max(GetRunMinutes(), m_map.durationSeconds / 60.0f);
        Creature* mob = SpawnMobAt(*summon.creature, summon.x, summon.y, summon.z);
        m_scalingMinuteOverride = -1.0f;
        BonkMobAI::SetNextAffixCount(0);
        if (!mob)
            continue;
        if (summon.boss)
        {
            m_bossSpawned = true;
            m_stoneBoss = true;
        }
        mob->CastSpell(mob, BONK_SPELL_SUMMON_SMOKE, true);
        if (summon.boss)
            bonk->Notify(std::string(mob->GetName()) + " answers the summons!");
        if (summon.altar.IsEmpty())
            continue;
        for (Shrine& shrine : m_shrines)
            if (shrine.model == summon.altar)
            {
                shrine.pack.push_back(mob->GetObjectGuid());
                if (!IsAltarWaking(summon.altar) && shrine.pack.size() == 1)
                    bonk->Notify("The altar's guardians awaken!");
            }
    }
}

void BonkRun::UpdateAltar(size_t index)
{
    Shrine const& shrine = m_shrines[index];
    if (!shrine.active || IsAltarWaking(shrine.model))
        return;

    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : shrine.pack)
        if (Creature* mob = map->GetCreature(guid))
            if (mob->IsAlive())
                return;

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

    if (!inside)
    {
        if (shrine.progressMs)
        {
            shrine.progressMs = 0;
            bonk->SendAddon("moonwell~0~0~" + std::to_string(GetMoonwellSeconds()));
        }
        return;
    }

    bool const entered = !shrine.progressMs;
    shrine.progressMs = 1;
    shrine.tickMs += diff;
    if (!entered && shrine.tickMs < IN_MILLISECONDS)
        return;
    if (shrine.tickMs >= IN_MILLISECONDS)
    {
        shrine.tickMs -= IN_MILLISECONDS;
        if (m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth())
        {
            SupportScope scope(*this, { "Spell_Nature_Rejuvenation", "Moonwell" });
            m_player->ModifyHealth(int32(ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * BONK_MOONWELL_HEAL)))));
        }
    }
    bonk->SendAddon("moonwell~" + std::to_string((shrine.leftMs + IN_MILLISECONDS - 1) / IN_MILLISECONDS) + "~1~" +
        std::to_string(GetMoonwellSeconds()));
}

uint32 BonkRun::GetMoonwellSeconds() const
{
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
    m_leyLinesPlaced = 0;
    m_stonePlaced = false;
    m_stoneAt = urand(1, std::max(1u, m_shrinesToPlace));
}

void BonkRun::DespawnShrines()
{
    Map* map = m_player->FindMap();
    for (Shrine const& shrine : m_shrines)
    {
        if (shrine.markKey)
            SendBonkUnmark(m_player, GetShrineMarkKey(shrine.markKey));
        if (map)
            if (GameObject* model = map->GetGameObject(shrine.model))
                model->Delete();
        RemoveObjective(shrine.objectiveId);
    }
    m_shrines.clear();
    m_pendingSummons.clear();
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
        error = "Only a running floor can have a shrine.";
        return false;
    }
    if ((type == BONK_SHRINE_MOONWELL && m_moonwellPlaced) || (type == BONK_SHRINE_STONE && m_stonePlaced))
    {
        error = std::string("This floor has already had its ") + SHRINE_LABELS[type] + ".";
        return false;
    }
    if (type == BONK_SHRINE_LEY_LINE && m_leyLinesPlaced >= BONK_MAX_LEY_LINES)
    {
        error = "This floor has already had " + std::to_string(BONK_MAX_LEY_LINES) + " Ley Lines.";
        return false;
    }
    if (!PlaceShrine(type))
    {
        error = "No spawn point 60-400 yards away yet; try again in a few seconds.";
        return false;
    }
    return true;
}
