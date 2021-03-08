#ifndef GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
#define GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H

#include <game/server/entities/character.h>

class CGameContext;
class CInfClassGameController;
class CInfClassPlayerClass;

class CInfClassCharacter : public CCharacter
{
	MACRO_ALLOC_POOL_ID()
public:
	CInfClassCharacter(CInfClassGameController *pGameController);
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

	CInfClassGameController *GameController() const { return m_pGameController; }
	CGameContext *GameContext() const;

	bool CanDie() const;

	void CheckSuperWeaponAccess();
	void FireSoldierBomb();
	void PlacePortal();
	CPortal *FindPortalInTarget();
	void OnPortalDestroy(CPortal *pPortal);
	bool ProcessCharacterOnPortal(CPortal *pPortal, CCharacter *pCharacter);
	bool CanOpenPortals() const;

protected:
	CInfClassGameController *m_pGameController = nullptr;
	CInfClassPlayerClass *m_pClass = nullptr;
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
