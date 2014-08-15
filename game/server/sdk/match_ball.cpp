#include "cbase.h"
#include "match_ball.h"
#include "team.h"
#include "ios_replaymanager.h"

LINK_ENTITY_TO_CLASS( match_ball, CMatchBall );

BEGIN_DATADESC(	CMatchBall )
END_DATADESC()

IMPLEMENT_SERVERCLASS_ST( CMatchBall, DT_MatchBall )
	SendPropEHandle(SENDINFO(m_pLastActivePlayer)),
	SendPropInt(SENDINFO(m_nLastActiveTeam)),
	SendPropInt(SENDINFO(m_bNonnormalshotsBlocked)),
	SendPropInt(SENDINFO(m_bShotsBlocked)),
END_SEND_TABLE()

CMatchBall *CreateMatchBall(const Vector &pos)
{
	CMatchBall *pBall = static_cast<CMatchBall *>(CreateEntityByName("match_ball"));
	pBall->SetAbsOrigin(pos);
	pBall->Spawn();
	pBall->SetPos(pos);

	return pBall;
}

CMatchBall *g_pMatchBall = NULL;

CMatchBall *GetMatchBall()
{
	return g_pMatchBall;
}

CMatchBall::CMatchBall()
{
	g_pMatchBall = NULL;
	m_pLastActivePlayer = NULL;
	m_nLastActiveTeam = TEAM_UNASSIGNED;
	m_pPossessingPl = NULL;
	m_nPossessingTeam = TEAM_INVALID;
	m_flPossessionStart = -1;
	m_pTurnoverPlayer = NULL;
	m_bNonnormalshotsBlocked = false;
	m_bShotsBlocked = false;
	m_ePenaltyState = PENALTY_NONE;
}

CMatchBall::~CMatchBall()
{

}

void CMatchBall::Spawn()
{
	//TODO: Move the ball skin parsing to a better spot
	CBallInfo::ParseBallSkins();
	
	g_pMatchBall = this;

	CBall::Spawn();
}

void CMatchBall::Reset()
{
	CBall::Reset();

	m_pLastActivePlayer = NULL;
	m_pSetpieceTaker = NULL;
	m_flStateLeaveTime = gpGlobals->curtime;
	m_flStateActivationDelay = 0;
	m_pOtherPl = NULL;
	m_pPossessingPl = NULL;
	m_nPossessingTeam = TEAM_INVALID;
	m_flPossessionStart = -1;
	m_pTurnoverPlayer = NULL;
	m_bNonnormalshotsBlocked = false;
	m_bShotsBlocked = false;
	m_ePenaltyState = PENALTY_NONE;

	UnmarkOffsidePlayers();
	RemoveAllTouches();
}

void CMatchBall::State_Transition(ball_state_t newState, float delay /*= 0.0f*/, bool cancelQueuedState /*= false*/, bool isShortMessageDelay /*= false*/)
{
	if (delay == 0)
	{
		State_Leave(newState);
		State_Enter(newState, cancelQueuedState);
	}
	else
	{
		m_eNextState = newState;
		m_flStateActivationDelay = delay;
		m_flStateLeaveTime = gpGlobals->curtime + m_flStateActivationDelay + (isShortMessageDelay ? sv_ball_statetransition_messagedelay_short : sv_ball_statetransition_messagedelay_normal).GetFloat();
		m_bHasQueuedState = true;
	}
}

void CMatchBall::State_Enter(ball_state_t newState, bool cancelQueuedState)
{
	if (cancelQueuedState)
	{
		m_eNextState = BALL_STATE_NONE;
		m_bHasQueuedState = false;
	}

	m_eBallState = newState;
	m_pCurStateInfo = State_LookupInfo( newState );

	m_flStateEnterTime = gpGlobals->curtime;
	m_flStateTimelimit = -1;
	m_bNextStateMessageSent = false;

	m_pPl = NULL;
	m_pOtherPl = NULL;
	m_pSetpieceTaker = NULL;

	IGameEvent *pEvent = gameeventmanager->CreateEvent("ball_state");
	if (pEvent)
	{
		pEvent->SetInt("state", State_Get());
		gameeventmanager->FireEvent(pEvent);
	}

	// Initialize the new state.
	if ( m_pCurStateInfo && m_pCurStateInfo->pfnEnterState )
	{
		(this->*m_pCurStateInfo->pfnEnterState)();
	}

	//State_Think();
}

void CMatchBall::State_Leave(ball_state_t newState)
{
	SDKGameRules()->DisableShield();

	if ( m_pCurStateInfo && m_pCurStateInfo->pfnLeaveState )
	{
		(this->*m_pCurStateInfo->pfnLeaveState)(newState);
	}
}

void CMatchBall::State_Think()
{
	m_pPhys->GetPosition(&m_vPos, &m_aAng);
	m_pPhys->GetVelocity(&m_vVel, &m_vRot);

	if (m_eNextState != BALL_STATE_NONE)
	{
		if (!m_bNextStateMessageSent && gpGlobals->curtime >= m_flStateLeaveTime - m_flStateActivationDelay && m_eNextState != BALL_STATE_KICKOFF)
		{
			SendNotifications();
			m_bNextStateMessageSent = true;
		}
		else if (gpGlobals->curtime >= m_flStateLeaveTime)
		{
			State_Leave(m_eNextState);
			State_Enter(m_eNextState, true);
		}
	}

	if (State_Get() == BALL_STATE_THROWIN || State_Get() == BALL_STATE_CORNER || State_Get() == BALL_STATE_GOALKICK)
	{
		if (SDKGameRules()->CheckTimeout())
			m_flStateTimelimit = -1;
	}

	if (m_pCurStateInfo
		&& State_Get() != BALL_STATE_NORMAL
		&& m_eNextState == BALL_STATE_NONE
		&& CSDKPlayer::IsOnField(m_pPl)
		&& m_flStateTimelimit != -1
		&& gpGlobals->curtime >= m_flStateTimelimit) // Player is afk or timed out
	{
		m_pPl->SetDesiredTeam(TEAM_SPECTATOR, m_pPl->GetTeamNumber(), 0, true, false, false);
	}

	if (m_pCurStateInfo && m_pCurStateInfo->pfnThink)
	{	
		(this->*m_pCurStateInfo->pfnThink)();
	}

	if (m_pPl) // Set info for the client
	{
		m_pLastActivePlayer = m_pPl;
		m_nLastActiveTeam = m_pPl->GetTeamNumber();
	}
}

void CMatchBall::State_STATIC_Enter()
{
	SDKGameRules()->StopMeteringInjuryTime();
}

void CMatchBall::State_STATIC_Think()
{
}

void CMatchBall::State_STATIC_Leave(ball_state_t newState)
{
	if (!SDKGameRules()->IsIntermissionState())
		SDKGameRules()->StartMeteringInjuryTime();
}

void CMatchBall::State_NORMAL_Enter()
{
	m_pPhys->EnableMotion(true);
	EnablePlayerCollisions(true);
	m_pPhys->Wake();
	SDKGameRules()->StopMeteringInjuryTime();
	//SetMatchEvent(MATCH_EVENT_NONE);
	//SetMatchSubEvent(MATCH_EVENT_NONE);
}

extern ConVar sv_ball_advantage_duration;

