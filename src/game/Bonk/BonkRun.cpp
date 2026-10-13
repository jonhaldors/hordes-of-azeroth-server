/*
 * Vanilla Bonk: the run loop.
 */

#include "BonkRun.h"
#include "BonkMobAI.h"
#include "BonkPlayer.h"
#include "BonkQuests.h"
#include "BonkRunStats.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "NullCreatureAI.h"
#include "ObjectMgr.h"
#include "PathFinder.h"
#include "Player.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "World.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <iterator>
#include <cmath>

namespace
{
    uint32 const BONK_ENTER_TIMEOUT_MS     = 30000;
    uint32 const BONK_END_DELAY_MS         = 4000;
    uint32 const BONK_STATE_INTERVAL_MS    = 1000;
    uint32 const BONK_BOUNDARY_INTERVAL_MS = 500;
    uint32 const BONK_FIRST_WAVE_MS        = 1500;
    uint32 const BONK_ENEMY_FACTION        = 14;    // Monster: hostile to everyone
    // Summons only count down when they despawn on the summoner's map, so the
    // count drifts a little every run; the run enforces max_alive itself.
    uint32 const BONK_SUMMON_LIMIT         = 1000;
    // Chests and portals despawn on their own after this long if the run
    // could not remove them (logout during a teleport).
    uint32 const BONK_OBJECT_LIFETIME_SEC  = 3600;

    float const BONK_SPAWN_MAX_HEIGHT_DIFF = 20.0f;
    // An enemy must be able to walk to the player, without a long way round.
    float const BONK_SPAWN_MAX_DETOUR      = 2.5f;     // times the straight distance
    float const BONK_SPAWN_SNAP_DISTANCE   = 3.0f;     // spot may move this far onto the path mesh

    // The arena belongs to the run: the zone's own hostile creatures near the
    // player are removed, and come back long after the run has left.
    uint32 const BONK_NATIVE_SWEEP_MS      = 500;
    float const BONK_NATIVE_SWEEP_RANGE    = 90.0f;
    uint32 const BONK_NATIVE_RESPAWN_SEC   = 30 * MINUTE;

    float const BONK_CHEST_MIN_DISTANCE    = 15.0f;
    float const BONK_CHEST_MAX_DISTANCE    = 45.0f;
    float const BONK_PORTAL_DISTANCE       = 6.0f;
    float const BONK_POWERUP_MIN_DISTANCE  = 20.0f;
    float const BONK_POWERUP_MAX_DISTANCE  = 50.0f;
    float const BONK_POWERUP_PICKUP_RANGE  = 3.0f;
    uint32 const BONK_POWERUP_LIFETIME_SEC = 90;
    uint32 const BONK_POWERUP_CHECK_MS     = 250;
    // Random-target weapons (damage over time) leave an enemy alone this
    // long after a cast, so the cast lands and its ticks are not restarted.
    uint32 const BONK_DOT_RETARGET_MS      = 4000;
    // Leech heals at most this % of max health a second for every 1% of Leech.
    float const BONK_LEECH_CAP_PER_PERCENT = 0.4f;
    float const BONK_SHARD_PICKUP_RANGE    = 3.0f;
    float const BONK_SHARD_SCATTER         = 2.0f;     // lands this far from where the enemy died
    uint32 const BONK_SHARD_CHECK_MS       = 250;
    // The crowd is topped up to the spawn curve every tick, a few at a time.
    uint32 const BONK_SPAWN_TICK_MS        = 1000;
    uint32 const BONK_SPAWN_PER_TICK       = 12;
    float const BONK_BOSS_CROWD            = 0.5f;     // of the crowd, while the stage boss is up
    uint32 const BONK_QUIET_SECONDS        = 30;       // no elites or events this close to a mini-boss or the boss
    uint32 const BONK_ELITE_RETRY_MS       = 5000;     // an elite held back looks again this soon
    float const BONK_BRUISER_SIZE          = 1.3f;     // bruisers are at least this much bigger than the biggest grunt
    float const BONK_BRUISER_MAX_GROWTH    = 2.5f;
    float const BONK_RING_DISTANCE         = 22.0f;    // a ring starts this close
    float const BONK_RUSH_WIDTH            = 0.6f;     // radians either side of a rush's direction

    // Where the player is heading: where they moved, sampled this often and
    // smoothed over about this long. Slower than this is standing still; a
    // step this long between samples is a teleport, not a run.
    uint32 const BONK_HEADING_SAMPLE_MS    = 250;
    float const BONK_HEADING_SMOOTH_SEC    = 1.5f;
    float const BONK_HEADING_MIN_SPEED     = 2.5f;     // yards a second
    float const BONK_HEADING_MAX_STEP      = 20.0f;
    // "Ahead of the player": this far either side of their heading and this
    // close. At most this share of the enemies alive (and never fewer than
    // the minimum) may be there; new ones go to the sides or behind.
    float const BONK_FRONT_ARC             = M_PI_F / 3.0f;
    float const BONK_FRONT_RANGE           = 95.0f;
    float const BONK_FRONT_SHARE           = 0.3f;
    uint32 const BONK_FRONT_MIN            = 5;
    // Enemies ahead (on screen) appear this far off, just inside the 100
    // yards the client is shown creatures from, and walk in from there; to
    // the sides and behind, at the map's spawn distance. Where the ground
    // ahead has no spot that far (indoors), the first tries give way to the
    // map's distance.
    float const BONK_FAR_MIN_DISTANCE      = 75.0f;
    float const BONK_FAR_MAX_DISTANCE      = 90.0f;
    uint32 const BONK_FAR_TRIES            = 6;
    // Enemies this far from the player for this long, and not ahead of them,
    // are left behind. They come back ahead of the player in packs of 3-5,
    // as far off as new enemies ahead. A pack lands this far from packs that
    // landed lately (centre to centre: about 15 yards between them) and from
    // any enemy. Fewer than a pack go anyway after a while; with no room
    // ahead for longer, they come back at the player's side instead.
    uint32 const BONK_STRAGGLER_TICK_MS    = 500;
    float const BONK_STRAY_DISTANCE        = 70.0f;
    uint32 const BONK_STRAY_MS             = 5000;
    uint32 const BONK_PACK_MIN             = 3;
    uint32 const BONK_PACK_MAX             = 5;
    float const BONK_PACK_RADIUS           = 3.0f;
    float const BONK_PACK_SPACING          = 21.0f;
    float const BONK_PACK_CLEARANCE        = 8.0f;
    uint32 const BONK_PACK_SPOT_MS         = 4000;
    uint32 const BONK_PACK_WAIT_MS         = 3000;
    uint32 const BONK_STRAGGLER_SIDE_MS    = 5000;
    float const BONK_SIDE_MIN_ANGLE        = M_PI_F / 3.0f;            // off the heading
    float const BONK_SIDE_MAX_ANGLE        = 2.0f * M_PI_F / 3.0f;

    // How far apart two directions are, 0 to pi.
    float AngleBetween(float a, float b)
    {
        return std::fabs(std::remainder(a - b, 2.0f * M_PI_F));
    }

    // Experience orbs.
    size_t const BONK_MAX_ORBS             = 300;      // past this, one red orb gathers the rest
    uint32 const BONK_ORB_CHECK_MS         = 100;
    float const BONK_ORB_PICKUP_RANGE      = 1.5f;
    float const BONK_ORB_MAGNET_RANGE      = 6.0f;     // before the Magnet passive
    // A pulled orb flies straight at the player this fast (yards per second),
    // turning toward the player again once they have moved this far.
    float const BONK_ORB_FLY_SPEED         = 28.0f;
    float const BONK_ORB_REAIM_DISTANCE    = 1.0f;
    uint32 const BONK_ORB_FACTION          = 35;       // friendly: enemies and area spells leave it be
    // Orbs the run lost track of (cleanup off the map) still go away eventually.
    uint32 const BONK_ORB_LIFETIME_MS      = 30 * MINUTE * IN_MILLISECONDS;

    size_t const BONK_MAX_WEAPONS  = 5;
    size_t const BONK_MAX_PASSIVES = 6;
    size_t const BONK_OFFER_SIZE   = 3;
    // Every choice has at least one spell rank of this rarity or better.
    BonkRarity const BONK_LEVEL_MIN_RARITY = BONK_RARITY_UNCOMMON;
    BonkRarity const BONK_CHEST_MIN_RARITY = BONK_RARITY_RARE;
    // A skipped choice pays this share of the next chest's price in Valor.
    float const BONK_SKIP_COINS = 0.25f;
    // An elite chest offers one more card per affix, up to this many.
    size_t const BONK_MAX_OFFER_SIZE = 6;

    // Spell behaviors.
    float const BONK_SPELL_SHIELD_CAP      = 0.5f;     // spell Shields stack up to this share of max health
    float const BONK_ECHO_AREA             = 1.5f;     // an echo's area, times the first pulse's
    float const BONK_FREEZE_ARC            = 2.0f * M_PI_F / 3.0f;     // "in front of the hero"
    float const BONK_FROZEN_CRIT_BONUS     = 0.5f;     // more crit damage on rooted enemies with a freezing spell
    uint32 const BONK_SPELL_WEAPON_ORB     = 22576;    // Glowy (Blue): an orb's look
    float const BONK_WEAPON_ORB_SCALE      = 1.2f;
    float const BONK_WEAPON_ORB_SPEED      = 0.35f;    // times a running hero's speed
    float const BONK_PLAYER_RUN_SPEED      = 7.0f;     // yards a second
    float const BONK_CONE_ARC              = M_PI_F / 2.0f;    // a cone's width before Area; never wider than half a circle
    float const BONK_KNOCKBACK_DISTANCE    = 6.0f;
    float const BONK_KNOCKBACK_SPEED       = 18.0f;
    float const BONK_KNOCKBACK_HEIGHT      = 2.0f;
    uint32 const BONK_BURST_INTERVAL_MS    = 150;      // between a burst's shots
    float const BONK_MAX_MARK              = 100.0f;   // % more damage a mark can add
    float const BONK_MAX_DEMORALIZE        = 60.0f;    // % less damage a shout can take off
    float const BONK_MARK_JUMP_RANGE       = 15.0f;    // a mark moves on to an enemy this close to the one that died
    // Traps: how near an enemy sets one off, how soon after it lands, how
    // long it lies, how many one spell can have out, and how near an enemy
    // must be before the hero drops any.
    float const BONK_TRAP_TRIGGER_RADIUS   = 2.5f;
    int32 const BONK_TRAP_ARM_MS           = 500;
    int32 const BONK_TRAP_LIFETIME_MS      = 30000;
    size_t const BONK_MAX_TRAPS            = 8;
    float const BONK_TRAP_AWARE_RANGE      = 25.0f;
    float const BONK_TRAP_SPACING          = 3.0f;     // between traps dropped together
    uint32 const BONK_TRAP_CHECK_MS        = 100;
    // Beasts: their creature, how near the hero they look for prey, how far
    // they may stray before they are brought back, and how often they look.
    uint32 const BONK_PET_ENTRY            = 900112;
    float const BONK_PET_HUNT_RANGE        = 25.0f;
    float const BONK_PET_LEASH_RANGE       = 40.0f;
    float const BONK_PET_SPEED             = 1.4f;     // times a creature's run speed: keeps up with the hero
    uint32 const BONK_PET_CHECK_MS         = 250;
    uint32 const BONK_SPELL_BLEED_HEAL     = 23881;    // Bloodthirst: names Bloodthirst's heal in combat text
    // Volcanic: the burst's look (a dummy, on an invisible marker), and the
    // enemy's hit on a hero who stayed in it.
    uint32 const BONK_SPELL_ERUPTION_LOOK  = 19823;    // Fire Nova Visual
    uint32 const BONK_SPELL_ERUPTION_HIT   = 13339;    // Fire Blast
    uint32 const BONK_ERUPTION_LOOK_MS     = 3000;

    // Elites drop a chest by chance (%), better with every affix and every
    // point of Luck; after this many elites without one, the next drops one.
    float const BONK_ELITE_CHEST_CHANCE    = 15.0f;
    float const BONK_ELITE_CHEST_PER_AFFIX = 10.0f;
    float const BONK_ELITE_CHEST_PER_LUCK  = 0.1f;
    uint32 const BONK_ELITE_CHEST_PITY     = 5;

    // Affixes the run carries out.
    uint32 const BONK_SPELL_NECROTIC       = 18951;     // Spirit Particles (green): the hero's look while Necrotic; Glowy (Green)'s tint would outlive the aura
    float const BONK_NECROTIC_PER_STACK    = 5.0f;      // % less healing
    uint32 const BONK_NECROTIC_MAX_STACKS  = 15;
    uint32 const BONK_NECROTIC_CHECK_MS    = 250;       // stacks clear once no living Necrotic enemy is in range
    float const BONK_BOLSTER_RANGE         = 12.0f;
    uint32 const BONK_SPELL_SANGUINE       = 31951;     // Spirit Particles (red, super big): the pool's look
    float const BONK_SANGUINE_RADIUS       = 5.0f;
    float const BONK_SANGUINE_HEAL         = 0.05f;     // of max health a second
    uint32 const BONK_SANGUINE_DURATION_MS = 12000;

    // Passive upgrades that need a real aura on the player.
    uint32 const BONK_SPELL_MOVE_SPEED = 17498;     // Speed: SPELL_AURA_MOD_INCREASE_SPEED
    uint32 const BONK_SPELL_MAX_HEALTH = 19255;     // Survivalist: SPELL_AURA_MOD_INCREASE_HEALTH_PERCENT
    uint32 const BONK_SPELL_DODGE      = 13669;     // Increased Dodge: SPELL_AURA_MOD_DODGE_PERCENT
    uint32 const BONK_SPELL_HEAL_TEXT  = 439;       // Healing Potion: names the turkey leg's heal in combat text

    // Turkey legs: drop chances (%) and heal (share of max health); taken
    // from as far as orbs are pulled.
    float const BONK_HEAL_CHANCE_NORMAL  = 1.0f;
    float const BONK_HEAL_CHANCE_ELITE   = 35.0f;
    float const BONK_HEAL_AMOUNT         = 0.20f;
    uint32 const BONK_HEAL_CHECK_MS      = 250;
    size_t const BONK_MAX_HEAL_DROPS     = 20;

    // Highlights on markers under pickups, so they stand out in a crowd.
    uint32 const BONK_SPELL_HEAL_SPARKLE = 6758;    // Party Fever: an eating sparkle on turkey legs
    uint32 const BONK_SPELL_SHARD_BEAM   = 25824;   // Spotlight: a moonbeam on Talent Shards
    float const BONK_HEAL_SPARKLE_SCALE  = 1.0f;
    float const BONK_SHARD_BEAM_SCALE    = 0.5f;
    // A taken orb goes invisible at once and is gone a moment later; removed
    // outright, the client would fade it out over a second or two.
    uint32 const BONK_DISPLAY_INVISIBLE  = 11686;   // Onyxia Trigger's model
    uint32 const BONK_ORB_FADE_MS        = 1000;

    // Each level gained heals this share of max health.
    float const BONK_LEVEL_UP_HEAL       = 0.10f;
    // A few seconds safe from melee after a revive.
    uint32 const BONK_SPELL_REVIVE_SHIELD = 1022;   // Blessing of Protection, rank 1
    float const BONK_REVIVE_CLEAR_RADIUS  = 15.0f;

    // The Shield's look while it holds: Barrier of Light, a dummy aura with
    // Power Word: Shield's bubble. The run does the absorbing itself.
    uint32 const BONK_SPELL_SHIELD_VISUAL = 16051;
    // The addon hears about Shield changes at most this often.
    uint32 const BONK_SHIELD_SEND_MS      = 250;

    // Class Mastery: what counts as an enemy at full and at low health, the
    // most Soul Siphon heals per second (% of max health), and how soon a
    // Clearcasting spell fires again.
    float const BONK_MASTERY_OPENER_HEALTH  = 80.0f;
    float const BONK_MASTERY_EXECUTE_HEALTH = 30.0f;
    float const BONK_MASTERY_SIPHON_CAP     = 3.0f;
    int32 const BONK_CLEARCAST_DELAY_MS     = 300;

    // Times a normal enemy's health at the same moment. Tuned so that, with a
    // typical build, an elite takes a few seconds, a mini-boss about ten and
    // a stage boss 30-50 seconds of the player's single-target damage.
    float GetKindHealth(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 4.0f;
            case BONK_SPAWN_MINIBOSS: return 10.0f;
            case BONK_SPAWN_BOSS:     return 30.0f;
            default:                  return 1.0f;
        }
    }

    // Enemy health grows with the player's level as well as with time, the
    // way Megabonk scales with XP gained and time: every level is one more
    // upgrade, and an average upgrade adds about 5% to a run's damage (a rank
    // is +15-20% on one of five or six spells; new spell slots run out early).
    float const BONK_HEALTH_PER_LEVEL = 0.05f;

    float GetKindDamage(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 2.0f;
            case BONK_SPAWN_MINIBOSS: return 3.0f;
            case BONK_SPAWN_BOSS:     return 4.0f;
            default:                  return 1.0f;
        }
    }

    // Gold, Valor and experience, in normal kills.
    uint32 GetKindReward(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 5;
            case BONK_SPAWN_MINIBOSS: return 20;
            case BONK_SPAWN_BOSS:     return 50;
            default:                  return 1;
        }
    }

    uint32 GetKindLevelBonus(BonkSpawnKind kind)
    {
        switch (kind)
        {
            case BONK_SPAWN_ELITE:    return 1;
            case BONK_SPAWN_MINIBOSS: return 2;
            case BONK_SPAWN_BOSS:     return 2;
            default:                  return 0;
        }
    }

    // Enemies may hurt the player but never slow, root, stun, fear or
    // confuse them: getting out of the swarm is the game.
    void SetMovementImmunity(Player* player, bool apply)
    {
        for (AuraType aura : { SPELL_AURA_MOD_DECREASE_SPEED, SPELL_AURA_MOD_ROOT, SPELL_AURA_MOD_STUN, SPELL_AURA_MOD_FEAR, SPELL_AURA_MOD_CONFUSE })
        {
            player->ApplySpellImmune(0, IMMUNITY_STATE, aura, apply);
            if (apply)
                player->RemoveSpellsCausingAura(aura);
        }
    }

    void InstallOrbAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new NullCreatureAI(creature));
    }

    void VanishOrb(Creature* orb)
    {
        orb->RemoveAllAuras();
        orb->SetDisplayId(BONK_DISPLAY_INVISIBLE);
        orb->DespawnOrUnsummon(BONK_ORB_FADE_MS);
    }

    // A hunter's beast: the run picks its prey (BonkRun::UpdatePetTargets);
    // it only bites. Run enemies never fight back, they fight the hero.
    class BonkPetAI : public CreatureAI
    {
        public:
            explicit BonkPetAI(Creature* creature) : CreatureAI(creature) {}

            void MoveInLineOfSight(Unit*) override {}
            void AttackedBy(Unit*) override {}
            void EnterEvadeMode() override {}
            void UpdateAI(uint32 const /*diff*/) override
            {
                Unit* victim = m_creature->GetVictim();
                if (victim && victim->IsAlive())
                    DoMeleeAttackIfReady();
            }
    };

    void InstallPetAI(Creature* creature)
    {
        creature->GetMotionMaster()->Initialize();
        creature->SetAI(new BonkPetAI(creature));
    }

    // Its biggest effect radius, 0 for none.
    float GetSpellRadiusOf(SpellEntry const* spellInfo)
    {
        float radius = 0.0f;
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->EffectRadiusIndex[i])
                radius = std::max(radius, Spells::GetSpellRadius(sSpellRadiusStore.LookupEntry(spellInfo->EffectRadiusIndex[i])));
        return radius;
    }

    // Bosses rank above mini-bosses, above elites, above normal enemies.
    uint32 GetKindRank(Creature const* mob)
    {
        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        switch (ai ? ai->GetKind() : BONK_SPAWN_NORMAL)
        {
            case BONK_SPAWN_BOSS:     return 3;
            case BONK_SPAWN_MINIBOSS: return 2;
            case BONK_SPAWN_ELITE:    return 1;
            default:                  return 0;
        }
    }

    // Weapon casts made while this is alive take no ammo (Spell::TakeAmmo).
    // A kill during a volley can spread a spell, nesting another one.
    struct FiringWeapon
    {
        explicit FiringWeapon(BonkPlayer* bonk) : m_bonk(bonk), m_wasFiring(bonk->IsFiringWeapon()) { m_bonk->SetFiringWeapon(true); }
        ~FiringWeapon() { m_bonk->SetFiringWeapon(m_wasFiring); }
        BonkPlayer* m_bonk;
        bool m_wasFiring;
    };

    std::string Percent(float value)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%g%%", value);
        return buffer;
    }

    // The effect that carries a spell's damage: direct damage first, damage
    // over time otherwise, -1 for none.
    int32 FindDamageEffectOf(SpellEntry const* spellInfo)
    {
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (spellInfo->Effect[i] == SPELL_EFFECT_SCHOOL_DAMAGE)
                return int32(i);
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if ((spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA || spellInfo->Effect[i] == SPELL_EFFECT_PERSISTENT_AREA_AURA) &&
                spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE)
                return int32(i);
        return -1;
    }

    // " Legendary from rank 8: <evolved>." for a spell that can evolve.
    std::string DescribeEvolution(BonkWeapon const& weapon)
    {
        BonkWeapon const* evolved = sBonkMgr.GetEvolution(weapon.id);
        if (!evolved)
            return "";
        return " Legendary from rank " + std::to_string(BONK_EVOLVE_RANK) + ": " + evolved->name + ".";
    }

    // A spell's extra jumps, read as what they are for it.
    std::string DescribeJump(BonkWeapon const& weapon)
    {
        if (weapon.behavior == BONK_BEHAVIOR_CLEAVE)
            return "cleaves 1 more enemy";
        if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
            return "spreads to 1 more enemy";
        return "hits 1 more enemy";
    }

    // CastCustomSpell for a spot on the ground.
    void CastAtGround(Player* player, SpellEntry const* spellInfo, optional<int32> const* basePoints, float x, float y, float z)
    {
        Spell* spell = new Spell(player, spellInfo, true);
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            if (basePoints[i])
                spell->m_currentBasePoints[i] = *basePoints[i];

        SpellCastTargets targets;
        targets.setDestination(x, y, z);
        spell->prepare(std::move(targets));
    }

    // Up to `count` spots for a ground effect of `radius`, each centred on the
    // enemy with the most others around it that no earlier spot covers.
    std::vector<Creature*> PickGroundSpots(std::vector<Creature*> const& mobs, float radius, uint32 count)
    {
        std::vector<Creature*> spots;
        std::vector<bool> covered(mobs.size(), false);
        while (spots.size() < count)
        {
            size_t best = mobs.size();
            uint32 bestCount = 0;
            for (size_t i = 0; i < mobs.size(); ++i)
            {
                if (covered[i])
                    continue;
                uint32 around = 0;
                for (size_t j = 0; j < mobs.size(); ++j)
                    if (!covered[j] && mobs[i]->GetDistance2d(mobs[j]) <= radius)
                        ++around;
                if (around > bestCount)
                {
                    best = i;
                    bestCount = around;
                }
            }
            if (best == mobs.size())
                break;

            for (size_t j = 0; j < mobs.size(); ++j)
                if (mobs[best]->GetDistance2d(mobs[j]) <= radius)
                    covered[j] = true;
            spots.push_back(mobs[best]);
        }
        return spots;
    }

    std::string FormatTime(uint32 seconds)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%u:%02u", seconds / 60, seconds % 60);
        return buffer;
    }

    std::string FormatGold(uint32 copper)
    {
        char buffer[48];
        snprintf(buffer, sizeof(buffer), "%ug %us %uc", copper / 10000, (copper / 100) % 100, copper % 100);
        return buffer;
    }
}

BonkRun::BonkRun(Player* player, BonkTier const& tier) : m_player(player), m_tier(tier),
    m_bonuses(player->GetBonkPlayer()->GetRunBonuses()), m_mastery(player->GetBonkPlayer()->GetMasteryBonuses())
{
    // Mastery bonuses are by weapon; damage comes in by spell.
    for (auto const& itr : sBonkMgr.GetWeapons())
        m_spellRoots.emplace(itr.second.spellId, BonkMastery::GetRootWeapon(itr.first));

    m_rerolls = m_bonuses.rerolls;
    m_banishes = m_bonuses.banishes;
    m_skips = m_bonuses.skips;
    m_revives = m_bonuses.revives;
    m_coins = m_bonuses.startCoins;
}

bool BonkRun::Begin(std::string& error)
{
    BonkStats::Reset(m_player->GetObjectGuid());

    BonkMap const* first = ChooseNextMap();
    if (!first)
    {
        error = "No maps are set up for " + m_tier.name + ".";
        return false;
    }
    m_map = *first;

    // GM mode makes the player untargetable, and GM accounts log in with god
    // mode (GM.CheatGod), which stops all damage at 1 health; either would
    // make the run unlosable. Only `.bonk god` keeps god mode on, for testing.
    if (m_player->IsGameMaster())
        m_player->SetGameMaster(false);
    m_player->SetCheatGod(m_player->GetBonkPlayer()->IsTestGod());
    SetMovementImmunity(m_player, true);

    m_player->GetBonkPlayer()->ResetCharacter();
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);

    return TeleportToStage(m_map, error);
}

