#include "infcplayer.h"

MACRO_ALLOC_POOL_ID_IMPL(CInfClassPlayer, MAX_CLIENTS)

CInfClassPlayer::CInfClassPlayer(CGameContext *pGameServer, int ClientID, int Team)
	: CPlayer(pGameServer, ClientID, Team)
{
}
