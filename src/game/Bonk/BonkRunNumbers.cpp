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
            keyword == BONK_KEYWORD_DECAY || keyword == BONK_KEYWORD_AGONY;
    }

    std::string FormatNumber(float value, char const* format)
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), format, value);
        return buffer;
    }

    std::string ShownNumber(float value)
    {
        return std::to_string(uint32(std::max(1.0f, std::round(value))));
    }

    std::string ShownSeconds(float ms)
    {
        float const seconds = ms / 1000.0f;
        return FormatNumber(seconds, seconds < 1.0f ? "%.2f" : "%.1f") + " sec";
    }

    std::string ShownPercent(float value, bool signed_)
    {
        return (signed_ && value >= 0.0f ? "+" : "") + FormatNumber(std::round(value * 10.0f) / 10.0f, "%g") + "%";
    }

    std::string CapitalizeFirst(std::string text)
    {
        if (!text.empty())
            text[0] = char(toupper(static_cast<unsigned char>(text[0])));
        return text;
    }

    std::string GetIntervalLabel(BonkWeapon const& weapon)
    {
        if (weapon.behavior == BONK_BEHAVIOR_DEEP_WOUNDS || weapon.IsRevenge())
            return "Can occur every";
        if (weapon.behavior == BONK_BEHAVIOR_BLOOD_AURA)
            return "Pulses every";
        return "Casts every";
    }

    BonkStatChange SplitUpgradeText(BonkWeapon const& weapon, BonkSpellUpgrade upgrade, float before, float after)
    {
        std::string const now = DescribeBonkSpellUpgrade(weapon, upgrade, before);
        std::string const next = DescribeBonkSpellUpgrade(weapon, upgrade, after);
        size_t const nowSpace = now.find(' ');
        size_t const nextSpace = next.find(' ');
        if (nowSpace == std::string::npos || nextSpace == std::string::npos)
            return {};
        return { CapitalizeFirst(next.substr(nextSpace + 1)), now.substr(0, nowSpace), next.substr(0, nextSpace) };
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
    bool const damageCounts = RankDamageCounts(owned);
    uint8 const count = GetRankUpgradeCount(&owned, offer.rarity);
    owned.damageBonus += GetRankDamage(&owned, offer.rarity);
    for (uint8 i = 0; i < count; ++i)
    {
        float const step = owned.weapon.GetUpgradeStep(offer.upgrades[i]);
        if (step > 0.0f)
            owned.upgrades[offer.upgrades[i]] += step;
        else if (damageCounts)
            owned.damageBonus += BONK_RANK_SPARE_DAMAGE;
    }
}

bool BonkRun::RankDamageCounts(OwnedWeapon const& owned) const
{
    if (owned.weapon.damageFraction > 0.0f || owned.weapon.healPct > 0.0f)
        return true;
    if (IsTickingKeyword(owned.weapon.applies) && owned.weapon.applyStacks)
        return true;
    float upgrades[MAX_BONK_SPELL_UPGRADE] = {};
    GetSpellUpgrades(&owned, upgrades);
    WeaponStrength strength;
    return GetWeaponStrength(owned.weapon, owned.damageBonus, upgrades, strength);
}

float BonkRun::GetRankDamage(OwnedWeapon const* owned, BonkRarity rarity) const
{
    return !owned || RankDamageCounts(*owned) ? BONK_RANK_DAMAGE[rarity] : 0.0f;
}

uint8 BonkRun::GetRankUpgradeCount(OwnedWeapon const* owned, BonkRarity rarity) const
{
    uint8 const count = !owned || RankDamageCounts(*owned) ? BONK_RANK_UPGRADES[rarity] : BONK_DAMAGELESS_RANK_UPGRADES[rarity];
    return std::min(count, BONK_MAX_RANK_UPGRADES);
}

