#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    uint32 const BONK_ARRIVE_MS = 750;
    uint32 const BONK_SPELL_ARRIVE = 10389;
    uint32 const BONK_AGGRO_CRY_MS = 250;

    float const BONK_FLEE_STEP          = 12.0f;
    uint32 const BONK_FLEE_STEP_MS      = 700;
    float const BONK_FLEE_CLIMB         = 6.0f;
    float const BONK_FLEE_BLINK         = 15.0f;
    uint32 const BONK_FLEE_BLINK_MS     = 4000;

    uint32 const BONK_UNREACHABLE_MS = 1500;

    uint32 const BONK_CASTER_MANA = 1000000;

    float const BONK_SWIFT_SPEED        = 1.5f;
    uint32 const BONK_SPELL_SHIELD      = 17;
    float const BONK_SHIELD_HEALTH      = 0.15f;
    uint32 const BONK_SHIELD_INTERVAL   = 15000;
    float const BONK_BURN_RANGE         = 9.0f;
    float const BONK_BURN_RADIUS        = 8.0f;
    uint32 const BONK_BURN_WARN_MS      = 1500;
    float const BONK_BURN_DAMAGE        = 0.5f;
    uint32 const BONK_BURN_INTERVAL     = 3000;
    float const BONK_ERUPTION_RANGE     = 40.0f;
    float const BONK_ERUPTION_RADIUS    = 4.5f;
    uint32 const BONK_ERUPTION_WARN_MS  = 1500;
    uint32 const BONK_ERUPTION_INTERVAL = 6000;
    float const BONK_ERUPTION_DAMAGE    = 1.5f;

    uint32 const BONK_AREA_WARN_MS      = 1500;
    float const BONK_AREA_DRIFT         = 1.5f;
    float const BONK_AREA_MAX_SIZE      = 15.0f;
    uint32 const BONK_AREA_RETRY_MS     = 1500;
    uint32 const BONK_WINDUP_LOST_MS    = 1000;
    uint32 const BONK_DEAD_REMOVE_MS    = 2000;

    uint32 const BONK_SPELL_LIT_FUSE    = 33400;
    uint32 const BONK_EXPLODED_MS       = 1500;
    struct BonkBurst
    {
        uint32 spellId;
        uint32 kitId;
        bool fuse;
    };
    BonkBurst const BONK_BURSTS[] = {
        { 3204, 5009, true },
        { 19497, 5009, false },
        { 24024, 5009, false },
        { 20602, 984, false },
        { 28361, 6377, false },
    };

    BonkBurst const* GetBurst(BonkMob const* mob)
    {
        if (mob)
            for (BonkMobAbility const& ability : mob->abilities)
                if (ability.target == BONK_ABILITY_SELF)
                    for (BonkBurst const& burst : BONK_BURSTS)
                        if (burst.spellId == ability.spellId)
                            return &burst;
        return nullptr;
    }

    float GetAreaReach(SpellEntry const* spellInfo)
    {
        float reach = 0.0f;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            uint32 const a = spellInfo->EffectImplicitTargetA[i];
            uint32 const b = spellInfo->EffectImplicitTargetB[i];
            if (!spellInfo->Effect[i] ||
                (a != TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC && a != TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC &&
                 b != TARGET_ENUM_UNITS_ENEMY_AOE_AT_SRC_LOC && b != TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC))
                continue;
            reach = std::max(reach, Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(spellInfo->EffectRadiusIndex[i])));
        }
        return reach;
    }

    float GetAreaSize(float reach)
    {
        return reach >= 8.0f ? std::floor(reach) : std::floor(reach * 2.0f) / 2.0f;
    }

    bool DealsDamage(SpellEntry const* spellInfo)
    {
        for (SpellEffects effect : { SPELL_EFFECT_SCHOOL_DAMAGE, SPELL_EFFECT_HEALTH_LEECH, SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL,
            SPELL_EFFECT_WEAPON_PERCENT_DAMAGE, SPELL_EFFECT_WEAPON_DAMAGE, SPELL_EFFECT_NORMALIZED_WEAPON_DMG })
            if (spellInfo->HasEffect(effect))
                return true;
        return false;
    }

    float const BONK_CHARGE_MIN_RANGE   = 8.0f;
    float const BONK_CHARGE_MAX_RANGE   = 20.0f;
    float const BONK_CHARGE_AIM_RANGE   = 16.0f;
    float const BONK_CHARGE_MAX_LENGTH  = 30.0f;
    float const BONK_CHARGE_OVERSHOOT   = 5.0f;
    uint32 const BONK_CHARGE_WARN_MS    = 1200;
    uint32 const BONK_CHARGE_INTERVAL   = 8000;
    uint32 const BONK_CHARGE_RETRY_MS   = 500;
    float const BONK_CHARGE_SPEED       = 30.0f;
    float const BONK_CHARGE_DAMAGE      = 2.0f;
    float const BONK_CHARGE_CONTACT     = 1.75f;
    float const BONK_CHARGE_KNOCKBACK_SPEED = 8.0f;
    float const BONK_CHARGE_KNOCKBACK_LIFT  = 4.0f;
    uint32 const BONK_SPELL_CHARGE_HIT  = 24257;
    float const BONK_BOLT_LEAD_BY_STEP[3] = { 0.0f, 0.5f, 1.0f };
    float const BONK_BARRAGE_SPREAD     = 10.0f;
    uint32 const BONK_BARRAGE_STAGGER_MS = 300;
    float const BONK_SHAPE_DAMAGE       = 1.5f;

    float const BONK_BOLT_RADIUS        = 3.5f;
    uint32 const BONK_BOLT_MIN_WARN_MS  = 1500;
    uint32 const BONK_BOLT_AIM_BEFORE_MS = 150;
    uint32 const BONK_BOLT_TARGET_LINGER_MS = 3000;
    uint32 const BONK_PREDICT_MAX_AGE_MS = 1000;

    uint32 GetBoltFlightMs(SpellEntry const* spellInfo, float distance)
    {
        if (spellInfo->speed <= 0.0f)
            return 0;
        return uint32(std::max(5.0f, distance) / spellInfo->speed * IN_MILLISECONDS);
    }

    void PredictPlayer(Player const* player, uint32 aheadMs, float& x, float& y, float& z)
    {
        player->GetPosition(x, y, z);
        if (!player->IsMoving())
            return;
        uint32 const age = std::min(WorldTimer::getMSTimeDiffToNow(player->m_movementInfo.stime), BONK_PREDICT_MAX_AGE_MS);
        float px, py, pz, o;
        if (!player->ExtrapolateMovement(player->m_movementInfo, age + aheadMs, px, py, pz, o) || !player->IsWithinLOS(px, py, pz))
            return;
        player->UpdateAllowedPositionZ(px, py, pz);
        x = px;
        y = py;
        z = pz;
    }
    uint32 GetChargeWindupMs()
    {
        return BONK_CHARGE_WARN_MS + GetBonkTelegraphLeadMs(BONK_TELEGRAPH_LINE);
    }

    float const BONK_VAMPIRIC_HEAL      = 0.5f;
    float const BONK_FRENZIED_SPEED     = 1.5f;
    float const BONK_FORTIFIED_HEALTH   = 1.5f;
    float const BONK_FORTIFIED_SCALE    = 1.15f;
    float const BONK_PHASE_BOSS[2]      = { 66.0f, 33.0f };
    float const BONK_PHASE_MINIBOSS     = 50.0f;
    uint32 const BONK_PHASE_ADDS        = 5;
    uint32 const BONK_PHASE_MINI_ADDS   = 3;
    float const BONK_PHASE_ENRAGE       = 1.25f;
    float const BONK_PHASE_SHIELD       = 0.25f;
    int32 const BONK_VOID_FIRST_MS      = 3000;
    int32 const BONK_VOID_INTERVAL      = 7000;
    float const BONK_VOID_RANGE         = 40.0f;
    float const BONK_VOID_RADIUS        = 6.0f;
    uint32 const BONK_VOID_WARN_MS      = 1500;
    uint32 const BONK_VOID_DURATION_MS  = 8000;
    float const BONK_VOID_DAMAGE        = 0.4f;

    float const BONK_RAGING_HEALTH      = 30.0f;
    float const BONK_RAGING_DAMAGE      = 1.5f;
    uint32 const BONK_SPELL_RAGING      = 20368;
    float const BONK_BOLSTER_BONUS      = 0.2f;
    float const BONK_BOLSTER_SCALE      = 1.05f;
    uint32 const BONK_BOLSTER_MAX       = 10;
    float const BONK_NECROTIC_RANGE     = 10.0f;

    enum AffixCategory : uint8
    {
        AFFIX_PRESSURE,
        AFFIX_SUSTAIN,
        AFFIX_BULK,
        AFFIX_ENRAGE,
        MAX_AFFIX_CATEGORY
    };

    struct AffixInfo
    {
        char const* name;
        char const* icon;
        AffixCategory category;
        uint32 glow;
    };
    AffixInfo const BONK_AFFIXES[] =
    {
        { "Swift",      "Ability_Rogue_Sprint",              AFFIX_PRESSURE, 22580 },
        { "Explosive",  "Spell_Fire_SelfDestruct",           AFFIX_PRESSURE, 22579 },
        { "Necrotic",   "Spell_Shadow_AbominationExplosion", AFFIX_PRESSURE, 22577 },
        { "Vampiric",   "Spell_Shadow_LifeDrain02",          AFFIX_SUSTAIN,  22578 },
        { "Bolstering", "Ability_Warrior_BattleShout",       AFFIX_SUSTAIN,  22581 },
        { "Sanguine",   "Spell_Shadow_BloodBoil",            AFFIX_SUSTAIN,  22518 },
        { "Bulwark",    "Spell_Holy_PowerWordShield",        AFFIX_BULK,     26388 },
        { "Fortified",  "Ability_Warrior_DefensiveStance",   AFFIX_BULK,     22576 },
        { "Frenzied",   "Ability_GhoulFrenzy",               AFFIX_ENRAGE,   28126 },
        { "Raging",     "Ability_Racial_BloodRage",          AFFIX_ENRAGE,   31309 },
        { "Volcanic",   "Spell_Fire_Volcano",                AFFIX_PRESSURE, 22579 },
    };
}

