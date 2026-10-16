/*
 * Vanilla Bonk: AI for run enemies.
 */

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
    // An enemy the run moved stands this long under a puff of smoke.
    uint32 const BONK_ARRIVE_MS = 750;
    uint32 const BONK_SPELL_ARRIVE = 10389;     // Spawn Smoke
    uint32 const BONK_AGGRO_CRY_MS = 250;       // between enemies' aggro yells

    // A fleeing treasure goblin.
    float const BONK_FLEE_STEP          = 12.0f;    // yards away at a time...
    uint32 const BONK_FLEE_STEP_MS      = 700;      // ...this often, so it never stops
    float const BONK_FLEE_CLIMB         = 6.0f;     // never up or down more than this a step
    float const BONK_FLEE_BLINK         = 15.0f;    // a blink's reach, under a puff of smoke
    uint32 const BONK_FLEE_BLINK_MS     = 4000;     // between blinks

    // An enemy that cannot walk to the player this long is moved: the core
    // would make it evade (and take no damage) after 3 seconds.
    uint32 const BONK_UNREACHABLE_MS = 1500;

    // Most creature templates are warriors without mana; casters get plenty.
    uint32 const BONK_CASTER_MANA = 1000000;

    // Affixes.
    float const BONK_SWIFT_SPEED        = 1.5f;     // times the enemy's speed
    uint32 const BONK_SPELL_SHIELD      = 17;       // Power Word: Shield
    float const BONK_SHIELD_HEALTH      = 0.15f;    // absorbs this share of max health
    uint32 const BONK_SHIELD_INTERVAL   = 15000;    // after the last shield broke or ran out
    // Explosive: fire bursts up round the enemy after a red circle (it was
    // an unwarned Fire Nova until 2026-10-16: no enemy damage is
    // unavoidable). The burst stays where the circle was laid.
    float const BONK_BURN_RANGE         = 9.0f;
    float const BONK_BURN_RADIUS        = 8.0f;     // a telegraph size
    uint32 const BONK_BURN_WARN_MS      = 1500;
    float const BONK_BURN_DAMAGE        = 0.5f;     // melee hits
    uint32 const BONK_BURN_INTERVAL     = 3000;
    float const BONK_ERUPTION_RANGE     = 40.0f;    // erupts under a player this close
    float const BONK_ERUPTION_RADIUS    = 3.5f;
    uint32 const BONK_ERUPTION_WARN_MS  = 1500;
    uint32 const BONK_ERUPTION_INTERVAL = 6000;
    float const BONK_ERUPTION_DAMAGE    = 1.5f;     // melee hits

    // Areas round an enemy (StartArea): the red circle is seen this long,
    // and the spell fizzles if the enemy was moved further than this
    // meanwhile. A circle is at most BONK_AREA_MAX_SIZE; a spell reaching
    // further is never cast (world data shrinks those, 20261016160000).
    uint32 const BONK_AREA_WARN_MS      = 1500;
    float const BONK_AREA_DRIFT         = 1.5f;
    float const BONK_AREA_MAX_SIZE      = 15.0f;
    uint32 const BONK_AREA_RETRY_MS     = 1500;     // one dropped by a pause comes back this soon

    // How far an area round the enemy reaches: the largest radius of its
    // effects that land on enemies round a point, damage or not (a fear, a
    // slow and a curse are warned as a hit is); 0 for none (a buff on itself).
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

    // The circle for a reach. The hero is hit while their edge is inside
    // the spell's reach, so the circle takes in their size, rounded up to a
    // size the stand-ins come in (half yards to 8, whole yards after).
    float GetAreaSize(float reach, Player const* hero)
    {
        float const size = reach + hero->GetObjectBoundingRadius();
        return size <= 8.0f ? std::ceil(size * 2.0f) / 2.0f : std::ceil(size);
    }

    // Chargers: they stop, warn with a red line through where the player
    // will be as the warning ends, then dash along it; a player in the line
    // when it fills is hit. They only charge with that spot well within
    // reach (BONK_CHARGE_AIM_RANGE), so a player running away is not
    // charged at from the edge of range, and one coming at them is met head
    // on. How often depends on the difficulty (BonkTier::chargeIntervalMs).
    float const BONK_CHARGE_MIN_RANGE   = 8.0f;
    float const BONK_CHARGE_MAX_RANGE   = 20.0f;    // how far off the player may be now
    float const BONK_CHARGE_AIM_RANGE   = 16.0f;    // and where they will be
    float const BONK_CHARGE_MAX_LENGTH  = 30.0f;    // a charge never runs further, overshoot included
    float const BONK_CHARGE_OVERSHOOT   = 5.0f;     // the line goes on past the player
    uint32 const BONK_CHARGE_WARN_MS    = 1200;     // the line seen; the wind-up adds the line's lead (GetBonkTelegraphLeadMs)
    uint32 const BONK_CHARGE_INTERVAL   = 8000;     // when the run has no say
    uint32 const BONK_CHARGE_RETRY_MS   = 500;      // out of reach: looks again this soon
    float const BONK_CHARGE_SPEED       = 30.0f;    // yards a second
    float const BONK_CHARGE_DAMAGE      = 2.0f;     // melee hits
    // A dashing charger runs into the player when its path passes this close
    // (the line is 3 yards wide). It hits once and knocks them this fast
    // along the charge and up: about 3 yards.
    float const BONK_CHARGE_CONTACT     = 1.75f;
    float const BONK_CHARGE_KNOCKBACK_SPEED = 8.0f;
    float const BONK_CHARGE_KNOCKBACK_LIFT  = 4.0f;
    uint32 const BONK_SPELL_CHARGE_HIT  = 24257;    // Jeklik's Crushing Blow: a plain physical hit

    // Casters' bolts: a circle this big where the player will be when the
    // bolt lands, aimed as the cast ends (at least this long before, so a
    // missile already on its way is not turned round), warning at least this
    // long, and the bolt's target lingering a little after.
    float const BONK_BOLT_RADIUS        = 2.5f;     // a telegraph size (BonkTelegraph, 0.5 yd steps); 2 until Oct 15 (user: too small)
    uint32 const BONK_BOLT_MIN_WARN_MS  = 1500;    // a new object takes a moment to show on the client
    uint32 const BONK_BOLT_AIM_BEFORE_MS = 150;
    uint32 const BONK_BOLT_TARGET_LINGER_MS = 3000;
    // A player's movement packet comes every half second while they run
    // straight; the server keeps the last one's position.
    uint32 const BONK_PREDICT_MAX_AGE_MS = 1000;

    uint32 GetBoltFlightMs(SpellEntry const* spellInfo, float distance)
    {
        // An instant hit (no missile) lands as its cast ends.
        if (spellInfo->speed <= 0.0f)
            return 0;
        return uint32(std::max(5.0f, distance) / spellInfo->speed * IN_MILLISECONDS);
    }

    // Where the player will be `aheadMs` from now, on the ground: their last
    // movement packet carried forward (where they stand, through a wall).
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
    // A charger stands from the start of its charge to the dash: the line is
    // seen for BONK_CHARGE_WARN_MS once its stand-ins have turned to it.
    uint32 GetChargeWindupMs()
    {
        return BONK_CHARGE_WARN_MS + GetBonkTelegraphLeadMs(BONK_TELEGRAPH_LINE);
    }

    float const BONK_VAMPIRIC_HEAL      = 0.5f;     // share of damage dealt
    float const BONK_FRENZIED_SPEED     = 1.5f;     // times the enemy's attack speed
    float const BONK_FORTIFIED_HEALTH   = 1.5f;
    float const BONK_FORTIFIED_SCALE    = 1.15f;
    float const BONK_RAGING_HEALTH      = 30.0f;    // % of max health it enrages below
    float const BONK_RAGING_DAMAGE      = 1.5f;     // times its damage once enraged
    uint32 const BONK_SPELL_RAGING      = 20368;    // Tag: Red Skulls, while enraged
    float const BONK_BOLSTER_BONUS      = 0.2f;     // more health and damage per stack
    float const BONK_BOLSTER_SCALE      = 1.05f;    // bigger per stack
    uint32 const BONK_BOLSTER_MAX       = 10;
    // The hero keeps Necrotic stacks within this of a Necrotic enemy (melee
    // reach and some leeway); ranged ones count out to where they cast from.
    float const BONK_NECROTIC_RANGE     = 10.0f;

    enum AffixCategory : uint8
    {
        AFFIX_PRESSURE,
        AFFIX_SUSTAIN,
        AFFIX_BULK,
        AFFIX_ENRAGE,
        MAX_AFFIX_CATEGORY
    };

    // By BonkMobAI::Affix. `glow`: a dummy aura with a coloured glow, its look
    // on the enemy.
    struct AffixInfo
    {
        char const* name;
        char const* icon;
        AffixCategory category;
        uint32 glow;
    };
    AffixInfo const BONK_AFFIXES[] =
    {
        { "Swift",      "Ability_Rogue_Sprint",              AFFIX_PRESSURE, 22580 },   // Glowy (Yellow)
        { "Explosive",  "Spell_Fire_SelfDestruct",           AFFIX_PRESSURE, 22579 },   // Glowy (Orange)
        { "Necrotic",   "Spell_Shadow_AbominationExplosion", AFFIX_PRESSURE, 22577 },   // Glowy (Green)
        { "Vampiric",   "Spell_Shadow_LifeDrain02",          AFFIX_SUSTAIN,  22578 },   // Glowy (Black)
        { "Bolstering", "Ability_Warrior_BattleShout",       AFFIX_SUSTAIN,  22581 },   // Glowy (Purple)
        { "Sanguine",   "Spell_Shadow_BloodBoil",            AFFIX_SUSTAIN,  22518 },   // Glowy (Red)
        { "Bulwark",    "Spell_Holy_PowerWordShield",        AFFIX_BULK,     26388 },   // Glowy (NoColor)
        { "Fortified",  "Ability_Warrior_DefensiveStance",   AFFIX_BULK,     22576 },   // Glowy (Blue)
        { "Frenzied",   "Ability_GhoulFrenzy",               AFFIX_ENRAGE,   28126 },   // Spirit Particles (purple)
        { "Raging",     "Ability_Racial_BloodRage",          AFFIX_ENRAGE,   31309 },   // Spirit Particles (red, big)
        { "Volcanic",   "Spell_Fire_Volcano",                AFFIX_PRESSURE, 22579 },   // Glowy (Orange), as Explosive: never both
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
    // Before the enemy enters the world, so the client's first sight of it
    // already has its health out of its nameplate number: a change from the
    // usual percent later would leave the nameplate's bar empty until a hit.
    creature->SetPlateId(s_nextPlateId);
    s_nextPlateId = 0;
    // It hits only what it touches, and chases until it does: with the core's
    // melee leeway it would stop chasing 2.66 yards out of its swing and run
    // past a hero on the move. It also chases where the hero is now, not where
    // their last movement packet put them.
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

    // Only the template's looks carry over: a creature type or template that
    // shrugs off poison, disease, curses or drain effects does not here.
    for (DispelType dispel : { DISPEL_POISON, DISPEL_DISEASE, DISPEL_CURSE })
        m_creature->ApplySpellImmune(0, IMMUNITY_DISPEL, dispel, false);
    m_creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_PERIODIC_LEECH, false);

    // Elites and bosses shrug off holds and other crowd control; slows still
    // work, and elites can be stunned (briefly: BonkRun::LimitStun). Spells
    // that stun still deal their damage.
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

    // Elites roll the tier's number of affixes, mini-bosses one more.
    Player* owner = GetOwner();
    BonkRun* run = owner ? owner->GetBonkPlayer()->GetRun() : nullptr;
    uint32 const affixes = s_nextAffixCount >= 0 ? uint32(s_nextAffixCount) : run ? run->GetTier().eliteAffixes : 1;
    s_nextAffixCount = -1;
    m_plateIcon = s_nextPlateIcon ? s_nextPlateIcon : "";
    s_nextPlateIcon = nullptr;
    if (kind == BONK_SPAWN_ELITE)
        RollAffixes(affixes);
    else if (kind == BONK_SPAWN_MINIBOSS)
        RollAffixes(affixes + 1);

    // The addon's nameplates know elites and bosses by their GUID, without
    // ClassicAPI by their plate number (or name).
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

    // Per map thread: a run's enemies all update on its map's.
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
    // At most one from each category.
    std::vector<Affix> available;
    for (uint8 affix = 0; affix < MAX_AFFIX; ++affix)
        available.push_back(Affix(affix));

    std::string names;
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
        names += (names.empty() ? "" : ", ") + std::string(BONK_AFFIXES[affix].name);
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
        m_creature->UpdateModelData();     // melee reach follows the size
    }

    if (!names.empty())
        if (Player* owner = GetOwner())
            owner->GetBonkPlayer()->Notify(std::string(m_creature->GetName()) + ": " + names);
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
        snprintf(text, sizeof(text), "%g sec", ms / 1000.0f);
        return std::string(text);
    };

    for (uint8 affix = 0; affix < MAX_AFFIX; ++affix)
    {
        std::string text;
        switch (Affix(affix))
        {
            case AFFIX_SWIFT:      text = "Moves " + percent((BONK_SWIFT_SPEED - 1.0f) * 100.0f) + " faster."; break;
            case AFFIX_EXPLOSIVE:  text = "While you are close, fire bursts around it every " + seconds(BONK_BURN_INTERVAL) + ", after a red circle."; break;
            case AFFIX_NECROTIC:   text = "Each of its hits lowers the healing you take, more with every hit. Stay away to lose it."; break;
            case AFFIX_VAMPIRIC:   text = "Heals for " + percent(BONK_VAMPIRIC_HEAL * 100.0f) + " of the damage it deals."; break;
            case AFFIX_BOLSTERING: text = "When it dies, enemies nearby gain " + percent(BONK_BOLSTER_BONUS * 100.0f) + " health and damage."; break;
            case AFFIX_SANGUINE:   text = "When it dies, it leaves a pool that heals enemies standing in it."; break;
            case AFFIX_BULWARK:    text = "Shields itself for " + percent(BONK_SHIELD_HEALTH * 100.0f) + " of its health, and again " +
                                       seconds(BONK_SHIELD_INTERVAL) + " after a shield is gone."; break;
            case AFFIX_FORTIFIED:  text = "Has " + percent((BONK_FORTIFIED_HEALTH - 1.0f) * 100.0f) + " more health."; break;
            case AFFIX_FRENZIED:   text = "Attacks " + percent((BONK_FRENZIED_SPEED - 1.0f) * 100.0f) + " faster."; break;
            case AFFIX_RAGING:     text = "Deals " + percent((BONK_RAGING_DAMAGE - 1.0f) * 100.0f) + " more damage below " + percent(BONK_RAGING_HEALTH) + " health."; break;
            case AFFIX_VOLCANIC:   text = "Now and then fire bursts up under you, after a red circle."; break;
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

void BonkMobAI::Arrive()
{
    m_arriveTimer = BONK_ARRIVE_MS;
    m_creature->m_targetNotReachableTimer = 0;

    // Whatever it was doing is dropped, as when the run pauses.
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
    m_creature->GetMotionMaster()->MoveIdle();
    m_creature->CastSpell(m_creature, BONK_SPELL_ARRIVE, true);
}

void BonkMobAI::AttackStart(Unit* victim)
{
    // Only ever fight the player this run belongs to, and not while it
    // stands where the run moved it (a hit on it would start the chase),
    // nor while it winds up or dashes: a hit then would set it running.
    Player* owner = GetOwner();
    if (!owner || victim != owner || m_arriveTimer > 0 || m_fleeing || m_chargeState != CHARGE_NONE)
        return;

    CreatureAI::AttackStart(victim);

    // Ranged enemies stop short and cast from there.
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
    if (m_creature->HasCreatureState(CSTATE_DESPAWNING))
        return;

    Player* owner = GetOwner();
    BonkRun* run = owner ? owner->GetBonkPlayer()->GetRun() : nullptr;
    if (!run || !run->IsActive() || !owner->IsAlive())
    {
        m_creature->DespawnOrUnsummon();
        return;
    }

    // The run freezes every enemy when it pauses; this catches any it missed.
    if (run->IsPaused())
    {
        SetFrozen(true);
        return;
    }
    SetFrozen(false);

    // Leashing is measured from where combat started, so keep that under the mob.
    m_creature->SetCombatStartPosition(m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ());

    // Just moved by the run: it stands until the smoke clears.
    if (m_arriveTimer > 0)
    {
        m_arriveTimer -= int32(diff);
        if (m_arriveTimer > 0)
            return;
    }

    // A treasure goblin only ever runs.
    if (m_fleeing)
    {
        UpdateFlee(diff, owner);
        return;
    }

    // A charger winding up or dashing does nothing else.
    if (UpdateCharge(diff, owner))
        return;
    // Nor does one about to set off an area round it.
    if (UpdateArea(diff, owner))
        return;

    if (m_creature->GetVictim() != owner)
        AttackStart(owner);

    // Spawned in a tree or up on a roof: move it before it starts to evade.
    if (m_creature->m_targetNotReachableTimer > BONK_UNREACHABLE_MS)
    {
        m_creature->m_targetNotReachableTimer = 0;
        run->OnMobStuck(m_creature);
    }

    // An exploder lights its fuse: it stops under a red circle and blows up
    // as it fills, unless it is killed first.
    if (m_mob && m_mob->role == BONK_ROLE_EXPLODER)
    {
        if (m_creature->IsWithinDistInMap(owner, m_mob->range))
            StartArea(-1, owner);
        return;
    }

    UpdateAffixes(diff, owner);
    UpdateAbilities(diff, owner);
    // Without melee leeway (BonkRun::SpawnMobAt): only what it touches.
    DoMeleeAttackIfReady();
}

void BonkMobAI::SetFrozen(bool frozen)
{
    if (m_frozen == frozen)
        return;
    m_frozen = frozen;
    // Its auras (damage over time) and casts wait as well.
    m_creature->SetTimeFrozen(frozen && m_creature->IsAlive());

    if (frozen)
    {
        // A charge's or a bolt's warning is dropped by the pause; a charge
        // starts over after, a bolt waiting for its moment is not cast.
        if (m_chargeState != CHARGE_NONE)
        {
            m_chargeState = CHARGE_NONE;
            m_chargeTimer = BONK_CHARGE_INTERVAL / 2;
        }
        m_boltTarget.Clear();
        // So is an area's: it comes back soon after (an exploder lights
        // its fuse again once it is close).
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
        // Rooted, so a chase path still being worked out cannot start it moving.
        m_creature->AddUnitState(UNIT_STATE_ROOT);
        m_creature->SetRooted(true);
        return;
    }

    m_creature->ClearUnitState(UNIT_STATE_ROOT);
    m_creature->SetRooted(false);
    if (m_fleeing && !m_fleeStanding)
        m_fleeTimer = 0;            // off again at once (one still at its head start keeps the rest)
    else if (Player* owner = GetOwner())
        AttackStart(owner);
}

void BonkMobAI::Cast(BonkMobAbility const& ability, Player* owner, bool triggered)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(ability.spellId);
    if (!spellInfo)
        return;

    // A Charge (Stitches, Nefaru and the like) warns with a red line and
    // dashes along it, as chargers do: the spell itself lands at once and
    // cannot be dodged.
    if (ability.target == BONK_ABILITY_PLAYER && spellInfo->HasEffect(SPELL_EFFECT_CHARGE))
    {
        StartCharge(owner, ability.damage > 0.0f ? ability.damage : BONK_CHARGE_DAMAGE);
        return;
    }

    // A bolt flies at where the player stands, not at the player: a red
    // circle warns, and only a player still in it is hit. A spell with no
    // missile (Chain Bolt, Holy Wrath) is the same, landing as its cast
    // ends: no enemy damage is unavoidable (user, 2026-10-16).
    if (ability.target == BONK_ABILITY_PLAYER && ability.damageEffectIndex >= 0 &&
        spellInfo->Effect[ability.damageEffectIndex] == SPELL_EFFECT_SCHOOL_DAMAGE)
        if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
        {
            AimBolt(ability, spellInfo, owner, run);
            return;
        }

    Unit* target = ability.target == BONK_ABILITY_SELF ? static_cast<Unit*>(m_creature) : owner;

    // Spell damage follows the enemy's own hits, so it scales with the stage.
    optional<int32> basePoints[MAX_EFFECT_INDEX];
    if (ability.damage > 0.0f && ability.damageEffectIndex >= 0)
        basePoints[ability.damageEffectIndex] = std::max(1, int32(GetMeleeHit() * ability.damage));

    if (target != m_creature)
        m_creature->SetFacingToObject(target);
    m_creature->CastCustomSpell(target, spellInfo, basePoints[0], basePoints[1], basePoints[2], triggered);
}

void BonkMobAI::AimBolt(BonkMobAbility const& ability, SpellEntry const* spellInfo, Player* owner, BonkRun* run)
{
    // The enemy casts at a stand-in target where the player stands; as the
    // cast ends, the stand-in moves to where the player will be when the
    // bolt lands and a red circle warns there (AimBoltNow). The aim comes
    // early enough to warn at least BONK_BOLT_MIN_WARN_MS before the hit: a
    // quick cast waits before it starts, an instant one aims first.
    float const x = owner->GetPositionX(), y = owner->GetPositionY(), z = owner->GetPositionZ();
    uint32 const castTime = spellInfo->GetCastTime(m_creature);
    uint32 const flight = GetBoltFlightMs(spellInfo, m_creature->GetDistance(x, y, z));
    uint32 const lead = std::max(BONK_BOLT_AIM_BEFORE_MS, BONK_BOLT_MIN_WARN_MS > flight ? BONK_BOLT_MIN_WARN_MS - flight : 0);

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

    // An instant or quick cast aims before it starts, so the bolt flies
    // straight to the warned spot.
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
    // A cast that was stopped (a stun, a freeze) fires nothing.
    if (!target || !run || !spellInfo || (m_boltStarted && !m_creature->IsNonMeleeSpellCasted(false)))
    {
        if (target)
            target->DespawnOrUnsummon();
        m_boltTarget.Clear();
        return;
    }

    // Where the player will be when it lands: the rest of the cast, then the
    // flight to there (worked out from where they are, then once more).
    float x, y, z;
    uint32 flight = GetBoltFlightMs(spellInfo, m_creature->GetDistance(owner));
    PredictPlayer(owner, m_boltLead + flight, x, y, z);
    flight = GetBoltFlightMs(spellInfo, m_creature->GetDistance(x, y, z));
    PredictPlayer(owner, m_boltLead + flight, x, y, z);
    target->NearTeleportTo(x, y, z, 0.0f);

    ObjectGuid const guid = m_creature->GetObjectGuid();
    ObjectGuid const targetGuid = m_boltTarget;
    uint32 const spellId = m_boltSpell;
    uint32 const damage = m_boltDamage;
    run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, BONK_BOLT_RADIUS, m_boltLead + flight,
        [run, guid, targetGuid, spellId, x, y, damage]() { run->LandBolt(guid, targetGuid, spellId, x, y, BONK_BOLT_RADIUS, damage); });
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
    // Its real cast, so the bolt is seen to fly; the damage is the run's
    // (BonkRun::LandBolt), the target only soaks a token hit.
    m_creature->SetFacingToObject(target);
    m_creature->CastCustomSpell(target, m_boltSpell, 1, 1, 1, false);
}

