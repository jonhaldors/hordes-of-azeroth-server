#include "BonkRun.h"
#include "BonkPlayer.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "Utilities/Random.h"

#include <algorithm>

namespace
{
    uint32 const BONK_WEAPON_STARFIRE         = 1001;
    uint32 const BONK_WEAPON_MOONFIRE         = 1002;
    uint32 const BONK_WEAPON_HURRICANE        = 1003;
    uint32 const BONK_WEAPON_RAKE             = 1004;
    uint32 const BONK_WEAPON_SHRED            = 1005;
    uint32 const BONK_WEAPON_SWIPE            = 1006;
    uint32 const BONK_WEAPON_FRENZIED_REGENERATION = 1007;
    uint32 const BONK_WEAPON_REJUVENATION     = 1008;
    uint32 const BONK_WEAPON_FORCE_OF_NATURE  = 1009;
    uint32 const BONK_WEAPON_ENTANGLING_ROOTS = 1010;
    uint32 const BONK_WEAPON_TRANQUILITY      = 1011;
    uint32 const BONK_WEAPON_THORNS           = 1012;
    uint32 const BONK_WEAPON_SHAPESHIFT       = 1013;

    uint32 const BONK_SHAPESHIFT_COOLDOWN_MS  = 500;
    uint32 const BONK_KIT_SHAPESHIFT_SMOKE    = 3610;
    uint32 const BONK_KIT_NATURE_CAST         = 3291;
    uint32 const BONK_KIT_ARCANE_CAST         = 730;
    uint32 const BONK_DRUID_MOTION_GAP_MS     = 500;
    uint32 const BONK_CLAW_LOOK_GAP_MS        = 500;
    uint32 const BONK_KIT_CLAW_IMPACT         = 220;
    uint32 const BONK_KIT_MAUL_IMPACT         = 290;
    uint32 const BONK_KIT_STILL_RAKE          = 7410;
    uint32 const BONK_KIT_STILL_CLAW          = 7411;
    uint32 const BONK_KIT_STILL_SWIPE         = 7413;
    uint32 const BONK_KIT_STILL_MAUL          = 7414;
    uint32 const BONK_SPELL_CLAW_LOOK         = 1082;
    uint32 const BONK_SPELL_RAKE_LOOK         = 1822;
    uint32 const BONK_SPELL_SHRED_LOOK        = 5221;
    uint32 const BONK_SPELL_SWIPE_LOOK        = 779;
    uint32 const BONK_SPELL_MAUL_LOOK         = 6807;
    uint32 const BONK_SHAPESHIFT_SMOKE_DELAY_MS = 100;
    float const BONK_RIGHT_FORM_BONUS         = 1.3f;
    float const BONK_BEAR_DAMAGE              = 0.8f;

    struct FormAttack
    {
        uint32 spellId;
        float share;
        float cooldownScale;
        uint32 targets;
        float range;
        uint32 bleedStacks;
        float healPerHit;
        char const* name;
    };

    FormAttack const BONK_FORM_ATTACKS[] =
    {
        { 0,    0.0f, 1.0f,  0, 0.0f,  0, 0.0f,  "caster" },
        { 5176, 2.0f, 1.0f,  1, 30.0f, 0, 0.0f,  "moonkin" },
        { 1082, 1.1f, 0.67f, 1, 10.0f, 1, 0.0f,  "cat" },
        { 6807, 0.9f, 1.0f,  3, 10.0f, 0, 0.0f,  "bear" },
        { 5570, 0.6f, 1.0f,  3, 20.0f, 0, 0.15f, "tree" },
    };

    uint32 const BONK_SPELL_FRENZIED_REGEN    = 22842;
    uint32 const BONK_SPELL_THORNS            = 467;
    uint32 const BONK_SPELL_BLOOM_LASH        = 5570;
    uint32 const BONK_SPELL_HURRICANE_LOOK    = 16914;
    uint32 const BONK_SPELL_TREANT_ROOTS      = 339;
    float const BONK_TREANT_ROOT_RADIUS       = 5.0f;