BonkMobAI::BonkMobAI(Creature* creature) : CreatureAI(creature)
{
    if (creature->IsTemporarySummon())
        m_ownerGuid = static_cast<TemporarySummon*>(creature)->GetSummonerGuid();
}

thread_local uint32 BonkMobAI::s_nextPlateId = 0;
thread_local int32 BonkMobAI::s_nextAffixCount = -1;
thread_local char const* BonkMobAI::s_nextPlateIcon = nullptr;

void BonkMobAI::Install(Creature* creature)
{
    creature->SetPlateId(s_nextPlateId);
    s_nextPlateId = 0;
    creature->SetBonkEnemy(true);

    creature->GetMotionMaster()->Initialize();
    creature->SetAI(new BonkMobAI(creature));
}

Player* BonkMobAI::GetOwner() const
{
    return m_creature->GetMap()->GetPlayer(m_ownerGuid);
}

void BonkMobAI::Setup(BonkSpawnKind kind)
{
    m_kind = kind;
    m_creature->SetBonkNormal(kind == BONK_SPAWN_NORMAL);
    m_mob = sBonkMgr.GetMob(m_creature->GetEntry());

    if (m_mob && m_mob->role == BONK_ROLE_EXPLODER)
        if (BonkBurst const* burst = GetBurst(m_mob))
            if (burst->fuse)
                m_creature->AddAura(BONK_SPELL_LIT_FUSE);

    if (m_mob && !m_mob->abilities.empty())
    {
        for (BonkMobAbility const& ability : m_mob->abilities)
            m_abilityTimers.push_back(int32(ability.firstMs));

        if (m_creature->GetPowerType() != POWER_MANA)
            m_creature->SetPowerType(POWER_MANA);
        m_creature->SetModifierValue(UNIT_MOD_MANA, BASE_VALUE, float(BONK_CASTER_MANA));
        m_creature->SetMaxPower(POWER_MANA, BONK_CASTER_MANA);
        m_creature->SetPower(POWER_MANA, BONK_CASTER_MANA);
    }

    for (DispelType dispel : { DISPEL_POISON, DISPEL_DISEASE, DISPEL_CURSE })
        m_creature->ApplySpellImmune(0, IMMUNITY_DISPEL, dispel, false);
    m_creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_PERIODIC_LEECH, false);

    if (kind != BONK_SPAWN_NORMAL)
    {
        for (AuraType aura : { SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE, SPELL_AURA_MOD_CHARM })
        {
            if (aura == SPELL_AURA_MOD_STUN && kind == BONK_SPAWN_ELITE)
                continue;
            m_creature->ApplySpellImmune(0, IMMUNITY_STATE, aura, true);
            m_creature->RemoveSpellsCausingAura(aura);
        }
        m_creature->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, true);
    }

    Player* owner = GetOwner();
    BonkRun* run = owner ? owner->GetBonkPlayer()->GetRun() : nullptr;
    uint32 const affixes = s_nextAffixCount >= 0 ? uint32(s_nextAffixCount) : run ? run->GetEliteAffixes() : 1;
    s_nextAffixCount = -1;
    m_plateIcon = s_nextPlateIcon ? s_nextPlateIcon : "";
    s_nextPlateIcon = nullptr;
    if (kind == BONK_SPAWN_ELITE)
        RollAffixes(affixes);
    else if (kind == BONK_SPAWN_MINIBOSS)
        RollAffixes(affixes + 1);

    if (kind != BONK_SPAWN_NORMAL && owner)
        owner->GetBonkPlayer()->SendAddon("mob~" + std::to_string(kind) + "~" + m_creature->GetName() + "~" + GetAffixIcons() + "~" +
            std::to_string(m_creature->GetPlateId()) + "~" + GetUnitGuidText(m_creature));
}

bool BonkMobAI::MayCry(Creature* creature)
{
    BonkMobAI* ai = dynamic_cast<BonkMobAI*>(creature->AI());
    if (!ai)
        return true;
    if (ai->m_cried)
        return false;
    ai->m_cried = true;

    static thread_local uint32 s_lastCryMs = 0;
    uint32 const now = WorldTimer::getMSTime();
    if (s_lastCryMs && WorldTimer::getMSTimeDiff(s_lastCryMs, now) < BONK_AGGRO_CRY_MS)
        return false;
    s_lastCryMs = now;
    return true;
}

std::string BonkMobAI::GetUnitGuidText(Creature const* creature)
{
    uint64 const raw = creature->GetObjectGuid().GetRawValue();
    char text[19];
    snprintf(text, sizeof(text), "0x%08X%08X", uint32(raw >> 32), uint32(raw));
    return text;
}

void BonkMobAI::RollAffixes(uint32 count)
{
    std::vector<Affix> available;
    for (uint8 affix = 0; affix < MAX_AFFIX; ++affix)
        available.push_back(Affix(affix));

    std::vector<std::string> names;
    for (uint32 i = 0; i < count && !available.empty(); ++i)
    {
        uint32 const pick = urand(0, uint32(available.size() - 1));
        Affix const affix = available[pick];
        AffixCategory const category = BONK_AFFIXES[affix].category;
        available.erase(std::remove_if(available.begin(), available.end(), [category](Affix other)
        {
            return BONK_AFFIXES[other].category == category;
        }), available.end());

        m_affixes |= 1u << affix;
        names.push_back(BONK_AFFIXES[affix].name);
        m_creature->AddAura(BONK_AFFIXES[affix].glow);
    }

    if (HasAffix(AFFIX_SWIFT))
    {
        m_creature->SetBaseSpeedRate(MOVE_WALK, m_creature->GetBaseWalkSpeedRate() * BONK_SWIFT_SPEED);
        m_creature->SetBaseSpeedRate(MOVE_RUN, m_creature->GetBaseRunSpeedRate() * BONK_SWIFT_SPEED);
    }

    if (HasAffix(AFFIX_FRENZIED))
        m_creature->SetAttackTime(BASE_ATTACK, uint32(m_creature->GetAttackTime(BASE_ATTACK) / BONK_FRENZIED_SPEED));

    if (HasAffix(AFFIX_FORTIFIED))
    {
        ScaleHealth(BONK_FORTIFIED_HEALTH);
        m_creature->SetObjectScale(m_creature->GetObjectScale() * BONK_FORTIFIED_SCALE);
        m_creature->UpdateModelData();
    }

    if (!names.empty())
        if (Player* owner = GetOwner())
        {
            std::string list;
            for (size_t i = 0; i < names.size(); ++i)
                list += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
            owner->GetBonkPlayer()->Notify(std::string(m_creature->GetName()) + " is " + list + "!");
        }
}

