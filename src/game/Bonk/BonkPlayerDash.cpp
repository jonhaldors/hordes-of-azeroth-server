/*
 * Vanilla Bonk: the dash, every class's escape move (feature plan, Balance
 * spec, 4. Sustain kit), at camp and in runs. A player control on Shift, not
 * a spell (Spells.md, Core rules): spells still never move the hero.
 *
 * It covers BONK_DASH_DISTANCE in BONK_DASH_MS, enemy hits do nothing for
 * BONK_DASH_INVULNERABLE_MS, and each of its BONK_DASH_CHARGES charges comes
 * back BONK_DASH_COOLDOWN_MS after it was used, one at a time. Range,
 * cooldown and extra charges (upgrades, late boots) are planned for later.
 *
 * How it moves (VanillaBonkClientFix.dll does the client's half, knowing a
 * dash by its speed: no hero runs at 20 yards a second otherwise):
 * - On the ground, a burst of speed: walk, run and run-back speed are the
 *   dash's (distance / time) for BONK_DASH_MS (Unit::UpdateSpeed), whatever
 *   slows the hero. The client goes on the way the keys move it, along the
 *   ground and sliding along walls as at any speed, and keeps the keys held.
 *   A hero standing still is set moving forward by the DLL for the burst and
 *   stopped after, unless a key held by then moves them on. A burst that ends
 *   in the air (a jump during it, or off a ledge) is cut to run speed by the
 *   DLL, since the client keeps a jump's speed whatever the speeds do.
 * - In the air, the way the hero faces: a knockback at the dash's speed with
 *   no lift, which the DLL flies level for BONK_DASH_MS; then the hero falls
 *   on at run speed. It is sent as Unit::KnockBack sends it, less its
 *   interrupt, which would cut a channel (Volley). The DLL moves the hero on
 *   with the keys still held once they land, as after any knockback.
 *
 * The addon binds Shift (VanillaBonkDash.lua) and sends `.bonk dash`, which
 * the map update runs right after that tick's movement packets, so the
 * hero's movement and facing are as fresh as the server has them. A dash it
 * can't make (a paused run, stunned, swimming, no charge) is dropped quietly.
 *
 * Addon: "dash~charges~maxCharges~msUntilNextCharge~cooldownMs" whenever a
 * charge is used or comes back, at hello, after a run and with a run's full
 * state.
 */

#include "BonkPlayer.h"
#include "BonkRun.h"
#include "Anticheat.h"
#include "MoveSpline.h"
#include "MovementPacketSender.h"
#include "Player.h"

#include <algorithm>
#include <cmath>

namespace
{
    float const BONK_DASH_DISTANCE          = 8.0f;     // yards
    uint32 const BONK_DASH_MS               = 200;      // 40 yards a second
    uint32 const BONK_DASH_INVULNERABLE_MS  = 250;
    uint32 const BONK_DASH_COOLDOWN_MS      = 10000;    // until a used charge is back
    uint32 const BONK_DASH_CHARGES          = 1;
    uint32 const BONK_DASH_MAX_AGE_MS       = 1000;     // the last movement packet carried forward at most this far
}

float BonkPlayer::GetDashDistance() const
{
    return BONK_DASH_DISTANCE;
}

uint32 BonkPlayer::GetDashCooldownMs() const
{
    return BONK_DASH_COOLDOWN_MS;
}

uint32 BonkPlayer::GetDashMaxCharges() const
{
    return BONK_DASH_CHARGES;
}

float BonkPlayer::GetDashSpeed() const
{
    return GetDashDistance() * IN_MILLISECONDS / BONK_DASH_MS;
}

bool BonkPlayer::IsDashPaused() const
{
    return m_run && m_run->IsPaused();
}