void CMatchBall::State_NORMAL_Think()
{
	if (m_eNextState == BALL_STATE_GOAL)
		return;

	if (SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
	{
		if (m_ePenaltyState == PENALTY_KICKED)
			m_pPl = FindNearestPlayer(m_nFoulingTeam, FL_POS_KEEPER, true);
		else
			m_pPl = NULL;

		if (!m_pPl)
			return;

		UpdateCarrier();
		DoBodyPartAction();
	}
	else
	{
		if (!SDKGameRules()->IsIntermissionState() && !m_bHasQueuedState)
		{
			if (m_flPossessionStart != -1
				&& CSDKPlayer::IsOnField(m_pPossessingPl)
				&& gpGlobals->curtime > m_flPossessionStart + sv_ball_turnovertime.GetFloat())
			{
				if (CSDKPlayer::IsOnField(m_pTurnoverPlayer) && m_pTurnoverPlayer->GetTeamNumber() != m_nPossessingTeam)
				{
					m_pTurnoverPlayer->AddTurnover();
				}

				m_pTurnoverPlayer = m_pPossessingPl;
			}

			for (int i = 1; i <= gpGlobals->maxClients; i++)
			{
				CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

				if (!CSDKPlayer::IsOnField(pPl) || !pPl->IsOffside())
					continue;

				Vector dir = (m_vPos - pPl->GetLocalOrigin());

				if (dir.z > sv_ball_offsidedist.GetFloat() || dir.Length2DSqr() > pow(sv_ball_offsidedist.GetFloat(), 2))
					continue;

				pPl->AddOffside();
				TriggerFoul(FOUL_OFFSIDE, pPl->GetOffsidePos(), pPl);
				SDKGameRules()->SetOffsideLinePositions(pPl->GetOffsideBallPos().y, pPl->GetOffsidePos().y, pPl->GetOffsideLastOppPlayerPos().y);
				State_Transition(BALL_STATE_FREEKICK, sv_ball_statetransition_activationdelay_long.GetFloat());
			
				return;
			}
		}

		for (int ignoredPlayerBits = 0;;)
		{
			m_pPl = FindNearestPlayer(TEAM_INVALID, FL_POS_ANY, false, ignoredPlayerBits, sv_ball_maxcheckdist.GetFloat());

			if (!m_pPl)
				return;

			UpdateCarrier();

			// The current player was able to perform an action, so exit the loop
			if (DoBodyPartAction())
				break;

			// Exclude the current player from subsequent checks
			ignoredPlayerBits |= (1 << (m_pPl->entindex() - 1));

			m_pPl = NULL;
		}
	}
}

void CMatchBall::State_NORMAL_Leave(ball_state_t newState)
{
	UnmarkOffsidePlayers();
	UpdatePossession(NULL);

	if (!SDKGameRules()->IsIntermissionState())
		SDKGameRules()->StartMeteringInjuryTime();
}

void CMatchBall::State_THROWIN_Enter()
{
	EnablePlayerCollisions(false);
	SetPos(Vector(m_vTriggerTouchPos.x + 0 * Sign(SDKGameRules()->m_vKickOff.GetX() - m_vTriggerTouchPos.x), m_vTriggerTouchPos.y, SDKGameRules()->m_vKickOff.GetZ()));
}

void CMatchBall::State_THROWIN_Think()
{
	if (!CSDKPlayer::IsOnField(m_pPl, LastOppTeam(false)))
	{
		m_pPl = NULL;

		if (CSDKPlayer::IsOnField(m_pSetpieceTaker, LastOppTeam(false)))
			m_pPl = m_pSetpieceTaker;

		if (!m_pPl)
			m_pPl = FindNearestPlayer(LastOppTeam(false));
		if (!m_pPl)
			return State_Transition(BALL_STATE_NORMAL);

		SDKGameRules()->EnableShield(SHIELD_THROWIN, m_pPl->GetTeamNumber(), m_vPos);
		m_pPl->SetPosInsideShield(Vector(m_vTriggerTouchPos.x, m_vTriggerTouchPos.y, SDKGameRules()->m_vKickOff.GetZ()), true);
		m_flStateTimelimit = -1;
		m_bShotsBlocked = true;
	}

	if (!PlayersAtTargetPos())
		return;

	if (m_flStateTimelimit == -1)
	{
		m_flStateTimelimit = gpGlobals->curtime + sv_ball_timelimit_setpiece.GetFloat();
		SetPos(Vector(m_vTriggerTouchPos.x, m_vTriggerTouchPos.y, SDKGameRules()->m_vKickOff.GetZ() + m_pPl->GetPlayerMaxs().z + 2));
		m_pPl->RemoveFlag(FL_ATCONTROLS);
		m_pPl->DoServerAnimationEvent(PLAYERANIMEVENT_THROWIN);
		m_pPl->SetShotButtonsReleased(false);
		m_bShotsBlocked = false;
	}

	UpdateCarrier();

	//Vector handPos;
	//QAngle handAng;
	//m_pPl->GetAttachment("ballrighthand", handPos, handAng);
	//handPos.z -= BALL_PHYS_RADIUS;
	//SetPos(handPos, false);
	//SetAng(handAng);

	if (m_pPl->ShotButtonsReleased() && (m_pPl->IsPowershooting() || m_pPl->IsChargedshooting()))
	{
		QAngle ang = m_aPlAng;

		ang[PITCH] = min(sv_ball_throwin_minangle.GetFloat(), m_aPlAng[PITCH]);

		Vector dir;
		AngleVectors(ang, &dir);
		float strength;

		if (m_pPl->IsPowershooting())
			strength = GetPowershotStrength(GetPitchCoeff(false), sv_ball_powerthrow_strength.GetInt());
		else
			strength = GetChargedshotStrength(GetPitchCoeff(false), sv_ball_chargedthrow_minstrength.GetInt(), sv_ball_chargedthrow_maxstrength.GetInt());

		Vector vel = dir * max(strength, sv_ball_throwin_minstrength.GetInt());

		m_pPl->AddThrowIn();
		RemoveAllTouches();
		SetVel(vel, 0, 0, BODY_PART_HANDS, false, false, false);
		State_Transition(BALL_STATE_NORMAL);
	}
}

void CMatchBall::State_THROWIN_Leave(ball_state_t newState)
{
	if (CSDKPlayer::IsOnField(m_pPl))
		m_pPl->DoServerAnimationEvent(PLAYERANIMEVENT_THROW);

	EnablePlayerCollisions(true);
	m_bShotsBlocked = false;
}

void CMatchBall::State_KICKOFF_Enter()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++) 
	{
		CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

		if (!CSDKPlayer::IsOnField(pPl))
			continue;

		pPl->m_Shared.SetStamina(100);
	}

	SetPos(SDKGameRules()->m_vKickOff);
}

void CMatchBall::State_KICKOFF_Think()
{
	if (!CSDKPlayer::IsOnField(m_pPl, SDKGameRules()->GetKickOffTeam()))
	{
		m_pPl = NULL;

		m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_ATTACKER | FL_POS_LEFT);
		if (!m_pPl)
			m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_ATTACKER | FL_POS_CENTER);
		if (!m_pPl)
			m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_ATTACKER | FL_POS_RIGHT);

		if (!m_pPl)
			m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_MIDFIELDER | FL_POS_LEFT);
		if (!m_pPl)
			m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_MIDFIELDER | FL_POS_CENTER);
		if (!m_pPl)
			m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_MIDFIELDER | FL_POS_RIGHT);

		if (!m_pPl)
			m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_DEFENDER | FL_POS_LEFT);
		if (!m_pPl)
			m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_DEFENDER | FL_POS_CENTER);
		if (!m_pPl)
			m_pPl = FindNearestPlayer(SDKGameRules()->GetKickOffTeam(), FL_POS_DEFENDER | FL_POS_RIGHT);

		if (!m_pPl)
		{
			SDKGameRules()->EnableShield(SHIELD_KICKOFF, GetGlobalTeam(TEAM_A)->GetTeamNumber(), SDKGameRules()->m_vKickOff);
			if (!PlayersAtTargetPos())
				return;

			return State_Transition(BALL_STATE_NORMAL);
		}

		SDKGameRules()->EnableShield(SHIELD_KICKOFF, m_pPl->GetTeamNumber(), SDKGameRules()->m_vKickOff);
		m_pPl->SetPosInsideShield(Vector(m_vPos.x - m_pPl->GetTeam()->m_nRight * 30, m_vPos.y, SDKGameRules()->m_vKickOff.GetZ()), true);
		m_flStateTimelimit = -1;
		m_bShotsBlocked = true;

		EmitSound("Ball.Whistle");

		IGameEvent *pEvent = gameeventmanager->CreateEvent("kickoff");
		if (pEvent)
		{
			pEvent->SetInt("team", m_pPl->GetTeamNumber());
			gameeventmanager->FireEvent(pEvent);
		}
	}

	if (!CSDKPlayer::IsOnField(m_pOtherPl, SDKGameRules()->GetKickOffTeam()) || m_pOtherPl == m_pPl)
	{
		m_pOtherPl = NULL;

		m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_ATTACKER | FL_POS_RIGHT, false, (1 << (m_pPl->entindex() - 1)));
		if (!m_pOtherPl)
			m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_ATTACKER | FL_POS_CENTER, false, (1 << (m_pPl->entindex() - 1)));
		if (!m_pOtherPl)
			m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_ATTACKER | FL_POS_LEFT, false, (1 << (m_pPl->entindex() - 1)));

		if (!m_pOtherPl)
			m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_MIDFIELDER | FL_POS_RIGHT, false, (1 << (m_pPl->entindex() - 1)));
		if (!m_pOtherPl)
			m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_MIDFIELDER | FL_POS_CENTER, false, (1 << (m_pPl->entindex() - 1)));
		if (!m_pOtherPl)
			m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_MIDFIELDER | FL_POS_LEFT, false, (1 << (m_pPl->entindex() - 1)));

		if (!m_pOtherPl)
			m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_DEFENDER | FL_POS_RIGHT, false, (1 << (m_pPl->entindex() - 1)));
		if (!m_pOtherPl)
			m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_DEFENDER | FL_POS_CENTER, false, (1 << (m_pPl->entindex() - 1)));
		if (!m_pOtherPl)
			m_pOtherPl = FindNearestPlayer(m_pPl->GetTeamNumber(), FL_POS_DEFENDER | FL_POS_LEFT, false, (1 << (m_pPl->entindex() - 1)));

		if (m_pOtherPl)
		{
			m_pOtherPl->SetPosInsideShield(Vector(m_vPos.x + m_pPl->GetTeam()->m_nRight * 100, m_vPos.y, SDKGameRules()->m_vKickOff.GetZ()), true);
		}
	}

	if (!PlayersAtTargetPos())
		return;

	for (int i = 1; i <= gpGlobals->maxClients; i++) 
	{
		CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

		if (!CSDKPlayer::IsOnField(pPl))
			continue;

		if (pPl->GetFlags() & FL_ATCONTROLS)
		{
			if (pPl != m_pPl && pPl != m_pOtherPl)
				pPl->RemoveFlag(FL_ATCONTROLS);
		}
	}

	if (m_flStateTimelimit == -1)
	{
		m_flStateTimelimit = gpGlobals->curtime + sv_ball_timelimit_setpiece.GetFloat();
		m_pPl->SetShotButtonsReleased(false);
		m_bShotsBlocked = false;
	}

	UpdateCarrier();

	if (m_pPl->ShotButtonsReleased() && m_pPl->IsShooting())
	{
		RemoveAllTouches();
		SetVel(m_vPlForward2D * 350, 0, 0, BODY_PART_FEET, false, false, false);
		m_pPl->DoServerAnimationEvent(PLAYERANIMEVENT_BLANK);
		m_pPl->RemoveFlag(FL_ATCONTROLS);
		if (m_pOtherPl)
			m_pOtherPl->RemoveFlag(FL_ATCONTROLS);
		State_Transition(BALL_STATE_NORMAL);
	}
}

