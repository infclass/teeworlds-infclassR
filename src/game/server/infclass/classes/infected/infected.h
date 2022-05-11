#ifndef GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
#define GAME_SERVER_INFCLASS_CLASSES_INFECTED_H

#include "../infcplayerclass.h"

#include <game/server/alloc.h>

class CInfClassInfected : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassInfected(CInfClassPlayer *pPlayer);

	static CInfClassInfected *GetInstance(CInfClassCharacter *pCharacter);

	bool IsHuman() const final { return false; }

	SkinGetter GetSkinGetter() const override;
	void SetupSkinContext(CSkinContext *pOutput) const override;
	static bool SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion);

	int GetDefaultEmote() const override;
	bool CanDie() const override;
	bool CanBeUnfreezed() const;

	void OnCharacterPreCoreTick() override;
	void OnCharacterTick() override;
	void OnCharacterSnap(int SnappingClient) override;
	void OnCharacterSpawned(const SpawnContext &Context) override;
	void OnCharacterDeath(DAMAGE_TYPE DamageType) override;

	void OnSlimeEffect(int Owner) override;
	void OnFloatingPointCollected(int Points) override;
	void OnLaserWall();

	float GetGhoulPercent() const override;
	void IncreaseGhoulLevel(int Diff);
	int GetGhoulLevel() const;

	bool FindWitchSpawnPosition(vec2 &Position);

	void PrepareToDie(int Killer, DAMAGE_TYPE DamageType, bool *pRefusedToDie) override;

protected:
	void GiveClassAttributes() override;
	void BroadcastWeaponState() override;

	void DoBoomerExplosion();

	void SetHookOnLimit(bool OnLimit);

	int m_SlimeHealTick = 0;
	int m_LaserWallTick = 0;

	int m_VoodooTimeAlive = 0;
	int m_VoodooKiller; // Save killer + weapon for delayed kill message
	DAMAGE_TYPE m_VoodooDamageType;
	bool m_VoodooAboutToDie = false;

	bool m_HookOnTheLimit = false;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
