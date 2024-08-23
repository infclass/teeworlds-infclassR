#ifndef GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
#define GAME_SERVER_INFCLASS_CLASSES_INFECTED_H

#include "../infcplayerclass.h"

#include <game/server/alloc.h>
#include <game/server/infclass/death_context.h>

class CSlugSlime;

class CInfClassInfected : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassInfected(CInfClassPlayer *pPlayer);

	static CInfClassInfected *GetInstance(CInfClassCharacter *pCharacter);

	bool IsHuman() const final { return false; }

	SkinGetter GetSkinGetter() const override;
	void SetupSkinContext(CSkinContext *pOutput, bool ForSameTeam) const override;
	static bool SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion);

	void ResetNormalEmote() override;
	int GetJumps() const override;

	void OnPlayerSnap(int SnappingClient, int InfClassVersion) override;

	bool CanDie() const override;
	bool CanBeUnfreezed() const override;

	void OnCharacterPreCoreTick() override;
	void OnCharacterTick() override;
	void OnCharacterTickPaused() override;
	void OnCharacterPostCoreTick() override;
	void OnCharacterTickDeferred() override;
	void OnCharacterSnap(int SnappingClient) override;
	void OnCharacterSpawned(const SpawnContext &Context) override;
	void OnCharacterDamage(SDamageContext *pContext) override;
	void OnCharacterDeath(EDamageType DamageType) override;

	void OnHookAttachedPlayer() override;

	void OnHammerFired(WeaponFireContext *pFireContext) override;

	void OnSlimeEffect(int Owner, int Damage, float DamageInterval) override;
	void OnFloatingPointCollected(int Points) override;
	void OnLaserWall();

	float GetGhoulPercent() const override;
	void IncreaseGhoulLevel(int Diff);
	int GetGhoulLevel() const;

	bool FindWitchSpawnPosition(vec2 &Position);

	void PrepareToDie(const DeathContext &Context, bool *pRefusedToDie) override;

protected:
	void GiveClassAttributes() override;
	void BroadcastWeaponState() const override;

	void DoBoomerExplosion();
	void PlaceSlugSlime(WeaponFireContext *pFireContext);
	CSlugSlime *PlaceSlime(vec2 PlaceToPos, float MinDistance);

	void SetHookOnLimit(bool OnLimit);

	void GhostPreCoreTick();
	void SpiderPreCoreTick();
	bool HasDrainingHook() const;
	bool HasHumansNearby();

	int m_HookDmgTick = 0;
	int m_SlimeEffectTicks = 0;
	int m_SlimeLastHealTick = 0;
	int m_LaserWallTick = 0;
	int m_LastSeenTick = 0;

	int m_VoodooTimeAlive = 0;
	DeathContext m_VoodooDeathContext;
	bool m_VoodooAboutToDie = false;

	bool m_HookOnTheLimit = false;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