void BonkMobAI::ScaleHealth(float factor)
{
    float const maxHealth = m_creature->GetMaxHealth() * factor;
    uint32 const health = std::max(1u, uint32(m_creature->GetHealth() * factor));
    m_creature->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, maxHealth);
    m_creature->SetMaxHealth(uint32(maxHealth));
    m_creature->SetHealth(std::min(health, m_creature->GetMaxHealth()));
}

float BonkMobAI::GetNecroticRange() const
{
    if (!HasAffix(AFFIX_NECROTIC))
        return 0.0f;
    if (m_mob && m_mob->role == BONK_ROLE_RANGED)
        return std::max(BONK_NECROTIC_RANGE, m_mob->range);
    return BONK_NECROTIC_RANGE;
}

uint32 BonkMobAI::GetAffixCount() const
{
    uint32 count = 0;
    for (uint8 affix = 0; affix < MAX_AFFIX; ++affix)
        if (HasAffix(Affix(affix)))
            ++count;
    return count;
}

std::string BonkMobAI::GetAffixIcons() const
{
    std::string icons = m_plateIcon;
    for (uint8 affix = 0; affix < MAX_AFFIX; ++affix)
        if (HasAffix(Affix(affix)))
            icons += (icons.empty() ? "" : ",") + std::string(BONK_AFFIXES[affix].icon);
    return icons;
}

std::string BonkMobAI::GetAffixNames() const
{
    std::string names;
    for (uint8 affix = 0; affix < MAX_AFFIX; ++affix)
        if (HasAffix(Affix(affix)))
            names += (names.empty() ? "" : ",") + std::string(BONK_AFFIXES[affix].name);
    return names;
}

void BonkMobAI::SendAffixInfo(BonkPlayer* bonk)
{
    auto percent = [](float value)
    {
        char text[16];
        snprintf(text, sizeof(text), "%g%%", value);
        return std::string(text);
    };
    auto seconds = [](uint32 ms)
    {
        char text[16];
        snprintf(text, sizeof(text), ms == 1000 ? "%g second" : "%g seconds", ms / 1000.0f);
        return std::string(text);
    };

    for (uint8 affix = 0; affix < MAX_AFFIX; ++affix)
    {
        std::string text;
        switch (Affix(affix))
        {
            case AFFIX_SWIFT:      text = "Moves " + percent((BONK_SWIFT_SPEED - 1.0f) * 100.0f) + " faster."; break;
            case AFFIX_EXPLOSIVE:  text = "While you are near it, fire erupts around it every " + seconds(BONK_BURN_INTERVAL) + ". A red circle warns you first."; break;
            case AFFIX_NECROTIC:   text = "Each hit reduces the healing you receive, and the effect stacks. Move away from it to clear the effect."; break;
            case AFFIX_VAMPIRIC:   text = "Heals for " + percent(BONK_VAMPIRIC_HEAL * 100.0f) + " of the damage it deals."; break;
            case AFFIX_BOLSTERING: text = "When it dies, nearby enemies gain " + percent(BONK_BOLSTER_BONUS * 100.0f) + " more health and damage."; break;
            case AFFIX_SANGUINE:   text = "When it dies, it leaves a pool that heals enemies standing in it."; break;
            case AFFIX_BULWARK:    text = "Shields itself for " + percent(BONK_SHIELD_HEALTH * 100.0f) + " of its health, and gets a new shield " +
                                       seconds(BONK_SHIELD_INTERVAL) + " after the old one breaks or fades."; break;
            case AFFIX_FORTIFIED:  text = "Has " + percent((BONK_FORTIFIED_HEALTH - 1.0f) * 100.0f) + " more health."; break;
            case AFFIX_FRENZIED:   text = "Attacks " + percent((BONK_FRENZIED_SPEED - 1.0f) * 100.0f) + " faster."; break;
            case AFFIX_RAGING:     text = "Deals " + percent((BONK_RAGING_DAMAGE - 1.0f) * 100.0f) + " more damage when below " + percent(BONK_RAGING_HEALTH) + " health."; break;
            case AFFIX_VOLCANIC:   text = "Now and then, fire erupts under you. A red circle warns you first."; break;
            default: break;
        }
        bonk->SendAddon("affixinfo~" + std::string(BONK_AFFIXES[affix].name) + "~" + BONK_AFFIXES[affix].icon + "~" + text);
    }
}

void BonkMobAI::Bolster()
{
    if (m_bolsterStacks >= BONK_BOLSTER_MAX || !m_creature->IsAlive())
        return;
    ++m_bolsterStacks;
    ScaleHealth(1.0f + BONK_BOLSTER_BONUS);
    m_creature->SetObjectScale(m_creature->GetObjectScale() * BONK_BOLSTER_SCALE);
    m_creature->UpdateModelData();
}

