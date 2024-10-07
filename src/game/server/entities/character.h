/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#ifndef GAME_SERVER_ENTITIES_CHARACTER_H
#define GAME_SERVER_ENTITIES_CHARACTER_H

#include <game/server/entity.h>

#include <game/gamecore.h>

class CGameTeams;
enum class EPlayerClass;

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

	void Reset() override;
	void Destroy() override;
	void PreTick();
	void Tick() override;
	void TickDeferred() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

	bool CanSnapCharacter(int SnappingClient);
	bool IsSnappingCharacterInView(int SnappingClientId);

	bool IsGrounded() const;

	void SetWeapon(int W);
	virtual void HandleWeaponSwitch();
	void DoWeaponSwitch();

	void HandleWeapons();
	virtual void HandleNinja() {};
	void HandleWaterJump();

	void OnPredictedInput(const CNetObj_PlayerInput *pNewInput);
	void OnDirectInput(const CNetObj_PlayerInput *pNewInput);
	void ReleaseHook();
	void ResetHook();
	void ResetInput();
	virtual void FireWeapon();

	virtual void Die(int Killer, int Weapon);

	bool Spawn(class CPlayer *pPlayer, vec2 Pos);
	bool Remove();

	bool IncreaseHealth(int Amount);
	bool IncreaseArmor(int Amount);
	bool IncreaseOverallHp(int Amount);
	void SetHealthArmor(int HealthAmount, int ArmorAmount);
	int GetHealthArmorSum();

	void SetMaxArmor(int Amount);

	void SetActiveWeapon(int Weapon);

	void SetEmote(int Emote, int Tick);
	void NoAmmo();

	int NeededFaketuning() { return m_NeededFaketuning; }
	bool IsAlive() const { return m_Alive; }
	class CPlayer *GetPlayer() { return m_pPlayer; }

	void SetPosition(const vec2 &Position);
	void Move(vec2 RelPos);

	void ResetVelocity();
	void SetVelocity(vec2 NewVelocity);
	void AddVelocity(vec2 Addition);

protected:
	// player controlling this character
	class CPlayer *m_pPlayer;

	bool m_Alive;
	int m_NeededFaketuning;

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

	int m_MoveRestrictions;

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

protected:
	// the player core for the physics
	CCharacterCore m_Core;
	CGameTeams *m_pTeams = nullptr;

	void DDRaceInit();
	void HandleSkippableTiles(int Index);

public:
	CGameTeams *Teams() { return m_pTeams; }
	void SetTeams(CGameTeams *pTeams);

	int Team();
	bool CanCollide(int ClientId);
	bool SameTeam(int ClientId);

protected:
	EPlayerClass PrivateGetPlayerClass() const;

	// info for dead reckoning
	int m_ReckoningTick; // tick that we are performing dead reckoning From
	CCharacterCore m_SendCore; // core that we should send
	CCharacterCore m_ReckoningCore; // the dead reckoning core

	// DDRace

	virtual void SnapCharacter(int SnappingClient, int Id);

	/* INFECTION MODIFICATION START ***************************************/
protected:
	virtual void OnTotalHealthChanged(int Difference) {};

	int m_AntiFireTime;
	int m_PainSoundTimer;
	vec2 m_PrevPos;

	int m_InAirTick;

public:
	bool m_HasIndicator;
	int m_LoveTick;
	int m_HallucinationTick;
	int m_SlipperyTick;
	int m_LastFreezer;
	int m_HookMode;
	int m_InWater;

public:
	vec2 GetDirection() const;

	bool IsInfected() const;
	bool IsHuman() const;
	void SetAntiFire();
	bool IsInLove() const;
	void LoveEffect(float Time);
	void HallucinationEffect();
	void SlipperyEffect();
	/* INFECTION MODIFICATION END *****************************************/

	int GetEffectiveHookMode() const;

	const CCharacterCore *Core() const { return &m_Core; }

	virtual void PreCoreTick() { }
	virtual void PostCoreTick();
};

#endif
