#include "BonkRun.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"

#include <algorithm>

namespace
{
    uint32 const BONK_WEAPON_IMP              = 900;
    uint32 const BONK_WEAPON_VOIDWALKER       = 901;
    uint32 const BONK_WEAPON_SUCCUBUS         = 902;
    uint32 const BONK_WEAPON_FELHUNTER        = 903;
    uint32 const BONK_WEAPON_SHADOW_BOLT      = 907;

    uint32 const BONK_SPELL_DRAIN_BEAM        = 755;
    uint32 const BONK_SPELL_CONSUME_SHADOWS   = 1112;
    uint32 const BONK_SPELL_SPELL_LOCK        = 19244;
    uint32 const BONK_SPELL_DEVOUR_MAGIC      = 17012;

    float const BONK_HELLFIRE_BURN_PCT        = 1.0f;
    float const BONK_HELLFIRE_HEALTH_FLOOR    = 0.35f;
    uint32 const BONK_NIGHTFALL_PER_SEC       = 3;
    uint32 const BONK_NIGHTFALL_WINDOW_MS     = 1000;
    float const BONK_CONSUME_RADIUS           = 10.0f;
    uint32 const BONK_CONSUME_STACKS          = 5;
    float const BONK_CONSUME_GROWTH           = 0.08f;
    float const BONK_CONSUME_BURST_RADIUS     = 10.0f;
    float const BONK_CONSUME_BURST_BITES      = 2.0f;
    uint32 const BONK_DEVOUR_EVERY_MS         = 10000;
    uint32 const BONK_SPELL_LOCK_EVERY_MS     = 6000;
    uint32 const BONK_IMP_VOLLEY_BOLTS        = 3;
    uint32 const BONK_IMP_VOLLEY_GAP_MS       = 300;
    float const BONK_SHADOW_BITE_PER_AFFLICTION = 0.1f;
    int32 const BONK_WARLOCK_POLL_MS          = 250;

    BonkKeyword const BONK_WARLOCK_AFFLICTIONS[] = { BONK_KEYWORD_DECAY, BONK_KEYWORD_AGONY, BONK_KEYWORD_BURNING };
}

uint32 BonkRun::GetWarlockPetFamily(OwnedWeapon const& owned) const
{
    switch (GetRootWeapon(owned.weapon))
    {
        case BONK_WEAPON_IMP:        return BONK_PET_FAMILY_IMP;
        case BONK_WEAPON_VOIDWALKER: return BONK_PET_FAMILY_VOIDWALKER;
        case BONK_WEAPON_SUCCUBUS:   return BONK_PET_FAMILY_SUCCUBUS;
        case BONK_WEAPON_FELHUNTER:  return BONK_PET_FAMILY_FELHUNTER;
        default:                     return 0;
    }
}

bool BonkRun::IsCasterPetFamily(BonkPetFamily const& family) const
{
    return family.id == BONK_PET_FAMILY_IMP;
}

bool BonkRun::FireWarlockSpell(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    switch (owned.weapon.behavior)
    {
        case BONK_BEHAVIOR_CORRUPTION:     SpreadCorruption(owned, spellInfo); break;
        case BONK_BEHAVIOR_CURSE_OF_AGONY: CurseWithAgony(owned, spellInfo); break;
        case BONK_BEHAVIOR_DRAIN_LIFE:     DrainLife(owned, spellInfo); break;
        case BONK_BEHAVIOR_HELLFIRE:       BurnHellfire(owned, spellInfo); break;
        default:                           return false;
    }
    return true;
}

void BonkRun::AddWarlockStacks(OwnedWeapon const& owned, SpellEntry const* spellInfo, std::vector<Creature*> mobs, BonkKeyword keyword)
{
    uint32 const wanted = owned.weapon.baseTargets + GetSpellCount(owned);
    if (mobs.size() > wanted)
        mobs.resize(wanted);
    BonkKeywordInfo const* info = sBonkMgr.GetKeyword(keyword);
    if (!info)
        return;
    uint32 const stacks = owned.weapon.applyStacks + uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_STACKS));
    uint32 const durationMs = uint32(info->durationMs * GetWeaponDurationScale(owned));
    float const strength = (1.0f + owned.damageBonus / 100.0f) * std::max(1.0f, owned.weapon.damageFraction);
    for (Creature* mob : mobs)
    {
        ShowStrike(m_player, spellInfo, mob);
        ApplyKeyword(mob, keyword, stacks, durationMs, strength, owned.weapon.spellId);
    }
}

