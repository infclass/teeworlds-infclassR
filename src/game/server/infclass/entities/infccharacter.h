#ifndef GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
#define GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H

#include <game/server/entities/character.h>

class CGameContext;
class CInfClassPlayerClass;

class CInfClassCharacter : public CCharacter
{
	MACRO_ALLOC_POOL_ID()
public:
	CInfClassCharacter(CGameContext *pContext);

	void Tick() override;

	void SetActiveWeapon(int Weapon);
	void SetLastWeapon(int Weapon);
	void TakeAllWeapons();

	CInfClassPlayerClass *GetClass() { return m_pClass; }
	void SetClass(CInfClassPlayerClass *pClass);

	CGameContext *GameContext() const { return m_pContext; }

protected:
	CGameContext *m_pContext = nullptr;
	CInfClassPlayerClass *m_pClass = nullptr;
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
