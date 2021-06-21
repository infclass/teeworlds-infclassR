#ifndef GAME_SERVER_INFCLASS_PLAYER_H
#define GAME_SERVER_INFCLASS_PLAYER_H

#include <game/gamecore.h>

class CGameContext;
class CInfClassGameController;
class CInfClassPlayerClass;

// We actually have to include player.h after all this stuff above.
#include <game/server/player.h>

class CInfClassPlayer : public CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassPlayer(CInfClassGameController *pGameController, int ClientID, int Team);
	~CInfClassPlayer() override;

	CInfClassGameController *GameController();

	void TryRespawn() override;

	void Tick() override;
	int GetDefaultEmote() const override;

	CInfClassPlayerClass *GetCharacterClass() { return m_pInfcPlayerClass; }
	const CInfClassPlayerClass *GetCharacterClass() const { return m_pInfcPlayerClass; }
	void SetCharacterClass(CInfClassPlayerClass *pClass);

protected:
	void onClassChanged() override;
	const char *GetClan(int SnappingClient = -1) const override;

	CInfClassGameController *m_pGameController = nullptr;
	CInfClassPlayerClass *m_pInfcPlayerClass = nullptr;
};

#endif // GAME_SERVER_INFCLASS_PLAYER_H
