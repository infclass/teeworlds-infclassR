/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "server.h"

#include <base/logger.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/map.h>
#include <engine/server.h>
#include <engine/storage.h>

#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/shared/masterserver.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol7.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/snapshot.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/version.h>

#include <engine/shared/linereader.h>

#include "server.h"

#include <cstring>
/* INFECTION MODIFICATION START ***************************************/
#include <engine/server/mapconverter.h>
#include <engine/server/crypt.h>
#include <game/server/infclass/events-director.h>

#include <teeuniverses/components/localization.h>
/* INFECTION MODIFICATION END *****************************************/

#include "databases/connection.h"
#include "databases/connection_pool.h"
#include "register.h"

#include <cinttypes>

extern const char *GIT_SHORTREV_HASH;

#ifdef CONF_SQL
static inline int ChallengeTypeToScoreType(int ChallengeType)
{
	switch(ChallengeType)
	{
		case 0:
			return SQL_SCORETYPE_ENGINEER_SCORE;
		case 1:
			return SQL_SCORETYPE_MERCENARY_SCORE;
		case 2:
			return SQL_SCORETYPE_SCIENTIST_SCORE;
		case 3:
			return SQL_SCORETYPE_NINJA_SCORE;
		case 4:
			return SQL_SCORETYPE_SOLDIER_SCORE;
		case 5:
			return SQL_SCORETYPE_SNIPER_SCORE;
		case 6:
			return SQL_SCORETYPE_MEDIC_SCORE;
		case 7:
			return SQL_SCORETYPE_HERO_SCORE;
		case 8:
			return SQL_SCORETYPE_BIOLOGIST_SCORE;
		case 9:
			return SQL_SCORETYPE_LOOPER_SCORE;
	}
	
	return SQL_SCORETYPE_ROUND_SCORE;
}
#endif

CSnapIdPool::CSnapIdPool()
{
	Reset();
}

void CSnapIdPool::Reset()
{
	for(int i = 0; i < MAX_IDS; i++)
	{
		m_aIds[i].m_Next = i + 1;
		m_aIds[i].m_State = ID_FREE;
	}

	m_aIds[MAX_IDS - 1].m_Next = -1;
	m_FirstFree = 0;
	m_FirstTimed = -1;
	m_LastTimed = -1;
	m_Usage = 0;
	m_InUsage = 0;
}

void CSnapIdPool::RemoveFirstTimeout()
{
	int NextTimed = m_aIds[m_FirstTimed].m_Next;

	// add it to the free list
	m_aIds[m_FirstTimed].m_Next = m_FirstFree;
	m_aIds[m_FirstTimed].m_State = ID_FREE;
	m_FirstFree = m_FirstTimed;

	// remove it from the timed list
	m_FirstTimed = NextTimed;
	if(m_FirstTimed == -1)
		m_LastTimed = -1;

	m_Usage--;
}

int CSnapIdPool::NewId()
{
	int64_t Now = time_get();

	// process timed ids
	while(m_FirstTimed != -1 && m_aIds[m_FirstTimed].m_Timeout < Now)
		RemoveFirstTimeout();

	int Id = m_FirstFree;
	if(Id == -1)
	{
		dbg_msg("server", "invalid id");
		return Id;
	}
	m_FirstFree = m_aIds[m_FirstFree].m_Next;
	m_aIds[Id].m_State = ID_ALLOCATED;
	m_Usage++;
	m_InUsage++;
	return Id;
}

void CSnapIdPool::TimeoutIds()
{
	// process timed ids
	while(m_FirstTimed != -1)
		RemoveFirstTimeout();
}

void CSnapIdPool::FreeId(int Id)
{
	if(Id < 0)
		return;
	dbg_assert((size_t)Id < std::size(m_aIds), "id is out of range");
	dbg_assert(m_aIds[Id].m_State == ID_ALLOCATED, "id is not allocated");

	m_InUsage--;
	m_aIds[Id].m_State = ID_TIMED;
	m_aIds[Id].m_Timeout = time_get() + time_freq() * 10;
	m_aIds[Id].m_Next = -1;

	if(m_LastTimed != -1)
	{
		m_aIds[m_LastTimed].m_Next = Id;
		m_LastTimed = Id;
	}
	else
	{
		m_FirstTimed = Id;
		m_LastTimed = Id;
	}
}

void CServerBan::InitServerBan(IConsole *pConsole, IStorage *pStorage, CServer *pServer)
{
	CNetBan::Init(pConsole, pStorage);

	m_pServer = pServer;

	// overwrites base command, todo: improve this
	Console()->Register("ban", "s[ip|id] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_STORE, ConBanExt, this, "Ban player with ip/client id for x minutes for any reason");
	Console()->Register("ban_region", "s[region] s[ip|id] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_STORE, ConBanRegion, this, "Ban player in a region");
	Console()->Register("ban_region_range", "s[region] s[first ip] s[last ip] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_STORE, ConBanRegionRange, this, "Ban range in a region");
}

template<class T>
int CServerBan::BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason)
{
	// validate address
	if(Server()->m_RconClientId >= 0 && Server()->m_RconClientId < MAX_CLIENTS &&
		Server()->m_aClients[Server()->m_RconClientId].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(NetMatch(pData, Server()->m_NetServer.ClientAddr(Server()->m_RconClientId)))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (you can't ban yourself)");
			return -1;
		}

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(i == Server()->m_RconClientId || Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_IsBot)
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (unable to ban a server-side bot)");
				return -1;
			}

			if(Server()->m_aClients[i].m_Authed >= Server()->m_RconAuthLevel && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}
	else if(Server()->m_RconClientId == IServer::RCON_CID_VOTE)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed != AUTHED_NO && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}

	int Result = Ban(pBanPool, pData, Seconds, pReason);
	if(Result != 0)
		return Result;

	// drop banned clients
	typename T::CDataType Data = *pData;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
			continue;

		if(NetMatch(&Data, Server()->m_NetServer.ClientAddr(i)))
		{
			CNetHash NetHash(&Data);
			char aBuf[256];
			MakeBanInfo(pBanPool->Find(&Data, &NetHash), aBuf, sizeof(aBuf), MSGTYPE_PLAYER);
			Server()->m_NetServer.Drop(i, EClientDropType::Ban, aBuf);
		}
	}

	return Result;
}

int CServerBan::BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason)
{
	return BanExt(&m_BanAddrPool, pAddr, Seconds, pReason);
}

int CServerBan::BanRange(const CNetRange *pRange, int Seconds, const char *pReason)
{
	if(pRange->IsValid())
		return BanExt(&m_BanRangePool, pRange, Seconds, pReason);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

void CServerBan::ConBanExt(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pThis = static_cast<CServerBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments() > 1 ? clamp(pResult->GetInteger(1), 0, 525600) : 10;
	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "Follow the server rules. Type /rules into the chat.";

	if(str_isallnum(pStr))
	{
		int ClientId = str_toint(pStr);
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || pThis->Server()->m_aClients[ClientId].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
			pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientId), Minutes * 60, pReason);
	}
	else
		ConBan(pResult, pUser);
}

void CServerBan::ConBanRegion(IConsole::IResult *pResult, void *pUser)
{
	const char *pRegion = pResult->GetString(0);
	if(str_comp_nocase(pRegion, g_Config.m_SvRegionName))
		return;

	pResult->RemoveArgument(0);
	ConBanExt(pResult, pUser);
}

void CServerBan::ConBanRegionRange(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pServerBan = static_cast<CServerBan *>(pUser);

	const char *pRegion = pResult->GetString(0);
	if(str_comp_nocase(pRegion, g_Config.m_SvRegionName))
		return;

	pResult->RemoveArgument(0);
	ConBanRange(pResult, static_cast<CNetBan *>(pServerBan));
}

// Not thread-safe!
class CRconClientLogger : public ILogger
{
	CServer *m_pServer;
	int m_ClientId;

public:
	CRconClientLogger(CServer *pServer, int ClientId) :
		m_pServer(pServer),
		m_ClientId(ClientId)
	{
	}
	void Log(const CLogMessage *pMessage) override;
};

void CRconClientLogger::Log(const CLogMessage *pMessage)
{
	if(m_Filter.Filters(pMessage))
	{
		return;
	}
	m_pServer->SendRconLogLine(m_ClientId, pMessage);
}

void CServer::CClient::Reset(bool ResetScore)
{
	// reset input
	for(auto &Input : m_aInputs)
		Input.m_GameTick = -1;
	m_CurrentInput = 0;
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));

	m_Snapshots.PurgeAll();
	m_LastAckedSnapshot = -1;
	m_LastInputTick = -1;
	m_Quitting = false;
	m_IsBot = false;
	m_SnapRate = CClient::SNAPRATE_INIT;
	m_NextMapChunk = 0;
	m_Flags = 0;
	m_RedirectDropTime = 0;
	
	if(ResetScore)
	{
		m_NbRound = 0;
		m_WaitingTime = 0;

		m_UserId = -1;
#ifdef CONF_SQL
		m_UserLevel = SQL_USERLEVEL_NORMAL;
#endif
		m_LogInstance = -1;

		str_copy(m_aLanguage, "en", sizeof(m_aLanguage));

		mem_zero(m_Memory, sizeof(m_Memory));
		
		m_Session.m_RoundId = -1;
		m_Session.m_Class = 0;
		m_Session.m_MuteTick = 0;
		m_Session.m_LastInfectionTime = 0;
		
		m_Accusation.m_Num = 0;
	}
}
/* INFECTION MODIFICATION END *****************************************/

CServer::CServer()
{
	m_pConfig = &g_Config;
	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aDemoRecorder[i] = CDemoRecorder(&m_SnapshotDelta, true);
	m_aDemoRecorder[MAX_CLIENTS] = CDemoRecorder(&m_SnapshotDelta, false);

	m_TickSpeed = SERVER_TICK_SPEED;

	m_pGameServer = 0;

	m_CurrentGameTick = MIN_TICK;
	m_RunServer = UNINITIALIZED;

	m_aShutdownReason[0] = 0;

	for(int i = 0; i < NUM_MAP_TYPES; i++)
	{
		m_apCurrentMapData[i] = 0;
		m_aCurrentMapSize[i] = 0;
	}

	m_MapReload = false;
	m_ReloadedWhenEmpty = false;
	m_aCurrentMap[0] = '\0';

	m_RconClientId = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;

	m_ServerInfoFirstRequest = 0;
	m_ServerInfoNumRequests = 0;
	m_ServerInfoNeedsUpdate = false;

#ifdef CONF_FAMILY_UNIX
	m_ConnLoggingSocketCreated = false;
#endif

	m_pConnectionPool = new CDbConnectionPool();
	m_pRegister = nullptr;

	m_aErrorShutdownReason[0] = 0;

	Init();
}

CServer::~CServer()
{
	for(auto &pCurrentMapData : m_apCurrentMapData)
	{
		free(pCurrentMapData);
	}

	if(m_RunServer != UNINITIALIZED)
	{
		for(auto &Client : m_aClients)
		{
			free(Client.m_pPersistentData);
		}
	}
	free(m_pPersistentData);

	delete m_pRegister;
	delete m_pConnectionPool;
}

bool CServer::IsClientNameAvailable(int ClientId, const char *pNameRequest)
{
	// check for empty names
	if(!pNameRequest[0])
		return false;

	// check for names starting with /, as they can be abused to make people
	// write chat commands
	if(pNameRequest[0] == '/')
		return false;

	// make sure that two clients don't have the same name
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i != ClientId && m_aClients[i].m_State >= CClient::STATE_READY)
		{
			if(str_utf8_comp_confusable(pNameRequest, m_aClients[i].m_aName) == 0)
				return false;
		}
	}

	return true;
}

bool CServer::SetClientNameImpl(int ClientId, const char *pNameRequest, bool Set)
{
	dbg_assert(0 <= ClientId && ClientId < MAX_CLIENTS, "invalid client id");
	if(m_aClients[ClientId].m_State < CClient::STATE_READY)
		return false;

	CNameBan *pBanned = IsNameBanned(pNameRequest, m_vNameBans);
	if(pBanned)
	{
		if(m_aClients[ClientId].m_State == CClient::STATE_READY && Set)
		{
			char aBuf[256];
			if(pBanned->m_aReason[0])
			{
				str_format(aBuf, sizeof(aBuf), "Kicked (your name is banned: %s)", pBanned->m_aReason);
			}
			else
			{
				str_copy(aBuf, "Kicked (your name is banned)");
			}
			// Kick(ClientId, aBuf);

			char aAddrStr[NETADDR_MAXSTRSIZE];
			net_addr_str(m_NetServer.ClientAddr(ClientId), aAddrStr, sizeof(aAddrStr), false);
			str_format(aBuf, sizeof(aBuf), "client ip=%s banned for using name '%s'", aAddrStr, pNameRequest);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
			Ban(ClientId, -1, "");
		}
		return false;
	}

	// trim the name
	char aTrimmedName[MAX_NAME_LENGTH];
	str_copy(aTrimmedName, str_utf8_skip_whitespaces(pNameRequest));
	str_utf8_trim_right(aTrimmedName);

	char aNameTry[MAX_NAME_LENGTH];
	str_copy(aNameTry, aTrimmedName);

	if(!IsClientNameAvailable(ClientId, aNameTry))
	{
		// auto rename
		for(int i = 1;; i++)
		{
			str_format(aNameTry, sizeof(aNameTry), "(%d)%s", i, aTrimmedName);
			if(IsClientNameAvailable(ClientId, aNameTry))
				break;
		}
	}

	bool Changed = str_comp(m_aClients[ClientId].m_aName, aNameTry) != 0;

	if(Set)
	{
		// set the client name
		str_copy(m_aClients[ClientId].m_aName, aNameTry);
	}

	return Changed;
}

bool CServer::WouldClientNameChange(int ClientId, const char *pNameRequest)
{
	return SetClientNameImpl(ClientId, pNameRequest, false);
}

void CServer::SetClientName(int ClientId, const char *pName)
{
	SetClientNameImpl(ClientId, pName, true);
}

void CServer::SetClientClan(int ClientId, const char *pClan)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_aClients[ClientId].m_State < CClient::STATE_READY || !pClan)
		return;

	str_copy(m_aClients[ClientId].m_aClan, pClan);
}

void CServer::SetClientCountry(int ClientId, int Country)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_aClients[ClientId].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientId].m_Country = Country;
}

void CServer::SetClientScore(int ClientId, std::optional<int> Score)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_aClients[ClientId].m_State < CClient::STATE_READY)
		return;

	// if(m_aClients[ClientId].m_Score != Score)
	// 	ExpireServerInfo();
	//
	// m_aClients[ClientId].m_Score = Score;
}

void CServer::SetClientFlags(int ClientId, int Flags)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_aClients[ClientId].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientId].m_Flags = Flags;
}

void CServer::Kick(int ClientId, const char *pReason)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_aClients[ClientId].m_State == CClient::STATE_EMPTY)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid client id to kick");
		return;
	}
	else if(m_RconClientId == ClientId)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "you can't kick yourself");
		return;
	}
	else if(m_aClients[ClientId].m_Authed > m_RconAuthLevel)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
 		return;
	}
	else if(m_aClients[ClientId].m_IsBot)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
		return;
	}

	m_NetServer.Drop(ClientId, EClientDropType::Kick, pReason);
}

/*int CServer::Tick()
{
	return m_CurrentGameTick;
}*/

void CServer::RedirectClient(int ClientId, int Port, bool Verbose)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	char aBuf[512];
	bool SupportsRedirect = GetClientVersion(ClientId) >= VERSION_DDNET_REDIRECT;
	if(Verbose)
	{
		str_format(aBuf, sizeof(aBuf), "redirecting '%s' to port %d supported=%d", ClientName(ClientId), Port, SupportsRedirect);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "redirect", aBuf);
	}

	if(!SupportsRedirect)
	{
		bool SamePort = Port == m_NetServer.Address().port;
		str_format(aBuf, sizeof(aBuf), "Redirect unsupported: please connect to port %d", Port);
		Kick(ClientId, SamePort ? "Redirect unsupported: please reconnect" : aBuf);
		return;
	}

	CMsgPacker Msg(NETMSG_REDIRECT, true);
	Msg.AddInt(Port);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientId);

	m_aClients[ClientId].m_RedirectDropTime = time_get() + time_freq() * 10;
	m_aClients[ClientId].m_State = CClient::STATE_REDIRECTED;
}

int64_t CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq() * Tick) / SERVER_TICK_SPEED;
}

int CServer::Init()
{
	for(auto &Client : m_aClients)
	{
		Client.m_State = CClient::STATE_EMPTY;
		Client.m_aName[0] = 0;
		Client.m_aClan[0] = 0;
		Client.m_Country = -1;
		Client.m_Snapshots.Init();
		Client.m_Traffic = 0;
		Client.m_TrafficSince = 0;
		Client.m_IsBot = false;
		Client.m_WaitingTime = 0;
		Client.m_Accusation.m_Num = 0;
		Client.m_ShowIps = false;
		Client.m_Latency = 0;
		Client.m_InfClassVersion = 0;
		Client.m_Sixup = false;
		Client.m_RedirectDropTime = 0;
	}

	m_CurrentGameTick = MIN_TICK;
	m_MapVotesCounter = 0;

	mem_zero(m_aPrevStates, sizeof(m_aPrevStates));

#ifdef CONF_SQL
	m_ChallengeType = 0;
	m_ChallengeRefreshTick = 0;
	m_aChallengeWinner[0] = 0;
#endif
		
/* INFECTION MODIFICATION START ***************************************/
	m_aPreviousMap[0] = 0;
	m_aCurrentMap[0] = 0;
	/* INFECTION MODIFICATION END *****************************************/

	return 0;
}

void CServer::SendLogLine(const CLogMessage *pMessage)
{
	if(pMessage->m_Level <= IConsole::ToLogLevel(g_Config.m_ConsoleOutputLevel))
	{
		SendRconLogLine(-1, pMessage);
	}
	if(pMessage->m_Level <= IConsole::ToLogLevel(g_Config.m_EcOutputLevel))
	{
		m_Econ.Send(-1, pMessage->m_aLine);
	}
}

void CServer::SetRconCid(int ClientId)
{
	m_RconClientId = ClientId;
}

int CServer::GetAuthedState(int ClientId) const
{
	return m_aClients[ClientId].m_Authed;
}

bool CServer::GetClientInfo(int ClientId, CClientInfo *pInfo) const
{
	dbg_assert(ClientId >= 0 && ClientId < MAX_CLIENTS, "ClientId is not valid");
	dbg_assert(pInfo != nullptr, "pInfo cannot be null");

	if(m_aClients[ClientId].m_State == CClient::STATE_INGAME)
	{
		pInfo->m_pName = ClientName(ClientId);
		pInfo->m_Latency = m_aClients[ClientId].m_Latency;
		pInfo->m_GotDDNetVersion = m_aClients[ClientId].m_DDNetVersionSettled;
		pInfo->m_DDNetVersion = m_aClients[ClientId].m_DDNetVersion >= 0 ? m_aClients[ClientId].m_DDNetVersion : VERSION_VANILLA;
		pInfo->m_InfClassVersion = m_aClients[ClientId].m_InfClassVersion;
		if(m_aClients[ClientId].m_GotDDNetVersionPacket)
		{
			pInfo->m_pConnectionId = &m_aClients[ClientId].m_ConnectionId;
			pInfo->m_pDDNetVersionStr = m_aClients[ClientId].m_aDDNetVersionStr;
		}
		else
		{
			pInfo->m_pConnectionId = nullptr;
			pInfo->m_pDDNetVersionStr = nullptr;
		}
		return true;
	}
	return false;
}

void CServer::SetClientDDNetVersion(int ClientId, int DDNetVersion)
{
	dbg_assert(ClientId >= 0 && ClientId < MAX_CLIENTS, "ClientId is not valid");

	if(m_aClients[ClientId].m_State == CClient::STATE_INGAME)
	{
		m_aClients[ClientId].m_DDNetVersion = DDNetVersion;
		m_aClients[ClientId].m_DDNetVersionSettled = true;
	}
}

void CServer::GetClientAddr(int ClientId, char *pAddrStr, int Size) const
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && m_aClients[ClientId].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientId), pAddrStr, Size, false);
}

const char *CServer::ClientName(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_aClients[ClientId].m_State == CServer::CClient::STATE_EMPTY)
		return "(invalid)";
		
	if(m_aClients[ClientId].m_State == CServer::CClient::STATE_INGAME)
	{
		if(m_aClients[ClientId].m_UserId >= 0)
			return m_aClients[ClientId].m_aUsername;
		else
			return m_aClients[ClientId].m_aName;
	}
	else
		return "(connecting)";

}

const char *CServer::ClientClan(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_aClients[ClientId].m_State == CServer::CClient::STATE_EMPTY)
		return "";
	if(m_aClients[ClientId].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientId].m_aClan;
	else
		return "";
}

int CServer::ClientCountry(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || m_aClients[ClientId].m_State == CServer::CClient::STATE_EMPTY)
		return -1;
	if(m_aClients[ClientId].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientId].m_Country;
	else
		return -1;
}

bool CServer::ClientIngame(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aClients[ClientId].m_State == CServer::CClient::STATE_INGAME;
}

bool CServer::ClientIsBot(int ClientId) const
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS && m_aClients[ClientId].m_IsBot;
}

int CServer::Port() const
{
	return m_NetServer.Address().port;
}

int CServer::MaxClients() const
{
	return m_RunServer == UNINITIALIZED ? 0 : m_NetServer.MaxClients();
}

int CServer::ClientCount() const
{
	int ClientCount = 0;
	for(const auto &Client : m_aClients)
	{
		if(Client.m_State != CClient::STATE_EMPTY)
		{
			ClientCount++;
		}
	}

	return ClientCount;
}

