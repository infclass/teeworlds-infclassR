/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_H
#define ENGINE_SERVER_H

#include <optional>
#include <type_traits>

#include <base/hash.h>
#include <base/math.h>

#include "kernel.h"
#include "message.h"
#include <engine/shared/protocol.h>
#include <game/generated/protocol.h>
#include <game/generated/protocol7.h>
#include <game/generated/protocolglue.h>

struct CAntibotRoundData;

enum class EClientDropType;

// When recording a demo on the server, the ClientId -1 is used
enum
{
	SERVER_DEMO_CLIENT = -1
};

/* INFECTION MODIFICATION START ***************************************/

enum
{
	CLIENTMEMORY_LANGUAGESELECTION=0,
	CLIENTMEMORY_TOP10,
	CLIENTMEMORY_MOTD,
	CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE,
	CLIENTMEMORY_SESSION_PROCESSED,
	NUM_CLIENTMEMORIES,
};

enum
{
	MAX_ACCUSATIONS = 8,
	MAX_MAPVOTEADDRESSES = 16,
};

enum
{
	CHATCATEGORY_DEFAULT=0,
	CHATCATEGORY_INFECTION,
	CHATCATEGORY_SCORE,
	CHATCATEGORY_PLAYER,
	CHATCATEGORY_INFECTED,
	CHATCATEGORY_HUMANS,
	CHATCATEGORY_ACCUSATION,
};

/* INFECTION MODIFICATION END *****************************************/

class IServer : public IInterface
{
	MACRO_INTERFACE("server")
protected:
	int m_CurrentGameTick;
	int m_TickSpeed;

public:
	class CLocalization* m_pLocalization;

public:
	enum
	{
		AUTHED_NO=0,
		AUTHED_MOD,
		AUTHED_ADMIN,
	};
	
public:
	/*
		Structure: CClientInfo
	*/
	struct CClientInfo
	{
		const char *m_pName;
		int m_Latency;
		bool m_GotDDNetVersion;
		int m_DDNetVersion;
		int m_InfClassVersion;
		const char *m_pDDNetVersionStr;
		const CUuid *m_pConnectionId;
	};
	
	struct CClientSession
	{
		int m_RoundId;
		int m_Class;
		int m_MuteTick;
		int m_LastInfectionTime;
	};
	
	struct CClientAccusation
	{
		int m_Num;
		NETADDR m_Addresses[MAX_ACCUSATIONS];
	};

	struct CMapVote
	{
		const char *m_pCommand; // for example "change_map infc_warehouse" or "skip_map"
		int m_Num; // how many people want to start this vote
		NETADDR *m_pAddresses; // addresses of the people who want to start this vote
		const char *m_pDesc; // name of the vote
		const char *m_pReason;
	};
	
	inline class CLocalization* Localization() { return m_pLocalization; }

	int Tick() const { return m_CurrentGameTick; }
	int TickSpeed() const { return m_TickSpeed; }

	virtual int Port() const = 0;
	virtual int MaxClients() const = 0;
	virtual int ClientCount() const = 0;
	virtual int DistinctClientCount() const = 0;
	virtual const char *ClientName(int ClientId) const = 0;
	virtual const char *ClientClan(int ClientId) const = 0;
	virtual int ClientCountry(int ClientId) const = 0;
	virtual bool ClientIngame(int ClientId) const = 0;
	virtual bool ClientIsBot(int ClientId) const = 0;
	virtual bool GetClientInfo(int ClientId, CClientInfo *pInfo) const = 0;
	virtual void SetClientDDNetVersion(int ClientId, int DDNetVersion) = 0;
	virtual void GetClientAddr(int ClientId, char *pAddrStr, int Size) const = 0;

