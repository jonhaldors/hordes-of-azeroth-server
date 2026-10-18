#include "BonkRun.h"
#include "BonkMgr.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Utilities/Random.h"

#include <cmath>
#include <string>

namespace
{
    float const BONK_DAMAGE_SPREAD = 0.07f;
    float const BONK_PET_SWING_SPREAD = 0.1f;

    char const* GetSchoolName(SpellEntry const* spellInfo)
    {
        switch (spellInfo ? spellInfo->School : SPELL_SCHOOL_NORMAL)
        {
            case SPELL_SCHOOL_HOLY:   return "Holy";
            case SPELL_SCHOOL_FIRE:   return "Fire";
            case SPELL_SCHOOL_NATURE: return "Nature";
            case SPELL_SCHOOL_FROST:  return "Frost";
            case SPELL_SCHOOL_SHADOW: return "Shadow";
            case SPELL_SCHOOL_ARCANE: return "Arcane";
            default:                  return "Physical";
        }
    }

    std::string DescribeRange(float hit, float spread)
    {
        uint32 const low = uint32(std::max(1.0f, std::floor(hit * (1.0f - spread))));
        uint32 const high = uint32(std::max(1.0f, std::ceil(hit * (1.0f + spread))));
        return low == high ? std::to_string(low) : std::to_string(low) + " to " + std::to_string(high);
    }

    bool IsTickingKeyword(BonkKeyword keyword)
    {
        return keyword == BONK_KEYWORD_BURNING || keyword == BONK_KEYWORD_BLEED || keyword == BONK_KEYWORD_POISON ||
            keyword == BONK_KEYWORD_DECAY;
    }
}

float BonkRun::GetShownSpread(OwnedWeapon const& owned) const
{
    return owned.weapon.behavior == BONK_BEHAVIOR_PET ? BONK_PET_SWING_SPREAD : BONK_DAMAGE_SPREAD;
}

void BonkRun::FloatSummonDamage(Unit* victim, uint32 damage, SpellEntry const* spellInfo) const
{
    SpellNonMeleeDamage shown(m_player, victim, spellInfo->Id, SpellSchools(spellInfo->School));
    shown.damage = damage;
    m_player->SendSpellNonMeleeDamageLog(&shown);
}

float BonkRun::RollDamageSpread() const
{
    return frand(1.0f - BONK_DAMAGE_SPREAD, 1.0f + BONK_DAMAGE_SPREAD);
}

void BonkRun::AddRankGains(OwnedWeapon& owned, Offer const& offer) const
{
    owned.damageBonus += BONK_RANK_DAMAGE[offer.rarity];
    for (uint8 i = 0; i < BONK_RANK_UPGRADES[offer.rarity] && i < BONK_MAX_RANK_UPGRADES; ++i)
    {
        float const step = owned.weapon.GetUpgradeStep(offer.upgrades[i]);
        if (step > 0.0f)
            owned.upgrades[offer.upgrades[i]] += step;
        else
            owned.damageBonus += BONK_RANK_SPARE_DAMAGE;
    }
}

float BonkRun::GetShownHit(OwnedWeapon const& owned, float extraPower) const
{
    if (owned.weapon.damageFraction <= 0.0f)
        return 0.0f;

    if (owned.weapon.behavior == BONK_BEHAVIOR_PET)
    {
        BonkPetFamily const* family = GetPetFamily(owned);
        if (!family)
            return 0.0f;
        float const powerShare = BONK_SUMMON_POWER_SHARE + GetPackPowerShare();
        return GetPetBiteDamage(owned, *family) * GetPetSwingFactor(owned) * GetDamageMultiplier(nullptr, nullptr, powerShare, extraPower) *
            (1.0f + GetSpecDamage(owned.weapon.id) / 100.0f);
    }

    return GetWeaponDamage(owned) * GetDamageMultiplier(sSpellMgr.GetSpellEntry(owned.weapon.spellId), nullptr, 1.0f, extraPower);
}

