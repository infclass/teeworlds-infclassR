#ifndef GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H
#define GAME_SERVER_INFCLASS_ENTITIES_CHARACTER_H

#include <base/tl/ic_array.h>
#include <game/infclass/classes.h>
#include <game/server/entities/character.h>

class CGameContext;
class CInfClassGameController;
class CInfClassPlayer;
class CInfClassPlayerClass;
class CWhiteHole;

enum class EDamageType;
enum class INFWEAPON;
enum class TAKEDAMAGEMODE;

enum FREEZEREASON
{
	FREEZEREASON_FLASH = 0,
	FREEZEREASON_UNDEAD = 1,
	FREEZEREASON_INFECTION = 2,
};

struct SDamageContext;
struct DeathContext;

struct CHelperInfo
{
	int m_Cid = -1;
	int m_Tick = 0;
};

struct CDamagePoint
{
	EDamageType DamageType;
	int From = 0;
	int Amount = 0;
	int Tick = 0;
};

struct EnforcerInfo
{
	int m_Cid;
	int m_Tick;
	EDamageType m_DamageType;
};

struct WeaponFireContext
{
	int Weapon = 0;
	bool FireAccepted = false;
	int AmmoAvailable = 0;
	int AmmoConsumed = 0;
	bool NoAmmo = false;
	float ReloadInterval = 0;
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
		Count,
		Invalid = Count
	};

	vec2 SpawnPos = vec2(0, 0);
	SPAWN_TYPE SpawnType = MapSpawn;
};

const char *toString(SpawnContext::SPAWN_TYPE SpawnType);

using ClientsArray = icArray<int, MAX_CLIENTS>;
using EntityFilter = bool (*)(const CEntity *);

class CInfClassCharacter : public CCharacter
{
	MACRO_ALLOC_POOL_ID()
public:
	CInfClassCharacter(CInfClassGameController *pGameController);
	~CInfClassCharacter() override;

	static const CInfClassCharacter *GetInstance(const CCharacter *pCharacter);
	static CInfClassCharacter *GetInstance(CCharacter *pCharacter);

	static EntityFilter GetInfectedFilterFunction();
	static EntityFilter GetHumansFilterFunction();

	void OnCharacterSpawned(const SpawnContext &Context);
	void OnCharacterInInfectionZone();
	void OnCharacterOutOfInfectionZone();
	void OnCharacterInDamageZone(float Damage, float DamageInterval = 1.0f);

	void Destroy() override;
	void TickBeforeWorld();
	void Tick() override;
	void TickDeferred() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	void SpecialSnapForClient(int SnappingClient, bool *pDoSnap);

	void HandleNinja() override;
	void HandleNinjaMove(float NinjaVelocity);
	void HandleWeaponSwitch() override;

	void FireWeapon() override;

	bool TakeDamage(const vec2 &Force, float Dmg, int From, EDamageType DamageType, float *pDamagePointsLeft = nullptr);

	bool Heal(int HitPoints, int FromCid = -1);
	bool GiveHealth(int HitPoints, int FromCid = -1);
	bool GiveArmor(int HitPoints, int FromCid = -1);

	int GetHealth() const { return m_Health; }
	int GetArmor() const { return m_Armor; }

	void SetJumpsLimit(int Limit);

	EPlayerClass GetPlayerClass() const;

	int GetDropLevel() const { return m_DropLevel; }
	void SetDropLevel(int Level);

	void OpenClassChooser();
	void HandleMapMenu();
	void HandleMapMenuClicked();
	void HandleWeaponsRegen();
	void HandleTeleports();
	void HandleIndirectKillerCleanup();

	void Die(int Killer, int Weapon) override;
	void Die(int Killer, EDamageType DamageType);
	void Die(const DeathContext &Context);

	void GiveWeapon(int Weapon, int Ammo);

	int ActiveWeapon() const { return m_ActiveWeapon; }
	void SetActiveWeapon(int Weapon);
	void SetLastWeapon(int Weapon);
	bool HasWeapon(int Weapon) const;
	void TakeAllWeapons();

	void AddAmmo(int Weapon, int Ammo);
	int GetAmmo(int Weapon) const;

	int GetCid() const;

	CInfClassPlayer *GetPlayer();

	const CInfClassPlayerClass *GetClass() const { return m_pClass; }
	CInfClassPlayerClass *GetClass() { return m_pClass; }
	void SetClass(CInfClassPlayerClass *pClass);

	CInputCount CountFireInput() const;
	bool FireJustPressed() const;

	int GetAttackTick() const { return m_AttackTick; }
	int GetLastNoAmmoSoundTick() const { return m_LastNoAmmoSound; }
	int GetActiveWeapon() const { return m_ActiveWeapon; }
	int GetReloadTimer() const { return m_ReloadTimer; }
	void SetReloadTimer(int Ticks);
	void SetReloadDuration(float Seconds);

	vec2 GetHookPos() const;
	int GetHookedPlayer() const;
	void SetHookedPlayer(int ClientId);

	vec2 Velocity() const;
	float Speed() const;

