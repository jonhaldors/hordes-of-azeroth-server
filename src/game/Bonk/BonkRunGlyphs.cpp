#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "Chat.h"
#include "Creature.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <cstdio>

namespace
{
    uint32 const BONK_GLYPH_PICKS      = 2;
    uint32 const BONK_GLYPH_CHOICES    = 4;
    uint32 const BONK_GLYPH_MIN_CLEAN  = 2;
    float const BONK_GLYPH_ODDS[MAX_BONK_RARITY] = { 55.0f, 30.0f, 11.0f, 3.5f, 0.5f };
    float const BONK_GLYPH_LUCK[MAX_BONK_RARITY] = { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f };

    float const BONK_GLYPH_STRIKE_RANGE  = 35.0f;
    uint32 const BONK_SPELL_GLYPH_STORM  = 403;
    uint32 const BONK_SPELL_GLYPH_METEOR = 20476;
    uint32 const BONK_KIT_LIGHTNING      = 283;
    uint32 const BONK_KIT_EXPLOSION      = 286;
    uint32 const BONK_KIT_FIRE_HIT       = 728;
    uint32 const BONK_KIT_SOUL_HARVEST   = 219;

    char const* const RARITY_NAMES[MAX_BONK_RARITY] = { "Common", "Uncommon", "Rare", "Epic", "Legendary" };

    std::string FormatTimes(float times)
    {
        uint32 const whole = uint32(times + 0.5f);
        if (whole == 2)
            return "double";
        if (whole == 3)
            return "triple";
        return std::to_string(whole) + " times your normal";
    }
}

void BonkRun::QueueGlyphPick()
{
    uint32 const picks = BONK_GLYPH_PICKS + (GetUtility(BONK_UTILITY_START_GLYPH) > 0.0f ? 1 : 0);
    if (m_glyphPicksGiven + m_pendingGlyphPicks >= picks || sBonkMgr.GetGlyphs().empty())
        return;
    ++m_pendingGlyphPicks;
    UpdateOffers();
}

BonkRarity BonkRun::RollGlyphRarity() const
{
    float const luck = std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
    float weights[MAX_BONK_RARITY];
    float total = 0.0f;
    for (uint8 rarity = 0; rarity < MAX_BONK_RARITY; ++rarity)
    {
        weights[rarity] = BONK_GLYPH_ODDS[rarity] * (1.0f + BONK_GLYPH_LUCK[rarity] * luck / 100.0f);
        total += weights[rarity];
    }
    float roll = frand(0.0f, total);
    for (uint8 rarity = 0; rarity < MAX_BONK_RARITY; ++rarity)
    {
        if (roll < weights[rarity])
            return BonkRarity(rarity);
        roll -= weights[rarity];
    }
    return BONK_RARITY_COMMON;
}

void BonkRun::OfferGlyphs()
{
    --m_pendingGlyphPicks;

    uint32 const account = m_player->GetSession()->GetAccountId();
    std::vector<BonkGlyph const*> pool;
    for (auto const& itr : sBonkMgr.GetGlyphs())
    {
        BonkGlyph const& glyph = itr.second;
        if (!sBonkQuests.IsInPool(account, BONK_REWARD_GLYPH, glyph.id) ||
            std::find(m_glyphs.begin(), m_glyphs.end(), &glyph) != m_glyphs.end())
            continue;
        pool.push_back(&glyph);
    }

    m_glyphOffer.clear();
    uint32 clean = 0;
    while (m_glyphOffer.size() < BONK_GLYPH_CHOICES && !pool.empty())
    {
        uint32 const slotsLeft = BONK_GLYPH_CHOICES - uint32(m_glyphOffer.size());
        bool const needClean = clean < BONK_GLYPH_MIN_CLEAN && slotsLeft <= BONK_GLYPH_MIN_CLEAN - clean &&
            std::any_of(pool.begin(), pool.end(), [](BonkGlyph const* glyph) { return glyph->drawback.empty(); });
        BonkRarity const rolled = RollGlyphRarity();
        std::vector<size_t> picks;
        for (int32 distance = 0; distance < MAX_BONK_RARITY && picks.empty(); ++distance)
            for (size_t i = 0; i < pool.size(); ++i)
                if (std::abs(int32(pool[i]->rarity) - int32(rolled)) == distance && (!needClean || pool[i]->drawback.empty()))
                    picks.push_back(i);
        if (picks.empty())
            break;
        size_t const chosen = picks[urand(0, uint32(picks.size()) - 1)];
        if (pool[chosen]->drawback.empty())
            ++clean;
        m_glyphOffer.push_back(pool[chosen]->id);
        pool.erase(pool.begin() + chosen);
    }

    ++m_glyphPicksGiven;
    if (m_glyphOffer.empty())
        return;
    SetPaused(true);
    SendGlyphOffer();
}

