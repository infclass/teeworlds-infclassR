#ifndef GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
#define GAME_SERVER_INFCLASS_CLASSES_HUMAN_H

#include "../infcplayerclass.h"

#include <base/tl/ic_array.h>
#include <game/server/alloc.h>

class CHeroFlag;
class CMercenaryBomb;
class CWhiteHole;

enum class EGiftType
{
	BonusZone,
	HeroFlag,
};

class CInfClassHuman : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	explicit CInfClassHuman(CInfClassPlayer *pPlayer);

	static CInfClassHuman *GetInstance(CInfClassPlayer *pPlayer);
	static CInfClassHuman *GetInstance(CInfClassCharacter *pCharacter);
	static CInfClassHuman *GetInstance(CInfClassPlayerClass *pClass);

	bool IsHuman() const final { return true; }

	SkinGetter GetSkinGetter() const override;
	void SetupSkinContext(CSkinContext *pOutput, bool ForSameTeam) const override;
	static bool SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion);

	void GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams) override;
	int GetJumps() const override;

	void GiveGift(EGiftType GiftType);

	bool CanBeHit() const override;
	SClassUpgrade GetNextUpgrade() const override;
	void OnPlayerClassChanged() override;

	void OnPlayerSnap(int SnappingClient, int InfClassVersion) override;

	void OnCharacterPreCoreTick() override;
	void OnCharacterTick() override;
	void OnCharacterTickPaused() override;
	void OnCharacterPostCoreTick() override;
	void OnCharacterSnap(int SnappingClient) override;
	void OnCharacterSpawned(const SpawnContext &Context) override;
	void OnCharacterDamage(SDamageContext *pContext) override;

	void OnKilledCharacter(CInfClassCharacter *pVictim, const DeathContext &Context) override;
	void OnHumanHammerHitHuman(CInfClassCharacter *pTarget);

	void OnHookAttachedPlayer() override;

	void HandleNinja() override;

	void OnWeaponFired(WeaponFireContext *pFireContext) override;
	void OnHammerFired(WeaponFireContext *pFireContext) override;
	void OnGunFired(WeaponFireContext *pFireContext) override;
	void OnShotgunFired(WeaponFireContext *pFireContext) override;
	void OnGrenadeFired(WeaponFireContext *pFireContext) override;
	void OnLaserFired(WeaponFireContext *pFireContext) override;

	void OnSlimeEffect(int Owner, int Damage, float DamageInterval) override;
	bool HasWhiteHole() const;
	void GiveWhiteHole();
	void RemoveWhiteHole();

	void UpgradeMercBomb(CMercenaryBomb *pBomb, float UpgradePoints);
	void OnHeroFlagTaken(CInfClassCharacter *pHero);
	void OnWhiteHoleSpawned(CWhiteHole *pWhiteHole);
	void GiveUpgrade() override;

protected:
	void GiveClassAttributes() override;
	void DestroyChildEntities() override;
	void BroadcastWeaponState() const override;

	void ResetUpgrades();

	void CheckSuperWeaponAccess();
	void OnNinjaTargetKiller(bool Assisted);
	void GiveNinjaBuf();

	void SnapHero(int SnappingClient);
	void SnapScientist(int SnappingClient);

	void ActivateNinja(WeaponFireContext *pFireContext);
	void PlaceEngineerWall(WeaponFireContext *pFireContext);
	void PlaceLooperWall(WeaponFireContext *pFireContext);
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

	std::optional<vec2> FindPortalPosition();

private:
	icArray<int, 2> m_BarrierHintIds;

	int m_BonusTick = 0;
	int m_ResetKillsTick = 0;
	float m_KillsProgression = 0;
	float m_WeaponRegenIntervalModifier[NUM_WEAPONS];
	float m_WeaponReloadIntervalModifier[NUM_WEAPONS];
	float m_LaserReachModifier = 1.0f;
	int m_TurretCount = 0;
	int m_BroadcastWhiteHoleReady; // used to broadcast "WhiteHole ready" for a short period of time
	int m_PositionLockTicksRemaining = 0;
	vec2 m_PositionLockPosition{};
	int m_NinjaTargetTick = 0;
	int m_NinjaTargetCid = -1;
	int m_NinjaVelocityBuff = 0;
	int m_NinjaExtraDamage = 0;
	int m_NinjaAmmoBuff = 0;
	icArray<CEntity *, 24> m_apHitObjects;
	bool m_HasWhiteHole = false;

	CHeroFlag *m_pHeroFlag = nullptr;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
