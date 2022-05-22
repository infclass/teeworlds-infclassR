/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "teamscore.h"
#include <base/math.h>
#include <engine/shared/config.h>

CTeamsCore::CTeamsCore()
{
	Reset();
}

bool CTeamsCore::SameTeam(int ClientID1, int ClientID2) const
{
	return m_Team[ClientID1] == TEAM_SUPER || m_Team[ClientID2] == TEAM_SUPER || m_Team[ClientID1] == m_Team[ClientID2];
}

int CTeamsCore::Team(int ClientID) const
{
	return m_Team[ClientID];
}

void CTeamsCore::Team(int ClientID, int Team)
{
	dbg_assert(Team >= TEAM_FLOCK && Team <= TEAM_SUPER, "invalid team");
	m_Team[ClientID] = Team;
}

bool CTeamsCore::CanKeepHook(int ClientID1, int ClientID2) const
{
	if(m_Team[ClientID1] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || m_Team[ClientID2] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || ClientID1 == ClientID2)
		return true;
	return m_Team[ClientID1] == m_Team[ClientID2];
}

bool CTeamsCore::CanCollide(int ClientID1, int ClientID2) const
{
	if(m_IsInfclass)
	{
		if(ClientID1 == ClientID2)
			return true;

		if(m_IsInfected[ClientID1] != m_IsInfected[ClientID2])
			return true;

		// Only infected can collide
		if(!m_IsInfected[ClientID1])
			return false;

		return !m_IsProtected[ClientID1] && !m_IsProtected[ClientID2];
	}

	if(m_Team[ClientID1] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || m_Team[ClientID2] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || ClientID1 == ClientID2)
		return true;
	if(m_IsSolo[ClientID1] || m_IsSolo[ClientID2])
		return false;
	return m_Team[ClientID1] == m_Team[ClientID2];
}

bool CTeamsCore::CanHook(int HookerID, int TargetID) const
{
	if(m_IsInfclass)
	{
		if(m_IsInfected[HookerID] != m_IsInfected[TargetID])
			return true;

		return !m_IsProtected[TargetID];
	}

	if(m_Team[HookerID] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || m_Team[TargetID] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || HookerID == TargetID)
		return true;
	if(m_IsSolo[HookerID] || m_IsSolo[TargetID])
		return false;
	return m_Team[HookerID] == m_Team[TargetID];
}

void CTeamsCore::Reset()
{
	m_IsDDRace16 = false;
	m_IsInfclass = false;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
			m_Team[i] = i;
		else
			m_Team[i] = TEAM_FLOCK;
		m_IsSolo[i] = false;

		m_IsInfected[i] = false;
		m_IsProtected[i] = false;
	}
}
