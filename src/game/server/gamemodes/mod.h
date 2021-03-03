/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_MOD_H
#define GAME_SERVER_GAMEMODES_MOD_H
#include <engine/console.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>
#include <game/server/classes.h>

// you can subclass GAMECONTROLLER_CTF, GAMECONTROLLER_TDM etc if you want
// todo a modification with their base as well.
class CGameControllerMOD : public IGameController
{
public:
	CGameControllerMOD(class CGameContext *pGameServer);
	virtual ~CGameControllerMOD();
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	// add more virtual functions here if you wish
	
	virtual bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv);
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual void OnCharacterSpawn(class CCharacter *pChr);
	virtual void OnPlayerInfoChange(class CPlayer *pP);
	virtual void DoWincheck();
	virtual void EndRound();
	virtual bool PreSpawn(CPlayer* pPlayer, vec2 *pPos);
	virtual bool PickupAllowed(int Index);
	virtual int ChooseHumanClass(const CPlayer *pPlayer) const;
	virtual int ChooseInfectedClass(const CPlayer *pPlayer) const;
	virtual bool IsEnabledClass(int PlayerClass);
	virtual bool IsChoosableClass(int PlayerClass);
	virtual bool CanVote();
	virtual void OnClientDrop(int ClientID, int Type);
	virtual void OnPlayerInfected(CPlayer* pPlayer, CPlayer* pInfectiousPlayer);
	virtual bool IsInfectionStarted();
	bool PortalsAvailableForCharacter(class CCharacter *pCharacter) override;
	
	void ResetFinalExplosion();
	
	static bool IsDefenderClass(int PlayerClass);
	static bool IsSupportClass(int PlayerClass);
	static int GetClassByName(const char *pClassName, bool *pOk = nullptr);
	static const char *GetClassName(int PlayerClass);
	static const char *GetClassPluralName(int PlayerClass);
	static const char *GetClassDisplayName(int PlayerClass);
	static const char *GetClassPluralDisplayName(int PlayerClass);

	void RegisterChatCommands(class IConsole *pConsole) override;

	static bool ConSetClass(IConsole::IResult *pResult, void *pUserData);
	bool ConSetClass(IConsole::IResult *pResult);

	static bool ChatWitch(IConsole::IResult *pResult, void *pUserData);
	bool ChatWitch(IConsole::IResult *pResult);

private:
	CGameWorld *GameWorld();
	
	void RewardTheKiller(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	bool IsSpawnable(vec2 Pos, int TeleZoneIndex);
	void GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected, int& NumFirstInfected);

	int RandomZombieToWitch();
	std::vector<int> m_WitchCallers;
	
private:	
	int m_MapWidth;
	int m_MapHeight;
	int* m_GrowingMap;
	bool m_ExplosionStarted;
	
	bool m_InfectedStarted;
};
#endif
