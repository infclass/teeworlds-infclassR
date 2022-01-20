#ifndef GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
#define GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H

#include <base/tl/array_on_stack.h>
#include <game/server/entities/character.h>

class CGameContext;
class CInfClassGameController;
class CInfClassPlayer;
class CInfClassPlayerClass;
class CWhiteHole;

enum class DAMAGE_TYPE;

enum
{
	GIFT_HEROFLAG=0,
};

struct CDamagePoint
{
	DAMAGE_TYPE DamageType;
	int From = 0;
	int Amount = 0;
	int Tick = 0;
};

struct EnforcerInfo
{
	int m_CID;
	int m_Tick;
	DAMAGE_TYPE m_DamageType;
};

struct WeaponFireContext
{
	int Weapon = 0;
	bool FireAccepted = false;
	int AmmoAvailable = 0;
	int AmmoConsumed = 0;
	bool NoAmmo = false;
};

struct WeaponRegenParams
{
	int MaxAmmo = 0;
	int RegenInterval = 0;
};

struct SpawnContext
{
	enum SPAWN_TYPE
	{
		MapSpawn,
		WitchSpawn,
	};

	vec2 SpawnPos = vec2(0, 0);
	SPAWN_TYPE SpawnType = MapSpawn;
};

using ClientsArray = array_on_stack<int, 64>; // MAX_CLIENTS

class CInfClassCharacter : public CCharacter
{
	MACRO_ALLOC_POOL_ID()
public:
	CInfClassCharacter(CInfClassGameController *pGameController);
	~CInfClassCharacter() override;

	static const CInfClassCharacter *GetInstance(const CCharacter *pCharacter);
	static CInfClassCharacter *GetInstance(CCharacter *pCharacter);

	void OnCharacterSpawned(const SpawnContext &Context);
	void OnCharacterInInfectionZone();
	void OnCharacterOutOfInfectionZone();
	void OnCharacterInBonusZoneTick();
	void OnWhiteHoleSpawned(const CWhiteHole *pWhiteHole);

	void Destroy() override;
	void Tick() override;
	void TickDefered() override;
	void Snap(int SnappingClient) override;
	void SpecialSnapForClient(int SnappingClient, bool *pDoSnap);

	void HandleNinja() override;
	void HandleWeaponSwitch() override;

	void FireWeapon() override;

	bool TakeDamage(vec2 Force, float Dmg, int From, DAMAGE_TYPE DamageType);
	bool TakeDamage(vec2 Force, int Dmg, int From, int Weapon, TAKEDAMAGEMODE Mode) override;

	bool Heal(int HitPoints, int FromCID = -1);
	bool GiveArmor(int HitPoints, int FromCID = -1);

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

	void OpenClassChooser();
	void HandleMapMenu();
	void HandleWeaponsRegen();
	void HandleHookDraining();
	void HandleIndirectKillerCleanup();

	void Die(int Killer, int Weapon) override;
	void Die(int Killer, DAMAGE_TYPE DamageType);
	void Die(DAMAGE_TYPE DamageType, int Killer, int Assistant);

	void SetActiveWeapon(int Weapon);
	void SetLastWeapon(int Weapon);
	void TakeAllWeapons();

	void AddAmmo(int Weapon, int Ammo);

	int GetCID() const;

	CInfClassPlayer *GetPlayer();

	const CInfClassPlayerClass *GetClass() const { return m_pClass; }
	CInfClassPlayerClass *GetClass() { return m_pClass; }
	void SetClass(CInfClassPlayerClass *pClass);

	CInputCount CountFireInput() const;
	bool FireJustPressed() const;

	int GetActiveWeapon() const { return m_ActiveWeapon; }
	int GetReloadTimer() const { return m_ReloadTimer; }

	vec2 GetHookPos() const;
	int GetHookedPlayer() const;
	void SetHookedPlayer(int ClientID);

	vec2 Velocity() const;
	float Speed() const;

	CInfClassGameController *GameController() const { return m_pGameController; }
	CGameContext *GameContext() const;

	bool CanDie() const;

	bool CanJump() const;
	void EnableJump();

	int GetInAirTick() const { return m_InAirTick; }

	bool IsInvisible() const;
	bool IsInvincible() const; // Invincible here means "ignores all damage"
	bool HasHallucination() const;
	void TryUnfreeze() override;
	FREEZEREASON GetFreezeReason() const { return m_FreezeReason; }

	bool IsBlind() const { return m_BlindnessTicks > 0; }
	void MakeBlind(int ClientID, float Duration);

	float WebHookLength() const;

	void CheckSuperWeaponAccess();
	void FireSoldierBomb();
	void PlaceSlugSlime(WeaponFireContext *pFireContext);

	void GiveGift(int GiftType);
	void GiveRandomClassSelectionBonus();
	void GiveLonelyZombieBonus();
	void MakeVisible();
	void GrantSpawnProtection();

	bool PositionIsLocked() const;
	void LockPosition();
	void UnlockPosition();

	void ResetMovementsInput();

	void GiveNinjaBuf();

	CHeroFlag *GetHeroFlag() { return m_pHeroFlag; }
	int GetFlagCoolDown();

	void GetActualKillers(int GivenKiller, DAMAGE_TYPE GivenWeapon, int *pKiller, int *pAssistant) const;

	void UpdateLastHookers(const ClientsArray &Hookers, int HookerTick);

	void UpdateLastEnforcer(int ClientID, float Force, DAMAGE_TYPE DamageType, int Tick);

	void SaturateVelocity(vec2 Force, float MaxSpeed);
	bool HasPassenger() const;
	CCharacter *GetPassenger();
	int GetInfZoneTick();

	using CCharacter::GameWorld;
	using CCharacter::Server;

	CGameWorld *GameWorld() const;
	const IServer *Server() const;
protected:
	void PreCoreTick() override;
	void PostCoreTick() override;

	void ClassSpawnAttributes();
	void UpdateTuningParam();

	void ResetClassObject();
	void HandleDamage(int From, int Damage, DAMAGE_TYPE DamageType);

	void OnTotalHealthChanged(int Difference) override;

protected:
	CInfClassGameController *m_pGameController = nullptr;
	CInfClassPlayerClass *m_pClass = nullptr;

	ClientsArray m_LastHookers;
	int m_LastHookerTick = -1;

	int m_BlindnessTicks = 0;
	int m_LastBlinder = -1;

	array_on_stack<EnforcerInfo, 4> m_EnforcersInfo;

	int m_DamageTaken = 0;
	array_on_stack<CDamagePoint, 4> m_TakenDamageDetails;
	bool m_NeedFullHeal = false;
	bool m_PositionLocked = false;
};

inline const CInfClassCharacter *CInfClassCharacter::GetInstance(const CCharacter *pCharacter)
{
	return static_cast<const CInfClassCharacter *>(pCharacter);
}

inline CInfClassCharacter *CInfClassCharacter::GetInstance(CCharacter *pCharacter)
{
	return static_cast<CInfClassCharacter *>(pCharacter);
}

#endif // GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