int CServer::DistinctClientCount() const
{
	NETADDR aAddresses[MAX_CLIENTS];
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			GetClientAddr(i, &aAddresses[i]);
		}
	}

	int ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// connecting clients with spoofed ips can clog slots without being ingame
		if(ClientIngame(i))
		{
			ClientCount++;
			for(int j = 0; j < i; j++)
			{
				if(!net_addr_comp_noport(&aAddresses[i], &aAddresses[j]))
				{
					ClientCount--;
					break;
				}
			}
		}
	}

	return ClientCount;
}

int CServer::GetClientVersion(int ClientId) const
{
	// Assume latest client version for server demos
	if(ClientId == SERVER_DEMO_CLIENT)
		return CLIENT_VERSIONNR;

	CClientInfo Info;
	if(GetClientInfo(ClientId, &Info))
		return Info.m_DDNetVersion;
	return VERSION_NONE;
}

static inline bool RepackMsg(const CMsgPacker *pMsg, CPacker &Packer, bool Sixup)
{
	int MsgId = pMsg->m_MsgId;
	Packer.Reset();

	if(Sixup && !pMsg->m_NoTranslate)
	{
		if(pMsg->m_System)
		{
			if(MsgId >= OFFSET_UUID)
				;
			else if(MsgId >= NETMSG_MAP_CHANGE && MsgId <= NETMSG_MAP_DATA)
				;
			else if(MsgId >= NETMSG_CON_READY && MsgId <= NETMSG_INPUTTIMING)
				MsgId += 1;
			else if(MsgId == NETMSG_RCON_LINE)
				MsgId = protocol7::NETMSG_RCON_LINE;
			else if(MsgId >= NETMSG_PING && MsgId <= NETMSG_PING_REPLY)
				MsgId += 4;
			else if(MsgId >= NETMSG_RCON_CMD_ADD && MsgId <= NETMSG_RCON_CMD_REM)
				MsgId -= 11;
			else
			{
				dbg_msg("net", "DROP send sys %d", MsgId);
				return true;
			}
		}
		else
		{
			if(MsgId >= 0 && MsgId < OFFSET_UUID)
				MsgId = Msg_SixToSeven(MsgId);

			if(MsgId < 0)
				return true;
		}
	}

	if(MsgId < OFFSET_UUID)
	{
		Packer.AddInt((MsgId << 1) | (pMsg->m_System ? 1 : 0));
	}
	else
	{
		Packer.AddInt(pMsg->m_System ? 1 : 0); // NETMSG_EX, NETMSGTYPE_EX
		g_UuidManager.PackUuid(MsgId, &Packer);
	}
	Packer.AddRaw(pMsg->Data(), pMsg->Size());

	return false;
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientId)
{
	// drop packet to dummy client
	if(ClientIsBot(ClientId))
		return 0;

	CNetChunk Packet;
	mem_zero(&Packet, sizeof(CNetChunk));
	if(Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if(ClientId < 0)
	{
		CPacker Pack6, Pack7;
		if(RepackMsg(pMsg, Pack6, false))
			return -1;
		if(RepackMsg(pMsg, Pack7, true))
			return -1;

		// write message to demo recorders
		if(!(Flags & MSGFLAG_NORECORD))
		{
			for(auto &Recorder : m_aDemoRecorder)
				if(Recorder.IsRecording())
					Recorder.RecordMessage(Pack6.Data(), Pack6.Size());
		}

		if(!(Flags & MSGFLAG_NOSEND))
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if((m_aClients[i].m_State == CClient::STATE_INGAME) && !m_aClients[i].m_IsBot)
				{
					CPacker *pPack = m_aClients[i].m_Sixup ? &Pack7 : &Pack6;
					Packet.m_pData = pPack->Data();
					Packet.m_DataSize = pPack->Size();
					Packet.m_ClientId = i;
					m_NetServer.Send(&Packet);
				}
			}
		}
	}
	else
	{
		CPacker Pack;
		if(RepackMsg(pMsg, Pack, m_aClients[ClientId].m_Sixup))
			return -1;

		Packet.m_ClientId = ClientId;
		Packet.m_pData = Pack.Data();
		Packet.m_DataSize = Pack.Size();

		// write message to demo recorders
		if(!(Flags & MSGFLAG_NORECORD))
		{
			if(m_aDemoRecorder[ClientId].IsRecording())
				m_aDemoRecorder[ClientId].RecordMessage(Pack.Data(), Pack.Size());
			if(m_aDemoRecorder[MAX_CLIENTS].IsRecording())
				m_aDemoRecorder[MAX_CLIENTS].RecordMessage(Pack.Data(), Pack.Size());
		}

		if(!(Flags & MSGFLAG_NOSEND))
			m_NetServer.Send(&Packet);
	}

	return 0;
}

void CServer::SendMsgRaw(int ClientId, const void *pData, int Size, int Flags)
{
	CNetChunk Packet;
	mem_zero(&Packet, sizeof(CNetChunk));
	Packet.m_ClientId = ClientId;
	Packet.m_pData = pData;
	Packet.m_DataSize = Size;
	Packet.m_Flags = 0;
	if(Flags & MSGFLAG_VITAL)
	{
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	}
	if(Flags & MSGFLAG_FLUSH)
	{
		Packet.m_Flags |= NETSENDFLAG_FLUSH;
	}
	m_NetServer.Send(&Packet);
}

void CServer::DoSnapshot()
{
	GameServer()->OnPreSnap();

	// create snapshot for demo recording
	if(m_aDemoRecorder[MAX_CLIENTS].IsRecording())
	{
		char aData[CSnapshot::MAX_SIZE];

		// build snap and possibly add some messages
		m_SnapshotBuilder.Init();
		GameServer()->OnSnap(-1);
		int SnapshotSize = m_SnapshotBuilder.Finish(aData);

		// write snapshot
		m_aDemoRecorder[MAX_CLIENTS].RecordSnapshot(Tick(), aData, SnapshotSize);
	}

	// create snapshots for all clients
	for(int i = 0; i < MaxClients(); i++)
	{
		// client must be ingame to receive snapshots
		if(m_aClients[i].m_State != CClient::STATE_INGAME)
			continue;

		// client must be human to recive snapshots
		if(m_aClients[i].m_IsBot)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_RECOVER && (Tick() % 50) != 0)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_INIT && (Tick() % 10) != 0)
			continue;

		{
			m_SnapshotBuilder.Init(m_aClients[i].m_Sixup);

			GameServer()->OnSnap(i);

			// finish snapshot
			char aData[CSnapshot::MAX_SIZE];
			CSnapshot *pData = (CSnapshot *)aData; // Fix compiler warning for strict-aliasing
			int SnapshotSize = m_SnapshotBuilder.Finish(pData);

			if(m_aDemoRecorder[i].IsRecording())
			{
				// write snapshot
				m_aDemoRecorder[i].RecordSnapshot(Tick(), aData, SnapshotSize);
			}

			int Crc = pData->Crc();

			// remove old snapshots
			// keep 3 seconds worth of snapshots
			m_aClients[i].m_Snapshots.PurgeUntil(m_CurrentGameTick - SERVER_TICK_SPEED * 3);

			// save the snapshot
			m_aClients[i].m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0, nullptr);

			// find snapshot that we can perform delta against
			int DeltaTick = -1;
			const CSnapshot *pDeltashot = CSnapshot::EmptySnapshot();
			{
				int DeltashotSize = m_aClients[i].m_Snapshots.Get(m_aClients[i].m_LastAckedSnapshot, 0, &pDeltashot, 0);
				if(DeltashotSize >= 0)
					DeltaTick = m_aClients[i].m_LastAckedSnapshot;
				else
				{
					// no acked package found, force client to recover rate
					if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_FULL)
						m_aClients[i].m_SnapRate = CClient::SNAPRATE_RECOVER;
				}
			}

			// create delta
			m_SnapshotDelta.SetStaticsize(protocol7::NETEVENTTYPE_SOUNDWORLD, m_aClients[i].m_Sixup);
			m_SnapshotDelta.SetStaticsize(protocol7::NETEVENTTYPE_DAMAGE, m_aClients[i].m_Sixup);
			char aDeltaData[CSnapshot::MAX_SIZE];
			int DeltaSize = m_SnapshotDelta.CreateDelta(pDeltashot, pData, aDeltaData);

			if(DeltaSize)
			{
				// compress it
				const int MaxSize = MAX_SNAPSHOT_PACKSIZE;

				char aCompData[CSnapshot::MAX_SIZE];
				SnapshotSize = CVariableInt::Compress(aDeltaData, DeltaSize, aCompData, sizeof(aCompData));
				int NumPackets = (SnapshotSize + MaxSize - 1) / MaxSize;

				for(int n = 0, Left = SnapshotSize; Left > 0; n++)
				{
					int Chunk = Left < MaxSize ? Left : MaxSize;
					Left -= Chunk;

					if(NumPackets == 1)
					{
						CMsgPacker Msg(NETMSG_SNAPSINGLE, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY, true);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick - DeltaTick);
				SendMsg(&Msg, MSGFLAG_FLUSH, i);
			}
		}
	}

	GameServer()->OnPostSnap();
}

int CServer::ClientRejoinCallback(int ClientId, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	pThis->m_aClients[ClientId].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientId].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientId].m_Quitting = false;

	pThis->m_aClients[ClientId].Reset();
	
	//Getback session about the client
	IServer::CClientSession* pSession = pThis->m_NetSession.GetData(pThis->m_NetServer.ClientAddr(ClientId));
	if(pSession)
	{
		dbg_msg("infclass", "session found for the client %d. Round id = %d, class id = %d", ClientId, pSession->m_RoundId, pSession->m_Class);
		pThis->m_aClients[ClientId].m_Session = *pSession;
		pThis->m_NetSession.RemoveSession(pThis->m_NetServer.ClientAddr(ClientId));
	}
	
	//Getback accusation about the client
	IServer::CClientAccusation* pAccusation = pThis->m_NetAccusation.GetData(pThis->m_NetServer.ClientAddr(ClientId));
	if(pAccusation)
	{
		dbg_msg("infclass", "%d accusation(s) found for the client %d", pAccusation->m_Num, ClientId);
		pThis->m_aClients[ClientId].m_Accusation = *pAccusation;
		pThis->m_NetAccusation.RemoveSession(pThis->m_NetServer.ClientAddr(ClientId));
	}

	pThis->SendMap(ClientId);

	return 0;
}

int CServer::NewBot(int ClientId)
{
	if(m_aClients[ClientId].m_State > CClient::STATE_EMPTY && !m_aClients[ClientId].m_IsBot)
		return 1;
	m_aClients[ClientId].m_State = CClient::STATE_INGAME;
	m_aClients[ClientId].m_Country = -1;
	m_aClients[ClientId].m_UserId = -1;
	m_aClients[ClientId].m_IsBot = true;

	return 0;
}

int CServer::DelBot(int ClientId)
{
	if( !m_aClients[ClientId].m_IsBot )
		return 1;
	m_aClients[ClientId].m_State = CClient::STATE_EMPTY;
	m_aClients[ClientId].m_aName[0] = 0;
	m_aClients[ClientId].m_aClan[0] = 0;
	m_aClients[ClientId].m_Country = -1;
	m_aClients[ClientId].m_UserId = -1;
	m_aClients[ClientId].m_Authed = AUTHED_NO;
	m_aClients[ClientId].m_AuthTries = 0;
	m_aClients[ClientId].m_pRconCmdToSend = 0;
	m_aClients[ClientId].m_IsBot = false;
	m_aClients[ClientId].m_Snapshots.PurgeAll();
	return 0;
}

int CServer::NewClientNoAuthCallback(int ClientId, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	NewClientCallback(ClientId, pUser, false);

	pThis->SendCapabilities(ClientId);
	pThis->SendMap(ClientId);
	return 0;
}

int CServer::NewClientCallback(int ClientId, void *pUser, bool Sixup)
{
	CServer *pThis = (CServer *)pUser;

	// Remove non human player on same slot
	if(pThis->ClientIsBot(ClientId))
	{
		pThis->GameServer()->OnClientDrop(ClientId, EClientDropType::Kick, "removing dummy");
	}

	pThis->m_aClients[ClientId].m_State = CClient::STATE_PREAUTH;
	pThis->m_aClients[ClientId].m_aName[0] = 0;
	pThis->m_aClients[ClientId].m_aClan[0] = 0;
	pThis->m_aClients[ClientId].m_Country = -1;
	pThis->m_aClients[ClientId].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientId].m_AuthTries = 0;
	pThis->m_aClients[ClientId].m_Traffic = 0;
	pThis->m_aClients[ClientId].m_TrafficSince = 0;
	pThis->m_aClients[ClientId].m_ShowIps = false;
	pThis->m_aClients[ClientId].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientId].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientId].m_DDNetVersionSettled = false;
	pThis->m_aClients[ClientId].m_InfClassVersion = 0;
	pThis->m_aClients[ClientId].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientId].m_Quitting = false;
	
	memset(&pThis->m_aClients[ClientId].m_Addr, 0, sizeof(NETADDR));
	pThis->m_aClients[ClientId].Reset();
	
	//Getback session about the client
	IServer::CClientSession* pSession = pThis->m_NetSession.GetData(pThis->m_NetServer.ClientAddr(ClientId));
	if(pSession)
	{
		dbg_msg("infclass", "session found for the client %d. Round id = %d, class id = %d", ClientId, pSession->m_RoundId, pSession->m_Class);
		pThis->m_aClients[ClientId].m_Session = *pSession;
		pThis->m_NetSession.RemoveSession(pThis->m_NetServer.ClientAddr(ClientId));
	}
	
	//Getback accusation about the client
	IServer::CClientAccusation* pAccusation = pThis->m_NetAccusation.GetData(pThis->m_NetServer.ClientAddr(ClientId));
	if(pAccusation)
	{
		dbg_msg("infclass", "%d accusation(s) found for the client %d", pAccusation->m_Num, ClientId);
		pThis->m_aClients[ClientId].m_Accusation = *pAccusation;
		pThis->m_NetAccusation.RemoveSession(pThis->m_NetServer.ClientAddr(ClientId));
	}

	return 0;
}

int CServer::DelClientCallback(int ClientId, EClientDropType Type, const char *pReason, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	
	if(pThis->m_aClients[ClientId].m_Quitting)
		return 0;

	const bool isBot = pThis->m_aClients[ClientId].m_IsBot;
	pThis->m_aClients[ClientId].m_Quitting = true;

	char aAddrStr[NETADDR_MAXSTRSIZE];

	// remove map votes for the dropped client
	pThis->RemoveMapVotesForId(ClientId);

	net_addr_str(pThis->m_NetServer.ClientAddr(ClientId), aAddrStr, sizeof(aAddrStr), true);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "client dropped. cid=%d addr=<{%s}> reason='%s'", ClientId, aAddrStr, pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

	// notify the mod about the drop
	if(pThis->m_aClients[ClientId].m_State >= CClient::STATE_READY && pThis->m_aClients[ClientId].m_WaitingTime <= 0)
		pThis->GameServer()->OnClientDrop(ClientId, Type, pReason);

	pThis->m_aClients[ClientId].m_State = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientId].m_aName[0] = 0;
	pThis->m_aClients[ClientId].m_aClan[0] = 0;
	pThis->m_aClients[ClientId].m_Country = -1;
	pThis->m_aClients[ClientId].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientId].m_AuthTries = 0;
	pThis->m_aClients[ClientId].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientId].m_Traffic = 0;
	pThis->m_aClients[ClientId].m_TrafficSince = 0;
	pThis->m_aClients[ClientId].m_ShowIps = false;
	pThis->m_aPrevStates[ClientId] = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientId].m_Snapshots.PurgeAll();
	pThis->m_aClients[ClientId].m_Sixup = false;
	pThis->m_aClients[ClientId].m_RedirectDropTime = 0;
	pThis->m_aClients[ClientId].m_WaitingTime = 0;
	pThis->m_aClients[ClientId].m_UserId = -1;
#ifdef CONF_SQL
	pThis->m_aClients[ClientId].m_UserLevel = SQL_USERLEVEL_NORMAL;
#endif
	pThis->m_aClients[ClientId].m_LogInstance = -1;
	pThis->m_aClients[ClientId].m_Quitting = false;

	if(isBot)
		return 0;

	//Keep information about client for 10 minutes
	pThis->m_NetSession.AddSession(pThis->m_NetServer.ClientAddr(ClientId), 10*60, &pThis->m_aClients[ClientId].m_Session);
	dbg_msg("infclass", "session created for the client %d", ClientId);
	
	//Keep accusation for 30 minutes
	pThis->m_NetAccusation.AddSession(pThis->m_NetServer.ClientAddr(ClientId), 30*60, &pThis->m_aClients[ClientId].m_Accusation);
	dbg_msg("infclass", "accusation created for the client %d", ClientId);
	
	return 0;
}

void CServer::SendRconType(int ClientId, bool UsernameReq)
{
	CMsgPacker Msg(NETMSG_RCONTYPE, true);
	Msg.AddInt(UsernameReq);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CServer::GetMapInfo(char *pMapName, int MapNameSize, int *pMapSize, SHA256_DIGEST *pMapSha256, int *pMapCrc)
{
	str_copy(pMapName, GetMapName(), MapNameSize);
	*pMapSize = m_aCurrentMapSize[MAP_TYPE_SIX];
	*pMapSha256 = m_aCurrentMapSha256[MAP_TYPE_SIX];
	*pMapCrc = m_aCurrentMapCrc[MAP_TYPE_SIX];
}

void CServer::SendCapabilities(int ClientId)
{
	CMsgPacker Msg(NETMSG_CAPABILITIES, true);
	Msg.AddInt(SERVERCAP_CURVERSION); // version
	Msg.AddInt(SERVERCAPFLAG_CHATTIMEOUTCODE | SERVERCAPFLAG_PINGEX); // flags
	SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CServer::SendMap(int ClientId)
{
	int MapType = IsSixup(ClientId) ? MAP_TYPE_SIXUP : MAP_TYPE_SIX;
	{
		CMsgPacker Msg(NETMSG_MAP_DETAILS, true);
		Msg.AddString(GetMapName(), 0);
		Msg.AddRaw(&m_aCurrentMapSha256[MapType].data, sizeof(m_aCurrentMapSha256[MapType].data));
		Msg.AddInt(m_aCurrentMapCrc[MapType]);
		Msg.AddInt(m_aCurrentMapSize[MapType]);
		Msg.AddString("", 0); // HTTPS map download URL
		SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
	}
	{
		CMsgPacker Msg(NETMSG_MAP_CHANGE, true);
		Msg.AddString(GetMapName(), 0);
		Msg.AddInt(m_aCurrentMapCrc[MapType]);
		Msg.AddInt(m_aCurrentMapSize[MapType]);
		if(MapType == MAP_TYPE_SIXUP)
		{
			Msg.AddInt(Config()->m_SvMapWindow);
			Msg.AddInt(1024 - 128);
			Msg.AddRaw(m_aCurrentMapSha256[MapType].data, sizeof(m_aCurrentMapSha256[MapType].data));
		}
		SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientId);
	}

	m_aClients[ClientId].m_NextMapChunk = 0;
}

void CServer::SendMapData(int ClientId, int Chunk)
{
	int MapType = IsSixup(ClientId) ? MAP_TYPE_SIXUP : MAP_TYPE_SIX;
	unsigned int ChunkSize = 1024 - 128;
	unsigned int Offset = Chunk * ChunkSize;
	int Last = 0;

	// drop faulty map data requests
	if(Chunk < 0 || Offset > m_aCurrentMapSize[MapType])
		return;

	if(Offset + ChunkSize >= m_aCurrentMapSize[MapType])
	{
		ChunkSize = m_aCurrentMapSize[MapType] - Offset;
		Last = 1;
	}

	CMsgPacker Msg(NETMSG_MAP_DATA, true);
	if(MapType == MAP_TYPE_SIX)
	{
		Msg.AddInt(Last);
		Msg.AddInt(m_aCurrentMapCrc[MAP_TYPE_SIX]);
		Msg.AddInt(Chunk);
		Msg.AddInt(ChunkSize);
	}
	Msg.AddRaw(&m_apCurrentMapData[MapType][Offset], ChunkSize);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientId);

	if(Config()->m_Debug)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
	}
}

void CServer::SendConnectionReady(int ClientId)
{
	CMsgPacker Msg(NETMSG_CON_READY, true);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientId);
}

void CServer::SendRconLine(int ClientId, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE, true);
	Msg.AddString(pLine, 512);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CServer::SendRconLogLine(int ClientId, const CLogMessage *pMessage)
{
	const char *pLine = pMessage->m_aLine;
	const char *pStart = str_find(pLine, "<{");
	const char *pEnd = pStart == NULL ? NULL : str_find(pStart + 2, "}>");
	const char *pLineWithoutIps;
	char aLine[512];
	char aLineWithoutIps[512];
	aLine[0] = '\0';
	aLineWithoutIps[0] = '\0';

	if(pStart == NULL || pEnd == NULL)
	{
		pLineWithoutIps = pLine;
	}
	else
	{
		str_append(aLine, pLine, pStart - pLine + 1);
		str_append(aLine, pStart + 2, pStart - pLine + pEnd - pStart - 1);
		str_append(aLine, pEnd + 2);

		str_append(aLineWithoutIps, pLine, pStart - pLine + 1);
		str_append(aLineWithoutIps, "XXX");
		str_append(aLineWithoutIps, pEnd + 2);

		pLine = aLine;
		pLineWithoutIps = aLineWithoutIps;
	}

	if(ClientId == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_aClients[i].m_State != CClient::STATE_EMPTY && m_aClients[i].m_Authed >= AUTHED_ADMIN)
				SendRconLine(i, m_aClients[i].m_ShowIps ? pLine : pLineWithoutIps);
		}
	}
	else
	{
		if(m_aClients[ClientId].m_State != CClient::STATE_EMPTY)
			SendRconLine(ClientId, m_aClients[ClientId].m_ShowIps ? pLine : pLineWithoutIps);
	}
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientId)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD, true);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientId)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM, true);
	Msg.AddString(pCommandInfo->m_pName, 256);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CServer::UpdateClientRconCommands()
{
	for(int ClientId = Tick() % MAX_RCONCMD_RATIO; ClientId < MaxClients(); ClientId += MAX_RCONCMD_RATIO)
	{
		if(m_aClients[ClientId].m_State != CClient::STATE_EMPTY && m_aClients[ClientId].m_Authed)
		{
			int ConsoleAccessLevel = m_aClients[ClientId].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD;
			for(int i = 0; i < MAX_RCONCMD_SEND && m_aClients[ClientId].m_pRconCmdToSend; ++i)
			{
				SendRconCmdAdd(m_aClients[ClientId].m_pRconCmdToSend, ClientId);
				m_aClients[ClientId].m_pRconCmdToSend = m_aClients[ClientId].m_pRconCmdToSend->NextCommandInfo(ConsoleAccessLevel, CFGFLAG_SERVER);
			}
		}
	}
}

