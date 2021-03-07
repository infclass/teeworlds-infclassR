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
	~CInfClassCharacter() override;

	void Tick() override;

	void FireWeapon() override;

	void OnWeaponFired(int Weapon, bool *pFireAccepted);

	void OnHammerFired(bool *pFireAccepted);
	void OnGunFired(bool *pFireAccepted);
	void OnShotgunFired(bool *pFireAccepted);
	void OnGrenadeFired(bool *pFireAccepted);
	void OnLaserFired(bool *pFireAccepted);
	void OnNinjaFired(bool *pFireAccepted);

	void HandleMapMenu() override;

	void Die(int Killer, int Weapon) override;

	void SetActiveWeapon(int Weapon);
	void SetLastWeapon(int Weapon);
	void TakeAllWeapons();

	CInfClassPlayerClass *GetClass() { return m_pClass; }
	void SetClass(CInfClassPlayerClass *pClass);

	CInputCount CountFireInput() const;
	bool FireJustPressed() const;

	CGameContext *GameContext() const { return m_pContext; }

protected:
	CGameContext *m_pContext = nullptr;
	CInfClassPlayerClass *m_pClass = nullptr;
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