BonkMap const* BonkRun::ChooseNextMap()
{
    // Each lap plays every map of the pool once in random order, and the
    // same map never comes twice in a row unless it is the only one.
    std::vector<BonkMap const*> pool;
    for (uint32 id : m_tier.mapIds)
        if (BonkMap const* map = sBonkMgr.GetMap(id))
            if (!map->creatures.empty())
                pool.push_back(map);

    if (pool.empty())
        return nullptr;

    std::vector<BonkMap const*> candidates;
    for (BonkMap const* map : pool)
        if (!m_playedMaps.count(map->id))
            candidates.push_back(map);

    if (candidates.empty())
    {
        m_playedMaps.clear();
        for (BonkMap const* map : pool)
            if (pool.size() == 1 || map->id != m_map.id)
                candidates.push_back(map);
    }

    BonkMap const* map = candidates[urand(0, uint32(candidates.size() - 1))];
    m_playedMaps.insert(map->id);
    return map;
}

bool BonkRun::TeleportToStage(BonkMap const& map, std::string& error)
{
    if (!m_player->TeleportTo(map.mapId, map.x, map.y, map.z, map.o))
    {
        error = "Could not enter " + map.name + ".";
        return false;
    }

    m_state = STATE_ENTERING;
    m_enterTimer = 0;
    return true;
}

void BonkRun::Update(uint32 diff)
{
    switch (m_state)
    {
        case STATE_ENTERING:
        {
            if (m_player->GetMapId() == m_map.mapId && !m_player->IsBeingTeleported())
            {
                StartStage();
                return;
            }

            m_enterTimer += diff;
            if (m_enterTimer >= BONK_ENTER_TIMEOUT_MS)
            {
                m_player->GetBonkPlayer()->Notify("Could not enter " + m_map.name + ".");
                Exit();
            }
            break;
        }
        case STATE_RUNNING:
            UpdateRunning(diff);
            break;
        case STATE_PORTAL:
        {
            if (!m_player->IsAlive())
            {
                if (m_revives)
                    Revive();
                else
                    Finish(false);
                return;
            }

            UpdateOffers();
            UpdateBoundary(diff);
            UpdateNatives(diff);
            UpdateShield(diff);
            UpdateNecrotic(diff);
            UpdatePools(diff);
            UpdatePowerups(diff, false);
            UpdateOrbs(diff);
            UpdateShards(diff);
            UpdateHeals(diff);

            m_stateTimer += diff;
            if (m_stateTimer >= BONK_STATE_INTERVAL_MS)
            {
                m_stateTimer = 0;
                SendState();

                // Never leave the player without a way out.
                if (!m_player->GetMap()->GetGameObject(m_portalGuid) && !m_player->IsBeingTeleported())
                {
                    float const angle = m_player->GetOrientation();
                    PlacePortal(m_player->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
                        m_player->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), m_player->GetPositionZ());
                }
            }
            break;
        }
        case STATE_ENDING:
            m_endTimer -= int32(diff);
            if (m_endTimer <= 0)
                Exit();
            break;
        case STATE_FINISHED:
            break;
    }
}

void BonkRun::StartStage()
{
    m_state = STATE_RUNNING;
    m_elapsedMs = 0;
    m_nextEvent = 0;
    m_spawnTimer = BONK_FIRST_WAVE_MS;
    m_eliteTimer = int32(m_tier.firstEliteSecond * IN_MILLISECONDS);
    m_heldEvents.clear();
    m_gruntRadius = 0.0f;
    m_bossSpawned = false;
    m_bossGuid.Clear();
    m_powerupSpawnTimer = m_map.powerupIntervalSeconds * IN_MILLISECONDS / 2;
    m_nativeTimer = BONK_NATIVE_SWEEP_MS;     // clear the arrival spot right away
    m_lastLevel = m_player->GetLevel();
    m_safeX = m_player->GetPositionX();
    m_safeY = m_player->GetPositionY();
    m_safeZ = m_player->GetPositionZ();
    m_lastX = m_safeX;
    m_lastY = m_safeY;
    m_moveX = m_moveY = 0.0f;
    m_headingTimer = 0;
    m_farSince.clear();
    m_packSpots.clear();

    // The summon limit is stored per map, so set it once we are in the arena.
    m_player->SetCreatureSummonLimit(BONK_SUMMON_LIMIT);

    if (m_weapons.empty())
    {
        uint32 const classMask = m_player->GetClassMask();
        for (auto const& itr : sBonkMgr.GetWeapons())
            if (itr.second.starterClassMask & classMask)
                AddWeapon(itr.second);

        // Every class needs something that fires on its own.
        if (m_weapons.empty())
            for (auto const& itr : sBonkMgr.GetWeapons())
                if (!itr.second.evolvesFrom && (!itr.second.classMask || (itr.second.classMask & classMask)))
                {
                    AddWeapon(itr.second);
                    break;
                }

        // Class Mastery: spells every run starts with, once a quest has
        // unlocked them.
        uint32 const account = m_player->GetSession()->GetAccountId();
        for (uint32 weaponId : m_mastery.startSpells)
        {
            BonkWeapon const* weapon = sBonkMgr.GetWeapon(weaponId);
            if (!weapon || m_weapons.size() >= BONK_MAX_WEAPONS || sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, weaponId))
                continue;
            if (std::none_of(m_weapons.begin(), m_weapons.end(), [weaponId](OwnedWeapon const& owned) { return owned.weapon.id == weaponId; }))
                AddWeapon(*weapon);
        }

        // Permanent upgrades may already need their auras.
        ApplyPassives();
    }
    else
    {
        // A fresh stage starts at full health.
        m_player->SetHealth(m_player->GetMaxHealth());
        m_player->SetPower(POWER_MANA, m_player->GetMaxPower(POWER_MANA));
    }

    // Worn gear adds its stats and set bonuses; checked again every second.
    UpdateGear();

    // Every stage starts behind a full Shield, clear of the last stage's
    // Necrotic stacks and pools.
    m_shield = GetMaxShield();
    m_shieldIdleMs = 0;
    SetNecroticStacks(0);
    DespawnPools();
    DespawnWeaponOrbs();
    m_pendingCasts.clear();
    m_telegraphs.Clear(m_player->FindMap());
    DespawnTraps();
    DespawnPets();
    m_marks.clear();
    m_buffs.clear();
    m_retaliations.clear();
    m_retaliateUntil = 0;
    m_dodged = false;
    m_bleedHeal = 0.0f;
    m_player->GetBonkPlayer()->SendAddon("mobclear");

    m_player->GetBonkPlayer()->RecordStage(m_tier.id, m_stage);

    SendFullState();
    m_player->GetBonkPlayer()->Notify("Stage " + std::to_string(m_stage) + ": " + m_map.name + " - the boss comes in " + FormatTime(m_map.durationSeconds) + "!");
}

void BonkRun::SendFullState() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("start~" + m_map.name + "~" + m_tier.name + "~" + std::to_string(m_map.durationSeconds) + "~" + std::to_string(m_stage) + "~" +
        std::to_string(m_map.minibossSecond));
    SendLoadout();
    SendState();
    if (!m_offer.empty())
        SendOffer();
}

void BonkRun::UpdateRunning(uint32 diff)
{
    if (!m_player->IsAlive())
    {
        if (m_revives)
            Revive();
        else
            Finish(false);
        return;
    }

    UpdateOffers();
    // The zone's own creatures do not pause with the run.
    UpdateNatives(diff);
    if (m_paused)
        return;

    m_elapsedMs += diff;
    m_totalMs += diff;

    UpdateHeading(diff);
    UpdateBoundary(diff);
    UpdateRecovery(diff);
    UpdateShield(diff);
    UpdateNecrotic(diff);
    UpdatePools(diff);
    UpdatePowerups(diff, true);
    UpdateOrbs(diff);
    UpdateShards(diff);
    UpdateHeals(diff);
    UpdateEvents();
    if (m_state != STATE_RUNNING)
        return;

    UpdateSpawns(diff);
    if (m_state != STATE_RUNNING)
        return;
    UpdateStragglers(diff);

    UpdateWeapons(diff);
    if (m_state != STATE_RUNNING)
        return;
    UpdatePendingCasts(diff);
    UpdateWeaponOrbs(diff);
    UpdateTraps(diff);
    UpdatePetTargets(diff);
    m_telegraphs.Update(m_player->GetMap(), diff);
    if (m_state != STATE_RUNNING)
        return;

    m_stateTimer += diff;
    if (m_stateTimer >= BONK_STATE_INTERVAL_MS)
    {
        m_stateTimer = 0;
        // Gear can be swapped from the bags mid-run.
        UpdateGear();
        SendState();
    }
}

void BonkRun::UpdateOffers()
{
    uint32 const level = m_player->GetLevel();
    if (level > m_lastLevel)
    {
        m_pendingLevels += level - m_lastLevel;
        m_lastLevel = level;
    }

    if (!m_offer.empty())
        return;

    // Chests first: the player just walked up to one.
    if (!m_pendingChestChoices.empty())
        OfferUpgrades(OFFER_CHEST);
    else if (m_pendingLevels)
        OfferUpgrades(OFFER_LEVEL);
}

void BonkRun::UpdateBoundary(uint32 diff)
{
    m_boundaryTimer += diff;
    if (m_boundaryTimer < BONK_BOUNDARY_INTERVAL_MS)
        return;
    m_boundaryTimer = 0;

    if (!m_map.zoneId || m_player->IsBeingTeleported())
        return;

    if (m_player->GetZoneId() == m_map.zoneId)
    {
        m_safeX = m_player->GetPositionX();
        m_safeY = m_player->GetPositionY();
        m_safeZ = m_player->GetPositionZ();
        return;
    }

    m_player->NearTeleportTo(m_safeX, m_safeY, m_safeZ, m_player->GetOrientation());
    m_player->GetBonkPlayer()->Notify("You can't leave " + m_map.name + ".");
}

void BonkRun::UpdateNatives(uint32 diff)
{
    m_nativeTimer += diff;
    if (m_nativeTimer < BONK_NATIVE_SWEEP_MS)
        return;
    m_nativeTimer = 0;

    if (m_player->IsBeingTeleported() || !m_player->IsInWorld())
        return;

    std::list<Creature*> creatures;
    MaNGOS::AnyUnitInObjectRangeCheck check(m_player, BONK_NATIVE_SWEEP_RANGE);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(creatures, check);
    Cell::VisitGridObjects(m_player, searcher, BONK_NATIVE_SWEEP_RANGE);

    ObjectGuid const playerGuid = m_player->GetObjectGuid();
    for (Creature* creature : creatures)
    {
        // Run enemies and orbs are the player's summons; pets and guardians
        // belong to someone. Friendly townsfolk can stay.
        if (creature->IsTemporarySummon() && static_cast<TemporarySummon*>(creature)->GetSummonerGuid() == playerGuid)
            continue;
        if (creature->IsPet() || creature->GetCharmerOrOwnerGuid() || creature->IsFriendlyTo(m_player))
            continue;
        if (creature->HasCreatureState(CSTATE_DESPAWNING))
            continue;

        creature->DespawnOrUnsummon(0, BONK_NATIVE_RESPAWN_SEC);
    }
}

void BonkRun::UpdateRecovery(uint32 diff)
{
    m_recoveryTimer += diff;
    if (m_recoveryTimer < IN_MILLISECONDS)
        return;
    m_recoveryTimer -= IN_MILLISECONDS;

    // Leech gathered over the last second, up to its cap; the rest is lost.
    uint32 heal = uint32(m_leechCarry);
    m_leechCarry = 0.0f;

    float const percent = GetPassiveTotal(BONK_PASSIVE_RECOVERY);
    if (percent > 0.0f)
        heal += std::max(1u, uint32(m_player->GetMaxHealth() * percent / 100.0f));

    if (heal && m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth())
        m_player->ModifyHealth(int32(ReduceHealing(heal)));

    // Bloodthirst: what the bleeds took this second, as one heal.
    uint32 const bleedHeal = uint32(m_bleedHeal);
    m_bleedHeal = 0.0f;
    if (bleedHeal && m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth())
    {
        uint32 const amount = ReduceHealing(bleedHeal);
        if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_BLEED_HEAL))
            m_player->DealHeal(m_player, amount, spellInfo);
        else
            m_player->ModifyHealth(int32(amount));
    }
}

void BonkRun::UpdateEvents()
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;
    bool const quiet = IsQuietTime();

    // Events held through a quiet time come once it is over; none once the
    // stage boss is up.
    if (!quiet && !m_heldEvents.empty())
    {
        std::vector<BonkMapEvent> const held = std::move(m_heldEvents);
        m_heldEvents.clear();
        if (!m_bossSpawned)
            for (BonkMapEvent const& event : held)
            {
                RunEvent(event);
                if (m_state != STATE_RUNNING)
                    return;
            }
    }

    while (m_nextEvent < m_map.events.size() && m_map.events[m_nextEvent].second <= elapsedSeconds)
    {
        BonkMapEvent const& event = m_map.events[m_nextEvent++];
        if (quiet && IsQuietEvent(event.type))
        {
            m_heldEvents.push_back(event);
            continue;
        }
        RunEvent(event);
        if (m_state != STATE_RUNNING)
            return;
    }
}

bool BonkRun::IsQuietEvent(BonkEventType type)
{
    return type == BONK_EVENT_ELITES || type == BONK_EVENT_SWARM || type == BONK_EVENT_RING || type == BONK_EVENT_RUSH;
}

bool BonkRun::IsQuietTime() const
{
    uint32 const second = m_elapsedMs / IN_MILLISECONDS;
    auto closeTo = [second](uint32 mark) { return second + BONK_QUIET_SECONDS >= mark && second <= mark + BONK_QUIET_SECONDS; };
    if (m_map.minibossSecond && closeTo(m_map.minibossSecond))
        return true;
    // From shortly before the boss until the stage ends.
    return second + BONK_QUIET_SECONDS >= m_map.durationSeconds;
}

void BonkRun::RunEvent(BonkMapEvent const& event)
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();

    switch (event.type)
    {
        case BONK_EVENT_MINIBOSS:
        {
            std::string name;
            for (uint32 i = 0; i < event.count; ++i)
            {
                BonkMapCreature const* creature = PickCreature(BONK_SPAWN_MINIBOSS);
                if (!creature)
                    creature = PickCreature(BONK_SPAWN_ELITE);
                if (creature)
                    if (Creature* mob = SpawnMob(*creature))
                        name = mob->GetName();
            }
            if (!name.empty())
                bonk->Notify(name + " has arrived!");
            break;
        }
        case BONK_EVENT_ELITES:
        {
            bool spawned = false;
            for (uint32 i = 0; i < event.count; ++i)
                if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_ELITE))
                    spawned |= SpawnMob(*creature) != nullptr;
            if (spawned)
                bonk->Notify("Elites are closing in!");
            break;
        }
        case BONK_EVENT_SWARM:
        {
            // On top of the crowd, but never past the tier's cap.
            PruneMobs();
            uint32 const alive = m_mobs.size();
            uint32 const count = std::min(event.count, m_tier.maxAlive > alive ? m_tier.maxAlive - alive : 0u);
            for (uint32 i = 0; i < count; ++i)
                if (BonkMapCreature const* creature = PickTrash())
                    SpawnMob(*creature);
            if (count)
                bonk->Notify("A swarm is coming!");
            break;
        }
        case BONK_EVENT_RING:
        case BONK_EVENT_RUSH:
        {
            if (SpawnFormation(event))
                bonk->Notify(event.type == BONK_EVENT_RING ? "You are surrounded!" : "Something is charging in!");
            break;
        }
        case BONK_EVENT_CHESTS:
        {
            uint32 placed = 0;
            for (uint32 i = 0; i < event.count; ++i)
                placed += SpawnChestNearby() ? 1 : 0;
            if (placed)
                bonk->Notify(placed == 1 ? "A chest appeared nearby." : "Chests appeared nearby.");
            break;
        }
        default:
            break;
    }
}

uint32 BonkRun::SpawnFormation(BonkMapEvent const& event)
{
    PruneMobs();
    uint32 const alive = m_mobs.size();
    uint32 const count = std::min(event.count, m_tier.maxAlive > alive ? m_tier.maxAlive - alive : 0u);

    // A ring surrounds the player evenly and close; a rush comes as a wedge
    // from one side at the usual distance.
    float const side = frand(0.0f, 2.0f * M_PI_F);
    uint32 spawned = 0;
    for (uint32 i = 0; i < count; ++i)
    {
        BonkMapCreature const* creature = PickTrash();
        if (!creature)
            break;

        float angle, distance;
        if (event.type == BONK_EVENT_RING)
        {
            angle = side + 2.0f * M_PI_F * i / count;
            distance = BONK_RING_DISTANCE;
        }
        else
        {
            angle = side + frand(-BONK_RUSH_WIDTH, BONK_RUSH_WIDTH);
            distance = frand(m_map.spawnMinDistance, m_map.spawnMaxDistance);
        }

        float x, y, z;
        if (FindSpawnPositionAt(angle, distance, x, y, z) && SpawnMobAt(*creature, x, y, z))
            ++spawned;
    }
    return spawned;
}

void BonkRun::PruneMobs()
{
    Map* map = m_player->GetMap();
    m_mobs.erase(std::remove_if(m_mobs.begin(), m_mobs.end(), [map](ObjectGuid const& guid)
    {
        Creature* mob = map->GetCreature(guid);
        return !mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING);
    }), m_mobs.end());
}

void BonkRun::UpdateSpawns(uint32 diff)
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;

    // A boss that left without dying (despawned, removed by a GM) comes back.
    if (m_bossSpawned)
    {
        Creature* boss = m_player->GetMap()->GetCreature(m_bossGuid);
        if (!boss || (boss->IsAlive() && boss->HasCreatureState(CSTATE_DESPAWNING)))
            m_bossSpawned = false;
    }

    if (!m_bossSpawned && elapsedSeconds >= m_map.durationSeconds)
    {
        BonkMapCreature const* boss = PickCreature(BONK_SPAWN_BOSS);
        if (!boss)
        {
            OpenPortal(nullptr);
            return;
        }

        // Without room around the player, try again next update.
        if (Creature* mob = SpawnMob(*boss))
        {
            m_bossSpawned = true;
            m_player->GetBonkPlayer()->Notify(std::string(mob->GetName()) + " has arrived!");
        }
    }

    m_spawnTimer -= int32(diff);
    m_eliteTimer -= int32(diff);
    if (m_spawnTimer > 0 && m_eliteTimer > 0)
        return;

    PruneMobs();

    // Elites by the spawn curve; held back near a mini-boss, once the boss
    // is up, and where the curve has none.
    if (m_eliteTimer <= 0)
    {
        uint32 const interval = GetEliteIntervalMs();
        if (!interval || IsQuietTime() || m_bossSpawned || m_mobs.size() >= m_tier.maxAlive)
            m_eliteTimer = int32(BONK_ELITE_RETRY_MS);
        else
        {
            m_eliteTimer += int32(interval);
            if (BonkMapCreature const* elite = PickCreature(BONK_SPAWN_ELITE))
                SpawnMob(*elite);
        }
    }

    // The crowd is kept at least as big as the curve says, a few at a time.
    if (m_spawnTimer <= 0)
    {
        m_spawnTimer += int32(BONK_SPAWN_TICK_MS);

        uint32 const target = GetSpawnTarget();
        uint32 const trash = CountTrash();
        uint32 const alive = m_mobs.size();
        uint32 count = target > trash ? target - trash : 0;
        count = std::min(count, m_tier.maxAlive > alive ? m_tier.maxAlive - alive : 0u);
        count = std::min(count, BONK_SPAWN_PER_TICK);
        for (uint32 i = 0; i < count; ++i)
            if (BonkMapCreature const* creature = PickTrash())
                SpawnMob(*creature);
    }
}

BonkSpawnPoint const& BonkRun::GetSpawnPoint() const
{
    std::vector<BonkSpawnPoint> const& curve = sBonkMgr.GetSpawnCurve();
    uint32 const second = m_elapsedMs / IN_MILLISECONDS;
    size_t index = 0;
    while (index + 1 < curve.size() && curve[index + 1].second <= second)
        ++index;
    return curve[index];
}

float BonkRun::GetCurveMinAlive() const
{
    // Rises evenly from one point of the curve to the next.
    std::vector<BonkSpawnPoint> const& curve = sBonkMgr.GetSpawnCurve();
    float const second = m_elapsedMs / float(IN_MILLISECONDS);
    size_t index = 0;
    while (index + 1 < curve.size() && curve[index + 1].second <= second)
        ++index;

    float minAlive = curve[index].minAlive;
    if (index + 1 < curve.size())
    {
        BonkSpawnPoint const& next = curve[index + 1];
        float const span = float(next.second - curve[index].second);
        if (span > 0.0f)
            minAlive += (next.minAlive - minAlive) * (second - curve[index].second) / span;
    }
    return minAlive * m_tier.minAliveMultiplier * (1.0f + m_bonuses.spawnRate / 100.0f);
}

uint32 BonkRun::GetSpawnTarget() const
{
    float target = std::min(GetCurveMinAlive(), float(m_tier.maxAlive));
    if (m_bossSpawned)
        target *= BONK_BOSS_CROWD;
    return uint32(target + 0.5f);
}

float BonkRun::GetCrowdOverflow() const
{
    return std::max(1.0f, GetCurveMinAlive() / float(m_tier.maxAlive));
}

uint32 BonkRun::GetEliteIntervalMs() const
{
    uint32 const interval = GetSpawnPoint().eliteIntervalSeconds;
    if (!interval)
        return 0;
    return uint32(interval * IN_MILLISECONDS / (1.0f + m_bonuses.eliteRate / 100.0f));
}

BonkMapCreature const* BonkRun::PickTrash() const
{
    bool const bruiser = !m_bossSpawned && CountBruisers() < m_tier.maxBruisers && roll_chance_f(GetSpawnPoint().bruiserShare * 100.0f);
    if (bruiser)
        if (BonkMapCreature const* creature = PickCreature(BONK_SPAWN_NORMAL, BruiserPick::ONLY))
            return creature;
    return PickCreature(BONK_SPAWN_NORMAL, BruiserPick::NONE);
}

uint32 BonkRun::CountTrash() const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI()))
                if (ai->GetKind() == BONK_SPAWN_NORMAL)
                    ++count;
    return count;
}

uint32 BonkRun::CountBruisers() const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive())
                if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI()))
                    if (ai->GetKind() == BONK_SPAWN_NORMAL && ai->GetArchetype() == BONK_ARCHETYPE_BRUISER)
                        ++count;
    return count;
}

BonkMapCreature const* BonkRun::PickCreature(BonkSpawnKind kind, BruiserPick bruisers) const
{
    uint32 const elapsedSeconds = m_elapsedMs / IN_MILLISECONDS;

    // Only the enemies whose window is open, and none past their cap: a few
    // chargers or casters in the crowd, never a whole pack of them.
    std::vector<BonkMapCreature const*> candidates;
    uint32 totalWeight = 0;
    for (auto const& creature : m_map.creatures)
    {
        if (creature.kind != kind || elapsedSeconds < creature.minSecond || (creature.maxSecond && elapsedSeconds > creature.maxSecond))
            continue;
        if (kind == BONK_SPAWN_NORMAL)
        {
            // Every archetype has its time in a stage; bruisers come sooner after the first.
            BonkArchetypeInfo const& archetype = sBonkMgr.GetArchetype(creature.archetype);
            if (elapsedSeconds < (m_stage > 1 ? archetype.firstSecondLater : archetype.firstSecond))
                continue;
            bool const isBruiser = creature.archetype == BONK_ARCHETYPE_BRUISER;
            if ((bruisers == BruiserPick::ONLY && !isBruiser) || (bruisers == BruiserPick::NONE && isBruiser))
                continue;
        }
        if (creature.maxAlive && CountAlive(creature.entry) >= creature.maxAlive)
            continue;
        candidates.push_back(&creature);
        totalWeight += creature.weight;
    }

    if (!totalWeight)
        return nullptr;

    uint32 roll = urand(1, totalWeight);
    for (BonkMapCreature const* creature : candidates)
    {
        if (roll <= creature->weight)
            return creature;
        roll -= creature->weight;
    }
    return nullptr;
}

uint32 BonkRun::CountAlive(uint32 entry) const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (guid.GetEntry() == entry)
            if (Creature const* mob = map->GetCreature(guid))
                if (mob->IsAlive() && !mob->HasCreatureState(CSTATE_DESPAWNING))
                    ++count;
    return count;
}

bool BonkRun::FindSpawnPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const
{
    for (uint32 attempt = 0; attempt < 10; ++attempt)
        if (FindSpawnPositionAt(frand(0.0f, 2.0f * M_PI_F), frand(minDistance, maxDistance), x, y, z))
            return true;
    return false;
}

