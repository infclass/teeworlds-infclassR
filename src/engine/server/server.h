/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_SERVER_H
#define ENGINE_SERVER_SERVER_H

#include <base/hash.h>
#include <base/math.h>

#include <engine/engine.h>
#include <engine/server.h>

#include <engine/map.h>
#include <engine/server/netsession.h>
#include <engine/server/register.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/snapshot.h>
#include <game/voting.h>

/* DDNET MODIFICATION START *******************************************/
#include "base/logger.h"
#include "sql_connector.h"
#include "sql_server.h"
/* DDNET MODIFICATION END *********************************************/

#include "name_ban.h"

class CLogMessage;

class CSnapIDPool
{
	enum
	{
		MAX_IDS = 32 * 1024,
	};

	// State of a Snap ID
	enum
	{
		ID_FREE = 0,
		ID_ALLOCATED = 1,
		ID_TIMED = 2,
	};

	class CID
	{
	public:
		short m_Next;
		short m_State; // 0 = free, 1 = allocated, 2 = timed
		int m_Timeout;
	};

	CID m_aIDs[MAX_IDS];

	int m_FirstFree;
	int m_FirstTimed;
	int m_LastTimed;
	int m_Usage;
	int m_InUsage;

public:
	CSnapIDPool();

	void Reset();
	void RemoveFirstTimeout();
	int NewID();
	void TimeoutIDs();
	void FreeID(int ID);
};

class CServerBan : public CNetBan
{
	class CServer *m_pServer;

	template<class T>
	int BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason);

public:
	class CServer *Server() const { return m_pServer; }

	void InitServerBan(class IConsole *pConsole, class IStorage *pStorage, class CServer *pServer);

	int BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason) override;
	int BanRange(const CNetRange *pRange, int Seconds, const char *pReason) override;

	static void ConBanExt(class IConsole::IResult *pResult, void *pUser);
	static void ConBanRegion(class IConsole::IResult *pResult, void *pUser);
	static void ConBanRegionRange(class IConsole::IResult *pResult, void *pUser);
};

class CServer : public IServer
{
	friend class CServerLogger;

	class IGameServer *m_pGameServer;
	class CConfig *m_pConfig;
	class IConsole *m_pConsole;
	class IStorage *m_pStorage;

/* DDNET MODIFICATION START *******************************************/
#ifdef CONF_SQL
	CSqlServer* m_apSqlReadServers[MAX_SQLSERVERS];
	CSqlServer* m_apSqlWriteServers[MAX_SQLSERVERS];
#endif
/* DDNET MODIFICATION END *********************************************/
public:
	class IGameServer *GameServer() { return m_pGameServer; }
	class CConfig *Config() { return m_pConfig; }
	const CConfig *Config() const { return m_pConfig; }
	class IConsole *Console() { return m_pConsole; }
	class IStorage *Storage() { return m_pStorage; }

	enum
	{
		MAX_RCONCMD_SEND=16,
	};

	class CClient
	{
	public:

		enum
		{
			STATE_EMPTY = 0,
			STATE_PREAUTH,
			STATE_AUTH,
			STATE_CONNECTING,
			STATE_READY,
			STATE_INGAME,

			SNAPRATE_INIT=0,
			SNAPRATE_FULL,
			SNAPRATE_RECOVER
		};

		class CInput
		{
		public:
			int m_aData[MAX_INPUT_SIZE];
			int m_GameTick; // the tick that was chosen for the input
		};

		// connection state info
		int m_State;
		int m_Latency;
		int m_SnapRate;
		bool m_Quitting;
		bool m_IsBot;

		int m_LastAckedSnapshot;
		int m_LastInputTick;
		CSnapshotStorage m_Snapshots;

		CInput m_LatestInput;
		CInput m_aInputs[200]; // TODO: handle input better
		int m_CurrentInput;

		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		int m_Country;
		int m_Authed;
		int m_AuthTries;
		int m_NextMapChunk;
		bool m_ShowIps;

		const IConsole::CCommandInfo *m_pRconCmdToSend;

		bool m_HasPersistentData;
		void *m_pPersistentData;