    uint32 const BONK_DISPLAY_MOONKIN_ALLIANCE = 15374;
    uint32 const BONK_DISPLAY_MOONKIN_HORDE   = 15375;
    uint32 const BONK_DISPLAY_CAT_ALLIANCE    = 892;
    uint32 const BONK_DISPLAY_CAT_HORDE       = 8571;
    uint32 const BONK_DISPLAY_BEAR_ALLIANCE   = 2281;
    uint32 const BONK_DISPLAY_BEAR_HORDE      = 2289;
    uint32 const BONK_DISPLAY_TREE            = 864;


    float const BONK_BEAR_TAKEN               = 0.65f;

    float const BONK_SHRED_MAX_STACKS         = 30.0f;
    float const BONK_FRENZIED_REGEN_RADIUS    = 8.0f;
    float const BONK_FRENZIED_REGEN_MAX_ENEMIES = 5.0f;
    uint32 const BONK_DRUID_LOOK_GAP_MS       = 3000;
    uint32 const BONK_ROOT_MS                 = 2000;
    uint32 const BONK_TRANQUILITY_PULSES      = 4;
    uint32 const BONK_TRANQUILITY_PULSE_MS    = 1000;
    uint32 const BONK_THORNS_PER_SEC          = 5;
    uint32 const BONK_HURRICANE_MS            = 4000;
    int32 const BONK_HURRICANE_PULSE_MS       = 1000;
    size_t const BONK_HURRICANE_ALIVE         = 2;
    size_t const BONK_SWIPE_LOOKS             = 4;
    float const BONK_DRUID_AWARE_RANGE        = 30.0f;
    int32 const BONK_DRUID_POLL_MS            = 250;
    uint32 const BONK_DRUID_IDLE_MS           = 1000;


    FormAttack const& GetFormAttack(BonkRun::BonkDruidForm form)
    {
        return BONK_FORM_ATTACKS[size_t(form)];
    }
}

bool BonkRun::IsDruidAnimal() const
{
    return m_druidForm == BonkDruidForm::Cat || m_druidForm == BonkDruidForm::Bear;
}

void BonkRun::PlayDruidMotion(uint32 spellId)
{
    if (!IsDruidHero() || !m_player->IsAlive() || m_totalMs - m_druidMotionMs < BONK_DRUID_MOTION_GAP_MS)
        return;
    m_druidMotionMs = m_totalMs;
    if (IsDruidAnimal())
        return;
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId);
    bool const arcane = spellInfo && spellInfo->School == SPELL_SCHOOL_ARCANE;
    m_player->SendPlaySpellVisualKit(arcane ? BONK_KIT_ARCANE_CAST : BONK_KIT_NATURE_CAST);
}

uint32 BonkRun::GetClawImpactKit(uint32 spellId) const
{
    switch (spellId)
    {
        case BONK_SPELL_CLAW_LOOK:
        case BONK_SPELL_RAKE_LOOK:
        case BONK_SPELL_SHRED_LOOK:
        case BONK_SPELL_SWIPE_LOOK:
            return BONK_KIT_CLAW_IMPACT;
        case BONK_SPELL_MAUL_LOOK:
            return BONK_KIT_MAUL_IMPACT;
        default:
            return 0;
    }
}

uint32 BonkRun::GetStillClawKit(uint32 spellId) const
{
    switch (spellId)
    {
        case BONK_SPELL_RAKE_LOOK:
        case BONK_SPELL_SHRED_LOOK:
            return BONK_KIT_STILL_RAKE;
        case BONK_SPELL_CLAW_LOOK:
            return BONK_KIT_STILL_CLAW;
        case BONK_SPELL_SWIPE_LOOK:
            return BONK_KIT_STILL_SWIPE;
        case BONK_SPELL_MAUL_LOOK:
            return BONK_KIT_STILL_MAUL;
        default:
            return 0;
    }
}