bool BonkRun::FindSpawnPositionAt(float angle, float distance, float& x, float& y, float& z) const
{
    Map const* map = m_player->GetMap();
    float const playerZ = m_player->GetPositionZ();

    x = m_player->GetPositionX() + distance * std::cos(angle);
    y = m_player->GetPositionY() + distance * std::sin(angle);
    z = map->GetHeight(x, y, playerZ + BONK_SPAWN_MAX_HEIGHT_DIFF, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);

    if (z <= INVALID_HEIGHT || std::fabs(z - playerZ) > BONK_SPAWN_MAX_HEIGHT_DIFF)
        return false;

    if (m_map.zoneId && map->GetTerrain()->GetZoneId(x, y, z) != m_map.zoneId)
        return false;

    // Ground behind a wall, up a cliff or inside rock has a height too; only
    // spots with a path to the player will do.
    PathFinder path(m_player);
    path.ExcludeSteepSlopes();
    path.calculate(x, y, z);
    if (path.getPathType() & PATHFIND_NOT_USING_PATH)
        return true;    // no path mesh for this map
    if (!(path.getPathType() & PATHFIND_NORMAL) || path.Length() > distance * BONK_SPAWN_MAX_DETOUR)
        return false;

    Vector3 const end = path.getActualEndPosition();
    if ((end.x - x) * (end.x - x) + (end.y - y) * (end.y - y) > BONK_SPAWN_SNAP_DISTANCE * BONK_SPAWN_SNAP_DISTANCE)
        return false;

    x = end.x;
    y = end.y;
    z = end.z;
    return true;
}

void BonkRun::UpdateHeading(uint32 diff)
{
    m_headingTimer += diff;
    if (m_headingTimer < BONK_HEADING_SAMPLE_MS)
        return;
    float const seconds = m_headingTimer / float(IN_MILLISECONDS);
    m_headingTimer = 0;

    float const x = m_player->GetPositionX();
    float const y = m_player->GetPositionY();
    float const dx = x - m_lastX;
    float const dy = y - m_lastY;
    m_lastX = x;
    m_lastY = y;

    if (dx * dx + dy * dy > BONK_HEADING_MAX_STEP * BONK_HEADING_MAX_STEP)
    {
        m_moveX = m_moveY = 0.0f;
        return;
    }

    float const blend = std::min(1.0f, seconds / BONK_HEADING_SMOOTH_SEC);
    m_moveX += (dx / seconds - m_moveX) * blend;
    m_moveY += (dy / seconds - m_moveY) * blend;
}

bool BonkRun::GetHeading(float& angle) const
{
    if (m_moveX * m_moveX + m_moveY * m_moveY < BONK_HEADING_MIN_SPEED * BONK_HEADING_MIN_SPEED)
        return false;
    angle = std::atan2(m_moveY, m_moveX);
    return true;
}

float BonkRun::GetForwardAngle() const
{
    float angle;
    return GetHeading(angle) ? angle : m_player->GetOrientation();
}

bool BonkRun::IsAhead(float x, float y, float forward) const
{
    float const dx = x - m_player->GetPositionX();
    float const dy = y - m_player->GetPositionY();
    if (dx * dx + dy * dy > BONK_FRONT_RANGE * BONK_FRONT_RANGE)
        return false;
    return AngleBetween(std::atan2(dy, dx), forward) <= BONK_FRONT_ARC;
}

uint32 BonkRun::CountAhead(float forward) const
{
    Map* map = m_player->GetMap();
    uint32 count = 0;
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive() && !mob->HasCreatureState(CSTATE_DESPAWNING) && IsAhead(mob->GetPositionX(), mob->GetPositionY(), forward))
                ++count;
    return count;
}

uint32 BonkRun::GetFrontCap() const
{
    return std::max(BONK_FRONT_MIN, uint32(m_mobs.size() * BONK_FRONT_SHARE + 0.5f));
}

bool BonkRun::FindMobPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const
{
    float const forward = GetForwardAngle();
    bool const frontFull = CountAhead(forward) >= GetFrontCap();
    for (uint32 attempt = 0; attempt < 10; ++attempt)
    {
        // With the front full, anywhere outside the cone ahead.
        float const angle = frontFull ? forward + BONK_FRONT_ARC + frand(0.0f, 2.0f * (M_PI_F - BONK_FRONT_ARC)) : frand(0.0f, 2.0f * M_PI_F);
        bool const distant = attempt < BONK_FAR_TRIES && AngleBetween(angle, forward) <= BONK_FRONT_ARC;
        float const distance = distant ? frand(BONK_FAR_MIN_DISTANCE, BONK_FAR_MAX_DISTANCE) : frand(minDistance, maxDistance);
        if (FindSpawnPositionAt(angle, distance, x, y, z))
            return true;
    }
    return false;
}

void BonkRun::UpdateStragglers(uint32 diff)
{
    m_stragglerTimer += diff;
    if (m_stragglerTimer < BONK_STRAGGLER_TICK_MS)
        return;
    m_stragglerTimer = 0;

    uint32 const now = m_elapsedMs;
    m_packSpots.erase(std::remove_if(m_packSpots.begin(), m_packSpots.end(), [now](PackSpot const& spot) { return spot.untilMs <= now; }), m_packSpots.end());

    // Who is far from the player, and since when; far long enough, they are
    // left behind. Far ahead is still coming.
    Map* map = m_player->GetMap();
    float const forward = GetForwardAngle();
    std::map<ObjectGuid, uint32> farSince;
    std::vector<std::pair<Creature*, uint32>> bosses, waiting;    // with the stage time each was left behind
    for (ObjectGuid const& guid : m_mobs)
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING) || mob->IsWithinDistInMap(m_player, BONK_STRAY_DISTANCE) ||
            IsAhead(mob->GetPositionX(), mob->GetPositionY(), forward))
            continue;
        auto const itr = m_farSince.find(guid);
        uint32 const since = itr != m_farSince.end() ? itr->second : now;
        farSince[guid] = since;
        if (now - since < BONK_STRAY_MS)
            continue;

        BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
        bool const boss = ai && (ai->GetKind() == BONK_SPAWN_BOSS || ai->GetKind() == BONK_SPAWN_MINIBOSS);
        (boss ? bosses : waiting).emplace_back(mob, since + BONK_STRAY_MS);
    }
    m_farSince = std::move(farSince);
    if (bosses.empty() && waiting.empty())
        return;

    // Standing still, they walk back on their own; ones still gone after a
    // while (too far off for the map to move them) come back like new ones.
    float heading;
    if (!GetHeading(heading))
    {
        waiting.insert(waiting.end(), bosses.begin(), bosses.end());
        for (auto const& itr : waiting)
        {
            float x, y, z;
            if (now - itr.second >= BONK_STRAGGLER_SIDE_MS && FindMobPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
                MoveMob(itr.first, x, y, z);
        }
        return;
    }

    uint32 ahead = CountAhead(heading);
    uint32 const cap = GetFrontCap();

    // Bosses and mini-bosses come back at once and alone: ahead if there is
    // room, else at the side.
    for (auto const& itr : bosses)
    {
        if (ahead < cap && PlacePack({ itr.first }, heading))
            ++ahead;
        else
            PlaceAside(itr.first, heading);
    }

    // The rest in packs, oldest first, while there is room ahead; fewer than
    // a pack once the oldest has waited a while.
    std::stable_sort(waiting.begin(), waiting.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    size_t next = 0;
    while (next < waiting.size())
    {
        uint32 const room = cap > ahead ? cap - ahead : 0;
        uint32 const left = uint32(waiting.size() - next);
        uint32 const size = std::min({ urand(BONK_PACK_MIN, BONK_PACK_MAX), left, room });
        bool const waitedLong = now - waiting[next].second >= BONK_PACK_WAIT_MS;
        if (!size || (size < BONK_PACK_MIN && !waitedLong))
            break;

        std::vector<Creature*> pack;
        for (uint32 i = 0; i < size; ++i)
            pack.push_back(waiting[next + i].first);
        if (!PlacePack(pack, heading))
            break;
        next += size;
        ahead += size;
    }

    // No room ahead for too long: at the side instead.
    for (size_t i = next; i < waiting.size(); ++i)
        if (now - waiting[i].second >= BONK_STRAGGLER_SIDE_MS)
            PlaceAside(waiting[i].first, heading);
}

bool BonkRun::PlacePack(std::vector<Creature*> const& pack, float heading)
{
    Map* map = m_player->GetMap();
    float const playerX = m_player->GetPositionX();
    float const playerY = m_player->GetPositionY();

    for (uint32 attempt = 0; attempt < 8; ++attempt)
    {
        // As far off as new enemies ahead, or the map's distance indoors.
        float const distance = attempt < BONK_FAR_TRIES ? frand(BONK_FAR_MIN_DISTANCE, BONK_FAR_MAX_DISTANCE) :
            frand(m_map.spawnMinDistance, m_map.spawnMaxDistance);
        float centreX, centreY, centreZ;
        if (!FindSpawnPositionAt(heading + frand(-BONK_FRONT_ARC, BONK_FRONT_ARC), distance, centreX, centreY, centreZ))
            continue;

        // A way between packs, and none on top of enemies already there.
        auto const within = [centreX, centreY](float x, float y, float range)
        {
            return (x - centreX) * (x - centreX) + (y - centreY) * (y - centreY) < range * range;
        };
        if (std::any_of(m_packSpots.begin(), m_packSpots.end(), [&within](PackSpot const& spot) { return within(spot.x, spot.y, BONK_PACK_SPACING); }))
            continue;
        bool crowded = false;
        for (ObjectGuid const& guid : m_mobs)
            if (Creature const* other = map->GetCreature(guid))
                if (other->IsAlive() && std::find(pack.begin(), pack.end(), other) == pack.end() &&
                    within(other->GetPositionX(), other->GetPositionY(), BONK_PACK_CLEARANCE))
                {
                    crowded = true;
                    break;
                }
        if (crowded)
            continue;

        // The first stands on the spot, the others around it where the
        // ground allows.
        for (size_t i = 0; i < pack.size(); ++i)
        {
            float x = centreX, y = centreY, z = centreZ;
            if (i)
            {
                float const offsetAngle = frand(0.0f, 2.0f * M_PI_F);
                float const offset = frand(1.0f, BONK_PACK_RADIUS);
                float const memberX = centreX + offset * std::cos(offsetAngle);
                float const memberY = centreY + offset * std::sin(offsetAngle);
                float const angle = std::atan2(memberY - playerY, memberX - playerX);
                float const distance = std::sqrt((memberX - playerX) * (memberX - playerX) + (memberY - playerY) * (memberY - playerY));
                if (!FindSpawnPositionAt(angle, distance, x, y, z))
                {
                    x = centreX;
                    y = centreY;
                    z = centreZ;
                }
            }
            MoveMob(pack[i], x, y, z);
        }

        m_packSpots.push_back({ centreX, centreY, m_elapsedMs + BONK_PACK_SPOT_MS });
        return true;
    }
    return false;
}

bool BonkRun::PlaceAside(Creature* mob, float heading)
{
    for (uint32 attempt = 0; attempt < 10; ++attempt)
    {
        float const side = urand(0, 1) ? 1.0f : -1.0f;
        float const angle = heading + side * frand(BONK_SIDE_MIN_ANGLE, BONK_SIDE_MAX_ANGLE);
        float x, y, z;
        if (FindSpawnPositionAt(angle, frand(m_map.spawnMinDistance, m_map.spawnMaxDistance), x, y, z))
        {
            MoveMob(mob, x, y, z);
            return true;
        }
    }
    return false;
}

void BonkRun::MoveMob(Creature* mob, float x, float y, float z)
{
    float const facing = std::atan2(m_player->GetPositionY() - y, m_player->GetPositionX() - x);
    mob->NearTeleportTo(x, y, z, facing);
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
        ai->Arrive();
}

uint32 BonkRun::GetMobLevel() const
{
    int32 const level = int32(m_player->GetLevel()) + m_map.levelOffset;
    return uint32(std::max(1, std::min(level, 63)));
}

float BonkRun::GetMobHealth(uint32 level) const
{
    if (CreatureClassLevelStats const* stats = sObjectMgr.GetCreatureClassLevelStats(CLASS_WARRIOR, level))
        return float(stats->health);
    return 40.0f * level;
}

float BonkRun::GetStageHealthMultiplier() const
{
    // Stage 2 +25%, stage 3 +60%, and steeper after (bonk_tier growth and accel).
    float const stages = float(m_stage - 1);
    return m_tier.healthMultiplier * (1.0f + m_tier.stageHealthGrowth * stages + m_tier.stageHealthAccel * stages * std::max(0.0f, stages - 1.0f)) *
        (1.0f + m_bonuses.enemyHealth / 100.0f);
}

float BonkRun::GetStageDamageMultiplier() const
{
    float const stages = float(m_stage - 1);
    return m_tier.damageMultiplier * (1.0f + m_tier.stageDamageGrowth * stages + m_tier.stageDamageAccel * stages * std::max(0.0f, stages - 1.0f)) *
        (1.0f + m_bonuses.enemyDamage / 100.0f);
}

void BonkRun::UpdateGear()
{
    BonkGearTotals const gear = sBonkMgr.EvaluateGear(m_player);
    bool const passivesChanged = !std::equal(std::begin(gear.passives), std::end(gear.passives), std::begin(m_gear.passives));
    m_gear = gear;

    // Set bonuses can raise passives that live on auras (speed, health).
    if (passivesChanged)
        ApplyPassives();
}

uint32 BonkRun::MitigateDamage(uint32 damage)
{
    if (!damage)
        return 0;

    if (float const armor = GetStat(BONK_PASSIVE_ARMOR))
        damage = std::max(1u, uint32(damage * (1.0f - armor / 100.0f)));

    m_shieldIdleMs = 0;

    // Spell Shields first: they do not come back on their own.
    if (m_spellShield)
    {
        uint32 const absorbed = std::min(m_spellShield, damage);
        m_spellShield -= absorbed;
        damage -= absorbed;
        if (!m_spellShield && HasBehavior(BONK_BEHAVIOR_WARD))
            m_wardBurst = true;
    }

    if (m_shield)
    {
        uint32 const absorbed = std::min(m_shield, damage);
        m_shield -= absorbed;
        damage -= absorbed;
    }
    if (!m_shield && !m_spellShield)
        m_player->RemoveAurasDueToSpell(BONK_SPELL_SHIELD_VISUAL);
    return damage;
}

void BonkRun::AddNecroticStack()
{
    if (m_necroticStacks < BONK_NECROTIC_MAX_STACKS)
        SetNecroticStacks(m_necroticStacks + 1);
}

void BonkRun::SetNecroticStacks(uint32 stacks)
{
    if (stacks == m_necroticStacks)
        return;

    bool const had = m_necroticStacks > 0;
    m_necroticStacks = stacks;
    if (stacks && !had && m_player->IsAlive())
        m_player->AddAura(BONK_SPELL_NECROTIC);
    else if (!stacks && had)
        m_player->RemoveAurasDueToSpell(BONK_SPELL_NECROTIC);
    m_player->GetBonkPlayer()->SendAddon("necrotic~" + std::to_string(stacks) + "~" + Percent(stacks * BONK_NECROTIC_PER_STACK));
}

void BonkRun::UpdateNecrotic(uint32 diff)
{
    if (!m_necroticStacks)
        return;
    m_necroticCheckMs += diff;
    if (m_necroticCheckMs < BONK_NECROTIC_CHECK_MS)
        return;
    m_necroticCheckMs = 0;

    // The stacks hold while a living Necrotic enemy is close, and are gone
    // the moment none is.
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : m_mobs)
        if (Creature const* mob = map->GetCreature(guid))
            if (mob->IsAlive())
                if (BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI()))
                    if (float const range = ai->GetNecroticRange())
                        if (m_player->IsWithinDistInMap(mob, range))
                            return;
    SetNecroticStacks(0);
}

uint32 BonkRun::ReduceHealing(uint32 heal) const
{
    if (!m_necroticStacks)
        return heal;
    float const kept = 1.0f - std::min(100.0f, m_necroticStacks * BONK_NECROTIC_PER_STACK) / 100.0f;
    return uint32(heal * kept);
}

void BonkRun::OnAffixedDeath(Creature* mob)
{
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
    if (!ai)
        return;

    // Bolstering: the enemies around it, bosses aside, grow stronger.
    if (ai->IsBolstering())
        for (Creature* other : GetMobsInRange(BONK_BOLSTER_RANGE, mob))
            if (BonkMobAI* otherAi = dynamic_cast<BonkMobAI*>(other->AI()))
                if (otherAi->GetKind() == BONK_SPAWN_NORMAL || otherAi->GetKind() == BONK_SPAWN_ELITE)
                    otherAi->Bolster();

    // Sanguine: a pool that heals enemies standing in it.
    if (ai->IsSanguine())
    {
        Pool pool;
        pool.x = mob->GetPositionX();
        pool.y = mob->GetPositionY();
        pool.z = mob->GetPositionZ();
        pool.leftMs = BONK_SANGUINE_DURATION_MS;
        if (Creature* marker = m_player->SummonCreature(BONK_SANGUINE_POOL, pool.x, pool.y, pool.z, 0.0f,
            TEMPSUMMON_TIMED_DESPAWN, BONK_SANGUINE_DURATION_MS * 2, false, 0, &InstallOrbAI))
        {
            marker->SetFactionTemplateId(BONK_ORB_FACTION);
            marker->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
            marker->AddAura(BONK_SPELL_SANGUINE);
            pool.marker = marker->GetObjectGuid();
        }
        m_pools.push_back(pool);
    }
}

void BonkRun::UpdatePools(uint32 diff)
{
    if (m_pools.empty())
        return;

    m_poolTimer += diff;
    bool const tick = m_poolTimer >= IN_MILLISECONDS;
    if (tick)
        m_poolTimer -= IN_MILLISECONDS;

    Map* map = m_player->GetMap();
    for (auto itr = m_pools.begin(); itr != m_pools.end();)
    {
        if (tick)
            for (ObjectGuid const& guid : m_mobs)
                if (Creature* mob = map->GetCreature(guid))
                    if (mob->IsAlive() && mob->GetHealth() < mob->GetMaxHealth() &&
                        mob->GetDistance2d(itr->x, itr->y) <= BONK_SANGUINE_RADIUS)
                        mob->ModifyHealth(int32(std::max(1.0f, mob->GetMaxHealth() * BONK_SANGUINE_HEAL)));

        itr->leftMs = itr->leftMs > diff ? itr->leftMs - diff : 0;
        if (itr->leftMs)
        {
            ++itr;
            continue;
        }
        if (Creature* marker = map->GetCreature(itr->marker))
            marker->DespawnOrUnsummon();
        itr = m_pools.erase(itr);
    }
}

void BonkRun::DespawnPools()
{
    if (Map* map = m_player->FindMap())
        for (Pool const& pool : m_pools)
            if (Creature* marker = map->GetCreature(pool.marker))
                marker->DespawnOrUnsummon();
    m_pools.clear();
}

uint32 BonkRun::GetMaxShield() const
{
    float const shield = GetPassiveTotal(BONK_PASSIVE_SHIELD);
    return shield > 0.0f ? uint32(m_player->GetMaxHealth() * shield / 100.0f) : 0;
}

void BonkRun::UpdateShield(uint32 diff)
{
    uint32 const max = GetMaxShield();
    if (m_shield > max)
        m_shield = max;
    else if (m_shield < max)
    {
        // Refills all at once after a few seconds without a hit.
        m_shieldIdleMs += diff;
        if (m_shieldIdleMs >= BONK_SHIELD_REGEN_DELAY_MS)
            m_shield = max;
    }

    // Both Shields together: the stat Shield and spell Shields.
    uint32 const total = m_shield + m_spellShield;
    if (m_player->IsAlive())
    {
        bool const shown = m_player->HasAura(BONK_SPELL_SHIELD_VISUAL);
        if (total && !shown)
            m_player->AddAura(BONK_SPELL_SHIELD_VISUAL, ADD_AURA_PERMANENT);
        else if (!total && shown)
            m_player->RemoveAurasDueToSpell(BONK_SPELL_SHIELD_VISUAL);
    }

    m_shieldSendTimer += diff;
    if ((total != m_shieldSent || max != m_shieldMaxSent) && m_shieldSendTimer >= BONK_SHIELD_SEND_MS)
    {
        m_shieldSendTimer = 0;
        m_shieldSent = total;
        m_shieldMaxSent = max;
        m_player->GetBonkPlayer()->SendAddon("shield~" + std::to_string(total) + "~" + std::to_string(max));
    }
}

BonkRun* BonkRun::FromAttack(SpellCaster const* attacker, Unit const* victim)
{
    Unit const* unit = attacker ? attacker->ToUnit() : nullptr;
    if (!unit || !victim || !victim->IsCreature())
        return nullptr;

    Player const* player = unit->GetCharmerOrOwnerPlayerOrPlayerItself();
    if (!player)
        return nullptr;

    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(victim->AI());
    if (!ai || ai->GetOwnerGuid() != player->GetObjectGuid())
        return nullptr;
    return player->GetBonkPlayer()->GetRun();
}

bool BonkRun::RollDodge(Unit const* victim) const
{
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(victim->AI());
    if (!ai || ai->GetKind() == BONK_SPAWN_NORMAL)
        return false;

    float chance = m_tier.eliteDodge - GetStat(BONK_PASSIVE_HIT);
    if (ai->GetKind() != BONK_SPAWN_ELITE)
        chance += BONK_BOSS_EXTRA_DODGE;
    return chance > 0.0f && roll_chance_f(chance);
}

float BonkRun::GetCritChance(SpellEntry const* spell) const
{
    float const chance = BONK_BASE_CRIT + GetPassiveTotal(BONK_PASSIVE_CRIT);
    if (IsReckless())
        return std::max(100.0f, chance);
    if (spell)
        for (OwnedWeapon const& owned : m_weapons)
            if (owned.weapon.spellId == spell->Id && owned.weapon.HasFlag(BONK_WEAPON_FLAG_ALWAYS_CRIT))
                return std::max(100.0f, chance);
    return chance;
}

float BonkRun::RollCritMultiplier(bool weaponAttack, Unit const* victim) const
{
    float bonus = (weaponAttack ? 1.0f : 0.5f) + std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_CRIT_DAMAGE)) / 100.0f;
    if (victim && victim->HasAuraType(SPELL_AURA_MOD_ROOT) && HasBehavior(BONK_BEHAVIOR_FREEZE))
        bonus += BONK_FROZEN_CRIT_BONUS;
    float multiplier = 1.0f + bonus;

    float const overcrit = GetCritChance() - 100.0f;
    if (overcrit > 0.0f && roll_chance_f(overcrit))
        multiplier *= 1.0f + bonus;
    return multiplier;
}

float BonkRun::GetDamageMultiplier(SpellEntry const* spell, Unit const* victim) const
{
    // Power, a shout's Power and Mastery add up rather than multiply; a
    // mark on the enemy multiplies everything.
    float percent = GetPassiveTotal(BONK_PASSIVE_POWER) + GetBuffPower();
    float const mark = 1.0f + GetMarkBonus(victim) / 100.0f;
    if (!spell)
        return std::max(0.0f, 1.0f + percent / 100.0f) * mark;

    // Class Mastery: the spell itself, its school, and the state of the enemy.
    uint32 const school = spell->GetSpellSchoolMask();
    auto root = m_spellRoots.find(spell->Id);
    if (root != m_spellRoots.end())
    {
        auto bonus = m_mastery.spellDamage.find(root->second);
        if (bonus != m_mastery.spellDamage.end())
            percent += bonus->second;
    }
    for (auto const& bonus : m_mastery.schoolDamage)
        if (bonus.first & school)
            percent += bonus.second;

    if (victim)
    {
        if (m_mastery.shatter > 0.0f && (victim->HasAuraType(SPELL_AURA_MOD_DECREASE_SPEED) || victim->HasAuraType(SPELL_AURA_MOD_ROOT)))
            percent += m_mastery.shatter;

        float const health = victim->GetHealthPercent();
        if (health > BONK_MASTERY_OPENER_HEALTH)
            for (auto const& bonus : m_mastery.opener)
                if (!bonus.first || (bonus.first & school))
                    percent += bonus.second;
        if (health < BONK_MASTERY_EXECUTE_HEALTH)
            for (auto const& bonus : m_mastery.execute)
                if (!bonus.first || (bonus.first & school))
                    percent += bonus.second;
    }

    float multiplier = std::max(0.0f, 1.0f + percent / 100.0f) * mark;

    // An executing spell hits enemies low on health many times as hard.
    if (victim)
        for (OwnedWeapon const& owned : m_weapons)
            if (owned.weapon.behavior == BONK_BEHAVIOR_EXECUTE && owned.weapon.spellId == spell->Id &&
                victim->GetHealthPercent() < owned.weapon.behaviorValue)
                multiplier *= owned.weapon.altDamage;
    return multiplier;
}

uint32 BonkRun::GetRootWeapon(BonkWeapon const& weapon) const
{
    auto itr = m_spellRoots.find(weapon.spellId);
    return itr != m_spellRoots.end() ? itr->second : weapon.id;
}

