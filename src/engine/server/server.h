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
#include <engine/shared/http.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/snapshot.h>
#include <game/voting.h>

#include <list>

/* DDNET MODIFICATION START *******************************************/
#include "base/logger.h"
/* DDNET MODIFICATION END *********************************************/

#include "name_ban.h"

class CLogMessage;

class CSnapIdPool
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

	CID m_aIds[MAX_IDS];

	int m_FirstFree;
	int m_FirstTimed;
	int m_LastTimed;
	int m_Usage;
	int m_InUsage;

public:
	CSnapIdPool();

	void Reset();
	void RemoveFirstTimeout();
	int NewId();
	void TimeoutIds();
	void FreeId(int Id);
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
	class IRegister *m_pRegister;
	IEngine *m_pEngine;

#if defined(CONF_UPNP)
	CUPnP m_UPnP;
#endif

#if defined(CONF_FAMILY_UNIX)
	UNIXSOCKETADDR m_ConnLoggingDestAddr;
	bool m_ConnLoggingSocketCreated;
	UNIXSOCKET m_ConnLoggingSocket;
#endif

	class CDbConnectionPool *m_pConnectionPool;

public:
	class IGameServer *GameServer() { return m_pGameServer; }
	class CConfig *Config() { return m_pConfig; }
	const CConfig *Config() const { return m_pConfig; }
	class IConsole *Console() { return m_pConsole; }
	class IStorage *Storage() { return m_pStorage; }
	class CDbConnectionPool *DbPool() { return m_pConnectionPool; }
	IEngine *Engine() { return m_pEngine; }

	enum
	{
		MAX_RCONCMD_SEND = 16,
		MAX_RCONCMD_RATIO = 8,
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
			STATE_REDIRECTED,

			SNAPRATE_INIT = 0,
			SNAPRATE_FULL,
			SNAPRATE_RECOVER,

			DNSBL_STATE_NONE = 0,
			DNSBL_STATE_PENDING,
			DNSBL_STATE_BLACKLISTED,
			DNSBL_STATE_WHITELISTED,
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

		double m_Traffic;
		int64_t m_TrafficSince;

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
		int m_Flags;
		bool m_ShowIps;

		const IConsole::CCommandInfo *m_pRconCmdToSend;

		bool m_HasPersistentData;
		void *m_pPersistentData;

		void Reset(bool ResetScore=true);
		
		int m_NbRound;
		
		char m_aLanguage[16];
		int m_WaitingTime;

		bool m_Memory[NUM_CLIENTMEMORIES]{};
		IServer::CClientSession m_Session{};
		IServer::CClientAccusation m_Accusation{};
		
		//Login
		int m_LogInstance;
		int m_UserId;
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
		CUuid m_ConnectionId;
		int64_t m_RedirectDropTime;

		int m_InfClassVersion;

		bool m_Sixup;
	};

	CClient m_aClients[MAX_CLIENTS];
	int m_aIdMap[MAX_CLIENTS * VANILLA_MAX_CLIENTS];

	CSnapshotDelta m_SnapshotDelta;
	CSnapshotBuilder m_SnapshotBuilder;
	CSnapIdPool m_IdPool;
	CNetServer m_NetServer;
	CEcon m_Econ;
	CServerBan m_ServerBan;
	CHttp m_Http;

	IEngineMap *m_pMap;

	int64_t m_GameStartTime;
	//int m_CurrentGameTick;

	enum
	{
		UNINITIALIZED = 0,
		RUNNING = 1,
		STOPPING = 2
	};

	int m_RunServer;
	bool m_ReconnectClients = false;

	bool m_MapReload;
	bool m_ReloadedWhenEmpty;
	int m_RconClientId;
	int m_RconAuthLevel;
	char m_aShutdownReason[128];

	//static NETADDR4 master_server;

	enum
	{
		MAP_TYPE_SIX = 0,
		MAP_TYPE_SIXUP,
		NUM_MAP_TYPES
	};

	enum
	{
		RECORDER_MANUAL = MAX_CLIENTS,
		RECORDER_AUTO = MAX_CLIENTS + 1,
		NUM_RECORDERS = MAX_CLIENTS + 2,
	};

	char m_aPreviousMap[IO_MAX_PATH_LENGTH];
	char m_aCurrentMap[IO_MAX_PATH_LENGTH];
	SHA256_DIGEST m_aCurrentMapSha256[NUM_MAP_TYPES];
	unsigned m_aCurrentMapCrc[NUM_MAP_TYPES];
	unsigned char *m_apCurrentMapData[NUM_MAP_TYPES];
	unsigned int m_aCurrentMapSize[NUM_MAP_TYPES];

	CDemoRecorder m_aDemoRecorder[NUM_RECORDERS];

	int64_t m_ServerInfoFirstRequest;
	int m_ServerInfoNumRequests;

	char m_aErrorShutdownReason[128];

	std::vector<CNameBan> m_vNameBans;

	size_t m_AnnouncementLastLine;
	std::vector<std::string> m_vAnnouncements;
	char m_aAnnouncementFile[IO_MAX_PATH_LENGTH];

	std::shared_ptr<ILogger> m_pFileLogger = nullptr;
	std::shared_ptr<ILogger> m_pStdoutLogger = nullptr;

	CServer();
	~CServer();

	bool IsClientNameAvailable(int ClientId, const char *pNameRequest);
	bool SetClientNameImpl(int ClientId, const char *pNameRequest, bool Set);

	bool WouldClientNameChange(int ClientId, const char *pNameRequest) override;
	void SetClientName(int ClientId, const char *pName) override;
	void SetClientClan(int ClientId, char const *pClan) override;
	void SetClientCountry(int ClientId, int Country) override;
	void SetClientScore(int ClientId, std::optional<int> Score) override;
	void SetClientFlags(int ClientId, int Flags) override;

	void Kick(int ClientId, const char *pReason) override;
	void Ban(int ClientId, int Seconds, const char *pReason) override;
	void RedirectClient(int ClientId, int Port, bool Verbose = false) override;

	void DemoRecorder_HandleAutoStart() override;

	//int Tick()
	int64_t TickStartTime(int Tick);
	//int TickSpeed()

	int Init();

	void SendLogLine(const CLogMessage *pMessage);
	void SetRconCid(int ClientId) override;
	int GetAuthedState(int ClientId) const override;
	void GetMapInfo(char *pMapName, int MapNameSize, int *pMapSize, SHA256_DIGEST *pMapSha256, int *pMapCrc) override;
	bool GetClientInfo(int ClientId, CClientInfo *pInfo) const override;
	void SetClientDDNetVersion(int ClientId, int DDNetVersion) override;
	void GetClientAddr(int ClientId, char *pAddrStr, int Size) const override;
	const char *ClientName(int ClientId) const override;
	const char *ClientClan(int ClientId) const override;
	int ClientCountry(int ClientId) const override;
	bool ClientIngame(int ClientId) const override;
	bool ClientIsBot(int ClientId) const override;
	int Port() const override;
	int MaxClients() const override;
	int ClientCount() const override;
	int DistinctClientCount() const override;

	int GetClientVersion(int ClientId) const override;
	int SendMsg(CMsgPacker *pMsg, int Flags, int ClientId) override;

	void DoSnapshot();

	int NewBot(int ClientId) override;
	int DelBot(int ClientId) override;

	static int NewClientCallback(int ClientId, void *pUser, bool Sixup);
	static int NewClientNoAuthCallback(int ClientId, void *pUser);
	static int DelClientCallback(int ClientId, EClientDropType Type, const char *pReason, void *pUser);

	static int ClientRejoinCallback(int ClientId, void *pUser);

	void SendRconType(int ClientId, bool UsernameReq);
	void SendCapabilities(int ClientId);
	void SendMap(int ClientId);
	void SendMapData(int ClientId, int Chunk);
	void SendConnectionReady(int ClientId);
	void SendRconLine(int ClientId, const char *pLine);
	// Accepts -1 as ClientId to mean "all clients with at least auth level admin"
	void SendRconLogLine(int ClientId, const CLogMessage *pMessage);

	void SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientId);
	void SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientId);
	void UpdateClientRconCommands();

	void ProcessClientPacket(CNetChunk *pPacket);

	class CCache
	{
	public:
		class CCacheChunk
		{
		public:
			CCacheChunk(const void *pData, int Size);
			CCacheChunk(const CCacheChunk &) = delete;
			CCacheChunk(CCacheChunk &&) = default;

			std::vector<uint8_t> m_vData;
		};

		std::vector<CCacheChunk> m_vCache;

		CCache();
		~CCache();

		void AddChunk(const void *pData, int Size);
		void Clear();
	};
	CCache m_aServerInfoCache[3 * 2];
	CCache m_aSixupServerInfoCache[2];
	bool m_ServerInfoNeedsUpdate;

	void FillAntibot(CAntibotRoundData *pData) override;

	void ExpireServerInfo() override;
	void CacheServerInfo(CCache *pCache, int Type, bool SendClients);
	void CacheServerInfoSixup(CCache *pCache, bool SendClients);
	void SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients);
	void GetServerInfoSixup(CPacker *pPacker, int Token, bool SendClients);
	bool RateLimitServerInfoConnless();
	void SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type);
	void UpdateRegisterServerInfo();
	void UpdateServerInfo(bool Resend = false);

	void PumpNetwork(bool PacketWaiting);

	bool GetMapReload() const override { return m_MapReload; }
	void ChangeMap(const char *pMap) override;
	const char *GetMapName() const override;
	int LoadMap(const char *pMapName);

	void SaveDemo(int ClientId, float Time) override;
	void StartRecord(int ClientId) override;
	void StopRecord(int ClientId) override;
	bool IsRecording(int ClientId) override;

	int Run();

	static void ConKick(IConsole::IResult *pResult, void *pUser);
	static void ConStatus(IConsole::IResult *pResult, void *pUser);
	static void ConStatusExtended(IConsole::IResult *pResult, void *pUser);
	static void ConShutdown(IConsole::IResult *pResult, void *pUser);
	static void ConShutdown2(IConsole::IResult *pResult, void *pUser);
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

	// console commands for sqlmasters
	static void ConAddSqlServer(IConsole::IResult *pResult, void *pUserData);
	static void ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData);

	void LogoutClient(int ClientId, const char *pReason);

	void ConchainRconPasswordChangeGeneric(int Level, const char *pCurrent, IConsole::IResult *pResult);
	static void ConchainRconPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainRconModPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainRconHelperPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainMapUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSixupUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainLoglevel(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainStdoutOutputLevel(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

#if defined(CONF_FAMILY_UNIX)
	static void ConchainConnLoggingServerChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
#endif

	void RegisterCommands();

	int SnapNewId() override;
	void SnapFreeId(int Id) override;
	void *SnapNewItem(int Type, int Id, int Size) override;
	void SnapSetStaticsize(int ItemType, int Size) override;

	// DDRace

	void GetClientAddr(int ClientId, NETADDR *pAddr) const override;
	int m_aPrevStates[MAX_CLIENTS];
	const char *GetAnnouncementLine(char const *pFileName) override;

	int *GetIdMap(int ClientId) override;

	bool ClientPrevIngame(int ClientId) override { return m_aPrevStates[ClientId] == CClient::STATE_INGAME; }
	const char *GetNetErrorString(int ClientId) override { return m_NetServer.ErrorString(ClientId); }
	void ResetNetErrorString(int ClientId) override { m_NetServer.ResetErrorString(ClientId); }
	bool SetTimedOut(int ClientId, int OrigId) override;
	void SetTimeoutProtected(int ClientId) override { m_NetServer.SetTimeoutProtected(ClientId); }

	void SendMsgRaw(int ClientId, const void *pData, int Size, int Flags) override;

	bool ErrorShutdown() const { return m_aErrorShutdownReason[0] != 0; }
	void SetErrorShutdown(const char *pReason) override;

	bool IsSixup(int ClientId) const override { return ClientId != SERVER_DEMO_CLIENT && m_aClients[ClientId].m_Sixup; }

	void SetLoggers(std::shared_ptr<ILogger> &&pFileLogger, std::shared_ptr<ILogger> &&pStdoutLogger);

#ifdef CONF_FAMILY_UNIX
	enum CONN_LOGGING_CMD
		{
			OPEN_SESSION = 1,
			CLOSE_SESSION = 2,
		};

	void SendConnLoggingCommand(CONN_LOGGING_CMD Cmd, const NETADDR *pAddr);
#endif
/* INFECTION MODIFICATION START ***************************************/
public:
	int GetClientInfclassVersion(int ClientId) const override;

	const char *GetClientLanguage(int ClientId) override;
	void SetClientLanguage(int ClientId, const char *pLanguage) override;

	int GetClientNbRound(int ClientId) override;

	bool IsClientLogged(int ClientId) override;
#ifdef CONF_SQL
	void Login(int ClientId, const char* pUsername, const char* pPassword) override;
	void Logout(int ClientId) override;
	void SetEmail(int ClientId, const char* pEmail) override;
	void Register(int ClientId, const char* pUsername, const char* pPassword, const char* pEmail) override;
	void ShowChallenge(int ClientId) override;
	void ShowTop10(int ClientId, int ScoreType) override;
	void ShowRank(int ClientId, int ScoreType) override;
	void ShowGoal(int ClientId, int ScoreType) override;
	void ShowStats(int ClientId, int UserId) override;
	void RefreshChallenge() override;
	int GetUserLevel(int ClientId) override;
#else
	void Register(int ClientId, const char* pUsername, const char* pPassword, const char* pEmail) override;
	void Login(int ClientId, const char* pUsername, const char* pPassword) override;
	void Logout(int ClientId) override;
#endif
private:
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

public:
	void AddGameServerCmd(CGameServerCmd* pCmd);
	
	virtual CRoundStatistics* RoundStatistics() override { return &m_RoundStatistics; }
	void ResetStatistics() override;
	void SendStatistics() override;

	void OnRoundIsOver() override;
	
	void SetClientMemory(int ClientId, int Memory, bool Value = true) override;
	void ResetClientMemoryAboutGame(int ClientId) override;
	bool GetClientMemory(int ClientId, int Memory) override;
	
	IServer::CClientSession* GetClientSession(int ClientId) override;
	
	void AddAccusation(int From, int To, const char* pReason) override;
	bool ClientShouldBeBanned(int ClientId) override;
	void RemoveAccusations(int ClientId) override;

	void AddMapVote(int From, const char* pCommand, const char* pReason, const char* pDesc) override;
	void RemoveMapVotesForId(int ClientId) override;
	void ResetMapVotes() override;
	IServer::CMapVote* GetMapVote() override;

	uint32_t GetActivePlayerCount() override;
	
	int GetTimeShiftUnit() const override { return m_TimeShiftUnit; } //In ms

	const char *GetPreviousMapName() const override;
/* INFECTION MODIFICATION END *****************************************/

};

extern CServer *CreateServer();
#endif