bool BonkPlayer::Dash()
{
    // In a run, only while it plays (and at its portal); anywhere outside one.
    if (m_run && ((!m_run->IsActive() && !m_run->IsPortalOpen()) || m_run->IsPaused()))
        return false;
    if (m_dashSpent >= GetDashMaxCharges())
        return false;
    if (!m_player->IsInWorld() || !m_player->IsAlive() || m_player->IsBeingTeleported() || m_player->IsTaxiFlying())
        return false;
    // As Unit::KnockBack refuses; and one knockback at a time (the last
    // dash's or a charger's, until its ack is in).
    if (m_player->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT) || !m_player->movespline->Finalized() ||
        m_player->HasPendingMovementChange(KNOCK_BACK))
        return false;

    MovementInfo const& mi = m_player->m_movementInfo;
    if (mi.moveFlags & (MOVEFLAG_ROOT | MOVEFLAG_SWIMMING | MOVEFLAG_ONTRANSPORT))
        return false;

    if (mi.moveFlags & (MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR))
    {
        // The way the hero faces, as their client has it: the last movement
        // packet carried forward by its age (turning keys held).
        float facing = m_player->GetOrientation();
        uint32 const age = std::min(WorldTimer::getMSTimeDiffToNow(mi.stime), BONK_DASH_MAX_AGE_MS);
        float x, y, z, o;
        if (m_player->ExtrapolateMovement(mi, age, x, y, z, o))
            facing = o;
        ThrowHero(facing, GetDashSpeed());
    }
    else
    {
        m_dashBurstUntilMs = m_dashClockMs + BONK_DASH_MS;
        SetDashBurst(true);
    }

    if (!m_dashSpent++)
        m_dashRechargeMs = GetDashCooldownMs();
    m_dashInvulnerableUntilMs = m_dashClockMs + BONK_DASH_INVULNERABLE_MS;
    SendDash();
    return true;
}

void BonkPlayer::SetDashBurst(bool on)
{
    m_dashBursting = on;
    // Unit::UpdateSpeed puts in the burst's speed, or takes it out; walk too,
    // since walking moves at the lower of walk and run speed.
    if (m_player->IsInWorld())
        for (UnitMoveType type : { MOVE_WALK, MOVE_RUN, MOVE_RUN_BACK })
            m_player->UpdateSpeed(type, false);
}

void BonkPlayer::ThrowHero(float angle, float speed)
{
    // Unit::KnockBack with no lift, less its InterruptNonMeleeSpells: the
    // knockback packet (the client acks it with its new movement), the jump
    // speed the server carries the hero's arc forward with, and the
    // anticheat's note.
    float const vcos = std::cos(angle);
    float const vsin = std::sin(angle);
    MovementPacketSender::SendKnockBackToController(m_player, vcos, vsin, speed, 0.0f);
    m_player->SetJumpInitialSpeed(0.0f);
    if (Player* mover = m_player->GetPlayerMovingMe())
        if (MovementAnticheat* cheatData = mover->GetCheatData())
            cheatData->OnKnockBack(m_player, speed, 0.0f, vcos, vsin);
}

void BonkPlayer::UpdateDash(uint32 diff)
{
    // The dash's clock stops with a paused run, as the hero does (speed 0,
    // and the DLL holds them in the air), so its charges, invulnerability
    // and burst all wait out a pause.
    if (IsDashPaused())
        return;
    m_dashClockMs += diff;

    if (m_dashSpent)
    {
        if (m_dashRechargeMs > diff)
            m_dashRechargeMs -= diff;
        else
        {
            --m_dashSpent;
            m_dashRechargeMs = m_dashSpent ? GetDashCooldownMs() : 0;
            SendDash();
        }
    }

    if (m_dashBursting && m_dashClockMs >= m_dashBurstUntilMs)
        SetDashBurst(false);
}

void BonkPlayer::SendDash() const
{
    uint32 const maxCharges = GetDashMaxCharges();
    SendAddon("dash~" + std::to_string(maxCharges - std::min(m_dashSpent, maxCharges)) + "~" + std::to_string(maxCharges) + "~" +
        std::to_string(m_dashSpent ? m_dashRechargeMs : 0) + "~" + std::to_string(GetDashCooldownMs()));
}