bool BonkRun::KeepsWindowUptime(OwnedWeapon const& owned, float const* rolled, BonkSpellUpgrade upgrade, float step) const
{
    bool const window = owned.weapon.behavior == BONK_BEHAVIOR_BLADE_FLURRY || owned.weapon.behavior == BONK_BEHAVIOR_SHADOW_DANCE ||
        owned.weapon.behavior == BONK_BEHAVIOR_EVASION;
    if (!window || owned.weapon.durationMs == 0 || (upgrade != BONK_SPELL_UPGRADE_HASTE && upgrade != BONK_SPELL_UPGRADE_DURATION))
        return true;
    OwnedWeapon after = owned;
    for (uint8 index = 0; index < MAX_BONK_SPELL_UPGRADE; ++index)
        after.upgrades[index] += rolled[index];
    after.upgrades[upgrade] += step;
    uint32 const cooldown = GetWeaponCooldown(after);
    return cooldown > 0 && owned.weapon.durationMs * GetWeaponDurationScale(after) <= cooldown * BONK_WINDOW_MAX_UPTIME;
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
        return;

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

void BonkRun::DescribeOfferStats(Offer const& offer, std::vector<BonkStatChange>& stats) const
{
    stats.clear();
    if (offer.isCard || offer.newLevel <= 1)
        return;

    if (!offer.isWeapon)
    {
        if (BonkPassive const* passive = sBonkMgr.GetPassive(offer.id))
            sBonkMgr.DescribePassiveStats(*passive, offer.newLevel, stats);
        return;
    }

    if (offer.step != BONK_STEP_RANK || offer.rarity >= BONK_RARITY_LEGENDARY)
        return;
    OwnedWeapon const* owned = FindOwned(offer.id);
    if (!owned)
        return;
    OwnedWeapon next = *owned;
    AddRankGains(next, offer);

    AddDamageStats(*owned, next, stats);
    bool const hasDamage = !stats.empty();

    float now[MAX_BONK_SPELL_UPGRADE] = {};
    float after[MAX_BONK_SPELL_UPGRADE] = {};
    GetSpellUpgrades(owned, now);
    GetSpellUpgrades(&next, after);
    WeaponStrength before, strength;
    if (GetWeaponStrength(owned->weapon, owned->damageBonus, now, before) &&
        GetWeaponStrength(next.weapon, next.damageBonus, after, strength) && std::fabs(strength.value - before.value) >= 0.05f)
    {
        bool const signed_ = strength.folded == BONK_SPELL_UPGRADE_MARK || strength.folded == BONK_SPELL_UPGRADE_SLOWED;
        stats.push_back({ strength.label, ShownPercent(before.value, signed_), ShownPercent(strength.value, signed_) });
    }

    AddUpgradeStats(*owned, next, hasDamage, stats);
}

void BonkRun::AddDamageStats(OwnedWeapon const& owned, OwnedWeapon const& next, std::vector<BonkStatChange>& stats) const
{
    float const hit = GetShownHit(next, 0.0f);
    if (hit > 0.0f)
        stats.push_back({ "Damage", ShownNumber(GetShownHit(owned, 0.0f)), ShownNumber(hit) });

    float const tick = GetShownTick(next, 0.0f);
    if (tick > 0.0f)
        if (BonkKeywordInfo const* info = sBonkMgr.GetKeyword(next.weapon.applies))
            stats.push_back({ info->name + " damage per stack", ShownNumber(GetShownTick(owned, 0.0f)), ShownNumber(tick) });
}

void BonkRun::AddUpgradeStats(OwnedWeapon const& owned, OwnedWeapon const& next, bool hasDamage, std::vector<BonkStatChange>& stats) const
{
    float now[MAX_BONK_SPELL_UPGRADE] = {};
    float after[MAX_BONK_SPELL_UPGRADE] = {};
    GetSpellUpgrades(&owned, now);
    GetSpellUpgrades(&next, after);
    WeaponStrength strength;
    bool const strengthens = GetWeaponStrength(next.weapon, next.damageBonus, after, strength);
    BonkWeapon const& weapon = next.weapon;

    for (uint8 index = BONK_SPELL_UPGRADE_NONE + 1; index < MAX_BONK_SPELL_UPGRADE; ++index)
    {
        BonkSpellUpgrade const upgrade = BonkSpellUpgrade(index);
        if (after[upgrade] - now[upgrade] < 0.001f)
            continue;
        if (strengthens && upgrade == strength.folded)
            continue;
        if (upgrade == BONK_SPELL_UPGRADE_PET_DAMAGE && hasDamage)
            continue;

        switch (upgrade)
        {
            case BONK_SPELL_UPGRADE_HASTE:
                stats.push_back({ GetIntervalLabel(weapon), ShownSeconds(float(GetWeaponCooldown(owned))), ShownSeconds(float(GetWeaponCooldown(next))) });
                break;
            case BONK_SPELL_UPGRADE_DURATION:
                if (weapon.durationMs > 0)
                {
                    BonkStatChange stat = SplitUpgradeText(weapon, upgrade, now[upgrade], after[upgrade]);
                    stats.push_back({ stat.label, ShownSeconds(weapon.durationMs * GetWeaponDurationScale(owned)),
                        ShownSeconds(weapon.durationMs * GetWeaponDurationScale(next)) });
                }
                else
                    stats.push_back(SplitUpgradeText(weapon, upgrade, now[upgrade], after[upgrade]));
                break;
            case BONK_SPELL_UPGRADE_STACKS:
            {
                BonkKeywordInfo const* info = sBonkMgr.GetKeyword(weapon.applies);
                uint32 const base = std::max<uint32>(1, weapon.applyStacks);
                stats.push_back({ (info ? info->name : std::string("Effect")) + " stacks per hit", std::to_string(base + uint32(now[upgrade])),
                    std::to_string(base + uint32(after[upgrade])) });
                break;
            }
            case BONK_SPELL_UPGRADE_ECHO:
                stats.push_back({ "Extra casts", FormatNumber(now[upgrade], "%g"), FormatNumber(after[upgrade], "%g") });
                break;
            case BONK_SPELL_UPGRADE_TARGETS:
                stats.push_back({ "Enemies affected", std::to_string(weapon.baseTargets + uint32(now[upgrade])),
                    std::to_string(weapon.baseTargets + uint32(after[upgrade])) });
                break;
            case BONK_SPELL_UPGRADE_REACH:
                stats.push_back({ "Reach", FormatNumber(weapon.range * GetWeaponArea(owned), "%.1f") + " yd",
                    FormatNumber(weapon.range * GetWeaponArea(next), "%.1f") + " yd" });
                break;
            case BONK_SPELL_UPGRADE_DEPLOY:
                stats.push_back({ "Deploy range","+" + FormatNumber(now[upgrade], "%g") + " yd", "+" + FormatNumber(after[upgrade], "%g") + " yd" });
                break;
            default:
            {
                BonkStatChange stat = SplitUpgradeText(weapon, upgrade, now[upgrade], after[upgrade]);
                if (!stat.label.empty())
                    stats.push_back(stat);
                break;
            }
        }
    }
}
