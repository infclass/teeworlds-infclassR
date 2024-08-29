/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "teamscore.h"
#include <base/system.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>

CTeamsCore::CTeamsCore()
{
	Reset();
}

bool CTeamsCore::SameTeam(int ClientId1, int ClientId2) const
{
	return m_aTeam[ClientId1] == TEAM_SUPER || m_aTeam[ClientId2] == TEAM_SUPER || m_aTeam[ClientId1] == m_aTeam[ClientId2];
}

int CTeamsCore::Team(int ClientId) const
{
	return m_aIsInfected[ClientId] ? TEAM_RED : TEAM_BLUE;
}

void CTeamsCore::Team(int ClientId, int Team)
{
	dbg_assert(Team >= TEAM_FLOCK && Team <= TEAM_SUPER, "invalid team");
	m_aTeam[ClientId] = Team;
}

bool CTeamsCore::CanKeepHook(int ClientId1, int ClientId2) const
{
	return CanHook(ClientId1, ClientId2);
}

bool CTeamsCore::CanCollide(int ClientId1, int ClientId2) const
{
	if(m_IsInfclass)
	{
		if(ClientId1 == ClientId2)
			return true;

		if(m_aIsInfected[ClientId1] != m_aIsInfected[ClientId2])
			return true;

		// Only infected can collide
		if(!m_aIsInfected[ClientId1])
			return false;

		return !m_aIsProtected[ClientId1] && !m_aIsProtected[ClientId2];
	}
	
	if(m_aTeam[ClientId1] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || m_aTeam[ClientId2] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || ClientId1 == ClientId2)
		return true;
	if(m_aIsSolo[ClientId1] || m_aIsSolo[ClientId2])
		return false;
	return m_aTeam[ClientId1] == m_aTeam[ClientId2];
}

bool CTeamsCore::CanHook(int HookerId, int TargetId) const
{
	if(m_IsInfclass)
	{
		if(m_aIsInfected[HookerId] != m_aIsInfected[TargetId])
			return true;

		return !m_aIsProtected[TargetId];
	}

	if(m_aTeam[HookerId] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || m_aTeam[TargetId] == (m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER) || HookerId == TargetId)
		return true;
	if(m_aIsSolo[HookerId] || m_aIsSolo[TargetId])
		return false;
	return m_aTeam[HookerId] == m_aTeam[TargetId];
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

void CTeamsCore::SetSolo(int ClientId, bool Value)
{
	dbg_assert(ClientId >= 0 && ClientId < MAX_CLIENTS, "Invalid client id");
	m_aIsSolo[ClientId] = Value;
}

bool CTeamsCore::GetSolo(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	return m_aIsSolo[ClientId];
}

void CTeamsCore::SetInfected(int ClientId, bool Value)
{
	dbg_assert(ClientId >= 0 && ClientId < MAX_CLIENTS, "Invalid client id");
	m_aIsInfected[ClientId] = Value;
}
void CTeamsCore::SetProtected(int ClientId, bool Value)
{
	dbg_assert(ClientId >= 0 && ClientId < MAX_CLIENTS, "Invalid client id");
	m_aIsProtected[ClientId] = Value;
}
