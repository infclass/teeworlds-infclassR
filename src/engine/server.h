/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#ifndef ENGINE_SERVER_H
#define ENGINE_SERVER_H

#include <base/math.h>

#include "kernel.h"
#include "message.h"

#include <game/generated/protocol.h>
#include <engine/shared/protocol.h>

#include <string>

// When recording a demo on the server, the ClientID -1 is used
enum
{
	SERVER_DEMO_CLIENT = -1
};

/* INFECTION MODIFICATION START ***************************************/
enum class INFWEAPON
{
	NONE,
	HAMMER,
	GUN,
	SHOTGUN,
	GRENADE,
	LASER,
	NINJA,

	ENGINEER_LASER,

	SNIPER_LASER,

	SOLDIER_GRENADE,

	SCIENTIST_GRENADE,
	SCIENTIST_LASER,

	MEDIC_GRENADE,
	MEDIC_LASER,
	MEDIC_SHOTGUN,

	HERO_GRENADE,
	HERO_LASER,
	HERO_SHOTGUN,

	BIOLOGIST_SHOTGUN,
	BIOLOGIST_LASER,

	LOOPER_LASER,
	LOOPER_GRENADE,

	NINJA_HAMMER,
	NINJA_GRENADE,

	MERCENARY_GUN,
	MERCENARY_GRENADE,
	MERCENARY_LASER,

	BLINDING_LASER,
};

constexpr int NB_INFWEAPON = static_cast<int>(INFWEAPON::BLINDING_LASER) + 1;