	/**
	 * Returns the version of the client with the given client ID.
	 *
	 * @param ClientId the client Id, which must be between 0 and
	 * MAX_CLIENTS - 1, or equal to SERVER_DEMO_CLIENT for server demos.
	 *
	 * @return The version of the client with the given client ID.
	 * For server demos this is always the latest client version.
	 * On errors, VERSION_NONE is returned.
	 */
	virtual int GetClientVersion(int ClientId) const = 0;
	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientId) = 0;

	template<class T, typename std::enable_if<!protocol7::is_sixup<T>::value, int>::type = 0>
	inline int SendPackMsg(const T *pMsg, int Flags, int ClientId)
	{
		int Result = 0;
		if(ClientId == -1)
		{
			for(int i = 0; i < MaxClients(); i++)
				if(ClientIngame(i))
					Result = SendPackMsgTranslate(pMsg, Flags, i);
		}
		else
		{
			Result = SendPackMsgTranslate(pMsg, Flags, ClientId);
		}
		return Result;
	}

	template<class T, typename std::enable_if<protocol7::is_sixup<T>::value, int>::type = 1>
	inline int SendPackMsg(const T *pMsg, int Flags, int ClientId)
	{
		int Result = 0;
		if(ClientId == -1)
		{
			for(int i = 0; i < MaxClients(); i++)
				if(ClientIngame(i) && IsSixup(i))
					Result = SendPackMsgOne(pMsg, Flags, i);
		}
		else if(IsSixup(ClientId))
			Result = SendPackMsgOne(pMsg, Flags, ClientId);

		return Result;
	}

	template<class T>
	int SendPackMsgTranslate(const T *pMsg, int Flags, int ClientId)
	{
		return SendPackMsgOne(pMsg, Flags, ClientId);
	}

	int SendPackMsgTranslate(const CNetMsg_Sv_Emoticon *pMsg, int Flags, int ClientId)
	{
		CNetMsg_Sv_Emoticon MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));
		return Translate(MsgCopy.m_ClientId, ClientId) && SendPackMsgOne(&MsgCopy, Flags, ClientId);
	}

	int SendPackMsgTranslate(const CNetMsg_Sv_Chat *pMsg, int Flags, int ClientId)
	{
		CNetMsg_Sv_Chat MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));

		char aBuf[1000];
		if(MsgCopy.m_ClientId >= 0 && !Translate(MsgCopy.m_ClientId, ClientId))
		{
			str_format(aBuf, sizeof(aBuf), "%s: %s", ClientName(MsgCopy.m_ClientId), MsgCopy.m_pMessage);
			MsgCopy.m_pMessage = aBuf;
			MsgCopy.m_ClientId = VANILLA_MAX_CLIENTS - 1;
		}

		if(IsSixup(ClientId))
		{
			protocol7::CNetMsg_Sv_Chat Msg7;
			Msg7.m_ClientId = MsgCopy.m_ClientId;
			Msg7.m_pMessage = MsgCopy.m_pMessage;
			Msg7.m_Mode = MsgCopy.m_Team > 0 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
			Msg7.m_TargetId = -1;
			return SendPackMsgOne(&Msg7, Flags, ClientId);
		}

		return SendPackMsgOne(&MsgCopy, Flags, ClientId);
	}

	int SendPackMsgTranslate(const CNetMsg_Sv_KillMsg *pMsg, int Flags, int ClientId)
	{
		CNetMsg_Sv_KillMsg MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));
		if(!Translate(MsgCopy.m_Victim, ClientId))
			return 0;
		if(!Translate(MsgCopy.m_Killer, ClientId))
			MsgCopy.m_Killer = MsgCopy.m_Victim;
		return SendPackMsgOne(&MsgCopy, Flags, ClientId);
	}

	template<class T>
	int SendPackMsgOne(const T *pMsg, int Flags, int ClientId)
	{
		dbg_assert(ClientId != -1, "SendPackMsgOne called with -1");
		CMsgPacker Packer(T::ms_MsgId, false, protocol7::is_sixup<T>::value);

		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags, ClientId);
	}

	bool Translate(int &Target, int Client)
	{
		if(IsSixup(Client))
			return true;
		if(GetClientVersion(Client) >= VERSION_DDNET_OLD)
			return true;
		int *pMap = GetIdMap(Client);
		bool Found = false;
		for(int i = 0; i < VANILLA_MAX_CLIENTS; i++)
		{
			if(Target == pMap[i])
			{
				Target = i;
				Found = true;
				break;
			}
		}
		return Found;
	}

	bool ReverseTranslate(int &Target, int Client)
	{
		if(IsSixup(Client))
			return true;
		if(GetClientVersion(Client) >= VERSION_DDNET_OLD)
			return true;
		Target = clamp(Target, 0, VANILLA_MAX_CLIENTS - 1);
		int *pMap = GetIdMap(Client);
		if(pMap[Target] == -1)
			return false;
		Target = pMap[Target];
		return true;
	}

	virtual int NewBot(int ClientId) = 0;
	virtual int DelBot(int ClientId) = 0;

	virtual void GetMapInfo(char *pMapName, int MapNameSize, int *pMapSize, SHA256_DIGEST *pSha256, int *pMapCrc) = 0;

	virtual bool WouldClientNameChange(int ClientId, const char *pNameRequest) = 0;
	virtual void SetClientName(int ClientId, char const *pName) = 0;
	virtual void SetClientClan(int ClientId, char const *pClan) = 0;
	virtual void SetClientCountry(int ClientId, int Country) = 0;
	virtual void SetClientScore(int ClientId, std::optional<int> Score) = 0;
	virtual void SetClientFlags(int ClientId, int Flags) = 0;

	virtual int SnapNewId() = 0;
	virtual void SnapFreeId(int Id) = 0;
	virtual void *SnapNewItem(int Type, int Id, int Size) = 0;

	template<typename T>
	T *SnapNewItem(int Id)
	{
		const int Type = protocol7::is_sixup<T>::value ? -T::ms_MsgId : T::ms_MsgId;
		return static_cast<T *>(SnapNewItem(Type, Id, sizeof(T)));
	}

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	enum
	{
		RCON_CID_SERV = -1,
		RCON_CID_VOTE = -2,
	};
	virtual void SetRconCid(int ClientId) = 0;
	virtual int GetAuthedState(int ClientId) const = 0;
	virtual void Kick(int ClientId, const char *pReason) = 0;
	virtual void Ban(int ClientId, int Seconds, const char *pReason) = 0;
	virtual void RedirectClient(int ClientId, int Port, bool Verbose = false) = 0;
	virtual bool GetMapReload() const = 0;
	virtual void ChangeMap(const char *pMap) = 0;

	virtual void DemoRecorder_HandleAutoStart() = 0;

	// DDRace

	virtual void SaveDemo(int ClientId, float Time) = 0;
	virtual void StartRecord(int ClientId) = 0;
	virtual void StopRecord(int ClientId) = 0;
	virtual bool IsRecording(int ClientId) = 0;

	virtual void GetClientAddr(int ClientId, NETADDR *pAddr) const = 0;

	virtual int *GetIdMap(int ClientId) = 0;

	virtual const char *GetAnnouncementLine(char const *pFileName) = 0;
	virtual bool ClientPrevIngame(int ClientId) = 0;
	virtual const char *GetNetErrorString(int ClientId) = 0;
	virtual void ResetNetErrorString(int ClientId) = 0;
	virtual bool SetTimedOut(int ClientId, int OrigId) = 0;
	virtual void SetTimeoutProtected(int ClientId) = 0;

	virtual void SetErrorShutdown(const char *pReason) = 0;
	virtual void ExpireServerInfo() = 0;

	virtual void FillAntibot(CAntibotRoundData *pData) = 0;

	virtual void SendMsgRaw(int ClientId, const void *pData, int Size, int Flags) = 0;

	virtual const char *GetMapName() const = 0;

	virtual bool IsSixup(int ClientId) const = 0;

