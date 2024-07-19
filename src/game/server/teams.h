/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_SERVER_TEAMS_H
#define GAME_SERVER_TEAMS_H

#include <game/server/gamecontext.h>
#include <game/teamscore.h>

class CGameTeams
{
	CGameContext *m_pGameContext;

public:
	CTeamsCore m_Core;

	CGameTeams(CGameContext *pGameContext);

	// helper methods
	CCharacter *Character(int ClientId)
	{
		return GameServer()->GetPlayerChar(ClientId);
	}
	CPlayer *GetPlayer(int ClientId)
	{
		return GameServer()->m_apPlayers[ClientId];
	}

	class CGameContext *GameServer()
	{
		return m_pGameContext;
	}
	class IServer *Server()
	{
		return m_pGameContext->Server();
	}

	// returns nullptr if successful, error string if failed
	const char *SetCharacterTeam(int ClientId, int Team);

	int64_t TeamMask(int Team, int ExceptId = -1, int Asker = -1);

	int Count(int Team) const;

	// need to be very careful using this method. SERIOUSLY...
	void SetForceCharacterTeam(int ClientId, int Team);

	void Reset();
};

#endif
