/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTROLLER_H
#define GAME_SERVER_GAMECONTROLLER_H

#include <base/vmath.h>
#include <base/tl/array.h>

class CConfig;
class CPlayer;

/*
	Class: Game Controller
		Controls the main game logic. Keeping track of team and player score,
		winning conditions and specific game logic.
*/
class IGameController
{
	class CGameContext *m_pGameServer;
	class IServer *m_pServer;

protected:
	CGameContext *GameServer() const { return m_pGameServer; }
	CConfig *Config() const;
	IServer *Server() const { return m_pServer; }

	void DoActivityCheck();

/* INFECTION MODIFICATION START ***************************************/
	array<vec2> m_HeroFlagPositions;
	array<vec2> m_SpawnPoints[2];
	int m_RoundId;
	
public:
	inline const array<vec2>& HeroFlagPositions() const { return m_HeroFlagPositions; }
/* INFECTION MODIFICATION START ***************************************/

protected:
	void CycleMap(bool Forced = false);
	void ResetGame();

	char m_aMapWish[128];
	char m_aQueuedMap[128];
	char m_aPreviousMap[128];


	int m_RoundStartTick;
	int m_GameOverTick;
	int m_SuddenDeath;

	int m_aTeamscore[2];

	int m_Warmup;
	int m_RoundCount;

	int m_GameFlags;
	int m_UnbalancedTick;
	bool m_ForceBalanced;

public:
	const char *m_pGameType;
	
	void SkipMap();

	bool IsTeamplay() const;
	bool IsGameOver() const { return m_GameOverTick != -1; }

	IGameController(class CGameContext *pGameServer);
	virtual ~IGameController();
	
	virtual void DoWincheck();

	void DoWarmup(int Seconds);

	virtual void StartRound();
	virtual void EndRound();
	virtual void IncreaseCurrentRoundCounter();
	void ChangeMap(const char *pToMap);
	void QueueMap(const char *pToMap);
	int GetRoundCount();
	bool IsRoundEndTime();

	bool IsForceBalanced();

	struct CMapRotationInfo
	{
		static const int MAX_MAPS = 256;
		int m_MapNameIndices[MAX_MAPS]; // saves Indices where mapNames start inside of g_Config.m_SvMaprotation
		int m_MapCount = 0; // how many maps are in rotation
		int m_CurrentMapNumber = -1; // at what place the current map is, from 0 to (m_MapCount-1)
	};
	void GetMapRotationInfo(CMapRotationInfo *pMapRotationInfo);
	static bool IsWordSeparator(char c);
	void GetWordFromList(char *pNextWord, const char *pList, int ListIndex);

	/*

	*/
	virtual bool CanBeMovedOnBalance(int ClientID);

	virtual void Tick();

	virtual void Snap(int SnappingClient);
	
	virtual bool CanVote();

	/*
		Function: on_entity
			Called when the map is loaded to process an entity
			in the map.

		Arguments:
			index - Entity index.
			pos - Where the entity is located in the world.

		Returns:
			bool?
	*/
	virtual bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv);

	/*
		Function: on_CCharacter_spawn
			Called when a CCharacter spawns into the game world.

		Arguments:
			chr - The CCharacter that was spawned.
	*/
	virtual void OnCharacterSpawn(class CCharacter *pChr);

	/*
		Function: on_CCharacter_death
			Called when a CCharacter in the world dies.

		Arguments:
			victim - The CCharacter that died.
			killer - The player that killed it.
			weapon - What weapon that killed it. Can be -1 for undefined
				weapon when switching team or player suicides.
	*/
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);

	virtual CPlayer *CreatePlayer(int ClientID) = 0;
	virtual void OnPlayerInfoChange(class CPlayer *pP);

	//
/* INFECTION MODIFICATION START ***************************************/
	virtual bool PreSpawn(CPlayer* pPlayer, vec2 *pPos) = 0;
	virtual int ChooseInfectedClass(const CPlayer* pPlayer) const = 0;
	virtual bool IsSpawnable(vec2 Position, int TeleZoneIndex) = 0;
	virtual void OnClientDrop(int ClientID, int Type) {};
	virtual void OnPlayerInfected(CPlayer* pPlayer, CPlayer* pInfectiousPlayer) = 0;
	virtual bool IsInfectionStarted() = 0;
	virtual bool PortalsAvailableForCharacter(class CCharacter *pCharacter) = 0;
	
	void MaybeSendStatistics();
	int GetRoundId() { return m_RoundId; }
/* INFECTION MODIFICATION END *****************************************/

	/*

	*/
	virtual const char *GetTeamName(int Team);
	virtual int GetAutoTeam(int NotThisID);
	virtual bool CanJoinTeam(int Team, int NotThisID);
	bool CheckTeamBalance();
	bool CanChangeTeam(CPlayer *pPplayer, int JoinTeam);
	int ClampTeam(int Team);

	virtual void PostReset();
	double GetTime();

	virtual void RegisterChatCommands(class IConsole *pConsole) = 0;
};

#endif