void BonkRun::SendGlyphOffer() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (bonk->IsAddonReady())
    {
        bonk->SendAddon("glyphoffer~" + std::to_string(m_glyphPicksGiven) + "~" + std::to_string(m_glyphOffer.size()));
        for (size_t i = 0; i < m_glyphOffer.size(); ++i)
            bonk->SendAddon("glyphopt~" + std::to_string(i + 1) + "~" + std::to_string(m_glyphOffer[i]));
        bonk->SendAddon("glyphofferend");
        return;
    }

    ChatHandler handler(m_player);
    handler.SendSysMessage(m_glyphPicksGiven > 1 ? "Choose your second Glyph (.bonk glyph <number>):" : "Choose a Glyph (.bonk glyph <number>):");
    for (size_t i = 0; i < m_glyphOffer.size(); ++i)
        if (BonkGlyph const* glyph = sBonkMgr.GetGlyph(m_glyphOffer[i]))
            handler.PSendSysMessage("  %u) %s (%s): %s%s", uint32(i + 1), glyph->name.c_str(), RARITY_NAMES[glyph->rarity], glyph->description.c_str(),
                glyph->drawback.empty() ? "" : (" Drawback: " + glyph->drawback).c_str());
}

bool BonkRun::ChooseGlyph(uint32 choice, std::string& error)
{
    if (m_glyphOffer.empty())
    {
        error = "There is no Glyph to choose right now.";
        return false;
    }
    if (!choice || choice > m_glyphOffer.size())
    {
        error = "Choose a Glyph from 1 to " + std::to_string(m_glyphOffer.size()) + ".";
        return false;
    }

    BonkGlyph const* glyph = sBonkMgr.GetGlyph(m_glyphOffer[choice - 1]);
    m_glyphOffer.clear();
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("glyphclose");
    if (glyph)
    {
        m_glyphs.push_back(glyph);
        if (BonkCard const* card = glyph->cardId ? sBonkMgr.GetCard(glyph->cardId) : nullptr)
        {
            m_glyphCardIds.insert(card->id);
            AddCard(*card);
        }
        switch (glyph->effect)
        {
            case BONK_GLYPH_GAMBLER:
                m_rerolls = 0;
                break;
            case BONK_GLYPH_STORM:
                m_glyphStormTimer = int32(glyph->values[0]);
                break;
            case BONK_GLYPH_METEOR:
                m_glyphMeteorTimer = int32(glyph->values[0]);
                break;
            default:
                break;
        }
        ApplyPassives();
        bonk->Notify("You chose " + glyph->name + ".");
        SendGlyphs();
        SendState();
    }

    SetPaused(false);
    UpdateOffers();
    return true;
}

void BonkRun::SendGlyphs() const
{
    std::string ids;
    for (BonkGlyph const* glyph : m_glyphs)
        ids += (ids.empty() ? "" : ",") + std::to_string(glyph->id);
    m_player->GetBonkPlayer()->SendAddon("glyphs~" + ids);
}

BonkGlyph const* BonkRun::GetGlyph(BonkGlyphEffect effect) const
{
    for (BonkGlyph const* glyph : m_glyphs)
        if (glyph->effect == effect)
            return glyph;
    return nullptr;
}

float BonkRun::GetGlyphValue(BonkGlyphEffect effect, uint8 index) const
{
    BonkGlyph const* glyph = GetGlyph(effect);
    return glyph && index < 3 ? glyph->values[index] : 0.0f;
}

float BonkRun::GetGlyphPower() const
{
    BonkGlyph const* vengeance = GetGlyph(BONK_GLYPH_VENGEANCE);
    if (!vengeance || vengeance->values[0] <= 0.0f || !m_player->GetMaxHealth())
        return 0.0f;
    float const missing = 100.0f - m_player->GetHealthPercent();
    return std::max(0.0f, missing / vengeance->values[0]);
}