void BonkMobAI::UpdateAbilities(uint32 diff, Player* owner)
{
    if (!m_mob)
        return;

    for (int32& timer : m_abilityTimers)
        timer -= int32(diff);

    // An aimed shot: its cast, and its aim as the cast ends.
    if (!m_boltTarget.IsEmpty())
    {
        UpdateBolt(diff, owner);
        return;
    }

    if (m_creature->IsNonMeleeSpellCasted(false))
        return;

    // One cast at a time: the first ready ability with the player in range.
    float const distance = m_creature->GetDistance(owner);
    for (size_t i = 0; i < m_mob->abilities.size(); ++i)
    {
        BonkMobAbility const& ability = m_mob->abilities[i];
        if (m_abilityTimers[i] > 0 || distance < ability.minRange || distance > ability.maxRange)
            continue;
        if (ability.target == BONK_ABILITY_PLAYER && !m_creature->IsWithinLOSInMap(owner))
            continue;

        m_abilityTimers[i] = int32(ability.cooldownMs * frand(0.85f, 1.15f));
        // An area round it waits under a red circle first.
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
    // The shield lasts longer than the wait, so the wait only starts once the
    // old shield is gone; otherwise it never comes down.
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
                ObjectGuid const guid = m_creature->GetObjectGuid();
                uint32 const damage = uint32(std::max(1.0f, GetMeleeHit() * BONK_BURN_DAMAGE));
                run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, BONK_BURN_RADIUS, BONK_BURN_WARN_MS,
                    [run, guid, x, y, z, damage]() { run->Erupt(guid, x, y, z, BONK_BURN_RADIUS, damage); });
            }
        }
    }

    // A red circle under the player; fire bursts up when it fills.
    if (HasAffix(AFFIX_VOLCANIC))
    {
        m_eruptionTimer -= int32(diff);
        if (m_eruptionTimer <= 0 && m_creature->IsWithinDistInMap(owner, BONK_ERUPTION_RANGE))
        {
            m_eruptionTimer = BONK_ERUPTION_INTERVAL;
            if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
            {
                float const x = owner->GetPositionX(), y = owner->GetPositionY(), z = owner->GetPositionZ();
                ObjectGuid const guid = m_creature->GetObjectGuid();
                uint32 const damage = uint32(std::max(1.0f, GetMeleeHit() * BONK_ERUPTION_DAMAGE));
                run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, x, y, z, 0.0f, BONK_ERUPTION_RADIUS, BONK_ERUPTION_WARN_MS,
                    [run, guid, x, y, z, damage]() { run->Erupt(guid, x, y, z, BONK_ERUPTION_RADIUS, damage); });
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

    // Blows up with everything it has, and leaves no reward behind.
    if (m_mob)
        for (BonkMobAbility const& ability : m_mob->abilities)
            if (ability.target == BONK_ABILITY_SELF)
                Cast(ability, owner, true);

    m_creature->DespawnOrUnsummon(400);
}

void BonkMobAI::StartArea(int32 abilityIndex, Player* owner)
{
    if (!m_mob || abilityIndex >= int32(m_mob->abilities.size()))
        return;

    // How far it reaches: the ability's spell, or for a blast the furthest
    // of the exploder's own.
    float reach = 0.0f;
    for (size_t i = 0; i < m_mob->abilities.size(); ++i)
    {
        BonkMobAbility const& ability = m_mob->abilities[i];
        if (abilityIndex >= 0 ? int32(i) != abilityIndex : ability.target != BONK_ABILITY_SELF)
            continue;
        if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(ability.spellId))
            reach = std::max(reach, GetAreaReach(spellInfo));
    }

    // Nothing that reaches the hero: no need to warn.
    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    if (!run || reach <= 0.0f)
    {
        if (abilityIndex < 0)
            Explode(owner);
        else
            Cast(m_mob->abilities[abilityIndex], owner, false);
        return;
    }
    // Further than a circle can show: never cast (unwarned damage).
    float const size = GetAreaSize(reach, owner);
    if (size > BONK_AREA_MAX_SIZE)
        return;

    m_areaWinding = true;
    m_areaAbility = abilityIndex;
    m_areaTimer = BONK_AREA_WARN_MS;
    m_areaX = m_creature->GetPositionX();
    m_areaY = m_creature->GetPositionY();
    m_creature->AttackStop();
    m_creature->StopMoving();
    m_creature->GetMotionMaster()->Clear(false);
    m_creature->GetMotionMaster()->MoveIdle();
    m_creature->SetFacingToObject(owner);
    run->AddTelegraph(BONK_TELEGRAPH_CIRCLE, m_areaX, m_areaY, m_creature->GetPositionZ(), 0.0f, size, BONK_AREA_WARN_MS);
}

