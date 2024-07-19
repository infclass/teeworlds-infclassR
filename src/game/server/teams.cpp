/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "teams.h"
#include "player.h"

CGameTeams::CGameTeams(CGameContext *pGameContext) :
	m_pGameContext(pGameContext)
{
	Reset();
}

void CGameTeams::Reset()
{
	m_Core.Reset();
}

const char *CGameTeams::SetCharacterTeam(int ClientId, int Team)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return "Invalid client ID";
	if(Team < 0 || Team >= MAX_CLIENTS + 1)
		return "Invalid team number";
	if(m_Core.Team(ClientId) == Team)
		return "You are in this team already";
	if(!Character(ClientId))
		return "Your character is not valid";

	SetForceCharacterTeam(ClientId, Team);
	return nullptr;
}

int64_t CGameTeams::TeamMask(int Team, int ExceptId, int Asker)
{
	if(Team == TEAM_SUPER)
	{
		if(ExceptId == -1)
			return 0xffffffffffffffff;
		return 0xffffffffffffffff & ~(1 << ExceptId);
	}

	int64_t Mask = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ExceptId)
			continue; // Explicitly excluded
		if(!GetPlayer(i))
			continue; // Player doesn't exist

		if(!(GetPlayer(i)->GetTeam() == TEAM_SPECTATORS || GetPlayer(i)->IsPaused()))
		{ // Not spectator
			if(i != Asker)
			{ // Actions of other players
				if(!Character(i))
					continue; // Player is currently dead
				if(GetPlayer(i)->m_ShowOthers == SHOW_OTHERS_ONLY_TEAM)
				{
					if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
						continue; // In different teams
				}
				else if(GetPlayer(i)->m_ShowOthers == SHOW_OTHERS_OFF)
				{
					if(m_Core.GetSolo(Asker))
						continue; // When in solo part don't show others
					if(m_Core.GetSolo(i))
						continue; // When in solo part don't show others
					if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
						continue; // In different teams
				}
			} // See everything of yourself
		}
		else if(GetPlayer(i)->m_SpectatorId != SPEC_FREEVIEW)
		{ // Spectating specific player
			if(GetPlayer(i)->m_SpectatorId != Asker)
			{ // Actions of other players
				if(!Character(GetPlayer(i)->m_SpectatorId))
					continue; // Player is currently dead
				if(GetPlayer(i)->m_ShowOthers == SHOW_OTHERS_ONLY_TEAM)
				{
					if(m_Core.Team(GetPlayer(i)->m_SpectatorId) != Team && m_Core.Team(GetPlayer(i)->m_SpectatorId) != TEAM_SUPER)
						continue; // In different teams
				}
				else if(GetPlayer(i)->m_ShowOthers == SHOW_OTHERS_OFF)
				{
					if(m_Core.GetSolo(Asker))
						continue; // When in solo part don't show others
					if(m_Core.GetSolo(GetPlayer(i)->m_SpectatorId))
						continue; // When in solo part don't show others
					if(m_Core.Team(GetPlayer(i)->m_SpectatorId) != Team && m_Core.Team(GetPlayer(i)->m_SpectatorId) != TEAM_SUPER)
						continue; // In different teams
				}
			} // See everything of player you're spectating
		}
		else
		{ // Freeview
			if(GetPlayer(i)->m_SpecTeam)
			{ // Show only players in own team when spectating
				if(m_Core.Team(i) != Team && m_Core.Team(i) != TEAM_SUPER)
					continue; // in different teams
			}
		}

		Mask |= 1LL << i;
	}
	return Mask;
}

int CGameTeams::Count(int Team) const
{
	if(Team == TEAM_SUPER)
		return -1;

	int Count = 0;

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(m_Core.Team(i) == Team)
			Count++;

	return Count;
}

void CGameTeams::SetForceCharacterTeam(int ClientId, int Team)
{
	m_Core.Team(ClientId, Team);
}