	CInfClassGameController *GameController() const { return m_pGameController; }
	CGameContext *GameContext() const;

	bool CanDie() const;

	bool CanJump() const;

	int GetInAirTick() const { return m_InAirTick; }

	bool IsInvisible() const;
	bool HasGrantedInvisibility() const;
	bool IsInvincible() const; // Invincible here means "ignores all damage"
	void SetInvincible(int Invincible);
	bool HasHallucination() const;
	void Freeze(float Time, int Player, FREEZEREASON Reason);
	bool IsFrozen() const;
	void Unfreeze();
	void TryUnfreeze(int UnfreezerCid = -1);
	FREEZEREASON GetFreezeReason() const { return m_FreezeReason; }
	int GetFreezer() const;

	bool IsBlind() const { return m_BlindnessTicks > 0; }

	void ResetBlinding();
	void MakeBlind(int ClientId, float Duration);

	float WebHookLength() const;

	void GiveRandomClassSelectionBonus();
	void MakeVisible();
	void MakeInvisible();
	void GrantInvisibility(float Duration);
	void GrantSpawnProtection(float Duration);

	bool PositionIsLocked() const;
	void LockPosition();
	void UnlockPosition();

	void CancelLoveEffect();

	bool IsInSlowMotion() const;
	float SlowMotionEffect(float Duration, int FromCid);
	void CancelSlowMotion();

	bool IsPoisoned() const;
	void Poison(int Count, int From, EDamageType DamageType, float Interval = 1.0f);
	void ResetPoisonEffect();

	void ResetMovementsInput();
	void ResetHookInput();

	int GetCursorId() const { return m_CursorId; }
	int GetFlagId() const { return m_FlagId; }
	int GetHeartId() const { return m_HeartId; }

	void AddHelper(int HelperCid, float Time);
	void ResetHelpers();

	void GetDeathContext(const SDamageContext &DamageContext, DeathContext *pContext) const;

	void UpdateLastHookers(const ClientsArray &Hookers, int HookerTick);

	void UpdateLastEnforcer(int ClientId, float Force, EDamageType DamageType, int Tick);

	void RemoveReferencesToCid(int ClientId);

	void SaturateVelocity(vec2 Force, float MaxSpeed);
	bool IsPassenger() const;
	bool HasPassenger() const;
	CInfClassCharacter *GetPassenger() const;
	CInfClassCharacter *GetTaxi() const;
	// Driver is the last Taxi in a chain
	CInfClassCharacter *GetTaxiDriver() const;
	void SetPassenger(CInfClassCharacter *pPassenger);
	void TryBecomePassenger(CInfClassCharacter *pTargetDriver);
	int GetInfZoneTick();

	bool HasSuperWeaponIndicator() const;
	void SetSuperWeaponIndicatorEnabled(bool Enabled);

	INFWEAPON GetInfWeaponId(int WID = -1) const;

	using CCharacter::GameWorld;
	using CCharacter::Server;

	CGameWorld *GameWorld() const;
	const IServer *Server() const;
protected:
	void PreCoreTick() override;
	void PostCoreTick() override;

	void SnapCharacter(int SnappingClient, int Id) override;

	void ClassSpawnAttributes();
	void DestroyChildEntities();

	void FreeChildSnapIds();

	void UpdateTuningParam();
	void TeleToId(int TeleNumber, int TeleType);

	void ResetClassObject();
	void HandleDamage(int From, int Damage, EDamageType DamageType);

	void OnTotalHealthChanged(int Difference) override;
	void PrepareToDie(const DeathContext &Context, bool *pRefusedToDie);

protected:
	CInfClassGameController *m_pGameController = nullptr;
	CInfClassPlayerClass *m_pClass = nullptr;

	CNetObj_PlayerInput m_InputBackup;

	int m_FlagId;
	int m_HeartId;
	int m_CursorId;
	int m_DropLevel = 0;

	CHelperInfo m_LastHelper;
	ClientsArray m_LastHookers;
	int m_LastHookerTick = -1;

	int m_BlindnessTicks = 0;
	int m_LastBlinder = -1;

	int m_ProtectionTick = 0;

	int m_InfZoneTick = 0;
	int m_DamageZoneTick;
	float m_DamageZoneDealtDamage = 0;

	icArray<EnforcerInfo, 4> m_EnforcersInfo;

	int m_DamageTaken = 0;
	icArray<CDamagePoint, 4> m_TakenDamageDetails;
	bool m_PositionLocked = false;

	bool m_IsFrozen = false;
	int m_FrozenTime;
	FREEZEREASON m_FreezeReason;

	int m_SlowMotionTick;
	int m_SlowEffectApplicant;

	int m_Poison = 0;
	float m_PoisonEffectInterval{};
	int m_PoisonTick = 0;
	int m_PoisonFrom = 0;
	EDamageType m_PoisonDamageType;

	bool m_IsInvisible = false;
	int m_GrantedInvisibilityUntilTick = 0;
	int m_Invincible = 0;

	int m_HealTick = 0;
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
