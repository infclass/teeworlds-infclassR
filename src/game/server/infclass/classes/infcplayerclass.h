#ifndef GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H
#define GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H

#include <base/vmath.h>
#include <game/server/entity.h>

class CConfig;
class CGameContext;
class CGameWorld;
class CInfClassCharacter;
class CInfClassGameContext;
class CInfClassGameController;
class CInfClassPlayer;
class CTeeInfo;
class IServer;

struct WeaponRegenParams;
struct WeaponFireContext;

class CInfClassPlayerClass
{
public:
	CInfClassPlayerClass(CInfClassPlayer *pPlayer);
	virtual ~CInfClassPlayerClass() = default;

	void SetCharacter(CInfClassCharacter *character);

	virtual bool IsHuman() const = 0;
	bool IsZombie() const;

	virtual int GetDefaultEmote() const;
	virtual void GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams);

	virtual bool CanDie() const;
	virtual float GetGhoulPercent() const;

	// Temp stuff
	void UpdateSkin();
	int GetPlayerClass() const;
	void OnPlayerClassChanged();

	virtual void PrepareToDie(int Killer, int Weapon, bool *pRefusedToDie);
	void Poison(int Count, int From);

	// Events
	virtual void OnCharacterPreCoreTick();
	virtual void OnCharacterTick();
	virtual void OnCharacterSnap(int SnappingClient);
	virtual void OnCharacterSpawned();
	virtual void OnCharacterDeath(int Weapon);

	virtual void OnWeaponFired(WeaponFireContext *pFireContext);

	virtual void OnHammerFired(WeaponFireContext *pFireContext);
	virtual void OnGunFired(WeaponFireContext *pFireContext);
	virtual void OnShotgunFired(WeaponFireContext *pFireContext);
	virtual void OnGrenadeFired(WeaponFireContext *pFireContext);
	virtual void OnLaserFired(WeaponFireContext *pFireContext);
	virtual void OnNinjaFired(WeaponFireContext *pFireContext);

	virtual void OnSlimeEffect(int Owner) = 0;
	virtual void OnFloatingPointCollected(int Points);

	CGameContext *GameContext() const;
	CGameContext *GameServer() const;
	CGameWorld *GameWorld() const;
	CInfClassGameController *GameController() const;
	CConfig *Config();
	const CConfig *Config() const;
	IServer *Server();
	CInfClassPlayer *GetPlayer();
	const CInfClassPlayer *GetPlayer() const;
	int GetCID();
	vec2 GetPos() const;
	vec2 GetDirection() const;
	float GetProximityRadius() const;

protected:
	virtual void GiveClassAttributes();
	virtual void DestroyChildEntities();
	virtual void SetupSkin(CTeeInfo *output);
	virtual void BroadcastWeaponState();

	CInfClassPlayer *m_pPlayer = nullptr;
	CInfClassCharacter *m_pCharacter = nullptr;

	int m_Poison = 0;
	int m_PoisonTick = 0;
	int m_PoisonFrom = 0;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H
