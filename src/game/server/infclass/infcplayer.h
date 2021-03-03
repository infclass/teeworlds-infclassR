#ifndef GAME_SERVER_INFCLASS_PLAYER_H
#define GAME_SERVER_INFCLASS_PLAYER_H

#include <game/gamecore.h>

class CGameContext;
class CInfClassPlayerClass;

// We actually have to include player.h after all this stuff above.
#include <game/server/player.h>

class CInfClassPlayer : public CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassPlayer(CGameContext *pGameServer, int ClientID, int Team);
	~CInfClassPlayer() override;

	void TryRespawn() override;

	CInfClassPlayerClass *GetCharacterClass() { return m_pInfcPlayerClass; }
	const CInfClassPlayerClass *GetCharacterClass() const { return m_pInfcPlayerClass; }
	void SetCharacterClass(CInfClassPlayerClass *pClass);

protected:
	void onClassChanged() override;

	CInfClassPlayerClass *m_pInfcPlayerClass = nullptr;
};

#endif // GAME_SERVER_INFCLASS_PLAYER_H