void BonkRun::OnGlyphKill(Creature const* mob, BonkSpawnKind kind)
{
    if (m_glyphs.empty())
        return;
    BonkPlayer* bonk = m_player->GetBonkPlayer();

    if (BonkGlyph const* wind = GetGlyph(BONK_GLYPH_SECOND_WIND))
        if (kind != BONK_SPAWN_NORMAL && m_player->IsWithinDistInMap(mob, wind->values[1]))
            HealHero(ReduceHealing(uint32(std::max(1.0f, m_player->GetMaxHealth() * wind->values[0] / 100.0f))));

    ++m_glyphKills;

    if (BonkGlyph const* lust = GetGlyph(BONK_GLYPH_KILL_HASTE))
        if (lust->values[0] >= 1.0f && m_glyphKills % uint32(lust->values[0]) == 0)
        {
            m_glyphHasteUntilMs = m_totalMs + uint32(lust->values[2]);
            char text[96];
            snprintf(text, sizeof(text), "Bloodlust! You attack and dots tick %g%% more often for %g seconds.", lust->values[1],
                lust->values[2] / 1000.0f);
            bonk->Notify(text);
        }

    if (BonkGlyph const* harvest = GetGlyph(BONK_GLYPH_SOUL_HARVEST))
        if (harvest->values[0] >= 1.0f && m_glyphKills % uint32(harvest->values[0]) == 0)
        {
            m_soulHarvestUntilMs = m_totalMs + uint32(harvest->values[2]);
            m_player->SendPlaySpellVisualKit(BONK_KIT_SOUL_HARVEST);
            bonk->Notify("Soul Harvest! You deal " + FormatTimes(harvest->values[1]) + " damage for " +
                std::to_string(uint32(harvest->values[2] / IN_MILLISECONDS + 0.5f)) + " seconds.");
        }
}

void BonkRun::ModifyGlyphHit(Unit* , uint32& damage)
{
    if (m_totalMs >= m_soulHarvestUntilMs || !damage)
        return;
    if (BonkGlyph const* harvest = GetGlyph(BONK_GLYPH_SOUL_HARVEST))
        damage = uint32(damage * harvest->values[1]);
}

void BonkRun::UpdateGlyphs(uint32 diff)
{
    if (BonkGlyph const* storm = GetGlyph(BONK_GLYPH_STORM))
    {
        m_glyphStormTimer -= int32(diff);
        if (m_glyphStormTimer <= 0)
        {
            m_glyphStormTimer = int32(storm->values[0]);
            StrikeStorm(*storm);
        }
    }
    if (BonkGlyph const* meteor = GetGlyph(BONK_GLYPH_METEOR))
    {
        m_glyphMeteorTimer -= int32(diff);
        if (m_glyphMeteorTimer <= 0)
        {
            m_glyphMeteorTimer = int32(meteor->values[0]);
            DropMeteor(*meteor);
        }
    }
}

void BonkRun::StrikeStorm(BonkGlyph const& glyph)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_GLYPH_STORM);
    std::vector<Creature*> mobs = GetMobsInRange(BONK_GLYPH_STRIKE_RANGE);
    if (!spellInfo || mobs.empty())
        return;
    for (size_t i = mobs.size(); i > 1; --i)
        std::swap(mobs[i - 1], mobs[urand(0, uint32(i) - 1)]);
    float const damage = GetSpawnHealth(BONK_SPAWN_NORMAL, BONK_ARCHETYPE_GRUNT, 1.0f) * glyph.values[2];
    for (size_t i = 0; i < mobs.size() && i < size_t(glyph.values[1]); ++i)
    {
        mobs[i]->SendPlaySpellVisualKit(BONK_KIT_LIGHTNING);
        DealSpellHit(spellInfo, mobs[i], damage, nullptr, true);
    }
}

void BonkRun::DropMeteor(BonkGlyph const& glyph)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_GLYPH_METEOR);
    std::vector<Creature*> const mobs = GetMobsInRange(BONK_GLYPH_STRIKE_RANGE);
    if (!spellInfo || mobs.empty())
        return;
    float const radius = glyph.values[2];
    Creature* centre = nullptr;
    size_t best = 0;
    for (Creature* mob : mobs)
    {
        size_t const crowd = size_t(std::count_if(mobs.begin(), mobs.end(), [mob, radius](Creature const* other)
            { return other->GetDistance2d(mob) <= radius; }));
        if (!centre || crowd > best)
        {
            centre = mob;
            best = crowd;
        }
    }

    float const damage = GetSpawnHealth(BONK_SPAWN_NORMAL, BONK_ARCHETYPE_GRUNT, 1.0f) * glyph.values[1];
    centre->SendPlaySpellVisualKit(BONK_KIT_EXPLOSION);
    std::vector<Creature*> struck;
    for (Creature* mob : mobs)
        if (mob->GetDistance2d(centre) <= radius)
            struck.push_back(mob);
    for (Creature* mob : struck)
    {
        if (mob != centre)
            mob->SendPlaySpellVisualKit(BONK_KIT_FIRE_HIT);
        DealSpellHit(spellInfo, mob, damage);
    }
}
