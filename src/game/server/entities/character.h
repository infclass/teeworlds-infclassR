/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#ifndef GAME_SERVER_ENTITIES_CHARACTER_H
#define GAME_SERVER_ENTITIES_CHARACTER_H

#include <base/tl/array.h>
#include <game/generated/protocol.h>
#include <game/generated/server_data.h>
#include <game/server/entity.h>

#include <game/gamecore.h>

class CGameTeams;

enum class INFWEAPON;

/* INFECTION MODIFICATION START ***************************************/
enum FREEZEREASON
{
	FREEZEREASON_FLASH = 0,
	FREEZEREASON_UNDEAD = 1,
	FREEZEREASON_INFECTION = 2,
};

enum class TAKEDAMAGEMODE
{
	NOINFECTION=0,
	INFECTION,
	SELFHARM, // works like NOINFECTION but also harms the owner of the damage with 50%
};

/* INFECTION MODIFICATION END *****************************************/

enum
{
	FAKETUNE_FREEZE = 1,
	FAKETUNE_SOLO = 2,
	FAKETUNE_NOJUMP = 4,
	FAKETUNE_NOCOLL = 8,
	FAKETUNE_NOHOOK = 16,
	FAKETUNE_JETPACK = 32,
	FAKETUNE_NOHAMMER = 64,
};

class CCharacter : public CEntity
{
	MACRO_ALLOC_POOL_ID()

public:
	static int EntityId;

	CCharacter(CGameWorld *pWorld);
	~CCharacter() override;

	virtual void Reset();
	virtual void Destroy();
	void PreTick();
	virtual void Tick();
	void TickDeferred() override;
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

	int NetworkClipped(int SnappingClient) const;

	bool IsGrounded() const;

	void SetWeapon(int W);
	virtual void HandleWeaponSwitch();
	void DoWeaponSwitch();

	void HandleWeapons();
	virtual void HandleNinja() = 0;
	void HandleWaterJump();

	void OnPredictedInput(CNetObj_PlayerInput *pNewInput);
	void OnDirectInput(CNetObj_PlayerInput *pNewInput);
	void ResetInput();
	virtual void FireWeapon();

	virtual void Die(int Killer, int Weapon);
	virtual bool TakeDamage(vec2 Force, int Dmg, int From, int Weapon, TAKEDAMAGEMODE Mode);

	bool Spawn(class CPlayer *pPlayer, vec2 Pos);
	bool Remove();

	bool IncreaseHealth(int Amount);
	bool IncreaseArmor(int Amount);
	bool IncreaseOverallHp(int Amount);
	void SetHealthArmor(int HealthAmount, int ArmorAmount);
	int GetHealthArmorSum();

	void SetMaxArmor(int Amount);

	bool GiveWeapon(int Weapon, int Ammo);
	void SetActiveWeapon(int Weapon);

	void SetEmote(int Emote, int Tick);
	void NoAmmo();

	int NeededFaketuning() { return m_NeededFaketuning; }
	bool IsAlive() const { return m_Alive; }
	class CPlayer *GetPlayer() { return m_pPlayer; }

protected:
	// player controlling this character
	class CPlayer *m_pPlayer;

	bool m_Alive;
	int m_NeededFaketuning;

	// weapon info
	CEntity *m_apHitObjects[10];
	int m_NumObjectsHit;

	struct WeaponStat
	{
		int m_AmmoRegenStart;
		int m_Ammo;
		int m_Ammocost;
		bool m_Got;

	} m_aWeapons[NUM_WEAPONS];

	int m_ActiveWeapon;
	int m_LastWeapon;
	int m_QueuedWeapon;

	int m_ReloadTimer;
	int m_AttackTick;

	int m_EmoteType;
	int m_EmoteStop;

	// last tick that the player took any action ie some input
	int m_LastAction;
	int m_LastNoAmmoSound;

public:
	// these are non-heldback inputs
	CNetObj_PlayerInput m_LatestPrevInput;
	CNetObj_PlayerInput m_LatestInput;

	// input
	CNetObj_PlayerInput m_PrevInput;
	CNetObj_PlayerInput m_Input;

protected:
	int m_NumInputs;
	int m_Jumped;

	int m_DamageTakenTick;

	int m_Health;
	int m_Armor;

/* INFECTION MODIFICATION START ***************************************/
	int m_MaxArmor;
	//Dart
public:
	int m_DartLifeSpan;
	vec2 m_DartDir;
	int m_DartLeft;
	int m_DartOldVelAmount;
	
	int m_WaterJumpLifeSpan;
	/* INFECTION MODIFICATION END *****************************************/

	// the player core for the physics
	CCharacterCore m_Core;
	CGameTeams *m_pTeams = nullptr;

	void DDRaceInit();
	void HandleSkippableTiles(int Index);

public:
	CGameTeams *Teams() { return m_pTeams; }
	void SetTeams(CGameTeams *pTeams);

	int Team();
	bool CanCollide(int ClientID);

protected:
	int PrivateGetPlayerClass() const;

	// info for dead reckoning
	int m_ReckoningTick; // tick that we are performing dead reckoning From
	CCharacterCore m_SendCore; // core that we should send
	CCharacterCore m_ReckoningCore; // the dead reckoning core

	// DDRace

	virtual void SnapCharacter(int SnappingClient, int ID);

	/* INFECTION MODIFICATION START ***************************************/
protected:
	virtual void OnTotalHealthChanged(int Difference) = 0;

	int m_HookDmgTick;
	int m_InfZoneTick;
	int m_ProtectionTick;
	
	int m_FlagID;
	int m_HeartID;
	int m_CursorID;
	int m_AntiFireTime;
	int m_PainSoundTimer;
	
	bool m_IsFrozen;
	int m_FrozenTime;
	bool m_IsInSlowMotion; //LooperClass changes here
	FREEZEREASON m_FreezeReason;
	int m_InAirTick;
	vec2 m_SpawnPosition;

public:
	bool m_HasIndicator;
	int m_LoveTick;
	int m_HallucinationTick;
	int m_SlipperyTick;
	int m_SlowMotionTick; //LooperClass changes here
	int m_SlowEffectApplicant;
	int m_LastFreezer;
	int m_HookMode;
	int m_InWater;
	int m_NinjaVelocityBuff;
	int m_NinjaStrengthBuff;
	int m_NinjaAmmoBuff;
	int m_RefreshTime;
	// Mercenary

public:
	void FreeChildSnapIDs();
	vec2 GetDirection() const;

	bool IsZombie() const;
	bool IsHuman() const;
	void SetAntiFire();
	void Freeze(float Time, int Player, FREEZEREASON Reason);
	bool IsFrozen() const;
	bool IsInSlowMotion() const; //LooperClass changes here
	float SlowMotionEffect(float Duration, int FromCID);
	void Unfreeze();
	bool IsInLove() const;
	void LoveEffect(float Time);
	void HallucinationEffect();
	void SlipperyEffect();
	INFWEAPON GetInfWeaponID(int WID) const;
	/* INFECTION MODIFICATION END *****************************************/

	int GetEffectiveHookMode() const;

	int m_MoveRestrictions;

	CCharacterCore *Core() { return &m_Core; }

	virtual void PreCoreTick() { }
	virtual void PostCoreTick();
};

#endif