void BonkRun::SoulSiphon(uint32 killingSpell)
{
    if (m_mastery.soulSiphon.empty() || !killingSpell || !m_player->IsAlive())
        return;
    SpellEntry const* spell = sSpellMgr.GetSpellEntry(killingSpell);
    if (!spell)
        return;

    float percent = 0.0f;
    for (auto const& siphon : m_mastery.soulSiphon)
        if (siphon.first & spell->GetSpellSchoolMask())
            percent += siphon.second;

    // A swarm dying at once must not fill the bar: capped per second.
    uint32 const now = WorldTimer::getMSTime();
    if (WorldTimer::getMSTimeDiff(m_siphonWindow, now) >= IN_MILLISECONDS)
    {
        m_siphonWindow = now;
        m_siphonHealed = 0.0f;
    }
    percent = std::min(percent, BONK_MASTERY_SIPHON_CAP - m_siphonHealed);
    if (percent <= 0.0f)
        return;
    m_siphonHealed += percent;
    m_player->ModifyHealth(int32(ReduceHealing(uint32(std::max(1.0f, m_player->GetMaxHealth() * percent / 100.0f)))));
}

void BonkRun::AwardMastery()
{
    if (m_masteryAwarded)
        return;
    m_masteryAwarded = true;
    m_player->GetBonkPlayer()->AddMasteryXp(m_totalMs / (MINUTE * IN_MILLISECONDS) * m_tier.masteryXpMinute + m_masteryXp);
}

void BonkRun::Revive()
{
    --m_revives;
    m_player->ResurrectPlayer(0.5f);
    m_player->SpawnCorpseBones();
    m_player->CastSpell(m_player, BONK_SPELL_REVIVE_SHIELD, true);

    // Room to breathe: normal enemies close by are swept away; bosses and
    // elites stay.
    Map* map = m_player->GetMap();
    for (ObjectGuid const& guid : m_mobs)
        if (Creature* mob = map->GetCreature(guid))
            if (mob->IsAlive() && m_player->IsWithinDistInMap(mob, BONK_REVIVE_CLEAR_RADIUS))
                if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                    if (ai->GetKind() == BONK_SPAWN_NORMAL)
                        mob->DespawnOrUnsummon();
    m_player->GetBonkPlayer()->Notify(m_revives ? "Second wind! " + std::to_string(m_revives) + " left." : "Second wind!");
    SendState();
}

Creature* BonkRun::SpawnMob(BonkMapCreature const& creature)
{
    float x, y, z;
    if (!FindMobPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
        return nullptr;
    return SpawnMobAt(creature, x, y, z);
}

Creature* BonkRun::SpawnMobAt(BonkMapCreature const& creature, float x, float y, float z)
{
    float const facing = std::atan2(m_player->GetPositionY() - y, m_player->GetPositionX() - x);
    Creature* mob = m_player->SummonCreature(creature.entry, x, y, z, facing, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 2000, false, 0, &BonkMobAI::Install);
    if (!mob)
        return nullptr;

    // Only a template's looks are used: no auras from it (Double Attack,
    // stealth), and no invulnerability or immunity from its static flags.
    mob->RemoveAllAuras();
    mob->SetInvincibilityHpThreshold(0);
    mob->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_NOT_SELECTABLE);

    uint32 const level = std::min(GetMobLevel() + GetKindLevelBonus(creature.kind), 63u);
    mob->SetLevel(level);
    mob->InitStatsForLevel();

    // Stats are set once, at spawn, from the run's clock (not the stage's)
    // and the player's level; spells deal a share of GetMobHealth, so only
    // these factors make enemies tougher relative to the player.
    float const minutes = GetRunMinutes();
    float const levelGrowth = 1.0f + BONK_HEALTH_PER_LEVEL * (m_player->GetLevel() - 1);
    // Normal enemies by their archetype; a crowd bigger than the tier's cap
    // makes everything tougher instead.
    BonkArchetypeInfo const* archetype = creature.kind == BONK_SPAWN_NORMAL ? &sBonkMgr.GetArchetype(creature.archetype) : nullptr;
    float const overflow = GetCrowdOverflow();
    float const archetypeHealth = archetype ? archetype->healthMultiplier : 1.0f;
    float const archetypeDamage = archetype ? archetype->damageMultiplier : 1.0f;
    uint32 const health = std::max(1u, uint32(GetMobHealth(level) * GetStageHealthMultiplier() * m_map.healthMultiplier *
        creature.healthMultiplier * archetypeHealth * overflow * GetKindHealth(creature.kind) * levelGrowth * (1.0f + m_map.healthGrowthPerMinute * minutes)));
    mob->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, float(health));
    mob->SetMaxHealth(health);
    mob->SetHealth(health);

    if (CreatureClassLevelStats const* stats = sObjectMgr.GetCreatureClassLevelStats(CLASS_WARRIOR, level))
    {
        // Gentle at the start of a run, harder the longer it goes.
        float const damage = stats->melee_damage * GetStageDamageMultiplier() * m_map.damageMultiplier * creature.damageMultiplier *
            archetypeDamage * overflow * GetKindDamage(creature.kind) * (1.0f + m_map.damageGrowthPerMinute * minutes);
        mob->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, damage * 0.85f);
        mob->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, damage * 1.15f);
        mob->UpdateDamagePhysical(BASE_ATTACK);
    }

    mob->SetFactionTemplateId(BONK_ENEMY_FACTION);
    mob->SetBaseSpeedRate(MOVE_WALK, creature.speed);
    mob->SetBaseSpeedRate(MOVE_RUN, creature.speed);
    if (creature.scale > 0.0f && creature.scale != 1.0f)
    {
        mob->SetObjectScale(mob->GetObjectScale() * creature.scale);
        // Melee reach and size follow the new scale.
        mob->UpdateModelData();
    }

    // Bruisers must read as bruisers: clearly bigger than the biggest grunt
    // this stage has seen.
    if (archetype && creature.archetype == BONK_ARCHETYPE_GRUNT)
        m_gruntRadius = std::max(m_gruntRadius, mob->GetObjectBoundingRadius());
    else if (archetype && creature.archetype == BONK_ARCHETYPE_BRUISER && m_gruntRadius > 0.0f)
    {
        float const radius = mob->GetObjectBoundingRadius();
        float const wanted = BONK_BRUISER_SIZE * m_gruntRadius;
        if (radius > 0.0f && radius < wanted)
        {
            mob->SetObjectScale(mob->GetObjectScale() * std::min(wanted / radius, BONK_BRUISER_MAX_GROWTH));
            mob->UpdateModelData();
        }
    }
    mob->SetNoXP();

    // Last, so abilities and affixes build on the final stats.
    if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
    {
        ai->SetArchetype(creature.archetype);
        ai->Setup(creature.kind);
    }

    m_mobs.push_back(mob->GetObjectGuid());
    if (creature.kind == BONK_SPAWN_BOSS)
        m_bossGuid = mob->GetObjectGuid();
    return mob;
}

std::vector<Creature*> BonkRun::GetMobsInRange(float range, WorldObject const* center) const
{
    if (!center)
        center = m_player;

    Map* map = m_player->GetMap();
    std::vector<std::pair<float, Creature*>> found;
    for (ObjectGuid const& guid : m_mobs)
    {
        Creature* mob = map->GetCreature(guid);
        if (!mob || !mob->IsAlive() || mob->HasCreatureState(CSTATE_DESPAWNING))
            continue;

        if (mob == center)
            continue;
        float const distance = center->GetDistance(mob);
        if (distance <= range)
            found.emplace_back(distance, mob);
    }

    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });

    std::vector<Creature*> mobs;
    mobs.reserve(found.size());
    for (auto const& itr : found)
        mobs.push_back(itr.second);
    return mobs;
}

void BonkRun::DespawnMobs()
{
    Map* map = m_player->FindMap();
    if (map)
        for (ObjectGuid const& guid : m_mobs)
            if (Creature* mob = map->GetCreature(guid))
            {
                // A delayed despawn waits on the mob's own clock.
                mob->SetTimeFrozen(false);
                mob->DespawnOrUnsummon();
            }
    m_mobs.clear();
    m_farSince.clear();
    m_packSpots.clear();
}

GameObject* BonkRun::PlaceObject(uint32 entry, float x, float y, float z, uint32 lifetimeSeconds)
{
    return m_player->SummonGameObject(entry, x, y, z, frand(0.0f, 2.0f * M_PI_F), 0.0f, 0.0f, 0.0f, 0.0f, lifetimeSeconds, false);
}

bool BonkRun::SpawnChest(bool free, float x, float y, float z, uint32 choices)
{
    GameObject* chest = PlaceObject(free ? BONK_GO_CHEST_FREE : BONK_GO_CHEST, x, y, z, BONK_OBJECT_LIFETIME_SEC);
    if (!chest)
        return false;
    m_chests.push_back(chest->GetObjectGuid());
    m_chestChoices[chest->GetObjectGuid()] = choices;
    return true;
}

void BonkRun::DropChest(Creature const* mob, BonkSpawnKind kind)
{
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
    uint32 const affixes = ai ? ai->GetAffixCount() : 0;

    // Elites drop one now and then (always Valor and experience); a run of
    // bad luck ends after BONK_ELITE_CHEST_PITY of them.
    if (kind == BONK_SPAWN_ELITE)
    {
        float const chance = BONK_ELITE_CHEST_CHANCE + BONK_ELITE_CHEST_PER_AFFIX * affixes +
            BONK_ELITE_CHEST_PER_LUCK * std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
        if (m_elitesWithoutChest < BONK_ELITE_CHEST_PITY && !roll_chance_f(chance))
        {
            ++m_elitesWithoutChest;
            return;
        }
        m_elitesWithoutChest = 0;
    }

    // Every affix adds a card to choose from.
    uint32 const choices = std::min(uint32(BONK_OFFER_SIZE) + affixes, uint32(BONK_MAX_OFFER_SIZE));
    SpawnChest(true, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), choices);
}

bool BonkRun::SpawnChestNearby()
{
    // Somewhere the player can see, so it is not buried in a wall.
    for (uint32 attempt = 0; attempt < 5; ++attempt)
    {
        float x, y, z;
        if (!FindSpawnPosition(x, y, z, BONK_CHEST_MIN_DISTANCE, BONK_CHEST_MAX_DISTANCE))
            return false;
        if (!m_player->IsWithinLOS(x, y, z))
            continue;
        return SpawnChest(false, x, y, z);
    }
    return false;
}

uint32 BonkRun::GetChestCost() const
{
    return uint32(m_tier.chestCost * (1.0f + m_tier.chestCostGrowth * m_chestsBought) + 0.5f);
}

void BonkRun::SpawnPowerup()
{
    std::vector<BonkPowerup> const& powerups = sBonkMgr.GetPowerups();
    uint32 totalWeight = 0;
    for (BonkPowerup const& powerup : powerups)
        totalWeight += powerup.weight;
    if (!totalWeight)
        return;

    uint32 roll = urand(1, totalWeight);
    BonkPowerup const* chosen = nullptr;
    for (BonkPowerup const& powerup : powerups)
    {
        if (roll <= powerup.weight)
        {
            chosen = &powerup;
            break;
        }
        roll -= powerup.weight;
    }
    if (!chosen)
        return;

    for (uint32 attempt = 0; attempt < 5; ++attempt)
    {
        float x, y, z;
        if (!FindSpawnPosition(x, y, z, BONK_POWERUP_MIN_DISTANCE, BONK_POWERUP_MAX_DISTANCE))
            return;
        if (!m_player->IsWithinLOS(x, y, z))
            continue;
        if (GameObject* go = PlaceObject(chosen->entry, x, y, z, BONK_POWERUP_LIFETIME_SEC))
            m_powerups.push_back(go->GetObjectGuid());
        return;
    }
}

void BonkRun::UpdatePowerups(uint32 diff, bool spawn)
{
    m_powerupCheckTimer += diff;
    if (m_powerupCheckTimer < BONK_POWERUP_CHECK_MS)
        return;
    uint32 const elapsed = m_powerupCheckTimer;
    m_powerupCheckTimer = 0;

    // Walking over a power-up takes it; unclaimed ones expire on their own.
    Map* map = m_player->GetMap();
    for (auto itr = m_powerups.begin(); itr != m_powerups.end();)
    {
        GameObject* go = map->GetGameObject(*itr);
        if (!go || !go->isSpawned())
        {
            itr = m_powerups.erase(itr);
            continue;
        }

        if (m_player->GetDistance(go) > BONK_POWERUP_PICKUP_RANGE)
        {
            ++itr;
            continue;
        }

        auto powerup = std::find_if(sBonkMgr.GetPowerups().begin(), sBonkMgr.GetPowerups().end(),
            [go](BonkPowerup const& p) { return p.entry == go->GetEntry(); });
        if (powerup != sBonkMgr.GetPowerups().end())
        {
            m_player->CastSpell(m_player, powerup->spellId, true);
            m_player->GetBonkPlayer()->Notify(std::string(go->GetGOInfo()->name) + "!");
        }
        go->Delete();
        itr = m_powerups.erase(itr);
    }

    if (!spawn || !m_map.powerupIntervalSeconds)
        return;

    m_powerupSpawnTimer -= int32(elapsed);
    if (m_powerupSpawnTimer > 0)
        return;
    m_powerupSpawnTimer += m_map.powerupIntervalSeconds * IN_MILLISECONDS;

    if (m_powerups.size() < m_map.powerupMax)
        SpawnPowerup();
}

void BonkRun::DespawnObjects()
{
    if (Map* map = m_player->FindMap())
    {
        for (ObjectGuid const& guid : m_chests)
            if (GameObject* chest = map->GetGameObject(guid))
                chest->Delete();
        for (ObjectGuid const& guid : m_powerups)
            if (GameObject* powerup = map->GetGameObject(guid))
                powerup->Delete();
        for (ShardDrop const& drop : m_shardDrops)
            RemovePickup(drop.guid, drop.marker);
        for (HealDrop const& drop : m_healDrops)
            RemovePickup(drop.guid, drop.marker);
        if (GameObject* portal = map->GetGameObject(m_portalGuid))
            portal->Delete();
    }
    m_chests.clear();
    m_powerups.clear();
    m_shardDrops.clear();
    m_healDrops.clear();
    m_portalGuid.Clear();
}

void BonkRun::UseChest(GameObject* chest)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;

    auto itr = std::find(m_chests.begin(), m_chests.end(), chest->GetObjectGuid());
    if (itr == m_chests.end())
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (chest->GetEntry() != BONK_GO_CHEST_FREE)
    {
        uint32 const cost = GetChestCost();
        if (m_coins < cost)
        {
            bonk->Notify("This chest costs " + std::to_string(cost) + " Valor. You have " + std::to_string(m_coins) + ".");
            return;
        }
        m_coins -= cost;
        ++m_chestsBought;
    }

    m_chests.erase(itr);
    auto choices = m_chestChoices.find(chest->GetObjectGuid());
    m_pendingChestChoices.push_back(choices != m_chestChoices.end() ? choices->second : uint32(BONK_OFFER_SIZE));
    if (choices != m_chestChoices.end())
        m_chestChoices.erase(choices);
    chest->Delete();

    UpdateOffers();
    SendState();
}

bool BonkRun::IsOwnPortal(GameObject const* portal) const
{
    return m_state == STATE_PORTAL && portal->GetObjectGuid() == m_portalGuid;
}

BonkMap const* BonkRun::GetNextMap() const
{
    return sBonkMgr.GetMap(m_nextMapId);
}

void BonkRun::OpenPortal(Creature* boss)
{
    // Enemies despawn themselves once the run is no longer active.
    m_state = STATE_PORTAL;
    m_bossGuid.Clear();
    GiveGold(m_map.stageGold * m_stage);
    CollectAllOrbs();

    BonkMap const* next = ChooseNextMap();
    m_nextMapId = next ? next->id : 0;

    // Just past where the boss died (its chest lands on the spot), or in
    // front of the player.
    if (boss)
    {
        float const angle = m_player->GetAngle(boss);
        PlacePortal(boss->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
            boss->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), boss->GetPositionZ());
    }
    else
    {
        float const angle = m_player->GetOrientation();
        PlacePortal(m_player->GetPositionX() + BONK_PORTAL_DISTANCE * std::cos(angle),
            m_player->GetPositionY() + BONK_PORTAL_DISTANCE * std::sin(angle), m_player->GetPositionZ());
    }

    m_player->GetBonkPlayer()->Notify("Stage " + std::to_string(m_stage) + " cleared! Take the portal onward, or home.");
    SendState();
}

void BonkRun::PlacePortal(float x, float y, float baseZ)
{
    float z = m_player->GetMap()->GetHeight(x, y, baseZ + 5.0f, true, 2.0f * BONK_SPAWN_MAX_HEIGHT_DIFF);
    if (z <= INVALID_HEIGHT || !m_player->IsWithinLOS(x, y, z))
    {
        x = m_player->GetPositionX();
        y = m_player->GetPositionY();
        z = m_player->GetPositionZ();
    }

    if (GameObject* portal = PlaceObject(BONK_GO_PORTAL, x, y, z, BONK_OBJECT_LIFETIME_SEC))
        m_portalGuid = portal->GetObjectGuid();
}

void BonkRun::EnterNextStage()
{
    if (m_state != STATE_PORTAL)
        return;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (!m_offer.empty())
    {
        bonk->Notify("Choose your upgrade first.");
        return;
    }

    BonkMap const* next = GetNextMap();
    if (!next)
        next = ChooseNextMap();
    if (!next)
    {
        bonk->Notify("The portal leads nowhere.");
        return;
    }

    // Shards still lying around go with the player.
    CollectAllShards();
    DespawnObjects();
    DespawnMobs();
    DespawnOrbs();
    DespawnTraps();
    DespawnPets();
    m_telegraphs.Clear(m_player->FindMap());

    std::string error;
    if (!TeleportToStage(*next, error))
    {
        // Stay at the portal (it comes back within a second) with another
        // map to try, rather than ending the run as if the player went home.
        bonk->Notify(error);
        BonkMap const* other = ChooseNextMap();
        m_nextMapId = other ? other->id : 0;
        return;
    }

    ++m_stage;
    m_map = *next;
    m_nextMapId = 0;
}

bool BonkRun::SkipTime(uint32 seconds, std::string& error)
{
    if (m_state != STATE_RUNNING)
    {
        error = "Only a running stage can be skipped.";
        return false;
    }
    if (m_bossSpawned)
    {
        error = "The boss is already here.";
        return false;
    }

    // 0 = straight to the boss.
    uint32 const bossMs = m_map.durationSeconds * IN_MILLISECONDS;
    uint32 const target = seconds ? std::min(m_elapsedMs + seconds * IN_MILLISECONDS, bossMs) : bossMs;
    if (target <= m_elapsedMs)
    {
        error = "The boss is already due.";
        return false;
    }

    m_totalMs += target - m_elapsedMs;
    m_elapsedMs = target;

    // Events in the skipped stretch are dropped, not all fired at once.
    while (m_nextEvent < m_map.events.size() && m_map.events[m_nextEvent].second * IN_MILLISECONDS < target)
        ++m_nextEvent;

    SendState();
    return true;
}

void BonkRun::GoHome()
{
    if (m_state == STATE_PORTAL)
        Finish(true);
}

float BonkRun::GetPassiveTotal(BonkPassiveEffect effect) const
{
    float total = m_bonuses.passives[effect] + m_gear.passives[effect];
    for (auto const& itr : m_passiveRanks)
        if (BonkPassive const* passive = sBonkMgr.GetPassive(itr.first))
            if (passive->effect == effect)
                total += passive->valuePerRank * itr.second;
    // Revenge needs dodges, so it brings some.
    if (effect == BONK_PASSIVE_DODGE)
        for (OwnedWeapon const& owned : m_weapons)
            if (owned.weapon.behavior == BONK_BEHAVIOR_REVENGE)
                total += owned.weapon.behaviorValue;
    return total;
}

float BonkRun::GetStat(BonkPassiveEffect effect) const
{
    float const total = GetPassiveTotal(effect);
    switch (effect)
    {
        case BONK_PASSIVE_ARMOR: return std::min(total, BONK_MAX_ARMOR);
        case BONK_PASSIVE_DODGE: return std::min(total, BONK_MAX_DODGE);
        case BONK_PASSIVE_HIT:   return std::min(total, BONK_MAX_HIT);
        default:                 return total;
    }
}

float BonkRun::GetHasteRate() const
{
    return 1.0f + std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_HASTE)) / 100.0f;
}

void BonkRun::SendStats() const
{
    float totals[MAX_BONK_PASSIVE_EFFECT];
    for (uint8 effect = 0; effect < MAX_BONK_PASSIVE_EFFECT; ++effect)
        totals[effect] = GetPassiveTotal(BonkPassiveEffect(effect)) - m_gear.passives[effect];
    m_player->GetBonkPlayer()->SendStatTotals(totals, true);
}

float BonkRun::GetWeaponDamage(OwnedWeapon const& owned) const
{
    // Power is not in here: GetDamageMultiplier applies it to every hit.
    return GetMobHealth(GetMobLevel()) * owned.weapon.damageFraction * (1.0f + owned.damageBonus / 100.0f);
}

uint32 BonkRun::GetWeaponCooldown(OwnedWeapon const& owned) const
{
    // Haste is a rate, so it never brings the cooldown to nothing; the
    // spell's own faster ranks and a Mastery cooldown bonus count as more.
    float mastery = 0.0f;
    auto itr = m_mastery.spellCooldown.find(GetRootWeapon(owned.weapon));
    if (itr != m_mastery.spellCooldown.end())
        mastery = itr->second;
    float const rate = GetHasteRate() + (owned.hasteBonus + mastery) / 100.0f;
    return std::max(200u, uint32(owned.weapon.cooldownMs / rate));
}

void BonkRun::UpdateWeapons(uint32 diff)
{
    for (auto& owned : m_weapons)
    {
        owned.timer -= int32(diff);
        if (owned.timer <= 0)
            FireWeapon(owned);

        if (m_state != STATE_RUNNING)
            return;
    }
}

float BonkRun::GetWeaponArea(OwnedWeapon const& owned) const
{
    return std::max(0.1f, 1.0f + (owned.areaBonus + GetPassiveTotal(BONK_PASSIVE_AREA)) / 100.0f);
}

uint32 BonkRun::GetWeaponTargets(OwnedWeapon const& owned) const
{
    uint32 mastery = 0;
    auto itr = m_mastery.spellTargets.find(GetRootWeapon(owned.weapon));
    if (itr != m_mastery.spellTargets.end())
        mastery = itr->second;
    return owned.weapon.baseTargets + owned.extraTargets + uint32(GetPassiveTotal(BONK_PASSIVE_AMOUNT)) + mastery;
}

void BonkRun::GetBasePoints(OwnedWeapon const& owned, SpellEntry const* spellInfo, float damageShare, optional<int32>* basePoints) const
{
    BonkWeapon const& weapon = owned.weapon;
    float const damage = GetWeaponDamage(owned) * damageShare;

    // Another spell (an alt cast, a burst): its first damage effect carries it all.
    if (spellInfo->Id != weapon.spellId)
    {
        int32 const index = FindDamageEffectOf(spellInfo);
        if (index >= 0)
            basePoints[index] = std::max(1, int32(damage));
        return;
    }

    if (weapon.damageEffectIndex >= 0)
    {
        // A weapon with its own tick rate deals its damage per second.
        float share = 1.0f;
        if (weapon.tickMs && spellInfo->EffectApplyAuraName[weapon.damageEffectIndex] == SPELL_AURA_PERIODIC_DAMAGE)
            share = weapon.tickMs / float(IN_MILLISECONDS);
        basePoints[weapon.damageEffectIndex] = std::max(1, int32(damage * share));
    }

    if (weapon.tickEffectIndex >= 0)
    {
        float share = 1.0f;
        if (spellInfo->Effect[weapon.tickEffectIndex] == SPELL_EFFECT_APPLY_AURA)
        {
            int32 const duration = m_player->GetBonkPlayer()->AdjustDuration(weapon.spellId, spellInfo->GetDuration());
            uint32 const amplitude = weapon.tickMs ? weapon.tickMs : spellInfo->EffectAmplitude[weapon.tickEffectIndex];
            if (duration > 0 && amplitude)
                share = std::min(1.0f, float(amplitude) / duration);
        }
        basePoints[weapon.tickEffectIndex] = std::max(1, int32(damage * share));
    }
}

