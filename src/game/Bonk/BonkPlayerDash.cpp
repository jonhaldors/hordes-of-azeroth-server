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
    float const BONK_DASH_DISTANCE          = 8.0f;
    uint32 const BONK_DASH_MS               = 200;
    uint32 const BONK_DASH_INVULNERABLE_MS  = 250;
    uint32 const BONK_DASH_COOLDOWN_MS      = 10000;
    uint32 const BONK_DASH_MIN_COOLDOWN_MS  = 3000;
    uint32 const BONK_DASH_CHARGES          = 1;
    uint32 const BONK_DASH_MAX_AGE_MS       = 1000;
}

float BonkPlayer::GetDashDistance() const
{
    return BONK_DASH_DISTANCE + GetUpgradeUtility(BONK_UTILITY_DASH_RANGE);
}

uint32 BonkPlayer::GetDashCooldownMs() const
{
    int32 const cooldown = int32(BONK_DASH_COOLDOWN_MS) - int32(GetUpgradeUtility(BONK_UTILITY_DASH_COOLDOWN) * IN_MILLISECONDS);
    return uint32(std::max(cooldown, int32(BONK_DASH_MIN_COOLDOWN_MS)));
}

uint32 BonkPlayer::GetDashMaxCharges() const
{
    return BONK_DASH_CHARGES + uint32(GetUpgradeUtility(BONK_UTILITY_DASH_CHARGES));
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
    if (m_run && ((!m_run->IsActive() && !m_run->IsPortalOpen()) || m_run->IsPaused()))
        return false;
    if (m_dashSpent >= GetDashMaxCharges())
        return false;
    if (!m_player->IsInWorld() || !m_player->IsAlive() || m_player->IsBeingTeleported() || m_player->IsTaxiFlying())
        return false;
    if (m_player->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT) || !m_player->movespline->Finalized() ||
        m_player->HasPendingMovementChange(KNOCK_BACK))
        return false;

    MovementInfo const& mi = m_player->m_movementInfo;
    if (mi.moveFlags & (MOVEFLAG_ROOT | MOVEFLAG_SWIMMING | MOVEFLAG_ONTRANSPORT))
        return false;

    if (mi.moveFlags & (MOVEFLAG_JUMPING | MOVEFLAG_FALLINGFAR))
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
        std::to_string(m_dashSpent ? m_dashRechargeMs : 0) + "~" + std::to_string(GetDashCooldownMs()));
}
