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

enum class DAMAGE_TYPE;

struct SDamageContext;
struct DeathContext;

struct CHelperInfo
{
	int m_CID = -1;
	int m_Tick = 0;
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
	};

	vec2 SpawnPos = vec2(0, 0);
	SPAWN_TYPE SpawnType = MapSpawn;
};

constexpr int GHOST_RADIUS = 11;
constexpr int GHOST_SEARCHMAP_SIZE = (2 * GHOST_RADIUS + 1);

using ClientsArray = icArray<int, MAX_CLIENTS>;

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
	void OnCharacterInDamageZone(float Damage);

	void Destroy() override;
	void Tick() override;
	void TickDeferred() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	void SpecialSnapForClient(int SnappingClient, bool *pDoSnap);

	void HandleNinja() override;
	void HandleWeaponSwitch() override;

	void FireWeapon() override;

	bool TakeDamage(const vec2 &Force, float Dmg, int From, DAMAGE_TYPE DamageType);
	bool TakeDamage(SDamageContext DamageContext);
	bool TakeDamage(vec2 Force, int Dmg, int From, int Weapon, TAKEDAMAGEMODE Mode) override;

	bool Heal(int HitPoints, int FromCID = -1);
	bool GiveHealth(int HitPoints, int FromCID = -1);
	bool GiveArmor(int HitPoints, int FromCID = -1);

	int GetHealth() const { return m_Health; }
	int GetArmor() const { return m_Armor; }

	PLAYERCLASS GetPlayerClass() const;

	void OpenClassChooser();
	void HandleMapMenu();
	void HandleMapMenuClicked();
	void HandleWeaponsRegen();
	void HandleTeleports();
	void HandleIndirectKillerCleanup();

	void Die(int Killer, int Weapon) override;
	void Die(int Killer, DAMAGE_TYPE DamageType);
	void Die(const DeathContext &Context);

	void GiveWeapon(int Weapon, int Ammo);
	void SetActiveWeapon(int Weapon);
	void SetLastWeapon(int Weapon);
	bool HasWeapon(int Weapon) const;
	void TakeAllWeapons();

	void AddAmmo(int Weapon, int Ammo);
	int GetAmmo(int Weapon) const;

	int GetCID() const;

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
	void SetHookedPlayer(int ClientID);

	vec2 Velocity() const;
	float Speed() const;

	CInfClassGameController *GameController() const { return m_pGameController; }
	CGameContext *GameContext() const;

	bool CanDie() const;

	bool CanJump() const;

	int GetInAirTick() const { return m_InAirTick; }

	bool IsInvisible() const;
	bool IsInvincible() const; // Invincible here means "ignores all damage"
	void SetInvincible(int Invincible);
	bool HasHallucination() const;
	void TryUnfreeze(int UnfreezerCID = -1);
	FREEZEREASON GetFreezeReason() const { return m_FreezeReason; }
	int GetFreezer() const;

	bool IsBlind() const { return m_BlindnessTicks > 0; }
	void MakeBlind(int ClientID, float Duration);

	float WebHookLength() const;

	void GiveRandomClassSelectionBonus();
	void MakeVisible();
	void GrantSpawnProtection();

	bool PositionIsLocked() const;
	void LockPosition();
	void UnlockPosition();

	bool IsInSlowMotion() const;
	float SlowMotionEffect(float Duration, int FromCID);
	void CancelSlowMotion();

	void ResetMovementsInput();
	void ResetHookInput();

	int GetCursorID() const { return m_CursorID; }

	void AddHelper(int HelperCID, float Time);
	void ResetHelpers();

	void GetDeathContext(const SDamageContext &DamageContext, DeathContext *pContext) const;

	void UpdateLastHookers(const ClientsArray &Hookers, int HookerTick);

	void UpdateLastEnforcer(int ClientID, float Force, DAMAGE_TYPE DamageType, int Tick);

	void RemoveReferencesToCID(int ClientID);

	void SaturateVelocity(vec2 Force, float MaxSpeed);
	bool IsPassenger() const;
	bool HasPassenger() const;
	CInfClassCharacter *GetPassenger();
	CInfClassCharacter *GetTaxiDriver();
	void SetPassenger(CCharacter *pPassenger);
	int GetInfZoneTick();

	bool HasSuperWeaponIndicator() const;
	void SetSuperWeaponIndicatorEnabled(bool Enabled);

	using CCharacter::GameWorld;
	using CCharacter::Server;

	CGameWorld *GameWorld() const;
	const IServer *Server() const;
protected:
	void PreCoreTick() override;
	void PostCoreTick() override;

	void SnapCharacter(int SnappingClient, int ID) override;

	void ClassSpawnAttributes();
	void DestroyChildEntities();
	void UpdateTuningParam();
	void TeleToId(int TeleNumber, int TeleType);

	void ResetClassObject();
	void HandleDamage(int From, int Damage, DAMAGE_TYPE DamageType);

	void OnTotalHealthChanged(int Difference) override;
	void PrepareToDie(const DeathContext &Context, bool *pRefusedToDie);

protected:
	CInfClassGameController *m_pGameController = nullptr;
	CInfClassPlayerClass *m_pClass = nullptr;

	CHelperInfo m_LastHelper;
	ClientsArray m_LastHookers;
	int m_LastHookerTick = -1;

	int m_BlindnessTicks = 0;
	int m_LastBlinder = -1;

	int m_DamageZoneTick;
	float m_DamageZoneDealtDamage = 0;

	icArray<EnforcerInfo, 4> m_EnforcersInfo;

	int m_DamageTaken = 0;
	icArray<CDamagePoint, 4> m_TakenDamageDetails;
	bool m_PositionLocked = false;

	int m_SlowMotionTick;
	int m_SlowEffectApplicant;

	bool m_IsInvisible = false;
	int m_InvisibleTick = 0;
	int m_Invincible = 0;

	char m_GhostSearchMap[GHOST_SEARCHMAP_SIZE * GHOST_SEARCHMAP_SIZE];

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
