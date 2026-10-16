/*
 * Vanilla Bonk: Glyphs (feature plan, Phase 3: the run-defining picks).
 *
 * Two a run: 1 of 4 at its start, and 1 of 4 after its first mini-boss.
 * Each of the four rolls a rarity (the Glyph Attunement rank 0 odds, Luck
 * tipping them), then a Glyph of it, or of the nearest rarity, that the
 * account owns (the starter set, or bought at the Spellbook once its quest
 * is done) and the run has not taken; every offer holds at least two
 * without a drawback while there are. The run pauses while it is up.
 *
 * A Glyph may give a hidden Darkmoon Card (bonk_glyph.card_id) for its stats
 * or a card's effect (Windfury's echo, Hunter's Mark's big game, Scholar's
 * experience); the card row leaves those out. Its own effect is read where
 * it applies (BonkRun: experience, chests, power-ups, Ley Lines, kills,
 * hits, deaths) or here, the strikes on a timer.
 *
 * Addon: "glyphinfo~id~name~icon~rarity~description~drawback" for every
 * Glyph at hello (BonkPlayer::SendGlyphInfo); an offer is
 * "glyphoffer~pick~count", "glyphopt~index~id" for each, "glyphofferend";
 * "glyphclose" once chosen; "glyphs~id,id" the run's. `.bonk glyph <n>`
 * answers it.
 */

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

namespace
{
    uint32 const BONK_GLYPH_PICKS      = 2;     // a run's
    uint32 const BONK_GLYPH_CHOICES    = 4;
    uint32 const BONK_GLYPH_MIN_CLEAN  = 2;     // without a drawback, in every offer
    // Glyph Attunement rank 0 (feature plan), common to legendary; Luck adds
    // this share of itself (per 100 Luck) to each, the rarest the most.
    float const BONK_GLYPH_ODDS[MAX_BONK_RARITY] = { 55.0f, 30.0f, 11.0f, 3.5f, 0.5f };
    float const BONK_GLYPH_LUCK[MAX_BONK_RARITY] = { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f };

    // The strikes on a timer.
    float const BONK_GLYPH_STRIKE_RANGE  = 35.0f;   // about the screen
    uint32 const BONK_SPELL_GLYPH_STORM  = 403;     // Lightning Bolt: the combat log's name and school
    uint32 const BONK_SPELL_GLYPH_METEOR = 20476;   // Explosion
    uint32 const BONK_KIT_LIGHTNING      = 283;     // Lightning Bolt's impact
    uint32 const BONK_KIT_EXPLOSION      = 286;     // Explosion's impact, at the meteor's middle
    uint32 const BONK_KIT_FIRE_HIT       = 728;     // Fire Blast's impact, on the rest

    char const* const RARITY_NAMES[MAX_BONK_RARITY] = { "Common", "Uncommon", "Rare", "Epic", "Legendary" };
}

void BonkRun::QueueGlyphPick()
{
    if (m_glyphPicksGiven + m_pendingGlyphPicks >= BONK_GLYPH_PICKS || sBonkMgr.GetGlyphs().empty())
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

    // What the account owns and the run has not taken.
    uint32 const account = m_player->GetSession()->GetAccountId();
    std::vector<BonkGlyph const*> pool;
    for (auto const& itr : sBonkMgr.GetGlyphs())
    {
        BonkGlyph const& glyph = itr.second;
        if (sBonkQuests.IsLocked(account, BONK_REWARD_GLYPH, glyph.id) ||
            std::find(m_glyphs.begin(), m_glyphs.end(), &glyph) != m_glyphs.end())
            continue;
        pool.push_back(&glyph);
    }

    m_glyphOffer.clear();
    uint32 clean = 0;
    while (m_glyphOffer.size() < BONK_GLYPH_CHOICES && !pool.empty())
    {
        // The last slots go to Glyphs without a drawback until there are
        // enough of them, while any are left.
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

    // Without the addon: in chat, with the command that answers it.
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
        // Its card, out of sight of the card row.
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
        bonk->Notify(glyph->name + "!");
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
    // Vengeance: Power for health missing.
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

    // Second Wind: an elite or better falling close by heals.
    if (BonkGlyph const* wind = GetGlyph(BONK_GLYPH_SECOND_WIND))
        if (kind != BONK_SPAWN_NORMAL && m_player->IsWithinDistInMap(mob, wind->values[1]))
            HealHero(ReduceHealing(uint32(std::max(1.0f, m_player->GetMaxHealth() * wind->values[0] / 100.0f))));

    ++m_glyphKills;

    // Bloodlust: every so many kills, a burst of Haste.
    if (BonkGlyph const* lust = GetGlyph(BONK_GLYPH_KILL_HASTE))
        if (lust->values[0] >= 1.0f && m_glyphKills % uint32(lust->values[0]) == 0)
        {
            m_glyphHasteUntilMs = m_totalMs + uint32(lust->values[2]);
            bonk->Notify("Bloodlust!");
        }

    // Soul Harvest: every so many kills, the next hit is a big one.
    if (BonkGlyph const* harvest = GetGlyph(BONK_GLYPH_SOUL_HARVEST))
        if (!m_soulHarvestReady && harvest->values[0] >= 1.0f && m_glyphKills % uint32(harvest->values[0]) == 0)
        {
            m_soulHarvestReady = true;
            bonk->Notify("Soul Harvest: your next hit is empowered!");
        }
}

void BonkRun::ModifyGlyphHit(Unit* victim, uint32& damage)
{
    if (!m_soulHarvestReady || !damage)
        return;
    if (BonkGlyph const* harvest = GetGlyph(BONK_GLYPH_SOUL_HARVEST))
    {
        m_soulHarvestReady = false;
        damage = uint32(damage * harvest->values[1]);
        victim->SendPlaySpellVisualKit(BONK_KIT_EXPLOSION);
    }
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
    // Lightning on a few enemies about, every one a crit.
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
    // On the thickest crowd about: the enemy with the most others close by.
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
