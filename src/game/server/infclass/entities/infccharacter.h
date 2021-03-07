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

	struct WeaponFireContext
	{
		int Weapon = 0;
		bool FireAccepted = false;
	};

	void OnWeaponFired(WeaponFireContext *pFireContext);

	void OnHammerFired(WeaponFireContext *pFireContext);
	void OnGunFired(WeaponFireContext *pFireContext);
	void OnShotgunFired(WeaponFireContext *pFireContext);
	void OnGrenadeFired(WeaponFireContext *pFireContext);
	void OnLaserFired(WeaponFireContext *pFireContext);
	void OnNinjaFired(WeaponFireContext *pFireContext);

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