void BonkRun::SpreadCorruption(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    std::vector<Creature*> const mobs = GetMobsInRange(owned.weapon.range);
    if (mobs.empty())
    {
        owned.timer = BONK_WARLOCK_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    AddWarlockStacks(owned, spellInfo, mobs, BONK_KEYWORD_DECAY);
}

void BonkRun::CurseWithAgony(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    std::vector<Creature*> mobs = GetMobsInRange(owned.weapon.range);
    if (mobs.empty())
    {
        owned.timer = BONK_WARLOCK_POLL_MS;
        return;
    }
    std::stable_sort(mobs.begin(), mobs.end(), [](Creature* a, Creature* b) { return a->GetHealth() > b->GetHealth(); });
    owned.timer = GetWeaponCooldown(owned);
    AddWarlockStacks(owned, spellInfo, mobs, BONK_KEYWORD_AGONY);
}

void BonkRun::LatchDrainBeam(Creature* mob)
{
    mob->SetChannelObjectGuid(m_player->GetObjectGuid());
    mob->SetUInt32Value(UNIT_CHANNEL_SPELL, BONK_SPELL_DRAIN_BEAM);
    m_drainBeams.push_back(mob->GetObjectGuid());
}

void BonkRun::ReleaseDrainBeam(ObjectGuid const& guid)
{
    if (Creature* mob = m_player->GetMap()->GetCreature(guid))
        if (mob->GetUInt32Value(UNIT_CHANNEL_SPELL) == BONK_SPELL_DRAIN_BEAM)
            mob->CancelSpellChannelingAnimationInstantly();
}

void BonkRun::ReleaseDrainBeams()
{
    if (!m_player->IsInWorld())
    {
        m_drainBeams.clear();
        return;
    }
    for (ObjectGuid const& guid : m_drainBeams)
        ReleaseDrainBeam(guid);
    m_drainBeams.clear();
}

void BonkRun::DrainLife(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    float const range = owned.weapon.range;
    Map* map = m_player->GetMap();
    m_drainBeams.erase(std::remove_if(m_drainBeams.begin(), m_drainBeams.end(), [this, map, range](ObjectGuid const& guid)
    {
        Creature* mob = map->GetCreature(guid);
        if (mob && mob->IsAlive() && m_player->IsWithinDistInMap(mob, range))
            return false;
        ReleaseDrainBeam(guid);
        return true;
    }), m_drainBeams.end());

    uint32 const wanted = owned.weapon.baseTargets + GetSpellCount(owned);
    while (m_drainBeams.size() > wanted)
    {
        ReleaseDrainBeam(m_drainBeams.back());
        m_drainBeams.pop_back();
    }
    for (Creature* mob : GetMobsInRange(range))
    {
        if (m_drainBeams.size() >= wanted)
            break;
        if (std::find(m_drainBeams.begin(), m_drainBeams.end(), mob->GetObjectGuid()) == m_drainBeams.end() && m_player->IsWithinLOSInMap(mob))
            LatchDrainBeam(mob);
    }
    if (m_drainBeams.empty())
    {
        owned.timer = BONK_WARLOCK_POLL_MS;
        return;
    }

    owned.timer = GetWeaponCooldown(owned);
    uint32 drained = 0;
    for (ObjectGuid const& guid : m_drainBeams)
        if (Creature* mob = map->GetCreature(guid))
        {
            DealShamanHit(spellInfo, owned.weapon.spellId, mob, GetWeaponDamage(owned));
            ++drained;
        }
    if (drained && owned.weapon.healPct > 0.0f && m_player->IsAlive())
        HealHero(uint32(m_player->GetMaxHealth() * owned.weapon.healPct * drained / 100.0f), owned.weapon.spellId);
}

void BonkRun::BurnHellfire(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    std::vector<Creature*> const mobs = GetMobsInRange(owned.weapon.range * GetWeaponArea(owned));
    if (mobs.empty())
    {
        owned.timer = BONK_WARLOCK_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    ShowStrike(m_player, spellInfo, m_player);
    for (Creature* mob : mobs)
    {
        DealShamanHit(spellInfo, owned.weapon.spellId, mob, GetWeaponDamage(owned));
        if (mob->IsAlive() && owned.weapon.applyStacks)
            ApplyWeaponKeyword(owned, mob, BonkKeyword(owned.weapon.applies), owned.weapon.applyStacks);
    }

    uint32 const health = m_player->GetHealth(), maxHealth = m_player->GetMaxHealth();
    uint32 const floor = uint32(maxHealth * BONK_HELLFIRE_HEALTH_FLOOR);
    uint32 const burn = std::min(uint32(maxHealth * BONK_HELLFIRE_BURN_PCT / 100.0f), health > floor ? health - floor : 0u);
    if (!burn || !m_player->IsAlive())
        return;
    SpellNonMeleeDamage hit(m_player, m_player, owned.weapon.spellId, SpellSchools(spellInfo->School));
    hit.damage = burn;
    m_player->SendSpellNonMeleeDamageLog(&hit);
    m_player->SetHealth(health - burn);
}

void BonkRun::OnWarlockKill(Unit* victim)
{
    bool const afflicted = std::any_of(std::begin(BONK_WARLOCK_AFFLICTIONS), std::end(BONK_WARLOCK_AFFLICTIONS),
        [this, victim](BonkKeyword keyword) { return GetKeywordStacks(victim, keyword) > 0; });
    if (afflicted && FindOwnedByRoot(BONK_WEAPON_SHADOW_BOLT))
        m_nightfallQueue.push_back(victim->GetObjectGuid());

    for (OwnedWeapon& owned : m_weapons)
    {
        if (GetWarlockPetFamily(owned) != BONK_PET_FAMILY_VOIDWALKER)
            continue;
        for (ObjectGuid const& guid : owned.pets)
        {
            Creature* pet = m_player->GetMap()->GetCreature(guid);
            if (!pet || !pet->IsAlive() || !pet->IsWithinDistInMap(victim, BONK_CONSUME_RADIUS * GetSummonReach(owned)))
                continue;
            uint32& stacks = m_consumedShadows[guid];
            stacks = std::min(stacks + 1, BONK_CONSUME_STACKS);
        }
    }
}

BonkRun::OwnedWeapon* BonkRun::FindOwnedByRoot(uint32 rootId)
{
    for (OwnedWeapon& owned : m_weapons)
        if (GetRootWeapon(owned.weapon) == rootId)
            return &owned;
    return nullptr;
}

float BonkRun::GetConsumedGrowth(ObjectGuid const& pet) const
{
    auto itr = m_consumedShadows.find(pet);
    return itr == m_consumedShadows.end() ? 1.0f : 1.0f + BONK_CONSUME_GROWTH * itr->second;
}

void BonkRun::FireNightfall()
{
    if (m_nightfallQueue.empty())
        return;
    uint32 const now = WorldTimer::getMSTime();
    m_nightfallMs.erase(std::remove_if(m_nightfallMs.begin(), m_nightfallMs.end(),
        [now](uint32 at) { return now - at >= BONK_NIGHTFALL_WINDOW_MS; }), m_nightfallMs.end());

    OwnedWeapon* owned = FindOwnedByRoot(BONK_WEAPON_SHADOW_BOLT);
    SpellEntry const* spellInfo = owned ? sSpellMgr.GetSpellEntry(owned->weapon.spellId) : nullptr;
    while (!m_nightfallQueue.empty() && spellInfo && m_nightfallMs.size() < BONK_NIGHTFALL_PER_SEC)
    {
        m_nightfallQueue.pop_front();
        std::vector<Creature*> const mobs = GetMobsInRange(owned->weapon.range);
        if (mobs.empty())
            break;
        StrikeWith(*owned, spellInfo, mobs.front(), 1.0f, true);
        m_nightfallMs.push_back(now);
    }
    m_nightfallQueue.clear();
}

void BonkRun::BurstConsumedShadows()
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_CONSUME_SHADOWS);
    if (!spellInfo)
        return;
    for (OwnedWeapon& owned : m_weapons)
    {
        BonkPetFamily const* family = GetPetFamily(owned);
        if (!family || family->id != BONK_PET_FAMILY_VOIDWALKER)
            continue;
        for (ObjectGuid const& guid : owned.pets)
        {
            auto stacks = m_consumedShadows.find(guid);
            Creature* pet = m_player->GetMap()->GetCreature(guid);
            if (stacks == m_consumedShadows.end() || stacks->second < BONK_CONSUME_STACKS || !pet || !pet->IsAlive())
                continue;
            m_consumedShadows.erase(stacks);
            ShowStrike(pet, spellInfo, pet);
            float const hit = GetPetBiteDamage(owned, *family) * BONK_CONSUME_BURST_BITES;
            float const powerShare = GetPowerShare(pet);
            for (Creature* mob : GetMobsNear(pet->GetPositionX(), pet->GetPositionY(), BONK_CONSUME_BURST_RADIUS * GetSummonReach(owned)))
                if (pet->IsWithinLOSInMap(mob))
                    DealExactHit(spellInfo, mob, uint32(std::max(1.0f, hit * GetDamageMultiplier(nullptr, mob, powerShare))), pet);
        }
    }
}