enum
{
	PLAYERSCOREMODE_CLASS = 0,
	PLAYERSCOREMODE_SCORE,
	PLAYERSCOREMODE_TIME,
	NB_PLAYERSCOREMODE,
};

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
	MACRO_INTERFACE("server", 0)
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
		int m_DDNetVersion;
		int m_InfClassVersion;
	};
	
	struct CClientSession
	{
		int m_RoundId;
		int m_Class;
		int m_MuteTick;
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
	
	virtual ~IServer() {};
	
	inline class CLocalization* Localization() { return m_pLocalization; }

	int Tick() const { return m_CurrentGameTick; }
	int TickSpeed() const { return m_TickSpeed; }

	virtual int Port() const = 0;
	virtual int MaxClients() const = 0;
	virtual int ClientCount() const = 0;
	virtual int DistinctClientCount() const = 0;
	virtual const char *ClientName(int ClientID) const = 0;
	virtual const char *ClientClan(int ClientID) const = 0;
	virtual int ClientCountry(int ClientID) const = 0;
	virtual bool ClientIngame(int ClientID) const = 0;
	virtual int GetClientInfo(int ClientID, CClientInfo *pInfo) const = 0;
	virtual void SetClientDDNetVersion(int ClientID, int DDNetVersion) = 0;
	virtual void GetClientAddr(int ClientID, char *pAddrStr, int Size) const = 0;

	virtual std::string GetClientIP(int ClientID) const = 0;

	/**
	 * Returns the version of the client with the given client ID.
	 *
	 * @param ClientID the client ID, which must be between 0 and
	 * MAX_CLIENTS - 1, or equal to SERVER_DEMO_CLIENT for server demos.
	 *
	 * @return The version of the client with the given client ID.
	 * For server demos this is always the latest client version.
	 * On errors, VERSION_NONE is returned.
	 */
	virtual int GetClientVersion(int ClientID) const = 0;
	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID) = 0;

	template<class T>
	inline int SendPackMsg(const T *pMsg, int Flags, int ClientID)
	{
		int Result = 0;
		T tmp;
		if (ClientID == -1)
		{
			for(int i = 0; i < MaxClients(); i++)
				if(ClientIngame(i))
				{
					Result = SendPackMsgOne(pMsg, Flags, i);
				}
		}
		else
			Result = SendPackMsgOne(pMsg, Flags, ClientID);

		return Result;
	}

	template<class T>
	int SendPackMsgTranslate(T *pMsg, int Flags, int ClientID)
	{
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	int SendPackMsgTranslate(const CNetMsg_Sv_Emoticon *pMsg, int Flags, int ClientID)
	{
		CNetMsg_Sv_Emoticon MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));
		return Translate(MsgCopy.m_ClientID, ClientID) && SendPackMsgOne(&MsgCopy, Flags, ClientID);
	}

	int SendPackMsgTranslate(const CNetMsg_Sv_Chat *pMsg, int Flags, int ClientID)
	{
		CNetMsg_Sv_Chat MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));

		char aBuf[1000];
		if(MsgCopy.m_ClientID >= 0 && !Translate(MsgCopy.m_ClientID, ClientID))
		{
			str_format(aBuf, sizeof(aBuf), "%s: %s", ClientName(MsgCopy.m_ClientID), MsgCopy.m_pMessage);
			MsgCopy.m_pMessage = aBuf;
			MsgCopy.m_ClientID = VANILLA_MAX_CLIENTS - 1;
		}

		return SendPackMsgOne(&MsgCopy, Flags, ClientID);
	}

	int SendPackMsgTranslate(const CNetMsg_Sv_KillMsg *pMsg, int Flags, int ClientID)
	{
		CNetMsg_Sv_KillMsg MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));
		if(!Translate(MsgCopy.m_Victim, ClientID))
			return 0;
		if(!Translate(MsgCopy.m_Killer, ClientID))
			MsgCopy.m_Killer = MsgCopy.m_Victim;
		return SendPackMsgOne(&MsgCopy, Flags, ClientID);
	}

	template<class T>
	int SendPackMsgOne(T *pMsg, int Flags, int ClientID)
	{
		CMsgPacker Packer(pMsg->MsgID(), false);
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags, ClientID);
	}

	bool Translate(int &Target, int Client)
	{
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
		if(GetClientVersion(Client) >= VERSION_DDNET_OLD)
			return true;
		Target = clamp(Target, 0, VANILLA_MAX_CLIENTS - 1);
		int *pMap = GetIdMap(Client);
		if(pMap[Target] == -1)
			return false;
		Target = pMap[Target];
		return true;
	}

	virtual int NewBot(int ClientID) = 0;
	virtual int DelBot(int ClientID) = 0;

	virtual bool WouldClientNameChange(int ClientID, const char *pNameRequest) = 0;
	virtual void SetClientName(int ClientID, char const *pName) = 0;
	virtual void SetClientClan(int ClientID, char const *pClan) = 0;
	virtual void SetClientCountry(int ClientID, int Country) = 0;

	virtual int SnapNewID() = 0;
	virtual void SnapFreeID(int ID) = 0;
	virtual void *SnapNewItem(int Type, int ID, int Size) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	enum
	{
		RCON_CID_SERV=-1,
		RCON_CID_VOTE=-2,
	};
	virtual void SetRconCID(int ClientID) = 0;
	virtual int GetAuthedState(int ClientID) const = 0;
	virtual void Kick(int ClientID, const char *pReason) = 0;
	virtual bool GetMapReload() const = 0;
	virtual void ChangeMap(const char *pMap) = 0;

	virtual void DemoRecorder_HandleAutoStart() = 0;
	virtual bool DemoRecorder_IsRecording() = 0;

	virtual void GetClientAddr(int ClientID, NETADDR *pAddr) const = 0;

/* INFECTION MODIFICATION START ***************************************/
	virtual int GetClientInfclassVersion(int ClientID) const = 0;

	virtual int GetClientNbRound(int ClientID) = 0;
	
	virtual int GetClientDefaultScoreMode(int ClientID) = 0;
	virtual void SetClientDefaultScoreMode(int ClientID, int Value) = 0;
	
	virtual const char* GetClientLanguage(int ClientID) = 0;
	virtual void SetClientLanguage(int ClientID, const char* pLanguage) = 0;

	virtual int GetFireDelay(INFWEAPON WID) = 0;
	virtual void SetFireDelay(INFWEAPON WID, int Time) = 0;

	virtual int GetAmmoRegenTime(INFWEAPON WID) = 0;
	virtual void SetAmmoRegenTime(INFWEAPON WID, int Time) = 0;

	virtual int GetMaxAmmo(INFWEAPON WID) = 0;
	virtual void SetMaxAmmo(INFWEAPON WID, int n) = 0;

	virtual void SetPlayerClassEnabled(int PlayerClass, bool Enabled) = 0;

	virtual void SetPlayerClassProbability(int PlayerClass, int Probability) = 0;
	
	virtual bool IsClientLogged(int ClientID) = 0;
