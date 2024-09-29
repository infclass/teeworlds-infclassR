/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTROLLER_H
#define GAME_SERVER_GAMECONTROLLER_H

#include <base/vmath.h>
#include <base/tl/array.h>

#include <engine/map.h>

class CConfig;
class CPlayer;
class CMapInfo;
class IConsole;

enum class EClientDropType;

/*
	Class: Game Controller
		Controls the main game logic. Keeping track of team and player score,
		winning conditions and specific game logic.
*/
class IGameController
{
	class CGameContext *m_pGameServer;
	class CConfig *m_pConfig;
	class IServer *m_pServer;

protected:
	CGameContext *GameServer() const { return m_pGameServer; }
	CConfig *Config() const { return m_pConfig; }
	IServer *Server() const { return m_pServer; }
	IConsole *Console();

	void DoActivityCheck();

/* INFECTION MODIFICATION START ***************************************/
	array<vec2> m_SpawnPoints[2];
	int m_RoundId;
	
public:
/* INFECTION MODIFICATION START ***************************************/

	void OnMapAdded(const char *pMapName);
	void InitSmartMapRotation();
	void SyncSmartMapRotationData();
	void ConSmartMapRotationStatus();
	void LoadMapRotationData();
	void SaveMapRotationData(const char *pFileName);
	void PrintMapRotationData(IOHANDLE Output = 0);
	virtual bool MapRotationEnabled() const = 0;

	static void ResetMapInfo(const char *pMapName);
	static void AddMapTimestamp(const char *pMapName, int Timestamp);
	static bool SetMapMinMaxPlayers(const char *pMapName, int MinPlayers, int MaxPlayers);

	virtual int PersistentClientDataSize() const = 0;
	virtual bool GetClientPersistentData(int ClientId, void *pData) const = 0;

protected:
	bool LoadMapConfig(const char *pMapName, CMapInfo *pInfo);

	void CycleMap(bool Forced = false);
	void DefaultMapCycle();
	void SmartMapCycle();
	void ResetGame();
	void RotateMapTo(const char *pMapName);

	int GetNextClientUniqueId();

	char m_aMapWish[MAX_MAP_LENGTH];
	char m_aQueuedMap[MAX_MAP_LENGTH];
	char m_aPreviousMap[MAX_MAP_LENGTH];

	int m_RoundStartTick;
	int m_GameOverTick;
	int m_SuddenDeath;

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

	// event
	/*
		Function: OnCharacterDeath
			Called when a CCharacter in the world dies.

		Arguments:
			victim - The CCharacter that died.
			killer - The player that killed it.
			weapon - What weapon that killed it. Can be -1 for undefined
				weapon when switching team or player suicides.
	*/
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	/*
		Function: OnCharacterSpawn
			Called when a CCharacter spawns into the game world.

		Arguments:
			chr - The CCharacter that was spawned.
	*/
	virtual void OnCharacterSpawn(class CCharacter *pChr);

	/*
		Function: OnEntity
			Called when the map is loaded to process an entity
			in the map.

		Arguments:
			index - Entity index.
			pos - Where the entity is located in the world.

		Returns:
			bool?
	*/
	virtual bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv);

	virtual void OnPlayerConnect(class CPlayer *pPlayer);
	virtual void OnPlayerDisconnect(CPlayer *pPlayer, EClientDropType Type, const char *pReason);

	virtual void OnReset();

	// game
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
	virtual bool CanBeMovedOnBalance(int ClientId);

	virtual void DoTeamBalance();

	virtual void TickBeforeWorld();
	virtual void Tick();
	virtual void OnGameRestart();

	virtual void Snap(int SnappingClient);
	
	virtual bool CanVote();
	virtual void OnPlayerVoteCommand(int ClientId, int Vote) = 0;

	void OnStartRound();

	virtual CPlayer *CreatePlayer(int ClientId, bool IsSpectator, void *pData) = 0;

	//
/* INFECTION MODIFICATION START ***************************************/
	int GetRoundId() { return m_RoundId; }
	/* INFECTION MODIFICATION END *****************************************/

	virtual void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true);
	/*

	*/
	virtual int GetPlayerTeam(int ClientId) const = 0;
	virtual const char *GetTeamName(int Team);
	virtual int GetAutoTeam(int NotThisId);
	virtual void OnTeamChangeRequested(int ClientId, int Team) = 0;
	virtual bool CanJoinTeam(int Team, int NotThisId);
	bool CanChangeTeam(CPlayer *pPplayer, int JoinTeam);
	int ClampTeam(int Team);

	double GetTime();

	virtual void RegisterChatCommands(class IConsole *pConsole) = 0;

private:
	// starting 1 to make 0 the special value "no client id"
	uint32_t m_NextUniqueClientId = 1;
};

#endif