void BonkRun::ShowDruidStrike(SpellEntry const* look, Creature* target)
{
    if (!look || !target)
        return;
    uint32 const impact = GetClawImpactKit(look->Id);
    if (!impact)
    {
        ShowStrike(m_player, look, target);
        return;
    }
    bool const clawReady = !m_druidClawLookMs || m_totalMs - m_druidClawLookMs >= BONK_CLAW_LOOK_GAP_MS;
    if (clawReady)
        m_druidClawLookMs = m_totalMs;
    if (clawReady && IsDruidAnimal())
    {
        ShowStrike(m_player, look, target);
        return;
    }
    if (clawReady)
        m_player->SendPlaySpellVisualKit(GetStillClawKit(look->Id));
    target->SendPlaySpellVisualKit(impact);
}

void BonkRun::OnDruidStrike(OwnedWeapon const& owned)
{
    if (owned.weapon.classMask & (1 << (CLASS_DRUID - 1)))
        PlayDruidMotion(owned.weapon.spellId);
}

bool BonkRun::IsDruidHero() const
{
    return m_player->GetClass() == CLASS_DRUID;
}

void BonkRun::ResetDruidState()
{
    m_druidLookMs.clear();
    m_thornsTimes.clear();
    m_tranquilityPulsesLeft = 0;
    m_tranquilityPulseMs = 0;
    m_tranquilityWeapon = 0;
    m_druidForm = BonkDruidForm::None;
    m_shapeshiftReadyMs = 0;
    m_shapeshiftSmokeAtMs = 0;
    m_druidMotionMs = 0;
    m_druidClawLookMs = 0;
    m_sentDruidForm.clear();
    if (IsDruidHero())
        m_player->GetBonkPlayer()->SendAddon("druidform~caster");
    if (IsDruidHero() && m_player->GetDisplayId() != m_player->GetNativeDisplayId())
        m_player->SetDisplayId(m_player->GetNativeDisplayId());
}

bool BonkRun::FireDruidSpell(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    switch (owned.weapon.behavior)
    {
        case BONK_BEHAVIOR_MOONFIRE:         CastMoonfire(owned, spellInfo); break;
        case BONK_BEHAVIOR_HURRICANE:        CallHurricane(owned); break;
        case BONK_BEHAVIOR_RAKE:             RakeEnemies(owned, spellInfo); break;
        case BONK_BEHAVIOR_SHRED:            ShredEnemy(owned, spellInfo); break;
        case BONK_BEHAVIOR_SWIPE:            SwipeAround(owned, spellInfo); break;
        case BONK_BEHAVIOR_FRENZIED_REGENERATION: RegenerateFrenzied(owned); break;
        case BONK_BEHAVIOR_REJUVENATION:     BloomRejuvenation(owned, spellInfo); break;
        case BONK_BEHAVIOR_ENTANGLING_ROOTS: EntangleEnemies(owned, spellInfo); break;
        case BONK_BEHAVIOR_TRANQUILITY:      StartTranquility(owned); break;
        case BONK_BEHAVIOR_THORNS:           owned.timer = BONK_DRUID_IDLE_MS; break;
        case BONK_BEHAVIOR_SHAPESHIFT:       FireFormAttack(owned); break;
        default:                             return false;
    }
    return true;
}

std::vector<Creature*> BonkRun::GetDruidTargets(float range) const
{
    std::vector<Creature*> mobs;
    for (Creature* mob : GetMobsInRange(range))
        if (m_player->IsWithinLOSInMap(mob))
            mobs.push_back(mob);
    return mobs;
}

bool BonkRun::IsDruidNear() const
{
    return !GetMobsInRange(BONK_DRUID_AWARE_RANGE).empty();
}