		void Reset(bool ResetScore=true);
		
		int m_NbRound;
		
		int m_DefaultScoreMode;
		char m_aLanguage[16];
		int m_WaitingTime;

		bool m_Memory[NUM_CLIENTMEMORIES];
		IServer::CClientSession m_Session;
		IServer::CClientAccusation m_Accusation;
		
		//Login
		int m_LogInstance;
		int m_UserID;
#ifdef CONF_SQL
		int m_UserLevel;
#endif
		char m_aUsername[MAX_NAME_LENGTH];

		// DDRace

		NETADDR m_Addr;
		bool m_GotDDNetVersionPacket;
		bool m_DDNetVersionSettled;
		int m_DDNetVersion;
		char m_aDDNetVersionStr[64];
		CUuid m_ConnectionID;

		int m_InfClassVersion;

		bool m_Sixup;
	};

	CClient m_aClients[MAX_CLIENTS];
	int IdMap[MAX_CLIENTS * VANILLA_MAX_CLIENTS];

	CSnapshotDelta m_SnapshotDelta;
	CSnapshotBuilder m_SnapshotBuilder;
	CSnapIDPool m_IDPool;
	CNetServer m_NetServer;
	CEcon m_Econ;
	CServerBan m_ServerBan;

	IEngineMap *m_pMap;

	int64_t m_GameStartTime;
	//int m_CurrentGameTick;
	int m_RunServer;

	bool m_MapReload;
	bool m_ReloadedWhenEmpty;
	int m_RconClientID;
	int m_RconAuthLevel;

	int64_t m_Lastheartbeat;
	//static NETADDR4 master_server;
	
	char m_aPreviousMap[64];
	char m_aCurrentMap[64];
	char m_aShutdownReason[128];
	SHA256_DIGEST m_CurrentMapSha256;
	unsigned m_CurrentMapCrc;
	unsigned char *m_pCurrentMapData;
	unsigned int m_CurrentMapSize;

	bool m_ServerInfoHighLoad;
	int64_t m_ServerInfoFirstRequest;
	int m_ServerInfoNumRequests;
	int64_t m_ServerInfoRequestLogTick;
	int m_ServerInfoRequestLogRecords;

	CDemoRecorder m_aDemoRecorder[1];
	CRegister m_Register;

	std::vector<CNameBan> m_vNameBans;

	CServer();
	virtual ~CServer();

	bool IsClientNameAvailable(int ClientID, const char *pNameRequest);
	bool SetClientNameImpl(int ClientID, const char *pNameRequest, bool Set);

	virtual bool WouldClientNameChange(int ClientID, const char *pNameRequest);
	virtual void SetClientName(int ClientID, const char *pName);
	virtual void SetClientClan(int ClientID, char const *pClan);
	virtual void SetClientCountry(int ClientID, int Country);

	void Kick(int ClientID, const char *pReason);

	void DemoRecorder_HandleAutoStart();
	bool DemoRecorder_IsRecording();

	//int Tick()
	int64_t TickStartTime(int Tick);
	//int TickSpeed()

	int Init();

	void SendLogLine(const CLogMessage *pMessage);
	void SetRconCID(int ClientID);
	int GetAuthedState(int ClientID) const;
	int GetClientInfo(int ClientID, CClientInfo *pInfo) const;
	void SetClientDDNetVersion(int ClientID, int DDNetVersion);
	void GetClientAddr(int ClientID, char *pAddrStr, int Size) const;
	const char *ClientName(int ClientID) const;
	const char *ClientClan(int ClientID) const;
	int ClientCountry(int ClientID) const;
	bool ClientIngame(int ClientID) const;
	bool ClientIsBot(int ClientID) const override;
	int Port() const;
	int MaxClients() const;
	int ClientCount() const;
	int DistinctClientCount() const;