float BonkMobAI::GetMeleeHit() const
{
    return (m_creature->GetFloatValue(UNIT_FIELD_MINDAMAGE) + m_creature->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2.0f;
}

float BonkMobAI::GetHitScale(BonkRun const* run) const
{
    float scale = (1.0f + BONK_BOLSTER_BONUS * m_bolsterStacks) * run->GetMobDamageMultiplier(m_creature);
    if (m_enraged)
        scale *= BONK_RAGING_DAMAGE;
    if (m_phaseEnraged)
        scale *= BONK_PHASE_ENRAGE;
    return scale;
}

BonkEnemyHit BonkMobAI::MakeHit(BonkRun const* run, uint32 spellId, uint32 damage, uint32 windId) const
{
    BonkEnemyHit hit;
    hit.caster = m_creature->GetObjectGuid();
    hit.striker = BonkRun::DescribeStriker(m_creature);
    hit.windId = windId;
    hit.spellId = spellId;
    hit.damage = damage;
    hit.scale = GetHitScale(run);
    return hit;
}

uint32 BonkMobAI::GetAreaDamage(BonkMobAbility const& ability, SpellEntry const* spellInfo) const
{
    if (ability.damage > 0.0f)
        return uint32(std::max(1.0f, GetMeleeHit() * ability.damage));
    return DealsDamage(spellInfo) ? uint32(std::max(1.0f, GetMeleeHit())) : 0;
}

void BonkMobAI::Arrive()
{
    m_arriveTimer = BONK_ARRIVE_MS;
    Halt();
    m_creature->GetMotionMaster()->MoveIdle();
    m_creature->CastSpell(m_creature, BONK_SPELL_ARRIVE, true);
}

void BonkMobAI::Pull(float x, float y, float z, float speed, uint32 settleMs)
{
    m_arriveTimer = int32(m_creature->GetDistance2d(x, y) / std::max(1.0f, speed) * IN_MILLISECONDS + settleMs);
    Halt();
    m_creature->GetMotionMaster()->MovePoint(0, x, y, z, MOVE_RUN_MODE | MOVE_FORCE_DESTINATION, speed);
}

void BonkMobAI::Halt()
{
    m_creature->m_targetNotReachableTimer = 0;

    if (m_chargeState != CHARGE_NONE)
    {
        m_chargeState = CHARGE_NONE;
        m_chargeTimer = BONK_CHARGE_INTERVAL / 2;
    }
    m_boltTarget.Clear();
    m_creature->InterruptNonMeleeSpells(false);
    m_creature->AttackStop();
    m_creature->StopMoving();
    m_creature->GetMotionMaster()->Clear(false);
}

void BonkMobAI::AttackStart(Unit* victim)
{
    Player* owner = GetOwner();
    if (!owner || victim != owner || m_arriveTimer > 0 || m_fleeing || m_stationary || m_passive || !m_raidTarget.IsEmpty() ||
        m_chargeState != CHARGE_NONE)
        return;

    SetMeleeAttack(true);
    CreatureAI::AttackStart(victim);

    if (m_mob && m_mob->role == BONK_ROLE_RANGED && m_creature->GetVictim() == victim)
        m_creature->GetMotionMaster()->MoveChase(victim, m_mob->range);
}

void BonkMobAI::EnterEvadeMode()
{
    Player* owner = GetOwner();
    BonkRun* run = owner ? owner->GetBonkPlayer()->GetRun() : nullptr;
    if (run && run->IsActive() && owner->IsAlive() && m_creature->IsAlive())
    {
        AttackStart(owner);
        return;
    }

    CreatureAI::EnterEvadeMode();
}

void BonkMobAI::UpdateAI(uint32 const diff)
{
    if (m_creature->HasCreatureState(CSTATE_DESPAWNING) || m_exploded)
        return;

    Player* owner = GetOwner();
    BonkRun* run = owner ? owner->GetBonkPlayer()->GetRun() : nullptr;
    if (!run || !run->IsActive() || !owner->IsAlive())
    {
        m_creature->DespawnOrUnsummon();
        return;
    }

    if (run->IsPaused())
    {
        SetFrozen(true);
        return;
    }
    SetFrozen(false);

    m_creature->SetCombatStartPosition(m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ());

    if (m_arriveTimer > 0)
    {
        m_arriveTimer -= int32(diff);
        if (m_arriveTimer > 0)
            return;
    }

    if (m_fleeing)
    {
        UpdateFlee(diff, owner);
        return;
    }
    if (m_stationary || m_passive)
        return;

    if (!m_raidTarget.IsEmpty())
    {
        Creature* target = m_creature->GetMap()->GetCreature(m_raidTarget);
        if (target && target->IsAlive())
        {
            if (m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
                m_creature->GetMotionMaster()->MoveChase(target);
            return;
        }
        m_raidTarget.Clear();
        AttackStart(owner);
    }

    if (UpdateCharge(diff, owner))
        return;
    if (UpdateArea(diff, owner))
        return;

    if (m_creature->GetVictim() != owner)
        AttackStart(owner);

    if (m_creature->m_targetNotReachableTimer > BONK_UNREACHABLE_MS)
    {
        m_creature->m_targetNotReachableTimer = 0;
        run->OnMobStuck(m_creature);
    }

    if (m_mob && m_mob->role == BONK_ROLE_EXPLODER)
    {
        if (m_creature->IsWithinDistInMap(owner, m_mob->range))
            StartArea(-1, owner);
        return;
    }

    UpdatePhases(diff, owner);
    UpdateAffixes(diff, owner);
    UpdateAbilities(diff, owner);
    DoMeleeAttackIfReady();
}

void BonkMobAI::SetFrozen(bool frozen)
{
    if (m_frozen == frozen)
        return;
    m_frozen = frozen;
    m_creature->SetTimeFrozen(frozen && m_creature->IsAlive());

    if (frozen)
    {
        if (m_stampede)
        {
            m_creature->DespawnOrUnsummon(1);
            return;
        }
        if (m_chargeState != CHARGE_NONE)
        {
            m_chargeState = CHARGE_NONE;
            m_chargeTimer = BONK_CHARGE_INTERVAL / 2;
        }
        m_boltTarget.Clear();
        if (m_areaWinding)
        {
            m_areaWinding = false;
            if (m_areaAbility >= 0 && m_areaAbility < int32(m_abilityTimers.size()))
                m_abilityTimers[m_areaAbility] = BONK_AREA_RETRY_MS;
        }
        m_creature->InterruptNonMeleeSpells(false);
        m_creature->AttackStop();
        m_creature->GetMotionMaster()->Clear(false);
        m_creature->GetMotionMaster()->MoveIdle();
        m_creature->AddUnitState(UNIT_STATE_ROOT);
        m_creature->SetRooted(true);
        return;
    }

    if (m_stationary)
        return;
    m_creature->ClearUnitState(UNIT_STATE_ROOT);
    m_creature->SetRooted(false);
    if (m_fleeing && !m_fleeStanding)
        m_fleeTimer = 0;
    else if (Player* owner = GetOwner())
        AttackStart(owner);
}

void BonkMobAI::Cast(BonkMobAbility const& ability, Player* owner, bool triggered)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(ability.spellId);
    if (!spellInfo)
        return;

    if (ability.target == BONK_ABILITY_PLAYER && spellInfo->HasEffect(SPELL_EFFECT_CHARGE))
    {
        StartCharge(owner, ability.damage > 0.0f ? ability.damage : BONK_CHARGE_DAMAGE);
        return;
    }

    if (ability.target == BONK_ABILITY_PLAYER && ability.damageEffectIndex >= 0 &&
        spellInfo->Effect[ability.damageEffectIndex] == SPELL_EFFECT_SCHOOL_DAMAGE)
        if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
        {
            AimBolt(ability, spellInfo, owner, run);
            return;
        }

    Unit* target = ability.target == BONK_ABILITY_SELF ? static_cast<Unit*>(m_creature) : owner;

    optional<int32> basePoints[MAX_EFFECT_INDEX];
    if (ability.damage > 0.0f && ability.damageEffectIndex >= 0)
        basePoints[ability.damageEffectIndex] = std::max(1, int32(GetMeleeHit() * ability.damage));

    if (target != m_creature)
        m_creature->SetFacingToObject(target);
    m_creature->CastCustomSpell(target, spellInfo, basePoints[0], basePoints[1], basePoints[2], triggered);
}

void BonkMobAI::AimBolt(BonkMobAbility const& ability, SpellEntry const* spellInfo, Player* owner, BonkRun* run)
{
    float const x = owner->GetPositionX(), y = owner->GetPositionY(), z = owner->GetPositionZ();
    uint32 const castTime = spellInfo->GetCastTime(m_creature);
    uint32 const flight = GetBoltFlightMs(spellInfo, m_creature->GetDistance(x, y, z));
    uint32 const lead = std::max(BONK_BOLT_AIM_BEFORE_MS, BONK_BOLT_MIN_WARN_MS > flight ? BONK_BOLT_MIN_WARN_MS - flight : 0);

    if (!run->ReserveBolt(std::max(castTime, lead) + flight))
        return;
    ObjectGuid const target = run->PlaceBoltTarget(x, y, z, std::max(castTime, lead) + 2 * flight + BONK_BOLT_TARGET_LINGER_MS);
    if (target.IsEmpty())
        return;

    m_boltTarget = target;
    m_boltSpell = spellInfo->Id;
    m_boltDamage = uint32(std::max(1.0f, GetMeleeHit() * ability.damage));
    m_boltLead = lead;
    m_boltStartTimer = castTime < lead ? int32(lead - castTime) : 0;
    m_boltAimTimer = castTime > lead ? int32(castTime - lead) : 0;
    m_boltStarted = m_boltAimed = false;
    UpdateBolt(0, owner);
}

void BonkMobAI::UpdateBolt(uint32 diff, Player* owner)
{
    m_boltStartTimer -= int32(diff);
    m_boltAimTimer -= int32(diff);

    if (!m_boltAimed && m_boltAimTimer <= 0)
        AimBoltNow(owner);
    if (!m_boltTarget.IsEmpty() && !m_boltStarted && m_boltStartTimer <= 0)
        LaunchBolt();
    if (m_boltStarted && m_boltAimed)
        m_boltTarget.Clear();
}

void BonkMobAI::AimBoltNow(Player* owner)
{
    m_boltAimed = true;
    Creature* target = m_creature->GetMap()->GetCreature(m_boltTarget);
    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(m_boltSpell);
    if (!target || !run || !spellInfo || (m_boltStarted && !m_creature->IsNonMeleeSpellCasted(false)))
    {
        if (target)
            target->DespawnOrUnsummon();
        m_boltTarget.Clear();
        return;
    }

    float const lead = BONK_BOLT_LEAD_BY_STEP[std::min(2u, run->GetDifficultyStep())];
    float x, y, z;
    uint32 flight = GetBoltFlightMs(spellInfo, m_creature->GetDistance(owner));
    PredictPlayer(owner, uint32((m_boltLead + flight) * lead), x, y, z);
    flight = GetBoltFlightMs(spellInfo, m_creature->GetDistance(x, y, z));
    PredictPlayer(owner, uint32((m_boltLead + flight) * lead), x, y, z);
    target->NearTeleportTo(x, y, z, 0.0f);

    ObjectGuid const targetGuid = m_boltTarget;
    BonkEnemyHit const hit = MakeHit(run, m_boltSpell, m_boltDamage);
    run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, BONK_BOLT_RADIUS, m_boltLead + flight,
        [run, hit, targetGuid, x, y]() { run->LandBolt(hit, targetGuid, x, y, BONK_BOLT_RADIUS); });
}