void CMatchBall::State_KICKOFF_Leave(ball_state_t newState)
{
	m_bShotsBlocked = false;
}

void CMatchBall::State_GOALKICK_Enter()
{
	Vector ballPos;
	if (m_vTriggerTouchPos.x - SDKGameRules()->m_vKickOff.GetX() > 0)
		ballPos = GetGlobalTeam(LastOppTeam(false))->m_vGoalkickLeft;
	else
		ballPos = GetGlobalTeam(LastOppTeam(false))->m_vGoalkickRight;

	SetPos(ballPos);
}

void CMatchBall::State_GOALKICK_Think()
{
	if (!CSDKPlayer::IsOnField(m_pPl, LastOppTeam(false)))
	{
		m_pPl = NULL;

		if (CSDKPlayer::IsOnField(m_pSetpieceTaker, LastOppTeam(false)))
			m_pPl = m_pSetpieceTaker;

		if (!m_pPl)
			m_pPl = FindNearestPlayer(LastOppTeam(false), FL_POS_KEEPER);

		if (!m_pPl)
			m_pPl = FindNearestPlayer(LastOppTeam(false));

		if (!m_pPl)
			return State_Transition(BALL_STATE_NORMAL);

		SDKGameRules()->EnableShield(SHIELD_GOALKICK, m_pPl->GetTeamNumber(), m_vPos);
		m_pPl->SetPosInsideShield(Vector(m_vPos.x, m_vPos.y - 100 * m_pPl->GetTeam()->m_nForward, SDKGameRules()->m_vKickOff.GetZ()), true);
		m_flStateTimelimit = -1;
		m_bShotsBlocked = true;
	}

	if (!PlayersAtTargetPos())
		return;

	UpdateCarrier();

	if (m_flStateTimelimit == -1)
	{
		m_flStateTimelimit = gpGlobals->curtime + sv_ball_timelimit_setpiece.GetFloat();
		m_pPl->RemoveFlag(FL_ATCONTROLS);
		m_bShotsBlocked = false;
		m_pPl->SetShotButtonsReleased(false);
	}

	if (IsPlayerClose())
	{
		if (m_flSetpieceCloseStartTime == -1)
			m_flSetpieceCloseStartTime = gpGlobals->curtime;
	}
	else
		m_flSetpieceCloseStartTime = -1;

	if (m_flSetpieceCloseStartTime != -1 && gpGlobals->curtime > m_flSetpieceCloseStartTime + sv_ball_setpiece_close_time.GetFloat())
	{
		m_bNonnormalshotsBlocked = true;
	}
	else
		m_bNonnormalshotsBlocked = false;

	if (!m_bShotsBlocked
		&& m_pPl->ShotButtonsReleased()
		&& CanTouchBallXY()
		&& (m_pPl->IsNormalshooting() || !m_bNonnormalshotsBlocked && (m_pPl->IsPowershooting() || m_pPl->IsChargedshooting())))
	{
		m_pPl->AddGoalKick();
		RemoveAllTouches();
		DoGroundShot(false, sv_ball_goalkick_speedcoeff.GetFloat());
		State_Transition(BALL_STATE_NORMAL);
	}
}

void CMatchBall::State_GOALKICK_Leave(ball_state_t newState)
{
	m_bNonnormalshotsBlocked = false;
	m_bShotsBlocked = false;
}

void CMatchBall::State_CORNER_Enter()
{
	Vector ballPos;
	CTeam *pTeam = GetGlobalTeam(LastTeam(false));

	if (Sign((m_vTriggerTouchPos - SDKGameRules()->m_vKickOff).x) == -pTeam->m_nRight)
		ballPos = pTeam->m_vCornerLeft;
	else
		ballPos = pTeam->m_vCornerRight;
	
	SetPos(ballPos);
}

void CMatchBall::State_CORNER_Think()
{
	if (!CSDKPlayer::IsOnField(m_pPl, LastOppTeam(false)))
	{
		m_pPl = NULL;

		if (CSDKPlayer::IsOnField(m_pSetpieceTaker, LastOppTeam(false)))
			m_pPl = m_pSetpieceTaker;

		if (!m_pPl)
			m_pPl = FindNearestPlayer(LastOppTeam(false));

		if (!m_pPl)
			return State_Transition(BALL_STATE_NORMAL);

		SDKGameRules()->EnableShield(SHIELD_CORNER, m_pPl->GetTeamNumber(), m_vPos);
		m_pPl->SetPosInsideShield(Vector(m_vPos.x - 25 * Sign((SDKGameRules()->m_vKickOff - m_vPos).x), m_vPos.y - 25 * Sign((SDKGameRules()->m_vKickOff - m_vPos).y), SDKGameRules()->m_vKickOff[2]), true);
		m_flStateTimelimit = -1;
		m_bShotsBlocked = true;
	}

	if (!PlayersAtTargetPos())
		return;

	UpdateCarrier();
	
	if (m_flStateTimelimit == -1)
	{
		m_flStateTimelimit = gpGlobals->curtime + sv_ball_timelimit_setpiece.GetFloat();
		m_pPl->RemoveFlag(FL_ATCONTROLS);
		m_bShotsBlocked = false;
		m_pPl->SetShotButtonsReleased(false);
	}

	if (IsPlayerClose())
	{
		if (m_flSetpieceCloseStartTime == -1)
			m_flSetpieceCloseStartTime = gpGlobals->curtime;
	}
	else
		m_flSetpieceCloseStartTime = -1;

	if (gpGlobals->curtime <= m_flStateEnterTime + sv_ball_nonnormalshotsblocktime_corner.GetFloat()
		|| m_flSetpieceCloseStartTime != -1 && gpGlobals->curtime > m_flSetpieceCloseStartTime + sv_ball_setpiece_close_time.GetFloat())
	{
		m_bNonnormalshotsBlocked = true;
	}
	else
		m_bNonnormalshotsBlocked = false;

	if (!m_bShotsBlocked
		&& m_pPl->ShotButtonsReleased()
		&& CanTouchBallXY()
		&& (m_pPl->IsNormalshooting() || !m_bNonnormalshotsBlocked && (m_pPl->IsPowershooting() || m_pPl->IsChargedshooting())))
	{
		//EmitSound("Crowd.Way");
		m_pPl->AddCorner();
		RemoveAllTouches();
		DoGroundShot(false);
		State_Transition(BALL_STATE_NORMAL);
	}
}

void CMatchBall::State_CORNER_Leave(ball_state_t newState)
{
	m_bNonnormalshotsBlocked = false;
	m_bShotsBlocked = false;
}

void CMatchBall::State_GOAL_Enter()
{
	bool isOwnGoal;
	int scoringTeam;
	CSDKPlayer *pScorer = NULL;
	CSDKPlayer *pFirstAssister = NULL;
	CSDKPlayer *pSecondAssister = NULL;
	GetGoalInfo(isOwnGoal, scoringTeam, &pScorer, &pFirstAssister, &pSecondAssister);

	m_pPl = pScorer;

	float delay = sv_ball_goalcelebduration.GetFloat();

	if (sv_replays.GetBool())
	{
		delay += sv_replay_duration1.GetFloat();

		if (sv_replay_count.GetInt() >= 2)
			delay += sv_replay_duration2.GetFloat();

		if (sv_replay_count.GetInt() >= 3)
			delay += sv_replay_duration3.GetFloat();
	}

	IGameEvent *pEvent = gameeventmanager->CreateEvent("transition");
	if (pEvent)
	{
		pEvent->SetInt("firstdelay", sv_ball_goalcelebduration.GetInt());
		pEvent->SetInt("seconddelay", delay);
		gameeventmanager->FireEvent(pEvent);
	}

	State_Transition(BALL_STATE_KICKOFF, delay);

	ReplayManager()->StartReplay(false);
}

void CMatchBall::State_GOAL_Think()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

		if (!CSDKPlayer::IsOnField(pPl))
			continue;

		if (pPl->GetTeamNumber() == m_nTeam)
			pPl->AddFlag(FL_FROZEN);
		else
			pPl->AddFlag(FL_CELEB);
	}
}

void CMatchBall::State_GOAL_Leave(ball_state_t newState)
{
	ReplayManager()->StopReplay();

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

		if (!CSDKPlayer::IsOnField(pPl))
			continue;

		pPl->RemoveFlag(FL_CELEB | FL_FROZEN);
		pPl->DoServerAnimationEvent(PLAYERANIMEVENT_CANCEL);
	}
}

void CMatchBall::State_FREEKICK_Enter()
{
	HandleFoul();
	SetPos(m_vFoulPos);
}