	int GetClientVersion(int ClientID) const override;
	int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID) override;

	void DoSnapshot();

	int NewBot(int ClientID);
	int DelBot(int ClientID);

	static int NewClientCallback(int ClientID, void *pUser, bool Sixup);
	static int NewClientNoAuthCallback(int ClientID, void *pUser);
	static int DelClientCallback(int ClientID, int Type, const char *pReason, void *pUser);

	static int ClientRejoinCallback(int ClientID, void *pUser);

	void SendMap(int ClientID);
	void SendMapData(int ClientID, int Chunk);
	
	void SendConnectionReady(int ClientID);
	void SendRconLine(int ClientID, const char *pLine);
	// Accepts -1 as ClientID to mean "all clients with at least auth level admin"
	void SendRconLogLine(int ClientID, const CLogMessage *pMessage);

	void SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void UpdateClientRconCommands();

	void ProcessClientPacket(CNetChunk *pPacket);

	void SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients);
	void SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type);
	void UpdateServerInfo();

	void PumpNetwork(bool PacketWaiting);

	bool GetMapReload() const override { return m_MapReload; }
	void ChangeMap(const char *pMap) override;
	const char *GetMapName() const override;
	int LoadMap(const char *pMapName);

	void InitRegister(CNetServer *pNetServer, IEngineMasterServer *pMasterServer, IConsole *pConsole);
	int Run();

	static void ConKick(IConsole::IResult *pResult, void *pUser);
	static void ConStatus(IConsole::IResult *pResult, void *pUser);
	static void ConStatusExtended(IConsole::IResult *pResult, void *pUser);
	static void ConShutdown(IConsole::IResult *pResult, void *pUser);
	static void ConRecord(IConsole::IResult *pResult, void *pUser);
	static void ConStopRecord(IConsole::IResult *pResult, void *pUser);
	static void ConMapReload(IConsole::IResult *pResult, void *pUser);
	static void ConLogout(IConsole::IResult *pResult, void *pUser);
	static void ConShowIps(IConsole::IResult *pResult, void *pUser);

	static void ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	static void ConMute(class IConsole::IResult *pResult, void *pUser);
	static void ConUnmute(class IConsole::IResult *pResult, void *pUser);
	static void ConWhisper(class IConsole::IResult *pResult, void *pUser);

	static void ConNameBan(IConsole::IResult *pResult, void *pUser);
	static void ConNameUnban(IConsole::IResult *pResult, void *pUser);
	static void ConNameBans(IConsole::IResult *pResult, void *pUser);

/* DDNET MODIFICATION START *******************************************/
#ifdef CONF_SQL
	static bool ConAddSqlServer(IConsole::IResult *pResult, void *pUserData);
	static bool ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData);

	static void CreateTablesThread(void *pData);
#endif
/* DDNET MODIFICATION END *********************************************/
	
	static void ConSetWeaponFireDelay(class IConsole::IResult *pResult, void *pUserData);
	static void ConSetWeaponAmmoRegen(class IConsole::IResult *pResult, void *pUserData);
	static void ConSetWeaponMaxAmmo(class IConsole::IResult *pResult, void *pUserData);

	void RegisterCommands();


	virtual int SnapNewID();
	virtual void SnapFreeID(int ID);
	virtual void *SnapNewItem(int Type, int ID, int Size);
	void SnapSetStaticsize(int ItemType, int Size);
	
/* INFECTION MODIFICATION START ***************************************/
public:
	int GetClientInfclassVersion(int ClientID) const override;

	virtual int GetClientDefaultScoreMode(int ClientID);
	virtual void SetClientDefaultScoreMode(int ClientID, int Value);

	virtual const char *GetClientLanguage(int ClientID);
	virtual void SetClientLanguage(int ClientID, const char *pLanguage);

	int GetFireDelay(INFWEAPON WID) override;
	void SetFireDelay(INFWEAPON WID, int Time) override;

	int GetAmmoRegenTime(INFWEAPON WID) override;
	void SetAmmoRegenTime(INFWEAPON WID, int Time) override;

	int GetMaxAmmo(INFWEAPON WID) override;
	void SetMaxAmmo(INFWEAPON WID, int n) override;

	virtual int GetClientNbRound(int ClientID);
	
	void SetPlayerClassEnabled(int PlayerClass, bool Enabled) override;
	void SetPlayerClassProbability(int PlayerClass, int Probability) override;
	virtual bool IsClientLogged(int ClientID);
