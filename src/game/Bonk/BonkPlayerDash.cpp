#include "BonkPlayer.h"
#include "BonkRun.h"
#include "BonkQuests.h"
#include "Anticheat.h"
#include "Map.h"
#include "MoveSpline.h"
#include "MovementPacketSender.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>

namespace
{
    float const BONK_DASH_DISTANCE          = 8.0f;
    uint32 const BONK_DASH_MS               = 200;
    uint32 const BONK_DASH_INVULNERABLE_MS  = 250;
    uint32 const BONK_DASH_COOLDOWN_MS      = 10000;
    uint32 const BONK_DASH_MIN_COOLDOWN_MS  = 3000;
    uint32 const BONK_DASH_CHARGES          = 1;
    uint32 const BONK_DASH_MAX_AGE_MS       = 1000;
    uint32 const BONK_KIT_BLINK_LEAVE       = 267;
    uint32 const BONK_KIT_BLINK_ARRIVE      = 3394;
}

float BonkPlayer::GetDashDistance() const
{
    return BONK_DASH_DISTANCE + GetUpgradeUtility(BONK_UTILITY_DASH_RANGE);
}

uint32 BonkPlayer::GetDashCooldownMs() const
{
    int32 const cooldown = int32(BONK_DASH_COOLDOWN_MS) - int32(GetUpgradeUtility(BONK_UTILITY_DASH_COOLDOWN) * IN_MILLISECONDS);
    float const blinkRate = m_run ? m_run->GetDashRechargeRate() * m_run->GetGlyphDashRecharge() : 1.0f;
    return uint32(std::max(int32(cooldown / blinkRate), int32(BONK_DASH_MIN_COOLDOWN_MS)));
}

uint32 BonkPlayer::GetDashMaxCharges() const
{
    uint32 const blinkCharges = m_run ? m_run->GetBlinkCharges() + m_run->GetGlyphDashCharges() : 0;
    return BONK_DASH_CHARGES + uint32(GetUpgradeUtility(BONK_UTILITY_DASH_CHARGES)) + blinkCharges;
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
    MovementInfo const& mi = m_player->m_movementInfo;
    auto const refuse = [this, &mi](char const* reason)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bonk dash debug: %s refused (%s), spent %u/%u, flags 0x%x", m_player->GetName(), reason,
            m_dashSpent, GetDashMaxCharges(), mi.moveFlags);
        return false;
    };
    if (m_run && ((!m_run->IsActive() && !m_run->IsPortalOpen()) || m_run->IsPaused()))
        return refuse("run inactive or paused");
    if (m_dashSpent >= GetDashMaxCharges())
        return refuse("no charges");
    if (!m_player->IsInWorld() || !m_player->IsAlive() || m_player->IsBeingTeleported() || m_player->IsTaxiFlying())
        return refuse("not in world, dead, teleporting or flying");
    if (m_player->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
        return refuse("stunned or rooted");
    if (!m_player->movespline->Finalized())
        return refuse("spline running");
    if (m_player->HasPendingMovementChange(KNOCK_BACK))
        return refuse("knockback pending");
    if (mi.moveFlags & (MOVEFLAG_ROOT | MOVEFLAG_SWIMMING | MOVEFLAG_ONTRANSPORT))
        return refuse("root, swimming or transport flag");
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "Bonk dash debug: %s dashes, spent %u/%u, flags 0x%x", m_player->GetName(),
        m_dashSpent, GetDashMaxCharges(), mi.moveFlags);

    bool const blink = m_run && m_run->IsBlinkDash();
    if (m_run)
        m_run->OnDash();

    m_dashRunTracked = false;
    if (blink)
        BlinkHero();
    else if (mi.moveFlags & (MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR))
    {
        float facing = m_player->GetOrientation();
        uint32 const age = std::min(WorldTimer::getMSTimeDiffToNow(mi.stime), BONK_DASH_MAX_AGE_MS);
        float x, y, z, o;
        if (m_player->ExtrapolateMovement(mi, age, x, y, z, o))
            facing = o;
        ThrowHero(facing, GetDashSpeed());
    }
    else
    {
        TrackDashRun();
        m_dashBurstUntilMs = m_dashClockMs + BONK_DASH_MS;
        SetDashBurst(true);
    }

    if (!m_dashSpent++)
        m_dashRechargeMs = GetDashCooldownMs();
    m_dashInvulnerableUntilMs = m_dashClockMs + std::max(BONK_DASH_INVULNERABLE_MS, blink ? m_run->GetBlinkInvulnerableMs() : 0u) +
        (m_run ? m_run->GetVanishMs() : 0u);
    if (m_run && !m_run->IsSandbox())
        sBonkQuests.OnProgress(m_player, BONK_QUEST_DASHES, 0, 1);
    SendDash();
    return true;
}