void CMatchBall::State_FREEKICK_Think()
{
	if (!CSDKPlayer::IsOnField(m_pPl, m_nFouledTeam))
	{
		m_pPl = NULL;

		if (CSDKPlayer::IsOnField(m_pSetpieceTaker, m_nFouledTeam))
			m_pPl = m_pSetpieceTaker;

		if (!m_pPl && (m_vPos - GetGlobalTeam(m_nFouledTeam)->m_vGoalCenter).Length2D() <= sv_ball_freekickdist_owngoal.GetInt()) // Close to own goal
			m_pPl = FindNearestPlayer(m_nFouledTeam, FL_POS_KEEPER);

		if (!CSDKPlayer::IsOnField(m_pPl) || m_pPl->GetTeamPosType() == POS_GK && m_pPl->IsBot())
			m_pPl = FindNearestPlayer(m_nFouledTeam, FL_POS_FIELD);

		if (!m_pPl)
			return State_Transition(BALL_STATE_NORMAL);

		SDKGameRules()->EnableShield(SHIELD_FREEKICK, m_pPl->GetTeamNumber(), m_vPos);
		m_pPl->SetPosInsideShield(Vector(m_vPos.x, m_vPos.y - 35 * m_pPl->GetTeam()->m_nForward, SDKGameRules()->m_vKickOff.GetZ()), true);
		m_flStateTimelimit = -1;
		m_bShotsBlocked = true;
	}

	if (!PlayersAtTargetPos())
		return;

	UpdateCarrier();

	if (m_flStateTimelimit == -1)
	{
		m_flStateTimelimit = gpGlobals->curtime + sv_ball_timelimit_setpiece.GetFloat();
		m_flSetpieceCloseStartTime = -1;
		m_pPl->RemoveFlag(FL_ATCONTROLS);
		m_bShotsBlocked = false;
		m_pPl->SetShotButtonsReleased(false);
	}

	if (IsPlayerClose())
	{
		if (m_flSetpieceCloseStartTime == -1)
			m_flSetpieceCloseStartTime = gpGlobals->curtime;
	}
	else
		m_flSetpieceCloseStartTime = -1;

	if (abs(m_vPos.y - GetGlobalTeam(m_nFoulingTeam)->m_vGoalCenter.GetY()) <= sv_ball_freekickdist_opponentgoal.GetInt()
		&& gpGlobals->curtime <= m_flStateEnterTime + sv_ball_nonnormalshotsblocktime_freekick.GetFloat()
		|| m_flSetpieceCloseStartTime != -1 && gpGlobals->curtime > m_flSetpieceCloseStartTime + sv_ball_setpiece_close_time.GetFloat())
	{
		m_bNonnormalshotsBlocked = true;
	}
	else
		m_bNonnormalshotsBlocked = false;

	if (!m_bShotsBlocked
		&& m_pPl->ShotButtonsReleased()
		&& CanTouchBallXY()
		&& (m_pPl->IsNormalshooting() || !m_bNonnormalshotsBlocked && (m_pPl->IsPowershooting() || m_pPl->IsChargedshooting())))
	{
		//EmitSound("Crowd.Way");
		m_pPl->AddFreeKick();
		RemoveAllTouches();
		DoGroundShot(true, sv_ball_freekick_speedcoeff.GetFloat());
		State_Transition(BALL_STATE_NORMAL);
	}
}

void CMatchBall::State_FREEKICK_Leave(ball_state_t newState)
{
	SDKGameRules()->SetOffsideLinesEnabled(false);
	m_bNonnormalshotsBlocked = false;
	m_bShotsBlocked = false;
}