float BonkRun::GetWarlockPetStrikeScale(OwnedWeapon const& owned, Creature const* prey) const
{
    if (GetWarlockPetFamily(owned) != BONK_PET_FAMILY_FELHUNTER)
        return 1.0f;
    uint32 const afflictions = uint32(std::count_if(std::begin(BONK_WARLOCK_AFFLICTIONS), std::end(BONK_WARLOCK_AFFLICTIONS),
        [this, prey](BonkKeyword keyword) { return GetKeywordStacks(prey, keyword) > 0; }));
    return 1.0f + BONK_SHADOW_BITE_PER_AFFLICTION * afflictions;
}

void BonkRun::QueueImpVolley(OwnedWeapon const& owned, Creature* pet, Creature* prey, uint32 abilityIndex)
{
    uint32 const now = WorldTimer::getMSTime();
    for (uint32 bolt = 1; bolt < BONK_IMP_VOLLEY_BOLTS; ++bolt)
        m_impVolleys.push_back({ pet->GetObjectGuid(), prey->GetObjectGuid(), owned.weapon.id, abilityIndex, now + bolt * BONK_IMP_VOLLEY_GAP_MS });
}

void BonkRun::FireImpVolleys()
{
    uint32 const now = WorldTimer::getMSTime();
    Map* map = m_player->GetMap();
    std::vector<ImpVolley> due;
    m_impVolleys.erase(std::remove_if(m_impVolleys.begin(), m_impVolleys.end(), [&due, now](ImpVolley const& volley)
    {
        if (int32(now - volley.atMs) < 0)
            return false;
        due.push_back(volley);
        return true;
    }), m_impVolleys.end());
    for (ImpVolley const& volley : due)
    {
        OwnedWeapon* owned = FindOwned(volley.weaponId);
        Creature* pet = map->GetCreature(volley.pet);
        Creature* prey = map->GetCreature(volley.prey);
        BonkPetFamily const* family = owned ? GetPetFamily(*owned) : nullptr;
        if (!family || volley.ability >= family->abilities.size() || !pet || !pet->IsAlive())
            continue;
        if (!prey || !prey->IsAlive())
        {
            std::vector<Creature*> const mobs = GetMobsNear(pet->GetPositionX(), pet->GetPositionY(), family->abilities[volley.ability].value);
            prey = mobs.empty() ? nullptr : mobs.front();
        }
        if (prey)
            PetStrike(*owned, pet, prey, family->abilities[volley.ability]);
    }
}