bool BonkRun::TakeDruidLookSlot(uint32 weaponId)
{
    uint32& lastMs = m_druidLookMs[weaponId];
    if (lastMs && m_totalMs - lastMs < BONK_DRUID_LOOK_GAP_MS)
        return false;
    lastMs = m_totalMs;
    return true;
}

void BonkRun::HealDruid(OwnedWeapon const& owned, float percent)
{
    if (!m_player->IsAlive() || percent <= 0.0f)
        return;
    SupportScope scope(*this, WeaponSupport(owned));
    float const scaled = percent * GetDruidFormScale(owned);
    HealHero(ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * scaled / 100.0f))), owned.weapon.spellId);
}

BonkRun::OwnedWeapon BonkRun::ScaleForForm(OwnedWeapon const& owned) const
{
    OwnedWeapon scaled = owned;
    scaled.damageBonus = ((1.0f + owned.damageBonus / 100.0f) * GetDruidFormScale(owned) - 1.0f) * 100.0f;
    return scaled;
}

BonkRun::BonkDruidForm BonkRun::GetDruidSpellForm(uint32 rootId) const
{
    switch (rootId)
    {
        case BONK_WEAPON_STARFIRE:
        case BONK_WEAPON_MOONFIRE:
        case BONK_WEAPON_HURRICANE:
            return BonkDruidForm::Moonkin;
        case BONK_WEAPON_RAKE:
        case BONK_WEAPON_SHRED:
            return BonkDruidForm::Cat;
        case BONK_WEAPON_SWIPE:
        case BONK_WEAPON_FRENZIED_REGENERATION:
        case BONK_WEAPON_THORNS:
            return BonkDruidForm::Bear;
        case BONK_WEAPON_REJUVENATION:
        case BONK_WEAPON_FORCE_OF_NATURE:
        case BONK_WEAPON_ENTANGLING_ROOTS:
        case BONK_WEAPON_TRANQUILITY:
            return BonkDruidForm::Tree;
        default:
            return BonkDruidForm::None;
    }
}

float BonkRun::GetDruidFormScale(OwnedWeapon const& owned) const
{
    if (!IsDruidHero() || m_druidForm == BonkDruidForm::None || !(owned.weapon.classMask & (1 << (CLASS_DRUID - 1))))
        return 1.0f;
    uint32 const root = GetRootWeapon(owned.weapon);
    if (root == BONK_WEAPON_SHAPESHIFT)
        return 1.0f;
    if (GetDruidSpellForm(root) == m_druidForm)
        return BONK_RIGHT_FORM_BONUS;
    return m_druidForm == BonkDruidForm::Bear ? BONK_BEAR_DAMAGE : 1.0f;
}

void BonkRun::FireFormAttack(OwnedWeapon& owned)
{
    FormAttack const& attack = GetFormAttack(m_druidForm);
    SpellEntry const* look = sSpellMgr.GetSpellEntry(attack.spellId);
    std::vector<Creature*> mobs;
    if (look)
        mobs = GetDruidTargets(attack.range * GetWeaponArea(owned));
    if (mobs.empty())
    {
        owned.timer = BONK_DRUID_POLL_MS;
        return;
    }
    owned.timer = int32(GetWeaponCooldown(owned) * attack.cooldownScale);
    PlayDruidMotion(attack.spellId);
    mobs.resize(std::min<size_t>(mobs.size(), attack.targets + GetSpellCount(owned)));
    float const damage = GetWeaponDamage(owned) * attack.share;
    for (Creature* mob : mobs)
    {
        ShowDruidStrike(look, mob);
        HitAsDruid(owned, look, mob, damage);
        if (attack.bleedStacks && mob->IsAlive())
            ApplyWeaponKeyword(owned, mob, BONK_KEYWORD_BLEED, attack.bleedStacks);
    }
    if (attack.healPerHit > 0.0f)
        HealDruid(owned, attack.healPerHit * mobs.size());
}