void CMatchBall::State_PENALTY_Enter()
{
	if (SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
	{
		SetPos(GetGlobalTeam(m_nFoulingTeam)->m_vPenalty);
	}
	else
	{
		HandleFoul();
		SetPos(GetGlobalTeam(m_nFoulingTeam)->m_vPenalty);
	}
}

void CMatchBall::State_PENALTY_Think()
{
	bool takerHasChanged = false;

	if (!CSDKPlayer::IsOnField(m_pPl, m_nFouledTeam))
	{
		m_pPl = NULL;

		if (SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
		{
			m_pPl = m_pFouledPl;

			if (!CSDKPlayer::IsOnField(m_pPl, m_nFouledTeam))
			{
				m_ePenaltyState = PENALTY_ABORTED_NO_TAKER;
				return State_Transition(BALL_STATE_NORMAL);
			}
		}
		else
		{
			if (CSDKPlayer::IsOnField(m_pSetpieceTaker, m_nFouledTeam))
				m_pPl = m_pSetpieceTaker;

			if (!m_pPl)
				m_pPl = m_pFouledPl;
			if (!CSDKPlayer::IsOnField(m_pPl))
				m_pPl = FindNearestPlayer(m_nFouledTeam);
			if (!m_pPl)
				return State_Transition(BALL_STATE_NORMAL);
		}

		SDKGameRules()->EnableShield(SHIELD_PENALTY, m_nFouledTeam, GetGlobalTeam(m_nFoulingTeam)->m_vPenalty);
		m_pPl->SetPosInsideShield(Vector(m_vPos.x, m_vPos.y - 150 * m_pPl->GetTeam()->m_nForward, SDKGameRules()->m_vKickOff.GetZ()), true);
		m_flStateTimelimit = -1;
		m_bShotsBlocked = true;
		takerHasChanged = true;
	}

	if (!CSDKPlayer::IsOnField(m_pOtherPl) || takerHasChanged)
	{
		if (SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
		{
			m_pOtherPl = FindNearestPlayer(m_pPl->GetOppTeamNumber(), FL_POS_KEEPER);
			if (!m_pOtherPl)
			{
				m_ePenaltyState = PENALTY_ABORTED_NO_KEEPER;
				return State_Transition(BALL_STATE_NORMAL);
			}
		}
		else
		{
			m_pOtherPl = FindNearestPlayer(m_nInPenBoxOfTeam, FL_POS_KEEPER);
			if (!m_pOtherPl)
				return State_Transition(BALL_STATE_NORMAL);
		}

		Vector pos = m_pOtherPl->GetTeam()->m_vGoalCenter + Vector(0, m_pOtherPl->GetTeam()->m_nForward * 20, 0);
		m_pOtherPl->SetPosInsideShield(pos, false);
		m_pOtherPl->AddFlag(FL_NO_Y_MOVEMENT);
	}

	if (!PlayersAtTargetPos())
		return;

	UpdateCarrier();

	if (m_flStateTimelimit == -1)
	{
		m_flStateTimelimit = gpGlobals->curtime + sv_ball_timelimit_setpiece.GetFloat();
		m_flSetpieceCloseStartTime = -1;
		m_pPl->RemoveFlag(FL_ATCONTROLS);
	}

	if (m_bShotsBlocked && gpGlobals->curtime - m_flStateEnterTime > sv_ball_shotsblocktime_penalty.GetFloat())
	{
		m_bShotsBlocked = false;
		m_pPl->SetShotButtonsReleased(false);
	}

	if (IsPlayerClose())
	{
		if (m_flSetpieceCloseStartTime == -1)
			m_flSetpieceCloseStartTime = gpGlobals->curtime;
	}
	else
		m_flSetpieceCloseStartTime = -1;

	if (m_flSetpieceCloseStartTime != -1 && gpGlobals->curtime > m_flSetpieceCloseStartTime + sv_ball_setpiece_close_time.GetFloat())
	{
		m_bNonnormalshotsBlocked = true;
	}
	else
		m_bNonnormalshotsBlocked = false;

	if (!m_bShotsBlocked
		&& m_pPl->ShotButtonsReleased()
		&& CanTouchBallXY()
		&& (m_pPl->IsNormalshooting() || !m_bNonnormalshotsBlocked && (m_pPl->IsPowershooting() || m_pPl->IsChargedshooting())))
	{
		m_pPl->AddPenalty();
		RemoveAllTouches();
		m_ePenaltyState = PENALTY_KICKED;
		m_pPl->m_ePenaltyState = PENALTY_KICKED;
		DoGroundShot(false);
		State_Transition(BALL_STATE_NORMAL);
	}
}

void CMatchBall::State_PENALTY_Leave(ball_state_t newState)
{
	if (CSDKPlayer::IsOnField(m_pOtherPl))
		m_pOtherPl->RemoveFlag(FL_NO_Y_MOVEMENT);

	m_bNonnormalshotsBlocked = false;
	m_bShotsBlocked = false;
}

void CMatchBall::TriggerGoal(int team)
{
	m_nTeam = team;

	if (SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
	{
		if (m_ePenaltyState == PENALTY_KICKED && m_nTeam == m_nFoulingTeam)
		{
			m_ePenaltyState = PENALTY_SCORED;
			m_bHasQueuedState = true;
		}

		return;
	}

	if (LastInfo(true) && LastInfo(true)->m_eBallState == BALL_STATE_THROWIN && !LastPl(false, LastPl(true)))
	{
		TriggerGoalLine(m_nTeam);
		return;
	}

	if (LastTeam(true) != m_nTeam && LastPl(true))
	{
		LastPl(true)->AddShot();
		LastPl(true)->AddShotOnGoal();
	}

	SDKGameRules()->SetKickOffTeam(m_nTeam);

	bool isOwnGoal;
	int scoringTeam;
	CSDKPlayer *pScorer = NULL;
	CSDKPlayer *pFirstAssister = NULL;
	CSDKPlayer *pSecondAssister = NULL;
	GetGoalInfo(isOwnGoal, scoringTeam, &pScorer, &pFirstAssister, &pSecondAssister);

	if (isOwnGoal)
	{
		if (pScorer)
			pScorer->AddOwnGoal();

		ReplayManager()->AddMatchEvent(MATCH_EVENT_OWNGOAL, scoringTeam, pScorer);
	}
	else
	{
		if (pScorer)
			pScorer->AddGoal();

		if (pFirstAssister)
			pFirstAssister->AddAssist();

		if (pSecondAssister)
			pSecondAssister->AddAssist();

		ReplayManager()->AddMatchEvent(MATCH_EVENT_GOAL, scoringTeam, pScorer, pFirstAssister, pSecondAssister);
	}

	CSDKPlayer *pKeeper = GetGlobalTeam(m_nTeam)->GetPlayerByPosType(POS_GK);
	if (pKeeper)
		pKeeper->AddGoalConceded();

	State_Transition(BALL_STATE_GOAL, sv_ball_statetransition_activationdelay_short.GetFloat(), false, true);
}

void CMatchBall::TriggerGoalLine(int team)
{
	//DevMsg("Trigger goal line\n");
	if (SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
		return;

	m_vTriggerTouchPos = GetPos();

	BallTouchInfo *pInfo = LastInfo(true);

	if (pInfo->m_nTeam != team && pInfo->m_pPl && (m_vTriggerTouchPos - pInfo->m_vBallPos).Length2DSqr() >= pow(sv_ball_stats_shot_mindist.GetFloat(), 2.0f)) // Don't add a missed shot or pass if the player accidentally dribbles the ball out
	{
		float minX = GetGlobalTeam(team)->m_vSixYardBoxMin.GetX();
		float maxX = GetGlobalTeam(team)->m_vSixYardBoxMax.GetX();

		if (m_bHitThePost || m_vTriggerTouchPos.x >= minX && m_vTriggerTouchPos.x <= maxX) // Bounced off the post or crossed the goal line inside the six-yard box
		{
			LastPl(true)->AddShot();
			//EmitSound("Crowd.Miss");
			ReplayManager()->AddMatchEvent(MATCH_EVENT_MISS, GetGlobalTeam(team)->GetOppTeamNumber(), LastPl(true));
		}
		else
			LastPl(true)->AddPass();
	}

	if (LastTeam(false) == team)
	{
		//SetMatchEvent(MATCH_EVENT_CORNER, NULL, LastOppTeam(false));
		State_Transition(BALL_STATE_CORNER, sv_ball_statetransition_activationdelay_normal.GetFloat());
	}
	else
	{
		//SetMatchEvent(MATCH_EVENT_THROWIN, NULL, LastOppTeam(false));
		State_Transition(BALL_STATE_GOALKICK, sv_ball_statetransition_activationdelay_normal.GetFloat());
	}
}

void CMatchBall::TriggerSideline()
{
	if (SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
		return;

	Vector ballPos = GetPos();

	BallTouchInfo *pInfo = LastInfo(true);
	CSDKPlayer *pLastPl = LastPl(true);

	if (pInfo && CSDKPlayer::IsOnField(pLastPl))
	{
		if (m_bHitThePost) // Goal post hits don't trigger a statistic change right away, since we don't know if it ends up being a goal or a miss. So do the check here.
			pLastPl->AddShot();
		else if (pInfo->m_eBodyPart != BODY_PART_KEEPERPUNCH
			&& GetVel().Length2DSqr() < pow(sv_ball_stats_clearance_minspeed.GetFloat(), 2.0f)
			&& (ballPos - pInfo->m_vBallPos).Length2DSqr() >= pow(sv_ball_stats_pass_mindist.GetFloat(), 2.0f)) // Pass or interception if over a distance threshold and wasn't punched away by a keeper
		{
			pLastPl->AddPass();
		}
	}

	CBaseEntity *pThrowIn = gEntList.FindEntityByClassnameNearest("info_throw_in", ballPos, 1000);
	if (!pThrowIn)
		return;

	m_vTriggerTouchPos = pThrowIn->GetLocalOrigin();
	//SetMatchEvent(MATCH_EVENT_THROWIN, NULL, LastOppTeam(false));
	State_Transition(BALL_STATE_THROWIN, sv_ball_statetransition_activationdelay_normal.GetFloat());
}

void CMatchBall::SendNotifications()
{
	if (m_eNextState == BALL_STATE_GOAL)
	{
		bool isOwnGoal;
		int scoringTeam;
		CSDKPlayer *pScorer = NULL;
		CSDKPlayer *pFirstAssister = NULL;
		CSDKPlayer *pSecondAssister = NULL;
		GetGoalInfo(isOwnGoal, scoringTeam, &pScorer, &pFirstAssister, &pSecondAssister);

		if (isOwnGoal)
		{
			IGameEvent *pEvent = gameeventmanager->CreateEvent("own_goal");
			if (pEvent)
			{
				pEvent->SetInt("causing_team", GetGlobalTeam(scoringTeam)->GetOppTeamNumber());
				pEvent->SetInt("causer_userid", pScorer ? pScorer->GetUserID() : 0);
				gameeventmanager->FireEvent(pEvent);
			}
		}
		else
		{
			IGameEvent *pEvent = gameeventmanager->CreateEvent("goal");
			if (pEvent)
			{
				pEvent->SetInt("scoring_team", scoringTeam);
				pEvent->SetInt("scorer_userid", pScorer ? pScorer->GetUserID() : 0);
				pEvent->SetInt("first_assister_userid", pFirstAssister ? pFirstAssister->GetUserID() : 0);
				pEvent->SetInt("second_assister_userid", pSecondAssister ? pSecondAssister->GetUserID() : 0);
				gameeventmanager->FireEvent(pEvent);
			}
		}

		EmitSound("Ball.Whistle");
		//EmitSound("Crowd.Goal1");
		//EmitSound("Crowd.Goal2");
		//EmitSound("Crowd.Goal3");
	}
	else
	{
		switch (m_eNextState)
		{
		case BALL_STATE_THROWIN:
			{
				IGameEvent *pEvent = gameeventmanager->CreateEvent("set_piece");
				if (pEvent)
				{
					pEvent->SetInt("type", MATCH_EVENT_THROWIN);
					pEvent->SetInt("taking_team", LastOppTeam(false));
					statistic_type_t statType;

					switch (g_IOSRand.RandomInt(0, 1))
					{
					case 0:
						statType = STATISTIC_POSSESSION_TEAM;
						break;
					case 1:
						statType = STATISTIC_PASSING_TEAM;
						break;
					default:
						statType = STATISTIC_POSSESSION_TEAM;
						break;
					}

					pEvent->SetInt("statistic_type", statType);
					gameeventmanager->FireEvent(pEvent);
				}
				EmitSound("Ball.Whistle");
			}
			break;
		case BALL_STATE_GOALKICK:
			{
				IGameEvent *pEvent = gameeventmanager->CreateEvent("set_piece");
				if (pEvent)
				{
					pEvent->SetInt("type", MATCH_EVENT_GOALKICK);
					pEvent->SetInt("taking_team", LastOppTeam(false));
					pEvent->SetInt("statistic_type", STATISTIC_SHOTSONGOAL_TEAM);
					gameeventmanager->FireEvent(pEvent);
				}
				EmitSound("Ball.Whistle");
			}
			break;
		case BALL_STATE_CORNER:
			{
				IGameEvent *pEvent = gameeventmanager->CreateEvent("set_piece");
				if (pEvent)
				{
					pEvent->SetInt("type", MATCH_EVENT_CORNER);
					pEvent->SetInt("taking_team", LastOppTeam(false));
					pEvent->SetInt("statistic_type", STATISTIC_SETPIECECOUNT_TEAM);
					gameeventmanager->FireEvent(pEvent);
				}
				EmitSound("Ball.Whistle");
			}
			break;
		case BALL_STATE_FREEKICK:
		case BALL_STATE_PENALTY:
			{
				match_event_t foulType;

				switch (m_eFoulType)
				{
				case FOUL_NORMAL_NO_CARD:
					foulType = MATCH_EVENT_FOUL;
					//EmitSound("Crowd.Foul");
					break;
				case FOUL_NORMAL_YELLOW_CARD:
					if (m_pFoulingPl->GetYellowCards() % 2 == 0)
						foulType = MATCH_EVENT_SECONDYELLOWCARD;
					else
						foulType = MATCH_EVENT_YELLOWCARD;
					//EmitSound("Crowd.YellowCard");
					break;
				case FOUL_NORMAL_RED_CARD:
					foulType = MATCH_EVENT_REDCARD;
					//EmitSound("Crowd.RedCard");
					break;
				case FOUL_OFFSIDE:
					foulType = MATCH_EVENT_OFFSIDE;
					SDKGameRules()->SetOffsideLinesEnabled(true);
					//EmitSound("Crowd.Foul");
					break;
				case FOUL_DOUBLETOUCH:
					foulType = MATCH_EVENT_DOUBLETOUCH;
					//EmitSound("Crowd.Foul");
					break;
				default:
					foulType = MATCH_EVENT_FOUL;
					//EmitSound("Crowd.Foul");
					break;
				}

				IGameEvent *pEvent = gameeventmanager->CreateEvent("foul");
				if (pEvent)
				{
					pEvent->SetInt("fouling_player_userid", (m_pFoulingPl ? m_pFoulingPl->GetUserID() : 0));
					pEvent->SetInt("fouled_player_userid", (m_pFouledPl ? m_pFouledPl->GetUserID() : 0));
					pEvent->SetInt("fouling_team", m_nFoulingTeam);
					pEvent->SetInt("foul_type", foulType);
					pEvent->SetInt("set_piece_type", (m_eNextState == BALL_STATE_PENALTY ? MATCH_EVENT_PENALTY : MATCH_EVENT_FREEKICK));

					float distToGoal = (m_vFoulPos - GetGlobalTeam(m_nFoulingTeam)->m_vGoalCenter).Length2D();

					statistic_type_t statType;

					if (m_eNextState == BALL_STATE_FREEKICK && distToGoal <= sv_ball_freekickdist_opponentgoal.GetInt())
						statType = STATISTIC_DISTANCETOGOAL;
					else if (m_eNextState == BALL_STATE_FREEKICK && m_eFoulType == FOUL_OFFSIDE)
						statType = STATISTIC_OFFSIDES_TEAM;
					else if (m_eNextState == BALL_STATE_FREEKICK && m_eFoulType != FOUL_DOUBLETOUCH)
						statType = STATISTIC_FOULS_TEAM;
					else
						statType = (g_IOSRand.RandomInt(0, 2) == 0 ? STATISTIC_POSSESSION_TEAM : STATISTIC_SETPIECECOUNT_TEAM);

					pEvent->SetInt("statistic_type", statType);
					pEvent->SetInt("distance_to_goal", distToGoal * 2.54f / 100);
					gameeventmanager->FireEvent(pEvent);
				}

				EmitSound("Ball.Whistle");
			}
			break;
		}
	}
}

void CMatchBall::GetGoalInfo(bool &isOwnGoal, int &scoringTeam, CSDKPlayer **pScorer, CSDKPlayer **pFirstAssister, CSDKPlayer **pSecondAssister)
{
	CSDKPlayer *pKeeper = NULL;

	// Prevent own goals from keeper punches
	if (LastInfo(true)->m_eBodyPart == BODY_PART_KEEPERPUNCH)
	{
		isOwnGoal = false;
		pKeeper = LastPl(true);
	}
	else if (m_nTeam == LastTeam(true))
		isOwnGoal = true;
	else
		isOwnGoal = false;

	if (isOwnGoal)
	{
		scoringTeam = LastOppTeam(true);
		*pScorer = LastPl(true);
	}
	else
	{
		scoringTeam = LastTeam(true, pKeeper);
		*pScorer = LastPl(true, pKeeper);

		if (!*pScorer)
			return;

		*pFirstAssister = LastPl(true, pKeeper, *pScorer);

		if (!*pFirstAssister
			|| (*pFirstAssister)->GetTeam() != (*pScorer)->GetTeam()
			|| gpGlobals->curtime - LastInfo(true, pKeeper, *pScorer)->m_flTime > sv_ball_stats_assist_maxtime.GetFloat())
		{
			*pFirstAssister = NULL;
			return;
		}

		*pSecondAssister = LastPl(true, pKeeper, *pScorer, *pFirstAssister);

		if (!*pSecondAssister
			|| (*pSecondAssister)->GetTeam() != (*pScorer)->GetTeam()
			|| gpGlobals->curtime - LastInfo(true, pKeeper, *pScorer, *pFirstAssister)->m_flTime > sv_ball_stats_assist_maxtime.GetFloat())
		{
			*pSecondAssister = NULL;
			return;
		}
	}
}

void CMatchBall::Touched(CSDKPlayer *pPl, bool isShot, body_part_t bodyPart, const Vector &oldVel)
{
	if (SDKGameRules()->IsIntermissionState() || m_bHasQueuedState || SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
		return;

	if (m_Touches.Count() > 0 && m_Touches.Tail()->m_pPl == pPl && m_Touches.Tail()->m_nTeam == pPl->GetTeamNumber()
		&& sv_ball_doubletouchfouls.GetBool() && State_Get() == BALL_STATE_NORMAL && m_Touches.Tail()->m_eBallState != BALL_STATE_NORMAL
		&& m_Touches.Tail()->m_eBallState != BALL_STATE_KEEPERHANDS && pPl->GetTeam()->GetNumPlayers() > 2 && pPl->GetOppTeam()->GetNumPlayers() > 2) // Double touch foul
	{
		TriggerFoul(FOUL_DOUBLETOUCH, pPl->GetLocalOrigin(), pPl);
		State_Transition(BALL_STATE_FREEKICK, sv_ball_statetransition_activationdelay_long.GetFloat());

		return;
	}

	// Regular touch
	BallTouchInfo *pInfo = LastInfo(true);
	CSDKPlayer *pLastPl = LastPl(true);

	if (pInfo && CSDKPlayer::IsOnField(pLastPl) && pLastPl != pPl)
	{ 
		if (pInfo->m_nTeam != pPl->GetTeamNumber()
			&& (bodyPart == BODY_PART_KEEPERPUNCH
				|| bodyPart == BODY_PART_KEEPERCATCH
				&& oldVel.Length2DSqr() >= pow(sv_ball_stats_save_minspeed.GetInt(), 2.0f))) // All fast balls by an opponent which are caught or punched away by the keeper count as shots on goal
		{
			pPl->AddKeeperSave();

			if (bodyPart == BODY_PART_KEEPERCATCH)
				pPl->AddKeeperSaveCaught();

			pLastPl->AddShot();
			pLastPl->AddShotOnGoal();
			//EmitSound("Crowd.Save");
			ReplayManager()->AddMatchEvent(MATCH_EVENT_KEEPERSAVE, pPl->GetTeamNumber(), pPl, pLastPl);
		}
		else if ((m_vPos - pInfo->m_vBallPos).Length2DSqr() >= pow(sv_ball_stats_pass_mindist.GetInt(), 2.0f) && pInfo->m_eBodyPart != BODY_PART_KEEPERPUNCH) // Pass or interception
		{
			if (m_bHitThePost)
			{
				pLastPl->AddShot();
			}
			else
			{
				pLastPl->AddPass();

				if (pInfo->m_nTeam == pPl->GetTeamNumber()) // Pass to teammate
				{
					pLastPl->AddPassCompleted();
				}
				else // Intercepted by opponent
				{
					pPl->AddInterception();
				}
			}
		}
	}

	UpdatePossession(pPl);
	BallTouchInfo *info = new BallTouchInfo;
	info->m_pPl = pPl;
	info->m_nTeam = pPl->GetTeamNumber();
	info->m_bIsShot = isShot;
	info->m_eBodyPart = bodyPart;
	info->m_eBallState = State_Get();
	info->m_vBallPos = m_vPos;
	info->m_vBallVel = m_vVel;
	info->m_flTime = gpGlobals->curtime;
	m_Touches.AddToTail(info);
	m_bHitThePost = false;
	m_bLastContactWasTouch = !isShot;

	//DevMsg("touches: %d\n", m_Touches.Count());
	
	if (pPl->IsOffside())
	{
		pPl->AddOffside();
		TriggerFoul(FOUL_OFFSIDE, pPl->GetOffsidePos(), pPl);
		SDKGameRules()->SetOffsideLinePositions(pPl->GetOffsideBallPos().y, pPl->GetOffsidePos().y, pPl->GetOffsideLastOppPlayerPos().y);
		State_Transition(BALL_STATE_FREEKICK, sv_ball_statetransition_activationdelay_long.GetFloat());
	}
}

bool CMatchBall::CheckFoul(bool canShootBall, const Vector &localDirToBall)
{
	for (int i = 1; i <= gpGlobals->maxClients; i++) 
	{
		CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

		if (!CSDKPlayer::IsOnField(pPl))
			continue;

		if (pPl == m_pPl || pPl->GetTeamNumber() == m_pPl->GetTeamNumber())
			continue;

		Vector plPos = pPl->GetLocalOrigin();

		//if (plPos.x < SDKGameRules()->m_vFieldMin.GetX() || plPos.y < SDKGameRules()->m_vFieldMin.GetY() ||
		//	plPos.x > SDKGameRules()->m_vFieldMax.GetX() || plPos.y > SDKGameRules()->m_vFieldMax.GetY())
		//	continue;

		Vector dirToPl = pPl->GetLocalOrigin() - m_vPlPos;
		float distToPl = dirToPl.Length2D();

		Vector localDirToPl;
		VectorIRotate(dirToPl, m_pPl->EntityToWorldTransform(), localDirToPl);

		dirToPl.z = 0;
		dirToPl.NormalizeInPlace();

		// Can't reach the other player
		if (localDirToPl.x < -sv_ball_slidebackwardreach_foul.GetInt()
			|| localDirToPl.x > sv_ball_slideforwardreach_foul.GetInt()
			|| abs(localDirToPl.y) > sv_ball_slidesidereach_foul.GetInt())		
			continue;

		// Can shoot the ball and ball is closer to the player than the opponent
		if (canShootBall && localDirToBall.x <= localDirToPl.x)
			continue;

		// It's a foul

		PlayerAnimEvent_t anim = RAD2DEG(acos(m_vPlForward2D.Dot(pPl->EyeDirection2D()))) <= 90 ? PLAYERANIMEVENT_TACKLED_BACKWARD : PLAYERANIMEVENT_TACKLED_FORWARD;
		pPl->DoAnimationEvent(anim);

		int teammatesCloserToGoalCount = 0;

		bool isCloseToOwnGoal = ((m_vPos - m_pPl->GetTeam()->m_vGoalCenter).Length2D() <= sv_ball_closetogoaldist.GetInt());

		if (isCloseToOwnGoal)
		{
			for (int j = 1; j <= gpGlobals->maxClients; j++) 
			{
				CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(j));

				if (!CSDKPlayer::IsOnField(pPl) || pPl == m_pPl || pPl->GetTeamNumber() != m_pPl->GetTeamNumber())
					continue;

				if ((m_pPl->GetTeam()->m_vGoalCenter - pPl->GetLocalOrigin()).Length2DSqr() < (m_pPl->GetTeam()->m_vGoalCenter - m_vPlPos).Length2DSqr())
					teammatesCloserToGoalCount += 1;
			}
		}

		bool isPenalty = pPl->m_nInPenBoxOfTeam == m_pPl->GetTeamNumber();

		foul_type_t foulType;

		if (isCloseToOwnGoal && teammatesCloserToGoalCount <= 1)
			foulType = FOUL_NORMAL_RED_CARD;
		else if (anim == PLAYERANIMEVENT_TACKLED_FORWARD && localDirToBall.Length2DSqr() >= Sqr(sv_ball_yellowcardballdist_forward.GetFloat()) ||
				 anim == PLAYERANIMEVENT_TACKLED_BACKWARD && localDirToBall.Length2DSqr() >= Sqr(sv_ball_yellowcardballdist_backward.GetFloat()))
			foulType = FOUL_NORMAL_YELLOW_CARD;
		else
			foulType = FOUL_NORMAL_NO_CARD;

		TriggerFoul(foulType, pPl->GetLocalOrigin(), m_pPl, pPl);
		m_pFoulingPl->AddFoul();
		m_pFouledPl->AddFoulSuffered();

		if (m_eFoulType == FOUL_NORMAL_YELLOW_CARD)
			m_pFoulingPl->AddYellowCard();

		if (m_eFoulType == FOUL_NORMAL_YELLOW_CARD && m_pFoulingPl->GetYellowCards() % 2 == 0 || m_eFoulType == FOUL_NORMAL_RED_CARD)
		{
			m_pFoulingPl->AddRedCard();
			int banDuration = 60 * (m_eFoulType == FOUL_NORMAL_YELLOW_CARD ? sv_ball_player_yellow_red_card_duration.GetFloat() : sv_ball_player_red_card_duration.GetFloat());
			int nextJoin = SDKGameRules()->GetMatchDisplayTimeSeconds(false) + banDuration;
			m_pFoulingPl->SetNextCardJoin(nextJoin);

			if (m_pFoulingPl->GetTeamPosType() != POS_GK)
				m_pFoulingPl->GetTeam()->SetPosNextJoinSeconds(m_pFoulingPl->GetTeamPosIndex(), nextJoin);

			ReplayManager()->AddMatchEvent(m_eFoulType == FOUL_NORMAL_YELLOW_CARD ? MATCH_EVENT_SECONDYELLOWCARD : MATCH_EVENT_REDCARD, m_nFoulingTeam, m_pFoulingPl);
		}
		else if (m_eFoulType == FOUL_NORMAL_YELLOW_CARD)
		{
			ReplayManager()->AddMatchEvent(MATCH_EVENT_YELLOWCARD, m_nFoulingTeam, m_pFoulingPl);
		}

		if (isPenalty)
			State_Transition(BALL_STATE_PENALTY, sv_ball_statetransition_activationdelay_long.GetFloat());
		else
			State_Transition(BALL_STATE_FREEKICK, sv_ball_statetransition_activationdelay_long.GetFloat());

		return true;
	}

	return false;
}

void CMatchBall::HandleFoul()
{
	if (CSDKPlayer::IsOnField(m_pFoulingPl))
	{
		if (m_eFoulType == FOUL_NORMAL_YELLOW_CARD && m_pFoulingPl->GetYellowCards() % 2 == 0 || m_eFoulType == FOUL_NORMAL_RED_CARD)
		{
			int team = m_pFoulingPl->GetTeamNumber();
			int posIndex = m_pFoulingPl->GetTeamPosIndex();
			int posType = m_pFoulingPl->GetTeamPosType();
			m_pFoulingPl->SetDesiredTeam(TEAM_SPECTATOR, m_pFoulingPl->GetTeamNumber(), 0, true, false, false);

			if (posType == POS_GK)
			{
				for (int i = 1; i <= gpGlobals->maxClients; i++)
				{
					CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));
					if (!CSDKPlayer::IsOnField(pPl) || pPl == m_pFoulingPl || pPl->GetTeamNumber() != team)
						continue;

					pPl->SetDesiredTeam(team, team, posIndex, true, false, false);
					break;
				}
			}
		}
	}
}

void CMatchBall::TriggerFoul(foul_type_t type, Vector pos, CSDKPlayer *pFoulingPl, CSDKPlayer *pFouledPl /*= NULL*/)
{
	m_eFoulType = type;
	m_pFoulingPl = pFoulingPl;
	m_pFouledPl = pFouledPl;
	m_nFoulingTeam = pFoulingPl->GetTeamNumber();
	m_nFouledTeam = pFoulingPl->GetOppTeamNumber();
	m_vFoulPos.x = clamp(pos.x, SDKGameRules()->m_vFieldMin.GetX() + 2 * m_flPhysRadius, SDKGameRules()->m_vFieldMax.GetX() - 2 * m_flPhysRadius);
	m_vFoulPos.y = clamp(pos.y, SDKGameRules()->m_vFieldMin.GetY() + 2 * m_flPhysRadius, SDKGameRules()->m_vFieldMax.GetY() - 2 * m_flPhysRadius);
	m_vFoulPos.z = SDKGameRules()->m_vKickOff.GetZ();

	// Move the ball to the edge of the penalty box if the foul happened inside. This will probably only be relevant for double touch fouls.

	Vector min = GetGlobalTeam(m_nFoulingTeam)->m_vPenBoxMin - m_flPhysRadius;
	Vector max = GetGlobalTeam(m_nFoulingTeam)->m_vPenBoxMax + m_flPhysRadius;

	// Ball inside the penalty box
	if (m_vFoulPos.x > min.x && m_vFoulPos.x < max.x)
	{
		if (GetGlobalTeam(m_nFoulingTeam)->m_nForward == 1 && m_vFoulPos.y < max.y)
			m_vFoulPos.y = max.y;
		else if (GetGlobalTeam(m_nFoulingTeam)->m_nForward == -1 && m_vFoulPos.y > min.y)
			m_vFoulPos.y = min.y;
	}
}

bool CMatchBall::IsPlayerClose()
{
	return (m_vPos - m_vPlPos).Length2DSqr() <= pow(sv_ball_setpiece_close_dist.GetFloat(), 2);
}

void CMatchBall::SetVel(Vector vel, float spinCoeff, int spinFlags, body_part_t bodyPart, bool isDeflection, bool markOffsidePlayers, bool ensureMinShotStrength, float nextShotMinDelay)
{
	CBall::SetVel(vel, spinCoeff, spinFlags, bodyPart, isDeflection, markOffsidePlayers, ensureMinShotStrength, nextShotMinDelay);

	if (markOffsidePlayers)
		MarkOffsidePlayers();
}

extern ConVar sv_singlekeeper;

void CMatchBall::MarkOffsidePlayers()
{
	if (SDKGameRules()->IsIntermissionState() || m_bHasQueuedState || SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
		return;

	m_vOffsidePos = m_vPos;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

		if (pPl)
			pPl->SetOffside(false);

		if (!CSDKPlayer::IsOnField(pPl) || pPl == m_pPl || pPl->GetTeamNumber() != m_pPl->GetTeamNumber())
			continue;

		Vector pos = pPl->GetLocalOrigin();
		int forward = pPl->GetTeam()->m_nForward;

		// In opponent half?
		if (Sign((pos - SDKGameRules()->m_vKickOff).y) != forward)
			continue;

		// Player closer to goal than the ball?
		if (Sign(pos.y - m_vPos.y) != forward)
			continue;

		int oppPlayerCount = 0;
		int nearerPlayerCount = 0;
		CSDKPlayer *pLastPl = NULL;
		float shortestDist = FLT_MAX;

		// Count opponent players who are nearer to the goal
		for (int j = 1; j <= gpGlobals->maxClients; j++)
		{
			CSDKPlayer *pOpp = ToSDKPlayer(UTIL_PlayerByIndex(j));
			if (!CSDKPlayer::IsOnField(pOpp) || pOpp->GetTeamNumber() == pPl->GetTeamNumber())
				continue;

			oppPlayerCount += 1;

			if (Sign(pOpp->GetLocalOrigin().y - pos.y) == forward)
			{
				nearerPlayerCount += 1;
			}
			else
			{
				float dist = abs(pos.y - pOpp->GetLocalOrigin().y);
				if (dist < shortestDist)
				{
					shortestDist = dist;
					pLastPl = pOpp;
				}
			}
		}

		// If the defending team doesn't have a keeper, just assume there is one at his goal line and increase the counter by one.
		// This is mainly relevant for matches with sv_singlekeeper enabled when the keeper spot of the defending team is empty when the attackers play a pass.
		// An attacker can be caught offside then, even if a defender is closer to the goal than he is.
		// It also helps preventing abuse by keepers who go to spec during an attack to force false offsides.
		if (!m_pPl->GetOppTeam()->GetPlayerByPosType(POS_GK))
		{
			nearerPlayerCount += 1;
		}

		// Require at least two opponent players on the field to consider an offside. Useful in public matches with few players.
		if (oppPlayerCount >= 2 && nearerPlayerCount <= 1)
		{
			pPl->SetOffside(true);
			pPl->SetOffsidePos(pPl->GetLocalOrigin());
			pPl->SetOffsideBallPos(m_vPos);

			Vector lastSafePos;

			if (abs(pLastPl->GetLocalOrigin().y - pPl->GetLocalOrigin().y) < abs(SDKGameRules()->m_vKickOff.GetY() - pPl->GetLocalOrigin().y))
				lastSafePos = pLastPl->GetLocalOrigin();
			else
				lastSafePos = SDKGameRules()->m_vKickOff;

			pPl->SetOffsideLastOppPlayerPos(lastSafePos);
		}
	}
}

void CMatchBall::UnmarkOffsidePlayers()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

		if (CSDKPlayer::IsOnField(pPl))
			pPl->SetOffside(false);
	}
}

void CMatchBall::UpdatePossession(CSDKPlayer *pNewPossessor)
{
	if (m_pPossessingPl == pNewPossessor)
		return;

	if (m_flPossessionStart != -1)
	{
		float duration = gpGlobals->curtime - m_flPossessionStart;

		if (m_pPossessingPl)
			m_pPossessingPl->AddPossessionTime(duration);

		float total = GetGlobalTeam(TEAM_A)->m_flPossessionTime + GetGlobalTeam(TEAM_B)->m_flPossessionTime;

		GetGlobalTeam(TEAM_A)->m_Possession = floor(GetGlobalTeam(TEAM_A)->m_flPossessionTime * 100 / max(1, total) + 0.5f);
		GetGlobalTeam(TEAM_B)->m_Possession = 100 - GetGlobalTeam(TEAM_A)->m_Possession;

		int possSum = 0;

		struct remainder_t
		{
			int dataIndex;
			float remainder;
			remainder_t() : dataIndex(0), remainder(0) {}
		};

		remainder_t *sortedRemainders = new remainder_t[CPlayerPersistentData::m_PlayerPersistentData.Count()];

		for (int i = 0; i < CPlayerPersistentData::m_PlayerPersistentData.Count(); i++)
		{
			CPlayerMatchData *pData = CPlayerPersistentData::m_PlayerPersistentData[i]->m_pMatchData;
			
			float exactPossession = pData->m_flPossessionTime * 100 / max(1, total);
			pData->m_nPossession = (int)exactPossession;
			float remainder = exactPossession - pData->m_nPossession;

			possSum += pData->m_nPossession;

			for (int j = 0; j <= i; j++)
			{
				if (sortedRemainders[j].remainder <= remainder)
				{
					for (int k = i; k > j; k--)
					{
						sortedRemainders[k] = sortedRemainders[k - 1];
					}

					sortedRemainders[j].dataIndex = i;
					sortedRemainders[j].remainder = remainder;

					break;
				}
			}
		}

		for (int i = 0; i < CPlayerPersistentData::m_PlayerPersistentData.Count(); i++)
		{
			if (possSum == 100)
				break;

			CPlayerMatchData *pData = CPlayerPersistentData::m_PlayerPersistentData[sortedRemainders[i].dataIndex]->m_pMatchData;
			pData->m_nPossession += 1;
			possSum += 1;
		}

		delete[] sortedRemainders;
	}

	if (CSDKPlayer::IsOnField(pNewPossessor))
	{
		m_pPossessingPl = pNewPossessor;
		m_nPossessingTeam = pNewPossessor->GetTeamNumber();
		m_flPossessionStart = gpGlobals->curtime;
	}
	else
	{
		m_pPossessingPl = NULL;
		m_nPossessingTeam = TEAM_INVALID;
		m_flPossessionStart = -1;
	}
}

// -100 = ball at the goal line of the home team
// 0 = ball at the the half-way line
// 100 = ball at the goal line of the away team
float CMatchBall::CalcFieldZone()
{
	Vector pos = GetPos();
	float fieldLength = SDKGameRules()->m_vFieldMax.GetY() - SDKGameRules()->m_vFieldMin.GetY();
	float dist = GetGlobalTeam(TEAM_A)->m_nForward * (pos.y - SDKGameRules()->m_vKickOff.GetY());
	return clamp(dist * 100 / (fieldLength / 2), -100, 100);
}

void CMatchBall::SetPenaltyTaker(CSDKPlayer *pPl)
{
	m_pFouledPl = pPl;
	m_nFouledTeam = pPl->GetTeamNumber();
	m_nFoulingTeam = pPl->GetOppTeamNumber();
}

// Make sure that all players are walked to the intended positions when setting shields
bool CMatchBall::PlayersAtTargetPos()
{
	bool playersAtTarget = true;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CSDKPlayer *pPl = ToSDKPlayer(UTIL_PlayerByIndex(i));

		if (!CSDKPlayer::IsOnField(pPl))
			continue;

		if (!pPl->m_bIsAtTargetPos)
		{
			if (!(pPl->GetFlags() & FL_REMOTECONTROLLED))
			{
				if (mp_shield_liberal_teammates_positioning.GetBool() && m_pCurStateInfo->m_eBallState != BALL_STATE_KICKOFF && m_pCurStateInfo->m_eBallState != BALL_STATE_PENALTY && pPl->GetTeamNumber() == m_pPl->GetTeamNumber())
					pPl->SetPosOutsideBall(pPl->GetLocalOrigin());
				else
					pPl->SetPosOutsideShield(false);
			}

			if (!pPl->m_bIsAtTargetPos)
			{
				if (pPl->m_flRemoteControlledStartTime == -1)
				{
					pPl->m_flRemoteControlledStartTime = gpGlobals->curtime;
					playersAtTarget = false;
				}
				else if (gpGlobals->curtime >= pPl->m_flRemoteControlledStartTime + sv_ball_timelimit_remotecontrolled.GetFloat()) // Player timed out and blocks progress, so move him to specs
					pPl->SetDesiredTeam(TEAM_SPECTATOR, pPl->GetTeamNumber(), 0, true, false, false);
				else
					playersAtTarget = false;
			}
		}
	}

	return playersAtTarget;
}

void CMatchBall::VPhysicsCollision(int index, gamevcollisionevent_t *pEvent)
{
	float speed = pEvent->collisionSpeed;
	int surfaceProps = pEvent->surfaceProps[!index];

	if (surfaceProps == POST_SURFACEPROPS && speed > 300.0f)
	{
		CSDKPlayer *pLastPl = LastPl(true);
		if (pLastPl && Sign(m_vPos.y - SDKGameRules()->m_vKickOff.GetY()) == pLastPl->GetTeam()->m_nForward) // Check if it's the opponent's goal
			m_bHitThePost = true;
	}

	CBall::VPhysicsCollision(index, pEvent);
}

void CMatchBall::RemoveAllTouches()
{
	if (!m_bHasQueuedState)
		m_Touches.PurgeAndDeleteElements();
}

BallTouchInfo *CMatchBall::LastInfo(bool wasShooting, CSDKPlayer *pSkipPl /*= NULL*/, CSDKPlayer *pSkipPl2 /*= NULL*/, CSDKPlayer *pSkipPl3 /*= NULL*/)
{
	for (int i = m_Touches.Count() - 1; i >= 0; i--)
	{
		if (pSkipPl && m_Touches[i]->m_pPl == pSkipPl)
			continue;

		if (pSkipPl2 && m_Touches[i]->m_pPl == pSkipPl2)
			continue;

		if (pSkipPl3 && m_Touches[i]->m_pPl == pSkipPl3)
			continue;

		if (!wasShooting || m_Touches[i]->m_bIsShot)
			return m_Touches[i];
	}

	return NULL;
}

CSDKPlayer *CMatchBall::LastPl(bool wasShooting, CSDKPlayer *pSkipPl /*= NULL*/, CSDKPlayer *pSkipPl2 /*= NULL*/, CSDKPlayer *pSkipPl3 /*= NULL*/)
{
	BallTouchInfo *info = LastInfo(wasShooting, pSkipPl, pSkipPl2, pSkipPl3);
	if (info && CSDKPlayer::IsOnField(info->m_pPl))
		return info->m_pPl;
	
	return NULL;
}

int CMatchBall::LastTeam(bool wasShooting, CSDKPlayer *pSkipPl /*= NULL*/, CSDKPlayer *pSkipPl2 /*= NULL*/, CSDKPlayer *pSkipPl3 /*= NULL*/)
{
	BallTouchInfo *info = LastInfo(wasShooting, pSkipPl, pSkipPl2, pSkipPl3);
	return info ? info->m_nTeam : TEAM_INVALID;
}

int CMatchBall::LastOppTeam(bool wasShooting, CSDKPlayer *pSkipPl /*= NULL*/, CSDKPlayer *pSkipPl2 /*= NULL*/, CSDKPlayer *pSkipPl3 /*= NULL*/)
{
	BallTouchInfo *info = LastInfo(wasShooting, pSkipPl, pSkipPl2, pSkipPl3);
	return info ? (info->m_nTeam == TEAM_A ? TEAM_B : TEAM_A) : TEAM_INVALID;
}

bool CMatchBall::IsLegallyCatchableByKeeper()
{
	if (SDKGameRules()->IsIntermissionState() || SDKGameRules()->State_Get() == MATCH_PERIOD_PENALTIES)
		return true;

	// Skip the keeper
	BallTouchInfo *pLastTouch = LastInfo(false, m_pPl);
	BallTouchInfo *pLastShot = LastInfo(true, m_pPl);

	if (pLastTouch && pLastShot)
	{
		// Can catch if opponent has touched or shot the ball
		if (pLastTouch->m_nTeam != m_pPl->GetTeamNumber() || pLastShot->m_nTeam != m_pPl->GetTeamNumber())
			return true;

		if (pLastShot->m_eBodyPart == BODY_PART_HEAD || pLastShot->m_eBodyPart == BODY_PART_CHEST)
		{
			// Only allow the keeper to pick up the ball if the shot or touch before the last header or chest action was by an opponent
			for (int i = m_Touches.Count() - 1; i >= 1; i--)
			{
				if (m_Touches[i]->m_bIsShot)
				{
					for (int j = i - 1; j >= 0; j--)
					{
						if (m_Touches[j]->m_bIsShot)
						{
							if (m_Touches[j]->m_nTeam != pLastTouch->m_nTeam)
								return true;

							break;
						}
					}

					break;
				}
			}
		}
	}

	return false;
}