#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SpellMgr.h"

#include <algorithm>

namespace
{
    uint32 const BONK_BLINK_CHARGES           = 1;
    float const BONK_BLINK_RECHARGE_RATE      = 1.0f / 0.7f;
    int32 const BONK_BLINK_LAND_MS            = 200;
    int32 const BONK_BLINK_RECAST_MS          = 300;
    uint32 const BONK_MISSILE_INTERVAL_MS     = 250;
    float const BONK_PYROBLAST_SPLASH_RADIUS  = 6.0f;

    enum class MageSchool
    {
        Frost,
        Fire,
        Arcane,
        Count,
    };

    bool GetMageSchool(SpellEntry const* spellInfo, MageSchool& school)
    {
        if (!spellInfo)
            return false;
        uint32 const mask = spellInfo->GetSpellSchoolMask();
        if (mask & SPELL_SCHOOL_MASK_FROST)
            school = MageSchool::Frost;
        else if (mask & SPELL_SCHOOL_MASK_FIRE)
            school = MageSchool::Fire;
        else if (mask & SPELL_SCHOOL_MASK_ARCANE)
            school = MageSchool::Arcane;
        else
            return false;
        return true;
    }

    uint32 GetElementalFamilyOf(MageSchool school)
    {
        switch (school)
        {
            case MageSchool::Fire:
                return BONK_PET_FAMILY_FIRE_ELEMENTAL;
            case MageSchool::Arcane:
                return BONK_PET_FAMILY_ARCANE_ELEMENTAL;
            default:
                return BONK_PET_FAMILY_WATER_ELEMENTAL;
        }
    }

    bool IsBigEnemy(Creature const* mob)
    {
        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        return ai && ai->GetKind() != BONK_SPAWN_NORMAL;
    }
}

bool BonkRun::IsFrostSpellOnFrozen(SpellEntry const* spell, Unit const* victim) const
{
    if (!spell || !victim || !(spell->GetSpellSchoolMask() & SPELL_SCHOOL_MASK_FROST))
        return false;
    return FindOwnedBySpell(spell->Id) && IsFrozen(victim);
}

uint32 BonkRun::GetBlinkCharges() const
{
    return FindOwnedByBehavior(BONK_BEHAVIOR_BLINK) ? BONK_BLINK_CHARGES : 0;
}

float BonkRun::GetBlinkRechargeRate() const
{
    OwnedWeapon const* blink = FindOwnedByBehavior(BONK_BEHAVIOR_BLINK);
    return blink ? BONK_BLINK_RECHARGE_RATE * GetWeaponHasteRate(*blink) : 1.0f;
}

bool BonkRun::IsBlinkDash() const
{
    return m_blinkDash || FindOwnedByBehavior(BONK_BEHAVIOR_BLINK);
}

void BonkRun::OnDash()
{
    if (m_state != STATE_RUNNING || !m_player->IsAlive() || !IsBlinkDash())
        return;

    OwnedWeapon const* found = FindOwnedByBehavior(BONK_BEHAVIOR_BLINK);
    OwnedWeapon* blink = found ? FindOwned(found->weapon.id) : nullptr;
    if (SpellEntry const* spellInfo = blink ? sSpellMgr.GetSpellEntry(blink->weapon.spellId) : nullptr)
    {
        CastSelfArea(*blink, spellInfo, GetWeaponArea(*blink));
        uint32 const pulses = std::min(uint32(GetStat(BONK_PASSIVE_AMOUNT)), BONK_MAX_COUNT_PULSES);
        for (uint32 i = 1; i <= pulses; ++i)
        {
            PendingCast again;
            again.weaponId = blink->weapon.id;
            again.spellId = spellInfo->Id;
            again.leftMs = int32(i) * BONK_BLINK_RECAST_MS;
            again.recast = true;
            m_pendingCasts.push_back(again);
        }
    }

    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.HasFlag(BONK_WEAPON_FLAG_ON_BLINK))
        {
            PendingCast landing;
            landing.weaponId = owned.weapon.id;
            landing.spellId = owned.weapon.spellId;
            landing.leftMs = BONK_BLINK_LAND_MS;
            landing.blinkLanding = true;
            m_pendingCasts.push_back(landing);
        }
}

void BonkRun::LandBlink(OwnedWeapon& owned)
{
    if (owned.weapon.behavior == BONK_BEHAVIOR_ARCANE_MISSILES)
        FireMissileVolley(owned);
    else if (owned.weapon.target == BONK_TARGET_SELF_AREA)
        if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(owned.weapon.spellId))
            CastSelfArea(owned, spellInfo, GetWeaponArea(owned));
}

void BonkRun::ResolveMageCast(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    if (cast.blinkLanding)
        LandBlink(owned);
    else if (cast.pyroblastSplash)
        SplashPyroblast(owned, spellInfo, cast);
}