void BonkPlayer::TrackDashRun()
{
    if (!m_dashBursting)
        m_dashRunBaseSpeed = m_player->GetSpeedForMovementInfo(m_player->m_movementInfo);
    m_dashRunStartedAtMs = WorldTimer::getMSTime() + m_player->GetSession()->GetLatency();
    m_dashRunTracked = true;
}

bool BonkPlayer::GetDashRunTravel(uint32 fromMs, uint32 toMs, float& travel) const
{
    if (!m_dashRunTracked)
        return false;
    uint32 const dashEndMs = m_dashRunStartedAtMs + BONK_DASH_MS;
    if (fromMs >= dashEndMs && !m_dashBursting)
        return false;
    uint32 const burstFromMs = std::max(fromMs, m_dashRunStartedAtMs);
    uint32 const burstToMs = std::min(toMs, dashEndMs);
    uint32 const burstMs = burstToMs > burstFromMs ? burstToMs - burstFromMs : 0;
    travel = (m_dashRunBaseSpeed * (toMs - fromMs) + (GetDashSpeed() - m_dashRunBaseSpeed) * burstMs) / IN_MILLISECONDS;
    return true;
}

void BonkPlayer::SetDashBurst(bool on)
{
    m_dashBursting = on;
    if (m_player->IsInWorld())
        for (UnitMoveType type : { MOVE_WALK, MOVE_RUN, MOVE_RUN_BACK })
            m_player->UpdateSpeed(type, false);
}

void BonkPlayer::ThrowHero(float angle, float speed)
{
    float const vcos = std::cos(angle);
    float const vsin = std::sin(angle);
    MovementPacketSender::SendKnockBackToController(m_player, vcos, vsin, speed, 0.0f);
    m_player->SetJumpInitialSpeed(0.0f);
    if (Player* mover = m_player->GetPlayerMovingMe())
        if (MovementAnticheat* cheatData = mover->GetCheatData())
            cheatData->OnKnockBack(m_player, speed, 0.0f, vcos, vsin);
}

float BonkPlayer::GetMoveAngle() const
{
    MovementInfo const& mi = m_player->m_movementInfo;
    float facing = m_player->GetOrientation();
    float x, y, z, o;
    uint32 const age = std::min(WorldTimer::getMSTimeDiffToNow(mi.stime), BONK_DASH_MAX_AGE_MS);
    if (m_player->ExtrapolateMovement(mi, age, x, y, z, o))
        facing = o;

    float forward = 0.0f, left = 0.0f;
    if (mi.moveFlags & MOVEFLAG_FORWARD)
        forward += 1.0f;
    if (mi.moveFlags & MOVEFLAG_BACKWARD)
        forward -= 1.0f;
    if (mi.moveFlags & MOVEFLAG_STRAFE_LEFT)
        left += 1.0f;
    if (mi.moveFlags & MOVEFLAG_STRAFE_RIGHT)
        left -= 1.0f;
    if (forward == 0.0f && left == 0.0f)
        return facing;
    return facing + std::atan2(left, forward);
}

void BonkPlayer::BlinkHero()
{
    MovementInfo const& mi = m_player->m_movementInfo;
    float x = m_player->GetPositionX(), y = m_player->GetPositionY(), z = m_player->GetPositionZ(), o;
    uint32 const age = std::min(WorldTimer::getMSTimeDiffToNow(mi.stime), BONK_DASH_MAX_AGE_MS);
    m_player->ExtrapolateMovement(mi, age, x, y, z, o);

    float const angle = GetMoveAngle();
    float destX = x + GetDashDistance() * std::cos(angle);
    float destY = y + GetDashDistance() * std::sin(angle);
    float destZ = z;
    m_player->GetMap()->GetWalkHitPosition(nullptr, x, y, z, destX, destY, destZ);

    m_player->SendPlaySpellVisualKit(BONK_KIT_BLINK_LEAVE);
    m_player->NearTeleportTo(destX, destY, destZ, m_player->GetOrientation(),
        TELE_TO_NOT_LEAVE_TRANSPORT | TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET | TELE_TO_SPELL);
    m_player->SendPlaySpellVisualKit(BONK_KIT_BLINK_ARRIVE);
}

void BonkPlayer::UpdateDash(uint32 diff)
{
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
        std::to_string(m_dashSpent ? m_dashRechargeMs : 0) + "~" + std::to_string(GetDashCooldownMs()) + "~" +
        (m_run && m_run->IsBlinkDash() ? "1" : "0"));
}