/* INFECTION MODIFICATION START ***************************************/
	virtual int GetClientInfclassVersion(int ClientId) const = 0;

	virtual int GetClientNbRound(int ClientId) = 0;
	
	virtual const char* GetClientLanguage(int ClientId) = 0;
	virtual void SetClientLanguage(int ClientId, const char* pLanguage) = 0;

	virtual bool IsClientLogged(int ClientId) = 0;
#ifdef CONF_SQL
	virtual void Login(int ClientId, const char* pUsername, const char* pPassword) = 0;
	virtual void Logout(int ClientId) = 0;
	virtual void SetEmail(int ClientId, const char* pEmail) = 0;
	virtual void Register(int ClientId, const char* pUsername, const char* pPassword, const char* pEmail) = 0;
	virtual void ShowTop10(int ClientId, int ScoreType) = 0;
	virtual void ShowChallenge(int ClientId) = 0;
	virtual void ShowRank(int ClientId, int ScoreType) = 0;
	virtual void ShowGoal(int ClientId, int ScoreType) = 0;
	virtual void ShowStats(int ClientId, int UserId) = 0;
	virtual int GetUserLevel(int ClientId) = 0;
#else
	virtual void Register(int ClientId, const char* pUsername, const char* pPassword, const char* pEmail) = 0;
	virtual void Login(int ClientId, const char* pUsername, const char* pPassword) = 0;
	virtual void Logout(int ClientId) = 0;
#endif