float BonkRun::GetPetSwingFactor(OwnedWeapon const& owned) const
{
    if (owned.pets.empty())
        return 1.0f;
    for (OwnedWeapon const& held : m_weapons)
    {
        if (held.pets.empty() || held.pets.front() != owned.pets.front())
            continue;
        BonkPetFamily const* family = GetPetFamily(held);
        Creature const* pet = family ? m_player->GetMap()->GetCreature(held.pets.front()) : nullptr;
        float const bite = family ? GetPetBiteDamage(held, *family) : 0.0f;
        if (!pet || bite <= 0.0f)
            return 1.0f;
        float const swing = (pet->GetFloatValue(UNIT_FIELD_MINDAMAGE) + pet->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2.0f;
        float const frenzy = IsPetFrenzied() ? GetFrenzyDamage() : 1.0f;
        return swing > 0.0f ? swing / (bite * frenzy) : 1.0f;
    }
    return 1.0f;
}

float BonkRun::GetShownTick(OwnedWeapon const& owned, float extraPower) const
{
    if (!IsTickingKeyword(owned.weapon.applies) || !owned.weapon.applyStacks)
        return 0.0f;
    BonkKeywordInfo const* info = sBonkMgr.GetKeyword(owned.weapon.applies);
    if (!info)
        return 0.0f;
    float const scale = 1.0f + (owned.damageBonus + GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_PET_DAMAGE)) / 100.0f;
    float const points = std::max(1.0f, std::round(GetKeywordPoints(*info) * scale));
    return points * GetDamageMultiplier(sSpellMgr.GetSpellEntry(info->spellId), nullptr, 1.0f, extraPower);
}

std::string BonkRun::DescribeShownRange(OwnedWeapon const& owned, float extraPower) const
{
    if (float const hit = GetShownHit(owned, extraPower))
        return DescribeRange(hit, GetShownSpread(owned));
    if (float const tick = GetShownTick(owned, extraPower))
        return std::to_string(uint32(std::max(1.0f, std::round(tick)))) + " a second per stack";
    return "";
}

std::string BonkRun::DescribeShownDamage(OwnedWeapon const& owned) const
{
    std::string text;
    if (float const hit = GetShownHit(owned, 0.0f))
    {
        if (owned.weapon.behavior == BONK_BEHAVIOR_PET)
            text = "Its attacks deal " + DescribeRange(hit, BONK_PET_SWING_SPREAD) + " damage";
        else
            text = DescribeRange(hit, BONK_DAMAGE_SPREAD) + " " + GetSchoolName(sSpellMgr.GetSpellEntry(owned.weapon.spellId)) + " damage";
    }
    if (float const tick = GetShownTick(owned, 0.0f))
        if (BonkKeywordInfo const* info = sBonkMgr.GetKeyword(owned.weapon.applies))
            text += (text.empty() ? "" : ", ") + std::to_string(uint32(std::max(1.0f, std::round(tick)))) + " " +
                GetSchoolName(sSpellMgr.GetSpellEntry(info->spellId)) + " damage a second per stack of " + info->name;
    return text;
}

void BonkRun::DescribeOfferDamage(Offer const& offer, std::string& before, std::string& after, std::string& label) const
{
    before.clear();
    after.clear();
    label.clear();
    if (offer.isCard)
        return;

    if (!offer.isWeapon)
    {
        BonkPassive const* passive = sBonkMgr.GetPassive(offer.id);
        if (!passive || passive->effect != BONK_PASSIVE_POWER)
            return;
        for (OwnedWeapon const& owned : m_weapons)
        {
            std::string const now = DescribeShownRange(owned);
            if (now.empty())
                continue;
            before = now;
            after = DescribeShownRange(owned, passive->valuePerRank);
            label = owned.weapon.name;
            return;
        }
        return;
    }

    BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id);
    if (!weapon)
        return;

    if (offer.step == BONK_STEP_NEW)
    {
        OwnedWeapon fresh;
        fresh.weapon = *weapon;
        after = DescribeShownRange(fresh);
        return;
    }

    OwnedWeapon const* owned = FindOwned(offer.id);
    if (!owned)
        return;
    OwnedWeapon next = *owned;
    if (offer.step == BONK_STEP_EVOLVE)
    {
        BonkWeapon const* evolved = sBonkMgr.GetWeapon(offer.evolveTo);
        if (!evolved)
            return;
        next.weapon = *evolved;
    }
    else if (offer.step == BONK_STEP_RANK && offer.rarity < BONK_RARITY_LEGENDARY)
        AddRankGains(next, offer);
    else
        return;

    before = DescribeShownRange(*owned);
    after = DescribeShownRange(next);
    if (before == after)
        before.clear();
}