void BonkRun::OnWarlockPetStrike(OwnedWeapon& owned, Creature* pet, Creature* prey, uint32 abilityIndex)
{
    if (GetWarlockPetFamily(owned) == BONK_PET_FAMILY_IMP)
    {
        QueueImpVolley(owned, pet, prey, abilityIndex);
        return;
    }
    if (GetWarlockPetFamily(owned) != BONK_PET_FAMILY_FELHUNTER || !prey->IsAlive() || !prey->IsNonMeleeSpellCasted(false))
        return;
    uint32 const now = WorldTimer::getMSTime();
    uint32& readyMs = m_spellLockReadyMs[pet->GetObjectGuid()];
    if (int32(now - readyMs) < 0)
        return;
    readyMs = now + BONK_SPELL_LOCK_EVERY_MS;
    prey->InterruptNonMeleeSpells(false);
    if (SpellEntry const* lock = sSpellMgr.GetSpellEntry(BONK_SPELL_SPELL_LOCK))
        ShowStrike(pet, lock, prey);
}

void BonkRun::DevourHarmfulEffect()
{
    Creature* felhunter = nullptr;
    for (OwnedWeapon& owned : m_weapons)
        if (GetWarlockPetFamily(owned) == BONK_PET_FAMILY_FELHUNTER)
            for (ObjectGuid const& guid : owned.pets)
                if (Creature* pet = m_player->GetMap()->GetCreature(guid))
                    if (pet->IsAlive())
                        felhunter = pet;
    if (!felhunter)
        return;

    for (auto const& itr : m_player->GetSpellAuraHolderMap())
    {
        SpellAuraHolder const* holder = itr.second;
        if (holder->IsPositive() || holder->IsPassive() || holder->GetCasterGuid() == m_player->GetObjectGuid())
            continue;
        m_player->RemoveAurasDueToSpell(holder->GetId());
        if (SpellEntry const* devour = sSpellMgr.GetSpellEntry(BONK_SPELL_DEVOUR_MAGIC))
            ShowStrike(felhunter, devour, m_player);
        return;
    }
}

void BonkRun::UpdateWarlock(uint32 diff)
{
    FireNightfall();
    BurstConsumedShadows();
    FireImpVolleys();

    if (!m_drainBeams.empty() && !std::any_of(m_weapons.begin(), m_weapons.end(),
        [](OwnedWeapon const& owned) { return owned.weapon.behavior == BONK_BEHAVIOR_DRAIN_LIFE; }))
        ReleaseDrainBeams();

    m_devourTimer += diff;
    if (m_devourTimer >= BONK_DEVOUR_EVERY_MS)
    {
        m_devourTimer = 0;
        DevourHarmfulEffect();
    }
}