void BonkRun::QueueMissiles(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, float share)
{
    uint32 const missiles = uint32(std::max(1.0f, owned.weapon.behaviorValue));
    for (uint32 missile = 1; missile < missiles; ++missile)
    {
        PendingCast follow;
        follow.weaponId = owned.weapon.id;
        follow.spellId = spellInfo->Id;
        follow.leftMs = int32(missile * BONK_MISSILE_INTERVAL_MS);
        follow.target = target->GetObjectGuid();
        follow.strike = true;
        follow.share = share;
        m_pendingCasts.push_back(follow);
    }
}

void BonkRun::FireMissileVolley(OwnedWeapon& owned)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(owned.weapon.spellId);
    if (!spellInfo)
        return;
    std::vector<Creature*> mobs = GetMobsInRange(owned.weapon.range);
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [this](Creature* mob) { return !m_player->IsWithinLOSInMap(mob); }), mobs.end());
    if (mobs.empty())
        return;

    uint32 const missiles = uint32(std::max(1.0f, owned.weapon.behaviorValue));
    FiringWeapon firing(m_player->GetBonkPlayer());
    for (uint32 missile = 0; missile < missiles; ++missile)
    {
        Creature* target = mobs[missile % mobs.size()];
        if (target->IsAlive())
            StrikeWith(owned, spellInfo, target, 1.0f, true);
    }
}

void BonkRun::SortMostBurningFirst(std::vector<Creature*>& mobs) const
{
    std::vector<std::pair<uint32, Creature*>> ranked;
    ranked.reserve(mobs.size());
    for (Creature* mob : mobs)
        ranked.emplace_back(GetKeywordStacks(mob, BONK_KEYWORD_BURNING), mob);
    std::stable_sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    for (size_t i = 0; i < ranked.size(); ++i)
        mobs[i] = ranked[i].second;
}

bool BonkRun::HurlPyroblasts(OwnedWeapon& owned, SpellEntry const* spellInfo, std::vector<Creature*> mobs)
{
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [this](Creature* mob) { return !m_player->IsWithinLOSInMap(mob); }), mobs.end());
    if (mobs.empty())
        return false;

    uint32 const shots = GetWeaponTargets(owned);
    std::vector<Creature*> aims;
    for (Creature* mob : mobs)
        if (IsBigEnemy(mob))
            aims.push_back(mob);
    SortStrongestFirst(aims);
    if (aims.size() > shots)
        aims.resize(shots);
    if (aims.size() < shots)
        for (Creature* center : PickGroupCenters(mobs, BONK_PYROBLAST_SPLASH_RADIUS, shots))
            if (aims.size() < shots && std::find(aims.begin(), aims.end(), center) == aims.end())
                aims.push_back(center);

    FiringWeapon firing(m_player->GetBonkPlayer());
    for (Creature* aim : aims)
    {
        StrikeWith(owned, spellInfo, aim, 1.0f);

        PendingCast splash;
        splash.weaponId = owned.weapon.id;
        splash.spellId = spellInfo->Id;
        splash.leftMs = int32(std::max(1u, GetMissileDelay(m_player, aim, spellInfo->speed)));
        splash.target = aim->GetObjectGuid();
        aim->GetPosition(splash.x, splash.y, splash.z);
        splash.share = owned.weapon.altDamage;
        splash.pyroblastSplash = true;
        m_pendingCasts.push_back(splash);
    }
    return true;
}

void BonkRun::SplashPyroblast(OwnedWeapon& owned, SpellEntry const* spellInfo, PendingCast const& cast)
{
    float x = cast.x, y = cast.y;
    if (Creature* aim = m_player->GetMap()->GetCreature(cast.target))
        if (aim->IsAlive())
        {
            x = aim->GetPositionX();
            y = aim->GetPositionY();
        }

    float const damage = GetWeaponDamage(owned) * cast.share;
    float const radius = BONK_PYROBLAST_SPLASH_RADIUS * GetWeaponArea(owned);
    FiringWeapon firing(m_player->GetBonkPlayer());
    for (Creature* mob : GetMobsNear(x, y, radius))
        if (mob->GetObjectGuid() != cast.target)
            DealSpellHit(spellInfo, mob, damage);
}

uint32 BonkRun::GetElementalFamily() const
{
    uint32 counts[size_t(MageSchool::Count)] = {};
    MageSchool order[size_t(MageSchool::Count)] = {};
    size_t seen = 0;
    for (OwnedWeapon const& owned : m_weapons)
    {
        MageSchool school;
        if (owned.weapon.behavior == BONK_BEHAVIOR_PET || !GetMageSchool(sSpellMgr.GetSpellEntry(owned.weapon.spellId), school))
            continue;
        if (!counts[size_t(school)]++)
            order[seen++] = school;
    }
    if (!seen)
        return BONK_PET_FAMILY_WATER_ELEMENTAL;

    MageSchool best = order[0];
    for (size_t i = 1; i < seen; ++i)
        if (counts[size_t(order[i])] > counts[size_t(best)])
            best = order[i];
    return GetElementalFamilyOf(best);
}