public:
	virtual class CRoundStatistics* RoundStatistics() = 0;
	virtual void ResetStatistics() = 0;
	virtual void SendStatistics() = 0;

	virtual void OnRoundIsOver() = 0;
	
	virtual void SetClientMemory(int ClientId, int Memory, bool Value = true) = 0;
	virtual void ResetClientMemoryAboutGame(int ClientId) = 0;
	virtual bool GetClientMemory(int ClientId, int Memory) = 0;
	virtual IServer::CClientSession* GetClientSession(int ClientId) = 0;
	virtual void AddAccusation(int From, int To, const char* pReason) = 0;
	virtual bool ClientShouldBeBanned(int ClientId) = 0;
	virtual void RemoveAccusations(int ClientId) = 0;
	virtual void AddMapVote(int From, const char* pCommand, const char* pReason, const char* pDesc) = 0;
	virtual void RemoveMapVotesForId(int ClientId) = 0;
	virtual void ResetMapVotes() = 0;
	virtual CMapVote* GetMapVote() = 0;
	
	virtual int GetTimeShiftUnit() const = 0; //In ms

	virtual const char *GetPreviousMapName() const = 0;
	virtual uint32_t GetActivePlayerCount() = 0;
/* INFECTION MODIFICATION END *****************************************/

};

class IGameServer : public IInterface
{
	MACRO_INTERFACE("gameserver")
protected:
public:
	virtual void OnInit() = 0;
	virtual void OnConsoleInit() = 0;
	virtual void OnMapChange(char *pNewMapName, int MapNameSize) = 0;
	virtual void OnShutdown() = 0;

	virtual void OnTick() = 0;
	virtual void OnPreSnap() = 0;
	virtual void OnSnap(int ClientId) = 0;
	virtual void OnPostSnap() = 0;

	virtual void OnMessage(int MsgId, CUnpacker *pUnpacker, int ClientId) = 0;

	// Called before map reload, for any data that the game wants to
	// persist to the next map.
	//
	// Has the size of the return value of `PersistentClientDataSize()`.
	//
	// Returns whether the game should be supplied with the data when the
	// client connects for the next map.
	virtual bool OnClientDataPersist(int ClientId, void *pData) = 0;

	// Called when a client connects.
	//
	// If it is reconnecting to the game after a map change, the
	// `pPersistentData` point is nonnull and contains the data the game
	// previously stored.
	virtual void OnClientConnected(int ClientId, void *pPersistentData) = 0;

	virtual void OnClientEnter(int ClientId) = 0;
	virtual void OnClientDrop(int ClientId, EClientDropType Type, const char *pReason) = 0;
	virtual void OnClientPrepareInput(int ClientId, void *pInput) = 0;
	virtual void OnClientDirectInput(int ClientId, void *pInput) = 0;
	virtual void OnClientPredictedInput(int ClientId, void *pInput) = 0;
	virtual void OnClientPredictedEarlyInput(int ClientId, void *pInput) = 0;

	virtual bool IsClientReady(int ClientId) const = 0;
	virtual bool IsClientPlayer(int ClientId) const = 0;

	virtual int PersistentClientDataSize() const = 0;

	virtual CUuid GameUuid() const = 0;
	virtual const char *GameType() const = 0;
	virtual const char *Version() const = 0;
	virtual const char *NetVersion() const = 0;

/* INFECTION MODIFICATION START ***************************************/
	virtual void ClearBroadcast(int To, int Priority) = 0;
	virtual void SendBroadcast_Localization(int To, int Priority, int LifeSpan, const char* pText, ...) = 0;
	virtual void SendBroadcast_Localization_P(int To, int Priority, int LifeSpan, int Number, const char* pText, ...) = 0;
	virtual void SendChatTarget(int To, const char* pText) = 0;
	virtual void SendChatTarget_Localization(int To, int Category, const char* pText, ...) = 0;
	virtual void SendChatTarget_Localization_P(int To, int Category, int Number, const char* pText, ...) = 0;
	virtual void SendMOTD(int To, const char* pText) = 0;
	virtual void SendMOTD_Localization(int To, const char* pText, ...) = 0;
/* INFECTION MODIFICATION END *****************************************/

	// DDRace

	virtual void OnPreTickTeehistorian() = 0;

	virtual void OnSetAuthed(int ClientId, int Level) = 0;
	virtual bool PlayerExists(int ClientId) const = 0;

	virtual void OnClientEngineJoin(int ClientId, bool Sixup) = 0;
	virtual void OnClientEngineDrop(int ClientId, const char *pReason) = 0;

	virtual void FillAntibot(CAntibotRoundData *pData) = 0;

	/**
	 * Used to report custom player info to master servers.
	 *
	 * @param aBuf Should be the json key values to add, starting with a ',' beforehand, like: ',"skin": "default", "team": 1'
	 * @param i The client id.
	 */
	virtual void OnUpdatePlayerServerInfo(char *aBuf, int BufSize, int Id) = 0;
};

extern IGameServer *CreateGameServer();
#endif