void BonkMobAI::LaunchBolt()
{
    m_boltStarted = true;
    Creature* target = m_creature->GetMap()->GetCreature(m_boltTarget);
    if (!target)
    {
        m_boltTarget.Clear();
        return;
    }
    m_creature->SetFacingToObject(target);
    m_creature->CastCustomSpell(target, m_boltSpell, 1, 1, 1, false);
}

void BonkMobAI::UpdateAbilities(uint32 diff, Player* owner)
{
    if (!m_mob)
        return;

    for (int32& timer : m_abilityTimers)
        timer -= int32(diff);

    if (!m_boltTarget.IsEmpty())
    {
        UpdateBolt(diff, owner);
        return;
    }

    if (m_creature->IsNonMeleeSpellCasted(false))
        return;

    float const distance = m_creature->GetDistance(owner);
    for (size_t i = 0; i < m_mob->abilities.size(); ++i)
    {
        BonkMobAbility const& ability = m_mob->abilities[i];
        if (m_abilityTimers[i] > 0 || distance < ability.minRange || distance > ability.maxRange)
            continue;
        if (m_creature->GetHealthPercent() > ability.belowHealth)
            continue;
        if (ability.target == BONK_ABILITY_PLAYER && !m_creature->IsWithinLOSInMap(owner))
            continue;

        m_abilityTimers[i] = int32(ability.cooldownMs * frand(0.85f, 1.15f));
        if (ability.shape != BONK_SHAPE_SPELL)
        {
            StartShape(int32(i), owner);
            return;
        }
        SpellEntry const* spellInfo = ability.target == BONK_ABILITY_SELF ? sSpellMgr.GetSpellEntry(ability.spellId) : nullptr;
        if (spellInfo && GetAreaReach(spellInfo) > 0.0f)
            StartArea(int32(i), owner);
        else
            Cast(ability, owner, false);
        return;
    }
}

void BonkMobAI::UpdateAffixes(uint32 diff, Player* owner)
{
    if (HasAffix(AFFIX_BULWARK))
    {
        if (m_creature->HasAura(BONK_SPELL_SHIELD))
            m_shieldTimer = BONK_SHIELD_INTERVAL;
        else
        {
            m_shieldTimer -= int32(diff);
            if (m_shieldTimer <= 0)
            {
                m_shieldTimer = BONK_SHIELD_INTERVAL;
                int32 const absorb = std::max(1, int32(m_creature->GetMaxHealth() * BONK_SHIELD_HEALTH));
                m_creature->CastCustomSpell(m_creature, BONK_SPELL_SHIELD, absorb, {}, {}, true);
            }
        }
    }

    if (HasAffix(AFFIX_EXPLOSIVE))
    {
        m_burnTimer -= int32(diff);
        if (m_burnTimer <= 0 && m_creature->IsWithinDistInMap(owner, BONK_BURN_RANGE))
        {
            m_burnTimer = BONK_BURN_INTERVAL;
            if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
            {
                float const x = m_creature->GetPositionX(), y = m_creature->GetPositionY(), z = m_creature->GetPositionZ();
                BonkEnemyHit const hit = MakeHit(run, 0, uint32(std::max(1.0f, GetMeleeHit() * BONK_BURN_DAMAGE)));
                run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, BONK_BURN_RADIUS, BONK_BURN_WARN_MS,
                    [run, hit, x, y, z]() { run->Erupt(hit, x, y, z, BONK_BURN_RADIUS); });
            }
        }
    }

    if (HasAffix(AFFIX_VOLCANIC))
    {
        m_eruptionTimer -= int32(diff);
        if (m_eruptionTimer <= 0 && m_creature->IsWithinDistInMap(owner, BONK_ERUPTION_RANGE))
        {
            m_eruptionTimer = BONK_ERUPTION_INTERVAL;
            if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
            {
                float const x = owner->GetPositionX(), y = owner->GetPositionY(), z = owner->GetPositionZ();
                BonkEnemyHit const hit = MakeHit(run, 0, uint32(std::max(1.0f, GetMeleeHit() * BONK_ERUPTION_DAMAGE)));
                run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, BONK_ERUPTION_RADIUS, BONK_ERUPTION_WARN_MS,
                    [run, hit, x, y, z]() { run->Erupt(hit, x, y, z, BONK_ERUPTION_RADIUS); });
            }
        }
    }

    if (HasAffix(AFFIX_RAGING) && !m_enraged && m_creature->GetHealthPercent() < BONK_RAGING_HEALTH)
    {
        m_enraged = true;
        m_creature->AddAura(BONK_SPELL_RAGING);
        owner->GetBonkPlayer()->Notify(std::string(m_creature->GetName()) + " is enraged!");
    }
}

void BonkMobAI::Explode(Player* owner)
{
    if (m_exploded)
        return;
    m_exploded = true;
    m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
    m_creature->RemoveAurasDueToSpell(BONK_SPELL_LIT_FUSE);

    if (BonkBurst const* burst = GetBurst(m_mob))
        m_creature->SendPlaySpellVisualKit(burst->kitId);

    if (m_mob)
        for (BonkMobAbility const& ability : m_mob->abilities)
            if (ability.target == BONK_ABILITY_SELF)
                Cast(ability, owner, true);

    if (m_creature->IsAlive())
    {
        m_creature->AttackStop();
        m_creature->StopMoving();
        m_creature->GetMotionMaster()->MoveIdle();
        m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
    }
    m_creature->DespawnOrUnsummon(BONK_EXPLODED_MS);
}

void BonkMobAI::StartArea(int32 abilityIndex, Player* owner)
{
    if (!m_mob || abilityIndex >= int32(m_mob->abilities.size()))
        return;

    float reach = 0.0f;
    uint32 damage = 0;
    uint32 spellId = 0;
    for (size_t i = 0; i < m_mob->abilities.size(); ++i)
    {
        BonkMobAbility const& ability = m_mob->abilities[i];
        if (abilityIndex >= 0 ? int32(i) != abilityIndex : ability.target != BONK_ABILITY_SELF)
            continue;
        if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(ability.spellId))
        {
            reach = std::max(reach, GetAreaReach(spellInfo));
            damage += GetAreaDamage(ability, spellInfo);
            if (!spellId)
                spellId = ability.spellId;
        }
    }

    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    if (!run || reach <= 0.0f)
    {
        if (abilityIndex < 0)
            Explode(owner);
        else
            Cast(m_mob->abilities[abilityIndex], owner, false);
        return;
    }
    float const size = GetAreaSize(reach);
    if (size > BONK_AREA_MAX_SIZE)
        return;

    m_areaWinding = true;
    m_areaWindId = ++m_windCount;
    m_areaAbility = abilityIndex;
    m_areaTimer = BONK_AREA_WARN_MS;
    m_areaX = m_creature->GetPositionX();
    m_areaY = m_creature->GetPositionY();
    m_creature->AttackStop();
    m_creature->StopMoving();
    m_creature->GetMotionMaster()->Clear(false);
    m_creature->GetMotionMaster()->MoveIdle();
    m_creature->SetFacingToObject(owner);
    float const x = m_areaX, y = m_areaY, z = m_creature->GetPositionZ();
    BonkEnemyHit const hit = MakeHit(run, spellId, damage, m_areaWindId);
    run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, size, BONK_AREA_WARN_MS,
        [run, hit, x, y, z, size]() { run->LandWarning(hit, BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, size); });
}