void BonkRun::ShapeshiftTo(uint32 form)
{
    if (form >= size_t(BonkDruidForm::Moonkin) && form <= size_t(BonkDruidForm::Tree))
        Shapeshift(BONK_FORM_ATTACKS[form].name);
}

void BonkRun::Shapeshift(char const* formName)
{
    if (m_state != STATE_RUNNING || !IsDruidHero() || !m_player->IsAlive() || !FindOwnedByRoot(BONK_WEAPON_SHAPESHIFT))
        return;
    BonkDruidForm next = BonkDruidForm(size_t(m_druidForm) % size_t(BonkDruidForm::Tree) + 1);
    if (formName && *formName)
    {
        bool found = false;
        for (size_t form = size_t(BonkDruidForm::Moonkin); form <= size_t(BonkDruidForm::Tree); ++form)
            if (!strcmp(formName, BONK_FORM_ATTACKS[form].name))
            {
                next = BonkDruidForm(form);
                found = true;
            }
        if (!found)
            return;
    }
    else if (m_totalMs < m_shapeshiftReadyMs)
        return;
    m_shapeshiftReadyMs = m_totalMs + BONK_SHAPESHIFT_COOLDOWN_MS;
    m_druidForm = next;
    UpdateDruidForm();
    m_shapeshiftSmokeAtMs = m_totalMs + BONK_SHAPESHIFT_SMOKE_DELAY_MS;
    if (OwnedWeapon* shapeshift = FindOwnedByRoot(BONK_WEAPON_SHAPESHIFT))
        shapeshift->timer = std::min(shapeshift->timer, BONK_DRUID_POLL_MS);
}

void BonkRun::HitAsDruid(OwnedWeapon const& owned, SpellEntry const* spellInfo, Creature* target, float damage)
{
    DealShamanHit(spellInfo, owned.weapon.spellId, target, damage);
    float const twice = GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_HIT_TWICE);
    if (twice > 0.0f && target->IsAlive() && roll_chance_f(twice))
        DealShamanHit(spellInfo, owned.weapon.spellId, target, damage);
}

