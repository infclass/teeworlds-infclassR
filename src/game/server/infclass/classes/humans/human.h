#ifndef GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
#define GAME_SERVER_INFCLASS_CLASSES_HUMAN_H

#include "../infcplayerclass.h"

#include <game/server/alloc.h>

class CInfClassHuman : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	explicit CInfClassHuman(CInfClassPlayer *pPlayer);

	bool IsHuman() const final { return true; }

	SkinGetter SetupSkin(CSkinContext *pOutput) const override;
	static bool SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion);

	void GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams) override;

	void OnCharacterPreCoreTick() override;
	void OnCharacterTick() override;
	void OnCharacterSnap(int SnappingClient) override;

	void OnHammerFired(WeaponFireContext *pFireContext) override;
	void OnLaserFired(WeaponFireContext *pFireContext) override;

	void OnSlimeEffect(int Owner) override;

protected:
	void GiveClassAttributes() override;
	void DestroyChildEntities() override;
	void BroadcastWeaponState() override;

	void OnBlindingLaserFired(WeaponFireContext *pFireContext);

	bool PositionLockAvailable() const;

private:
	int m_PositionLockTicksRemaining = 0;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