static inline int MsgFromSixup(int Msg, bool System)
{
	if(System)
	{
		if(Msg == NETMSG_INFO)
			;
		else if(Msg >= 14 && Msg <= 15)
			Msg += 11;
		else if(Msg >= 18 && Msg <= 28)
			Msg = NETMSG_READY + Msg - 18;
		else if(Msg < OFFSET_UUID)
			return -1;
	}

	return Msg;
}

bool CServer::GenerateClientMap(const char *pMapFilePath, const char *pMapName)
{
	if(!m_pMap->Load(pMapFilePath))
		return 0;

	//The map format of InfectionClass is different from the vanilla format.
	//We need to convert the map to something that the client can use
	//First, try to find if the client map is already generated

	CDataFileReader dfServerMap;
	dfServerMap.Open(Storage(), pMapFilePath, IStorage::TYPE_ALL);
	unsigned ServerMapCrc = dfServerMap.Crc();
	dfServerMap.Close();

	EventsDirector::SetPreloadedMapName(pMapName);

	char aClientMapDir[256];
	char aClientMapName[256];
	const char *pConverterId = Config()->m_InfConverterId;
	pConverterId = EventsDirector::GetMapConverterId(pConverterId);
	str_format(aClientMapDir, sizeof(aClientMapDir), "clientmaps/%s", pConverterId);
	str_format(aClientMapName, sizeof(aClientMapName), "%s/%s_%08x.map", aClientMapDir, pMapName, ServerMapCrc);

	CMapConverter MapConverter(Storage(), m_pMap, Console());
	if(!MapConverter.Load())
		return false;

	m_TimeShiftUnit = MapConverter.GetTimeShiftUnit();

	CDataFileReader dfClientMap;
	//The map is already converted
	if(!Config()->m_InfConverterForceRegeneration && dfClientMap.Open(Storage(), aClientMapName, IStorage::TYPE_ALL))
	{
		m_aCurrentMapCrc[MAP_TYPE_SIX] = dfClientMap.Crc();
		m_aCurrentMapSha256[MAP_TYPE_SIX] = dfClientMap.Sha256();
		dfClientMap.Close();
	}
	//The map must be converted
	else
	{
		char aFullPath[512];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aClientMapDir, aFullPath, sizeof(aFullPath));
		if(fs_makedir_rec_for(aFullPath) != 0 || fs_makedir(aFullPath) != 0)
		{
			dbg_msg("infclass", "Can't create the directory '%s'", aClientMapDir);
		}

		if(!MapConverter.CreateMap(aClientMapName))
			return false;

		CDataFileReader dfGeneratedMap;
		dfGeneratedMap.Open(Storage(), aClientMapName, IStorage::TYPE_ALL);
		m_aCurrentMapCrc[MAP_TYPE_SIX] = dfGeneratedMap.Crc();
		m_aCurrentMapSha256[MAP_TYPE_SIX] = dfGeneratedMap.Sha256();
		dfGeneratedMap.Close();
	}

	char aBufMsg[128];
	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(m_aCurrentMapSha256[MAP_TYPE_SIX], aSha256, sizeof(aSha256));
	str_format(aBufMsg, sizeof(aBufMsg), "%s sha256 is %s", pMapName, aSha256);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);
	str_format(aBufMsg, sizeof(aBufMsg), "map crc is %08x, generated map crc is %08x", ServerMapCrc, m_aCurrentMapCrc[MAP_TYPE_SIX]);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);

	// load complete map into memory for download
	{
		free(m_apCurrentMapData[MAP_TYPE_SIX]);
		void *pData;
		Storage()->ReadFile(aClientMapName, IStorage::TYPE_ALL, &pData, &m_aCurrentMapSize[MAP_TYPE_SIX]);
		m_apCurrentMapData[MAP_TYPE_SIX] = (unsigned char *)pData;
	}

	return true;
}

void CServer::ProcessClientPacket(CNetChunk *pPacket)
{
	int ClientId = pPacket->m_ClientId;
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int Msg;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageId(&Msg, &Sys, &Uuid, &Unpacker, &Packer);
	if(Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}

	if(m_aClients[ClientId].m_Sixup && (Msg = MsgFromSixup(Msg, Sys)) < 0)
	{
		return;
	}

	if(Config()->m_SvNetlimit && Msg != NETMSG_REQUEST_MAP_DATA)
	{
		int64_t Now = time_get();
		int64_t Diff = Now - m_aClients[ClientId].m_TrafficSince;
		double Alpha = Config()->m_SvNetlimitAlpha / 100.0;
		double Limit = (double)(Config()->m_SvNetlimit * 1024) / time_freq();

		if(m_aClients[ClientId].m_Traffic > Limit)
		{
			m_NetServer.NetBan()->BanAddr(&pPacket->m_Address, 600, "Stressing network");
			return;
		}
		if(Diff > 100)
		{
			m_aClients[ClientId].m_Traffic = (Alpha * ((double)pPacket->m_DataSize / Diff)) + (1.0 - Alpha) * m_aClients[ClientId].m_Traffic;
			m_aClients[ClientId].m_TrafficSince = Now;
		}
	}

	if(Result == UNPACKMESSAGE_ANSWER)
	{
		SendMsg(&Packer, MSGFLAG_VITAL, ClientId);
	}

	if(Sys)
	{
		// system message
		if(Msg == NETMSG_CLIENTVER)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientId].m_State == CClient::STATE_PREAUTH)
			{
				CUuid *pConnectionId = (CUuid *)Unpacker.GetRaw(sizeof(*pConnectionId));
				int DDNetVersion = Unpacker.GetInt();
				const char *pDDNetVersionStr = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Unpacker.Error() || !str_utf8_check(pDDNetVersionStr) || DDNetVersion < 0)
				{
					return;
				}
				m_aClients[ClientId].m_ConnectionId = *pConnectionId;
				m_aClients[ClientId].m_DDNetVersion = DDNetVersion;
				str_copy(m_aClients[ClientId].m_aDDNetVersionStr, pDDNetVersionStr);
				m_aClients[ClientId].m_DDNetVersionSettled = true;
				m_aClients[ClientId].m_GotDDNetVersionPacket = true;
				m_aClients[ClientId].m_State = CClient::STATE_AUTH;
			}
		}
		if(Msg == NETMSG_CLIENTVER_INFCLASS)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0) // Ignore STATE_AUTH for now, see ddnet#4445 // && m_aClients[ClientId].m_State == CClient::STATE_AUTH)
			{
				int InfClassVersion = Unpacker.GetInt();
				if(Unpacker.Error() || InfClassVersion < 0)
				{
					return;
				}
				m_aClients[ClientId].m_InfClassVersion = InfClassVersion;
			}
		}
		else if(Msg == NETMSG_INFO)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientId].m_State == CClient::STATE_PREAUTH || m_aClients[ClientId].m_State == CClient::STATE_AUTH))
			{
				const char *pVersion = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(!str_utf8_check(pVersion))
				{
					return;
				}
				if(str_comp(pVersion, GameServer()->NetVersion()) != 0 && str_comp(pVersion, "0.7 802f1be60a05665f") != 0)
				{
					// wrong version
					char aReason[256];
					str_format(aReason, sizeof(aReason), "Wrong version. Server is running '%s' and client '%s'", GameServer()->NetVersion(), pVersion);
					m_NetServer.Drop(ClientId, EClientDropType::WrongVersion, aReason);
					return;
				}

				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(!str_utf8_check(pPassword))
				{
					return;
				}
				if(Config()->m_Password[0] != 0 && str_comp(Config()->m_Password, pPassword) != 0)
				{
					// wrong password
					m_NetServer.Drop(ClientId, EClientDropType::WrongPassword, "Wrong password");
					return;
				}

				m_aClients[ClientId].m_State = CClient::STATE_CONNECTING;
				SendCapabilities(ClientId);
				SendMap(ClientId);
			}
		}
		else if(Msg == NETMSG_REQUEST_MAP_DATA)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) == 0 || m_aClients[ClientId].m_State < CClient::STATE_CONNECTING)
				return;

			int Chunk = Unpacker.GetInt();
			if(Chunk != m_aClients[ClientId].m_NextMapChunk || !Config()->m_SvFastDownload)
			{
				SendMapData(ClientId, Chunk);
				return;
			}

			if(Chunk == 0)
			{
				for(int i = 0; i < Config()->m_SvMapWindow; i++)
				{
					SendMapData(ClientId, i);
				}
			}
			SendMapData(ClientId, Config()->m_SvMapWindow + m_aClients[ClientId].m_NextMapChunk);
			m_aClients[ClientId].m_NextMapChunk++;
		}
		else if(Msg == NETMSG_READY)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientId].m_State == CClient::STATE_CONNECTING))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientId), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player is ready. ClientId=%d addr=<{%s}> secure=%s", ClientId, aAddrStr, m_NetServer.HasSecurityToken(ClientId) ? "yes" : "no");
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_aClients[ClientId].m_State = CClient::STATE_READY;
				m_aClients[ClientId].m_WaitingTime = TickSpeed()*g_Config.m_InfConWaitingTime;
			}
		}
		else if(Msg == NETMSG_ENTERGAME)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientId].m_State == CClient::STATE_READY && GameServer()->IsClientReady(ClientId))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientId), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player has entered the game. ClientId=%d addr=%s", ClientId, aAddrStr);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				m_aClients[ClientId].m_State = CClient::STATE_INGAME;
				
				if(m_aClients[ClientId].m_WaitingTime <= 0)
				{
					GameServer()->OnClientEnter(ClientId);
					ExpireServerInfo();
				}
			}
		}
		else if(Msg == NETMSG_INPUT)
		{
			m_aClients[ClientId].m_LastAckedSnapshot = Unpacker.GetInt();
			int IntendedTick = Unpacker.GetInt();
			int Size = Unpacker.GetInt();

			// check for errors
			if(Unpacker.Error() || Size / 4 > MAX_INPUT_SIZE || IntendedTick < MIN_TICK || IntendedTick >= MAX_TICK)
				return;

			if(m_aClients[ClientId].m_LastAckedSnapshot > 0)
				m_aClients[ClientId].m_SnapRate = CClient::SNAPRATE_FULL;

			int64_t TagTime;
			if(m_aClients[ClientId].m_Snapshots.Get(m_aClients[ClientId].m_LastAckedSnapshot, &TagTime, 0, 0) >= 0)
				m_aClients[ClientId].m_Latency = (int)(((time_get() - TagTime) * 1000) / time_freq());

			// add message to report the input timing
			// skip packets that are old
			if(IntendedTick > m_aClients[ClientId].m_LastInputTick)
			{
				const int TimeLeft = (TickStartTime(IntendedTick) - time_get()) / (time_freq() / 1000);

				CMsgPacker Msgp(NETMSG_INPUTTIMING, true);
				Msgp.AddInt(IntendedTick);
				Msgp.AddInt(TimeLeft);
				SendMsg(&Msgp, 0, ClientId);
			}

			m_aClients[ClientId].m_LastInputTick = IntendedTick;

			CClient::CInput *pInput = &m_aClients[ClientId].m_aInputs[m_aClients[ClientId].m_CurrentInput];

			if(IntendedTick <= Tick())
				IntendedTick = Tick() + 1;

			pInput->m_GameTick = IntendedTick;

			for(int i = 0; i < Size / 4; i++)
				pInput->m_aData[i] = Unpacker.GetInt();

			GameServer()->OnClientPrepareInput(ClientId, pInput->m_aData);
			mem_copy(m_aClients[ClientId].m_LatestInput.m_aData, pInput->m_aData, MAX_INPUT_SIZE * sizeof(int));

			m_aClients[ClientId].m_CurrentInput++;
			m_aClients[ClientId].m_CurrentInput %= 200;

			// call the mod with the fresh input data
			if(m_aClients[ClientId].m_State == CClient::STATE_INGAME)
				GameServer()->OnClientDirectInput(ClientId, m_aClients[ClientId].m_LatestInput.m_aData);
		}
		else if(Msg == NETMSG_RCON_CMD)
		{
			const char *pCmd = Unpacker.GetString();
			if(!str_utf8_check(pCmd))
			{
				return;
			}
			if(Unpacker.Error() == 0 && !str_comp(pCmd, "crashmeplx"))
			{
				int Version = m_aClients[ClientId].m_DDNetVersion;
				if(GameServer()->PlayerExists(ClientId) && Version < VERSION_DDNET_OLD)
				{
					m_aClients[ClientId].m_DDNetVersion = VERSION_DDNET_OLD;
				}
			}
			else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0 && m_aClients[ClientId].m_Authed)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "ClientId=%d rcon='%s'", ClientId, pCmd);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				m_RconClientId = ClientId;
				m_RconAuthLevel = m_aClients[ClientId].m_Authed;
				switch(m_aClients[ClientId].m_Authed)
				{
					case AUTHED_ADMIN:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
						break;
					case AUTHED_MOD:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_MOD);
						break;
					default:
						Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
				}
				{
					CRconClientLogger Logger(this, ClientId);
					CLogScope Scope(&Logger);
					Console()->ExecuteLineFlag(pCmd, CFGFLAG_SERVER, ClientId);
				}
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				m_RconClientId = IServer::RCON_CID_SERV;
				m_RconAuthLevel = AUTHED_ADMIN;
			}
		}
		else if(Msg == NETMSG_RCON_AUTH)
		{
			const char *pPw;
			Unpacker.GetString(); // login name, not used
			pPw = Unpacker.GetString(CUnpacker::SANITIZE_CC);

			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0)
			{
				if(g_Config.m_SvRconPassword[0] == 0 && g_Config.m_SvRconModPassword[0] == 0)
				{
					SendRconLine(ClientId, "No rcon password set on server. Set sv_rcon_password and/or sv_rcon_mod_password to enable the remote console.");
				}
				else if(g_Config.m_SvRconTokenCheck && !m_NetServer.HasSecurityToken(ClientId))
				{
					SendRconLine(ClientId, "You must use a client that support anti-spoof protection (DDNet-like)");
				}
#ifdef CONF_SQL
				else if(m_aClients[ClientId].m_UserId < 0)
				{
					SendRconLine(ClientId, "You must be logged to your account. Please use /login");
				}
#endif
				else if(g_Config.m_SvRconPassword[0] && str_comp(pPw, g_Config.m_SvRconPassword) == 0)
				{
#ifdef CONF_SQL
					if(m_aClients[ClientId].m_UserLevel == SQL_USERLEVEL_ADMIN)
					{
#endif
						CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
						Msg.AddInt(1);	//authed
						Msg.AddInt(1);	//cmdlist
						SendMsg(&Msg, MSGFLAG_VITAL, ClientId);

						m_aClients[ClientId].m_Authed = AUTHED_ADMIN;
						GameServer()->OnSetAuthed(ClientId, m_aClients[ClientId].m_Authed);
						int SendRconCmds = Unpacker.GetInt();
						if(Unpacker.Error() == 0 && SendRconCmds)
							m_aClients[ClientId].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_ADMIN, CFGFLAG_SERVER);
						SendRconLine(ClientId, "Admin authentication successful. Full remote console access granted.");
						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "ClientId=%d authed (admin)", ClientId);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
#ifdef CONF_SQL
					}
					else
					{
						SendRconLine(ClientId, "You are not admin.");
					}
#endif
				}
				else if(g_Config.m_SvRconModPassword[0] && str_comp(pPw, g_Config.m_SvRconModPassword) == 0)
				{
#ifdef CONF_SQL
					if(m_aClients[ClientId].m_UserLevel == SQL_USERLEVEL_ADMIN || m_aClients[ClientId].m_UserLevel == SQL_USERLEVEL_MOD)
					{
#endif
						CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
						Msg.AddInt(1);	//authed
						Msg.AddInt(1);	//cmdlist
						SendMsg(&Msg, MSGFLAG_VITAL, ClientId);

						m_aClients[ClientId].m_Authed = AUTHED_MOD;
						int SendRconCmds = Unpacker.GetInt();
						if(Unpacker.Error() == 0 && SendRconCmds)
							m_aClients[ClientId].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_MOD, CFGFLAG_SERVER);
						SendRconLine(ClientId, "Moderator authentication successful. Limited remote console access granted.");
						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "ClientId=%d authed (moderator)", ClientId);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
#ifdef CONF_SQL
					}
					else
					{
						SendRconLine(ClientId, "You are not moderator.");
					}
#endif
				}
				else if(g_Config.m_SvRconMaxTries)
				{
					m_aClients[ClientId].m_AuthTries++;
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Wrong password %d/%d.", m_aClients[ClientId].m_AuthTries, g_Config.m_SvRconMaxTries);
					SendRconLine(ClientId, aBuf);
					if(m_aClients[ClientId].m_AuthTries >= g_Config.m_SvRconMaxTries)
					{
						if(!g_Config.m_SvRconBantime)
							m_NetServer.Drop(ClientId, EClientDropType::Kick, "Too many remote console authentication tries");
						else
							m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientId), g_Config.m_SvRconBantime*60, "Too many remote console authentication tries");
					}
				}
				else
				{
					SendRconLine(ClientId, "Wrong password.");
				}
			}
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msgp(NETMSG_PING_REPLY, true);
			SendMsg(&Msgp, MSGFLAG_FLUSH, ClientId);
		}
		else if(Msg == NETMSG_PINGEX)
		{
			CUuid *pId = (CUuid *)Unpacker.GetRaw(sizeof(*pId));
			if(Unpacker.Error())
			{
				return;
			}
			CMsgPacker Msgp(NETMSG_PONGEX, true);
			Msgp.AddRaw(pId, sizeof(*pId));
			SendMsg(&Msgp, MSGFLAG_FLUSH, ClientId);
		}
		else
		{
			if(Config()->m_Debug)
			{
				constexpr int MaxDumpedDataSize = 32;
				char aBuf[MaxDumpedDataSize * 3 + 1];
				str_hex(aBuf, sizeof(aBuf), pPacket->m_pData, minimum(pPacket->m_DataSize, MaxDumpedDataSize));

				char aBufMsg[256];
				str_format(aBufMsg, sizeof(aBufMsg), "strange message ClientId=%d msg=%d data_size=%d", ClientId, Msg, pPacket->m_DataSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBufMsg);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}
	else
	{
		// game message
		if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientId].m_State >= CClient::STATE_READY)
			GameServer()->OnMessage(Msg, &Unpacker, ClientId);
	}
}

bool CServer::RateLimitServerInfoConnless()
{
	bool SendClients = true;
	if(Config()->m_SvServerInfoPerSecond)
	{
		SendClients = m_ServerInfoNumRequests <= Config()->m_SvServerInfoPerSecond;
		const int64_t Now = Tick();

		if(Now <= m_ServerInfoFirstRequest + TickSpeed())
		{
			m_ServerInfoNumRequests++;
		}
		else
		{
			m_ServerInfoNumRequests = 1;
			m_ServerInfoFirstRequest = Now;
		}
	}

	return SendClients;
}

void CServer::SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type)
{
	SendServerInfo(pAddr, Token, Type, RateLimitServerInfoConnless());
}

static inline int GetCacheIndex(int Type, bool SendClient)
{
	if(Type == SERVERINFO_INGAME)
		Type = SERVERINFO_VANILLA;
	else if(Type == SERVERINFO_EXTENDED_MORE)
		Type = SERVERINFO_EXTENDED;

	return Type * 2 + SendClient;
}

CServer::CCache::CCache()
{
	m_vCache.clear();
}

CServer::CCache::~CCache()
{
	Clear();
}

CServer::CCache::CCacheChunk::CCacheChunk(const void *pData, int Size)
{
	m_vData.assign((const uint8_t *)pData, (const uint8_t *)pData + Size);
}

void CServer::CCache::AddChunk(const void *pData, int Size)
{
	m_vCache.emplace_back(pData, Size);
}

void CServer::CCache::Clear()
{
	m_vCache.clear();
}

void CServer::CacheServerInfo(CCache *pCache, int Type, bool SendClients)
{
	pCache->Clear();

	// One chance to improve the protocol!
	CPacker p;
	char aBuf[256];

	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if(ClientIsBot(i))
				continue;

			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	p.Reset();

#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
#define ADD_INT(p, x) \
	do \
	{ \
		str_from_int(x, aBuf); \
		(p).AddString(aBuf, 0); \
	} while(0)

	p.AddString(GameServer()->Version(), 32);

#ifdef CONF_SQL
	if(Config()->m_InfChallenge)
	{
		lock_wait(m_ChallengeLock);
		int ScoreType = ChallengeTypeToScoreType(m_ChallengeType);
		switch(ScoreType)
		{
		case SQL_SCORETYPE_ENGINEER_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "EngineerOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_MERCENARY_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "MercenaryOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_SCIENTIST_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "ScientistOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_BIOLOGIST_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "BiologistOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_LOOPER_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "LooperOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_NINJA_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "NinjaOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_SOLDIER_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "SoldierOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_SNIPER_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "SniperOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_MEDIC_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "MedicOfTheDay", m_aChallengeWinner);
			break;
		case SQL_SCORETYPE_HERO_SCORE:
			str_format(aBuf, sizeof(aBuf), "%s | %s: %s", Config()->m_SvName, "HeroOfTheDay", m_aChallengeWinner);
			break;
		}
		lock_release(m_ChallengeLock);
	}
