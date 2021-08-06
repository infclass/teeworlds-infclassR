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

	void GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams) override;

	void OnCharacterPreCoreTick() override;
	void OnCharacterTick() override;
	void OnCharacterSnap(int SnappingClient) override;
	void OnSlimeEffect(int Owner) override;

	static bool SetupSkin(int PlayerClass, CTeeInfo *output);

protected:
	void GiveClassAttributes() override;
	void SetupSkin(CTeeInfo *output) override;
	void BroadcastWeaponState() override;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