void BonkMobAI::StartShape(int32 abilityIndex, Player* owner)
{
    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    if (!run || !m_mob || abilityIndex < 0 || abilityIndex >= int32(m_mob->abilities.size()))
        return;
    BonkMobAbility const& ability = m_mob->abilities[abilityIndex];
    float const size = std::max(1.0f, ability.size);
    uint32 const damage = uint32(std::max(1.0f, GetMeleeHit() * (ability.damage > 0.0f ? ability.damage : BONK_SHAPE_DAMAGE)));

    switch (ability.shape)
    {
        case BONK_SHAPE_BARRAGE:
        {
            BonkEnemyHit const hit = MakeHit(run, 0, damage);
            for (uint32 i = 0; i < std::max(1u, ability.count); ++i)
            {
                uint32 const warnMs = BONK_AREA_WARN_MS + i * BONK_BARRAGE_STAGGER_MS;
                float x, y, z;
                PredictPlayer(owner, i ? 0 : warnMs, x, y, z);
                if (i)
                {
                    float const angle = frand(0.0f, 2.0f * M_PI_F);
                    float const distance = frand(size, BONK_BARRAGE_SPREAD);
                    float const ax = x + distance * std::cos(angle), ay = y + distance * std::sin(angle);
                    float const az = m_creature->GetMap()->GetHeight(ax, ay, z + 5.0f, true, 15.0f);
                    if (az <= INVALID_HEIGHT)
                        continue;
                    x = ax;
                    y = ay;
                    z = az;
                }
                run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, size, warnMs,
                    [run, hit, x, y, z, size]() { run->Erupt(hit, x, y, z, size); });
            }
            return;
        }
        case BONK_SHAPE_TRAIL:
            run->SpawnPhaseAdds(m_creature, std::max(1u, ability.count), 0);
            return;
        default:
            break;
    }

    m_areaWinding = true;
    m_areaWindId = ++m_windCount;
    m_areaAbility = abilityIndex;
    m_areaTimer = BONK_AREA_WARN_MS;
    m_areaX = m_creature->GetPositionX();
    m_areaY = m_creature->GetPositionY();
    m_areaOrientation = m_creature->GetAngle(owner);
    m_creature->AttackStop();
    m_creature->StopMoving();
    m_creature->GetMotionMaster()->Clear(false);
    m_creature->GetMotionMaster()->MoveIdle();
    m_creature->SetFacingTo(m_areaOrientation);
    BonkTelegraphShape const shape = ability.shape == BONK_SHAPE_CONE ? BONK_TELEGRAPH_CONE : BONK_TELEGRAPH_RING;
    float const x = m_areaX, y = m_areaY, z = m_creature->GetPositionZ(), o = m_areaOrientation;
    BonkEnemyHit const hit = MakeHit(run, BONK_SPELL_CHARGE_HIT, damage, m_areaWindId);
    run->AddTelegraph(shape, x, y, z, o, size, BONK_AREA_WARN_MS,
        [run, hit, shape, x, y, z, o, size]() { run->LandWarning(hit, shape, x, y, z, o, size); });
}

bool BonkMobAI::UpdateArea(uint32 diff, Player* owner)
{
    if (!m_areaWinding)
        return false;
    m_areaTimer -= int32(diff);
    if (m_areaTimer > -int32(BONK_WINDUP_LOST_MS))
        return true;
    m_areaWinding = false;
    ResumeChase(owner);
    return true;
}

bool BonkMobAI::IsWindingUp(uint32 windId) const
{
    return (m_areaWinding && m_areaWindId == windId) || (m_chargeState == CHARGE_WINDUP && m_chargeWindId == windId);
}

void BonkMobAI::LandWindup(uint32 windId)
{
    Player* owner = GetOwner();
    if (!owner || m_frozen || !IsWindingUp(windId))
        return;
    if (m_areaWinding && m_areaWindId == windId)
        ResolveArea(owner);
    else
        Dash(owner);
}

void BonkMobAI::ResumeChase(Player* owner)
{
    AttackStart(owner);
    if (m_creature->GetVictim() == owner && m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        m_creature->GetMotionMaster()->MoveChase(owner, m_mob && m_mob->role == BONK_ROLE_RANGED ? m_mob->range : 0.0f);
}

void BonkMobAI::ResolveArea(Player* owner)
{
    m_areaWinding = false;

    float const dx = m_creature->GetPositionX() - m_areaX;
    float const dy = m_creature->GetPositionY() - m_areaY;
    bool const fizzles = dx * dx + dy * dy > BONK_AREA_DRIFT * BONK_AREA_DRIFT ||
        m_creature->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING);
    if (!fizzles)
    {
        if (m_areaAbility < 0)
        {
            Explode(owner);
            return;
        }
        BonkMobAbility const& ability = m_mob->abilities[m_areaAbility];
        if (ability.shape == BONK_SHAPE_CONE || ability.shape == BONK_SHAPE_RING)
        {
            m_creature->HandleEmote(ability.shape == BONK_SHAPE_CONE ? EMOTE_ONESHOT_ATTACK1H : EMOTE_ONESHOT_BATTLEROAR);
            float x, y, z;
            PredictPlayer(owner, 0, x, y, z);
            BonkTelegraphShape const shape = ability.shape == BONK_SHAPE_CONE ? BONK_TELEGRAPH_CONE : BONK_TELEGRAPH_RING;
            if (owner->IsAlive() && !owner->GetBonkPlayer()->IsDashInvulnerable() &&
                IsInBonkTelegraph(shape, m_areaX, m_areaY, m_areaOrientation, ability.size, x, y))
            {
                int32 const damage = std::max(1, int32(GetMeleeHit() * (ability.damage > 0.0f ? ability.damage : BONK_SHAPE_DAMAGE)));
                m_creature->CastCustomSpell(owner, BONK_SPELL_CHARGE_HIT, damage, {}, {}, true);
            }
        }
        else
            Cast(ability, owner, true);
    }

    ResumeChase(owner);
}

bool BonkMobAI::UpdateCharge(uint32 diff, Player* owner)
{
    bool const charger = m_archetype == BONK_ARCHETYPE_CHARGER && m_kind == BONK_SPAWN_NORMAL;
    if (!charger && m_chargeState == CHARGE_NONE)
        return false;

    m_chargeTimer -= int32(diff);
    switch (m_chargeState)
    {
        case CHARGE_WINDUP:
            if (m_chargeTimer <= -int32(BONK_WINDUP_LOST_MS))
                EndCharge(owner);
            return true;
        case CHARGE_DASH:
            UpdateDash(owner);
            if (m_chargeTimer <= 0)
                EndCharge(owner);
            return m_chargeState != CHARGE_NONE;
        default:
            break;
    }

    if (!charger || m_chargeTimer > 0)
        return false;
    float x, y, z;
    PredictPlayer(owner, GetChargeWindupMs(), x, y, z);
    float const aim = m_creature->GetDistance2d(x, y);
    if (aim < BONK_CHARGE_MIN_RANGE || aim > BONK_CHARGE_AIM_RANGE || m_creature->GetDistance(owner) > BONK_CHARGE_MAX_RANGE ||
        !m_creature->IsWithinLOSInMap(owner) || !m_creature->IsWithinLOS(x, y, z + 2.0f) ||
        m_creature->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED))
    {
        m_chargeTimer = BONK_CHARGE_RETRY_MS;
        return false;
    }
    StartCharge(owner, BONK_CHARGE_DAMAGE);
    return m_chargeState != CHARGE_NONE;
}