#else
	memcpy(aBuf, Config()->m_SvName, sizeof(aBuf));
#endif

	const char *pMapName = GetMapName();
	if(Config()->m_SvHideInfo)
	{
		// Full hide
		ClientCount = 0;
		PlayerCount = 0;
		SendClients = false;
		pMapName = "";
	}
	else if (Config()->m_SvInfoMaxClients >= 0)
	{
		ClientCount = minimum(ClientCount, Config()->m_SvInfoMaxClients);
		PlayerCount = minimum(ClientCount, PlayerCount);
	}

	if(Type != SERVERINFO_VANILLA)
	{
		p.AddString(aBuf, 256);
	}
	else
	{
		if(m_NetServer.MaxClients() <= VANILLA_MAX_CLIENTS)
		{
			p.AddString(aBuf, 64);
		}
		else
		{
			const int MaxClients = maximum(ClientCount, m_NetServer.MaxClients());
			str_format(aBuf, sizeof(aBuf), "%s [%d/%d]", aBuf, ClientCount, MaxClients);
			p.AddString(aBuf, 64);
		}
	}
	p.AddString(pMapName, 32);

	if(Type == SERVERINFO_EXTENDED)
	{
		ADD_INT(p, m_aCurrentMapCrc[MAP_TYPE_SIX]);
		ADD_INT(p, m_aCurrentMapSize[MAP_TYPE_SIX]);
	}

	// gametype
	p.AddString(GameServer()->GameType(), 16);

	// flags
	ADD_INT(p, Config()->m_Password[0] ? SERVER_FLAG_PASSWORD : 0);

	int MaxClients = m_NetServer.MaxClients();
	if(Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
	{
		if(ClientCount >= VANILLA_MAX_CLIENTS)
		{
			if(ClientCount < MaxClients)
				ClientCount = VANILLA_MAX_CLIENTS - 1;
			else
				ClientCount = VANILLA_MAX_CLIENTS;
		}
		if(MaxClients > VANILLA_MAX_CLIENTS)
			MaxClients = VANILLA_MAX_CLIENTS;
		if(PlayerCount > ClientCount)
			PlayerCount = ClientCount;
	}

	ADD_INT(p, PlayerCount); // num players
	ADD_INT(p, MaxClients - Config()->m_SvSpectatorSlots); // max players
	ADD_INT(p, ClientCount); // num clients
	ADD_INT(p, MaxClients); // max clients

	if(Type == SERVERINFO_EXTENDED)
		p.AddString("", 0); // extra info, reserved

	const void *pPrefix = p.Data();
	int PrefixSize = p.Size();

	CPacker q;
	int ChunksStored = 0;
	int PlayersStored = 0;

#define SAVE(size) \
	do \
	{ \
		pCache->AddChunk(q.Data(), size); \
		ChunksStored++; \
	} while(0)

#define RESET() \
	do \
	{ \
		q.Reset(); \
		q.AddRaw(pPrefix, PrefixSize); \
	} while(0)

	RESET();

	if(Type == SERVERINFO_64_LEGACY)
		q.AddInt(PlayersStored); // offset

	if(!SendClients)
	{
		SAVE(q.Size());
		return;
	}

	if(Type == SERVERINFO_EXTENDED)
	{
		pPrefix = "";
		PrefixSize = 0;
	}

	int Remaining;
	switch(Type)
	{
	case SERVERINFO_EXTENDED: Remaining = -1; break;
	case SERVERINFO_64_LEGACY: Remaining = 24; break;
	case SERVERINFO_VANILLA: Remaining = VANILLA_MAX_CLIENTS; break;
	case SERVERINFO_INGAME: Remaining = VANILLA_MAX_CLIENTS; break;
	default: dbg_assert(0, "caught earlier, unreachable"); return;
	}

	// Use the following strategy for sending:
	// For vanilla, send the first 16 players.
	// For legacy 64p, send 24 players per packet.
	// For extended, send as much players as possible.

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if(ClientIsBot(i))
				continue;

			if(ClientCount == 0)
				break;

			--ClientCount;

			if(Remaining == 0)
			{
				if(Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
					break;

				// Otherwise we're SERVERINFO_64_LEGACY.
				SAVE(q.Size());
				RESET();
				q.AddInt(PlayersStored); // offset
				Remaining = 24;
			}
			if(Remaining > 0)
			{
				Remaining--;
			}

			int PreviousSize = q.Size();

			q.AddString(ClientName(i), MAX_NAME_LENGTH); // client name
			q.AddString(ClientClan(i), MAX_CLAN_LENGTH); // client clan

			ADD_INT(q, m_aClients[i].m_Country); // client country
			ADD_INT(q, RoundStatistics()->PlayerScore(i)); // client score
			ADD_INT(q, GameServer()->IsClientPlayer(i) ? 1 : 0); // is player?
			if(Type == SERVERINFO_EXTENDED)
				q.AddString("", 0); // extra info, reserved

			if(Type == SERVERINFO_EXTENDED)
			{
				if(q.Size() >= NET_MAX_PAYLOAD - 18) // 8 bytes for type, 10 bytes for the largest token
				{
					// Retry current player.
					i--;
					SAVE(PreviousSize);
					RESET();
					ADD_INT(q, ChunksStored);
					q.AddString("", 0); // extra info, reserved
					continue;
				}
			}
			PlayersStored++;
		}
	}

	SAVE(q.Size());
#undef SAVE
#undef RESET
#undef ADD_RAW
#undef ADD_INT
}

void CServer::CacheServerInfoSixup(CCache *pCache, bool SendClients)
{
	pCache->Clear();

	CPacker Packer;
	Packer.Reset();

	// Could be moved to a separate function and cached
	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	char aVersion[32];
	str_format(aVersion, sizeof(aVersion), "0.7↔%s", GameServer()->Version());
	Packer.AddString(aVersion, 32);
	Packer.AddString(Config()->m_SvName, 64);
	Packer.AddString(Config()->m_SvHostname, 128);
	Packer.AddString(GetMapName(), 32);

	// gametype
	Packer.AddString(GameServer()->GameType(), 16);

	// flags
	int Flags = SERVER_FLAG_TIMESCORE;
	if(Config()->m_Password[0]) // password set
		Flags |= SERVER_FLAG_PASSWORD;
	Packer.AddInt(Flags);

	int MaxClients = m_NetServer.MaxClients();
	Packer.AddInt(Config()->m_SvSkillLevel); // server skill level
	Packer.AddInt(PlayerCount); // num players
	Packer.AddInt(maximum(MaxClients - maximum(Config()->m_SvSpectatorSlots, Config()->m_SvReservedSlots), PlayerCount)); // max players
	Packer.AddInt(ClientCount); // num clients
	Packer.AddInt(maximum(MaxClients - Config()->m_SvReservedSlots, ClientCount)); // max clients

	if(SendClients)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			{
				Packer.AddString(ClientName(i), MAX_NAME_LENGTH); // client name
				Packer.AddString(ClientClan(i), MAX_CLAN_LENGTH); // client clan
				Packer.AddInt(m_aClients[i].m_Country); // client country
				Packer.AddInt(RoundStatistics()->PlayerScore(i)); // client score
				Packer.AddInt(GameServer()->IsClientPlayer(i) ? 0 : 1); // flag spectator=1, bot=2 (player=0)
			}
		}
	}

	pCache->AddChunk(Packer.Data(), Packer.Size());
}

void CServer::SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients)
{
	CPacker p;
	char aBuf[128];
	p.Reset();

	CCache *pCache = &m_aServerInfoCache[GetCacheIndex(Type, SendClients)];

#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
#define ADD_INT(p, x) \
	do \
	{ \
		str_from_int(x, aBuf); \
		(p).AddString(aBuf, 0); \
	} while(0)

	CNetChunk Packet;
	Packet.m_ClientId = -1;
	Packet.m_Address = *pAddr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;

	for(const auto &Chunk : pCache->m_vCache)
	{
		p.Reset();
		if(Type == SERVERINFO_EXTENDED)
		{
			if(&Chunk == &pCache->m_vCache.front())
				p.AddRaw(SERVERBROWSE_INFO_EXTENDED, sizeof(SERVERBROWSE_INFO_EXTENDED));
			else
				p.AddRaw(SERVERBROWSE_INFO_EXTENDED_MORE, sizeof(SERVERBROWSE_INFO_EXTENDED_MORE));
			ADD_INT(p, Token);
		}
		else if(Type == SERVERINFO_64_LEGACY)
		{
			ADD_RAW(p, SERVERBROWSE_INFO_64_LEGACY);
			ADD_INT(p, Token);
		}
		else if(Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
		{
			ADD_RAW(p, SERVERBROWSE_INFO);
			ADD_INT(p, Token);
		}
		else
		{
			dbg_assert(false, "unknown serverinfo type");
		}

		p.AddRaw(Chunk.m_vData.data(), Chunk.m_vData.size());
		Packet.m_pData = p.Data();
		Packet.m_DataSize = p.Size();
		m_NetServer.Send(&Packet);
	}
}

void CServer::GetServerInfoSixup(CPacker *pPacker, int Token, bool SendClients)
{
	if(Token != -1)
	{
		pPacker->Reset();
		pPacker->AddRaw(SERVERBROWSE_INFO, sizeof(SERVERBROWSE_INFO));
		pPacker->AddInt(Token);
	}

	SendClients = SendClients && Token != -1;

	CCache::CCacheChunk &FirstChunk = m_aSixupServerInfoCache[SendClients].m_vCache.front();
	pPacker->AddRaw(FirstChunk.m_vData.data(), FirstChunk.m_vData.size());
}

void CServer::FillAntibot(CAntibotRoundData *pData)
{
}

void CServer::ExpireServerInfo()
{
	m_ServerInfoNeedsUpdate = true;
}

void CServer::UpdateRegisterServerInfo()
{
	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if(ClientIsBot(i))
				continue;

			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	int MaxPlayers = maximum(m_NetServer.MaxClients() - maximum(g_Config.m_SvSpectatorSlots, g_Config.m_SvReservedSlots), PlayerCount);
	int MaxClients = maximum(m_NetServer.MaxClients() - g_Config.m_SvReservedSlots, ClientCount);
	char aName[256];
	char aGameType[32];
	char aMapName[64];
	char aVersion[64];
	char aMapSha256[SHA256_MAXSTRSIZE];

	const char *pMapName = GetMapName();
	if(Config()->m_SvHideInfo)
	{
		// Full hide
		ClientCount = 0;
		PlayerCount = 0;
		pMapName = "";
	}
	else if (Config()->m_SvInfoMaxClients >= 0)
	{
		ClientCount = minimum(ClientCount, Config()->m_SvInfoMaxClients);
		PlayerCount = minimum(ClientCount, PlayerCount);
	}

	if(pMapName[0])
		sha256_str(m_aCurrentMapSha256[MAP_TYPE_SIX], aMapSha256, sizeof(aMapSha256));
	else
		aMapSha256[0] = '\0';

	char aInfo[16384];
	str_format(aInfo, sizeof(aInfo),
		"{"
		"\"max_clients\":%d,"
		"\"max_players\":%d,"
		"\"passworded\":%s,"
		"\"game_type\":\"%s\","
		"\"name\":\"%s\","
		"\"map\":{"
		"\"name\":\"%s\","
		"\"sha256\":\"%s\","
		"\"size\":%d"
		"},"
		"\"version\":\"%s\","
		"\"client_score_kind\":\"points\","
		"\"clients\":[",
		MaxClients,
		MaxPlayers,
		JsonBool(g_Config.m_Password[0]),
		EscapeJson(aGameType, sizeof(aGameType), GameServer()->GameType()),
		EscapeJson(aName, sizeof(aName), g_Config.m_SvName),
		EscapeJson(aMapName, sizeof(aMapName), pMapName),
		aMapSha256,
		m_aCurrentMapSize[MAP_TYPE_SIX],
		EscapeJson(aVersion, sizeof(aVersion), GameServer()->Version()));

	bool FirstPlayer = true;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if(ClientIsBot(i))
				continue;

			if(ClientCount == 0)
				break;

			--ClientCount;

			char aCName[32];
			char aCClan[32];

			char aExtraPlayerInfo[512];
			GameServer()->OnUpdatePlayerServerInfo(aExtraPlayerInfo, sizeof(aExtraPlayerInfo), i);

			char aClientInfo[1024];
			str_format(aClientInfo, sizeof(aClientInfo),
				"%s{"
				"\"name\":\"%s\","
				"\"clan\":\"%s\","
				"\"country\":%d,"
				"\"score\":%d,"
				"\"is_player\":%s"
				"%s"
				"}",
				!FirstPlayer ? "," : "",
				EscapeJson(aCName, sizeof(aCName), ClientName(i)),
				EscapeJson(aCClan, sizeof(aCClan), ClientClan(i)),
				m_aClients[i].m_Country,
				RoundStatistics()->PlayerScore(i),
				JsonBool(GameServer()->IsClientPlayer(i)),
				aExtraPlayerInfo);
			str_append(aInfo, aClientInfo);
			FirstPlayer = false;
		}
	}

	str_append(aInfo, "]}");

	m_pRegister->OnNewInfo(aInfo);
}

void CServer::UpdateServerInfo(bool Resend)
{
	if(m_RunServer == UNINITIALIZED)
		return;

	UpdateRegisterServerInfo();

	for(int i = 0; i < 3; i++)
		for(int j = 0; j < 2; j++)
			CacheServerInfo(&m_aServerInfoCache[i * 2 + j], i, j);

	for(int i = 0; i < 2; i++)
		CacheServerInfoSixup(&m_aSixupServerInfoCache[i], i);

	if(Resend)
	{
		for(int i = 0; i < MaxClients(); ++i)
		{
			if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			{
				if(!IsSixup(i))
					SendServerInfo(m_NetServer.ClientAddr(i), -1, SERVERINFO_INGAME, false);
				else
				{
					CMsgPacker Msg(protocol7::NETMSG_SERVERINFO, true, true);
					GetServerInfoSixup(&Msg, -1, false);
					SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, i);
				}
			}
		}
	}

	m_ServerInfoNeedsUpdate = false;
}

void CServer::PumpNetwork(bool PacketWaiting)
{
	CNetChunk Packet;
	SECURITY_TOKEN ResponseToken;

	m_NetServer.Update();

	if(PacketWaiting)
	{
		// process packets
		while(m_NetServer.Recv(&Packet, &ResponseToken))
		{
			if(Packet.m_ClientId == -1)
			{
				if(ResponseToken == NET_SECURITY_TOKEN_UNKNOWN && m_pRegister->OnPacket(&Packet))
					continue;

				{
					int ExtraToken = 0;
					int Type = -1;
					if(Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO) + 1 &&
						mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO)) == 0)
					{
						if(Packet.m_Flags & NETSENDFLAG_EXTENDED)
						{
							Type = SERVERINFO_EXTENDED;
							ExtraToken = (Packet.m_aExtraData[0] << 8) | Packet.m_aExtraData[1];
						}
						else
							Type = SERVERINFO_VANILLA;
					}
					else if(Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO_64_LEGACY) + 1 &&
							mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO_64_LEGACY, sizeof(SERVERBROWSE_GETINFO_64_LEGACY)) == 0)
					{
						Type = SERVERINFO_64_LEGACY;
					}
					if(Type == SERVERINFO_VANILLA && ResponseToken != NET_SECURITY_TOKEN_UNKNOWN && Config()->m_SvSixup)
					{
						CUnpacker Unpacker;
						Unpacker.Reset((unsigned char *)Packet.m_pData + sizeof(SERVERBROWSE_GETINFO), Packet.m_DataSize - sizeof(SERVERBROWSE_GETINFO));
						int SrvBrwsToken = Unpacker.GetInt();
						if(Unpacker.Error())
							continue;

						CPacker Packer;
						CNetChunk Response;

						GetServerInfoSixup(&Packer, SrvBrwsToken, RateLimitServerInfoConnless());

						Response.m_ClientId = -1;
						Response.m_Address = Packet.m_Address;
						Response.m_Flags = NETSENDFLAG_CONNLESS;
						Response.m_pData = Packer.Data();
						Response.m_DataSize = Packer.Size();
						m_NetServer.SendConnlessSixup(&Response, ResponseToken);
					}
					else if(Type != -1)
					{
						int Token = ((unsigned char *)Packet.m_pData)[sizeof(SERVERBROWSE_GETINFO)];
						Token |= ExtraToken << 8;
						SendServerInfoConnless(&Packet.m_Address, Token, Type);
					}
				}
			}
			else
			{
				if(m_aClients[Packet.m_ClientId].m_State == CClient::STATE_REDIRECTED)
					continue;

				int GameFlags = 0;
				if(Packet.m_Flags & NET_CHUNKFLAG_VITAL)
				{
					GameFlags |= MSGFLAG_VITAL;
				}

				ProcessClientPacket(&Packet);
			}
		}
	}

	m_ServerBan.Update();
	m_NetSession.Update();
	m_NetAccusation.Update();
	m_Econ.Update();
}

const char *CServer::GetMapName() const
{
	// get the name of the map without his path
	const char *pMapShortName = &Config()->m_SvMap[0];
	for(int i = 0; i < str_length(Config()->m_SvMap) - 1; i++)
	{
		if(Config()->m_SvMap[i] == '/' || Config()->m_SvMap[i] == '\\')
			pMapShortName = &Config()->m_SvMap[i + 1];
	}
	return pMapShortName;
}

void CServer::ChangeMap(const char *pMap)
{
	str_copy(Config()->m_SvMap, pMap);
	m_MapReload = str_comp(Config()->m_SvMap, m_aCurrentMap) != 0;
}

int CServer::LoadMap(const char *pMapName)
{
	m_MapReload = false;

	char aBuf[IO_MAX_PATH_LENGTH];
/* INFECTION MODIFICATION START ***************************************/
	const char *pLoadedMapFileName = nullptr;
	const char *pEventMapName = EventsDirector::GetEventMapName(pMapName);
	for(const char *pMapFileName : {pEventMapName, pMapName})
	{
		if(!pMapFileName)
			continue;

		str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapFileName);

		if(str_startswith(pMapName, "infc_"))
		{
			if(!GenerateClientMap(aBuf, pMapFileName))
				continue;
		}
		else
		{
			continue;
		}

		pLoadedMapFileName = pMapFileName;
		break;
	}
	if(!pLoadedMapFileName)
		return 0;

	str_format(aBuf, sizeof(aBuf), "map_loaded name='%s' file='maps/%s.map'", pMapName, pLoadedMapFileName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
/* INFECTION MODIFICATION END *****************************************/

	// stop recording when we change map
	for(int i = 0; i < MAX_CLIENTS + 1; i++)
	{
		if(!m_aDemoRecorder[i].IsRecording())
			continue;

		m_aDemoRecorder[i].Stop();

		// remove tmp demos
		if(i < MAX_CLIENTS)
		{
			char aPath[256];
			str_format(aPath, sizeof(aPath), "demos/%s_%d_%d_tmp.demo", m_aCurrentMap, m_NetServer.Address().port, i);
			Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
		}
	}

	// reinit snapshot ids
	m_IdPool.TimeoutIds();

/* INFECTION MODIFICATION START ***************************************/
	str_copy(m_aPreviousMap, m_aCurrentMap);
	str_copy(m_aCurrentMap, pMapName);
	ResetMapVotes();

/* INFECTION MODIFICATION END *****************************************/

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aPrevStates[i] = m_aClients[i].m_State;

	return 1;
}

static bool IsSeparator(char c) { return c == ';' || c == ' ' || c == ',' || c == '\t'; }