void BonkRun::StrikeWith(OwnedWeapon& owned, SpellEntry const* spellInfo, Creature* target, float damageShare, bool followUp)
{
    BonkWeapon const& weapon = owned.weapon;

    // An enemy low enough dies to this hit: it hits for more than is left.
    if (CanKill(weapon, target))
    {
        if (weapon.HasFlag(BONK_WEAPON_FLAG_KILL_WITH_ALT) && weapon.altSpellId)
            if (SpellEntry const* killInfo = sSpellMgr.GetSpellEntry(weapon.altSpellId))
                spellInfo = killInfo;
        damageShare = std::max(damageShare, 2.0f * target->GetHealth() / std::max(1.0f, GetWeaponDamage(owned)));
        owned.killed = true;
    }

    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, spellInfo, damageShare, basePoints);
    if (weapon.weaponDamage)
        m_player->GetBonkPlayer()->TweakSpell(weapon.spellId).weaponDamage = std::max(1, int32(GetWeaponDamage(owned) * damageShare));

    m_player->CastCustomSpell(target, spellInfo, basePoints[0], basePoints[1], basePoints[2], true);

    if (followUp)
        return;

    uint32 const now = WorldTimer::getMSTime();
    if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
    {
        int32 const duration = m_player->GetBonkPlayer()->AdjustDuration(weapon.spellId, spellInfo->GetDuration());
        owned.carriers[target->GetObjectGuid()] = now + uint32(std::max(0, duration));
    }

    // A stacking debuff on everything it hits.
    if (weapon.behavior == BONK_BEHAVIOR_VULNERABLE && weapon.altSpellId)
        m_player->CastSpell(target, weapon.altSpellId, true);

    // A burst that follows the target and goes off a little later.
    if (weapon.behavior == BONK_BEHAVIOR_DELAYED && weapon.altSpellId)
    {
        PendingCast burst;
        burst.weaponId = weapon.id;
        burst.spellId = weapon.altSpellId;
        burst.leftMs = int32(weapon.behaviorValue);
        burst.target = target->GetObjectGuid();
        burst.x = target->GetPositionX();
        burst.y = target->GetPositionY();
        burst.z = target->GetPositionZ();
        m_pendingCasts.push_back(burst);
    }

    // A mark: the enemy takes more damage from everything for a while.
    if (weapon.behavior == BONK_BEHAVIOR_MARK)
    {
        Mark& mark = m_marks[target->GetObjectGuid()];
        mark.weaponId = weapon.id;
        mark.untilMs = now + uint32(weapon.durationMs * m_player->GetBonkPlayer()->TweakSpell(weapon.spellId).durationScale);
        if (weapon.altSpellId && !weapon.HasFlag(BONK_WEAPON_FLAG_EXPLODE_ON_DEATH))
            m_player->CastSpell(target, weapon.altSpellId, true);
    }

    // More shots at the same enemy, quickly after the first.
    if (weapon.behavior == BONK_BEHAVIOR_BURST)
        for (uint32 shot = 1; shot <= uint32(weapon.behaviorValue); ++shot)
        {
            PendingCast follow;
            follow.weaponId = weapon.id;
            follow.spellId = spellInfo->Id;
            follow.leftMs = int32(shot * BONK_BURST_INTERVAL_MS);
            follow.target = target->GetObjectGuid();
            follow.strike = true;
            follow.share = damageShare;
            m_pendingCasts.push_back(follow);
        }

    if (weapon.HasFlag(BONK_WEAPON_FLAG_KNOCKBACK))
        KnockBack(target);

    // The hero's damage over time on the enemy starts over.
    if (weapon.HasFlag(BONK_WEAPON_FLAG_REFRESH_DOTS))
        for (OwnedWeapon& other : m_weapons)
            if (&other != &owned && other.weapon.lasting && other.weapon.IsAimed() && target->HasAura(other.weapon.spellId))
                if (SpellEntry const* otherInfo = sSpellMgr.GetSpellEntry(other.weapon.spellId))
                    StrikeWith(other, otherInfo, target, 1.0f, true);
}

bool BonkRun::CanKill(BonkWeapon const& weapon, Creature const* target) const
{
    if (weapon.killBelowPct <= 0.0f || !target->IsAlive() || target->GetHealthPercent() >= weapon.killBelowPct)
        return false;
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(target->AI());
    return ai && ai->GetKind() == BONK_SPAWN_NORMAL;
}

void BonkRun::SortStrongestFirst(std::vector<Creature*>& mobs) const
{
    std::stable_sort(mobs.begin(), mobs.end(), [](Creature const* a, Creature const* b)
    {
        uint32 const rankA = GetKindRank(a);
        uint32 const rankB = GetKindRank(b);
        if (rankA != rankB)
            return rankA > rankB;
        return a->GetHealth() > b->GetHealth();
    });
}

std::vector<Creature*> BonkRun::GetMobsInCone(OwnedWeapon const& owned, float range) const
{
    std::vector<Creature*> mobs = GetMobsInRange(range);
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob) { return !m_player->IsWithinLOSInMap(mob); }), mobs.end());
    if (mobs.empty())
        return mobs;

    // Aimed at the nearest enemy, whichever way the hero faces.
    float const aim = m_player->GetAngle(mobs.front());
    float const halfArc = std::min(M_PI_F, BONK_CONE_ARC * GetWeaponArea(owned)) / 2.0f;
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
    {
        float diff = std::fabs(m_player->GetAngle(mob) - aim);
        if (diff > M_PI_F)
            diff = 2.0f * M_PI_F - diff;
        return diff > halfArc;
    }), mobs.end());
    return mobs;
}

void BonkRun::KnockBack(Creature* mob)
{
    // Bosses stand their ground; held enemies stay held.
    BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
    if (!ai || ai->GetKind() == BONK_SPAWN_BOSS || ai->GetKind() == BONK_SPAWN_MINIBOSS || !mob->IsAlive() ||
        mob->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED))
        return;

    float const angle = m_player->GetAngle(mob);
    float x = mob->GetPositionX() + BONK_KNOCKBACK_DISTANCE * std::cos(angle);
    float y = mob->GetPositionY() + BONK_KNOCKBACK_DISTANCE * std::sin(angle);
    float z = mob->GetPositionZ();
    mob->GetMap()->GetWalkHitPosition(nullptr, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), x, y, z);
    mob->GetMotionMaster()->MoveJump(x, y, z, BONK_KNOCKBACK_SPEED, BONK_KNOCKBACK_HEIGHT);
}

float BonkRun::GetMarkBonus(Unit const* victim) const
{
    if (!victim)
        return 0.0f;
    auto itr = m_marks.find(victim->GetObjectGuid());
    if (itr == m_marks.end() || int32(itr->second.untilMs - WorldTimer::getMSTime()) <= 0)
        return 0.0f;
    OwnedWeapon const* owned = FindOwned(itr->second.weaponId);
    if (!owned)
        return 0.0f;

    // A mark that deals no damage of its own grows with its damage ranks.
    float bonus = owned->weapon.behaviorValue;
    if (owned->weapon.damageEffectIndex < 0 && !owned->weapon.weaponDamage)
        bonus *= 1.0f + owned->damageBonus / 100.0f;
    return std::min(bonus, BONK_MAX_MARK);
}

void BonkRun::OnMarkedDeath(Creature const* mob)
{
    auto itr = m_marks.find(mob->GetObjectGuid());
    if (itr == m_marks.end())
        return;
    Mark const mark = itr->second;
    m_marks.erase(itr);

    OwnedWeapon* owned = FindOwned(mark.weaponId);
    if (!owned || int32(mark.untilMs - WorldTimer::getMSTime()) <= 0 || !owned->weapon.HasFlag(BONK_WEAPON_FLAG_EXPLODE_ON_DEATH))
        return;

    // It blows up where it fell...
    if (owned->weapon.altSpellId)
    {
        PendingCast blast;
        blast.weaponId = owned->weapon.id;
        blast.spellId = owned->weapon.altSpellId;
        blast.x = mob->GetPositionX();
        blast.y = mob->GetPositionY();
        blast.z = mob->GetPositionZ();
        m_pendingCasts.push_back(blast);
    }

    // ...and the mark moves on to the strongest enemy nearby without one.
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(owned->weapon.spellId);
    if (!spellInfo)
        return;
    std::vector<Creature*> mobs = GetMobsInRange(BONK_MARK_JUMP_RANGE, mob);
    SortStrongestFirst(mobs);
    for (Creature* next : mobs)
        if (GetMarkBonus(next) <= 0.0f)
        {
            FiringWeapon firing(m_player->GetBonkPlayer());
            StrikeWith(*owned, spellInfo, next, 1.0f);
            break;
        }
}

float BonkRun::GetBuffPower() const
{
    float power = 0.0f;
    uint32 const now = WorldTimer::getMSTime();
    for (auto const& itr : m_buffs)
        if (int32(itr.second - now) > 0)
            if (OwnedWeapon const* owned = FindOwned(itr.first))
                power += owned->weapon.behaviorValue * (1.0f + owned->damageBonus / 100.0f);
    return power;
}

bool BonkRun::IsReckless() const
{
    uint32 const now = WorldTimer::getMSTime();
    for (auto const& itr : m_buffs)
        if (int32(itr.second - now) > 0)
            if (OwnedWeapon const* owned = FindOwned(itr.first))
                if (owned->weapon.HasFlag(BONK_WEAPON_FLAG_ALWAYS_CRIT))
                    return true;
    return false;
}

void BonkRun::SpreadFrom(Creature const* dead)
{
    uint32 const now = WorldTimer::getMSTime();
    FiringWeapon firing(m_player->GetBonkPlayer());
    for (auto& owned : m_weapons)
    {
        BonkWeapon const& weapon = owned.weapon;
        if (weapon.behavior != BONK_BEHAVIOR_SPREAD)
            continue;

        auto carrier = owned.carriers.find(dead->GetObjectGuid());
        if (carrier == owned.carriers.end())
            continue;
        bool const carried = int32(carrier->second - now) > 0;
        owned.carriers.erase(carrier);

        // Only from BONK_EVOLVE_RANK on, or once evolved.
        if (!carried || (owned.rank < BONK_EVOLVE_RANK && !weapon.evolvesFrom))
            continue;
        SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
        if (!spellInfo)
            continue;

        uint32 const jumps = uint32(weapon.behaviorValue) + uint32(GetPassiveTotal(BONK_PASSIVE_AMOUNT)) + owned.extraJumps;
        uint32 spread = 0;
        for (Creature* next : GetMobsInRange(BONK_SPREAD_RADIUS * GetWeaponArea(owned), dead))
        {
            if (spread >= jumps)
                break;
            auto itr = owned.carriers.find(next->GetObjectGuid());
            if (itr != owned.carriers.end() && int32(itr->second - now) > 0)
                continue;
            StrikeWith(owned, spellInfo, next, 1.0f);
            ++spread;
        }
    }
}

void BonkRun::FireWeapon(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
    if (!spellInfo)
        return;
    owned.killed = false;

    // Shield spells cast nothing at enemies: the hero gets a spell Shield,
    // bigger with every damage rank.
    if (weapon.behavior == BONK_BEHAVIOR_SHIELD || weapon.behavior == BONK_BEHAVIOR_WARD)
    {
        if (m_player->IsAlive())
            AddSpellShield(uint32(m_player->GetMaxHealth() * weapon.behaviorValue / 100.0f * (1.0f + owned.damageBonus / 100.0f)));
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    // What the weapon's level and the passives do to its spell (Spell reads
    // these): a bigger area, more jumps, a longer or shorter duration.
    float const areaScale = GetWeaponArea(owned);
    for (uint32 spellId : { weapon.spellId, weapon.altSpellId })
    {
        if (!spellId)
            continue;
        BonkPlayer::SpellTweak& tweak = m_player->GetBonkPlayer()->TweakSpell(spellId);
        tweak.areaScale = areaScale;
        tweak.chainBonus = weapon.chains ? owned.extraJumps : 0;
        tweak.durationMs = weapon.durationMs;
        tweak.durationScale = std::max(0.1f, 1.0f + (GetPassiveTotal(BONK_PASSIVE_DURATION) + owned.durationBonus) / 100.0f);
        tweak.weaponDamage = weapon.weaponDamage ? std::max(1, int32(GetWeaponDamage(owned))) : 0;
        tweak.tickMs = weapon.tickMs;
        tweak.tickScale = 1.0f / GetHasteRate();
    }
    BonkPlayer::SpellTweak& tweak = m_player->GetBonkPlayer()->TweakSpell(weapon.spellId);
    uint32 const now = WorldTimer::getMSTime();

    // A shout on the hero: more Power while it lasts.
    if (weapon.behavior == BONK_BEHAVIOR_BUFF)
    {
        if (m_player->IsAlive())
        {
            FiringWeapon firing(m_player->GetBonkPlayer());
            m_player->CastSpell(m_player, spellInfo, true);
            m_buffs[weapon.id] = now + uint32(weapon.durationMs * tweak.durationScale);
        }
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    // Beasts are called, brought back and kept up to strength.
    if (weapon.behavior == BONK_BEHAVIOR_PET)
    {
        UpdatePets(owned);
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    // Traps at the hero's feet, once enemies are about.
    if (weapon.behavior == BONK_BEHAVIOR_TRAP && weapon.target == BONK_TARGET_SELF)
    {
        if (GetMobsInRange(BONK_TRAP_AWARE_RANGE).empty())
        {
            owned.timer = 250;
            return;
        }
        DropTraps(owned);
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    // Revenge answers a dodge, and only a dodge.
    if (weapon.behavior == BONK_BEHAVIOR_REVENGE && !m_dodged)
    {
        owned.timer = 100;
        return;
    }

    // Marks that ran out are forgotten.
    if (weapon.behavior == BONK_BEHAVIOR_MARK)
        for (auto itr = m_marks.begin(); itr != m_marks.end();)
        {
            if (int32(itr->second.untilMs - now) <= 0)
                itr = m_marks.erase(itr);
            else
                ++itr;
        }

    // Area weapons, cones and melee reach grow; a ground weapon's range is
    // where it can aim, not its area.
    bool const reachGrows = weapon.target == BONK_TARGET_SELF_AREA || weapon.target == BONK_TARGET_CONE || weapon.behavior == BONK_BEHAVIOR_CLEAVE;
    float const range = weapon.range * (reachGrows ? areaScale : 1.0f + owned.rangeBonus / 100.0f);
    std::vector<Creature*> mobs = GetMobsInRange(range);

    // Damage over time wants a fresh enemy each cast: casting again on one
    // that has it restarts its ticks, so with few enemies around they never
    // came.
    if (weapon.target == BONK_TARGET_RANDOM)
    {
        for (auto itr = owned.recentTargets.begin(); itr != owned.recentTargets.end();)
        {
            if (now - itr->second > BONK_DOT_RETARGET_MS)
                itr = owned.recentTargets.erase(itr);
            else
                ++itr;
        }
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
        {
            return mob->HasAura(weapon.spellId) || owned.recentTargets.count(mob->GetObjectGuid());
        }), mobs.end());
    }
    else if (weapon.target == BONK_TARGET_STRONGEST)
        SortStrongestFirst(mobs);
    else if (weapon.target == BONK_TARGET_FARTHEST)
        std::reverse(mobs.begin(), mobs.end());

    // A killing shot goes for the enemies it can finish first.
    if (weapon.killBelowPct > 0.0f)
        std::stable_partition(mobs.begin(), mobs.end(), [&](Creature* mob) { return CanKill(weapon, mob); });

    if (mobs.empty())
    {
        owned.timer = 250;      // look again soon
        return;
    }
    if (weapon.behavior == BONK_BEHAVIOR_REVENGE)
        m_dodged = false;

    // An orb sets off towards where the hero faces and does the casting.
    if (weapon.behavior == BONK_BEHAVIOR_ORB)
    {
        LaunchOrb(owned);
        owned.timer = GetWeaponCooldown(owned);
        return;
    }

    // Every altEvery-th cast is the alt spell, for altDamage times the damage.
    ++owned.casts;
    SpellEntry const* castInfo = spellInfo;
    float castShare = 1.0f;
    if (weapon.altEvery && weapon.altSpellId && owned.casts % weapon.altEvery == 0)
        if (SpellEntry const* altInfo = sSpellMgr.GetSpellEntry(weapon.altSpellId))
        {
            castInfo = altInfo;
            castShare = weapon.altDamage;
        }

    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, castInfo, castShare, basePoints);

    FiringWeapon firing(m_player->GetBonkPlayer());
    if (weapon.target == BONK_TARGET_SELF_AREA)
    {
        CastSelfArea(owned, castInfo, areaScale);

        // A second pulse, wider.
        if (weapon.behavior == BONK_BEHAVIOR_ECHO)
        {
            PendingCast echo;
            echo.weaponId = weapon.id;
            echo.spellId = castInfo->Id;
            echo.leftMs = int32(weapon.behaviorValue);
            echo.echo = true;
            m_pendingCasts.push_back(echo);
        }

        // A spin: the same pulse again and again while it lasts, wherever
        // the hero has got to.
        if (weapon.behavior == BONK_BEHAVIOR_SPIN && weapon.behaviorValue >= 100.0f)
        {
            uint32 const every = uint32(weapon.behaviorValue);
            uint32 const length = uint32(weapon.durationMs * tweak.durationScale);
            for (uint32 at = every; at <= length; at += every)
            {
                PendingCast pulse;
                pulse.weaponId = weapon.id;
                pulse.spellId = castInfo->Id;
                pulse.leftMs = int32(at);
                pulse.selfArea = true;
                m_pendingCasts.push_back(pulse);
            }
        }

        // Something more goes off around the hero a moment later.
        if (weapon.behavior == BONK_BEHAVIOR_DELAYED && weapon.altSpellId)
        {
            PendingCast wave;
            wave.weaponId = weapon.id;
            wave.spellId = weapon.altSpellId;
            wave.leftMs = int32(weapon.behaviorValue);
            wave.selfArea = true;
            m_pendingCasts.push_back(wave);
        }

        // A spell Shield for every enemy in reach.
        if (weapon.behavior == BONK_BEHAVIOR_SHIELD_PER_HIT && m_player->IsAlive())
            AddSpellShield(uint32(m_player->GetMaxHealth() * weapon.behaviorValue / 100.0f * mobs.size()));

        // The enemies in front of the hero freeze.
        if (weapon.behavior == BONK_BEHAVIOR_FREEZE && weapon.altSpellId)
            for (Creature* mob : mobs)
                if (m_player->HasInArc(mob, BONK_FREEZE_ARC))
                    m_player->CastSpell(mob, weapon.altSpellId, true);
    }
    else if (weapon.target == BONK_TARGET_CONE)
    {
        std::vector<Creature*> const cone = GetMobsInCone(owned, range);
        for (Creature* mob : cone)
            StrikeWith(owned, castInfo, mob, castShare);
        if (cone.empty())
        {
            owned.timer = 250;
            return;
        }
    }
    else if (weapon.target == BONK_TARGET_GROUND)
    {
        // Triggered casts skip the line of sight check, so do it here.
        mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [&](Creature* mob)
        {
            return !m_player->IsWithinLOSInMap(mob);
        }), mobs.end());

        uint32 const drops = 1 + owned.extraTargets + uint32(GetPassiveTotal(BONK_PASSIVE_AMOUNT));
        std::vector<Creature*> const spots = PickGroundSpots(mobs, weapon.groundRadius * areaScale, drops);
        if (spots.empty())
        {
            owned.timer = 250;
            return;
        }
        for (Creature* spot : spots)
        {
            CastAtGround(m_player, castInfo, basePoints, spot->GetPositionX(), spot->GetPositionY(), spot->GetPositionZ());

            // Something more lands on the same spot a little later.
            if (weapon.behavior == BONK_BEHAVIOR_DELAYED && weapon.altSpellId)
            {
                PendingCast burst;
                burst.weaponId = weapon.id;
                burst.spellId = weapon.altSpellId;
                burst.leftMs = int32(weapon.behaviorValue);
                burst.x = spot->GetPositionX();
                burst.y = spot->GetPositionY();
                burst.z = spot->GetPositionZ();
                m_pendingCasts.push_back(burst);
            }
        }
    }
    else
    {
        if (weapon.target == BONK_TARGET_RANDOM)
            for (size_t i = mobs.size(); i > 1; --i)
                std::swap(mobs[i - 1], mobs[urand(0, uint32(i - 1))]);

        // Spreading spells forget enemies they have worn off.
        if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
            for (auto itr = owned.carriers.begin(); itr != owned.carriers.end();)
            {
                if (int32(itr->second - now) <= 0)
                    itr = owned.carriers.erase(itr);
                else
                    ++itr;
            }

        // Triggered casts skip the line of sight check, so do it here.
        uint32 const targets = GetWeaponTargets(owned);
        std::vector<Creature*> struck;
        for (Creature* mob : mobs)
        {
            if (struck.size() >= targets)
                break;
            if (!m_player->IsWithinLOSInMap(mob))
                continue;
            StrikeWith(owned, castInfo, mob, castShare);
            if (weapon.target == BONK_TARGET_RANDOM)
                owned.recentTargets[mob->GetObjectGuid()] = now;
            struck.push_back(mob);
        }
        uint32 const fired = uint32(struck.size());

        // A cleave also hits the enemies next to each target, for a share
        // of the damage; an aimed spell's pierces and bounces hit them in
        // full. Every enemy is hit once.
        bool const cleave = weapon.behavior == BONK_BEHAVIOR_CLEAVE;
        bool const pierce = weapon.IsAimed() && !weapon.chains && weapon.behavior != BONK_BEHAVIOR_SPREAD;
        uint32 const victims = cleave ? 1 + weapon.baseJumps + owned.extraJumps : pierce ? weapon.baseJumps + owned.extraJumps : 0;
        if (victims)
        {
            float const share = cleave ? weapon.behaviorValue : 1.0f;
            float const radius = BONK_CLEAVE_RADIUS * (cleave ? areaScale : 1.0f);
            for (size_t i = 0; i < fired; ++i)
            {
                uint32 hit = 0;
                for (Creature* next : GetMobsInRange(radius, struck[i]))
                {
                    if (hit >= victims)
                        break;
                    if (std::find(struck.begin(), struck.end(), next) != struck.end())
                        continue;
                    StrikeWith(owned, castInfo, next, share * castShare);
                    if (weapon.target == BONK_TARGET_RANDOM)
                        owned.recentTargets[next->GetObjectGuid()] = now;
                    struck.push_back(next);
                    ++hit;
                }
            }
            // The next swing hits for full damage again.
            if (weapon.weaponDamage)
                tweak.weaponDamage = std::max(1, int32(GetWeaponDamage(owned)));
        }

        if (!fired)
        {
            owned.timer = 250;
            return;
        }
    }

    // A share of max health, so it keeps pace with the player's level.
    if (weapon.healPct > 0.0f && m_player->IsAlive())
        m_player->DealHeal(m_player, ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * weapon.healPct / 100.0f))), spellInfo);

    owned.timer = GetWeaponCooldown(owned);

    // Class Mastery: now and then a spell is ready again at once; a killing
    // shot that resets is too.
    if ((m_mastery.clearcasting > 0.0f && roll_chance_f(m_mastery.clearcasting)) ||
        (owned.killed && weapon.HasFlag(BONK_WEAPON_FLAG_RESET_ON_KILL)))
        owned.timer = std::min<int32>(owned.timer, BONK_CLEARCAST_DELAY_MS);
}

bool BonkRun::HasBehavior(BonkWeaponBehavior behavior) const
{
    return std::any_of(m_weapons.begin(), m_weapons.end(), [behavior](OwnedWeapon const& owned) { return owned.weapon.behavior == behavior; });
}

void BonkRun::CastSelfArea(OwnedWeapon& owned, SpellEntry const* spellInfo, float areaScale)
{
    float const share = spellInfo->Id == owned.weapon.spellId ? 1.0f : owned.weapon.altDamage;
    FiringWeapon firing(m_player->GetBonkPlayer());

    // A spell for one enemy (Revenge): every enemy in reach.
    if (spellInfo->EffectImplicitTargetA[EFFECT_INDEX_0] == TARGET_UNIT_ENEMY)
    {
        for (Creature* mob : GetMobsInRange(owned.weapon.range * areaScale))
            if (m_player->IsWithinLOSInMap(mob))
                StrikeWith(owned, spellInfo, mob, share);
        return;
    }

    m_player->GetBonkPlayer()->TweakSpell(spellInfo->Id).areaScale = areaScale;
    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, spellInfo, share, basePoints);
    m_player->CastCustomSpell(m_player, spellInfo, basePoints[0], basePoints[1], basePoints[2], true);
}

void BonkRun::AddSpellShield(uint32 amount)
{
    uint32 const cap = uint32(m_player->GetMaxHealth() * BONK_SPELL_SHIELD_CAP);
    m_spellShield = std::min(m_spellShield + amount, cap);
}

void BonkRun::ScaleRadius(SpellEntry const* spellInfo, float radius)
{
    float const own = GetSpellRadiusOf(spellInfo);
    m_player->GetBonkPlayer()->TweakSpell(spellInfo->Id).areaScale = own > 0.0f ? radius / own : 1.0f;
}