void BonkMobAI::StartCharge(Player* owner, float damage)
{
    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    if (!run)
        return;

    float x, y, z;
    PredictPlayer(owner, GetChargeWindupMs(), x, y, z);
    m_chargeX = m_creature->GetPositionX();
    m_chargeY = m_creature->GetPositionY();
    m_chargeZ = m_creature->GetPositionZ();
    m_chargeAngle = m_creature->GetAngle(x, y);
    float const length = std::min(BONK_CHARGE_MAX_LENGTH, m_creature->GetDistance2d(x, y) + BONK_CHARGE_OVERSHOOT);
    m_dashX = m_chargeX + length * std::cos(m_chargeAngle);
    m_dashY = m_chargeY + length * std::sin(m_chargeAngle);
    m_dashZ = m_chargeZ;
    m_creature->GetMap()->GetWalkHitPosition(nullptr, m_chargeX, m_chargeY, m_chargeZ, m_dashX, m_dashY, m_dashZ);
    m_chargeLength = m_creature->GetDistance2d(m_dashX, m_dashY);
    if (m_chargeLength < BONK_CHARGE_MIN_RANGE || m_chargeLength + 1.0f < std::min(length - BONK_CHARGE_OVERSHOOT, BONK_CHARGE_MAX_LENGTH))
    {
        m_chargeTimer = BONK_CHARGE_RETRY_MS;
        return;
    }

    m_chargeState = CHARGE_WINDUP;
    m_chargeTimer = GetChargeWindupMs();
    m_chargeDamage = damage;
    m_creature->AttackStop();
    m_creature->StopMoving();
    m_creature->GetMotionMaster()->Clear(false);
    m_creature->GetMotionMaster()->MoveIdle();
    m_creature->SetFacingTo(m_chargeAngle);
    WarnCharge(run, GetChargeWindupMs());
}

void BonkMobAI::WarnCharge(BonkRun* run, uint32 windupMs)
{
    m_chargeWindId = ++m_windCount;
    float const x = m_chargeX, y = m_chargeY, z = m_chargeZ, o = m_chargeAngle, length = m_chargeLength;
    BonkEnemyHit const hit = MakeHit(run, BONK_SPELL_CHARGE_HIT, uint32(std::max(1.0f, GetMeleeHit() * m_chargeDamage)), m_chargeWindId);
    run->AddTelegraph(BONK_TELEGRAPH_LINE, x, y, z, o, length, windupMs,
        [run, hit, x, y, z, o, length]() { run->LandWarning(hit, BONK_TELEGRAPH_LINE, x, y, z, o, length); });
}

void BonkMobAI::KnockBackHero(Player* hero, float angle)
{
    hero->KnockBack(angle, BONK_CHARGE_KNOCKBACK_SPEED, BONK_CHARGE_KNOCKBACK_LIFT);
}

void BonkMobAI::Dash(Player* owner)
{
    m_chargeState = CHARGE_DASH;
    m_chargeTimer = int32(m_chargeLength / BONK_CHARGE_SPEED * IN_MILLISECONDS) + 300;
    m_dashFromX = m_creature->GetPositionX();
    m_dashFromY = m_creature->GetPositionY();
    m_dashHit = false;
    m_creature->GetMotionMaster()->MovePoint(0, m_dashX, m_dashY, m_dashZ, MOVE_RUN_MODE | MOVE_FORCE_DESTINATION, BONK_CHARGE_SPEED);
}

void BonkMobAI::UpdateDash(Player* owner)
{
    float const toX = m_creature->GetPositionX(), toY = m_creature->GetPositionY();
    if (!m_dashHit && owner->IsAlive() && !owner->GetBonkPlayer()->IsDashInvulnerable())
    {
        float x, y, z;
        PredictPlayer(owner, 0, x, y, z);
        float const segX = toX - m_dashFromX, segY = toY - m_dashFromY;
        float const segLength2 = segX * segX + segY * segY;
        float const along = segLength2 > 0.0f ? std::max(0.0f, std::min(1.0f, ((x - m_dashFromX) * segX + (y - m_dashFromY) * segY) / segLength2)) : 0.0f;
        float const dx = x - (m_dashFromX + along * segX), dy = y - (m_dashFromY + along * segY);
        if (dx * dx + dy * dy <= BONK_CHARGE_CONTACT * BONK_CHARGE_CONTACT && std::fabs(z - m_creature->GetPositionZ()) < 3.0f)
        {
            m_dashHit = true;
            int32 const damage = std::max(1, int32(GetMeleeHit() * m_chargeDamage));
            m_creature->CastCustomSpell(owner, BONK_SPELL_CHARGE_HIT, damage, {}, {}, true);
            if (owner->IsAlive())
                KnockBackHero(owner, m_chargeAngle);
        }
    }
    m_dashFromX = toX;
    m_dashFromY = toY;
}

void BonkMobAI::EndCharge(Player* owner)
{
    m_chargeState = CHARGE_NONE;
    if (m_stampede)
    {
        m_creature->DespawnOrUnsummon(1);
        return;
    }
    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    m_chargeTimer = run && run->GetTier().chargeIntervalMs ? int32(run->GetTier().chargeIntervalMs) : BONK_CHARGE_INTERVAL;
    AttackStart(owner);
    if (m_creature->GetVictim() == owner && m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        m_creature->GetMotionMaster()->MoveChase(owner, m_mob && m_mob->role == BONK_ROLE_RANGED ? m_mob->range : 0.0f);
}

void BonkMobAI::DamageDeal(Unit* victim, uint32& damage)
{
    if (!damage || victim == m_creature)
        return;

    if (victim->GetObjectGuid() == m_ownerGuid)
        if (BonkRun* run = static_cast<Player*>(victim)->GetBonkPlayer()->GetRun())
        {
            if (static_cast<Player*>(victim)->GetBonkPlayer()->IsDashInvulnerable())
            {
                damage = 0;
                return;
            }
            float const multiplier = GetHitScale(run);
            run->OnHeroStruck(m_creature);
            damage = std::max(1u, uint32(damage * multiplier));
            if (HasAffix(AFFIX_NECROTIC))
                run->AddNecroticStack();
            damage = run->MitigateDamage(damage);
            run->OnHeroHitBy(m_creature, damage);
        }

    if (HasAffix(AFFIX_VAMPIRIC))
        m_creature->ModifyHealth(int32(damage * BONK_VAMPIRIC_HEAL));
}

void BonkMobAI::DamageTaken(Unit* attacker, uint32& damage)
{
    if (m_fleeing && m_blinks && damage && damage < m_creature->GetHealth() && m_blinkTimer <= 0)
        m_blinkPending = true;

    if (!damage || !attacker || attacker->GetObjectGuid() != m_ownerGuid)
        return;

    if (BonkRun* run = static_cast<Player*>(attacker)->GetBonkPlayer()->GetRun())
    {
        run->ModifyHeroHit(m_creature, damage);
        run->OnWeaponDamage(std::min(damage, m_creature->GetHealth()));
    }
}

void BonkMobAI::UpdatePhases(uint32 diff, Player* owner)
{
    if (m_kind != BONK_SPAWN_BOSS && m_kind != BONK_SPAWN_MINIBOSS)
        return;
    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    if (!run)
        return;

    bool const boss = m_kind == BONK_SPAWN_BOSS;
    uint8 const phases = boss ? 2 : 1;
    uint32 const step = run->GetDifficultyStep();
    BonkPlayer* bonk = owner->GetBonkPlayer();
    std::string const name = m_creature->GetName();
    if (m_phase < phases && m_creature->GetHealthPercent() < (boss ? BONK_PHASE_BOSS[m_phase] : BONK_PHASE_MINIBOSS))
    {
        ++m_phase;
        run->SpawnPhaseAdds(m_creature, boss ? BONK_PHASE_ADDS + 2 * step : BONK_PHASE_MINI_ADDS + step, boss ? step : 0);
        bonk->Notify(name + " calls for aid!");

        if (boss && m_phase == 1 && step >= 2)
        {
            int32 const absorb = std::max(1, int32(m_creature->GetMaxHealth() * BONK_PHASE_SHIELD));
            m_creature->CastCustomSpell(m_creature, BONK_SPELL_SHIELD, absorb, {}, {}, true);
            bonk->Notify(name + " raises a shield! Break through it.");
        }

        if (boss && m_phase == phases)
        {
            m_phaseEnraged = true;
            m_creature->AddAura(BONK_SPELL_RAGING);
            bonk->Notify(name + " is enraged!");
            if (step >= 1)
            {
                m_voidZones = true;
                m_voidTimer = BONK_VOID_FIRST_MS;
            }
        }
    }

    if (!m_voidZones)
        return;
    m_voidTimer -= int32(diff);
    if (m_voidTimer > 0 || !m_creature->IsWithinDistInMap(owner, BONK_VOID_RANGE))
        return;
    m_voidTimer = BONK_VOID_INTERVAL;
    float const x = owner->GetPositionX(), y = owner->GetPositionY(), z = owner->GetPositionZ();
    BonkEnemyHit const hit = MakeHit(run, 0, uint32(std::max(1.0f, GetMeleeHit() * BONK_VOID_DAMAGE)));
    run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, BONK_VOID_RADIUS, BONK_VOID_WARN_MS,
        [run, hit, x, y, z]() { run->StartVoidZone(hit, x, y, z, BONK_VOID_RADIUS, BONK_VOID_DURATION_MS); });
}