int CServer::Run()
{
	if(m_RunServer == UNINITIALIZED)
		m_RunServer = RUNNING;

	if(Config()->m_Debug)
	{
		g_UuidManager.DebugDump();
	}

	{
		// int Size = GameServer()->PersistentClientDataSize();
		for(CClient &Client : m_aClients)
		{
			Client.m_HasPersistentData = false;
			// The code is disabled to lazily allocate the data
			// Client.m_pPersistentData = malloc(Size);
			Client.m_pPersistentData = nullptr;
		}
	}

	m_pPersistentData = malloc(GameServer()->PersistentDataSize());

	//Choose a random map from the rotation
	if(!str_length(g_Config.m_SvMap) && str_length(g_Config.m_SvMaprotation))
	{
		int nbMaps = 0;
		{
			const char *pNextMap = g_Config.m_SvMaprotation;
			
			//Skip initial separator
			while(*pNextMap && IsSeparator(*pNextMap))
				pNextMap++;
				
			while(*pNextMap)
			{
				while(*pNextMap && !IsSeparator(*pNextMap))
					pNextMap++;
				while(*pNextMap && IsSeparator(*pNextMap))
					pNextMap++;
			
				nbMaps++;
			}
		}
		
		int MapPos = random_int(0, nbMaps-1);
		char aBuf[512] = {0};
		
		{
			int MapPosIter = 0;
			const char *pNextMap = g_Config.m_SvMaprotation;
			
			//Skip initial separator
			while(*pNextMap && IsSeparator(*pNextMap))
				pNextMap++;
				
			while(*pNextMap)
			{
				if(MapPosIter == MapPos)
				{
					int MapNameLength = 0;
					while(pNextMap[MapNameLength] && !IsSeparator(pNextMap[MapNameLength]))
						MapNameLength++;
					mem_copy(aBuf, pNextMap, MapNameLength);	
					aBuf[MapNameLength] = 0;
					break;
				}
				
				while(*pNextMap && !IsSeparator(*pNextMap))
					pNextMap++;
				while(*pNextMap && IsSeparator(*pNextMap))
					pNextMap++;
			
				MapPosIter++;
			}
		}
		
		str_copy(g_Config.m_SvMap, aBuf, sizeof(g_Config.m_SvMap));
	}

	// load map
	if(!LoadMap(Config()->m_SvMap))
	{
		dbg_msg("server", "failed to load map. mapname='%s'", Config()->m_SvMap);
		return -1;
	}

	if(Config()->m_SvSqliteFile[0] != '\0')
	{
		char aFullPath[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE_OR_ABSOLUTE, Config()->m_SvSqliteFile, aFullPath, sizeof(aFullPath));

		if(Config()->m_SvUseSql)
		{
			DbPool()->RegisterSqliteDatabase(CDbConnectionPool::WRITE_BACKUP, aFullPath);
		}
		else
		{
			DbPool()->RegisterSqliteDatabase(CDbConnectionPool::READ, aFullPath);
			DbPool()->RegisterSqliteDatabase(CDbConnectionPool::WRITE, aFullPath);
		}
	}

	// start server
	NETADDR BindAddr;
	if(!g_Config.m_Bindaddr[0] || net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) != 0)
		mem_zero(&BindAddr, sizeof(BindAddr));

	BindAddr.type = NETTYPE_ALL;

	int Port = Config()->m_SvPort;
	for(BindAddr.port = Port != 0 ? Port : 8303; !m_NetServer.Open(BindAddr, &m_ServerBan, Config()->m_SvMaxClients, Config()->m_SvMaxClientsPerIp); BindAddr.port++)
	{
		if(Port != 0 || BindAddr.port >= 8310)
		{
			dbg_msg("server", "couldn't open socket. port %d might already be in use", BindAddr.port);
			return -1;
		}
	}

	if(Port == 0)
		dbg_msg("server", "using port %d", BindAddr.port);

	if(!m_Http.Init(std::chrono::seconds{2}))
	{
		log_error("server", "Failed to initialize the HTTP client.");
		return -1;
	}

	m_pEngine = Kernel()->RequestInterface<IEngine>();
	m_pRegister = CreateRegister(&g_Config, m_pConsole, m_pEngine, &m_Http, this->Port(), m_NetServer.GetGlobalToken());

	m_NetServer.SetCallbacks(NewClientCallback, NewClientNoAuthCallback, ClientRejoinCallback, DelClientCallback, this);

	m_Econ.Init(Config(), Console(), &m_ServerBan);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "server name is '%s'", Config()->m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	GameServer()->OnInit(nullptr);
	if(ErrorShutdown())
	{
		m_RunServer = STOPPING;
	}
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "version " GAME_RELEASE_VERSION " on " CONF_PLATFORM_STRING " " CONF_ARCH_STRING);
	if(GIT_SHORTREV_HASH)
	{
		str_format(aBuf, sizeof(aBuf), "git revision hash: %s", GIT_SHORTREV_HASH);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

	// process pending commands
	m_pConsole->StoreCommands(false);
	m_pRegister->OnConfigChange();

	// start game
	{
		bool NonActive = false;
		bool PacketWaiting = false;

		m_GameStartTime = time_get();

		UpdateServerInfo();
		while(m_RunServer < STOPPING)
		{
			if(NonActive)
				PumpNetwork(PacketWaiting);

			set_new_tick();

			int64_t t = time_get();
			int NewTicks = 0;
			
#ifdef CONF_SQL
			//Update informations each 10 seconds
			if(t - m_ChallengeRefreshTick >= time_freq()*10)
			{
				RefreshChallenge();
				m_ChallengeRefreshTick = t;
			}
#endif

			// load new map TODO: don't poll this
			if(str_comp(g_Config.m_SvMap, m_aCurrentMap) != 0 || m_MapReload || m_CurrentGameTick >= MAX_TICK) // force reload to make sure the ticks stay within a valid range
			{
				// load map
				if(LoadMap(Config()->m_SvMap))
				{
					// new map loaded

					// ask the game to for the data it wants to persist past a map change
					const int ClientDataSize = GameServer()->PersistentClientDataSize();
					for(int i = 0; i < MAX_CLIENTS; i++)
					{
						if(m_aClients[i].m_State == CClient::STATE_INGAME)
						{
							if(!m_aClients[i].m_pPersistentData)
							{
								m_aClients[i].m_pPersistentData = malloc(ClientDataSize);
							}
							m_aClients[i].m_HasPersistentData = GameServer()->OnClientDataPersist(i, m_aClients[i].m_pPersistentData);
						}
					}

					GameServer()->OnShutdown(m_pPersistentData);

					for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
					{
						if(m_aClients[ClientId].m_State <= CClient::STATE_AUTH)
							continue;

						if(m_aClients[ClientId].m_IsBot)
						{
							DelBot(ClientId);
							continue;
						}

						SendMap(ClientId);
						bool HasPersistentData = m_aClients[ClientId].m_HasPersistentData;
/* INFECTION MODIFICATION START ***************************************/
						m_aClients[ClientId].Reset(false);
/* INFECTION MODIFICATION END *****************************************/
						m_aClients[ClientId].m_HasPersistentData = HasPersistentData;
						m_aClients[ClientId].m_State = CClient::STATE_CONNECTING;
						SetClientMemory(ClientId, CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE, true);
					}

					m_GameStartTime = time_get();
					m_CurrentGameTick = 0;
					m_ServerInfoFirstRequest = 0;
					Kernel()->ReregisterInterface(GameServer());
					GameServer()->OnInit(m_pPersistentData);
					if(ErrorShutdown())
					{
						break;
					}
					UpdateServerInfo(true);
				}
				else
				{
					str_format(aBuf, sizeof(aBuf), "failed to load map. mapname='%s'", g_Config.m_SvMap);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
					str_copy(g_Config.m_SvMap, m_aCurrentMap, sizeof(g_Config.m_SvMap));
				}
			}

			while(t > TickStartTime(m_CurrentGameTick + 1))
			{
				for(int c = 0; c < MAX_CLIENTS; c++)
				{
					if(m_aClients[c].m_State != CClient::STATE_INGAME)
						continue;
					bool ClientHadInput = false;
					for(auto &Input : m_aClients[c].m_aInputs)
					{
						if(Input.m_GameTick == Tick() + 1)
						{
							GameServer()->OnClientPredictedEarlyInput(c, Input.m_aData);
							ClientHadInput = true;
						}
					}
					if(!ClientHadInput)
						GameServer()->OnClientPredictedEarlyInput(c, nullptr);
				}

				m_CurrentGameTick++;
				NewTicks++;

				//Check for name collision. We add this because the login is in a different thread and can't check it himself.
				for(int i=MAX_CLIENTS-1; i>=0; i--)
				{
					if(m_aClients[i].m_State >= CClient::STATE_READY && m_aClients[i].m_Session.m_MuteTick > 0)
						m_aClients[i].m_Session.m_MuteTick--;
				}
				
				for(int ClientId=0; ClientId<MAX_CLIENTS; ClientId++)
				{
					if(m_aClients[ClientId].m_WaitingTime > 0)
					{
						m_aClients[ClientId].m_WaitingTime--;
						if(m_aClients[ClientId].m_WaitingTime <= 0)
						{
							if(m_aClients[ClientId].m_State == CClient::STATE_READY)
							{
								void *pPersistentData = 0;
								if(m_aClients[ClientId].m_HasPersistentData)
								{
									pPersistentData = m_aClients[ClientId].m_pPersistentData;
									m_aClients[ClientId].m_HasPersistentData = false;
								}

								GameServer()->OnClientConnected(ClientId, pPersistentData);
								SendConnectionReady(ClientId);
							}
							else if(m_aClients[ClientId].m_State == CClient::STATE_INGAME)
							{
								GameServer()->OnClientEnter(ClientId);
							}
						}
					}
				}

				// apply new input
				for(int c = 0; c < MAX_CLIENTS; c++)
				{
					if(m_aClients[c].m_State != CClient::STATE_INGAME)
						continue;
					bool ClientHadInput = false;
					for(auto &Input : m_aClients[c].m_aInputs)
					{
						if(Input.m_GameTick == Tick())
						{
							GameServer()->OnClientPredictedInput(c, Input.m_aData);
							ClientHadInput = true;
							break;
						}
					}
					if(!ClientHadInput)
						GameServer()->OnClientPredictedInput(c, nullptr);
				}

				GameServer()->OnTick();
				
#ifdef CONF_SQL
				if(m_lGameServerCmds.size())
				{
					lock_wait(m_GameServerCmdLock);
					for(int i=0; i<m_lGameServerCmds.size(); i++)
					{
						m_lGameServerCmds[i]->Execute(GameServer());
						delete m_lGameServerCmds[i];
					}
					m_lGameServerCmds.clear();
					lock_release(m_GameServerCmdLock);
				} 
#endif
				if(ErrorShutdown())
				{
					break;
				}
			}

			// snap game
			if(NewTicks)
			{
				if(Config()->m_SvHighBandwidth || (m_CurrentGameTick % 2) == 0)
					DoSnapshot();

				UpdateClientRconCommands();

				// master server stuff
				m_pRegister->Update();

				if(m_ServerInfoNeedsUpdate)
					UpdateServerInfo();

				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					if(m_aClients[i].m_State == CClient::STATE_REDIRECTED)
					{
						if(time_get() > m_aClients[i].m_RedirectDropTime)
						{
							m_NetServer.Drop(i, EClientDropType::Redirected, "redirected");
						}
					}
				}
			}

			if(!NonActive)
				PumpNetwork(PacketWaiting);

			NonActive = true;
			for(const auto &Client : m_aClients)
			{
				if(Client.m_State != CClient::STATE_EMPTY)
				{
					NonActive = false;
					break;
				}
			}

			// wait for incoming data
			if(NonActive)
			{
				if(Config()->m_SvReloadWhenEmpty == 1)
				{
					m_MapReload = true;
					Config()->m_SvReloadWhenEmpty = 0;
				}
				else if(Config()->m_SvReloadWhenEmpty == 2 && !m_ReloadedWhenEmpty)
				{
					m_MapReload = true;
					m_ReloadedWhenEmpty = true;
				}

				if(Config()->m_SvShutdownWhenEmpty)
					m_RunServer = STOPPING;
				else
					PacketWaiting = net_socket_read_wait(m_NetServer.Socket(), 1000000);
			}
			else
			{
				m_ReloadedWhenEmpty = false;

				set_new_tick();
				t = time_get();
				int x = (TickStartTime(m_CurrentGameTick + 1) - t) * 1000000 / time_freq() + 1;

				PacketWaiting = x > 0 ? net_socket_read_wait(m_NetServer.Socket(), x) : true;
			}
		}
	}

	if(Config()->m_SvShutdownFile[0])
		Console()->ExecuteFile(Config()->m_SvShutdownFile);

	const char *pDisconnectReason = "Server shutdown";
	if(m_aShutdownReason[0])
		pDisconnectReason = m_aShutdownReason;

	if(ErrorShutdown())
	{
		dbg_msg("server", "shutdown from game server (%s)", m_aErrorShutdownReason);
		pDisconnectReason = m_aErrorShutdownReason;
	}
	// disconnect all clients on shutdown
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if((m_aClients[i].m_State != CClient::STATE_EMPTY) && !m_aClients[i].m_IsBot)
		{
			if(m_ReconnectClients && Port && (GetClientVersion(i) >= VERSION_DDNET_REDIRECT))
			{
				RedirectClient(i, Port);
				continue;
			}
			m_NetServer.Drop(i, EClientDropType::Shutdown, pDisconnectReason);
		}
	}

	m_pRegister->OnShutdown();
	m_Econ.Shutdown();
	Engine()->ShutdownJobs();

	GameServer()->OnShutdown(nullptr);
	m_pMap->Unload();
	DbPool()->OnShutdown();

/* DDNET MODIFICATION START *******************************************/
#ifdef CONF_SQL
	for (int i = 0; i < MAX_SQLSERVERS; i++)
	{
		if (m_apSqlReadServers[i])
			delete m_apSqlReadServers[i];

		if (m_apSqlWriteServers[i])
			delete m_apSqlWriteServers[i];
	}
#endif
/* DDNET MODIFICATION END *********************************************/

	m_NetServer.Close();

	return ErrorShutdown();
}

void CServer::ConUnmute(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	const char *pStr = pResult->GetString(0);
	
	if(str_isallnum(pStr))
	{
		int ClientId = str_toint(pStr);
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || pThis->m_aClients[ClientId].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
			pThis->m_aClients[ClientId].m_Session.m_MuteTick = 0;
	}
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
}

void CServer::ConMute(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments()>1 ? clamp(pResult->GetInteger(1), 0, 44640) : 5;
	const char *pReason = pResult->NumArguments()>2 ? pResult->GetString(2) : "No reason given";
	
	if(str_isallnum(pStr))
	{
		int ClientId = str_toint(pStr);
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || pThis->m_aClients[ClientId].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
		{
			int Time = 60*Minutes;
			pThis->m_aClients[ClientId].m_Session.m_MuteTick = pThis->TickSpeed()*60*Minutes;
			pThis->GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_ACCUSATION, _("{str:Victim} has been muted for {sec:Duration} ({str:Reason})"), "Victim", pThis->ClientName(ClientId) ,"Duration", &Time, "Reason", pReason, NULL);
		}
	}
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
}

void CServer::ConWhisper(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	const char *pStrClientId = pResult->GetString(0);
	const char *pText = pResult->GetString(1);

	if(str_isallnum(pStrClientId))
	{
		int ClientId = str_toint(pStrClientId);
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || pThis->m_aClients[ClientId].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
		{
			// Send to target
			CNetMsg_Sv_Chat Msg;
			Msg.m_Team = 0;
			Msg.m_ClientId = -1;
			Msg.m_pMessage = pText;
			pThis->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);

			// Confirm message sent
			char aBuf[1024];
			str_format(aBuf, sizeof(aBuf), "Whisper '%s' sent to %s",
				pText,
				pThis->ClientName(ClientId)
			);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		}
	}
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
}

void CServer::ConKick(IConsole::IResult *pResult, void *pUser)
{
	if(pResult->NumArguments() > 1)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Kicked (%s)", pResult->GetString(1));
		((CServer *)pUser)->Kick(pResult->GetInteger(0), aBuf);
	}
	else
		((CServer *)pUser)->Kick(pResult->GetInteger(0), "Kicked by console");
}

void CServer::ConStatusExtended(IConsole::IResult *pResult, void *pUser)
{
	ConStatus(pResult, pUser);
}

void CServer::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	char aAddrStr[NETADDR_MAXSTRSIZE];
	CServer* pThis = static_cast<CServer *>(pUser);

/* INFECTION MODIFICATION START ***************************************/
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if((pThis->m_aClients[i].m_State != CClient::STATE_EMPTY) && !pThis->m_aClients[i].m_IsBot)
		{
			net_addr_str(pThis->m_NetServer.ClientAddr(i), aAddrStr, sizeof(aAddrStr), true);
			if(pThis->m_aClients[i].m_State == CClient::STATE_INGAME)
			{				
				//Add some padding to make the command more readable
				char aBufName[18];
				str_copy(aBufName, pThis->ClientName(i), sizeof(aBufName));
				for(int c=str_length(aBufName); c<((int)sizeof(aBufName))-1; c++)
					aBufName[c] = ' ';
				aBufName[sizeof(aBufName)-1] = 0;
				
				int AuthLevel = pThis->m_aClients[i].m_Authed == CServer::AUTHED_ADMIN ? 2 :
										pThis->m_aClients[i].m_Authed == CServer::AUTHED_MOD ? 1 : 0;
				
				str_format(aBuf, sizeof(aBuf), "(#%02i) %s: [antispoof=%d] [login=%d] [level=%d] [ip=%s] [version=%d] [inf=%d]",
					i,
					aBufName,
					pThis->m_NetServer.HasSecurityToken(i),
					pThis->IsClientLogged(i),
					AuthLevel,
					aAddrStr,
					pThis->m_aClients[i].m_DDNetVersion,
					pThis->m_aClients[i].m_InfClassVersion
				);
			}
			else
				str_format(aBuf, sizeof(aBuf), "id=%d addr=%s connecting", i, aAddrStr);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		}
	}
/* INFECTION MODIFICATION END *****************************************/
}

void CServer::ConNameBan(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	char aBuf[256];
	const char *pName = pResult->GetString(0);
	const char *pReason = pResult->NumArguments() > 3 ? pResult->GetString(3) : "";
	int Distance = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : str_length(pName) / 3;
	int IsSubstring = pResult->NumArguments() > 2 ? pResult->GetInteger(2) : 0;

	for(auto &Ban : pThis->m_vNameBans)
	{
		if(str_comp(Ban.m_aName, pName) == 0)
		{
			str_format(aBuf, sizeof(aBuf), "changed name='%s' distance=%d old_distance=%d is_substring=%d old_is_substring=%d reason='%s' old_reason='%s'", pName, Distance, Ban.m_Distance, IsSubstring, Ban.m_IsSubstring, pReason, Ban.m_aReason);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
			Ban.m_Distance = Distance;
			Ban.m_IsSubstring = IsSubstring;
			str_copy(Ban.m_aReason, pReason);
			return;
		}
	}

	pThis->m_vNameBans.emplace_back(pName, Distance, IsSubstring, pReason);
	str_format(aBuf, sizeof(aBuf), "added name='%s' distance=%d is_substring=%d reason='%s'", pName, Distance, IsSubstring, pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
}

void CServer::ConNameUnban(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	const char *pName = pResult->GetString(0);

	for(size_t i = 0; i < pThis->m_vNameBans.size(); i++)
	{
		CNameBan *pBan = &pThis->m_vNameBans[i];
		if(str_comp(pBan->m_aName, pName) == 0)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "removed name='%s' distance=%d is_substring=%d reason='%s'", pBan->m_aName, pBan->m_Distance, pBan->m_IsSubstring, pBan->m_aReason);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
			pThis->m_vNameBans.erase(pThis->m_vNameBans.begin() + i);
		}
	}
}

void CServer::ConNameBans(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	for(auto &Ban : pThis->m_vNameBans)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "name='%s' distance=%d is_substring=%d reason='%s'", Ban.m_aName, Ban.m_Distance, Ban.m_IsSubstring, Ban.m_aReason);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "name_ban", aBuf);
	}
}

void CServer::ConShutdown(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = static_cast<CServer *>(pUser);
	pThis->m_RunServer = STOPPING;
	const char *pReason = pResult->GetString(0);
	if(pReason[0])
	{
		str_copy(pThis->m_aShutdownReason, pReason);
	}
}

void CServer::ConShutdown2(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = static_cast<CServer *>(pUser);
	pThis->m_ReconnectClients = true;

	ConShutdown(pResult, pUser);
}

void CServer::DemoRecorder_HandleAutoStart()
{
	if(Config()->m_SvAutoDemoRecord)
	{
		m_aDemoRecorder[0].Stop();
		char aFilename[IO_MAX_PATH_LENGTH];
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", "auto/autorecord", aDate);
		m_aDemoRecorder[0].Start(Storage(), m_pConsole, aFilename, GameServer()->NetVersion(), m_aCurrentMap, &m_aCurrentMapSha256[MAP_TYPE_SIX], m_aCurrentMapCrc[MAP_TYPE_SIX], "server", m_aCurrentMapSize[MAP_TYPE_SIX], m_apCurrentMapData[MAP_TYPE_SIX]);
		if(Config()->m_SvAutoDemoMax)
		{
			// clean up auto recorded demos
			CFileCollection AutoDemos;
			AutoDemos.Init(Storage(), "demos/server", "autorecord", ".demo", Config()->m_SvAutoDemoMax);
		}
	}
}

void CServer::SaveDemo(int ClientId, float Time)
{
	if(IsRecording(ClientId))
	{
		m_aDemoRecorder[ClientId].Stop();

		// rename the demo
		char aOldFilename[IO_MAX_PATH_LENGTH];
		char aNewFilename[IO_MAX_PATH_LENGTH];
		str_format(aOldFilename, sizeof(aOldFilename), "demos/%s_%d_%d_tmp.demo", m_aCurrentMap, m_NetServer.Address().port, ClientId);
		str_format(aNewFilename, sizeof(aNewFilename), "demos/%s_%s_%05.2f.demo", m_aCurrentMap, m_aClients[ClientId].m_aName, Time);
		Storage()->RenameFile(aOldFilename, aNewFilename, IStorage::TYPE_SAVE);
	}
}

void CServer::StartRecord(int ClientId)
{
	if(Config()->m_SvPlayerDemoRecord)
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "demos/%s_%d_%d_tmp.demo", m_aCurrentMap, m_NetServer.Address().port, ClientId);
		m_aDemoRecorder[ClientId].Start(Storage(), Console(), aFilename, GameServer()->NetVersion(), m_aCurrentMap, &m_aCurrentMapSha256[MAP_TYPE_SIX], m_aCurrentMapCrc[MAP_TYPE_SIX], "server", m_aCurrentMapSize[MAP_TYPE_SIX], m_apCurrentMapData[MAP_TYPE_SIX]);
	}
}

void CServer::StopRecord(int ClientId)
{
	if(IsRecording(ClientId))
	{
		m_aDemoRecorder[ClientId].Stop();

		char aFilename[IO_MAX_PATH_LENGTH];
		str_format(aFilename, sizeof(aFilename), "demos/%s_%d_%d_tmp.demo", m_aCurrentMap, m_NetServer.Address().port, ClientId);
		Storage()->RemoveFile(aFilename, IStorage::TYPE_SAVE);
	}
}

bool CServer::IsRecording(int ClientId)
{
	return m_aDemoRecorder[ClientId].IsRecording();
}

