#ifndef GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
#define GAME_SERVER_INFCLASS_CLASSES_HUMAN_H

#include "../infcplayerclass.h"

#include <game/server/alloc.h>

class CHeroFlag;

class CInfClassHuman : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	explicit CInfClassHuman(CInfClassPlayer *pPlayer);

	static CInfClassHuman *GetInstance(CInfClassPlayer *pPlayer);
	static CInfClassHuman *GetInstance(CInfClassCharacter *pCharacter);

	bool IsHuman() const final { return true; }

	SkinGetter GetSkinGetter() const override;
	void SetupSkinContext(CSkinContext *pOutput, bool ForSameTeam) const override;
	static bool SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion);

	void GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams) override;

	void CheckSuperWeaponAccess() override;

	void OnPlayerSnap(int SnappingClient, int InfClassVersion) override;

	void OnCharacterPreCoreTick() override;
	void OnCharacterTick() override;
	void OnCharacterSnap(int SnappingClient) override;

	void OnKilledCharacter(int Victim, bool Assisted) override;

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

protected:
	void GiveClassAttributes() override;
	void DestroyChildEntities() override;
	void BroadcastWeaponState() override;

	void OnNinjaTargetKiller(bool Assisted);
	void OnBlindingLaserFired(WeaponFireContext *pFireContext);
	void OnBiologistLaserFired(WeaponFireContext *pFireContext);
	void OnMercLaserFired(WeaponFireContext *pFireContext);

	bool PositionLockAvailable() const;

	bool FindPortalPosition(vec2 *pPosition);

private:
	int m_BroadcastWhiteHoleReady; // used to broadcast "WhiteHole ready" for a short period of time
	int m_PositionLockTicksRemaining = 0;
	int m_NinjaTargetTick = 0;
	int m_NinjaTargetCID = -1;
	bool m_HasWhiteHole;

	CHeroFlag *m_pHeroFlag = nullptr;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