bool BonkMobAI::UpdateArea(uint32 diff, Player* owner)
{
    if (!m_areaWinding)
        return false;
    m_areaTimer -= int32(diff);
    if (m_areaTimer > 0)
        return true;
    m_areaWinding = false;

    // Stunned, feared or knocked away meanwhile: the circle was for where it
    // stood, so it fizzles (an exploder lights its fuse again).
    float const dx = m_creature->GetPositionX() - m_areaX;
    float const dy = m_creature->GetPositionY() - m_areaY;
    bool const fizzles = dx * dx + dy * dy > BONK_AREA_DRIFT * BONK_AREA_DRIFT ||
        m_creature->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING);
    if (!fizzles)
    {
        if (m_areaAbility < 0)
        {
            Explode(owner);
            return true;
        }
        Cast(m_mob->abilities[m_areaAbility], owner, true);
    }

    // After the chase again, as after a charge.
    AttackStart(owner);
    if (m_creature->GetVictim() == owner && m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        m_creature->GetMotionMaster()->MoveChase(owner, m_mob && m_mob->role == BONK_ROLE_RANGED ? m_mob->range : 0.0f);
    return true;
}

bool BonkMobAI::UpdateCharge(uint32 diff, Player* owner)
{
    // Others only finish a charge an ability started.
    bool const charger = m_archetype == BONK_ARCHETYPE_CHARGER && m_kind == BONK_SPAWN_NORMAL;
    if (!charger && m_chargeState == CHARGE_NONE)
        return false;

    m_chargeTimer -= int32(diff);
    switch (m_chargeState)
    {
        case CHARGE_WINDUP:
            if (m_chargeTimer <= 0)
                Dash(owner);
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
    // Where the player will be as the warning ends: it has to be well within
    // reach, and the player not too far off now.
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

    // Through where the player will be as the warning ends and a little
    // past, as far as the ground allows.
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
    // Nor into a wall short of the player.
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
    run->AddTelegraph(BONK_TELEGRAPH_LINE, m_chargeX, m_chargeY, m_chargeZ, m_chargeAngle, m_chargeLength, GetChargeWindupMs());
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
    // The stretch it covered since the last look, against where the player
    // is now (not where the server last heard): a player on it is run into,
    // hurt and bowled over the way the charge runs. Once a charge.
    float const toX = m_creature->GetPositionX(), toY = m_creature->GetPositionY();
    // A dashing hero is not there to be run into (still hit after, if on the line).
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
                owner->KnockBack(m_chargeAngle, BONK_CHARGE_KNOCKBACK_SPEED, BONK_CHARGE_KNOCKBACK_LIFT);
        }
    }
    m_dashFromX = toX;
    m_dashFromY = toY;
}

