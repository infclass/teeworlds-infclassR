#ifndef GAME_SERVER_INFCLASS_PLAYER_H
#define GAME_SERVER_INFCLASS_PLAYER_H

#include <game/gamecore.h>

class CGameContext;
class CInfClassCharacter;

// We actually have to include player.h after all this stuff above.
#include <game/server/player.h>

class CInfClassPlayer : public CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassPlayer(CGameContext *pGameServer, int ClientID, int Team);

	void TryRespawn() override;

protected:
	CInfClassCharacter *m_pInfcCharacter = nullptr;
};

#endif // GAME_SERVER_INFCLASS_PLAYER_H