void BonkRun::UpdatePendingCasts(uint32 diff)
{
    // A ward's Shield broke: its blast goes off around the hero.
    if (m_wardBurst)
    {
        m_wardBurst = false;
        for (OwnedWeapon& owned : m_weapons)
            if (owned.weapon.behavior == BONK_BEHAVIOR_WARD && owned.weapon.altSpellId)
                if (SpellEntry const* info = sSpellMgr.GetSpellEntry(owned.weapon.altSpellId))
                    CastSelfArea(owned, info, GetWeaponArea(owned));
    }

    Map* map = m_player->GetMap();

    // Retaliation: every enemy that hit the hero is struck back.
    if (!m_retaliations.empty())
    {
        std::vector<ObjectGuid> const attackers = std::move(m_retaliations);
        m_retaliations.clear();
        for (OwnedWeapon& owned : m_weapons)
        {
            if (owned.weapon.behavior != BONK_BEHAVIOR_REVENGE || !m_player->IsAlive())
                continue;
            SpellEntry const* info = sSpellMgr.GetSpellEntry(owned.weapon.spellId);
            if (!info)
                continue;
            FiringWeapon firing(m_player->GetBonkPlayer());
            for (ObjectGuid const& guid : attackers)
                if (Creature* mob = map->GetCreature(guid))
                    if (mob->IsAlive())
                        StrikeWith(owned, info, mob, 1.0f, true);
        }
    }

    if (m_pendingCasts.empty())
        return;

    std::vector<PendingCast> due;
    for (auto itr = m_pendingCasts.begin(); itr != m_pendingCasts.end();)
    {
        itr->leftMs -= int32(diff);
        if (itr->leftMs > 0)
        {
            ++itr;
            continue;
        }
        due.push_back(*itr);
        itr = m_pendingCasts.erase(itr);
    }

    for (PendingCast const& cast : due)
    {
        OwnedWeapon* owned = FindOwned(cast.weaponId);
        SpellEntry const* info = sSpellMgr.GetSpellEntry(cast.spellId);
        if (!owned || !info || !m_player->IsAlive())
            continue;

        // An echo: the same pulse again, half again as wide.
        if (cast.echo)
        {
            CastSelfArea(*owned, info, GetWeaponArea(*owned) * BONK_ECHO_AREA);
            continue;
        }

        // Around the hero again: a spin's next pulse, or a wave half again
        // as wide as the spell that sent it.
        if (cast.selfArea)
        {
            float scale = GetWeaponArea(*owned);
            if (info->Id != owned->weapon.spellId)
                if (float const own = GetSpellRadiusOf(info))
                    scale *= owned->weapon.range * BONK_ECHO_AREA / own;
            CastSelfArea(*owned, info, scale);
            continue;
        }

        // A burst's later shot: at the same enemy, or the nearest if it fell.
        if (cast.strike)
        {
            Creature* target = map->GetCreature(cast.target);
            if (!target || !target->IsAlive())
            {
                std::vector<Creature*> const mobs = GetMobsInRange(owned->weapon.range * (1.0f + owned->rangeBonus / 100.0f));
                target = mobs.empty() ? nullptr : mobs.front();
            }
            if (target)
            {
                FiringWeapon firing(m_player->GetBonkPlayer());
                StrikeWith(*owned, info, target, cast.share, true);
            }
            continue;
        }

        // A burst follows its target while it lives.
        float x = cast.x, y = cast.y, z = cast.z;
        if (Creature* target = map->GetCreature(cast.target))
            if (target->IsAlive())
            {
                x = target->GetPositionX();
                y = target->GetPositionY();
                z = target->GetPositionZ();
            }

        FiringWeapon firing(m_player->GetBonkPlayer());
        if (info->EffectImplicitTargetA[EFFECT_INDEX_0] == TARGET_UNIT_ENEMY)
        {
            // A spell for one enemy (a root): every enemy around the spot.
            float const radius = std::max(BONK_CLEAVE_RADIUS, owned->weapon.groundRadius) * GetWeaponArea(*owned);
            for (ObjectGuid const& guid : m_mobs)
                if (Creature* mob = map->GetCreature(guid))
                    if (mob->IsAlive() && mob->GetDistance2d(x, y) <= radius)
                        m_player->CastSpell(mob, info, true);
        }
        else
        {
            optional<int32> basePoints[MAX_EFFECT_INDEX];
            GetBasePoints(*owned, info, owned->weapon.altDamage, basePoints);
            m_player->GetBonkPlayer()->TweakSpell(info->Id).areaScale = GetWeaponArea(*owned);
            CastAtGround(m_player, info, basePoints, x, y, z);
        }
    }
}

void BonkRun::LaunchOrb(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    float const angle = m_player->GetOrientation();
    float const x = m_player->GetPositionX();
    float const y = m_player->GetPositionY();
    float const z = m_player->GetPositionZ();
    uint32 const lifetime = std::max(1000u, weapon.durationMs);

    Creature* orb = m_player->SummonCreature(BONK_ORB_SMALL, x, y, z + 1.0f, angle, TEMPSUMMON_TIMED_DESPAWN, lifetime + 2000, false, 0, &InstallOrbAI);
    if (!orb)
        return;
    orb->SetFactionTemplateId(BONK_ORB_FACTION);
    orb->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
    orb->SetObjectScale(BONK_WEAPON_ORB_SCALE);
    orb->AddAura(BONK_SPELL_WEAPON_ORB);
    orb->SetSpeedRate(MOVE_RUN, BONK_WEAPON_ORB_SPEED);

    // Slowly ahead, as far as it gets before it fades.
    float const distance = BONK_WEAPON_ORB_SPEED * BONK_PLAYER_RUN_SPEED * lifetime / float(IN_MILLISECONDS);
    float dx = x + distance * std::cos(angle);
    float dy = y + distance * std::sin(angle);
    float dz = z;
    m_player->GetMap()->GetWalkHitPosition(nullptr, x, y, z, dx, dy, dz);
    orb->GetMotionMaster()->MovePoint(0, dx, dy, dz, MOVE_RUN_MODE);

    WeaponOrb launched;
    launched.creature = orb->GetObjectGuid();
    launched.weaponId = weapon.id;
    launched.leftMs = int32(lifetime);
    launched.tickMs = 0;
    m_weaponOrbs.push_back(launched);
}

void BonkRun::UpdateWeaponOrbs(uint32 diff)
{
    if (m_weaponOrbs.empty())
        return;

    Map* map = m_player->GetMap();
    for (auto itr = m_weaponOrbs.begin(); itr != m_weaponOrbs.end();)
    {
        itr->leftMs -= int32(diff);
        Creature* orb = map->GetCreature(itr->creature);
        OwnedWeapon* owned = FindOwned(itr->weaponId);
        if (!orb || !owned || itr->leftMs <= 0)
        {
            if (orb)
                orb->DespawnOrUnsummon();
            itr = m_weaponOrbs.erase(itr);
            continue;
        }

        itr->tickMs -= int32(diff);
        if (itr->tickMs <= 0)
        {
            BonkWeapon const& weapon = owned->weapon;
            itr->tickMs = int32(std::max(150.0f, std::max(200u, weapon.tickMs) / GetHasteRate()));
            if (SpellEntry const* info = sSpellMgr.GetSpellEntry(weapon.spellId))
            {
                // The orb's casts come from the hero, at the enemies around the orb.
                uint32 const targets = GetWeaponTargets(*owned);
                uint32 struck = 0;
                FiringWeapon firing(m_player->GetBonkPlayer());
                for (Creature* mob : GetMobsInRange(weapon.range * GetWeaponArea(*owned), orb))
                {
                    if (struck >= targets)
                        break;
                    StrikeWith(*owned, info, mob, 1.0f);
                    ++struck;
                }
            }
        }
        ++itr;
    }
}

void BonkRun::DespawnWeaponOrbs()
{
    if (Map* map = m_player->FindMap())
        for (WeaponOrb const& orb : m_weaponOrbs)
            if (Creature* creature = map->GetCreature(orb.creature))
                creature->DespawnOrUnsummon();
    m_weaponOrbs.clear();
}

float BonkRun::GetMobDamageMultiplier(Creature const* mob) const
{
    // The strongest shout the enemy hears counts.
    float reduction = 0.0f;
    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.behavior == BONK_BEHAVIOR_DEMORALIZE && mob->HasAura(owned.weapon.spellId))
            reduction = std::max(reduction, owned.weapon.behaviorValue * (1.0f + owned.damageBonus / 100.0f));
    return 1.0f - std::min(reduction, BONK_MAX_DEMORALIZE) / 100.0f;
}

void BonkRun::AddTelegraph(BonkTelegraphShape shape, float x, float y, float z, float orientation, float size, uint32 durationMs,
    std::function<void()> onFire)
{
    if (m_state != STATE_RUNNING || m_paused)
        return;
    m_telegraphs.Add(m_player->GetMap(), shape, x, y, z, orientation, size, durationMs, std::move(onFire));
}

void BonkRun::Erupt(ObjectGuid mobGuid, float x, float y, float z, float radius, uint32 damage)
{
    if (Creature* marker = m_player->SummonCreature(BONK_PICKUP_MARKER, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, BONK_ERUPTION_LOOK_MS,
        false, 0, &InstallOrbAI))
    {
        marker->SetFactionTemplateId(BONK_ORB_FACTION);
        marker->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        marker->CastSpell(marker, BONK_SPELL_ERUPTION_LOOK, true);
    }

    // The enemy's own spell, so Armor, the Shield and its affixes count.
    Creature* mob = m_player->GetMap()->GetCreature(mobGuid);
    if (!mob || !mob->IsAlive() || !m_player->IsAlive() || m_player->GetDistance2d(x, y) > radius)
        return;
    mob->CastCustomSpell(m_player, BONK_SPELL_ERUPTION_HIT, int32(std::max(1u, damage)), {}, {}, true);
}

ObjectGuid BonkRun::PlaceBoltTarget(float x, float y, float z, uint32 lifetimeMs)
{
    Creature* target = m_player->SummonCreature(BONK_PICKUP_MARKER, x, y, z, 0.0f, TEMPSUMMON_TIMED_DESPAWN, lifetimeMs, false, 0, &InstallOrbAI);
    if (!target)
        return ObjectGuid();

    // On the hero's side, so enemies may cast at it (a target they cannot
    // select would stop the cast) and the hero's spells leave it be; it
    // soaks the bolt.
    target->SetFactionTemplateId(m_player->GetFactionTemplateId());
    target->SetMaxHealth(1000000);
    target->SetHealth(1000000);
    return target->GetObjectGuid();
}

void BonkRun::LandBolt(ObjectGuid mobGuid, ObjectGuid targetGuid, uint32 spellId, float x, float y, float radius, uint32 damage)
{
    Map* map = m_player->GetMap();
    if (Creature* target = map->GetCreature(targetGuid))
        target->DespawnOrUnsummon(500);

    Creature* mob = map->GetCreature(mobGuid);
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(spellId);
    if (!mob || !mob->IsAlive() || !spellInfo || !m_player->IsAlive() || m_player->GetDistance2d(x, y) > radius)
        return;

    // As a spell hit: the enemy's damage hook (affixes, Armor, the Shield),
    // the combat log, then the damage.
    SpellNonMeleeDamage hit(mob, m_player, spellId, SpellSchools(spellInfo->School));
    hit.damage = std::max(1u, damage);
    mob->DealDamageMods(m_player, hit.damage, &hit.absorb);
    mob->SendSpellNonMeleeDamageLog(&hit);
    mob->DealSpellDamage(&hit, false);
}

void BonkRun::OnHeroDodge()
{
    if (m_state != STATE_RUNNING)
        return;
    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.behavior == BONK_BEHAVIOR_REVENGE)
        {
            m_dodged = true;
            // Retaliation: for a while, every hit taken strikes back.
            if (owned.weapon.durationMs)
            {
                float const scale = std::max(0.1f, 1.0f + (GetPassiveTotal(BONK_PASSIVE_DURATION) + owned.durationBonus) / 100.0f);
                m_retaliateUntil = WorldTimer::getMSTime() + uint32(owned.weapon.durationMs * scale);
            }
        }
}

void BonkRun::OnHeroStruck(Creature* attacker)
{
    if (m_state != STATE_RUNNING || int32(m_retaliateUntil - WorldTimer::getMSTime()) <= 0)
        return;
    if (std::find(m_retaliations.begin(), m_retaliations.end(), attacker->GetObjectGuid()) == m_retaliations.end())
        m_retaliations.push_back(attacker->GetObjectGuid());
}

void BonkRun::OnPeriodicDamage(uint32 spellId, uint32 damage)
{
    if (m_state != STATE_RUNNING)
        return;
    for (OwnedWeapon const& owned : m_weapons)
        if (owned.weapon.behavior == BONK_BEHAVIOR_LEECH && owned.weapon.spellId == spellId)
            m_bleedHeal += damage * owned.weapon.behaviorValue / 100.0f;
}

void BonkRun::DropTraps(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    uint32 const count = 1 + owned.extraTargets + uint32(GetPassiveTotal(BONK_PASSIVE_AMOUNT));
    float const startAngle = frand(0.0f, 2.0f * M_PI_F);
    for (uint32 i = 0; i < count; ++i)
    {
        // The first at the hero's feet, the rest in a ring around them.
        float x = m_player->GetPositionX();
        float y = m_player->GetPositionY();
        float z = m_player->GetPositionZ();
        if (i)
        {
            float const angle = startAngle + 2.0f * M_PI_F * i / count;
            float dx = x + BONK_TRAP_SPACING * std::cos(angle);
            float dy = y + BONK_TRAP_SPACING * std::sin(angle);
            float dz = z;
            m_player->GetMap()->GetWalkHitPosition(nullptr, x, y, z, dx, dy, dz);
            x = dx;
            y = dy;
            z = dz;
        }

        // The oldest goes once a spell has too many out.
        size_t out = 0;
        for (Trap const& trap : m_traps)
            if (trap.weaponId == weapon.id)
                ++out;
        if (out >= BONK_MAX_TRAPS)
            for (auto itr = m_traps.begin(); itr != m_traps.end(); ++itr)
                if (itr->weaponId == weapon.id)
                {
                    if (GameObject* go = m_player->GetMap()->GetGameObject(itr->object))
                        go->Delete();
                    m_traps.erase(itr);
                    break;
                }

        Trap trap;
        trap.weaponId = weapon.id;
        trap.x = x;
        trap.y = y;
        trap.z = z;
        trap.armMs = BONK_TRAP_ARM_MS;
        trap.leftMs = BONK_TRAP_LIFETIME_MS;
        if (GameObject* go = PlaceObject(uint32(weapon.behaviorValue), x, y, z, BONK_TRAP_LIFETIME_MS / IN_MILLISECONDS + 5))
            trap.object = go->GetObjectGuid();
        m_traps.push_back(trap);
    }
}

void BonkRun::UpdateTraps(uint32 diff)
{
    m_trapTimer += diff;
    if (m_trapTimer < BONK_TRAP_CHECK_MS || m_traps.empty())
        return;
    int32 const elapsed = int32(m_trapTimer);
    m_trapTimer = 0;

    Map* map = m_player->GetMap();
    std::vector<Trap> sprung;
    for (auto itr = m_traps.begin(); itr != m_traps.end();)
    {
        itr->armMs -= elapsed;
        itr->leftMs -= elapsed;
        bool remove = itr->leftMs <= 0 || !FindOwned(itr->weaponId);
        if (!remove && itr->armMs <= 0)
            for (ObjectGuid const& guid : m_mobs)
                if (Creature* mob = map->GetCreature(guid))
                    if (mob->IsAlive() && mob->GetDistance2d(itr->x, itr->y) <= BONK_TRAP_TRIGGER_RADIUS)
                    {
                        sprung.push_back(*itr);
                        remove = true;
                        break;
                    }
        if (!remove)
        {
            ++itr;
            continue;
        }
        if (GameObject* go = map->GetGameObject(itr->object))
            go->Delete();
        itr = m_traps.erase(itr);
    }

    for (Trap const& trap : sprung)
        if (OwnedWeapon* owned = FindOwned(trap.weaponId))
            SpringTrap(*owned, trap.x, trap.y, trap.z);
}

void BonkRun::SpringTrap(OwnedWeapon& owned, float x, float y, float z)
{
    BonkWeapon const& weapon = owned.weapon;
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(weapon.spellId);
    if (!spellInfo || !m_player->IsAlive())
        return;

    FiringWeapon firing(m_player->GetBonkPlayer());
    float const radius = weapon.range * GetWeaponArea(owned);
    ScaleRadius(spellInfo, radius);
    optional<int32> basePoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, spellInfo, 1.0f, basePoints);
    CastAtGround(m_player, spellInfo, basePoints, x, y, z);

    // A second effect where it went off: a patch of ice, or a hold on every
    // enemy in reach.
    SpellEntry const* altInfo = weapon.altSpellId ? sSpellMgr.GetSpellEntry(weapon.altSpellId) : nullptr;
    if (!altInfo)
        return;
    if (altInfo->EffectImplicitTargetA[EFFECT_INDEX_0] == TARGET_UNIT_ENEMY)
    {
        Map* map = m_player->GetMap();
        for (ObjectGuid const& guid : m_mobs)
            if (Creature* mob = map->GetCreature(guid))
                if (mob->IsAlive() && mob->GetDistance2d(x, y) <= radius)
                    m_player->CastSpell(mob, altInfo, true);
        return;
    }
    ScaleRadius(altInfo, radius);
    optional<int32> altPoints[MAX_EFFECT_INDEX];
    GetBasePoints(owned, altInfo, weapon.altDamage, altPoints);
    CastAtGround(m_player, altInfo, altPoints, x, y, z);
}

void BonkRun::DespawnTraps()
{
    if (Map* map = m_player->FindMap())
        for (Trap const& trap : m_traps)
            if (GameObject* go = map->GetGameObject(trap.object))
                go->Delete();
    m_traps.clear();
}

void BonkRun::UpdatePets(OwnedWeapon& owned)
{
    BonkWeapon const& weapon = owned.weapon;
    Map* map = m_player->GetMap();
    owned.pets.erase(std::remove_if(owned.pets.begin(), owned.pets.end(), [map](ObjectGuid const& guid)
    {
        Creature* pet = map->GetCreature(guid);
        return !pet || !pet->IsAlive();
    }), owned.pets.end());

    // One more each call until there are enough, so a lost one comes back.
    uint32 const wanted = GetWeaponTargets(owned);
    if (owned.pets.size() < wanted && m_player->IsAlive())
    {
        float const angle = frand(0.0f, 2.0f * M_PI_F);
        float x = m_player->GetPositionX() + 2.0f * std::cos(angle);
        float y = m_player->GetPositionY() + 2.0f * std::sin(angle);
        float z = m_player->GetPositionZ();
        m_player->GetMap()->GetWalkHitPosition(nullptr, m_player->GetPositionX(), m_player->GetPositionY(), m_player->GetPositionZ(), x, y, z);
        if (Creature* pet = m_player->SummonCreature(BONK_PET_ENTRY, x, y, z, m_player->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN,
            BONK_OBJECT_LIFETIME_SEC * IN_MILLISECONDS, false, 0, &InstallPetAI))
        {
            // The hero's: their crit and Power, and run enemies ignore it.
            pet->SetOwnerGuid(m_player->GetObjectGuid());
            // Its hits count for the weapon on the results screen.
            pet->SetUInt32Value(UNIT_CREATED_BY_SPELL, weapon.spellId);
            pet->SetFactionTemplateId(m_player->GetFactionTemplateId());
            pet->SetLevel(m_player->GetLevel());
            pet->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            pet->SetBaseSpeedRate(MOVE_RUN, BONK_PET_SPEED);
            pet->GetMotionMaster()->MoveFollow(m_player, 2.0f, angle);
            owned.pets.push_back(pet->GetObjectGuid());
        }
    }
    while (owned.pets.size() > wanted)
    {
        if (Creature* pet = map->GetCreature(owned.pets.back()))
            pet->DespawnOrUnsummon();
        owned.pets.pop_back();
    }

    // Their bite is the spell's damage; Haste and faster ranks make them
    // bite more often, the evolution makes them bigger and frenzied.
    float const damage = GetWeaponDamage(owned);
    float const pace = GetWeaponCooldown(owned) / float(std::max(1u, weapon.cooldownMs));
    uint32 const swing = std::max(300u, uint32(std::max(500u, weapon.tickMs) * pace));
    float const scale = weapon.behaviorValue > 0.0f ? weapon.behaviorValue : 1.0f;
    for (ObjectGuid const& guid : owned.pets)
        if (Creature* pet = map->GetCreature(guid))
        {
            pet->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, damage * 0.9f);
            pet->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, damage * 1.1f);
            pet->UpdateDamagePhysical(BASE_ATTACK);
            pet->SetAttackTime(BASE_ATTACK, swing);
            if (pet->GetObjectScale() != scale)
                pet->SetObjectScale(scale);
            if (weapon.altSpellId && !pet->HasAura(weapon.altSpellId))
                pet->AddAura(weapon.altSpellId, ADD_AURA_PERMANENT);
        }
}

void BonkRun::UpdatePetTargets(uint32 diff)
{
    m_petTimer += diff;
    if (m_petTimer < BONK_PET_CHECK_MS)
        return;
    m_petTimer = 0;

    Map* map = m_player->GetMap();
    for (OwnedWeapon& owned : m_weapons)
        for (ObjectGuid const& guid : owned.pets)
        {
            Creature* pet = map->GetCreature(guid);
            if (!pet || !pet->IsAlive())
                continue;

            // Too far behind: back to the hero's side.
            if (!pet->IsWithinDistInMap(m_player, BONK_PET_LEASH_RANGE))
            {
                pet->AttackStop();
                pet->NearTeleportTo(m_player->GetPositionX(), m_player->GetPositionY(), m_player->GetPositionZ(), m_player->GetOrientation());
                continue;
            }

            // Its prey stays its prey while it lives and stays near the hero.
            Unit* victim = pet->GetVictim();
            if (victim && victim->IsAlive() && victim->IsWithinDistInMap(m_player, BONK_PET_HUNT_RANGE))
                continue;

            // Else the enemy nearest the hero.
            std::vector<Creature*> const prey = GetMobsInRange(BONK_PET_HUNT_RANGE);
            if (!prey.empty())
            {
                pet->AI()->AttackStart(prey.front());
                continue;
            }
            // Nothing to hunt: at the hero's heel.
            if (victim || pet->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            {
                pet->AttackStop();
                pet->GetMotionMaster()->Clear(false);
                pet->GetMotionMaster()->MoveFollow(m_player, 2.0f, frand(0.0f, 2.0f * M_PI_F));
            }
        }
}

void BonkRun::DespawnPets()
{
    Map* map = m_player->FindMap();
    for (OwnedWeapon& owned : m_weapons)
    {
        if (map)
            for (ObjectGuid const& guid : owned.pets)
                if (Creature* pet = map->GetCreature(guid))
                    pet->DespawnOrUnsummon();
        owned.pets.clear();
    }
}

void BonkRun::OnWeaponDamage(uint32 damage)
{
    if (m_state != STATE_RUNNING || !m_player->IsAlive())
        return;

    float const percent = GetPassiveTotal(BONK_PASSIVE_LEECH);
    if (percent <= 0.0f)
        return;

    // Gathered here and handed out once a second (UpdateRecovery), never
    // more than the cap: hitting a whole swarm at once must not make the
    // hero unkillable.
    float const cap = m_player->GetMaxHealth() * percent * BONK_LEECH_CAP_PER_PERCENT / 100.0f;
    m_leechCarry = std::min(cap, m_leechCarry + damage * percent / 100.0f);
}

void BonkRun::AddWeapon(BonkWeapon const& weapon)
{
    OwnedWeapon owned;
    owned.weapon = weapon;
    owned.timer = 500;
    m_weapons.push_back(owned);
    // Revenge brings its own Dodge.
    if (weapon.behavior == BONK_BEHAVIOR_REVENGE)
        ApplyPassives();
}

void BonkRun::ApplyPassives()
{
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    if (int32 const speed = int32(GetPassiveTotal(BONK_PASSIVE_MOVE_SPEED)))
        m_player->CastCustomSpell(m_player, BONK_SPELL_MOVE_SPEED, speed, {}, {}, true);

    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    if (int32 const health = int32(GetPassiveTotal(BONK_PASSIVE_MAX_HEALTH)))
        m_player->CastCustomSpell(m_player, BONK_SPELL_MAX_HEALTH, health, {}, {}, true);

    // A real dodge chance, so enemy swings show "Dodge".
    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);
    if (int32 const dodge = int32(GetStat(BONK_PASSIVE_DODGE) + 0.5f))
        m_player->CastCustomSpell(m_player, BONK_SPELL_DODGE, dodge, {}, {}, true);

    SendStats();
}

void BonkRun::GiveExperience(uint32 xp)
{
    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    uint32 level = m_player->GetLevel();
    if (!xp || level >= maxLevel)
        return;

    // Same as Player::GiveXP without the chat line for every kill, and
    // without the full heal a level up normally gives.
    uint32 const health = m_player->GetHealth();
    uint32 const startLevel = level;
    uint32 current = m_player->GetUInt32Value(PLAYER_XP) + xp;
    uint32 next = m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    while (current >= next && level < maxLevel)
    {
        current -= next;
        m_player->GiveLevel(level + 1);
        level = m_player->GetLevel();
        next = m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    }
    m_player->SetUInt32Value(PLAYER_XP, level >= maxLevel ? 0 : current);
    if (level != startLevel && m_player->IsAlive())
    {
        // Not the full heal a level normally gives: a share per level.
        uint32 const heal = ReduceHealing(uint32(m_player->GetMaxHealth() * BONK_LEVEL_UP_HEAL * (level - startLevel)));
        m_player->SetHealth(std::min(health + heal, m_player->GetMaxHealth()));
    }
}

void BonkRun::DropOrb(Creature const* mob, uint32 experience, BonkOrb colour)
{
    if (!experience)
        return;

    // Past the cap, one red orb gathers the experience instead.
    Map* map = m_player->GetMap();
    bool const overflow = m_orbs.size() >= BONK_MAX_ORBS;
    if (overflow)
    {
        for (Orb& orb : m_orbs)
            if (orb.guid == m_overflowOrb && map->GetCreature(orb.guid))
            {
                orb.experience += experience;
                return;
            }
        colour = BONK_ORB_RED;
    }

    uint32 const entry = colour == BONK_ORB_BLUE ? BONK_ORB_SMALL : colour == BONK_ORB_GREEN ? BONK_ORB_LARGE : BONK_ORB_HUGE;
    Creature* orb = m_player->SummonCreature(entry, mob->GetPositionX(), mob->GetPositionY(), mob->GetPositionZ(), 0.0f,
        TEMPSUMMON_TIMED_DESPAWN, BONK_ORB_LIFETIME_MS, false, 0, &InstallOrbAI);
    if (!orb)
    {
        GiveExperience(experience);
        return;
    }

    orb->SetFactionTemplateId(BONK_ORB_FACTION);
    // The colour is the orb's own model (bonk orb templates), never a glow:
    // the Glowy auras are what affixed elites wear.
    orb->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);

    Orb dropped;
    dropped.guid = orb->GetObjectGuid();
    dropped.experience = experience;
    m_orbs.push_back(dropped);
    if (overflow)
        m_overflowOrb = dropped.guid;
}