void BonkRun::CastMoonfire(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    std::vector<Creature*> const mobs = GetMobsInRange(owned.weapon.range);
    if (mobs.empty())
    {
        owned.timer = BONK_DRUID_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    PlayDruidMotion(owned.weapon.spellId);
    AddWarlockStacks(ScaleForForm(owned), spellInfo, mobs, BONK_KEYWORD_MOONFIRE);
}

void BonkRun::CallHurricane(OwnedWeapon& owned)
{
    float x, y, z;
    if (!FindLargestGroup(owned.weapon.range, x, y, z))
    {
        owned.timer = BONK_DRUID_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    PlayDruidMotion(owned.weapon.spellId);
    StartKeywordCloud(owned, x, y, z);
}

bool BonkRun::GetDruidCloudKind(OwnedWeapon const& owned, KeywordCloudKind& kind) const
{
    if (GetRootWeapon(owned.weapon) != BONK_WEAPON_HURRICANE)
        return false;
    kind.lifeMs = BONK_HURRICANE_MS;
    kind.pulseEveryMs = BONK_HURRICANE_PULSE_MS;
    kind.stacks = owned.weapon.applyStacks;
    kind.maxAlive = BONK_HURRICANE_ALIVE;
    kind.lookSpellId = BONK_SPELL_HURRICANE_LOOK;
    return true;
}

void BonkRun::OnDruidCloudPulse(OwnedWeapon const& owned, KeywordCloud const& cloud)
{
    if (GetRootWeapon(owned.weapon) != BONK_WEAPON_HURRICANE)
        return;
    SpellEntry const* look = sSpellMgr.GetSpellEntry(owned.weapon.spellId);
    float const damage = GetWeaponDamage(owned);
    for (Creature* mob : GetMobsNear(cloud.x, cloud.y, cloud.radius))
        DealShamanHit(look, owned.weapon.spellId, mob, damage);
}

void BonkRun::RakeEnemies(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    std::vector<Creature*> mobs = GetDruidTargets(owned.weapon.range * GetWeaponArea(owned));
    if (mobs.empty())
    {
        owned.timer = BONK_DRUID_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    PlayDruidMotion(owned.weapon.spellId);
    mobs.resize(std::min<size_t>(mobs.size(), GetWeaponTargets(owned)));
    uint32 const stacks = owned.weapon.applyStacks + uint32(GetSpellUpgrade(owned, BONK_SPELL_UPGRADE_STACKS));
    float const damage = GetWeaponDamage(owned);
    for (Creature* mob : mobs)
    {
        ShowDruidStrike(spellInfo, mob);
        DealShamanHit(spellInfo, owned.weapon.spellId, mob, damage);
        if (mob->IsAlive())
            ApplyWeaponKeyword(ScaleForForm(owned), mob, BONK_KEYWORD_BLEED, stacks);
    }
}

void BonkRun::ShredEnemy(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    std::vector<Creature*> mobs = GetDruidTargets(owned.weapon.range * GetWeaponArea(owned));
    if (mobs.empty())
    {
        owned.timer = BONK_DRUID_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    PlayDruidMotion(owned.weapon.spellId);
    std::stable_sort(mobs.begin(), mobs.end(), [this](Creature* a, Creature* b)
    {
        return GetKeywordStacks(a, BONK_KEYWORD_BLEED) > GetKeywordStacks(b, BONK_KEYWORD_BLEED);
    });
    mobs.resize(std::min<size_t>(mobs.size(), GetWeaponTargets(owned)));
    for (Creature* mob : mobs)
    {
        float const bleeds = std::min(float(GetKeywordStacks(mob, BONK_KEYWORD_BLEED)), BONK_SHRED_MAX_STACKS);
        ShowDruidStrike(spellInfo, mob);
        HitAsDruid(owned, spellInfo, mob, GetWeaponDamage(owned) * (1.0f + owned.weapon.usesPct * bleeds / 100.0f));
    }
}

void BonkRun::SwipeAround(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    std::vector<Creature*> const mobs = GetMobsInRange(owned.weapon.range * GetWeaponArea(owned));
    if (mobs.empty())
    {
        owned.timer = BONK_DRUID_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    PlayDruidMotion(owned.weapon.spellId);
    float const damage = GetWeaponDamage(owned);
    size_t shown = 0;
    for (Creature* mob : mobs)
    {
        if (shown++ < BONK_SWIPE_LOOKS)
            ShowDruidStrike(spellInfo, mob);
        DealShamanHit(spellInfo, owned.weapon.spellId, mob, damage);
    }
}

float BonkRun::GetFrenziedRegenerationHeal(OwnedWeapon const& owned) const
{
    return owned.weapon.healPct * (1.0f + owned.damageBonus / 100.0f);
}

void BonkRun::RegenerateFrenzied(OwnedWeapon& owned)
{
    owned.timer = GetWeaponCooldown(owned);
    if (!m_player->IsAlive() || m_player->GetHealth() >= m_player->GetMaxHealth())
        return;
    size_t const nearby = GetMobsInRange(BONK_FRENZIED_REGEN_RADIUS * GetWeaponArea(owned)).size();
    if (!nearby)
        return;
    float const enemies = std::min(float(nearby), BONK_FRENZIED_REGEN_MAX_ENEMIES);
    HealDruid(owned, GetFrenziedRegenerationHeal(owned) * enemies);
    if (TakeDruidLookSlot(owned.weapon.id))
        if (SpellEntry const* look = sSpellMgr.GetSpellEntry(BONK_SPELL_FRENZIED_REGEN))
            ShowStrike(m_player, look, m_player);
}

void BonkRun::BloomRejuvenation(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    owned.timer = GetWeaponCooldown(owned);
    if (!m_player->IsAlive())
        return;
    if (TakeDruidLookSlot(owned.weapon.id))
        ShowStrike(m_player, spellInfo, m_player);
    HealDruid(owned, owned.weapon.healPct);

    std::vector<Creature*> mobs = GetDruidTargets(owned.weapon.range * GetWeaponArea(owned));
    mobs.resize(std::min<size_t>(mobs.size(), GetWeaponTargets(owned)));
    SpellEntry const* lash = sSpellMgr.GetSpellEntry(BONK_SPELL_BLOOM_LASH);
    if (!mobs.empty())
        PlayDruidMotion(owned.weapon.spellId);
    float const damage = GetWeaponDamage(owned);
    for (Creature* mob : mobs)
    {
        ShowStrike(m_player, lash, mob);
        HitAsDruid(owned, spellInfo, mob, damage);
    }
}

void BonkRun::EntangleEnemies(OwnedWeapon& owned, SpellEntry const* spellInfo)
{
    std::vector<Creature*> mobs = GetDruidTargets(owned.weapon.range);
    if (mobs.empty())
    {
        owned.timer = BONK_DRUID_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    PlayDruidMotion(owned.weapon.spellId);
    mobs.resize(std::min<size_t>(mobs.size(), GetWeaponTargets(owned)));
    float const damage = GetWeaponDamage(owned);
    for (Creature* mob : mobs)
    {
        ShowDruidStrike(spellInfo, mob);
        HitAsDruid(owned, spellInfo, mob, damage);
        int32 durationMs = int32(BONK_ROOT_MS);
        if (mob->IsAlive() && LimitRoot(mob, durationMs))
            HoldMob(mob, spellInfo, SPELL_AURA_MOD_ROOT, durationMs);
    }
}

void BonkRun::StartTranquility(OwnedWeapon& owned)
{
    if (!IsDruidNear() || !m_player->IsAlive())
    {
        owned.timer = BONK_DRUID_POLL_MS;
        return;
    }
    owned.timer = GetWeaponCooldown(owned);
    PlayDruidMotion(owned.weapon.spellId);
    m_tranquilityWeapon = owned.weapon.id;
    m_tranquilityPulsesLeft = std::max(1u, uint32(BONK_TRANQUILITY_PULSES * GetWeaponDurationScale(owned) + 0.5f));
    m_tranquilityPulseMs = 0;
}

void BonkRun::PulseTranquility(uint32 diff)
{
    if (!m_tranquilityPulsesLeft)
        return;
    OwnedWeapon* owned = FindOwned(m_tranquilityWeapon);
    if (!owned || !m_player->IsAlive())
    {
        m_tranquilityPulsesLeft = 0;
        return;
    }
    if (m_tranquilityPulseMs > diff)
    {
        m_tranquilityPulseMs -= diff;
        return;
    }
    m_tranquilityPulseMs = BONK_TRANQUILITY_PULSE_MS;
    --m_tranquilityPulsesLeft;

    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(owned->weapon.spellId);
    ShowStrike(m_player, spellInfo, m_player);
    float const damage = GetWeaponDamage(*owned);
    for (Creature* mob : GetMobsInRange(owned->weapon.range * GetWeaponArea(*owned)))
        DealShamanHit(spellInfo, owned->weapon.spellId, mob, damage);
    HealDruid(*owned, owned->weapon.healPct);
}

void BonkRun::OnDruidStruck(Creature* attacker)
{
    if (!attacker || !attacker->IsAlive() || IsPeriodicHit())
        return;
    OwnedWeapon const* thorns = FindOwnedByBehavior(BONK_BEHAVIOR_THORNS);
    if (!thorns || !TakeRateSlot(m_thornsTimes, BONK_THORNS_PER_SEC))
        return;
    SpellEntry const* look = sSpellMgr.GetSpellEntry(BONK_SPELL_THORNS);
    HitAsDruid(*thorns, look ? look : sSpellMgr.GetSpellEntry(thorns->weapon.spellId), attacker, GetWeaponDamage(*thorns));
}

uint32 BonkRun::ApplyDruidDamageTaken(uint32 damage) const
{
    if (m_druidForm != BonkDruidForm::Bear)
        return damage;
    return std::max(1u, uint32(damage * BONK_BEAR_TAKEN));
}

uint32 BonkRun::GetDruidPetFamily(OwnedWeapon const& owned) const
{
    return owned.weapon.classMask & (1 << (CLASS_DRUID - 1)) ? BONK_PET_FAMILY_TREANT : 0;
}

uint32 BonkRun::GetDruidFormDisplay(BonkDruidForm form) const
{
    bool const alliance = Player::TeamForRace(m_player->GetRace()) == ALLIANCE;
    switch (form)
    {
        case BonkDruidForm::Moonkin: return alliance ? BONK_DISPLAY_MOONKIN_ALLIANCE : BONK_DISPLAY_MOONKIN_HORDE;
        case BonkDruidForm::Cat:     return alliance ? BONK_DISPLAY_CAT_ALLIANCE : BONK_DISPLAY_CAT_HORDE;
        case BonkDruidForm::Bear:    return alliance ? BONK_DISPLAY_BEAR_ALLIANCE : BONK_DISPLAY_BEAR_HORDE;
        case BonkDruidForm::Tree:    return BONK_DISPLAY_TREE;
        default:                     return m_player->GetNativeDisplayId();
    }
}

void BonkRun::UpdateDruidForm()
{
    if (!IsDruidHero() || !m_player->IsAlive())
        return;
    bool const shifts = FindOwnedByRoot(BONK_WEAPON_SHAPESHIFT) != nullptr;
    if (!shifts)
        m_druidForm = BonkDruidForm::None;
    else if (m_druidForm == BonkDruidForm::None)
        m_druidForm = BonkDruidForm::Moonkin;
    std::string const sent = std::string("druidform~") + GetFormAttack(m_druidForm).name;
    if (sent != m_sentDruidForm)
    {
        m_sentDruidForm = sent;
        m_player->GetBonkPlayer()->SendAddon(sent);
    }
    uint32 const display = GetDruidFormDisplay(m_druidForm);
    if (m_player->GetDisplayId() != display)
        m_player->SetDisplayId(display);
}

void BonkRun::OnDruidPetStrike(OwnedWeapon const& owned, Creature* pet, Creature* prey)
{
    if (GetDruidPetFamily(owned) != BONK_PET_FAMILY_TREANT || !prey)
        return;
    SpellEntry const* roots = sSpellMgr.GetSpellEntry(BONK_SPELL_TREANT_ROOTS);
    if (!roots)
        return;
    std::vector<Creature*> mobs = GetMobsNear(prey->GetPositionX(), prey->GetPositionY(), BONK_TREANT_ROOT_RADIUS * GetSummonReach(owned));
    if (std::find(mobs.begin(), mobs.end(), prey) == mobs.end())
        mobs.push_back(prey);
    for (Creature* mob : mobs)
    {
        int32 durationMs = int32(BONK_ROOT_MS);
        if (mob->IsAlive() && pet->IsWithinLOSInMap(mob) && LimitRoot(mob, durationMs))
            HoldMob(mob, roots, SPELL_AURA_MOD_ROOT, durationMs);
    }
}

void BonkRun::UpdateDruid(uint32 diff)
{
    if (m_shapeshiftSmokeAtMs && m_totalMs >= m_shapeshiftSmokeAtMs)
    {
        m_shapeshiftSmokeAtMs = 0;
        m_player->SendPlaySpellVisualKit(BONK_KIT_SHAPESHIFT_SMOKE);
    }
    UpdateDruidForm();
    PulseTranquility(diff);
}
