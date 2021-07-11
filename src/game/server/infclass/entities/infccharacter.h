#ifndef GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
#define GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H

#include <game/server/entities/character.h>

class CGameContext;
class CInfClassGameController;
class CInfClassPlayerClass;

struct WeaponFireContext
{
	int Weapon = 0;
	bool FireAccepted = false;
	int AmmoAvailable = 0;
	int AmmoConsumed = 0;
	bool NoAmmo = false;
};

class CInfClassCharacter : public CCharacter
{
	MACRO_ALLOC_POOL_ID()
public:
	CInfClassCharacter(CInfClassGameController *pGameController);
	~CInfClassCharacter() override;

	void OnCharacterSpawned();

	void Tick() override;
	void SpecialSnapForClient(int SnappingClient, bool *pDoSnap) override;

	void FireWeapon() override;

	void OnWeaponFired(WeaponFireContext *pFireContext);

	void OnHammerFired(WeaponFireContext *pFireContext);
	void OnGunFired(WeaponFireContext *pFireContext);
	void OnShotgunFired(WeaponFireContext *pFireContext);
	void OnGrenadeFired(WeaponFireContext *pFireContext);
	void OnLaserFired(WeaponFireContext *pFireContext);
	void OnNinjaFired(WeaponFireContext *pFireContext);

	void OnMercGrenadeFired(WeaponFireContext *pFireContext);
	void OnMedicGrenadeFired(WeaponFireContext *pFireContext);
	void OnBiologistLaserFired(WeaponFireContext *pFireContext);

	void OpenClassChooser() override;
	void HandleMapMenu();

	void Die(int Killer, int Weapon) override;

	void SetActiveWeapon(int Weapon);
	void SetLastWeapon(int Weapon);
	void TakeAllWeapons();

	int GetCID() const;

	CInfClassPlayerClass *GetClass() { return m_pClass; }
	void SetClass(CInfClassPlayerClass *pClass);

	CInputCount CountFireInput() const;
	bool FireJustPressed() const;

	CInfClassGameController *GameController() const { return m_pGameController; }
	CGameContext *GameContext() const;

	bool CanDie() const;

	bool IsInvisible() const;
	bool HasHallucination() const;

	void CheckSuperWeaponAccess();
	void MaybeGiveStunGrenades();
	void FireSoldierBomb();
	void PlacePortal(WeaponFireContext *pFireContext);
	CPortal *FindPortalInTarget();
	void OnPortalDestroy(CPortal *pPortal);
	bool ProcessCharacterOnPortal(CPortal *pPortal, CCharacter *pCharacter);
	bool CanOpenPortals() const;

	void GiveRandomClassSelectionBonus();

	void LockPosition();
	void UnlockPosition();

protected:
	void PreCoreTick() override;
	void PostCoreTick() override;

	void UpdateTuningParam();

protected:
	CInfClassGameController *m_pGameController = nullptr;
	CInfClassPlayerClass *m_pClass = nullptr;
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