void CServer::ConRecord(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;
	char aFilename[IO_MAX_PATH_LENGTH];

	if(pResult->NumArguments())
		str_format(aFilename, sizeof(aFilename), "demos/%s.demo", pResult->GetString(0));
	else
	{
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/demo_%s.demo", aDate);
	}
	pServer->m_aDemoRecorder[0].Start(pServer->Storage(), pServer->Console(), aFilename, pServer->GameServer()->NetVersion(), pServer->m_aCurrentMap, &pServer->m_aCurrentMapSha256[MAP_TYPE_SIX], pServer->m_aCurrentMapCrc[MAP_TYPE_SIX], "server", pServer->m_aCurrentMapSize[MAP_TYPE_SIX], pServer->m_apCurrentMapData[MAP_TYPE_SIX]);
}

void CServer::ConStopRecord(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_aDemoRecorder[0].Stop();
}

void CServer::ConMapReload(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_MapReload = true;
}

void CServer::ConLogout(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(pServer->m_RconClientId >= 0 && pServer->m_RconClientId < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientId].m_State != CServer::CClient::STATE_EMPTY)
	{
		pServer->LogoutClient(pServer->m_RconClientId, "");
	}
}

void CServer::ConShowIps(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(pServer->m_RconClientId >= 0 && pServer->m_RconClientId < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientId].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(pResult->NumArguments())
		{
			pServer->m_aClients[pServer->m_RconClientId].m_ShowIps = pResult->GetInteger(0);
		}
		else
		{
			char aStr[9];
			str_format(aStr, sizeof(aStr), "Value: %d", pServer->m_aClients[pServer->m_RconClientId].m_ShowIps);
			char aBuf[32];
			pServer->SendRconLine(pServer->m_RconClientId, pServer->Console()->Format(aBuf, sizeof(aBuf), "server", aStr));
		}
	}
}

void CServer::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		((CServer *)pUserData)->UpdateServerInfo(true);
	}
}

void CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		((CServer *)pUserData)->m_NetServer.SetMaxClientsPerIp(pResult->GetInteger(0));
	}
}

void CServer::ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() == 2)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		const IConsole::CCommandInfo *pInfo = pThis->Console()->GetCommandInfo(pResult->GetString(0), CFGFLAG_SERVER, false);
		int OldAccessLevel = 0;
		if(pInfo)
			OldAccessLevel = pInfo->GetAccessLevel();
		pfnCallback(pResult, pCallbackUserData);
		if(pInfo && OldAccessLevel != pInfo->GetAccessLevel())
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(pThis->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY || pThis->m_aClients[i].m_Authed != CServer::AUTHED_MOD ||
					(pThis->m_aClients[i].m_pRconCmdToSend && str_comp(pResult->GetString(0), pThis->m_aClients[i].m_pRconCmdToSend->m_pName) >= 0))
					continue;

				if(OldAccessLevel == IConsole::ACCESS_LEVEL_ADMIN)
					pThis->SendRconCmdAdd(pInfo, i);
				else
					pThis->SendRconCmdRem(pInfo, i);
			}
		}
	}
	else
	{
		pfnCallback(pResult, pCallbackUserData);
	}
}

void CServer::ConAddSqlServer(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	if(!MysqlAvailable())
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "can't add MySQL server: compiled without MySQL support");
		return;
	}

	if(!pSelf->Config()->m_SvUseSql)
		return;

	if(pResult->NumArguments() != 7 && pResult->NumArguments() != 8)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "7 or 8 arguments are required");
		return;
	}

	CMysqlConfig Config;
	bool Write;
	if(str_comp_nocase(pResult->GetString(0), "w") == 0)
		Write = false;
	else if(str_comp_nocase(pResult->GetString(0), "r") == 0)
		Write = true;
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return;
	}

	str_copy(Config.m_aDatabase, pResult->GetString(1), sizeof(Config.m_aDatabase));
	str_copy(Config.m_aPrefix, pResult->GetString(2), sizeof(Config.m_aPrefix));
	str_copy(Config.m_aUser, pResult->GetString(3), sizeof(Config.m_aUser));
	str_copy(Config.m_aPass, pResult->GetString(4), sizeof(Config.m_aPass));
	str_copy(Config.m_aIp, pResult->GetString(5), sizeof(Config.m_aIp));
	str_copy(Config.m_aBindaddr, Config.m_aBindaddr, sizeof(Config.m_aBindaddr));
	Config.m_Port = pResult->GetInteger(6);
	Config.m_Setup = pResult->NumArguments() == 8 ? pResult->GetInteger(7) : true;

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"Adding new Sql%sServer: DB: '%s' Prefix: '%s' User: '%s' IP: <{%s}> Port: %d",
		Write ? "Write" : "Read",
		Config.m_aDatabase, Config.m_aPrefix, Config.m_aUser, Config.m_aIp, Config.m_Port);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	pSelf->DbPool()->RegisterMysqlDatabase(Write ? CDbConnectionPool::WRITE : CDbConnectionPool::READ, &Config);
}

void CServer::ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	if(str_comp_nocase(pResult->GetString(0), "w") == 0)
	{
		pSelf->DbPool()->Print(pSelf->Console(), CDbConnectionPool::WRITE);
		pSelf->DbPool()->Print(pSelf->Console(), CDbConnectionPool::WRITE_BACKUP);
	}
	else if(str_comp_nocase(pResult->GetString(0), "r") == 0)
	{
		pSelf->DbPool()->Print(pSelf->Console(), CDbConnectionPool::READ);
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return;
	}
}

void CServer::LogoutClient(int ClientId, const char *pReason)
{
	CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
	Msg.AddInt(0); //authed
	Msg.AddInt(0); //cmdlist
	SendMsg(&Msg, MSGFLAG_VITAL, ClientId);

	m_aClients[ClientId].m_AuthTries = 0;
	m_aClients[ClientId].m_pRconCmdToSend = 0;

	char aBuf[64];
	if(*pReason)
	{
		str_format(aBuf, sizeof(aBuf), "Logged out by %s.", pReason);
		SendRconLine(ClientId, aBuf);
		str_format(aBuf, sizeof(aBuf), "ClientId=%d logged out by %s", ClientId, pReason);
	}
	else
	{
		SendRconLine(ClientId, "Logout successful.");
		str_format(aBuf, sizeof(aBuf), "ClientId=%d logged out", ClientId);
	}

	m_aClients[ClientId].m_Authed = AUTHED_NO;

	GameServer()->OnSetAuthed(ClientId, AUTHED_NO);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CServer::ConchainRconPasswordChangeGeneric(int Level, const char *pCurrent, IConsole::IResult *pResult)
{
}

void CServer::ConchainRconPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CServer *pThis = static_cast<CServer *>(pUserData);
	pThis->ConchainRconPasswordChangeGeneric(AUTHED_ADMIN, pThis->Config()->m_SvRconPassword, pResult);
	pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainRconModPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CServer *pThis = static_cast<CServer *>(pUserData);
	pThis->ConchainRconPasswordChangeGeneric(AUTHED_MOD, pThis->Config()->m_SvRconModPassword, pResult);
	pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainRconHelperPasswordChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CServer *pThis = static_cast<CServer *>(pUserData);
	pThis->ConchainRconPasswordChangeGeneric(AUTHED_HELPER, pThis->Config()->m_SvRconHelperPassword, pResult);
	pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainMapUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() >= 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->m_MapReload = str_comp(pThis->Config()->m_SvMap, pThis->m_aCurrentMap) != 0;
	}
}

void CServer::ConchainSixupUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CServer *pThis = static_cast<CServer *>(pUserData);
	if(pResult->NumArguments() >= 1 && pThis->m_aCurrentMap[0] != '\0')
		pThis->m_MapReload |= (pThis->m_apCurrentMapData[MAP_TYPE_SIXUP] != 0) != (pResult->GetInteger(0) != 0);
}

void CServer::ConchainLoglevel(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CServer *pSelf = (CServer *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		pSelf->m_pFileLogger->SetFilter(CLogFilter{IConsole::ToLogLevelFilter(g_Config.m_Loglevel)});
	}
}

void CServer::ConchainStdoutOutputLevel(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CServer *pSelf = (CServer *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pSelf->m_pStdoutLogger)
	{
		pSelf->m_pStdoutLogger->SetFilter(CLogFilter{IConsole::ToLogLevelFilter(g_Config.m_StdoutOutputLevel)});
	}
}

#if defined(CONF_FAMILY_UNIX)
void CServer::ConchainConnLoggingServerChange(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		CServer *pServer = (CServer *)pUserData;

		// open socket to send new connections
		if(!pServer->m_ConnLoggingSocketCreated)
		{
			pServer->m_ConnLoggingSocket = net_unix_create_unnamed();
			if(pServer->m_ConnLoggingSocket == -1)
			{
				pServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Failed to created socket for communication with the connection logging server.");
			}
			else
			{
				pServer->m_ConnLoggingSocketCreated = true;
			}
		}

		// set the destination address for the connection logging
		net_unix_set_addr(&pServer->m_ConnLoggingDestAddr, pResult->GetString(0));
	}
}
#endif

void CServer::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGameServer = Kernel()->RequestInterface<IGameServer>();
	m_pMap = Kernel()->RequestInterface<IEngineMap>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	Kernel()->RegisterInterface(static_cast<IHttp *>(&m_Http), false);

	// register console commands
	Console()->Register("kick", "i[id] ?r[reason]", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "?r[name]", CFGFLAG_SERVER, ConStatus, this, "List players containing name or all players");
	Console()->Register("shutdown", "?r[reason]", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("shutdown2", "?r[reason]", CFGFLAG_SERVER, ConShutdown2, this, "Shut down and reconnect clients");
	Console()->Register("logout", "", CFGFLAG_SERVER, ConLogout, this, "Logout of rcon");
	Console()->Register("show_ips", "?i[show]", CFGFLAG_SERVER, ConShowIps, this, "Show IP addresses in rcon commands (1 = on, 0 = off)");

	Console()->Register("record", "?s[file]", CFGFLAG_SERVER | CFGFLAG_STORE, ConRecord, this, "Record to a file");
	Console()->Register("stoprecord", "", CFGFLAG_SERVER, ConStopRecord, this, "Stop recording");

	Console()->Register("reload", "", CFGFLAG_SERVER, ConMapReload, this, "Reload the map");

	Console()->Register("add_sqlserver", "s['r'|'w'] s[Database] s[Prefix] s[User] s[Password] s[IP] i[Port] ?i[SetUpDatabase ?]", CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConAddSqlServer, this, "add a sqlserver");
	Console()->Register("dump_sqlservers", "s['r'|'w']", CFGFLAG_SERVER, ConDumpSqlServers, this, "dumps all sqlservers readservers = r, writeservers = w");

	Console()->Register("name_ban", "s[name] ?i[distance] ?i[is_substring] ?r[reason]", CFGFLAG_SERVER, ConNameBan, this, "Ban a certain nickname");
	Console()->Register("name_unban", "s[name]", CFGFLAG_SERVER, ConNameUnban, this, "Unban a certain nickname");
	Console()->Register("name_bans", "", CFGFLAG_SERVER, ConNameBans, this, "List all name bans");

	Console()->Chain("sv_name", ConchainSpecialInfoupdate, this);
	Console()->Chain("sv_hide_info", ConchainSpecialInfoupdate, this);
	Console()->Chain("password", ConchainSpecialInfoupdate, this);

	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("mod_command", ConchainModCommandUpdate, this);

	Console()->Chain("sv_map", ConchainMapUpdate, this);
	Console()->Chain("sv_sixup", ConchainSixupUpdate, this);

	Console()->Chain("loglevel", ConchainLoglevel, this);
	Console()->Chain("stdout_output_level", ConchainStdoutOutputLevel, this);

#if defined(CONF_FAMILY_UNIX)
	Console()->Chain("sv_conn_logging_server", ConchainConnLoggingServerChange, this);
#endif

	Console()->Register("mute", "s[ClientId] ?i[minutes] ?r[reason]", CFGFLAG_SERVER, ConMute, this, "Mute player with specified id for x minutes for any reason");
	Console()->Register("unmute", "s[ClientId]", CFGFLAG_SERVER, ConUnmute, this, "Unmute player with specified id");
	Console()->Register("whisper", "s[id] r[txt]", CFGFLAG_SERVER, ConWhisper, this, "Analogous to 'Say' but sent to a single client only");

	// register console commands in sub parts
	m_ServerBan.InitServerBan(Console(), Storage(), this);
	m_NetSession.Init();
	m_NetAccusation.Init();
	m_pGameServer->OnConsoleInit();
}


int CServer::SnapNewId()
{
	return m_IdPool.NewId();
}

void CServer::SnapFreeId(int Id)
{
	m_IdPool.FreeId(Id);
}

void *CServer::SnapNewItem(int Type, int Id, int Size)
{
	dbg_assert(Id >= -1 && Id <= 0xffff, "incorrect id");
	return Id < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, Id, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

int CServer::GetClientInfclassVersion(int ClientId) const
{
	if(ClientId == SERVER_DEMO_CLIENT)
	{
		return 1000;
	}

	if(m_aClients[ClientId].m_State == CClient::STATE_INGAME)
	{
		return m_aClients[ClientId].m_InfClassVersion;
	}

	return 0;
}

CServer *CreateServer() { return new CServer(); }

// DDRace

void CServer::GetClientAddr(int ClientId, NETADDR *pAddr) const
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && m_aClients[ClientId].m_State == CClient::STATE_INGAME)
	{
		*pAddr = *m_NetServer.ClientAddr(ClientId);
	}
}

const char *CServer::GetAnnouncementLine(char const *pFileName)
{
	if(str_comp(pFileName, m_aAnnouncementFile) != 0)
	{
		str_copy(m_aAnnouncementFile, pFileName);
		m_vAnnouncements.clear();

		CLineReader LineReader;
		if(!LineReader.OpenFile(m_pStorage->OpenFile(pFileName, IOFLAG_READ, IStorage::TYPE_ALL)))
		{
			return 0;
		}
		while(const char *pLine = LineReader.Get())
		{
			if(str_length(pLine) && pLine[0] != '#')
			{
				m_vAnnouncements.emplace_back(pLine);
			}
		}
	}

	if(m_vAnnouncements.empty())
	{
		return 0;
	}
	else if(m_vAnnouncements.size() == 1)
	{
		m_AnnouncementLastLine = 0;
	}
	else if(!Config()->m_SvAnnouncementRandom)
	{
		if(++m_AnnouncementLastLine >= m_vAnnouncements.size())
			m_AnnouncementLastLine %= m_vAnnouncements.size();
	}
	else
	{
		unsigned Rand;
		do
		{
			Rand = rand() % m_vAnnouncements.size();
		} while(Rand == m_AnnouncementLastLine);

		m_AnnouncementLastLine = Rand;
	}

	return m_vAnnouncements[m_AnnouncementLastLine].c_str();
}

/* INFECTION MODIFICATION START ***************************************/
const char* CServer::GetClientLanguage(int ClientId)
{
	return m_aClients[ClientId].m_aLanguage;
}

void CServer::SetClientLanguage(int ClientId, const char* pLanguage)
{
	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(m_NetServer.ClientAddr(ClientId), aAddrStr, sizeof(aAddrStr), true);

	dbg_msg("lang", "set_language ClientId=%d lang=%s addr=%s", ClientId, pLanguage, aAddrStr);
	str_copy(m_aClients[ClientId].m_aLanguage, pLanguage, sizeof(m_aClients[ClientId].m_aLanguage));
}

int CServer::GetClientNbRound(int ClientId)
{
	return m_aClients[ClientId].m_NbRound;
}

bool CServer::IsClientLogged(int ClientId)
{
	return m_aClients[ClientId].m_UserId >= 0;
}

#ifdef CONF_SQL
void CServer::AddGameServerCmd(CGameServerCmd* pCmd)
{
	lock_wait(m_GameServerCmdLock);
	m_lGameServerCmds.add(pCmd);
	lock_release(m_GameServerCmdLock);
}

class CGameServerCmd_SendChatMOTD : public CServer::CGameServerCmd
{
private:
	int m_ClientId;
	char m_aText[512];
	
public:
	CGameServerCmd_SendChatMOTD(int ClientId, const char* pText)
	{
		m_ClientId = ClientId;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendMOTD(m_ClientId, m_aText);
	}
};

class CGameServerCmd_SendChatTarget : public CServer::CGameServerCmd
{
private:
	int m_ClientId;
	char m_aText[128];
	
public:
	CGameServerCmd_SendChatTarget(int ClientId, const char* pText)
	{
		m_ClientId = ClientId;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendChatTarget(m_ClientId, m_aText);
	}
};

class CGameServerCmd_SendChatTarget_Language : public CServer::CGameServerCmd
{
private:
	int m_ClientId;
	int m_ChatCategory;
	char m_aText[128];
	
public:
	CGameServerCmd_SendChatTarget_Language(int ClientId, int ChatCategory, const char* pText)
	{
		m_ClientId = ClientId;
		m_ChatCategory = ChatCategory;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendChatTarget_Localization(m_ClientId, m_ChatCategory, m_aText, NULL);
	}
};

class CSqlJob_Server_Login : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientId;
	CSqlString<64> m_sName;
	CSqlString<64> m_sPasswordHash;
	
public:
	CSqlJob_Server_Login(CServer* pServer, int ClientId, const char* pName, const char* pPasswordHash)
	{
		m_pServer = pServer;
		m_ClientId = ClientId;
		m_sName = CSqlString<64>(pName);
		m_sPasswordHash = CSqlString<64>(pPasswordHash);
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		
		try
		{	
			//Check for username/password
			str_format(aBuf, sizeof(aBuf), 
				"SELECT UserId, Level FROM %s_Users "
				"WHERE Username = '%s' AND PasswordHash = '%s';"
				, pSqlServer->GetPrefix(), m_sName.ClrStr(), m_sPasswordHash.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{
				//The client is still the same
				if(m_pServer->m_aClients[m_ClientId].m_LogInstance == GetInstance() && m_pServer->m_aClients[m_ClientId].m_UserId == -1)
				{
					int UserId = (int)pSqlServer->GetResults()->getInt("UserId");
					int UserLevel = (int)pSqlServer->GetResults()->getInt("Level");
					m_pServer->m_aClients[m_ClientId].m_UserId = UserId;
					m_pServer->m_aClients[m_ClientId].m_UserLevel = UserLevel;

					char aOldName[MAX_NAME_LENGTH];
					str_copy(aOldName, m_pServer->m_aClients[m_ClientId].m_aName, sizeof(aOldName));
					str_copy(m_pServer->m_aClients[m_ClientId].m_aUsername, m_sName.Str(), sizeof(m_pServer->m_aClients[m_ClientId].m_aUsername));

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "change_name previous='%s' now='%s'", aOldName, m_pServer->m_aClients[m_ClientId].m_aUsername);
					m_pServer->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

					//If we are really unlucky, the client can deconnect and another one connect during this small code
					if(m_pServer->m_aClients[m_ClientId].m_LogInstance != GetInstance())
					{
						m_pServer->m_aClients[m_ClientId].m_UserId = -1;
						m_pServer->m_aClients[m_ClientId].m_UserLevel = SQL_USERLEVEL_NORMAL;
					}
					else
					{
						str_format(aBuf, sizeof(aBuf), "%s logged in (id: %d)", m_pServer->m_aClients[m_ClientId].m_aUsername,
							m_pServer->m_aClients[m_ClientId].m_UserId);
						CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(-1, aBuf);
						m_pServer->AddGameServerCmd(pCmd);
					}
				}
				else {
					str_format(aBuf, sizeof(aBuf), "You are already logged in.");
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientId, aBuf);
					m_pServer->AddGameServerCmd(pCmd);
				}
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("Wrong username/password."));
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, "An error occured during the logging.");
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't check username/password (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
	
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientId].m_LogInstance = -1;
	}
};

void CServer::Login(int ClientId, const char* pUsername, const char* pPassword)
{
	if(m_aClients[ClientId].m_LogInstance >= 0)
		return;
	
	char aHash[64]; //Result
	mem_zero(aHash, sizeof(aHash));
	Crypt(pPassword, (const unsigned char*) "d9", 1, 16, aHash);
	
	CSqlJob* pJob = new CSqlJob_Server_Login(this, ClientId, pUsername, aHash);
	m_aClients[ClientId].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

void CServer::Logout(int ClientId)
{
	m_aClients[ClientId].m_UserId = -1;
	m_aClients[ClientId].m_UserLevel = SQL_USERLEVEL_NORMAL;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "change_name previous='%s' now='%s'", m_aClients[ClientId].m_aUsername, m_aClients[ClientId].m_aName);
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

class CSqlJob_Server_SetEmail : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientId;
	int m_UserId;
	CSqlString<64> m_sEmail;
	
public:
	CSqlJob_Server_SetEmail(CServer* pServer, int ClientId, int UserId, const char* pEmail)
	{
		m_pServer = pServer;
		m_ClientId = ClientId;
		m_UserId = UserId;
		m_sEmail = CSqlString<64>(pEmail);
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"UPDATE %s_Users "
				"SET Email = '%s' "
				"WHERE UserId = '%d';"
				, pSqlServer->GetPrefix(), m_sEmail.ClrStr(), m_UserId);
			
			pSqlServer->executeSqlQuery(aBuf);
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("An error occured during the operation."));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't change email (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};

void CServer::SetEmail(int ClientId, const char* pEmail)
{
	if(m_aClients[ClientId].m_UserId < 0 && m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("You must be logged for this operation"), NULL);
	}
	else
	{
		CSqlJob* pJob = new CSqlJob_Server_SetEmail(this, ClientId, m_aClients[ClientId].m_UserId, pEmail);
		pJob->Start();
	}
}

class CSqlJob_Server_Register : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientId;
	CSqlString<64> m_sName;
	CSqlString<64> m_sPasswordHash;
	CSqlString<64> m_sEmail;
	