void BonkMobAI::SetRaidTarget(ObjectGuid target)
{
    m_raidTarget = target;
    m_creature->AttackStop();
    m_creature->GetMotionMaster()->Clear(false);
    if (Creature* caravan = m_creature->GetMap()->GetCreature(target))
        m_creature->GetMotionMaster()->MoveChase(caravan);
}

void BonkMobAI::SetStationary()
{
    m_stationary = true;
    m_creature->AttackStop();
    m_creature->StopMoving();
    m_creature->GetMotionMaster()->Clear(false);
    m_creature->GetMotionMaster()->MoveIdle();
    m_creature->AddUnitState(UNIT_STATE_ROOT);
    m_creature->SetRooted(true);
}

void BonkMobAI::SetPassive(bool passive)
{
    if (m_passive == passive)
        return;
    m_passive = passive;

    if (!passive)
    {
        if (Player* owner = GetOwner())
            AttackStart(owner);
        return;
    }

    if (m_areaWinding)
    {
        m_areaWinding = false;
        if (m_areaAbility >= 0 && m_areaAbility < int32(m_abilityTimers.size()))
            m_abilityTimers[m_areaAbility] = BONK_AREA_RETRY_MS;
    }
    Halt();
    m_creature->GetMotionMaster()->MoveIdle();
}

void BonkMobAI::StartStampede(Player* owner, float angle, float length, uint32 windupMs, float damage)
{
    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    if (!run)
        return;

    m_stampede = true;
    m_chargeX = m_creature->GetPositionX();
    m_chargeY = m_creature->GetPositionY();
    m_chargeZ = m_creature->GetPositionZ();
    m_chargeAngle = angle;
    m_dashX = m_chargeX + length * std::cos(angle);
    m_dashY = m_chargeY + length * std::sin(angle);
    m_dashZ = m_chargeZ;
    m_creature->GetMap()->GetWalkHitPosition(nullptr, m_chargeX, m_chargeY, m_chargeZ, m_dashX, m_dashY, m_dashZ);
    m_chargeLength = m_creature->GetDistance2d(m_dashX, m_dashY);
    if (m_chargeLength < BONK_CHARGE_MIN_RANGE)
    {
        m_creature->DespawnOrUnsummon(1);
        return;
    }

    m_chargeState = CHARGE_WINDUP;
    m_chargeTimer = int32(windupMs);
    m_chargeDamage = damage;
    m_creature->AttackStop();
    m_creature->StopMoving();
    m_creature->GetMotionMaster()->Clear(false);
    m_creature->GetMotionMaster()->MoveIdle();
    m_creature->SetFacingTo(angle);
    WarnCharge(run, windupMs);
}

void BonkMobAI::SetFleeing(bool blinks, uint32 headStartMs)
{
    m_fleeing = true;
    m_blinks = blinks;
    m_fleeTimer = int32(headStartMs);
    m_fleeStanding = headStartMs > 0;
    m_creature->AttackStop();
    m_creature->GetMotionMaster()->Clear(false);
    m_creature->GetMotionMaster()->MoveIdle();
    if (Player* owner = GetOwner())
        m_creature->SetFacingToObject(owner);
}

void BonkMobAI::UpdateFlee(uint32 diff, Player* owner)
{
    m_blinkTimer -= int32(diff);
    if (m_blinkPending)
    {
        m_blinkPending = false;
        float x, y, z;
        if (FindFleePoint(owner, BONK_FLEE_BLINK, x, y, z))
        {
            m_creature->GetMotionMaster()->Clear(false);
            m_creature->NearTeleportTo(x, y, z, owner->GetAngle(x, y));
            m_creature->CastSpell(m_creature, BONK_SPELL_ARRIVE, true);
            m_blinkTimer = BONK_FLEE_BLINK_MS;
            m_fleeTimer = 0;
        }
    }

    m_fleeTimer -= int32(diff);
    if (m_fleeTimer > 0)
        return;
    m_fleeTimer = BONK_FLEE_STEP_MS;
    m_fleeStanding = false;

    float x, y, z;
    if (FindFleePoint(owner, BONK_FLEE_STEP, x, y, z))
        m_creature->GetMotionMaster()->MovePoint(1, x, y, z, MOVE_PATHFINDING | MOVE_RUN_MODE);
}

bool BonkMobAI::FindFleePoint(Player* owner, float distance, float& x, float& y, float& z) const
{
    Map* map = m_creature->GetMap();
    float const away = owner->GetAngle(m_creature);
    for (float swerve : { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f, 2.2f, -2.2f })
    {
        float const angle = away + swerve + frand(-0.25f, 0.25f);
        x = m_creature->GetPositionX() + distance * std::cos(angle);
        y = m_creature->GetPositionY() + distance * std::sin(angle);
        z = map->GetHeight(x, y, m_creature->GetPositionZ() + BONK_FLEE_CLIMB, true, BONK_FLEE_CLIMB * 2.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - m_creature->GetPositionZ()) > BONK_FLEE_CLIMB)
            continue;
        if (map->GetTerrain()->GetZoneId(x, y, z) != owner->GetZoneId() || map->GetTerrain()->IsInWater(x, y, z + 1.0f))
            continue;
        return true;
    }
    return false;
}

void BonkMobAI::JustSummoned(Creature* summoned)
{
    summoned->DespawnOrUnsummon(1);
}

std::string BonkMobAI::Describe() const
{
    static char const* const kinds[MAX_BONK_SPAWN_KIND] = { "normal", "elite", "boss", "mini-boss" };
    std::string text = std::string("kind ") + kinds[m_kind] + ", archetype " + sBonkMgr.GetArchetype(m_archetype).name;
    auto note = [&text](char const* name, bool on) { text += std::string(", ") + name + (on ? " yes" : " no"); };
    note("passive", m_passive);
    note("stationary", m_stationary);
    note("fleeing", m_fleeing);
    note("frozen", m_frozen);
    note("winding up", m_areaWinding);
    note("charging", m_chargeState != CHARGE_NONE);
    note("exploded", m_exploded);
    note("raiding", !m_raidTarget.IsEmpty());
    note("melee", IsMeleeAttackEnabled());
    return text;
}

void BonkMobAI::JustDied(Unit* killer)
{
    m_creature->SetTimeFrozen(false);
    if (m_creature->HasStaticFlag(CREATURE_STATIC_FLAG_DESPAWN_INSTANTLY))
        m_creature->DespawnOrUnsummon(BONK_DEAD_REMOVE_MS);

    Player* owner = GetOwner();
    if (!owner || m_exploded)
        return;

    if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
        run->OnMobKilled(m_creature, m_kind, killer);
}