void BonkMobAI::EndCharge(Player* owner)
{
    m_chargeState = CHARGE_NONE;
    BonkRun* run = owner->GetBonkPlayer()->GetRun();
    m_chargeTimer = run && run->GetTier().chargeIntervalMs ? int32(run->GetTier().chargeIntervalMs) : BONK_CHARGE_INTERVAL;
    AttackStart(owner);
    // AttackStart only sets it moving when the attack is new; one still
    // counted as attacking would stand where the dash ended.
    if (m_creature->GetVictim() == owner && m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        m_creature->GetMotionMaster()->MoveChase(owner, m_mob && m_mob->role == BONK_ROLE_RANGED ? m_mob->range : 0.0f);
}

void BonkMobAI::DamageDeal(Unit* victim, uint32& damage)
{
    if (!damage || victim == m_creature)
        return;

    // Bolstered and enraged enemies hit harder, shouted-at ones softer (and a
    // hit can be struck back); then Armor takes its share
    // off every hit and spell, and the Shield soaks what it can.
    if (victim->GetObjectGuid() == m_ownerGuid)
        if (BonkRun* run = static_cast<Player*>(victim)->GetBonkPlayer()->GetRun())
        {
            // A dashing hero is not there to be hit.
            if (static_cast<Player*>(victim)->GetBonkPlayer()->IsDashInvulnerable())
            {
                damage = 0;
                return;
            }
            float multiplier = (1.0f + BONK_BOLSTER_BONUS * m_bolsterStacks) * run->GetMobDamageMultiplier(m_creature);
            if (m_enraged)
                multiplier *= BONK_RAGING_DAMAGE;
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
    // A goblin that blinks does so at its next update, not in the middle of
    // the hit.
    if (m_fleeing && m_blinks && damage && damage < m_creature->GetHealth() && m_blinkTimer <= 0)
        m_blinkPending = true;

    // Only what actually comes off the enemy counts towards Leech.
    if (!damage || !attacker || attacker->GetObjectGuid() != m_ownerGuid)
        return;

    if (BonkRun* run = static_cast<Player*>(attacker)->GetBonkPlayer()->GetRun())
    {
        run->ModifyHeroHit(m_creature, damage);
        run->OnWeaponDamage(std::min(damage, m_creature->GetHealth()));
    }
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
    // Straight away from the player, swerving a little; another way where
    // the ground ahead is no good (water, a cliff, out of the arena).
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

void BonkMobAI::JustDied(Unit* /*killer*/)
{
    // The corpse's despawn timer runs on its own clock.
    m_creature->SetTimeFrozen(false);

    // An exploder that already blew up is not a kill.
    Player* owner = GetOwner();
    if (!owner || m_exploded)
        return;

    if (BonkRun* run = owner->GetBonkPlayer()->GetRun())
        run->OnMobKilled(m_creature, m_kind);
}