void BonkRun::UpdateOrbs(uint32 diff)
{
    m_orbTimer += diff;
    if (m_orbTimer < BONK_ORB_CHECK_MS || m_orbs.empty())
        return;
    m_orbTimer = 0;

    float const magnet = BONK_ORB_MAGNET_RANGE * (1.0f + GetPassiveTotal(BONK_PASSIVE_PICKUP) / 100.0f);
    Map* map = m_player->GetMap();
    uint32 collected = 0;
    for (auto itr = m_orbs.begin(); itr != m_orbs.end();)
    {
        Creature* orb = map->GetCreature(itr->guid);
        float const distance = orb ? m_player->GetDistance(orb) : 0.0f;

        // Touching an orb takes it; one that vanished is not lost either.
        if (!orb || distance <= BONK_ORB_PICKUP_RANGE)
        {
            collected += itr->experience;
            if (orb)
                VanishOrb(orb);
            itr = m_orbs.erase(itr);
            continue;
        }

        // Pulled orbs fly in a straight line, not along a follow path: that
        // slows to a walk at the end and aims ahead of a running player, so
        // orbs would hover beside the player before being taken.
        float const x = m_player->GetPositionX();
        float const y = m_player->GetPositionY();
        float const z = m_player->GetPositionZ() + 0.5f;
        if (!itr->pulled ? distance <= magnet :
            (x - itr->aimX) * (x - itr->aimX) + (y - itr->aimY) * (y - itr->aimY) + (z - itr->aimZ) * (z - itr->aimZ) >
                BONK_ORB_REAIM_DISTANCE * BONK_ORB_REAIM_DISTANCE)
        {
            itr->pulled = true;
            itr->aimX = x;
            itr->aimY = y;
            itr->aimZ = z;
            orb->MonsterMoveWithSpeed(x, y, z, -10.0f, BONK_ORB_FLY_SPEED, MOVE_FLY_MODE);
        }
        ++itr;
    }

    if (collected)
        GiveExperience(collected);
}

void BonkRun::CollectAllOrbs()
{
    uint32 collected = 0;
    for (Orb const& orb : m_orbs)
        collected += orb.experience;
    DespawnOrbs();
    if (collected)
        GiveExperience(collected);
}

void BonkRun::DespawnOrbs()
{
    if (Map* map = m_player->FindMap())
        for (Orb const& orb : m_orbs)
            if (Creature* creature = map->GetCreature(orb.guid))
                VanishOrb(creature);
    m_orbs.clear();
}

void BonkRun::DropShards(Creature const* mob, BonkSpawnKind kind)
{
    // Rare on purpose: talents are meant to take many runs.
    uint32 count = 0;
    switch (kind)
    {
        case BONK_SPAWN_NORMAL:   count = roll_chance_f(m_tier.shardChanceNormal) ? 1 : 0; break;
        case BONK_SPAWN_ELITE:    count = roll_chance_f(m_tier.shardChanceElite) ? 1 : 0; break;
        case BONK_SPAWN_MINIBOSS: count = m_tier.shardsMiniboss; break;
        case BONK_SPAWN_BOSS:     count = m_tier.shardsBoss; break;
        default: break;
    }
    if (!count)
        return;

    // Beside the spot, so it doesn't hide under the chest that drops there too.
    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = mob->GetPositionX() + BONK_SHARD_SCATTER * std::cos(angle);
    float y = mob->GetPositionY() + BONK_SHARD_SCATTER * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, mob->GetPositionZ() + 2.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT)
    {
        x = mob->GetPositionX();
        y = mob->GetPositionY();
        z = mob->GetPositionZ();
    }

    GameObject* go = PlaceObject(BONK_GO_SHARD, x, y, z, BONK_OBJECT_LIFETIME_SEC);
    if (!go)
    {
        GiveShards(count);
        return;
    }

    ShardDrop drop;
    drop.guid = go->GetObjectGuid();
    drop.count = count;
    drop.marker = PlaceMarker(go, BONK_SPELL_SHARD_BEAM, BONK_SHARD_BEAM_SCALE);
    m_shardDrops.push_back(drop);
    m_player->GetBonkPlayer()->Notify(count == 1 ? "A Talent Shard dropped!" : FormatBonkShards(count) + " dropped!");
}

void BonkRun::UpdateShards(uint32 diff)
{
    m_shardTimer += diff;
    if (m_shardTimer < BONK_SHARD_CHECK_MS || m_shardDrops.empty())
        return;
    m_shardTimer = 0;

    Map* map = m_player->GetMap();
    uint32 taken = 0;
    for (auto itr = m_shardDrops.begin(); itr != m_shardDrops.end();)
    {
        GameObject* go = map->GetGameObject(itr->guid);
        if (go && go->isSpawned() && m_player->GetDistance(go) > BONK_SHARD_PICKUP_RANGE)
        {
            ++itr;
            continue;
        }

        // Touched, or gone some other way: the shards are not lost.
        taken += itr->count;
        RemovePickup(itr->guid, itr->marker);
        itr = m_shardDrops.erase(itr);
    }

    if (taken)
        GiveShards(taken);
}

void BonkRun::CollectAllShards()
{
    uint32 taken = 0;
    for (ShardDrop const& drop : m_shardDrops)
    {
        taken += drop.count;
        RemovePickup(drop.guid, drop.marker);
    }
    m_shardDrops.clear();

    if (taken)
        GiveShards(taken);
}

void BonkRun::DropHeal(Creature const* mob, BonkSpawnKind kind)
{
    float chance = 100.0f;
    if (kind == BONK_SPAWN_NORMAL)
        chance = BONK_HEAL_CHANCE_NORMAL;
    else if (kind == BONK_SPAWN_ELITE)
        chance = BONK_HEAL_CHANCE_ELITE;
    if (m_healDrops.size() >= BONK_MAX_HEAL_DROPS || !roll_chance_f(chance))
        return;

    // The other side of the spot from a shard.
    float const angle = frand(0.0f, 2.0f * M_PI_F);
    float x = mob->GetPositionX() - BONK_SHARD_SCATTER * std::cos(angle);
    float y = mob->GetPositionY() - BONK_SHARD_SCATTER * std::sin(angle);
    float z = m_player->GetMap()->GetHeight(x, y, mob->GetPositionZ() + 2.0f, true, 10.0f);
    if (z <= INVALID_HEIGHT)
    {
        x = mob->GetPositionX();
        y = mob->GetPositionY();
        z = mob->GetPositionZ();
    }

    if (GameObject* go = PlaceObject(BONK_GO_HEAL, x, y, z, BONK_OBJECT_LIFETIME_SEC))
    {
        HealDrop drop;
        drop.guid = go->GetObjectGuid();
        drop.marker = PlaceMarker(go, BONK_SPELL_HEAL_SPARKLE, BONK_HEAL_SPARKLE_SCALE);
        m_healDrops.push_back(drop);
    }
}

void BonkRun::UpdateHeals(uint32 diff)
{
    m_healTimer += diff;
    if (m_healTimer < BONK_HEAL_CHECK_MS || m_healDrops.empty())
        return;
    m_healTimer = 0;

    // Left lying while the hero is unhurt: one may be needed later.
    bool hurt = m_player->IsAlive() && m_player->GetHealth() < m_player->GetMaxHealth();
    float const range = BONK_ORB_MAGNET_RANGE * (1.0f + GetPassiveTotal(BONK_PASSIVE_PICKUP) / 100.0f);
    Map* map = m_player->GetMap();
    for (auto itr = m_healDrops.begin(); itr != m_healDrops.end();)
    {
        GameObject* go = map->GetGameObject(itr->guid);
        if (!go || !go->isSpawned())
        {
            RemovePickup(itr->guid, itr->marker);
            itr = m_healDrops.erase(itr);
            continue;
        }
        if (!hurt || m_player->GetDistance(go) > range)
        {
            ++itr;
            continue;
        }

        uint32 const heal = ReduceHealing(std::max(1u, uint32(m_player->GetMaxHealth() * BONK_HEAL_AMOUNT)));
        if (SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BONK_SPELL_HEAL_TEXT))
            m_player->DealHeal(m_player, heal, spellInfo);
        else
            m_player->ModifyHealth(int32(heal));
        RemovePickup(itr->guid, itr->marker);
        itr = m_healDrops.erase(itr);
        hurt = m_player->GetHealth() < m_player->GetMaxHealth();
    }
}

void BonkRun::GiveShards(uint32 count)
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->AddShards(count);
    sBonkQuests.OnShards(m_player, count);
    m_shardsEarned += count;
    bonk->SendAddon("shard~" + std::to_string(count) + "~" + std::to_string(bonk->GetShards()));
    bonk->Notify("+" + FormatBonkShards(count) + "!");
    SendState();
}

ObjectGuid BonkRun::PlaceMarker(GameObject const* go, uint32 spellId, float scale)
{
    Creature* marker = m_player->SummonCreature(BONK_PICKUP_MARKER, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), 0.0f,
        TEMPSUMMON_TIMED_DESPAWN, BONK_OBJECT_LIFETIME_SEC * IN_MILLISECONDS, false, 0, &InstallOrbAI);
    if (!marker)
        return ObjectGuid();

    marker->SetFactionTemplateId(BONK_ORB_FACTION);
    marker->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
    marker->SetObjectScale(scale);
    marker->AddAura(spellId, ADD_AURA_PERMANENT);
    return marker->GetObjectGuid();
}

void BonkRun::RemovePickup(ObjectGuid const& goGuid, ObjectGuid const& markerGuid)
{
    Map* map = m_player->FindMap();
    if (!map)
        return;
    if (GameObject* go = map->GetGameObject(goGuid))
        go->Delete();
    // The highlight first: left on, the client would keep showing it while
    // the marker fades out.
    if (Creature* marker = map->GetCreature(markerGuid))
    {
        marker->RemoveAllAuras();
        marker->DespawnOrUnsummon();
    }
}

void BonkRun::GiveGold(uint32 copper)
{
    copper = uint32(copper * (1.0f + (GetPassiveTotal(BONK_PASSIVE_GREED) + m_bonuses.goldBonus) / 100.0f));
    if (!copper)
        return;
    m_player->ModifyMoney(int32(copper));
    m_goldEarned += copper;
}

void BonkRun::OnMobKilled(Creature* mob, BonkSpawnKind kind)
{
    if (m_state != STATE_RUNNING)
        return;

    ++m_kills;
    SpreadFrom(mob);
    OnMarkedDeath(mob);
    uint32 const killingSpell = BonkStats::CountKill(m_player->GetObjectGuid(), mob->GetObjectGuid(), kind);
    sBonkQuests.OnKill(m_player, kind, killingSpell);
    SoulSiphon(killingSpell);
    if (kind == BONK_SPAWN_MINIBOSS)
        m_masteryXp += m_tier.masteryXpMiniboss;
    else if (kind == BONK_SPAWN_BOSS)
        m_masteryXp += m_tier.masteryXpBoss;
    uint32 const reward = GetKindReward(kind);
    GiveGold(m_map.goldPerKill * reward);
    m_coins += m_map.coinsPerKill * reward;

    if (kind != BONK_SPAWN_BOSS)
    {
        // Early levels take a handful of kills, later ones a few dozen.
        uint32 const level = m_player->GetLevel();
        uint32 const killsForLevel = 4 + 2 * level;
        float const growth = 1.0f + GetPassiveTotal(BONK_PASSIVE_GROWTH) / 100.0f;

        // Normal enemies by archetype: swarmers give half, bruisers a green
        // orb worth several; elites and bosses red ones.
        float experience = float(reward);
        BonkOrb colour = BONK_ORB_RED;
        if (kind == BONK_SPAWN_NORMAL)
        {
            BonkMobAI const* ai = dynamic_cast<BonkMobAI const*>(mob->AI());
            BonkArchetypeInfo const& archetype = sBonkMgr.GetArchetype(ai ? ai->GetArchetype() : BONK_ARCHETYPE_GRUNT);
            experience *= archetype.experience;
            colour = archetype.orb;
        }
        uint32 const xp = uint32(std::ceil(m_player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP) / float(killsForLevel) * experience * growth));
        DropOrb(mob, xp, colour);
    }

    if (kind != BONK_SPAWN_NORMAL)
    {
        DropChest(mob, kind);
        OnAffixedDeath(mob);
        m_player->GetBonkPlayer()->SendAddon("mobdead~" + std::string(mob->GetName()));
    }

    DropShards(mob, kind);
    DropHeal(mob, kind);

    if (mob->GetObjectGuid() == m_bossGuid)
        OpenPortal(mob);
}

BonkRun::OwnedWeapon* BonkRun::FindOwned(uint32 weaponId)
{
    auto itr = std::find_if(m_weapons.begin(), m_weapons.end(), [weaponId](OwnedWeapon const& owned) { return owned.weapon.id == weaponId; });
    return itr != m_weapons.end() ? &*itr : nullptr;
}

BonkRun::OwnedWeapon const* BonkRun::FindOwned(uint32 weaponId) const
{
    auto itr = std::find_if(m_weapons.begin(), m_weapons.end(), [weaponId](OwnedWeapon const& owned) { return owned.weapon.id == weaponId; });
    return itr != m_weapons.end() ? &*itr : nullptr;
}

bool BonkRun::CanEvolve(OwnedWeapon const& owned) const
{
    if (owned.weapon.evolvesFrom)
        return false;
    BonkWeapon const* evolved = sBonkMgr.GetEvolution(owned.weapon.id);
    return evolved && !sBonkQuests.IsLocked(m_player->GetSession()->GetAccountId(), BONK_REWARD_WEAPON, evolved->id);
}

float BonkRun::GetLegendaryChance(OwnedWeapon const& owned) const
{
    if (owned.rank < BONK_EVOLVE_RANK)
        return 0.0f;
    if (owned.rank >= BONK_EVOLVE_PITY_RANK && CanEvolve(owned))
        return 100.0f;
    return BONK_LEGENDARY_BASE_CHANCE + BONK_LEGENDARY_CHANCE_PER_RANK * (owned.rank - BONK_EVOLVE_RANK) +
        BONK_LEGENDARY_CHANCE_PER_LUCK * std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
}

BonkRarity BonkRun::RollRarity(BonkRarity minimum) const
{
    // Luck raises the weights of the better rarities, the best the most.
    float const luck = std::max(0.0f, GetPassiveTotal(BONK_PASSIVE_LUCK));
    float weights[BONK_RARITY_LEGENDARY];
    float total = 0.0f;
    for (uint8 rarity = 0; rarity < BONK_RARITY_LEGENDARY; ++rarity)
    {
        weights[rarity] = rarity < minimum ? 0.0f : BONK_RARITY_WEIGHT[rarity] * (1.0f + BONK_RARITY_LUCK[rarity] * luck / 100.0f);
        total += weights[rarity];
    }

    float roll = frand(0.0f, total);
    for (uint8 rarity = 0; rarity < BONK_RARITY_LEGENDARY; ++rarity)
    {
        if (weights[rarity] > 0.0f && roll < weights[rarity])
            return BonkRarity(rarity);
        roll -= weights[rarity];
    }
    return BONK_RARITY_EPIC;
}

BonkRankStep BonkRun::RollStep(OwnedWeapon const& owned, BonkRarity rarity) const
{
    BonkWeapon const& weapon = owned.weapon;
    std::vector<BonkRankStep> options;
    switch (rarity)
    {
        case BONK_RARITY_LEGENDARY:
            return CanEvolve(owned) ? BONK_STEP_EVOLVE : BONK_STEP_LEGENDARY_DAMAGE;
        case BONK_RARITY_EPIC:
            // Spells around the hero, cones and buffs have no target to add.
            if (owned.epicSteps < BONK_MAX_EPIC_STEPS && weapon.HasTargets())
                return BONK_STEP_TARGET;
            return BONK_STEP_EPIC_DAMAGE;
        case BONK_RARITY_RARE:
            if (weapon.IsAimed())
                options.push_back(BONK_STEP_RANGE);
            if (weapon.HasJumps() && owned.extraJumps < BONK_MAX_JUMPS)
                options.push_back(BONK_STEP_JUMP);
            if (weapon.IsWide())
                options.push_back(BONK_STEP_WIDE);
            // A buff or a beast: it lasts longer instead.
            if (options.empty() && weapon.lasting)
                options.push_back(BONK_STEP_DURATION);
            break;
        case BONK_RARITY_UNCOMMON:
            options.push_back(BONK_STEP_COOLDOWN);
            if (weapon.HasArea())
                options.push_back(BONK_STEP_AREA);
            if (weapon.lasting)
                options.push_back(BONK_STEP_DURATION);
            break;
        default:
            return BONK_STEP_DAMAGE;
    }

    if (options.empty())
        return BONK_STEP_COOLDOWN;
    return options[urand(0, uint32(options.size() - 1))];
}

BonkRun::Offer BonkRun::MakeRankOffer(OwnedWeapon const& owned, BonkRarity minimum) const
{
    Offer offer;
    offer.id = owned.weapon.id;
    offer.newLevel = owned.rank + 1;
    float const legendary = GetLegendaryChance(owned);
    offer.rarity = legendary > 0.0f && roll_chance_f(legendary) ? BONK_RARITY_LEGENDARY : RollRarity(minimum);
    offer.step = RollStep(owned, offer.rarity);
    return offer;
}

void BonkRun::ApplyStep(OwnedWeapon& owned, Offer const& offer)
{
    ++owned.rank;
    owned.rarities += char('0' + int(offer.rarity));
    switch (offer.step)
    {
        case BONK_STEP_DAMAGE:
            owned.damageBonus += BONK_RANK_DAMAGE;
            break;
        case BONK_STEP_COOLDOWN:
            owned.damageBonus += BONK_RANK_DAMAGE_MINOR;
            owned.hasteBonus += BONK_RANK_HASTE;
            break;
        case BONK_STEP_AREA:
            owned.damageBonus += BONK_RANK_DAMAGE_MINOR;
            owned.areaBonus += BONK_RANK_AREA;
            break;
        case BONK_STEP_DURATION:
            owned.damageBonus += BONK_RANK_DAMAGE_MINOR;
            owned.durationBonus += BONK_RANK_DURATION;
            break;
        case BONK_STEP_RANGE:
            owned.damageBonus += BONK_RANK_DAMAGE_MINOR;
            owned.rangeBonus += BONK_RANK_RANGE;
            break;
        case BONK_STEP_JUMP:
            owned.damageBonus += BONK_RANK_DAMAGE_MINOR;
            ++owned.extraJumps;
            break;
        case BONK_STEP_WIDE:
            owned.damageBonus += BONK_RANK_DAMAGE_MINOR;
            owned.areaBonus += BONK_RANK_WIDE;
            break;
        case BONK_STEP_TARGET:
            ++owned.extraTargets;
            ++owned.epicSteps;
            break;
        case BONK_STEP_EPIC_DAMAGE:
            owned.damageBonus += BONK_RANK_EPIC_DAMAGE;
            break;
        case BONK_STEP_EVOLVE:
            if (BonkWeapon const* evolved = sBonkMgr.GetEvolution(owned.weapon.id))
            {
                // Ranks and what they added carry over.
                std::string const from = owned.weapon.name;
                owned.weapon = *evolved;
                owned.timer = 500;
                m_player->GetBonkPlayer()->Notify(from + " evolved into " + evolved->name + "!");
            }
            break;
        case BONK_STEP_LEGENDARY_DAMAGE:
            owned.damageBonus += BONK_RANK_LEGENDARY_DAMAGE;
            break;
        default:
            break;
    }
}

bool BonkRun::IsPassiveAllowed(BonkPassive const& passive) const
{
    for (auto const& itr : m_passiveRanks)
    {
        if (!itr.second || itr.first == passive.id)
            continue;
        BonkPassive const* owned = sBonkMgr.GetPassive(itr.first);
        if (passive.excludes == itr.first || (owned && owned->excludes == passive.id))
            return false;
    }
    return true;
}

bool BonkRun::RefreshKeptOffer(Offer& offer) const
{
    if (offer.isWeapon)
    {
        if (m_banished.count({ true, offer.id }))
            return false;

        OwnedWeapon const* owned = FindOwned(offer.id);
        if (offer.step == BONK_STEP_NEW)
            return !owned && m_weapons.size() < BONK_MAX_WEAPONS &&
                std::none_of(m_weapons.begin(), m_weapons.end(), [&](OwnedWeapon const& w) { return w.weapon.evolvesFrom == offer.id; });
        if (!owned)
            return false;

        // The rarity stays; what it adds is rolled again only if it no
        // longer fits (evolved, Epic targets used up).
        offer.newLevel = owned->rank + 1;
        bool fits = true;
        switch (offer.step)
        {
            case BONK_STEP_EVOLVE: fits = CanEvolve(*owned); break;
            case BONK_STEP_TARGET: fits = owned->epicSteps < BONK_MAX_EPIC_STEPS; break;
            case BONK_STEP_JUMP:   fits = owned->extraJumps < BONK_MAX_JUMPS; break;
            default: break;
        }
        if (!fits)
            offer.step = RollStep(*owned, offer.rarity);
        return true;
    }

    BonkPassive const* passive = sBonkMgr.GetPassive(offer.id);
    if (!passive || m_banished.count({ false, offer.id }) || !IsPassiveAllowed(*passive))
        return false;
    auto rank = m_passiveRanks.find(offer.id);
    uint8 const current = rank != m_passiveRanks.end() ? rank->second : 0;
    size_t const ownedPassives = std::count_if(m_passiveRanks.begin(), m_passiveRanks.end(), [](auto const& itr) { return itr.second > 0; });
    if (current >= passive->maxRank || (!current && ownedPassives >= BONK_MAX_PASSIVES))
        return false;
    offer.newLevel = current + 1;
    return true;
}

