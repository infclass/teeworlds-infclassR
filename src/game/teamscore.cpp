/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "teamscore.h"
#include <engine/shared/config.h>
#include <game/generated/protocol.h>

CTeamsCore::CTeamsCore()
{
	Reset();
}

bool CTeamsCore::SameTeam(int ClientID1, int ClientID2) const
{
	return m_aTeam[ClientID1] == TEAM_SUPER || m_aTeam[ClientID2] == TEAM_SUPER || m_aTeam[ClientID1] == m_aTeam[ClientID2];
}

int CTeamsCore::Team(int ClientID) const
{
	return m_aIsInfected[ClientID] ? TEAM_RED : TEAM_BLUE;
}

void CTeamsCore::Team(int ClientID, int Team)
{
	dbg_assert(Team >= TEAM_FLOCK && Team <= TEAM_SUPER, "invalid team");
	m_aTeam[ClientID] = Team;
}

bool CTeamsCore::CanKeepHook(int ClientID1, int ClientID2) const
{
	return CanHook(ClientID1, ClientID2);
}

bool CTeamsCore::CanCollide(int ClientID1, int ClientID2) const
{
	if(m_IsInfclass)
	{
		if(ClientID1 == ClientID2)
			return true;

		if(m_aIsInfected[ClientID1] != m_aIsInfected[ClientID2])
			return true;

		// Only infected can collide
		if(!m_aIsInfected[ClientID1])
			return false;

		return !m_aIsProtected[ClientID1] && !m_aIsProtected[ClientID2];
	}
	
	if(m_aTeam[ClientID1] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || m_aTeam[ClientID2] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || ClientID1 == ClientID2)
		return true;
	if(m_aIsSolo[ClientID1] || m_aIsSolo[ClientID2])
		return false;
	return m_aTeam[ClientID1] == m_aTeam[ClientID2];
}

bool CTeamsCore::CanHook(int HookerID, int TargetID) const
{
	if(m_IsInfclass)
	{
		if(m_aIsInfected[HookerID] != m_aIsInfected[TargetID])
			return true;

		return !m_aIsProtected[TargetID];
	}

	if(m_aTeam[HookerID] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || m_aTeam[TargetID] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || HookerID == TargetID)
		return true;
	if(m_aIsSolo[HookerID] || m_aIsSolo[TargetID])
		return false;
	return m_aTeam[HookerID] == m_aTeam[TargetID];
}

void CTeamsCore::Reset()
{
	m_IsDDRace16 = false;
	m_IsInfclass = false;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(g_Config.m_SvTeam == SV_TEAM_FORCED_SOLO)
			m_aTeam[i] = i;
		else
			m_aTeam[i] = TEAM_FLOCK;
		m_aIsSolo[i] = false;

		m_aIsInfected[i] = false;
		m_aIsProtected[i] = false;
	}
}

void CTeamsCore::SetSolo(int ClientID, bool Value)
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "Invalid client id");
	m_aIsSolo[ClientID] = Value;
}

bool CTeamsCore::GetSolo(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	return m_aIsSolo[ClientID];
}

void CTeamsCore::SetInfected(int ClientID, bool Value)
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "Invalid client id");
	m_aIsInfected[ClientID] = Value;
}
void CTeamsCore::SetProtected(int ClientID, bool Value)
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "Invalid client id");
	m_aIsProtected[ClientID] = Value;
}