public:
	CSqlJob_Server_Register(CServer* pServer, int ClientId, const char* pName, const char* pPasswordHash, const char* pEmail)
	{
		m_pServer = pServer;
		m_ClientId = ClientId;
		m_sName = CSqlString<64>(pName);
		m_sPasswordHash = CSqlString<64>(pPasswordHash);
		if(pEmail)
			m_sEmail = CSqlString<64>(pEmail);
		else
			m_sEmail = CSqlString<64>("");
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		char aAddrStr[64];
		
		//Drop the registration if the client leave because we will not be able to detect register flooding
		if(m_pServer->m_aClients[m_ClientId].m_LogInstance != GetInstance())
			return true;
		
		net_addr_str(m_pServer->m_NetServer.ClientAddr(m_ClientId), aAddrStr, sizeof(aAddrStr), false);
		
		try
		{
			//Check for registration flooding
			str_format(aBuf, sizeof(aBuf), 
				"SELECT UserId FROM %s_Users "
				"WHERE RegisterIp = '%s' AND TIMESTAMPDIFF(MINUTE, RegisterDate, UTC_TIMESTAMP()) < 5;"
				, pSqlServer->GetPrefix(), aAddrStr);
			pSqlServer->executeSqlQuery(aBuf);
			
			if(pSqlServer->GetResults()->next())
			{
				dbg_msg("infclass", "Registration flooding");
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("Please wait 5 minutes before creating another account"));
				m_pServer->AddGameServerCmd(pCmd);
				
				return true;
			}
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't check username existance (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		try
		{
			//Check if the username is already taken
			str_format(aBuf, sizeof(aBuf), 
				"SELECT UserId FROM %s_Users "
				"WHERE Username COLLATE UTF8_GENERAL_CI = '%s' COLLATE UTF8_GENERAL_CI;"
				, pSqlServer->GetPrefix(), m_sName.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{
				dbg_msg("infclass", "User already taken");
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("This username is already taken by an existing account"));
				m_pServer->AddGameServerCmd(pCmd);
				
				return true;
			}
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't check username existance (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		//Create the account
		try
		{	
			str_format(aBuf, sizeof(aBuf), 
				"INSERT INTO %s_Users "
				"(Username, PasswordHash, Email, RegisterDate, RegisterIp) "
				"VALUES ('%s', '%s', '%s', UTC_TIMESTAMP(), '%s');"
				, pSqlServer->GetPrefix(), m_sName.ClrStr(), m_sPasswordHash.ClrStr(), m_sEmail.ClrStr(), aAddrStr);
			pSqlServer->executeSql(aBuf);
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't create new user (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		//Get the new user
		try
		{	
			str_format(aBuf, sizeof(aBuf), 
				"SELECT UserId FROM %s_Users "
				"WHERE Username = '%s' AND PasswordHash = '%s';"
				, pSqlServer->GetPrefix(), m_sName.ClrStr(), m_sPasswordHash.ClrStr());
			pSqlServer->executeSqlQuery(aBuf);

			if(pSqlServer->GetResults()->next())
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("Your account has been created and you are now logged."));
				m_pServer->AddGameServerCmd(pCmd);
							
				//The client is still the same
				if(m_pServer->m_aClients[m_ClientId].m_LogInstance == GetInstance())
				{
					int UserId = (int)pSqlServer->GetResults()->getInt("UserId");
					m_pServer->m_aClients[m_ClientId].m_UserId = UserId;
					str_copy(m_pServer->m_aClients[m_ClientId].m_aUsername, m_sName.Str(), sizeof(m_pServer->m_aClients[m_ClientId].m_aUsername));
					
					//If we are really unlucky, the client can deconnect and another one connect during this small code
					if(m_pServer->m_aClients[m_ClientId].m_LogInstance != GetInstance())
					{
						m_pServer->m_aClients[m_ClientId].m_UserId = -1;
					}
				}
				
				return true;
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
				m_pServer->AddGameServerCmd(pCmd);
				
				return false;
			}
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientId, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't get the ID of the new user (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
	
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientId].m_LogInstance = -1;
	}
};