bool BonkRun::OfferUpgrades(OfferSource source, std::vector<Offer> const* avoid, std::vector<Offer> const* kept)
{
    struct Candidate
    {
        Offer offer;
        uint32 weight;
    };
    std::vector<Candidate> candidates;
    BonkRarity const minimum = source == OFFER_CHEST ? BONK_CHEST_MIN_RARITY : BONK_LEVEL_MIN_RARITY;
    // An elite chest offers a card more for every affix the elite had.
    size_t const offerSize = source == OFFER_CHEST && !m_pendingChestChoices.empty() ?
        std::min<size_t>(m_pendingChestChoices.front(), BONK_MAX_OFFER_SIZE) : BONK_OFFER_SIZE;

    // Locked cards stay: from this choice on a reroll or banish, or from the
    // last chest.
    std::vector<Offer> cards;
    std::vector<Offer> carried;
    if (kept)
        carried = *kept;
    else if (source == OFFER_CHEST)
    {
        carried = m_lockedOffers;
        m_lockedOffers.clear();
    }
    for (Offer offer : carried)
        if (cards.size() < offerSize && RefreshKeptOffer(offer))
        {
            offer.locked = true;
            cards.push_back(offer);
        }
    auto isCard = [&cards](bool isWeapon, uint32 id)
    {
        return std::any_of(cards.begin(), cards.end(), [&](Offer const& card) { return card.isWeapon == isWeapon && card.id == id; });
    };

    // A rank for every spell the hero has. One owed its Legendary rank
    // (BONK_EVOLVE_PITY_RANK) always comes up.
    for (auto const& owned : m_weapons)
    {
        if (m_banished.count({ true, owned.weapon.id }) || isCard(true, owned.weapon.id))
            continue;
        Offer const offer = MakeRankOffer(owned, BONK_RARITY_COMMON);
        if (offer.step == BONK_STEP_EVOLVE && owned.rank >= BONK_EVOLVE_PITY_RANK && cards.size() < offerSize)
            cards.push_back(offer);
        else
            candidates.push_back({ offer, 3 });
    }

    // New spells while a slot is free. An evolved spell's base counts as owned.
    uint32 const classMask = m_player->GetClassMask();
    uint32 const account = m_player->GetSession()->GetAccountId();
    if (m_weapons.size() < BONK_MAX_WEAPONS)
        for (auto const& itr : sBonkMgr.GetWeapons())
        {
            BonkWeapon const& weapon = itr.second;
            if (weapon.evolvesFrom || (weapon.classMask && !(weapon.classMask & classMask)))
                continue;
            if (m_banished.count({ true, weapon.id }) || isCard(true, weapon.id) || sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, weapon.id))
                continue;
            if (std::any_of(m_weapons.begin(), m_weapons.end(), [&](OwnedWeapon const& owned)
                {
                    return owned.weapon.id == weapon.id || owned.weapon.evolvesFrom == weapon.id;
                }))
                continue;
            Offer offer;
            offer.id = weapon.id;
            candidates.push_back({ offer, 2 });
        }

    size_t const ownedPassives = std::count_if(m_passiveRanks.begin(), m_passiveRanks.end(), [](auto const& itr) { return itr.second > 0; });
    for (auto const& itr : sBonkMgr.GetPassives())
    {
        auto rank = m_passiveRanks.find(itr.first);
        uint8 const current = rank != m_passiveRanks.end() ? rank->second : 0;
        if (current >= itr.second.maxRank || (!current && ownedPassives >= BONK_MAX_PASSIVES))
            continue;
        if (m_banished.count({ false, itr.first }) || isCard(false, itr.first) || sBonkQuests.IsLocked(account, BONK_REWARD_PASSIVE, itr.first))
            continue;
        if (!IsPassiveAllowed(itr.second))
            continue;
        Offer offer;
        offer.isWeapon = false;
        offer.id = itr.first;
        offer.newLevel = current + 1;
        candidates.push_back({ offer, 2 });
    }

    // Cards to avoid are only drawn once the fresh ones run out.
    std::vector<Candidate> repeats;
    if (avoid)
    {
        auto shown = [avoid](Candidate const& candidate)
        {
            return std::any_of(avoid->begin(), avoid->end(), [&](Offer const& offer)
            {
                return offer.isWeapon == candidate.offer.isWeapon && offer.id == candidate.offer.id;
            });
        };
        for (auto itr = candidates.begin(); itr != candidates.end();)
        {
            if (shown(*itr))
            {
                repeats.push_back(*itr);
                itr = candidates.erase(itr);
            }
            else
                ++itr;
        }
    }

    for (std::vector<Candidate>* pool : { &candidates, &repeats })
    {
        while (cards.size() < offerSize && !pool->empty())
        {
            uint32 totalWeight = 0;
            for (auto const& candidate : *pool)
                totalWeight += candidate.weight;

            uint32 roll = urand(1, totalWeight);
            for (auto itr = pool->begin(); itr != pool->end(); ++itr)
            {
                if (roll > itr->weight)
                {
                    roll -= itr->weight;
                    continue;
                }
                cards.push_back(itr->offer);
                pool->erase(itr);
                break;
            }
        }
    }

    // Never only plain picks: one spell rank is at least `minimum` when
    // there is a spell rank on offer.
    bool met = false;
    std::vector<size_t> raisable;
    for (size_t i = 0; i < cards.size(); ++i)
    {
        if (!cards[i].isWeapon || cards[i].step == BONK_STEP_NEW)
            continue;
        if (cards[i].rarity >= minimum)
            met = true;
        else if (!cards[i].locked)
            raisable.push_back(i);
    }
    if (!met && !raisable.empty())
    {
        Offer& card = cards[raisable[urand(0, uint32(raisable.size() - 1))]];
        if (OwnedWeapon const* owned = FindOwned(card.id))
        {
            card.rarity = RollRarity(minimum);
            card.step = RollStep(*owned, card.rarity);
        }
    }

    m_offer = cards;
    if (m_offer.empty())
    {
        // Nothing left to offer: a bag of gold instead.
        if (source != OFFER_CHEST)
            --m_pendingLevels;
        else if (!m_pendingChestChoices.empty())
            m_pendingChestChoices.erase(m_pendingChestChoices.begin());
        GiveGold(m_map.goldPerKill * 25);
        m_player->GetBonkPlayer()->Notify("Nothing left to learn - have some gold!");
        return false;
    }

    m_offerSource = source;
    SetPaused(true);
    SendOffer();
    return true;
}

std::string BonkRun::DescribeOffer(Offer const& offer, std::string* icon) const
{
    if (offer.isWeapon)
    {
        BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id);
        if (offer.step == BONK_STEP_EVOLVE)
            if (BonkWeapon const* evolved = sBonkMgr.GetEvolution(offer.id))
                weapon = evolved;
        if (!weapon)
            return "?";
        if (icon)
            *icon = weapon->icon;
        return weapon->name;
    }

    BonkPassive const* passive = sBonkMgr.GetPassive(offer.id);
    if (!passive)
        return "?";
    if (icon)
        *icon = passive->icon;
    return passive->name;
}

std::string BonkRun::DescribeStep(Offer const& offer) const
{
    if (!offer.isWeapon)
    {
        BonkPassive const* passive = sBonkMgr.GetPassive(offer.id);
        return passive ? sBonkMgr.DescribePassive(*passive) : "";
    }

    BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id);
    if (!weapon)
        return "";

    std::string const minor = "+" + Percent(BONK_RANK_DAMAGE_MINOR) + " damage, ";
    std::string text;
    switch (offer.step)
    {
        case BONK_STEP_NEW:
            return weapon->description + DescribeEvolution(*weapon);
        case BONK_STEP_DAMAGE:
            text = "+" + Percent(BONK_RANK_DAMAGE) + " damage";
            break;
        case BONK_STEP_COOLDOWN:
            text = minor + "attacks " + Percent(BONK_RANK_HASTE) + " more often";
            break;
        case BONK_STEP_AREA:
            text = minor + "+" + Percent(BONK_RANK_AREA) + (weapon->behavior == BONK_BEHAVIOR_CLEAVE ? " reach" : " area");
            break;
        case BONK_STEP_DURATION:
            text = minor + "lasts " + Percent(BONK_RANK_DURATION) + " longer";
            break;
        case BONK_STEP_RANGE:
            text = minor + "+" + Percent(BONK_RANK_RANGE) + " range";
            break;
        case BONK_STEP_JUMP:
            text = minor + DescribeJump(*weapon);
            break;
        case BONK_STEP_WIDE:
            text = minor + "+" + Percent(BONK_RANK_WIDE) + " area";
            break;
        case BONK_STEP_TARGET:
        {
            OwnedWeapon const* owned = FindOwned(offer.id);
            uint32 const used = (owned ? owned->epicSteps : 0) + 1;
            text = weapon->DescribeExtraTarget() + " (" + std::to_string(used) + "/" + std::to_string(BONK_MAX_EPIC_STEPS) + ")";
            text[0] = char(toupper(text[0]));
            break;
        }
        case BONK_STEP_EPIC_DAMAGE:
            text = "+" + Percent(BONK_RANK_EPIC_DAMAGE) + " damage";
            break;
        case BONK_STEP_EVOLVE:
        {
            BonkWeapon const* evolved = sBonkMgr.GetEvolution(offer.id);
            return evolved ? "Evolves " + weapon->name + ": " + evolved->description : "";
        }
        case BONK_STEP_LEGENDARY_DAMAGE:
            text = "+" + Percent(BONK_RANK_LEGENDARY_DAMAGE) + " damage";
            break;
        default:
            break;
    }

    text += ".";
    if (weapon->behavior == BONK_BEHAVIOR_SPREAD && !weapon->evolvesFrom && offer.newLevel == BONK_EVOLVE_RANK)
        text += " Now spreads to nearby enemies when one carrying it dies.";
    return text;
}

bool BonkRun::Pick(uint32 choice, std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = "There is nothing to choose right now.";
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (choice < 1 || choice > m_offer.size())
    {
        error = "Choose a number from 1 to " + std::to_string(m_offer.size()) + ".";
        SendOffer();
        return false;
    }

    Offer const offer = m_offer[choice - 1];
    if (offer.isWeapon)
    {
        if (OwnedWeapon* owned = FindOwned(offer.id))
            ApplyStep(*owned, offer);
        else if (BonkWeapon const* weapon = sBonkMgr.GetWeapon(offer.id))
            AddWeapon(*weapon);
    }
    else
    {
        m_passiveRanks[offer.id] = uint8(offer.newLevel);
        ApplyPassives();
    }

    CloseOffer(choice - 1);
    return true;
}

void BonkRun::CloseOffer(uint32 pickedIndex)
{
    // A chest's locked cards that were not taken wait for the next chest.
    if (m_offerSource == OFFER_CHEST)
    {
        m_lockedOffers.clear();
        for (size_t i = 0; i < m_offer.size(); ++i)
            if (m_offer[i].locked && i != pickedIndex)
                m_lockedOffers.push_back(m_offer[i]);
    }

    m_offer.clear();
    if (m_offerSource == OFFER_CHEST)
    {
        if (!m_pendingChestChoices.empty())
            m_pendingChestChoices.erase(m_pendingChestChoices.begin());
    }
    else if (m_pendingLevels)
        --m_pendingLevels;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon("picked");
    SendLoadout();

    // The next pending choice pauses again right away.
    SetPaused(false);
    UpdateOffers();
}

bool BonkRun::Reroll(std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = "There is nothing to reroll right now.";
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (!m_rerolls)
    {
        error = "No rerolls left.";
        SendOffer();
        return false;
    }

    --m_rerolls;
    std::vector<Offer> const previous = m_offer;
    std::vector<Offer> kept;
    for (Offer const& offer : previous)
        if (offer.locked)
            kept.push_back(offer);
    // Nothing left to offer turns into gold; close the choice then.
    if (!OfferUpgrades(m_offerSource, &previous, &kept))
    {
        SetPaused(false);
        m_player->GetBonkPlayer()->SendAddon("picked");
    }
    return true;
}

bool BonkRun::Banish(uint32 choice, std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = "There is nothing to banish right now.";
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (!m_banishes || choice < 1 || choice > m_offer.size())
    {
        error = m_banishes ? "Choose a number from 1 to " + std::to_string(m_offer.size()) + "." : "No banishes left.";
        SendOffer();
        return false;
    }

    Offer const offer = m_offer[choice - 1];
    m_banished.insert({ offer.isWeapon, offer.id });
    m_player->GetBonkPlayer()->Notify(DescribeOffer(offer, nullptr) + " is banished for this run.");
    --m_banishes;

    std::vector<Offer> kept;
    for (size_t i = 0; i < m_offer.size(); ++i)
        if (m_offer[i].locked && i + 1 != choice)
            kept.push_back(m_offer[i]);
    // Nothing left to offer turns into gold; close the choice then.
    if (!OfferUpgrades(m_offerSource, nullptr, &kept))
    {
        SetPaused(false);
        m_player->GetBonkPlayer()->SendAddon("picked");
    }
    return true;
}

bool BonkRun::Skip(std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = "There is nothing to skip right now.";
        m_player->GetBonkPlayer()->SendAddon("picked");
        return false;
    }

    if (!m_skips)
    {
        error = "No skips left.";
        SendOffer();
        return false;
    }

    --m_skips;
    uint32 const coins = std::max(1u, uint32(GetChestCost() * BONK_SKIP_COINS));
    m_coins += coins;
    m_player->GetBonkPlayer()->Notify("Skipped: +" + std::to_string(coins) + " Valor.");
    CloseOffer(uint32(m_offer.size()));     // no card taken
    SendState();
    return true;
}

bool BonkRun::Lock(uint32 choice, std::string& error)
{
    if ((m_state != STATE_RUNNING && m_state != STATE_PORTAL) || m_offer.empty())
    {
        error = "There is nothing to lock right now.";
        return false;
    }

    if (m_offerSource != OFFER_CHEST)
    {
        error = "Only chest choices can be locked.";
        return false;
    }

    if (choice < 1 || choice > m_offer.size())
    {
        error = "Choose a number from 1 to " + std::to_string(m_offer.size()) + ".";
        return false;
    }

    m_offer[choice - 1].locked = !m_offer[choice - 1].locked;
    SendOffer();
    return true;
}

void BonkRun::SendState() const
{
    m_player->GetBonkPlayer()->SendAddon("state~" + std::to_string(m_elapsedMs / IN_MILLISECONDS) + "~" + std::to_string(m_map.durationSeconds) + "~" +
        std::to_string(m_kills) + "~" + std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + (m_bossSpawned ? "1" : "0") + "~" +
        std::to_string(m_coins) + "~" + std::to_string(GetChestCost()) + "~" + std::to_string(m_stage) + "~" + (m_state == STATE_PORTAL ? "1" : "0") + "~" +
        std::to_string(m_shardsEarned) + "~" + DescribeWeaponStats());
}

std::string BonkRun::DescribeWeaponStats() const
{
    std::string text;
    for (auto const& owned : m_weapons)
    {
        BonkRunStats::Spell const stats = BonkStats::GetSpellStats(m_player->GetObjectGuid(), owned.weapon.spellId);
        text += std::to_string(stats.damage) + ":" + std::to_string(stats.kills) + ",";
    }
    return text;
}

void BonkRun::SendLoadout() const
{
    // w:<icon>:<rank>:0,... (e: for evolved) then p:<icon>:<rank>:<max>,...;
    // spell ranks have no maximum.
    std::string line = "loadout~";
    for (auto const& owned : m_weapons)
        line += std::string(owned.weapon.evolvesFrom ? "e:" : "w:") + owned.weapon.icon + ":" + std::to_string(owned.rank) + ":0,";
    for (auto const& itr : m_passiveRanks)
        if (BonkPassive const* passive = sBonkMgr.GetPassive(itr.first))
            line += "p:" + passive->icon + ":" + std::to_string(itr.second) + ":" + std::to_string(passive->maxRank) + ",";
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->SendAddon(line);

    // pw~index~name~ranks~intervalMs~nextCastMs~line~description and, for
    // spells, pwe~index~evolution~evolutionText~evolvedFrom~chanceText~chance;
    // the index counts the loadout's entries above, from 1.
    uint32 index = 0;
    for (auto const& owned : m_weapons)
    {
        ++index;
        BonkWeapon const& weapon = owned.weapon;
        uint32 const interval = GetWeaponCooldown(owned);
        uint32 const tenths = (interval + 50) / 100;
        std::string stats = "Every " + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " s";
        uint32 const targets = weapon.baseTargets + owned.extraTargets;
        if (weapon.HasTargets() && targets > 1)
            stats += " - " + weapon.DescribeTargets(targets);
        if (owned.extraJumps)
        {
            std::string const count = std::to_string(owned.extraJumps);
            if (weapon.behavior == BONK_BEHAVIOR_CLEAVE)
                stats += " - cleaves " + count + " more";
            else if (weapon.behavior == BONK_BEHAVIOR_SPREAD)
                stats += " - spreads to " + count + " more";
            else
                stats += " - hits " + count + " more";
        }
        bonk->SendAddon("pw~" + std::to_string(index) + "~" + weapon.name + "~" + owned.rarities + "~" + std::to_string(interval) + "~" +
            std::to_string(std::max<int32>(0, owned.timer)) + "~" + stats + "~" + weapon.description);

        std::string evolution, evolutionText, evolvedFrom, chanceText;
        uint32 chance = 0;
        if (weapon.evolvesFrom)
        {
            if (BonkWeapon const* base = sBonkMgr.GetWeapon(weapon.evolvesFrom))
                evolvedFrom = base->name;
        }
        else if (BonkWeapon const* evolved = sBonkMgr.GetEvolution(weapon.id))
        {
            evolution = evolved->name;
            evolutionText = evolved->description;
            if (!CanEvolve(owned))
                chanceText = "A quest still holds it back.";
            else if (owned.rank < BONK_EVOLVE_RANK)
                chanceText = "Offered from rank " + std::to_string(BONK_EVOLVE_RANK) + ".";
            else
            {
                chance = uint32(std::min(100.0f, GetLegendaryChance(owned)) + 0.5f);
                chanceText = chance >= 100 ? "Comes up at the next level-up." : std::to_string(chance) + "% chance for each card of this spell.";
            }
        }
        bonk->SendAddon("pwe~" + std::to_string(index) + "~" + evolution + "~" + evolutionText + "~" + evolvedFrom + "~" + chanceText + "~" +
            std::to_string(chance));
    }
    for (auto const& itr : m_passiveRanks)
    {
        BonkPassive const* passive = sBonkMgr.GetPassive(itr.first);
        if (!passive)
            continue;
        ++index;
        bonk->SendAddon("pw~" + std::to_string(index) + "~" + passive->name + "~~0~0~~Each rank: " + sBonkMgr.DescribePassive(*passive));
    }
}

void BonkRun::SendOffer() const
{
    BonkPlayer* bonk = m_player->GetBonkPlayer();
    if (bonk->IsAddonReady())
    {
        // offer~level~cards~source~rerolls~banishes~skips, then for each card
        // opt~index~kind~rank~maxRank~name~icon~description~rarity~locked:
        // kind w (spell), e (evolution) or p (passive); maxRank 0 for spells;
        // rarity empty for new spells and passives.
        bonk->SendAddon("offer~" + std::to_string(m_player->GetLevel()) + "~" + std::to_string(m_offer.size()) + "~" +
            (m_offerSource == OFFER_CHEST ? "chest" : "level") + "~" + std::to_string(m_rerolls) + "~" + std::to_string(m_banishes) + "~" +
            std::to_string(m_skips));
        for (size_t i = 0; i < m_offer.size(); ++i)
        {
            Offer const& offer = m_offer[i];
            std::string icon;
            std::string const name = DescribeOffer(offer, &icon);
            uint32 maxRank = 0;
            if (!offer.isWeapon)
                if (BonkPassive const* passive = sBonkMgr.GetPassive(offer.id))
                    maxRank = passive->maxRank;
            bool const ranked = offer.isWeapon && offer.step != BONK_STEP_NEW;

            bonk->SendAddon("opt~" + std::to_string(i + 1) + "~" + (offer.step == BONK_STEP_EVOLVE ? "e" : offer.isWeapon ? "w" : "p") + "~" +
                std::to_string(offer.newLevel) + "~" + std::to_string(maxRank) + "~" + name + "~" + icon + "~" + DescribeStep(offer) + "~" +
                (ranked ? std::to_string(offer.rarity) : "") + "~" + (offer.locked ? "1" : "0"));
        }
        bonk->SendAddon("offerend");
        return;
    }

    ChatHandler handler(m_player);
    if (m_offerSource == OFFER_CHEST)
        handler.SendSysMessage("Chest! Choose an upgrade with .bonk pick <number>:");
    else
        handler.PSendSysMessage("Level %u! Choose an upgrade with .bonk pick <number>:", m_player->GetLevel());
    for (size_t i = 0; i < m_offer.size(); ++i)
    {
        Offer const& offer = m_offer[i];
        std::string const name = DescribeOffer(offer, nullptr);
        std::string kind;
        if (offer.isWeapon && offer.step == BONK_STEP_NEW)
            kind = "new";
        else if (offer.isWeapon)
            kind = std::string(GetBonkRarityName(offer.rarity)) + " rank " + std::to_string(offer.newLevel);
        else
            kind = "rank " + std::to_string(offer.newLevel);
        handler.PSendSysMessage("  %u) %s (%s): %s", uint32(i + 1), name.c_str(), kind.c_str(), DescribeStep(offer).c_str());
    }
    if (m_rerolls)
        handler.PSendSysMessage("  .bonk reroll for new choices (%u left)", m_rerolls);
    if (m_banishes)
        handler.PSendSysMessage("  .bonk banish <number> to remove one for this run (%u left)", m_banishes);
    if (m_skips)
        handler.PSendSysMessage("  .bonk pass to skip this choice for a little Valor (%u left)", m_skips);
}

void BonkRun::Finish(bool wentHome)
{
    if (m_state != STATE_RUNNING && m_state != STATE_PORTAL)
        return;

    m_state = STATE_ENDING;
    m_wentHome = wentHome;
    SetPaused(false);

    // Going home takes the shards still lying around; dying leaves them.
    if (wentHome)
        CollectAllShards();

    m_player->GetBonkPlayer()->RecordRun(m_stage, m_totalMs / IN_MILLISECONDS, m_kills, m_goldEarned);
    AwardMastery();
    m_offer.clear();
    m_pendingLevels = 0;
    m_pendingChestChoices.clear();
    m_endTimer = BONK_END_DELAY_MS;

    // Enemies despawn themselves once the run is no longer active.
    if (m_player->IsAlive())
        m_player->CombatStop(true);

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    uint32 const seconds = m_totalMs / IN_MILLISECONDS;
    if (bonk->IsAddonReady())
        bonk->SendAddon("end~" + std::string(wentHome ? "1" : "0") + "~" + std::to_string(seconds) + "~" + std::to_string(m_kills) + "~" +
            std::to_string(m_goldEarned) + "~" + std::to_string(m_player->GetLevel()) + "~" + std::to_string(m_stage) + "~" + std::to_string(m_shardsEarned));
    else
        ChatHandler(m_player).PSendSysMessage("%s Reached stage %u in %s, %u kills, level %u, earned %s and %s.", wentHome ? "Run complete!" : "Defeated.",
            m_stage, FormatTime(seconds).c_str(), m_kills, m_player->GetLevel(), FormatGold(m_goldEarned).c_str(), FormatBonkShards(m_shardsEarned).c_str());

    // The results screen: what each weapon did.
    if (bonk->IsAddonReady())
    {
        std::vector<BonkSummaryWeapon> weapons;
        for (OwnedWeapon const& owned : m_weapons)
        {
            BonkSummaryWeapon weapon;
            weapon.spellId = owned.weapon.spellId;
            weapon.spellIds = BonkStats::GetWeaponSpells(owned.weapon);
            weapon.name = owned.weapon.name;
            weapon.icon = owned.weapon.icon;
            weapon.level = owned.rank;
            weapon.maxLevel = 0;      // spell ranks are uncapped
            weapon.evolved = owned.weapon.evolvesFrom != 0;
            weapons.push_back(weapon);
        }
        BonkStats::SendSummary(m_player, weapons);
    }

    sBonkQuests.Save(m_player->GetSession()->GetAccountId());
    sBonkQuests.SendLog(m_player, "");
}

void BonkRun::SetPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;

    // Everything stops at once, rather than each enemy on its own next
    // update. A root, not an aura: runs make the player immune to those.
    // Auras, casts and ground spells stop too (Unit::SetTimeFrozen).
    if (m_player->IsInWorld())
        m_player->SetRooted(paused);
    m_player->SetTimeFrozen(paused);

    if (Map* map = m_player->FindMap())
    {
        for (ObjectGuid const& guid : m_mobs)
            if (Creature* mob = map->GetCreature(guid))
                if (BonkMobAI* ai = dynamic_cast<BonkMobAI*>(mob->AI()))
                    ai->SetFrozen(paused);

        // The client fills a warning on its own clock, so one held over a
        // pause would look full long before it fired: drop them instead.
        if (paused)
            m_telegraphs.Clear(map);

        // Beasts hold still too; they find their prey again after.
        for (OwnedWeapon const& owned : m_weapons)
            for (ObjectGuid const& guid : owned.pets)
                if (Creature* pet = map->GetCreature(guid))
                {
                    if (paused)
                    {
                        pet->AttackStop();
                        pet->GetMotionMaster()->Clear(false);
                        pet->GetMotionMaster()->MoveIdle();
                    }
                    pet->SetTimeFrozen(paused && pet->IsAlive());
                }
    }
}

void BonkRun::OnMobStuck(Creature* mob)
{
    if (m_state != STATE_RUNNING || m_paused)
        return;

    // Where new enemies appear, so never right in front of the player.
    float x, y, z;
    if (FindMobPosition(x, y, z, m_map.spawnMinDistance, m_map.spawnMaxDistance))
        MoveMob(mob, x, y, z);
}

void BonkRun::CleanUp()
{
    SetPaused(false);
    m_player->SetTimeFrozen(false);
    m_player->GetBonkPlayer()->ClearSpellTweaks();
    DespawnMobs();
    DespawnObjects();
    DespawnOrbs();
    m_player->SetCreatureSummonLimit(sWorld.getConfig(CONFIG_UINT32_CREATURE_SUMMON_LIMIT));
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MOVE_SPEED);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_MAX_HEALTH);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_DODGE);
    m_player->RemoveAurasDueToSpell(BONK_SPELL_SHIELD_VISUAL);
    m_shield = 0;
    m_player->GetBonkPlayer()->SendAddon("shield~0~0");
    SetNecroticStacks(0);
    DespawnPools();
    DespawnWeaponOrbs();
    m_pendingCasts.clear();
    m_telegraphs.Clear(m_player->FindMap());
    DespawnTraps();
    DespawnPets();
    m_marks.clear();
    m_buffs.clear();
    m_retaliations.clear();
    m_retaliateUntil = 0;
    m_dodged = false;
    m_bleedHeal = 0.0f;
    m_spellShield = 0;
    m_chestChoices.clear();
    m_player->GetBonkPlayer()->SendAddon("mobclear");
    SetMovementImmunity(m_player, false);
}

void BonkRun::Exit()
{
    CleanUp();
    m_state = STATE_FINISHED;

    BonkPlayer* bonk = m_player->GetBonkPlayer();
    bonk->ResetCharacter();
    bonk->TeleportToCamp();
}

void BonkRun::Abort()
{
    if (m_state == STATE_FINISHED)
        return;

    // A run that ended before the logout was recorded by Finish already.
    if (m_state == STATE_RUNNING || m_state == STATE_PORTAL || (m_state == STATE_ENTERING && m_stage > 1))
    {
        m_player->GetBonkPlayer()->RecordRun(m_stage, m_totalMs / IN_MILLISECONDS, m_kills, m_goldEarned);
        AwardMastery();
    }

    CleanUp();
    m_state = STATE_FINISHED;
    m_player->GetBonkPlayer()->ResetCharacter();
}

void BonkRun::Surrender()
{
    if (m_state == STATE_RUNNING)
        Finish(false);
    else if (m_state == STATE_PORTAL)
        Finish(true);
    else if (m_state == STATE_ENTERING)
        Exit();
}