#ifdef CONF_SQL
	virtual void Login(int ClientID, const char* pUsername, const char* pPassword);
	virtual void Logout(int ClientID);
	virtual void SetEmail(int ClientID, const char* pEmail);
	virtual void Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail);
	virtual void ShowChallenge(int ClientID);
	virtual void ShowTop10(int ClientID, int ScoreType);
	virtual void ShowRank(int ClientID, int ScoreType);
	virtual void ShowGoal(int ClientID, int ScoreType);
	virtual void ShowStats(int ClientID, int UserId);
	virtual void RefreshChallenge();
	virtual int GetUserLevel(int ClientID);
#else
	virtual void Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail);
	virtual void Login(int ClientID, const char* pUsername, const char* pPassword);
	virtual void Logout(int ClientID);
#endif
	virtual void Ban(int ClientID, int Seconds, const char* pReason);
private:
	bool InitCaptcha();
	bool GenerateClientMap(const char *pMapFilePath, const char *pMapName);
	
public:
	class CGameServerCmd
	{
	public:
		virtual ~CGameServerCmd() {};
		virtual void Execute(IGameServer* pGameServer) = 0;
	};

private:
	CRoundStatistics m_RoundStatistics;
	CNetSession<IServer::CClientSession> m_NetSession;
	CNetSession<IServer::CClientAccusation> m_NetAccusation;

	IServer::CMapVote m_MapVotes[MAX_VOTE_OPTIONS];
	int m_MapVotesCounter;
	
#ifdef CONF_SQL
public:
	array<CGameServerCmd*> m_lGameServerCmds;
	LOCK m_GameServerCmdLock;
	LOCK m_ChallengeLock;
	char m_aChallengeWinner[16];
	int64_t m_ChallengeRefreshTick;
	int m_ChallengeType;
#endif
	int m_LastRegistrationRequestId = 0;

	int m_TimeShiftUnit;

	int m_InfAmmoRegenTime[NB_INFWEAPON];
	int m_InfFireDelay[NB_INFWEAPON];
	int m_InfMaxAmmo[NB_INFWEAPON];

public:
	void AddGameServerCmd(CGameServerCmd* pCmd);
	
	virtual CRoundStatistics* RoundStatistics() { return &m_RoundStatistics; }
	virtual void ResetStatistics();
	virtual void SendStatistics();

	void OnRoundIsOver();
	
	virtual void SetClientMemory(int ClientID, int Memory, bool Value = true);
	virtual void ResetClientMemoryAboutGame(int ClientID);
	virtual bool GetClientMemory(int ClientID, int Memory);
	
	virtual IServer::CClientSession* GetClientSession(int ClientID);
	
	virtual void AddAccusation(int From, int To, const char* pReason);
	virtual bool ClientShouldBeBanned(int ClientID);
	virtual void RemoveAccusations(int ClientID);

	virtual void AddMapVote(int From, const char* pCommand, const char* pReason, const char* pDesc);
	virtual void RemoveMapVotesForID(int ClientID);
	virtual void ResetMapVotes();
	virtual IServer::CMapVote* GetMapVote();

	virtual int GetActivePlayerCount();
	
	virtual int GetTimeShiftUnit() const { return m_TimeShiftUnit; } //In ms
/* INFECTION MODIFICATION END *****************************************/

	void GetClientAddr(int ClientID, NETADDR *pAddr) const;
	int m_aPrevStates[MAX_CLIENTS];
	char *GetAnnouncementLine(char const *FileName);
	unsigned m_AnnouncementLastLine;

	virtual const char *GetPreviousMapName() const;

	int *GetIdMap(int ClientID) override;

	bool ClientPrevIngame(int ClientID) override { return m_aPrevStates[ClientID] == CClient::STATE_INGAME; }

	void SendMsgRaw(int ClientID, const void *pData, int Size, int Flags) override;

	bool IsSixup(int ClientID) const override { return ClientID != SERVER_DEMO_CLIENT && m_aClients[ClientID].m_Sixup; }
};

extern CServer *CreateServer();
#endif