void CServer::Register(int ClientId, const char* pUsername, const char* pPassword, const char* pEmail)
{
	if(m_aClients[ClientId].m_LogInstance >= 0)
		return;
	
	char aHash[64]; //Result
	mem_zero(aHash, sizeof(aHash));
	Crypt(pPassword, (const unsigned char*) "d9", 1, 16, aHash);
	
	CSqlJob* pJob = new CSqlJob_Server_Register(this, ClientId, pUsername, aHash, pEmail);
	m_aClients[ClientId].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

class CSqlJob_Server_ShowTop10 : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientId;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowTop10(CServer* pServer, const char* pMapName, int ClientId, int ScoreType)
	{
		m_pServer = pServer;
		m_sMapName = CSqlString<64>(pMapName);
		m_ClientId = ClientId;
		m_ScoreType = ScoreType;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[1024];
		
		try
		{
			//Get the top 10 with this very simple, intuitive and optimized SQL function >_<
			pSqlServer->executeSql("SET @VarRowNum := 0, @VarType := -1");
			str_format(aBuf, sizeof(aBuf), 
				"SELECT "
					"TableUsers.Username, "
					"SUM(y.Score) AS AccumulatedScore, "
					"COUNT(y.Score) AS NbRounds "
				"FROM ("
					"SELECT "
						"x.UserId AS UserId, "
						"x.Score AS Score, "
						"@VarRowNum := IF(@VarType = x.UserId, @VarRowNum + 1, 1) as RowNumber, "
						"@VarType := x.UserId AS dummy "
					"FROM ("
						"SELECT "
							"TableRoundScore.UserId, "
							"TableRoundScore.Score "
						"FROM %s_infc_RoundScore AS TableRoundScore "
						"WHERE ScoreType = '%d' AND MapName = '%s' "
						"ORDER BY TableRoundScore.UserId ASC, TableRoundScore.Score DESC "
					") AS x "
				") AS y "
				"INNER JOIN %s_Users AS TableUsers ON y.UserId = TableUsers.UserId "
				"WHERE y.RowNumber <= %d "
				"GROUP BY y.UserId "
				"ORDER BY AccumulatedScore DESC, y.UserId ASC "
				"LIMIT 10"
				, pSqlServer->GetPrefix()
				, m_ScoreType
				, m_sMapName.ClrStr()
				, pSqlServer->GetPrefix()
				, SQL_SCORE_NUMROUND
			);
			pSqlServer->executeSqlQuery(aBuf);
			
			char* pMOTD = aBuf;
			
			switch(m_ScoreType)
			{
				case SQL_SCORETYPE_ENGINEER_SCORE:
					str_copy(pMOTD, "== Best Engineer ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_SOLDIER_SCORE:
					str_copy(pMOTD, "== Best Soldier ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_SCIENTIST_SCORE:
					str_copy(pMOTD, "== Best Scientist ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_BIOLOGIST_SCORE:
					str_copy(pMOTD, "== Best Biologist ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_LOOPER_SCORE:
					str_copy(pMOTD, "== Best Looper ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_MEDIC_SCORE:
					str_copy(pMOTD, "== Best Medic ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_HERO_SCORE:
					str_copy(pMOTD, "== Best Hero ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_NINJA_SCORE:
					str_copy(pMOTD, "== Best Ninja ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_MERCENARY_SCORE:
					str_copy(pMOTD, "== Best Mercenary ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_SNIPER_SCORE:
					str_copy(pMOTD, "== Best Sniper ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_SMOKER_SCORE:
					str_copy(pMOTD, "== Best Smoker ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_HUNTER_SCORE:
					str_copy(pMOTD, "== Best Hunter ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_BOOMER_SCORE:
					str_copy(pMOTD, "== Best Boomer ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_GHOST_SCORE:
					str_copy(pMOTD, "== Best Ghost ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_SPIDER_SCORE:
					str_copy(pMOTD, "== Best Spider ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_GHOUL_SCORE:
					str_copy(pMOTD, "== Best Ghoul ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_SLUG_SCORE:
					str_copy(pMOTD, "== Best Slug ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_UNDEAD_SCORE:
					str_copy(pMOTD, "== Best Undead ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_WITCH_SCORE:
					str_copy(pMOTD, "== Best Witch ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
				case SQL_SCORETYPE_ROUND_SCORE:
					str_copy(pMOTD, "== Best Player ==\n32 best scores on this map\n\n", sizeof(aBuf)-(pMOTD-aBuf));
					pMOTD += str_length(pMOTD);
					break;
			}
			
			int Rank = 0;
			while(pSqlServer->GetResults()->next())
			{
				Rank++;
				str_format(pMOTD, sizeof(aBuf)-(pMOTD-aBuf), "%d. %s: %d pts\n",
					Rank,
					pSqlServer->GetResults()->getString("Username").c_str(),
					pSqlServer->GetResults()->getInt("AccumulatedScore")/10
				);
				pMOTD += str_length(pMOTD);
			}
			str_copy(pMOTD, "\nCreate an account with /register and try to beat them!", sizeof(aBuf)-(pMOTD-aBuf));
			
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatMOTD(m_ClientId, aBuf);
			m_pServer->AddGameServerCmd(pCmd);
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("sql", "Can't get top10 (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};

void CServer::ShowTop10(int ClientId, int ScoreType)
{
	CSqlJob* pJob = new CSqlJob_Server_ShowTop10(this, m_aCurrentMap, ClientId, ScoreType);
	pJob->Start();
}

class CSqlJob_Server_ShowChallenge : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientId;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowChallenge(CServer* pServer, const char* pMapName, int ClientId, int ChallengeType)
	{
		m_pServer = pServer;
		m_ScoreType = ChallengeTypeToScoreType(ChallengeType);
		m_ClientId = ClientId;
		m_sMapName = CSqlString<64>(pMapName);
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aMotdBuf[1024];
		char aBuf[1024];
		
		try
		{
			char* pMOTD = aMotdBuf;
			
			if(m_ScoreType >= 0)
			{
				str_format(aBuf, sizeof(aBuf), 
					"SELECT "
						"TableUsers.Username, "
						"TableScore.Score "
					"FROM %s_infc_RoundScore AS TableScore "
					"INNER JOIN %s_Users AS TableUsers ON TableScore.UserId = TableUsers.UserId "
					"WHERE DATE(TableScore.ScoreDate) = DATE(UTC_TIMESTAMP()) AND TableScore.ScoreType = %d "
					"ORDER BY TableScore.Score DESC "
					"LIMIT 5"
					, pSqlServer->GetPrefix()
					, pSqlServer->GetPrefix()
					, m_ScoreType
				);
				pSqlServer->executeSqlQuery(aBuf);
				
				switch(m_ScoreType)
				{
					case SQL_SCORETYPE_ROUND_SCORE:
						str_copy(pMOTD, "== Player of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_ENGINEER_SCORE:
						str_copy(pMOTD, "== Engineer of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_SOLDIER_SCORE:
						str_copy(pMOTD, "== Soldier of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_SCIENTIST_SCORE:
						str_copy(pMOTD, "== Scientist of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_BIOLOGIST_SCORE:
						str_copy(pMOTD, "== Biologist of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_LOOPER_SCORE:
						str_copy(pMOTD, "== Looper of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_MEDIC_SCORE:
						str_copy(pMOTD, "== Medic of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_HERO_SCORE:
						str_copy(pMOTD, "== Hero of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_NINJA_SCORE:
						str_copy(pMOTD, "== Ninja of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_SNIPER_SCORE:
						str_copy(pMOTD, "== Sniper of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
					case SQL_SCORETYPE_MERCENARY_SCORE:
						str_copy(pMOTD, "== Mercenary of the day ==\nBest score in one round\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
						break;
				}
				pMOTD += str_length(pMOTD);
				
				int Rank = 0;
				while(pSqlServer->GetResults()->next())
				{
					Rank++;
					str_format(pMOTD, sizeof(aMotdBuf)-(pMOTD-aMotdBuf), "%d. %s: %d pts\n",
						Rank,
						pSqlServer->GetResults()->getString("Username").c_str(),
						pSqlServer->GetResults()->getInt("Score")/10
					);
					pMOTD += str_length(pMOTD);
				}
			}
			
			{
				//Get the top 5 with this very simple, intuitive and optimized SQL function >_<
				pSqlServer->executeSql("SET @VarRowNum := 0, @VarType := -1");
				str_format(aBuf, sizeof(aBuf), 
					"SELECT "
						"TableUsers.Username, "
						"SUM(y.Score) AS AccumulatedScore, "
						"COUNT(y.Score) AS NbRounds "
					"FROM ("
						"SELECT "
							"x.UserId AS UserId, "
							"x.Score AS Score, "
							"@VarRowNum := IF(@VarType = x.UserId, @VarRowNum + 1, 1) as RowNumber, "
							"@VarType := x.UserId AS dummy "
						"FROM ("
							"SELECT "
								"TableRoundScore.UserId, "
								"TableRoundScore.Score "
							"FROM %s_infc_RoundScore AS TableRoundScore "
							"WHERE ScoreType = '%d' AND MapName = '%s' "
							"ORDER BY TableRoundScore.UserId ASC, TableRoundScore.Score DESC "
						") AS x "
					") AS y "
					"INNER JOIN %s_Users AS TableUsers ON y.UserId = TableUsers.UserId "
					"WHERE y.RowNumber <= %d "
					"GROUP BY y.UserId "
					"ORDER BY AccumulatedScore DESC, y.UserId ASC "
					"LIMIT 5"
					, pSqlServer->GetPrefix()
					, SQL_SCORETYPE_ROUND_SCORE
					, m_sMapName.ClrStr()
					, pSqlServer->GetPrefix()
					, SQL_SCORE_NUMROUND
				);
				pSqlServer->executeSqlQuery(aBuf);
				
				str_copy(pMOTD, "\n== Best Players ==\n32 best scores on this map\n\n", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
				pMOTD += str_length(pMOTD);
				
				int Rank = 0;
				while(pSqlServer->GetResults()->next())
				{
					Rank++;
					str_format(pMOTD, sizeof(aMotdBuf)-(pMOTD-aMotdBuf), "%d. %s: %d pts\n",
						Rank,
						pSqlServer->GetResults()->getString("Username").c_str(),
						pSqlServer->GetResults()->getInt("AccumulatedScore")/10
					);
					pMOTD += str_length(pMOTD);
				}
			}
			
			str_copy(pMOTD, "\n\nCreate an account with /register and try to beat them!", sizeof(aMotdBuf)-(pMOTD-aMotdBuf));
			
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatMOTD(m_ClientId, aMotdBuf);
			m_pServer->AddGameServerCmd(pCmd);
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("sql", "Can't get challenge (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};

void CServer::ShowChallenge(int ClientId)
{
	if(g_Config.m_InfChallenge)
	{
		int ChallengeType;
		lock_wait(m_ChallengeLock);
		ChallengeType = m_ChallengeType;
		lock_release(m_ChallengeLock);
		
		CSqlJob* pJob = new CSqlJob_Server_ShowChallenge(this, m_aCurrentMap, ClientId, ChallengeType);
		pJob->Start();
	}
}

class CSqlJob_Server_RefreshChallenge : public CSqlJob
{
private:
	CServer* m_pServer;
	
public:
	CSqlJob_Server_RefreshChallenge(CServer* pServer)
	{
		m_pServer = pServer;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[1024];
		char aWinner[32];
		int ChallengeType = m_pServer->m_ChallengeType;
		int ScoreType;
		
		aWinner[0] = 0;
		
		try
		{
			//Get the day
			str_format(aBuf, sizeof(aBuf), "SELECT WEEKDAY(UTC_TIMESTAMP()) AS WeekDay, WEEK(UTC_TIMESTAMP()) AS Week");
			pSqlServer->executeSqlQuery(aBuf);
			
			if(pSqlServer->GetResults()->next())
			{
				int CurrentDay = pSqlServer->GetResults()->getInt("WeekDay");
				int CurrentWeek = pSqlServer->GetResults()->getInt("Week");
				ChallengeType = (CurrentWeek*7 + CurrentDay)%NB_HUMANCLASS;
			}
			
			ScoreType = ChallengeTypeToScoreType(ChallengeType);
			
			str_format(aBuf, sizeof(aBuf), 
				"SELECT "
					"TableUsers.Username "
				"FROM %s_infc_RoundScore AS TableScore "
				"INNER JOIN %s_Users AS TableUsers ON TableScore.UserId = TableUsers.UserId "
				"WHERE DATE(TableScore.ScoreDate) = DATE(UTC_TIMESTAMP()) AND TableScore.ScoreType = %d "
				"ORDER BY TableScore.Score DESC "
				"LIMIT 1"
				, pSqlServer->GetPrefix()
				, pSqlServer->GetPrefix()
				, ScoreType
			);
			pSqlServer->executeSqlQuery(aBuf);
			
			if(pSqlServer->GetResults()->next())
			{
				str_copy(aWinner, pSqlServer->GetResults()->getString("Username").c_str(), sizeof(aWinner));
			}
			
			lock_wait(m_pServer->m_ChallengeLock);
			m_pServer->m_ChallengeType = ChallengeType;
			str_copy(m_pServer->m_aChallengeWinner, aWinner, sizeof(m_pServer->m_aChallengeWinner));
			lock_release(m_pServer->m_ChallengeLock);
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("sql", "Can't refresh challenge (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};

void CServer::RefreshChallenge()
{
	if(g_Config.m_InfChallenge)
	{
		CSqlJob* pJob = new CSqlJob_Server_RefreshChallenge(this);
		pJob->Start();
	}
}

class CSqlJob_Server_ShowRank : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientId;
	int m_UserId;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowRank(CServer* pServer, const char* pMapName, int ClientId, int UserId, int ScoreType)
	{
		m_pServer = pServer;
		m_sMapName = CSqlString<64>(pMapName);
		m_ClientId = ClientId;
		m_UserId = UserId;
		m_ScoreType = ScoreType;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[1024];
		
		try
		{
			//Get the top 10 with this very simple, intuitive and optimized SQL fuction >_<
			pSqlServer->executeSql("SET @VarRowNum := 0, @VarType := -1");
			str_format(aBuf, sizeof(aBuf), 
				"SELECT "
					"y.UserId, "
					"SUM(y.Score) AS AccumulatedScore, "
					"COUNT(y.Score) AS NbRounds "
				"FROM ("
					"SELECT "
						"x.UserId AS UserId, "
						"x.Score AS Score, "
						"@VarRowNum := IF(@VarType = x.UserId, @VarRowNum + 1, 1) as RowNumber, "
						"@VarType := x.UserId AS dummy "
					"FROM ("
						"SELECT "
							"TableRoundScore.UserId, "
							"TableRoundScore.Score "
						"FROM %s_infc_RoundScore AS TableRoundScore "
						"WHERE ScoreType = '%d' AND MapName = '%s' "
						"ORDER BY TableRoundScore.UserId ASC, TableRoundScore.Score DESC "
					") AS x "
				") AS y "
				"WHERE y.RowNumber <= %d "
				"GROUP BY y.UserId "
				"ORDER BY AccumulatedScore DESC, y.UserId ASC "
				, pSqlServer->GetPrefix()
				, m_ScoreType
				, m_sMapName.ClrStr()
				, SQL_SCORE_NUMROUND
			);
			pSqlServer->executeSqlQuery(aBuf);
			
			int Rank = 0;
			bool RankFound = false;
			while(pSqlServer->GetResults()->next() && !RankFound)
			{
				Rank++;
				if(pSqlServer->GetResults()->getInt("UserId") == m_UserId)
				{
					int Score = pSqlServer->GetResults()->getInt("AccumulatedScore")/10;
					int Rounds = pSqlServer->GetResults()->getInt("NbRounds");
					str_format(aBuf, sizeof(aBuf), "You are rank %d in %s (%d pts in %d rounds)", Rank, m_sMapName.Str(), Score, Rounds);
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientId, aBuf);
					m_pServer->AddGameServerCmd(pCmd);
					
					RankFound = true;
				}
			}
			
			if(!RankFound)
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientId, "You must gain at least one point to see your rank");
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("sql", "Can't get rank (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};

void CServer::ShowRank(int ClientId, int ScoreType)
{
	if(m_aClients[ClientId].m_UserId >= 0)
	{
		CSqlJob* pJob = new CSqlJob_Server_ShowRank(this, m_aCurrentMap, ClientId, m_aClients[ClientId].m_UserId, ScoreType);
		pJob->Start();
	}
	else if(m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("You must be logged to see your rank"), NULL);
	}
}

class CSqlJob_Server_ShowGoal : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientId;
	int m_UserId;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowGoal(CServer* pServer, const char* pMapName, int ClientId, int UserId, int ScoreType)
	{
		m_pServer = pServer;
		m_sMapName = CSqlString<64>(pMapName);
		m_ClientId = ClientId;
		m_UserId = UserId;
		m_ScoreType = ScoreType;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[1024];
		
		try
		{
			//Get the list of best rounds
			str_format(aBuf, sizeof(aBuf), 
				"SELECT Score "
				"FROM %s_infc_RoundScore "
				"WHERE UserId = '%d' AND MapName = '%s' AND ScoreType = '%d' "
				"ORDER BY Score DESC "
				"LIMIT %d "
				, pSqlServer->GetPrefix()
				, m_UserId
				, m_sMapName.ClrStr()
				, m_ScoreType
				, SQL_SCORE_NUMROUND
			);
			pSqlServer->executeSqlQuery(aBuf);
			
			int RoundCounter = 0;
			int Score = 0;
			while(pSqlServer->GetResults()->next())
			{
				Score = pSqlServer->GetResults()->getInt("Score")/10;
				RoundCounter++;
			}
			
			if(RoundCounter == SQL_SCORE_NUMROUND)
			{
				str_format(aBuf, sizeof(aBuf), "You must gain at least %d points to increase your score", (Score+1)); 
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientId, aBuf);
				m_pServer->AddGameServerCmd(pCmd);
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientId, "Gain at least one point to increase your score");
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("sql", "Can't get rank (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};

void CServer::ShowGoal(int ClientId, int ScoreType)
{
	if(m_aClients[ClientId].m_UserId >= 0)
	{
		CSqlJob* pJob = new CSqlJob_Server_ShowGoal(this, m_aCurrentMap, ClientId, m_aClients[ClientId].m_UserId, ScoreType);
		pJob->Start();
	}
	else if(m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("You must be logged to see your goal"), NULL);
	}
}

class CSqlJob_Server_ShowStats : public CSqlJob // under konstruktion (copypasted draft)
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientId;
	int m_UserId;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowStats(CServer* pServer, const char* pMapName, int ClientId, int UserId, int ScoreType)
	{
		m_pServer = pServer;
		m_sMapName = CSqlString<64>(pMapName);
		m_ClientId = ClientId;
		m_UserId = UserId;
		m_ScoreType = ScoreType;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[1024];
		
		try
		{
			//Get the list of best rounds
			str_format(aBuf, sizeof(aBuf), 
				"SELECT Score "
				"FROM %s_infc_RoundScore "
				"WHERE UserId = '%d' AND MapName = '%s' AND ScoreType = '%d' "
				"ORDER BY Score DESC "
				"LIMIT %d "
				, pSqlServer->GetPrefix()
				, m_UserId
				, m_sMapName.ClrStr()
				, m_ScoreType
				, SQL_SCORE_NUMROUND
			);
			pSqlServer->executeSqlQuery(aBuf);
			
			int RoundCounter = 0;
			int Score = 0;
			while(pSqlServer->GetResults()->next())
			{
				Score = pSqlServer->GetResults()->getInt("Score")/10;
				RoundCounter++;
			}
			
			if(RoundCounter == SQL_SCORE_NUMROUND)
			{
				str_format(aBuf, sizeof(aBuf), "Stats - You must gain at least %d points to increase your score", (Score+1)); 
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientId, aBuf);
				m_pServer->AddGameServerCmd(pCmd);
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientId, "stats Gain at least one point to increase your score");
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("sql", "Can't get rank (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};

void CServer::ShowStats(int ClientId, int UserId)
{
	if(m_aClients[ClientId].m_UserId >= 0)
	{
		CSqlJob* pJob = new CSqlJob_Server_ShowStats(this, m_aCurrentMap, ClientId, m_aClients[ClientId].m_UserId, UserId);
		pJob->Start();
	}
	else if(m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("You must be logged to see stats"), NULL);
	}
}

#endif

void CServer::Register(int ClientId, const char* pUsername, const char* pPassword, const char* pEmail)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "register request=%d login='%s' password='%s'", m_LastRegistrationRequestId, pUsername, pPassword);
	++m_LastRegistrationRequestId;

	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "registration", aBuf);
}

void CServer::Login(int ClientId, const char *pUsername, const char *pPassword)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "login request=%d login='%s' password='%s'", m_LastRegistrationRequestId, pUsername, pPassword);
	++m_LastRegistrationRequestId;

	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "registration", aBuf);
}

void CServer::Logout(int ClientId)
{
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "registration", "logout");
}

void CServer::Ban(int ClientId, int Seconds, const char* pReason)
{
	m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientId), Seconds, pReason);
}

#ifdef CONF_SQL

class CSqlJob_Server_SendRoundStatistics : public CSqlJob
{
private:
	CSqlString<64> m_sMapName;
	int m_NumPlayersMin;
	int m_NumPlayersMax;
	int m_RoundDuration;
	int m_NumWinners;
	
	int m_RoundId;
	
public:
	CSqlJob_Server_SendRoundStatistics(CServer* pServer, const CRoundStatistics* pRoundStatistics, const char* pMapName)
	{
		m_sMapName = CSqlString<64>(pMapName);
		m_NumPlayersMin = pRoundStatistics->m_NumPlayersMin;
		m_NumPlayersMax = pRoundStatistics->m_NumPlayersMax;
		m_NumWinners = pRoundStatistics->NumWinners();
		m_RoundDuration = pRoundStatistics->m_PlayedTicks/pServer->TickSpeed();
		m_RoundId = -1;
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		
		try
		{
			str_format(aBuf, sizeof(aBuf), 
				"INSERT INTO %s_infc_Rounds "
				"(MapName, NumPlayersMin, NumPlayersMax, NumWinners, RoundDate, RoundDuration) "
				"VALUES "
				"('%s', '%d', '%d', '%d', UTC_TIMESTAMP(), '%d')"
				, pSqlServer->GetPrefix(), m_sMapName.ClrStr(), m_NumPlayersMin, m_NumPlayersMax, m_NumWinners, m_RoundDuration);
			pSqlServer->executeSql(aBuf);
			
			//Get old score
			str_format(aBuf, sizeof(aBuf), 
				"SELECT RoundId FROM %s_infc_Rounds "
				"WHERE RoundDuration = '%d' AND NumPlayersMin = '%d' AND NumPlayersMax = '%d'"
				"ORDER BY RoundId DESC LIMIT 1"
				, pSqlServer->GetPrefix(), m_RoundDuration, m_NumPlayersMin, m_NumPlayersMax);
			pSqlServer->executeSqlQuery(aBuf);
			
			if(pSqlServer->GetResults()->next())
			{
				m_RoundId = (int)pSqlServer->GetResults()->getInt("RoundId");
			}
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("sql", "Can't send round statistics (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
	
	virtual void* GenerateChildData()
	{
		return &m_RoundId;
	}
};

class CSqlJob_Server_SendPlayerStatistics : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientId;
	int m_UserId;
	int m_RoundId;
	CSqlString<64> m_sMapName;
	CRoundStatistics::CPlayer m_PlayerStatistics;
	
public:
	CSqlJob_Server_SendPlayerStatistics(CServer* pServer, const CRoundStatistics::CPlayer* pPlayerStatistics, const char* pMapName, int UserId, int ClientId)
	{
		m_RoundId = -1;
		m_pServer = pServer;
		m_ClientId = ClientId;
		m_UserId = UserId;
		m_sMapName = CSqlString<64>(pMapName);
		m_PlayerStatistics = *pPlayerStatistics;
	}
	
	virtual void ProcessParentData(void* pData)
	{
		int* pRoundId = (int*) pData;
		m_RoundId = *pRoundId;
	}
	
	void UpdateScore(CSqlServer* pSqlServer, int ScoreType, int Score, const char* pScoreName)
	{
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), 
			"INSERT INTO %s_infc_RoundScore "
			"(UserId, RoundId, MapName, ScoreType, ScoreDate, Score) "
			"VALUES ('%d', '%d', '%s', '%d', UTC_TIMESTAMP(), '%d');"
			, pSqlServer->GetPrefix(), m_UserId, m_RoundId, m_sMapName.ClrStr(), ScoreType, Score);
		pSqlServer->executeSql(aBuf);
	}

	virtual bool Job(CSqlServer* pSqlServer)
	{
		char aBuf[512];
		
		if(m_RoundId < 0)
			return false;
		
		try
		{
			//Get old score
			str_format(aBuf, sizeof(aBuf), 
				"SELECT Score FROM %s_infc_RoundScore "
				"WHERE UserId = '%d' AND MapName = '%s' AND ScoreType = '%d'"
				"ORDER BY Score DESC "
				"LIMIT %d "
				, pSqlServer->GetPrefix(), m_UserId, m_sMapName.ClrStr(), 0, SQL_SCORE_NUMROUND);
			pSqlServer->executeSqlQuery(aBuf);

			int OldScore = 0;
			while(pSqlServer->GetResults()->next())
			{
				OldScore += (int)pSqlServer->GetResults()->getInt("Score");
			}
			
			if(m_PlayerStatistics.m_Score > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_ROUND_SCORE, m_PlayerStatistics.m_Score, "");
				
			if(m_PlayerStatistics.m_EngineerScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_ENGINEER_SCORE, m_PlayerStatistics.m_EngineerScore, "Engineer");
			if(m_PlayerStatistics.m_SoldierScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_SOLDIER_SCORE, m_PlayerStatistics.m_SoldierScore, "Soldier");
			if(m_PlayerStatistics.m_ScientistScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_SCIENTIST_SCORE, m_PlayerStatistics.m_ScientistScore, "Scientist");
			if(m_PlayerStatistics.m_BiologistScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_BIOLOGIST_SCORE, m_PlayerStatistics.m_BiologistScore, "Biologist");
			if(m_PlayerStatistics.m_LooperScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_LOOPER_SCORE, m_PlayerStatistics.m_LooperScore, "Looper");
			if(m_PlayerStatistics.m_MedicScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_MEDIC_SCORE, m_PlayerStatistics.m_MedicScore, "Medic");
			if(m_PlayerStatistics.m_HeroScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_HERO_SCORE, m_PlayerStatistics.m_HeroScore, "Hero");
			if(m_PlayerStatistics.m_NinjaScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_NINJA_SCORE, m_PlayerStatistics.m_NinjaScore, "Ninja");
			if(m_PlayerStatistics.m_MercenaryScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_MERCENARY_SCORE, m_PlayerStatistics.m_MercenaryScore, "Mercenary");
			if(m_PlayerStatistics.m_SniperScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_SNIPER_SCORE, m_PlayerStatistics.m_SniperScore, "Sniper");
				
			if(m_PlayerStatistics.m_SmokerScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_SMOKER_SCORE, m_PlayerStatistics.m_SmokerScore, "Smoker");
			if(m_PlayerStatistics.m_HunterScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_HUNTER_SCORE, m_PlayerStatistics.m_HunterScore, "Hunter");
			if(m_PlayerStatistics.m_BoomerScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_BOOMER_SCORE, m_PlayerStatistics.m_BoomerScore, "Boomer");
			if(m_PlayerStatistics.m_GhostScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_GHOST_SCORE, m_PlayerStatistics.m_GhostScore, "Ghost");
			if(m_PlayerStatistics.m_SpiderScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_SPIDER_SCORE, m_PlayerStatistics.m_SpiderScore, "Spider");
			if(m_PlayerStatistics.m_GhoulScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_GHOUL_SCORE, m_PlayerStatistics.m_GhoulScore, "Ghoul");
			if(m_PlayerStatistics.m_SlugScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_SLUG_SCORE, m_PlayerStatistics.m_SlugScore, "Slug");
			if(m_PlayerStatistics.m_UndeadScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_UNDEAD_SCORE, m_PlayerStatistics.m_UndeadScore, "Undead");
			if(m_PlayerStatistics.m_WitchScore > 0)
				UpdateScore(pSqlServer, SQL_SCORETYPE_WITCH_SCORE, m_PlayerStatistics.m_WitchScore, "Witch");
		
			//Get new score
			str_format(aBuf, sizeof(aBuf), 
				"SELECT Score FROM %s_infc_RoundScore "
				"WHERE UserId = '%d' AND MapName = '%s' AND ScoreType = '%d'"
				"ORDER BY Score DESC "
				"LIMIT %d "
				, pSqlServer->GetPrefix(), m_UserId, m_sMapName.ClrStr(), 0, SQL_SCORE_NUMROUND);
			pSqlServer->executeSqlQuery(aBuf);

			int NewScore = 0;
			if(pSqlServer->GetResults()->next())
			{
				NewScore += (int)pSqlServer->GetResults()->getInt("Score");
			}
			
			if(OldScore < NewScore)
			{
				str_format(aBuf, sizeof(aBuf), "You increased your score: +%d", (NewScore-OldScore)/10);
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientId, aBuf);
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException &e)
		{
			dbg_msg("sql", "Can't send player statistics (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};
#endif

void CServer::SendStatistics()
{
#ifdef CONF_SQL
	//Send round statistics
	CSqlJob* pRoundJob = new CSqlJob_Server_SendRoundStatistics(this, RoundStatistics(), m_aCurrentMap);
	pRoundJob->Start();
	
	//Send player statistics
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State == CClient::STATE_INGAME)
		{
			if(m_aClients[i].m_UserId >= 0 && RoundStatistics()->IsValidePlayer(i))
			{
				CSqlJob* pJob = new CSqlJob_Server_SendPlayerStatistics(this, RoundStatistics()->PlayerStatistics(i), m_aCurrentMap, m_aClients[i].m_UserId, i);
				pRoundJob->AddQueuedJob(pJob);
			}
		}
	}
#endif
}

void CServer::OnRoundIsOver()
{
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State == CClient::STATE_INGAME)
		{
			m_aClients[i].m_NbRound++;
		}
	}
}

void CServer::ResetStatistics()
{
	RoundStatistics()->Reset();
}
	
void CServer::SetClientMemory(int ClientId, int Memory, bool Value)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || Memory < 0 || Memory >= NUM_CLIENTMEMORIES)
		return;
	
	m_aClients[ClientId].m_Memory[Memory] = Value;
}

bool CServer::GetClientMemory(int ClientId, int Memory)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || Memory < 0 || Memory >= NUM_CLIENTMEMORIES)
		return false;
	
	return m_aClients[ClientId].m_Memory[Memory];
}

void CServer::ResetClientMemoryAboutGame(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	
	m_aClients[ClientId].m_Memory[CLIENTMEMORY_TOP10] = false;
}

IServer::CClientSession* CServer::GetClientSession(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return 0;
	
	return &m_aClients[ClientId].m_Session;
}

// returns how many players are currently playing and not spectating
uint32_t CServer::GetActivePlayerCount()
{
	uint32_t PlayerCount = 0;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State == CClient::STATE_INGAME)
		{
			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;
		}
	}
	return PlayerCount;
}

const char *CServer::GetPreviousMapName() const
{
	return m_aPreviousMap;
}

void CServer::AddAccusation(int From, int To, const char* pReason)
{
	if(From < 0 || From >= MAX_CLIENTS || To < 0 || To >= MAX_CLIENTS)
		return;
	
	//Check if "From" already accusate "To"
	NETADDR FromAddr = *m_NetServer.ClientAddr(From);
	FromAddr.port = 0;
	for(int i=0; i<m_aClients[To].m_Accusation.m_Num; i++)
	{
		if(net_addr_comp(&m_aClients[To].m_Accusation.m_Addresses[i], &FromAddr) == 0)
		{
			if(m_pGameServer)
				m_pGameServer->SendChatTarget_Localization(From, CHATCATEGORY_DEFAULT, _("You have already notified that {str:PlayerName} ought to be banned"), "PlayerName", ClientName(To), NULL);
			return;
		}
	}
	
	//Check the number of accusation against "To"
	if(m_aClients[To].m_Accusation.m_Num < MAX_ACCUSATIONS)
	{
		//Add the accusation
		m_aClients[To].m_Accusation.m_Addresses[m_aClients[To].m_Accusation.m_Num] = FromAddr;
		m_aClients[To].m_Accusation.m_Num++;
	}
		
	if(m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(-1, CHATCATEGORY_ACCUSATION, _("{str:PlayerName} wants {str:VictimName} to be banned ({str:Reason})"),
			"PlayerName", ClientName(From),
			"VictimName", ClientName(To),
			"Reason", pReason,
			NULL
		);
	}
}

bool CServer::ClientShouldBeBanned(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	
	return (m_aClients[ClientId].m_Accusation.m_Num >= g_Config.m_InfAccusationThreshold);
}

void CServer::RemoveAccusations(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	
	m_aClients[ClientId].m_Accusation.m_Num = 0;
}

void CServer::AddMapVote(int From, const char* pCommand, const char* pReason, const char* pDesc)
{
	NETADDR FromAddr = *m_NetServer.ClientAddr(From);
	int Index = -1;
	for (int i=0; i<m_MapVotesCounter; i++)
	{
		if(str_comp_nocase(m_MapVotes[i].m_pCommand, pCommand) == 0)
		{
			Index = i;
			break;

		}
	}
	if (Index < 0)
	{		
		// create a new variable of type CMapVote for a specific map
		// in order to count how many players want to start this map vote
		Index = m_MapVotesCounter;
		m_MapVotes[Index].m_pCommand = new char[VOTE_CMD_LENGTH];
		str_copy(const_cast<char*>(m_MapVotes[Index].m_pCommand), pCommand, VOTE_CMD_LENGTH);
		m_MapVotes[Index].m_pAddresses = new NETADDR[MAX_MAPVOTEADDRESSES];
		m_MapVotes[Index].m_pAddresses[0] = FromAddr;
		m_MapVotes[Index].m_Num = 1;
		m_MapVotes[Index].m_pReason = new char[VOTE_REASON_LENGTH];
		str_copy(const_cast<char*>(m_MapVotes[Index].m_pReason), pReason, VOTE_REASON_LENGTH);
		m_MapVotes[Index].m_pDesc = new char[VOTE_DESC_LENGTH];
		str_copy(const_cast<char*>(m_MapVotes[Index].m_pDesc), pDesc, VOTE_DESC_LENGTH);
		m_MapVotesCounter++;
	}
	else 
	{
		// CMapVote variable for this map already exists -> add player to it

		if (str_comp_nocase(m_MapVotes[Index].m_pReason, "No reason given") == 0)
			// if there is a reason, use it instead of "No reason given"
			str_copy(const_cast<char*>(m_MapVotes[Index].m_pReason), pReason, VOTE_REASON_LENGTH);

		// check if the player has already voted
		for(int i=0; i<m_MapVotes[Index].m_Num; i++)
		{
			if(net_addr_comp(&m_MapVotes[Index].m_pAddresses[i], &FromAddr) == 0)
			{
				if(m_pGameServer)
					m_pGameServer->SendChatTarget_Localization(From, CHATCATEGORY_DEFAULT, _("You have already voted to change this map"), NULL);
				return;
			}
		}
		// save address from the player, so he cannot vote twice for the same map
		m_MapVotes[Index].m_pAddresses[m_MapVotes[Index].m_Num] = FromAddr;
		// increase number that counts how many people have already voted
		m_MapVotes[Index].m_Num++;
	}

	if(m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT, _("{str:PlayerName} wants to start the vote '{str:VoteName}'"),
			"PlayerName", ClientName(From),
			"VoteName", pDesc,
			NULL
		);
	}
}

void CServer::RemoveMapVotesForId(int ClientId)
{
	NETADDR Addr = *m_NetServer.ClientAddr(ClientId);
	for (int i=0; i<m_MapVotesCounter; i++)
	{
		for(int k=0; k<m_MapVotes[i].m_Num; k++)
		{
			if(net_addr_comp(&m_MapVotes[i].m_pAddresses[k], &Addr) == 0)
			{
				if (k+1 == m_MapVotes[i].m_Num)
				{
					// leaving player has the last position inside the array - just decrease the size and continue
					m_MapVotes[i].m_Num--;
					continue;
				}
				// save the last address to the position which the player used that left (overwrite it)
				// in order to not lose the last address when we decrease the size of the array in the next line
				m_MapVotes[i].m_pAddresses[k] = m_MapVotes[i].m_pAddresses[m_MapVotes[i].m_Num-1];
				m_MapVotes[i].m_Num--;
			}
		}
	}
}

IServer::CMapVote* CServer::GetMapVote()
{
	if (m_MapVotesCounter <= 0)
		return 0;

	float PlayerCount = GetActivePlayerCount();

	int HighestNum = -1;
	int HighestNumIndex = -1;
	for (int i = 0; i < m_MapVotesCounter; i++)
	{
		if (m_MapVotes[i].m_Num <= 0)
			continue;
		if (m_MapVotes[i].m_Num >= g_Config.m_InfMinPlayerNumberForMapVote)
		{
			if (m_MapVotes[i].m_Num > HighestNum)
			{
				HighestNum = m_MapVotes[i].m_Num;
				HighestNumIndex = i;
			}
		}
		if (m_MapVotes[i].m_Num/PlayerCount >= g_Config.m_InfMinPlayerPercentForMapVote/(float)100)
		{
			if (m_MapVotes[i].m_Num > HighestNum)
			{
				HighestNum = m_MapVotes[i].m_Num;
				HighestNumIndex = i;
			}
		}
	}
	if (HighestNumIndex >= 0)
		return &m_MapVotes[HighestNumIndex];

	return 0;
}

void CServer::ResetMapVotes()
{
	for (int i = 0; i < m_MapVotesCounter; i++)
	{
		delete[] m_MapVotes[i].m_pCommand;
		delete[] m_MapVotes[i].m_pAddresses;
		delete[] m_MapVotes[i].m_pReason;
	}
	m_MapVotesCounter = 0;
}

#ifdef CONF_SQL
int CServer::GetUserLevel(int ClientId)
{
	return m_aClients[ClientId].m_UserLevel;
}
#endif

/* INFECTION MODIFICATION END *****************************************/

int *CServer::GetIdMap(int ClientId)
{
	return m_aIdMap + VANILLA_MAX_CLIENTS * ClientId;
}

bool CServer::SetTimedOut(int ClientId, int OrigId)
{
	if(!m_NetServer.SetTimedOut(ClientId, OrigId))
	{
		return false;
	}
	m_aClients[ClientId].m_Sixup = m_aClients[OrigId].m_Sixup;

	if(m_aClients[OrigId].m_Authed != AUTHED_NO)
	{
		LogoutClient(ClientId, "Timeout Protection");
	}
	DelClientCallback(OrigId, EClientDropType::TimeoutProtectionUsed, "Timeout Protection used", this);
	m_aClients[ClientId].m_Authed = AUTHED_NO;
	m_aClients[ClientId].m_Flags = m_aClients[OrigId].m_Flags;
	m_aClients[ClientId].m_DDNetVersion = m_aClients[OrigId].m_DDNetVersion;
	m_aClients[ClientId].m_InfClassVersion = m_aClients[OrigId].m_InfClassVersion;
	m_aClients[ClientId].m_GotDDNetVersionPacket = m_aClients[OrigId].m_GotDDNetVersionPacket;
	m_aClients[ClientId].m_DDNetVersionSettled = m_aClients[OrigId].m_DDNetVersionSettled;
	return true;
}

void CServer::SetErrorShutdown(const char *pReason)
{
	str_copy(m_aErrorShutdownReason, pReason);
}

void CServer::SetLoggers(std::shared_ptr<ILogger> &&pFileLogger, std::shared_ptr<ILogger> &&pStdoutLogger)
{
	m_pFileLogger = pFileLogger;
	m_pStdoutLogger = pStdoutLogger;
}
