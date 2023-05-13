#ifndef GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
#define GAME_SERVER_INFCLASS_CLASSES_HUMAN_H

#include "../infcplayerclass.h"

#include <base/tl/ic_array.h>
#include <game/server/alloc.h>

class CHeroFlag;
class CWhiteHole;

enum
{
	GIFT_HEROFLAG = 0,
};

class CInfClassHuman : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	explicit CInfClassHuman(CInfClassPlayer *pPlayer);
	~CInfClassHuman();

	static CInfClassHuman *GetInstance(CInfClassPlayer *pPlayer);
	static CInfClassHuman *GetInstance(CInfClassCharacter *pCharacter);

	bool IsHuman() const final { return true; }

	SkinGetter GetSkinGetter() const override;
	void SetupSkinContext(CSkinContext *pOutput, bool ForSameTeam) const override;
	static bool SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion);

	void GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams) override;
	int GetJumps() const override;

	void GiveGift(int GiftType);

	bool CanBeHit() const override;

	void OnPlayerSnap(int SnappingClient, int InfClassVersion) override;

	void OnCharacterPreCoreTick() override;
	void OnCharacterTick() override;
	void OnCharacterSnap(int SnappingClient) override;
	void OnCharacterDamage(SDamageContext *pContext) override;

	void OnKilledCharacter(int Victim, bool Assisted) override;
	void OnHumanHammerHitHuman(CInfClassCharacter *pTarget);

	void OnHookAttachedPlayer() override;

	void OnHammerFired(WeaponFireContext *pFireContext) override;
	void OnGunFired(WeaponFireContext *pFireContext) override;
	void OnShotgunFired(WeaponFireContext *pFireContext) override;
	void OnGrenadeFired(WeaponFireContext *pFireContext) override;
	void OnLaserFired(WeaponFireContext *pFireContext) override;

	void OnSlimeEffect(int Owner) override;
	bool HasWhiteHole() const;
	void GiveWhiteHole();
	void RemoveWhiteHole();

	void OnHeroFlagTaken(CInfClassCharacter *pHero);
	void OnWhiteHoleSpawned(const CWhiteHole *pWhiteHole);

protected:
	void GiveClassAttributes() override;
	void DestroyChildEntities() override;
	void BroadcastWeaponState() const override;

	void CheckSuperWeaponAccess();
	void OnNinjaTargetKiller(bool Assisted);

	void SnapHero(int SnappingClient);
	void SnapEngineer(int SnappingClient);
	void SnapLooper(int SnappingClient);
	void SnapScientist(int SnappingClient);

	void ActivateNinja(WeaponFireContext *pFireContext);
	void PlaceEngineerWall(WeaponFireContext *pFireContext);
	void PlaceLooperWall(WeaponFireContext *pFireContext);
	void FireSoldierBomb(WeaponFireContext *pFireContext);
	void FireMercenaryBomb(WeaponFireContext *pFireContext);
	void PlaceScientistMine(WeaponFireContext *pFireContext);
	void PlaceTurret(WeaponFireContext *pFireContext);

	void OnMercGrenadeFired(WeaponFireContext *pFireContext);
	void OnMedicGrenadeFired(WeaponFireContext *pFireContext);
	void OnPortalGunFired(WeaponFireContext *pFireContext);

	void OnBlindingLaserFired(WeaponFireContext *pFireContext);
	void OnBiologistLaserFired(WeaponFireContext *pFireContext);
	void OnMercLaserFired(WeaponFireContext *pFireContext);

	bool PositionLockAvailable() const;

	bool FindPortalPosition(vec2 *pPosition);

private:
	bool m_FirstShot = false;
	vec2 m_FirstShotCoord;

	icArray<int, 2> m_BarrierHintIDs;

	int m_BonusTick = 0;
	int m_ResetKillsTime = 0;
	int m_KillsProgression = 0;
	int m_TurretCount = 0;
	int m_BroadcastWhiteHoleReady; // used to broadcast "WhiteHole ready" for a short period of time
	int m_PositionLockTicksRemaining = 0;
	int m_NinjaTargetTick = 0;
	int m_NinjaTargetCID = -1;
	bool m_HasWhiteHole = false;

	CHeroFlag *m_pHeroFlag = nullptr;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
