#ifndef GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H
#define GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H

#include <base/vmath.h>
#include <game/server/entity.h>
#include <game/server/skininfo.h>

class CConfig;
class CGameContext;
class CGameWorld;
class CInfClassCharacter;
class CInfClassGameContext;
class CInfClassGameController;
class CInfClassPlayer;
class IServer;

struct SpawnContext;
struct SDamageContext;
struct DeathContext;
struct WeaponFireContext;
struct WeaponRegenParams;

enum class EDamageType;

struct SClassUpgrade
{
	SClassUpgrade() = default;
	explicit SClassUpgrade(int T) :
		Type(T)
	{
	}

	explicit SClassUpgrade(int T, int S) :
		Type(T)
	{
		Subtype = S;
	}

	int Type = 0;
	int Subtype = 0;

	bool IsValid() const { return Type >= 0; }

	static SClassUpgrade Invalid()
	{
		return SClassUpgrade(-1);
	}
};

class CInfClassPlayerClass
{
public:
	CInfClassPlayerClass(CInfClassPlayer *pPlayer);
	virtual ~CInfClassPlayerClass() = default;

	void SetCharacter(CInfClassCharacter *character);

	virtual bool IsHuman() const = 0;
	bool IsZombie() const;

	virtual SkinGetter GetSkinGetter() const = 0;
	virtual void SetupSkinContext(CSkinContext *pOutput, bool ForSameTeam) const = 0;

	virtual void ResetNormalEmote();
	void SetNormalEmote(int Emote);
	virtual int GetDefaultEmote() const;
	virtual void GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams);
	virtual int GetJumps() const;

	virtual bool CanDie() const;
	virtual bool CanBeHit() const;
	virtual bool CanBeUnfreezed() const;
	virtual SClassUpgrade GetNextUpgrade() const;

	float GetHammerProjOffset() const;
	virtual float GetHammerRange() const;
	virtual float GetGhoulPercent() const;

	// Temp stuff
	void UpdateSkin();
	EPlayerClass GetPlayerClass() const;
	virtual void OnPlayerClassChanged();

	virtual void PrepareToDie(const DeathContext &Context, bool *pRefusedToDie);

	bool IsHealingDisabled() const;
	void DisableHealing(float Duration, int From, EDamageType DamageType);

	// Events
	virtual void OnPlayerSnap(int SnappingClient, int InfClassVersion);

	virtual void OnCharacterPreCoreTick();
	virtual void OnCharacterTick();
	virtual void OnCharacterTickPaused();
	virtual void OnCharacterPostCoreTick();
	virtual void OnCharacterTickDeferred();
	virtual void OnCharacterSnap(int SnappingClient);
	virtual void OnCharacterSpawned(const SpawnContext &Context);
	virtual void OnCharacterDeath(EDamageType DamageType);
	virtual void OnCharacterDamage(SDamageContext *pContext);

	virtual void OnKilledCharacter(CInfClassCharacter *pVictim, const DeathContext &Context);

	virtual void OnHookAttachedPlayer();

	virtual void HandleNinja() {};

	virtual void OnWeaponFired(WeaponFireContext *pFireContext);

	virtual void OnHammerFired(WeaponFireContext *pFireContext);
	virtual void OnGunFired(WeaponFireContext *pFireContext);
	virtual void OnShotgunFired(WeaponFireContext *pFireContext);
	virtual void OnGrenadeFired(WeaponFireContext *pFireContext);
	virtual void OnLaserFired(WeaponFireContext *pFireContext);
	virtual void OnNinjaFired(WeaponFireContext *pFireContext);

	virtual void OnSlimeEffect(int Owner, int Damage, float DamageInterval) = 0;
	virtual void OnFloatingPointCollected(int Points);

	virtual void GiveUpgrade() {}

	CGameContext *GameContext() const;
	CGameContext *GameServer() const;
	CGameWorld *GameWorld() const;
	CInfClassGameController *GameController() const;
	CConfig *Config();
	const CConfig *Config() const;
	IServer *Server() const;
	CInfClassPlayer *GetPlayer();
	const CInfClassPlayer *GetPlayer() const;
	int GetCid() const;
	vec2 GetPos() const;
	vec2 GetDirection() const;
	vec2 GetProjectileStartPos(float Offset) const;
	float GetProximityRadius() const;

	int GetUpgradeLevel() const;
	void ResetUpgradeLevel();

protected:
	virtual void GiveClassAttributes();
	virtual void DestroyChildEntities();
	virtual void BroadcastWeaponState() const;

	void CreateHammerHit(const vec2 &ProjStartPos, const CInfClassCharacter *pTarget);

	CInfClassPlayer *m_pPlayer = nullptr;
	CInfClassCharacter *m_pCharacter = nullptr;
	int m_NormalEmote;

	int m_UpgradeLevel = 0;
	int m_HealingDisabledTicks = 0;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H