#ifdef CONF_SQL
	virtual void Login(int ClientID, const char* pUsername, const char* pPassword) = 0;
	virtual void Logout(int ClientID) = 0;
	virtual void SetEmail(int ClientID, const char* pEmail) = 0;
	virtual void Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail) = 0;
	virtual void ShowTop10(int ClientID, int ScoreType) = 0;
	virtual void ShowChallenge(int ClientID) = 0;
	virtual void ShowRank(int ClientID, int ScoreType) = 0;
	virtual void ShowGoal(int ClientID, int ScoreType) = 0;
	virtual void ShowStats(int ClientID, int UserId) = 0;
	virtual int GetUserLevel(int ClientID) = 0;
#else
	virtual void Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail) = 0;
	virtual void Login(int ClientID, const char* pUsername, const char* pPassword) = 0;
	virtual void Logout(int ClientID) = 0;
#endif
	virtual void Ban(int i, int Seconds, const char* pReason) = 0;

public:
	virtual class CRoundStatistics* RoundStatistics() = 0;
	virtual void ResetStatistics() = 0;
	virtual void SendStatistics() = 0;

	virtual void OnRoundIsOver() = 0;
	
	virtual void SetClientMemory(int ClientID, int Memory, bool Value = true) = 0;
	virtual void ResetClientMemoryAboutGame(int ClientID) = 0;
	virtual bool GetClientMemory(int ClientID, int Memory) = 0;
	virtual IServer::CClientSession* GetClientSession(int ClientID) = 0;
	virtual void AddAccusation(int From, int To, const char* pReason) = 0;
	virtual bool ClientShouldBeBanned(int ClientID) = 0;
	virtual void RemoveAccusations(int ClientID) = 0;
	virtual void AddMapVote(int From, const char* pCommand, const char* pReason, const char* pDesc) = 0;
	virtual void RemoveMapVotesForID(int ClientID) = 0;
	virtual void ResetMapVotes() = 0;
	virtual CMapVote* GetMapVote() = 0;
	
	virtual int GetTimeShiftUnit() const = 0; //In ms
/* INFECTION MODIFICATION END *****************************************/

	virtual const char *GetPreviousMapName() const = 0;
	virtual int* GetIdMap(int ClientID) = 0;

	virtual bool ClientPrevIngame(int ClientID) = 0;

	virtual int GetActivePlayerCount() = 0;

	virtual const char *GetMapName() const = 0;
};

class IGameServer : public IInterface
{
	MACRO_INTERFACE("gameserver", 0)
protected:
public:
	virtual void OnInit() = 0;
	virtual void OnConsoleInit() = 0;
	virtual void OnShutdown() = 0;

	virtual void OnTick() = 0;
	virtual void OnPreSnap() = 0;
	virtual void OnSnap(int ClientID) = 0;
	virtual void OnPostSnap() = 0;

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID) = 0;

	// Called before map reload, for any data that the game wants to
	// persist to the next map.
	//
	// Has the size of the return value of `PersistentClientDataSize()`.
	//
	// Returns whether the game should be supplied with the data when the
	// client connects for the next map.
	virtual bool OnClientDataPersist(int ClientID, void *pData) = 0;

	// Called when a client connects.
	//
	// If it is reconnecting to the game after a map change, the
	// `pPersistentData` point is nonnull and contains the data the game
	// previously stored.
	virtual void OnClientConnected(int ClientID, void *pPersistentData) = 0;

	virtual void OnClientEnter(int ClientID) = 0;
	virtual void OnClientDrop(int ClientID, int Type, const char *pReason) = 0;
	virtual void OnClientDirectInput(int ClientID, void *pInput) = 0;
	virtual void OnClientPredictedInput(int ClientID, void *pInput) = 0;

	virtual bool IsClientBot(int ClientID) const = 0;
	virtual bool IsClientReady(int ClientID) const = 0;
	virtual bool IsClientPlayer(int ClientID) const = 0;

	virtual int PersistentClientDataSize() const = 0;

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
	
	virtual void OnSetAuthed(int ClientID, int Level) = 0;
	virtual bool PlayerExists(int ClientID) const = 0;

	/* INFECTION MODIFICATION END *****************************************/
};

extern IGameServer *CreateGameServer();
#endif
