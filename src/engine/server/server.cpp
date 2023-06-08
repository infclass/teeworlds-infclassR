/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/

#include "server.h"

#include <base/logger.h>
#include <base/math.h>
#include <base/system.h>
#include <base/tl/array.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/map.h>
#include <engine/masterserver.h>
#include <engine/server.h>
#include <engine/storage.h>

#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/snapshot.h>

#include <game/gamecore.h>
#include <game/mapitems.h>
#include <game/version.h>

#include <mastersrv/mastersrv.h>

#include "server.h"

#include <cstring>
/* INFECTION MODIFICATION START ***************************************/
#include <engine/server/mapconverter.h>
#include <engine/server/sql_job.h>
#include <engine/server/crypt.h>
#include <game/server/infclass/events-director.h>

#include <teeuniverses/components/localization.h>
/* INFECTION MODIFICATION END *****************************************/

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

CSnapIDPool::CSnapIDPool()
{
	Reset();
}

void CSnapIDPool::Reset()
{
	for(int i = 0; i < MAX_IDS; i++)
	{
		m_aIDs[i].m_Next = i + 1;
		m_aIDs[i].m_State = ID_FREE;
	}

	m_aIDs[MAX_IDS - 1].m_Next = -1;
	m_FirstFree = 0;
	m_FirstTimed = -1;
	m_LastTimed = -1;
	m_Usage = 0;
	m_InUsage = 0;
}

void CSnapIDPool::RemoveFirstTimeout()
{
	int NextTimed = m_aIDs[m_FirstTimed].m_Next;

	// add it to the free list
	m_aIDs[m_FirstTimed].m_Next = m_FirstFree;
	m_aIDs[m_FirstTimed].m_State = ID_FREE;
	m_FirstFree = m_FirstTimed;

	// remove it from the timed list
	m_FirstTimed = NextTimed;
	if(m_FirstTimed == -1)
		m_LastTimed = -1;

	m_Usage--;
}

int CSnapIDPool::NewID()
{
	int64_t Now = time_get();

	// process timed ids
	while(m_FirstTimed != -1 && m_aIDs[m_FirstTimed].m_Timeout < Now)
		RemoveFirstTimeout();

	int ID = m_FirstFree;
	if(ID == -1)
	{
		dbg_msg("server", "invalid id");
		return ID;
	}
	m_FirstFree = m_aIDs[m_FirstFree].m_Next;
	m_aIDs[ID].m_State = ID_ALLOCATED;
	m_Usage++;
	m_InUsage++;
	return ID;
}

void CSnapIDPool::TimeoutIDs()
{
	// process timed ids
	while(m_FirstTimed != -1)
		RemoveFirstTimeout();
}

void CSnapIDPool::FreeID(int ID)
{
	if(ID < 0)
		return;
	dbg_assert((size_t)ID < std::size(m_aIDs), "id is out of range");
	dbg_assert(m_aIDs[ID].m_State == ID_ALLOCATED, "id is not allocated");

	m_InUsage--;
	m_aIDs[ID].m_State = ID_TIMED;
	m_aIDs[ID].m_Timeout = time_get() + time_freq() * 5;
	m_aIDs[ID].m_Next = -1;

	if(m_LastTimed != -1)
	{
		m_aIDs[m_LastTimed].m_Next = ID;
		m_LastTimed = ID;
	}
	else
	{
		m_FirstTimed = ID;
		m_LastTimed = ID;
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
	if(Server()->m_RconClientID >= 0 && Server()->m_RconClientID < MAX_CLIENTS &&
		Server()->m_aClients[Server()->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(NetMatch(pData, Server()->m_NetServer.ClientAddr(Server()->m_RconClientID)))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (you can't ban yourself)");
			return -1;
		}

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(i == Server()->m_RconClientID || Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed >= Server()->m_RconAuthLevel && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}
	else if(Server()->m_RconClientID == IServer::RCON_CID_VOTE)
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
			Server()->m_NetServer.Drop(i, CLIENTDROPTYPE_BAN, aBuf);
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
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->Server()->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
			pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientID), Minutes * 60, pReason);
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

void CServer::CClient::Reset(bool ResetScore)
{
	// reset input
	for(int i = 0; i < 200; i++)
		m_aInputs[i].m_GameTick = -1;
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
	
	if(ResetScore)
	{
		m_NbRound = 0;
		m_WaitingTime = 0;

		m_UserID = -1;
#ifdef CONF_SQL
		m_UserLevel = SQL_USERLEVEL_NORMAL;
#endif
		m_LogInstance = -1;

		m_DefaultScoreMode = PLAYERSCOREMODE_SCORE;
		str_copy(m_aLanguage, "en", sizeof(m_aLanguage));

		mem_zero(m_Memory, sizeof(m_Memory));
		
		m_Session.m_RoundId = -1;
		m_Session.m_Class = PLAYERCLASS_NONE;
		m_Session.m_MuteTick = 0;
		
		m_Accusation.m_Num = 0;
	}
}
/* INFECTION MODIFICATION END *****************************************/

CServer::CServer()
{
	m_pConfig = &g_Config;
	m_aDemoRecorder[0] = CDemoRecorder(&m_SnapshotDelta);

	m_TickSpeed = SERVER_TICK_SPEED;

	m_pGameServer = 0;

	m_CurrentGameTick = 0;
	m_RunServer = UNINITIALIZED;

	m_aShutdownReason[0] = 0;

	m_pCurrentMapData = 0;
	m_CurrentMapSize = 0;

	m_MapReload = false;
	m_ReloadedWhenEmpty = false;

	m_RconClientID = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;

	m_ServerInfoFirstRequest = 0;
	m_ServerInfoNumRequests = 0;
	m_ServerInfoHighLoad = false;

	m_ServerInfoRequestLogTick = 0;
	m_ServerInfoRequestLogRecords = 0;

	m_aErrorShutdownReason[0] = 0;

#ifdef CONF_SQL
/* DDNET MODIFICATION START *******************************************/
	for (int i = 0; i < MAX_SQLSERVERS; i++)
	{
		m_apSqlReadServers[i] = 0;
		m_apSqlWriteServers[i] = 0;
	}

	CSqlConnector::SetReadServers(m_apSqlReadServers);
	CSqlConnector::SetWriteServers(m_apSqlWriteServers);
/* DDNET MODIFICATION END *********************************************/
	
	m_GameServerCmdLock = lock_create();
	m_ChallengeLock = lock_create();
#endif

	Init();
}

CServer::~CServer()
{
	if(m_RunServer != UNINITIALIZED)
	{
		for(auto &Client : m_aClients)
		{
			free(Client.m_pPersistentData);
		}
	}

#ifdef CONF_SQL
	lock_destroy(m_GameServerCmdLock);
	lock_destroy(m_ChallengeLock);
#endif
}

bool CServer::IsClientNameAvailable(int ClientID, const char *pNameRequest)
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
		if(i != ClientID && m_aClients[i].m_State >= CClient::STATE_READY)
		{
			if(str_utf8_comp_confusable(pNameRequest, m_aClients[i].m_aName) == 0)
				return false;
		}
	}

	return true;
}

bool CServer::SetClientNameImpl(int ClientID, const char *pNameRequest, bool Set)
{
	dbg_assert(0 <= ClientID && ClientID < MAX_CLIENTS, "invalid client id");
	if(m_aClients[ClientID].m_State < CClient::STATE_READY)
		return false;

	CNameBan *pBanned = IsNameBanned(pNameRequest, m_vNameBans);
	if(pBanned)
	{
		if(m_aClients[ClientID].m_State == CClient::STATE_READY && Set)
		{
			char aBuf[256];
			if(pBanned->m_aReason[0])
			{
				str_format(aBuf, sizeof(aBuf), "Kicked (your name is banned: %s)", pBanned->m_aReason);
			}
			else
			{
				str_copy(aBuf, "Kicked (your name is banned)", sizeof(aBuf));
			}
			Kick(ClientID, aBuf);
		}
		return false;
	}

	// trim the name
	char aTrimmedName[MAX_NAME_LENGTH];
	str_copy(aTrimmedName, str_utf8_skip_whitespaces(pNameRequest), sizeof(aTrimmedName));
	str_utf8_trim_right(aTrimmedName);

	char aNameTry[MAX_NAME_LENGTH];
	str_copy(aNameTry, aTrimmedName, sizeof(aNameTry));

	if(!IsClientNameAvailable(ClientID, aNameTry))
	{
		// auto rename
		for(int i = 1;; i++)
		{
			str_format(aNameTry, sizeof(aNameTry), "(%d)%s", i, aTrimmedName);
			if(IsClientNameAvailable(ClientID, aNameTry))
				break;
		}
	}

	bool Changed = str_comp(m_aClients[ClientID].m_aName, aNameTry) != 0;

	if(Set)
	{
		// set the client name
		str_copy(m_aClients[ClientID].m_aName, aNameTry, MAX_NAME_LENGTH);
	}

	return Changed;
}

bool CServer::WouldClientNameChange(int ClientID, const char *pNameRequest)
{
	return SetClientNameImpl(ClientID, pNameRequest, false);
}

void CServer::SetClientName(int ClientID, const char *pName)
{
	SetClientNameImpl(ClientID, pName, true);
}

void CServer::SetClientClan(int ClientID, const char *pClan)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY || !pClan)
		return;

	str_copy(m_aClients[ClientID].m_aClan, pClan, MAX_CLAN_LENGTH);
}

void CServer::SetClientCountry(int ClientID, int Country)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_Country = Country;
}

void CServer::Kick(int ClientID, const char *pReason)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid client id to kick");
		return;
	}
	else if(m_RconClientID == ClientID)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "you can't kick yourself");
 		return;
	}
	else if(m_aClients[ClientID].m_Authed > m_RconAuthLevel)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
 		return;
	}
	else if(m_aClients[ClientID].m_IsBot)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
		return;
	}

	m_NetServer.Drop(ClientID, CLIENTDROPTYPE_KICK, pReason);
}

/*int CServer::Tick()
{
	return m_CurrentGameTick;
}*/

int64_t CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq()*Tick)/SERVER_TICK_SPEED;
}

/*int CServer::TickSpeed()
{
	return SERVER_TICK_SPEED;
}*/

int CServer::Init()
{
	for(auto &Client : m_aClients)
	{
		Client.m_State = CClient::STATE_EMPTY;
		Client.m_aName[0] = 0;
		Client.m_aClan[0] = 0;
		Client.m_Country = -1;
		Client.m_Snapshots.Init();
		Client.m_IsBot = false;
		Client.m_WaitingTime = 0;
		Client.m_Accusation.m_Num = 0;
		Client.m_ShowIps = false;
		Client.m_Latency = 0;
		Client.m_InfClassVersion = 0;
		Client.m_Sixup = false;
	}

	m_CurrentGameTick = 0;
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

	SetFireDelay(INFWEAPON::NONE, 0);
	SetFireDelay(INFWEAPON::HAMMER, 125);
	SetFireDelay(INFWEAPON::GUN, 125);
	SetFireDelay(INFWEAPON::SHOTGUN, 500);
	SetFireDelay(INFWEAPON::GRENADE, 500);
	SetFireDelay(INFWEAPON::LASER, 800);
	SetFireDelay(INFWEAPON::NINJA, 800);
	SetFireDelay(INFWEAPON::ENGINEER_LASER, GetFireDelay(INFWEAPON::LASER));
	SetFireDelay(INFWEAPON::SOLDIER_GRENADE, GetFireDelay(INFWEAPON::GRENADE));
	SetFireDelay(INFWEAPON::SCIENTIST_LASER, GetFireDelay(INFWEAPON::LASER));
	SetFireDelay(INFWEAPON::SCIENTIST_GRENADE, GetFireDelay(INFWEAPON::GRENADE));
	SetFireDelay(INFWEAPON::MEDIC_GRENADE, GetFireDelay(INFWEAPON::GRENADE));
	SetFireDelay(INFWEAPON::MEDIC_LASER, GetFireDelay(INFWEAPON::LASER));
	SetFireDelay(INFWEAPON::MEDIC_SHOTGUN, 250);
	SetFireDelay(INFWEAPON::HERO_SHOTGUN, 250);
	SetFireDelay(INFWEAPON::BIOLOGIST_SHOTGUN, 250);
	SetFireDelay(INFWEAPON::BIOLOGIST_LASER, GetFireDelay(INFWEAPON::LASER));
	SetFireDelay(INFWEAPON::LOOPER_LASER, 250);
	SetFireDelay(INFWEAPON::LOOPER_GRENADE, GetFireDelay(INFWEAPON::GRENADE));
	SetFireDelay(INFWEAPON::HERO_LASER, GetFireDelay(INFWEAPON::LASER));
	SetFireDelay(INFWEAPON::HERO_GRENADE, GetFireDelay(INFWEAPON::GRENADE));
	SetFireDelay(INFWEAPON::SNIPER_LASER, GetFireDelay(INFWEAPON::LASER));
	SetFireDelay(INFWEAPON::NINJA_HAMMER, GetFireDelay(INFWEAPON::NINJA));
	SetFireDelay(INFWEAPON::NINJA_GRENADE, GetFireDelay(INFWEAPON::GRENADE));
	SetFireDelay(INFWEAPON::MERCENARY_GRENADE, GetFireDelay(INFWEAPON::GRENADE));
	SetFireDelay(INFWEAPON::MERCENARY_GUN, 50);
	SetFireDelay(INFWEAPON::MERCENARY_LASER, 250);
	SetFireDelay(INFWEAPON::BLINDING_LASER, GetFireDelay(INFWEAPON::LASER));

	SetAmmoRegenTime(INFWEAPON::NONE, 0);
	SetAmmoRegenTime(INFWEAPON::HAMMER, 0);
	SetAmmoRegenTime(INFWEAPON::GUN, 500);
	SetAmmoRegenTime(INFWEAPON::SHOTGUN, 0);
	SetAmmoRegenTime(INFWEAPON::GRENADE, 0);
	SetAmmoRegenTime(INFWEAPON::LASER, 0);
	SetAmmoRegenTime(INFWEAPON::NINJA, 0);

	SetAmmoRegenTime(INFWEAPON::ENGINEER_LASER, 6000);
	SetAmmoRegenTime(INFWEAPON::SOLDIER_GRENADE, 7000);
	SetAmmoRegenTime(INFWEAPON::SCIENTIST_LASER, 6000);
	SetAmmoRegenTime(INFWEAPON::SCIENTIST_GRENADE, 10000);
	SetAmmoRegenTime(INFWEAPON::MEDIC_GRENADE, 0);
	SetAmmoRegenTime(INFWEAPON::MEDIC_LASER, 6000);
	SetAmmoRegenTime(INFWEAPON::MEDIC_SHOTGUN, 750);
	SetAmmoRegenTime(INFWEAPON::HERO_SHOTGUN, 750);
	SetAmmoRegenTime(INFWEAPON::HERO_LASER, 3000);
	SetAmmoRegenTime(INFWEAPON::HERO_GRENADE, 3000);
	SetAmmoRegenTime(INFWEAPON::SNIPER_LASER, 2000);
	SetAmmoRegenTime(INFWEAPON::MERCENARY_GRENADE, 5000);
	SetAmmoRegenTime(INFWEAPON::MERCENARY_GUN, 125);
	SetAmmoRegenTime(INFWEAPON::MERCENARY_LASER, 4000);
	SetAmmoRegenTime(INFWEAPON::NINJA_HAMMER, 0);
	SetAmmoRegenTime(INFWEAPON::NINJA_GRENADE, 15000);
	SetAmmoRegenTime(INFWEAPON::BIOLOGIST_LASER, 175);
	SetAmmoRegenTime(INFWEAPON::BIOLOGIST_SHOTGUN, 675);
	SetAmmoRegenTime(INFWEAPON::LOOPER_LASER, 500);
	SetAmmoRegenTime(INFWEAPON::LOOPER_GRENADE, 5000);
	SetAmmoRegenTime(INFWEAPON::BLINDING_LASER, 10000);

	SetMaxAmmo(INFWEAPON::NONE, -1);
	SetMaxAmmo(INFWEAPON::HAMMER, -1);
	SetMaxAmmo(INFWEAPON::GUN, 10);
	SetMaxAmmo(INFWEAPON::SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON::GRENADE, 10);
	SetMaxAmmo(INFWEAPON::LASER, 10);
	SetMaxAmmo(INFWEAPON::NINJA, 10);
	SetMaxAmmo(INFWEAPON::ENGINEER_LASER, 10);
	SetMaxAmmo(INFWEAPON::SCIENTIST_LASER, 10);
	SetMaxAmmo(INFWEAPON::SCIENTIST_GRENADE, 3);
	SetMaxAmmo(INFWEAPON::SOLDIER_GRENADE, 10);
	SetMaxAmmo(INFWEAPON::MEDIC_GRENADE, 10);
	SetMaxAmmo(INFWEAPON::MEDIC_LASER, 1);
	SetMaxAmmo(INFWEAPON::MEDIC_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON::HERO_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON::HERO_LASER, 10);
	SetMaxAmmo(INFWEAPON::HERO_GRENADE, 10);
	SetMaxAmmo(INFWEAPON::SNIPER_LASER, 10);
	SetMaxAmmo(INFWEAPON::NINJA_HAMMER, -1);
	SetMaxAmmo(INFWEAPON::NINJA_GRENADE, 5);
	SetMaxAmmo(INFWEAPON::MERCENARY_GRENADE, 8);
	SetMaxAmmo(INFWEAPON::MERCENARY_GUN, 40);
	SetMaxAmmo(INFWEAPON::MERCENARY_LASER, 10);
	SetMaxAmmo(INFWEAPON::BIOLOGIST_LASER, 10);
	SetMaxAmmo(INFWEAPON::BIOLOGIST_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON::LOOPER_LASER, 20);
	SetMaxAmmo(INFWEAPON::LOOPER_GRENADE, 10);
	SetMaxAmmo(INFWEAPON::BLINDING_LASER, 10);
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

void CServer::SetRconCID(int ClientID)
{
	m_RconClientID = ClientID;
}

int CServer::GetAuthedState(int ClientID) const
{
	return m_aClients[ClientID].m_Authed;
}

int CServer::GetClientInfo(int ClientID, CClientInfo *pInfo) const
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "client_id is not valid");
	dbg_assert(pInfo != 0, "info can not be null");

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		pInfo->m_pName = ClientName(ClientID);
		pInfo->m_Latency = m_aClients[ClientID].m_Latency;
		pInfo->m_DDNetVersion = m_aClients[ClientID].m_DDNetVersion >= 0 ? m_aClients[ClientID].m_DDNetVersion : VERSION_VANILLA;
		pInfo->m_InfClassVersion = m_aClients[ClientID].m_InfClassVersion;
		return 1;
	}
	return 0;
}

void CServer::SetClientDDNetVersion(int ClientID, int DDNetVersion)
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "client_id is not valid");

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		m_aClients[ClientID].m_DDNetVersion = DDNetVersion;
	}
}

void CServer::GetClientAddr(int ClientID, char *pAddrStr, int Size) const
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientID), pAddrStr, Size, false);
}

const char *CServer::ClientName(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "(invalid)";
		
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
	{
		if(m_aClients[ClientID].m_UserID >= 0)
			return m_aClients[ClientID].m_aUsername;
		else
			return m_aClients[ClientID].m_aName;
	}
	else
		return "(connecting)";

}

const char *CServer::ClientClan(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aClan;
	else
		return "";
}

int CServer::ClientCountry(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return -1;
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_Country;
	else
		return -1;
}

bool CServer::ClientIngame(int ClientID) const
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME;
}

bool CServer::ClientIsBot(int ClientID) const
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_IsBot;
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
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
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

int CServer::GetClientVersion(int ClientID) const
{
	// Assume latest client version for server demos
	if(ClientID == SERVER_DEMO_CLIENT)
		return CLIENT_VERSIONNR;

	CClientInfo Info;
	if(GetClientInfo(ClientID, &Info))
		return Info.m_DDNetVersion;
	return VERSION_NONE;
}

static inline bool RepackMsg(const CMsgPacker *pMsg, CPacker &Packer)
{
	Packer.Reset();
	if(pMsg->m_MsgID < OFFSET_UUID)
	{
		Packer.AddInt((pMsg->m_MsgID << 1) | (pMsg->m_System ? 1 : 0));
	}
	else
	{
		Packer.AddInt((0 << 1) | (pMsg->m_System ? 1 : 0)); // NETMSG_EX, NETMSGTYPE_EX
		g_UuidManager.PackUuid(pMsg->m_MsgID, &Packer);
	}
	Packer.AddRaw(pMsg->Data(), pMsg->Size());

	return false;
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientID)
{
	// drop packet to dummy client
	if(ClientIsBot(ClientID))
		return 0;

	CNetChunk Packet;
	mem_zero(&Packet, sizeof(CNetChunk));
	if(Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if(ClientID < 0)
	{
		CPacker Pack6;
		if(RepackMsg(pMsg, Pack6))
			return -1;

		// write message to demo recorder
		if(!(Flags & MSGFLAG_NORECORD))
		{
			for(auto &Recorder :  m_aDemoRecorder)
				if(Recorder.IsRecording())
					Recorder.RecordMessage(Pack6.Data(), Pack6.Size());
		}

		if(!(Flags & MSGFLAG_NOSEND))
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if((m_aClients[i].m_State == CClient::STATE_INGAME) && !m_aClients[i].m_IsBot)
				{
					CPacker *pPack = &Pack6;
					Packet.m_pData = pPack->Data();
					Packet.m_DataSize = pPack->Size();
					Packet.m_ClientID = i;
					m_NetServer.Send(&Packet);
				}
			}
		}
	}
	else
	{
		CPacker Pack;
		if(RepackMsg(pMsg, Pack))
			return -1;

		Packet.m_ClientID = ClientID;
		Packet.m_pData = Pack.Data();
		Packet.m_DataSize = Pack.Size();

		if(!(Flags & MSGFLAG_NORECORD))
		{
			if(m_aDemoRecorder[0].IsRecording())
				m_aDemoRecorder[0].RecordMessage(Pack.Data(), Pack.Size());
		}

		if(!(Flags & MSGFLAG_NOSEND))
			m_NetServer.Send(&Packet);
	}

	return 0;
}

void CServer::SendMsgRaw(int ClientID, const void *pData, int Size, int Flags)
{
	CNetChunk Packet;
	mem_zero(&Packet, sizeof(CNetChunk));
	Packet.m_ClientID = ClientID;
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
	if(m_aDemoRecorder[0].IsRecording())
	{
		char aData[CSnapshot::MAX_SIZE];
		int SnapshotSize;

		// build snap and possibly add some messages
		m_SnapshotBuilder.Init();
		GameServer()->OnSnap(-1);
		SnapshotSize = m_SnapshotBuilder.Finish(aData);

		// write snapshot
		m_aDemoRecorder[0].RecordSnapshot(Tick(), aData, SnapshotSize);
	}

	// create snapshots for all clients
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// client must be ingame to recive snapshots
		if(m_aClients[i].m_State != CClient::STATE_INGAME)
			continue;

		// client must be human to recive snapshots
		if(m_aClients[i].m_IsBot)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_RECOVER && (Tick()%50) != 0)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_INIT && (Tick()%10) != 0)
			continue;

		{
			char aData[CSnapshot::MAX_SIZE];
			CSnapshot *pData = (CSnapshot*)aData;	// Fix compiler warning for strict-aliasing
			char aDeltaData[CSnapshot::MAX_SIZE];
			char aCompData[CSnapshot::MAX_SIZE];
			int SnapshotSize;
			int Crc;
			static CSnapshot EmptySnap;
			CSnapshot *pDeltashot = &EmptySnap;
			int DeltashotSize;
			int DeltaTick = -1;
			int DeltaSize;

			m_SnapshotBuilder.Init();

			GameServer()->OnSnap(i);

			// finish snapshot
			SnapshotSize = m_SnapshotBuilder.Finish(pData);
			Crc = pData->Crc();

			// remove old snapshos
			// keep 3 seconds worth of snapshots
			m_aClients[i].m_Snapshots.PurgeUntil(m_CurrentGameTick-SERVER_TICK_SPEED*3);

			// save the snapshot
			m_aClients[i].m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0, nullptr);

			// find snapshot that we can preform delta against
			EmptySnap.Clear();

			{
				DeltashotSize = m_aClients[i].m_Snapshots.Get(m_aClients[i].m_LastAckedSnapshot, 0, &pDeltashot, 0);
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
			DeltaSize = m_SnapshotDelta.CreateDelta(pDeltashot, pData, aDeltaData);

			if(DeltaSize)
			{
				// compress it
				int SnapshotSize;
				const int MaxSize = MAX_SNAPSHOT_PACKSIZE;
				int NumPackets;

				SnapshotSize = CVariableInt::Compress(aDeltaData, DeltaSize, aCompData, sizeof(aCompData));
				NumPackets = (SnapshotSize+MaxSize-1)/MaxSize;

				for(int n = 0, Left = SnapshotSize; Left > 0; n++)
				{
					int Chunk = Left < MaxSize ? Left : MaxSize;
					Left -= Chunk;

					if(NumPackets == 1)
					{
						CMsgPacker Msg(NETMSG_SNAPSINGLE, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick-DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n*MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick-DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n*MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY, true);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick-DeltaTick);
				SendMsg(&Msg, MSGFLAG_FLUSH, i);
			}
		}
	}

	GameServer()->OnPostSnap();
}

int CServer::ClientRejoinCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_Quitting = false;

	pThis->m_aClients[ClientID].Reset();
	
	//Getback session about the client
	IServer::CClientSession* pSession = pThis->m_NetSession.GetData(pThis->m_NetServer.ClientAddr(ClientID));
	if(pSession)
	{
		dbg_msg("infclass", "session found for the client %d. Round id = %d, class id = %d", ClientID, pSession->m_RoundId, pSession->m_Class);
		pThis->m_aClients[ClientID].m_Session = *pSession;
		pThis->m_NetSession.RemoveSession(pThis->m_NetServer.ClientAddr(ClientID));
	}
	
	//Getback accusation about the client
	IServer::CClientAccusation* pAccusation = pThis->m_NetAccusation.GetData(pThis->m_NetServer.ClientAddr(ClientID));
	if(pAccusation)
	{
		dbg_msg("infclass", "%d accusation(s) found for the client %d", pAccusation->m_Num, ClientID);
		pThis->m_aClients[ClientID].m_Accusation = *pAccusation;
		pThis->m_NetAccusation.RemoveSession(pThis->m_NetServer.ClientAddr(ClientID));
	}

	pThis->SendMap(ClientID);

	return 0;
}

int CServer::NewBot(int ClientID)
{
	if(m_aClients[ClientID].m_State > CClient::STATE_EMPTY && !m_aClients[ClientID].m_IsBot)
		return 1;
	m_aClients[ClientID].m_State = CClient::STATE_INGAME;
	m_aClients[ClientID].m_Country = -1;
	m_aClients[ClientID].m_UserID = -1;
	m_aClients[ClientID].m_IsBot = true;

	return 0;
}

int CServer::DelBot(int ClientID)
{
	if( !m_aClients[ClientID].m_IsBot )
		return 1;
	m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	m_aClients[ClientID].m_aName[0] = 0;
	m_aClients[ClientID].m_aClan[0] = 0;
	m_aClients[ClientID].m_Country = -1;
	m_aClients[ClientID].m_UserID = -1;
	m_aClients[ClientID].m_Authed = AUTHED_NO;
	m_aClients[ClientID].m_AuthTries = 0;
	m_aClients[ClientID].m_pRconCmdToSend = 0;
	m_aClients[ClientID].m_IsBot = false;
	m_aClients[ClientID].m_Snapshots.PurgeAll();
	return 0;
}

int CServer::NewClientNoAuthCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	NewClientCallback(ClientID, pUser, false);

	pThis->SendCapabilities(ClientID);
	pThis->SendMap(ClientID);
	return 0;
}

int CServer::NewClientCallback(int ClientID, void *pUser, bool Sixup)
{
	CServer *pThis = (CServer *)pUser;

	// Remove non human player on same slot
	if(pThis->ClientIsBot(ClientID))
	{
		pThis->GameServer()->OnClientDrop(ClientID, CLIENTDROPTYPE_KICK, "removing dummy");
	}

	pThis->m_aClients[ClientID].m_State = CClient::STATE_PREAUTH;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_ShowIps = false;
	pThis->m_aClients[ClientID].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientID].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientID].m_DDNetVersionSettled = false;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_Quitting = false;
	
	memset(&pThis->m_aClients[ClientID].m_Addr, 0, sizeof(NETADDR));
	pThis->m_aClients[ClientID].Reset();
	
	//Getback session about the client
	IServer::CClientSession* pSession = pThis->m_NetSession.GetData(pThis->m_NetServer.ClientAddr(ClientID));
	if(pSession)
	{
		dbg_msg("infclass", "session found for the client %d. Round id = %d, class id = %d", ClientID, pSession->m_RoundId, pSession->m_Class);
		pThis->m_aClients[ClientID].m_Session = *pSession;
		pThis->m_NetSession.RemoveSession(pThis->m_NetServer.ClientAddr(ClientID));
	}
	
	//Getback accusation about the client
	IServer::CClientAccusation* pAccusation = pThis->m_NetAccusation.GetData(pThis->m_NetServer.ClientAddr(ClientID));
	if(pAccusation)
	{
		dbg_msg("infclass", "%d accusation(s) found for the client %d", pAccusation->m_Num, ClientID);
		pThis->m_aClients[ClientID].m_Accusation = *pAccusation;
		pThis->m_NetAccusation.RemoveSession(pThis->m_NetServer.ClientAddr(ClientID));
	}

	return 0;
}

int CServer::DelClientCallback(int ClientID, int Type, const char *pReason, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	
	if(pThis->m_aClients[ClientID].m_Quitting)
		return 0;

	const bool isBot = pThis->m_aClients[ClientID].m_IsBot;
	pThis->m_aClients[ClientID].m_Quitting = true;

	char aAddrStr[NETADDR_MAXSTRSIZE];

	// remove map votes for the dropped client
	pThis->RemoveMapVotesForID(ClientID);

	net_addr_str(pThis->m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "client dropped. cid=%d addr=<{%s}> reason='%s'", ClientID, aAddrStr, pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

	// notify the mod about the drop
	if(pThis->m_aClients[ClientID].m_State >= CClient::STATE_READY && pThis->m_aClients[ClientID].m_WaitingTime <= 0)
		pThis->GameServer()->OnClientDrop(ClientID, Type, pReason);

	pThis->m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_ShowIps = false;
	pThis->m_aPrevStates[ClientID] = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientID].m_Snapshots.PurgeAll();
	pThis->m_aClients[ClientID].m_WaitingTime = 0;
	pThis->m_aClients[ClientID].m_UserID = -1;
#ifdef CONF_SQL
	pThis->m_aClients[ClientID].m_UserLevel = SQL_USERLEVEL_NORMAL;
#endif
	pThis->m_aClients[ClientID].m_LogInstance = -1;
	pThis->m_aClients[ClientID].m_Quitting = false;

	if(isBot)
		return 0;

	//Keep information about client for 10 minutes
	pThis->m_NetSession.AddSession(pThis->m_NetServer.ClientAddr(ClientID), 10*60, &pThis->m_aClients[ClientID].m_Session);
	dbg_msg("infclass", "session created for the client %d", ClientID);
	
	//Keep accusation for 30 minutes
	pThis->m_NetAccusation.AddSession(pThis->m_NetServer.ClientAddr(ClientID), 30*60, &pThis->m_aClients[ClientID].m_Accusation);
	dbg_msg("infclass", "accusation created for the client %d", ClientID);
	
	return 0;
}

void CServer::SendCapabilities(int ClientID)
{
	CMsgPacker Msg(NETMSG_CAPABILITIES, true);
	Msg.AddInt(SERVERCAP_CURVERSION); // version
	Msg.AddInt(SERVERCAPFLAG_CHATTIMEOUTCODE | SERVERCAPFLAG_PINGEX); // flags
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendMap(int ClientID)
{
	{
		CMsgPacker Msg(NETMSG_MAP_DETAILS, true);
		Msg.AddString(GetMapName(), 0);
		Msg.AddRaw(&m_CurrentMapSha256.data, sizeof(m_CurrentMapSha256.data));
		Msg.AddInt(m_CurrentMapCrc);
		Msg.AddInt(m_CurrentMapSize);
		SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
	{
		CMsgPacker Msg(NETMSG_MAP_CHANGE, true);
		Msg.AddString(GetMapName(), 0);
		Msg.AddInt(m_CurrentMapCrc);
		Msg.AddInt(m_CurrentMapSize);
		SendMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
	}

	m_aClients[ClientID].m_NextMapChunk = 0;
}

void CServer::SendMapData(int ClientID, int Chunk)
{
	unsigned int ChunkSize = 1024-128;
	unsigned int Offset = Chunk * ChunkSize;
	int Last = 0;

	// drop faulty map data requests
	if(Chunk < 0 || Offset > m_CurrentMapSize)
		return;

	if(Offset+ChunkSize >= m_CurrentMapSize)
	{
		ChunkSize = m_CurrentMapSize-Offset;
		Last = 1;
	}

	CMsgPacker Msg(NETMSG_MAP_DATA, true);
	Msg.AddInt(Last);
	Msg.AddInt(m_CurrentMapCrc);
	Msg.AddInt(Chunk);
	Msg.AddInt(ChunkSize);
	Msg.AddRaw(&m_pCurrentMapData[Offset], ChunkSize);
	SendMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);

	if(g_Config.m_Debug)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
	}
}

void CServer::SendConnectionReady(int ClientID)
{
	CMsgPacker Msg(NETMSG_CON_READY, true);
	SendMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
}

void CServer::SendRconLine(int ClientID, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE, true);
	Msg.AddString(pLine, 512);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconLogLine(int ClientID, const CLogMessage *pMessage)
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
		str_append(aLine, pEnd + 2, sizeof(aLine));

		str_append(aLineWithoutIps, pLine, pStart - pLine + 1);
		str_append(aLineWithoutIps, "XXX", sizeof(aLineWithoutIps));
		str_append(aLineWithoutIps, pEnd + 2, sizeof(aLineWithoutIps));

		pLine = aLine;
		pLineWithoutIps = aLineWithoutIps;
	}

	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_aClients[i].m_State != CClient::STATE_EMPTY && m_aClients[i].m_Authed >= AUTHED_ADMIN)
				SendRconLine(i, m_aClients[i].m_ShowIps ? pLine : pLineWithoutIps);
		}
	}
	else
	{
		if(m_aClients[ClientID].m_State != CClient::STATE_EMPTY)
			SendRconLine(ClientID, m_aClients[ClientID].m_ShowIps ? pLine : pLineWithoutIps);
	}
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD, true);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM, true);
	Msg.AddString(pCommandInfo->m_pName, 256);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::UpdateClientRconCommands()
{
	int ClientID = Tick() % MAX_CLIENTS;

	if(m_aClients[ClientID].m_State != CClient::STATE_EMPTY && m_aClients[ClientID].m_Authed)
	{
		int ConsoleAccessLevel = m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD;
		for(int i = 0; i < MAX_RCONCMD_SEND && m_aClients[ClientID].m_pRconCmdToSend; ++i)
		{
			SendRconCmdAdd(m_aClients[ClientID].m_pRconCmdToSend, ClientID);
			m_aClients[ClientID].m_pRconCmdToSend = m_aClients[ClientID].m_pRconCmdToSend->NextCommandInfo(ConsoleAccessLevel, CFGFLAG_SERVER);
		}
	}
}

bool CServer::InitCaptcha()
{
	IOHANDLE File = Storage()->OpenFile("security/captcha.txt", IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
		return false;
	
	char CaptchaText[16];
	bool isEndOfFile = false;
	while(!isEndOfFile)
	{
		isEndOfFile = true;
		
		//Load one line
		int LineLenth = 0;
		char c;
		while(io_read(File, &c, 1))
		{
			isEndOfFile = false;
			
			if(c == '\n') break;
			else
			{
				CaptchaText[LineLenth] = c;
				LineLenth++;
			}
		}
		
		CaptchaText[LineLenth] = 0;
		
		if(CaptchaText[0])
		{
			m_NetServer.AddCaptcha(CaptchaText);
		}
	}

	io_close(File);
	
	return true;
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
	const char *pConverterId = g_Config.m_InfConverterId;
	pConverterId = EventsDirector::GetMapConverterId(pConverterId);
	str_format(aClientMapDir, sizeof(aClientMapDir), "clientmaps/%s", pConverterId);
	str_format(aClientMapName, sizeof(aClientMapName), "%s/%s_%08x.map", aClientMapDir, pMapName, ServerMapCrc);

	CMapConverter MapConverter(Storage(), m_pMap, Console());
	if(!MapConverter.Load())
		return false;

	m_TimeShiftUnit = MapConverter.GetTimeShiftUnit();

	CDataFileReader dfClientMap;
	//The map is already converted
	if(!g_Config.m_InfConverterForceRegeneration && dfClientMap.Open(Storage(), aClientMapName, IStorage::TYPE_ALL))
	{
		m_CurrentMapCrc = dfClientMap.Crc();
		m_CurrentMapSha256 = dfClientMap.Sha256();
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
		m_CurrentMapCrc = dfGeneratedMap.Crc();
		m_CurrentMapSha256 = dfGeneratedMap.Sha256();
		dfGeneratedMap.Close();
	}

	char aBufMsg[128];
	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(m_CurrentMapSha256, aSha256, sizeof(aSha256));
	str_format(aBufMsg, sizeof(aBufMsg), "%s sha256 is %s", pMapName, aSha256);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);
	str_format(aBufMsg, sizeof(aBufMsg), "map crc is %08x, generated map crc is %08x", ServerMapCrc, m_CurrentMapCrc);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);

	//Download the generated map in memory to send it to clients
	IOHANDLE File = Storage()->OpenFile(aClientMapName, IOFLAG_READ, IStorage::TYPE_ALL);
	m_CurrentMapSize = (int)io_length(File);
	free(m_pCurrentMapData);
	m_pCurrentMapData = (unsigned char *)malloc(m_CurrentMapSize);
	io_read(File, m_pCurrentMapData, m_CurrentMapSize);
	io_close(File);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", "maps/infc_x_current.map loaded in memory");

	return true;
}

void CServer::ProcessClientPacket(CNetChunk *pPacket)
{
	int ClientID = pPacket->m_ClientID;
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int Msg;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageID(&Msg, &Sys, &Uuid, &Unpacker, &Packer);
	if(Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}

	if(Result == UNPACKMESSAGE_ANSWER)
	{
		SendMsg(&Packer, MSGFLAG_VITAL, ClientID);
	}

	if(Sys)
	{
		// system message
		if(Msg == NETMSG_CLIENTVER)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_PREAUTH)
			{
				CUuid *pConnectionID = (CUuid *)Unpacker.GetRaw(sizeof(*pConnectionID));
				int DDNetVersion = Unpacker.GetInt();
				const char *pDDNetVersionStr = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Unpacker.Error() || !str_utf8_check(pDDNetVersionStr) || DDNetVersion < 0)
				{
					return;
				}
				m_aClients[ClientID].m_ConnectionID = *pConnectionID;
				m_aClients[ClientID].m_DDNetVersion = DDNetVersion;
				str_copy(m_aClients[ClientID].m_aDDNetVersionStr, pDDNetVersionStr, sizeof(m_aClients[ClientID].m_aDDNetVersionStr));
				m_aClients[ClientID].m_DDNetVersionSettled = true;
				m_aClients[ClientID].m_GotDDNetVersionPacket = true;
				m_aClients[ClientID].m_State = CClient::STATE_AUTH;
			}
		}
		if(Msg == NETMSG_CLIENTVER_INFCLASS)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0) // Ignore STATE_AUTH for now, see ddnet#4445 // && m_aClients[ClientID].m_State == CClient::STATE_AUTH)
			{
				int InfClassVersion = Unpacker.GetInt();
				if(Unpacker.Error() || InfClassVersion < 0)
				{
					return;
				}
				m_aClients[ClientID].m_InfClassVersion = InfClassVersion;
			}
		}
		else if(Msg == NETMSG_INFO)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_PREAUTH || m_aClients[ClientID].m_State == CClient::STATE_AUTH))
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
					m_NetServer.Drop(ClientID, CLIENTDROPTYPE_WRONG_VERSION, aReason);
					return;
				}

				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(!str_utf8_check(pPassword))
				{
					return;
				}
				if(!Config()->m_InfCaptcha && Config()->m_Password[0] != 0 && str_comp(Config()->m_Password, pPassword) != 0)
				{
					// wrong password
					m_NetServer.Drop(ClientID, CLIENTDROPTYPE_WRONG_PASSWORD, "Wrong password");
					return;
				}

				m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
				SendCapabilities(ClientID);
				SendMap(ClientID);
			}
		}
		else if(Msg == NETMSG_REQUEST_MAP_DATA)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) == 0 || m_aClients[ClientID].m_State < CClient::STATE_CONNECTING)
				return;

			int Chunk = Unpacker.GetInt();
			if(Chunk != m_aClients[ClientID].m_NextMapChunk || !Config()->m_SvFastDownload)
			{
				SendMapData(ClientID, Chunk);
				return;
			}

			if(Chunk == 0)
			{
				for(int i = 0; i < Config()->m_SvMapWindow; i++)
				{
					SendMapData(ClientID, i);
				}
			}
			SendMapData(ClientID, Config()->m_SvMapWindow + m_aClients[ClientID].m_NextMapChunk);
			m_aClients[ClientID].m_NextMapChunk++;
		}
		else if(Msg == NETMSG_READY)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_CONNECTING))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player is ready. ClientID=%d addr=<{%s}> secure=%s", ClientID, aAddrStr, m_NetServer.HasSecurityToken(ClientID) ? "yes" : "no");
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_aClients[ClientID].m_State = CClient::STATE_READY;
				m_aClients[ClientID].m_WaitingTime = TickSpeed()*g_Config.m_InfConWaitingTime;
			}
		}
		else if(Msg == NETMSG_ENTERGAME)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_READY && GameServer()->IsClientReady(ClientID))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player has entered the game. ClientID=%d addr=%s", ClientID, aAddrStr);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				m_aClients[ClientID].m_State = CClient::STATE_INGAME;
				
				if(m_aClients[ClientID].m_WaitingTime <= 0)
				{
					GameServer()->OnClientEnter(ClientID);
				}
			}
		}
		else if(Msg == NETMSG_INPUT)
		{
			CClient::CInput *pInput;
			int64_t TagTime;

			m_aClients[ClientID].m_LastAckedSnapshot = Unpacker.GetInt();
			int IntendedTick = Unpacker.GetInt();
			int Size = Unpacker.GetInt();

			// check for errors
			if(Unpacker.Error() || Size/4 > MAX_INPUT_SIZE)
				return;

			if(m_aClients[ClientID].m_LastAckedSnapshot > 0)
				m_aClients[ClientID].m_SnapRate = CClient::SNAPRATE_FULL;

			if(m_aClients[ClientID].m_Snapshots.Get(m_aClients[ClientID].m_LastAckedSnapshot, &TagTime, 0, 0) >= 0)
				m_aClients[ClientID].m_Latency = (int)(((time_get()-TagTime)*1000)/time_freq());

			// add message to report the input timing
			// skip packets that are old
			if(IntendedTick > m_aClients[ClientID].m_LastInputTick)
			{
				int TimeLeft = ((TickStartTime(IntendedTick)-time_get())*1000) / time_freq();

				CMsgPacker Msg(NETMSG_INPUTTIMING, true);
				Msg.AddInt(IntendedTick);
				Msg.AddInt(TimeLeft);
				SendMsg(&Msg, 0, ClientID);
			}

			m_aClients[ClientID].m_LastInputTick = IntendedTick;

			pInput = &m_aClients[ClientID].m_aInputs[m_aClients[ClientID].m_CurrentInput];

			if(IntendedTick <= Tick())
				IntendedTick = Tick()+1;

			pInput->m_GameTick = IntendedTick;

			for(int i = 0; i < Size/4; i++)
				pInput->m_aData[i] = Unpacker.GetInt();

			mem_copy(m_aClients[ClientID].m_LatestInput.m_aData, pInput->m_aData, MAX_INPUT_SIZE*sizeof(int));

			m_aClients[ClientID].m_CurrentInput++;
			m_aClients[ClientID].m_CurrentInput %= 200;

			// call the mod with the fresh input data
			if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
				GameServer()->OnClientDirectInput(ClientID, m_aClients[ClientID].m_LatestInput.m_aData);
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
				int Version = m_aClients[ClientID].m_DDNetVersion;
				if(GameServer()->PlayerExists(ClientID) && Version < VERSION_DDNET_OLD)
				{
					m_aClients[ClientID].m_DDNetVersion = VERSION_DDNET_OLD;
				}
			}
			else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0 && m_aClients[ClientID].m_Authed)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "ClientID=%d rcon='%s'", ClientID, pCmd);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_RconClientID = ClientID;
				m_RconAuthLevel = m_aClients[ClientID].m_Authed;
				switch(m_aClients[ClientID].m_Authed)
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
				Console()->ExecuteLineFlag(pCmd, CFGFLAG_SERVER, ClientID, false);
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				m_RconClientID = IServer::RCON_CID_SERV;
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
					SendRconLine(ClientID, "No rcon password set on server. Set sv_rcon_password and/or sv_rcon_mod_password to enable the remote console.");
				}
				else if(g_Config.m_SvRconTokenCheck && !m_NetServer.HasSecurityToken(ClientID))
				{
					SendRconLine(ClientID, "You must use a client that support anti-spoof protection (DDNet-like)");
				}
#ifdef CONF_SQL
				else if(m_aClients[ClientID].m_UserID < 0)
				{
					SendRconLine(ClientID, "You must be logged to your account. Please use /login");
				}
#endif
				else if(g_Config.m_SvRconPassword[0] && str_comp(pPw, g_Config.m_SvRconPassword) == 0)
				{
#ifdef CONF_SQL
					if(m_aClients[ClientID].m_UserLevel == SQL_USERLEVEL_ADMIN)
					{
#endif
						CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
						Msg.AddInt(1);	//authed
						Msg.AddInt(1);	//cmdlist
						SendMsg(&Msg, MSGFLAG_VITAL, ClientID);

						m_aClients[ClientID].m_Authed = AUTHED_ADMIN;
						GameServer()->OnSetAuthed(ClientID, m_aClients[ClientID].m_Authed);
						int SendRconCmds = Unpacker.GetInt();
						if(Unpacker.Error() == 0 && SendRconCmds)
							m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_ADMIN, CFGFLAG_SERVER);
						SendRconLine(ClientID, "Admin authentication successful. Full remote console access granted.");
						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "ClientID=%d authed (admin)", ClientID);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
#ifdef CONF_SQL
					}
					else
					{
						SendRconLine(ClientID, "You are not admin.");
					}
#endif
				}
				else if(g_Config.m_SvRconModPassword[0] && str_comp(pPw, g_Config.m_SvRconModPassword) == 0)
				{
#ifdef CONF_SQL
					if(m_aClients[ClientID].m_UserLevel == SQL_USERLEVEL_ADMIN || m_aClients[ClientID].m_UserLevel == SQL_USERLEVEL_MOD)
					{
#endif
						CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
						Msg.AddInt(1);	//authed
						Msg.AddInt(1);	//cmdlist
						SendMsg(&Msg, MSGFLAG_VITAL, ClientID);

						m_aClients[ClientID].m_Authed = AUTHED_MOD;
						int SendRconCmds = Unpacker.GetInt();
						if(Unpacker.Error() == 0 && SendRconCmds)
							m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_MOD, CFGFLAG_SERVER);
						SendRconLine(ClientID, "Moderator authentication successful. Limited remote console access granted.");
						char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "ClientID=%d authed (moderator)", ClientID);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
#ifdef CONF_SQL
					}
					else
					{
						SendRconLine(ClientID, "You are not moderator.");
					}
#endif
				}
				else if(g_Config.m_SvRconMaxTries)
				{
					m_aClients[ClientID].m_AuthTries++;
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Wrong password %d/%d.", m_aClients[ClientID].m_AuthTries, g_Config.m_SvRconMaxTries);
					SendRconLine(ClientID, aBuf);
					if(m_aClients[ClientID].m_AuthTries >= g_Config.m_SvRconMaxTries)
					{
						if(!g_Config.m_SvRconBantime)
							m_NetServer.Drop(ClientID, CLIENTDROPTYPE_KICK, "Too many remote console authentication tries");
						else
							m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientID), g_Config.m_SvRconBantime*60, "Too many remote console authentication tries");
					}
				}
				else
				{
					SendRconLine(ClientID, "Wrong password.");
				}
			}
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY, true);
			SendMsg(&Msg, 0, ClientID);
		}
		else if(Msg == NETMSG_PINGEX)
		{
			CUuid *pID = (CUuid *)Unpacker.GetRaw(sizeof(*pID));
			if(Unpacker.Error())
			{
				return;
			}
			CMsgPacker Msgp(NETMSG_PONGEX, true);
			Msgp.AddRaw(pID, sizeof(*pID));
			SendMsg(&Msgp, MSGFLAG_FLUSH, ClientID);
		}
		else
		{
			if(g_Config.m_Debug)
			{
				char aHex[] = "0123456789ABCDEF";
				char aBuf[512];

				for(int b = 0; b < pPacket->m_DataSize && b < 32; b++)
				{
					aBuf[b*3] = aHex[((const unsigned char *)pPacket->m_pData)[b]>>4];
					aBuf[b*3+1] = aHex[((const unsigned char *)pPacket->m_pData)[b]&0xf];
					aBuf[b*3+2] = ' ';
					aBuf[b*3+3] = 0;
				}

				char aBufMsg[256];
				str_format(aBufMsg, sizeof(aBufMsg), "strange message ClientID=%d msg=%d data_size=%d", ClientID, Msg, pPacket->m_DataSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBufMsg);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}
	else
	{
		// game message
		if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State >= CClient::STATE_READY)
			GameServer()->OnMessage(Msg, &Unpacker, ClientID);
	}
}

void CServer::SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type)
{
	const int MaxRequests = g_Config.m_SvServerInfoPerSecond;
	int64_t Now = Tick();
	if(abs(Now - m_ServerInfoFirstRequest) <= TickSpeed())
	{
		m_ServerInfoNumRequests++;
	}
	else
	{
		m_ServerInfoHighLoad = m_ServerInfoNumRequests > MaxRequests;
		m_ServerInfoNumRequests = 1;
		m_ServerInfoFirstRequest = Now;
	}

	if(!m_ServerInfoHighLoad)
	{
		m_ServerInfoRequestLogTick = 0;
		m_ServerInfoRequestLogRecords = 0;
	}

	bool SendResponse = m_ServerInfoNumRequests <= MaxRequests && !m_ServerInfoHighLoad;
	if(!SendResponse) {
		constexpr int MaxRecords = 50;
		constexpr int MaxRecordsTime = 20; // Seconds

		if(m_ServerInfoRequestLogRecords > MaxRecords && Now < m_ServerInfoRequestLogTick + TickSpeed() * MaxRecordsTime)
		{
			return;
		}

		if(Now >= m_ServerInfoRequestLogTick + TickSpeed() * MaxRecordsTime)
		{
			m_ServerInfoRequestLogTick = Now;
			m_ServerInfoRequestLogRecords = 0;
		}

		m_ServerInfoRequestLogRecords++;

		char aBuf[256];
		char aAddrStr[256];
		net_addr_str(pAddr, aAddrStr, sizeof(aAddrStr), true);
		str_format(aBuf, sizeof(aBuf), "Too many info requests from %s: %d > %d (Now = %" PRId64 ", mSIFR = %" PRId64 ")",
			aAddrStr, m_ServerInfoNumRequests, MaxRequests, Now, m_ServerInfoFirstRequest);
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "inforequests", aBuf);
		return;
	}

	bool SendClients = m_ServerInfoNumRequests <= MaxRequests && !m_ServerInfoHighLoad;
	SendServerInfo(pAddr, Token, Type, SendClients);
}

void CServer::SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients)
{
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
		str_format(aBuf, sizeof(aBuf), "%d", x); \
		(p).AddString(aBuf, 0); \
	} while(0)

	switch(Type)
	{
	case SERVERINFO_EXTENDED: ADD_RAW(p, SERVERBROWSE_INFO_EXTENDED); break;
	case SERVERINFO_64_LEGACY: ADD_RAW(p, SERVERBROWSE_INFO_64_LEGACY); break;
	case SERVERINFO_VANILLA: ADD_RAW(p, SERVERBROWSE_INFO); break;
	case SERVERINFO_INGAME: ADD_RAW(p, SERVERBROWSE_INFO); break;
	default: dbg_assert(false, "unknown serverinfo type");
	}

	ADD_INT(p, Token);

	p.AddString(GameServer()->Version(), 32);
	
	//Add captcha if needed
	if(g_Config.m_InfCaptcha)
	{
		str_format(aBuf, sizeof(aBuf), "%s - PASSWORD: %s", g_Config.m_SvName, m_NetServer.GetCaptcha(pAddr));
	}
	else
	{
#ifdef CONF_SQL
		if(g_Config.m_InfChallenge)
		{
			lock_wait(m_ChallengeLock);
			int ScoreType = ChallengeTypeToScoreType(m_ChallengeType);
			switch(ScoreType)
			{
				case SQL_SCORETYPE_ENGINEER_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "EngineerOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_MERCENARY_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "MercenaryOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_SCIENTIST_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "ScientistOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_BIOLOGIST_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "BiologistOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_LOOPER_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "LooperOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_NINJA_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "NinjaOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_SOLDIER_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "SoldierOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_SNIPER_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "SniperOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_MEDIC_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "MedicOfTheDay", m_aChallengeWinner);
					break;
				case SQL_SCORETYPE_HERO_SCORE:
					str_format(aBuf, sizeof(aBuf), "%s | %s: %s", g_Config.m_SvName, "HeroOfTheDay", m_aChallengeWinner);
					break;
			}
			lock_release(m_ChallengeLock);
		}
#else
		memcpy(aBuf, g_Config.m_SvName, sizeof(aBuf));
#endif
	}

	const char *pMapName = GetMapName();
	if(g_Config.m_SvHideInfo)
	{
		// Full hide
		ClientCount = 0;
		PlayerCount = 0;
		SendClients = false;
		pMapName = "";
	}
	else if (g_Config.m_SvInfoMaxClients >= 0)
	{
		ClientCount = minimum(ClientCount, g_Config.m_SvInfoMaxClients);
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
			char aNameBuf[64];
			str_format(aNameBuf, sizeof(aNameBuf), "%s [%d/%d]", aBuf, ClientCount, m_NetServer.MaxClients());
			p.AddString(aBuf, 64);
		}
	}
	p.AddString(pMapName, 32);

	if(Type == SERVERINFO_EXTENDED)
	{
		ADD_INT(p, m_CurrentMapCrc);
		ADD_INT(p, m_CurrentMapSize);
	}

	// gametype
	p.AddString(GameServer()->GameType(), 16);

	// flags
	ADD_INT(p, g_Config.m_Password[0] ? SERVER_FLAG_PASSWORD : 0);

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
	ADD_INT(p, MaxClients-g_Config.m_SvSpectatorSlots); // max players
	ADD_INT(p, ClientCount); // num clients
	ADD_INT(p, MaxClients); // max clients

	if(Type == SERVERINFO_EXTENDED)
		p.AddString("", 0); // extra info, reserved

	const void *pPrefix = p.Data();
	int PrefixSize = p.Size();

	CPacker pp;
	CNetChunk Packet;
	int PacketsSent = 0;
	int PlayersSent = 0;
	Packet.m_ClientID = -1;
	Packet.m_Address = *pAddr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;

	#define SEND(size) \
		do \
		{ \
			Packet.m_pData = pp.Data(); \
			Packet.m_DataSize = size; \
			m_NetServer.Send(&Packet); \
			PacketsSent++; \
		} while(0)

	#define RESET() \
		do \
		{ \
			pp.Reset(); \
			pp.AddRaw(pPrefix, PrefixSize); \
		} while(0)

	RESET();

	if(Type == SERVERINFO_64_LEGACY)
		pp.AddInt(PlayersSent); // offset

	if(!SendClients)
	{
		SEND(pp.Size());
		return;
	}

	if(Type == SERVERINFO_EXTENDED)
	{
		pPrefix = SERVERBROWSE_INFO_EXTENDED_MORE;
		PrefixSize = sizeof(SERVERBROWSE_INFO_EXTENDED_MORE);
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
				SEND(pp.Size());
				RESET();
				pp.AddInt(PlayersSent); // offset
				Remaining = 24;
			}
			if(Remaining > 0)
			{
				Remaining--;
			}

			int PreviousSize = pp.Size();

			pp.AddString(ClientName(i), MAX_NAME_LENGTH); // client name
			pp.AddString(ClientClan(i), MAX_CLAN_LENGTH); // client clan

			ADD_INT(pp, m_aClients[i].m_Country); // client country
			ADD_INT(pp, RoundStatistics()->PlayerScore(i)); // client score
			ADD_INT(pp, GameServer()->IsClientPlayer(i) ? 1 : 0); // is player?
			if(Type == SERVERINFO_EXTENDED)
				pp.AddString("", 0); // extra info, reserved

			if(Type == SERVERINFO_EXTENDED)
			{
				if(pp.Size() >= NET_MAX_PAYLOAD)
				{
					// Retry current player.
					i--;
					SEND(PreviousSize);
					RESET();
					ADD_INT(pp, Token);
					ADD_INT(pp, PacketsSent);
					pp.AddString("", 0); // extra info, reserved
					continue;
				}
			}
			PlayersSent++;
		}
	}

	SEND(pp.Size());
	#undef SEND
	#undef RESET
	#undef ADD_RAW
	#undef ADD_INT
}

void CServer::UpdateServerInfo()
{
	if(m_RunServer == UNINITIALIZED)
		return;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			SendServerInfo(m_NetServer.ClientAddr(i), -1, SERVERINFO_INGAME, false);
		}
	}
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
			if(Packet.m_ClientID == -1)
			{
				// stateless
				if(!m_Register.RegisterProcessPacket(&Packet))
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
					if(Type != -1)
					{
						int Token = ((unsigned char *)Packet.m_pData)[sizeof(SERVERBROWSE_GETINFO)];
						Token |= ExtraToken << 8;
						SendServerInfoConnless(&Packet.m_Address, Token, Type);
					}
				}
			}
			else
				ProcessClientPacket(&Packet);
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
/* INFECTION MODIFICATION START ***************************************/
	const char *pMapFileName = EventsDirector::GetEventMapName(pMapName);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapFileName);

	if(!GenerateClientMap(aBuf, pMapFileName))
	{
		if(str_comp(pMapFileName, pMapName) == 0)
			return 0;

		pMapFileName = pMapName;
		str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapFileName);

		if(!GenerateClientMap(aBuf, pMapFileName))
			return 0;
	}
/* INFECTION MODIFICATION END *****************************************/

	str_format(aBuf, sizeof(aBuf), "map_loaded name='%s' file='maps/%s.map'", pMapName, pMapFileName);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

	// stop recording when we change map
	m_aDemoRecorder[0].Stop();
	
	// reinit snapshot ids
	m_IDPool.TimeoutIDs();

/* INFECTION MODIFICATION START ***************************************/
	str_copy(m_aPreviousMap, m_aCurrentMap, sizeof(m_aPreviousMap));
	str_copy(m_aCurrentMap, pMapName, sizeof(m_aCurrentMap));
	ResetMapVotes();

/* INFECTION MODIFICATION END *****************************************/

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aPrevStates[i] = m_aClients[i].m_State;

	return 1;
}

void CServer::InitRegister(CNetServer *pNetServer, IEngineMasterServer *pMasterServer, IConsole *pConsole)
{
	m_Register.Init(pNetServer, pMasterServer, pConsole);
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

	// start server
	NETADDR BindAddr;
	if(!g_Config.m_Bindaddr[0] || net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) != 0)
		mem_zero(&BindAddr, sizeof(BindAddr));

	BindAddr.type = NETTYPE_ALL;

	int Port = Config()->m_SvPort;
	for(BindAddr.port = Port != 0 ? Port : 8303; !m_NetServer.Open(BindAddr, &m_ServerBan, Config()->m_SvMaxClients, Config()->m_SvMaxClientsPerIP); BindAddr.port++)
	{
		if(Port != 0 || BindAddr.port >= 8310)
		{
			dbg_msg("server", "couldn't open socket. port %d might already be in use", BindAddr.port);
			return -1;
		}
	}

	if(Port == 0)
		dbg_msg("server", "using port %d", BindAddr.port);

	if(g_Config.m_InfCaptcha)
	{
		InitCaptcha();
		if(!m_NetServer.IsCaptchaInitialized())
		{
			dbg_msg("server", "failed to create captcha list -> disable captcha");
			g_Config.m_InfCaptcha = 0;
		}
	}

	m_NetServer.SetCallbacks(NewClientCallback, NewClientNoAuthCallback, ClientRejoinCallback, DelClientCallback, this);

	m_Econ.Init(Config(), Console(), &m_ServerBan);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "server name is '%s'", Config()->m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	GameServer()->OnInit();
	if(ErrorShutdown())
	{
		m_RunServer = STOPPING;
	}
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "version " GAME_RELEASE_VERSION " on " CONF_PLATFORM_STRING " " CONF_ARCH_STRING);
	if(GIT_SHORTREV_HASH)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Git revision hash: %s", GIT_SHORTREV_HASH);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

	// process pending commands
	m_pConsole->StoreCommands(false);

	// start game
	{
		bool NonActive = false;
		bool PacketWaiting = false;

		m_Lastheartbeat = 0;
		m_GameStartTime = time_get();

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
			if(str_comp(g_Config.m_SvMap, m_aCurrentMap) != 0 || m_MapReload)
			{
				m_MapReload = 0;

				// load map
				if(LoadMap(g_Config.m_SvMap))
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

					GameServer()->OnShutdown();

					for(int ClientID = 0; ClientID < MAX_CLIENTS; ClientID++)
					{
						if(m_aClients[ClientID].m_State <= CClient::STATE_AUTH)
							continue;

						SendMap(ClientID);
						bool HasPersistentData = m_aClients[ClientID].m_HasPersistentData;
/* INFECTION MODIFICATION START ***************************************/
						m_aClients[ClientID].Reset(false);
/* INFECTION MODIFICATION END *****************************************/
						m_aClients[ClientID].m_HasPersistentData = HasPersistentData;
						m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
						SetClientMemory(ClientID, CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE, true);
					}

					m_GameStartTime = time_get();
					m_CurrentGameTick = 0;
					m_ServerInfoFirstRequest = 0;
					Kernel()->ReregisterInterface(GameServer());
					GameServer()->OnInit();
					UpdateServerInfo();
				}
				else
				{
					str_format(aBuf, sizeof(aBuf), "failed to load map. mapname='%s'", g_Config.m_SvMap);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
					str_copy(g_Config.m_SvMap, m_aCurrentMap, sizeof(g_Config.m_SvMap));
				}
			}

			while(t > TickStartTime(m_CurrentGameTick+1))
			{
				m_CurrentGameTick++;
				NewTicks++;

				//Check for name collision. We add this because the login is in a different thread and can't check it himself.
				for(int i=MAX_CLIENTS-1; i>=0; i--)
				{
					if(m_aClients[i].m_State >= CClient::STATE_READY && m_aClients[i].m_Session.m_MuteTick > 0)
						m_aClients[i].m_Session.m_MuteTick--;
				}
				
				for(int ClientID=0; ClientID<MAX_CLIENTS; ClientID++)
				{
					if(m_aClients[ClientID].m_WaitingTime > 0)
					{
						m_aClients[ClientID].m_WaitingTime--;
						if(m_aClients[ClientID].m_WaitingTime <= 0)
						{
							if(m_aClients[ClientID].m_State == CClient::STATE_READY)
							{
								void *pPersistentData = 0;
								if(m_aClients[ClientID].m_HasPersistentData)
								{
									pPersistentData = m_aClients[ClientID].m_pPersistentData;
									m_aClients[ClientID].m_HasPersistentData = false;
								}

								GameServer()->OnClientConnected(ClientID, pPersistentData);
								SendConnectionReady(ClientID);
							}
							else if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
							{
								GameServer()->OnClientEnter(ClientID);
							}
						}
					}
				}
				
				// apply new input
				for(int c = 0; c < MAX_CLIENTS; c++)
				{
					if(m_aClients[c].m_State != CClient::STATE_INGAME)
						continue;
					for(int i = 0; i < 200; i++)
					{
						if(m_aClients[c].m_aInputs[i].m_GameTick == Tick())
						{
							GameServer()->OnClientPredictedInput(c, m_aClients[c].m_aInputs[i].m_aData);
							break;
						}
					}
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
			}

			// snap game
			if(NewTicks)
			{
				if(g_Config.m_SvHighBandwidth || (m_CurrentGameTick%2) == 0)
					DoSnapshot();

				UpdateClientRconCommands();
			}

			// master server stuff
			m_Register.RegisterUpdate(m_NetServer.NetType());

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
			m_NetServer.Drop(i, CLIENTDROPTYPE_SHUTDOWN, pDisconnectReason);
	}

	m_Econ.Shutdown();

	GameServer()->OnShutdown();
	m_pMap->Unload();

	free(m_pCurrentMapData);
		
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

	return 0;
}

void CServer::ConUnmute(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	const char *pStr = pResult->GetString(0);
	
	if(str_isallnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
			pThis->m_aClients[ClientID].m_Session.m_MuteTick = 0;
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
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
		{
			int Time = 60*Minutes;
			pThis->m_aClients[ClientID].m_Session.m_MuteTick = pThis->TickSpeed()*60*Minutes;
			pThis->GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_ACCUSATION, _("{str:Victim} has been muted for {sec:Duration} ({str:Reason})"), "Victim", pThis->ClientName(ClientID) ,"Duration", &Time, "Reason", pReason, NULL);
		}
	}
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
}

void CServer::ConWhisper(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	const char *pStrClientID = pResult->GetString(0);
	const char *pText = pResult->GetString(1);

	if(str_isallnum(pStrClientID))
	{
		int ClientID = str_toint(pStrClientID);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
		{
			// Send to target
			CNetMsg_Sv_Chat Msg;
			Msg.m_Team = 0;
			Msg.m_ClientID = -1;
			Msg.m_pMessage = pText;
			pThis->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

			// Confirm message sent
			char aBuf[1024];
			str_format(aBuf, sizeof(aBuf), "Whisper '%s' sent to %s",
				pText,
				pThis->ClientName(ClientID)
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

void CServer::DemoRecorder_HandleAutoStart()
{
	if(g_Config.m_SvAutoDemoRecord)
	{
		m_aDemoRecorder[0].Stop();
		char aFilename[128];
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", "auto/autorecord", aDate);
		m_aDemoRecorder[0].Start(Storage(), m_pConsole, aFilename, GameServer()->NetVersion(), m_aCurrentMap, m_CurrentMapSha256, m_CurrentMapCrc, "server");
		if(g_Config.m_SvAutoDemoMax)
		{
			// clean up auto recorded demos
			CFileCollection AutoDemos;
			AutoDemos.Init(Storage(), "demos/server", "autorecord", ".demo", g_Config.m_SvAutoDemoMax);
		}
	}
}

bool CServer::DemoRecorder_IsRecording()
{
	return m_aDemoRecorder[0].IsRecording();
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
	pServer->m_aDemoRecorder[0].Start(pServer->Storage(), pServer->Console(), aFilename, pServer->GameServer()->NetVersion(), pServer->m_aCurrentMap, pServer->m_CurrentMapSha256, pServer->m_CurrentMapCrc, "server");
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

	if(pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		pServer->LogoutClient(pServer->m_RconClientID, "");
	}
}

void CServer::ConShowIps(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(pResult->NumArguments())
		{
			pServer->m_aClients[pServer->m_RconClientID].m_ShowIps = pResult->GetInteger(0);
		}
		else
		{
			char aStr[9];
			str_format(aStr, sizeof(aStr), "Value: %d", pServer->m_aClients[pServer->m_RconClientID].m_ShowIps);
			char aBuf[32];
			pServer->SendRconLine(pServer->m_RconClientID, pServer->Console()->Format(aBuf, sizeof(aBuf), "server", aStr));
		}
	}
}

void CServer::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		((CServer *)pUserData)->UpdateServerInfo();
	}
}

void CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		((CServer *)pUserData)->m_NetServer.SetMaxClientsPerIP(pResult->GetInteger(0));
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

/* DDNET MODIFICATION START *******************************************/
#ifdef CONF_SQL
void CServer::ConAddSqlServer(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	if (pResult->NumArguments() != 7 && pResult->NumArguments() != 8)
		return;

	bool ReadOnly;
	if (str_comp_nocase(pResult->GetString(0), "w") == 0)
		ReadOnly = false;
	else if (str_comp_nocase(pResult->GetString(0), "r") == 0)
		ReadOnly = true;
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return;
	}

	bool SetUpDb = pResult->NumArguments() == 8 ? pResult->GetInteger(7) : false;

	CSqlServer** apSqlServers = ReadOnly ? pSelf->m_apSqlReadServers : pSelf->m_apSqlWriteServers;

	for (int i = 0; i < MAX_SQLSERVERS; i++)
	{
		if (!apSqlServers[i])
		{
			apSqlServers[i] = new CSqlServer(pResult->GetString(1), pResult->GetString(2), pResult->GetString(3), pResult->GetString(4), pResult->GetString(5), pResult->GetInteger(6), ReadOnly, SetUpDb);

			if(SetUpDb)
			{
				void *TablesThread = thread_init(CreateTablesThread, apSqlServers[i]);
				thread_detach(TablesThread);
			}

			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "Added new Sql%sServer: %d: DB: '%s' Prefix: '%s' User: '%s' IP: '%s' Port: %d", ReadOnly ? "Read" : "Write", i, apSqlServers[i]->GetDatabase(), apSqlServers[i]->GetPrefix(), apSqlServers[i]->GetUser(), apSqlServers[i]->GetIP(), apSqlServers[i]->GetPort());
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return true;
		}
	}
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "failed to add new sqlserver: limit of sqlservers reached");
}

void CServer::ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	bool ReadOnly;
	if (str_comp_nocase(pResult->GetString(0), "w") == 0)
		ReadOnly = false;
	else if (str_comp_nocase(pResult->GetString(0), "r") == 0)
		ReadOnly = true;
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return;
	}

	CSqlServer** apSqlServers = ReadOnly ? pSelf->m_apSqlReadServers : pSelf->m_apSqlWriteServers;

	for (int i = 0; i < MAX_SQLSERVERS; i++)
	{
		if (apSqlServers[i])
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "SQL-%s %d: DB: '%s' Prefix: '%s' User: '%s' Pass: '%s' IP: '%s' Port: %d", ReadOnly ? "Read" : "Write", i, apSqlServers[i]->GetDatabase(), apSqlServers[i]->GetPrefix(), apSqlServers[i]->GetUser(), apSqlServers[i]->GetPass(), apSqlServers[i]->GetIP(), apSqlServers[i]->GetPort());
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		}
	}
}

void CServer::CreateTablesThread(void *pData)
{
	((CSqlServer *)pData)->CreateTables();
}
#endif

/* DDNET MODIFICATION END *********************************************/

void CServer::ConSetWeaponFireDelay(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;
	if(pResult->NumArguments() != 2)
		return;

	int WeaponID = pResult->GetInteger(0);
	if((WeaponID < 0) || (WeaponID >= NB_INFWEAPON))
	{
		return;
	}
	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return;
	}

	pSelf->SetFireDelay(static_cast<INFWEAPON>(WeaponID), Interval);
}

void CServer::ConSetWeaponAmmoRegen(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;
	if(pResult->NumArguments() != 2)
		return;

	int WeaponID = pResult->GetInteger(0);
	if((WeaponID < 0) || (WeaponID >= NB_INFWEAPON))
	{
		return;
	}
	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return;
	}

	pSelf->SetAmmoRegenTime(static_cast<INFWEAPON>(WeaponID), Interval);

	return;
}

void CServer::ConSetWeaponMaxAmmo(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;
	if(pResult->NumArguments() != 2)
		return;

	int WeaponID = pResult->GetInteger(0);
	if((WeaponID < 0) || (WeaponID >= NB_INFWEAPON))
	{
		return;
	}
	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return;
	}

	pSelf->SetMaxAmmo(static_cast<INFWEAPON>(WeaponID), Interval);
}

void CServer::LogoutClient(int ClientID, const char *pReason)
{
	CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
	Msg.AddInt(0); //authed
	Msg.AddInt(0); //cmdlist
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);

	m_aClients[ClientID].m_AuthTries = 0;
	m_aClients[ClientID].m_pRconCmdToSend = 0;

	char aBuf[64];
	if(*pReason)
	{
		str_format(aBuf, sizeof(aBuf), "Logged out by %s.", pReason);
		SendRconLine(ClientID, aBuf);
		str_format(aBuf, sizeof(aBuf), "ClientID=%d logged out by %s", ClientID, pReason);
	}
	else
	{
		SendRconLine(ClientID, "Logout successful.");
		str_format(aBuf, sizeof(aBuf), "ClientID=%d logged out", ClientID);
	}

	m_aClients[ClientID].m_Authed = AUTHED_NO;

	GameServer()->OnSetAuthed(ClientID, AUTHED_NO);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CServer::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGameServer = Kernel()->RequestInterface<IGameServer>();
	m_pMap = Kernel()->RequestInterface<IEngineMap>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	// register console commands
	Console()->Register("kick", "i[id] ?r[reason]", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "?r[name]", CFGFLAG_SERVER, ConStatus, this, "List players containing name or all players");
	Console()->Register("shutdown", "?r[reason]", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("logout", "", CFGFLAG_SERVER, ConLogout, this, "Logout of rcon");
	Console()->Register("show_ips", "?i[show]", CFGFLAG_SERVER, ConShowIps, this, "Show IP addresses in rcon commands (1 = on, 0 = off)");

	Console()->Register("record", "?s[file]", CFGFLAG_SERVER | CFGFLAG_STORE, ConRecord, this, "Record to a file");
	Console()->Register("stoprecord", "", CFGFLAG_SERVER, ConStopRecord, this, "Stop recording");

	Console()->Register("reload", "", CFGFLAG_SERVER, ConMapReload, this, "Reload the map");

	Console()->Register("name_ban", "s[name] ?i[distance] ?i[is_substring] ?r[reason]", CFGFLAG_SERVER, ConNameBan, this, "Ban a certain nickname");
	Console()->Register("name_unban", "s[name]", CFGFLAG_SERVER, ConNameUnban, this, "Unban a certain nickname");
	Console()->Register("name_bans", "", CFGFLAG_SERVER, ConNameBans, this, "List all name bans");

	Console()->Chain("sv_name", ConchainSpecialInfoupdate, this);
	Console()->Chain("password", ConchainSpecialInfoupdate, this);

	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("mod_command", ConchainModCommandUpdate, this);

	Console()->Register("mute", "s[clientid] ?i[minutes] ?r[reason]", CFGFLAG_SERVER, ConMute, this, "Mute player with specified id for x minutes for any reason");
	Console()->Register("unmute", "s[clientid]", CFGFLAG_SERVER, ConUnmute, this, "Unmute player with specified id");
	Console()->Register("whisper", "s[id] r[txt]", CFGFLAG_SERVER, ConWhisper, this, "Analogous to 'Say' but sent to a single client only");

/* INFECTION MODIFICATION START ***************************************/
#ifdef CONF_SQL
	Console()->Register("inf_add_sqlserver", "ssssssi?i", CFGFLAG_SERVER, ConAddSqlServer, this, "add a sqlserver");
	Console()->Register("inf_list_sqlservers", "s", CFGFLAG_SERVER, ConDumpSqlServers, this, "list all sqlservers readservers = r, writeservers = w");
#endif

	Console()->Register("inf_set_weapon_fire_delay", "i[weapon] i[msec]", CFGFLAG_SERVER, ConSetWeaponFireDelay, this,
		"Set InfClass weapon fire delay");
	Console()->Register("inf_set_weapon_ammo_regen", "i[weapon] i[msec]", CFGFLAG_SERVER, ConSetWeaponAmmoRegen, this,
		"Set InfClass weapon ammo regen interval");
	Console()->Register("inf_set_weapon_max_ammo", "i[weapon] i[ammo]", CFGFLAG_SERVER, ConSetWeaponMaxAmmo, this,
		"Set InfClass weapon max ammo");
	/* INFECTION MODIFICATION END *************CServer::~CServer()****************************/

	// register console commands in sub parts
	m_ServerBan.InitServerBan(Console(), Storage(), this);
	m_NetSession.Init();
	m_NetAccusation.Init();
	m_pGameServer->OnConsoleInit();
}


int CServer::SnapNewID()
{
	return m_IDPool.NewID();
}

void CServer::SnapFreeID(int ID)
{
	m_IDPool.FreeID(ID);
}

void *CServer::SnapNewItem(int Type, int ID, int Size)
{
	dbg_assert(ID >= -1 && ID <= 0xffff, "incorrect id");
	return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

int CServer::GetClientInfclassVersion(int ClientID) const
{
	if(ClientID == SERVER_DEMO_CLIENT)
	{
		return 1000;
	}

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		return m_aClients[ClientID].m_InfClassVersion;
	}

	return 0;
}

CServer *CreateServer() { return new CServer(); }

// DDRace

void CServer::GetClientAddr(int ClientID, NETADDR *pAddr) const
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		*pAddr = *m_NetServer.ClientAddr(ClientID);
	}
}

/* INFECTION MODIFICATION START ***************************************/
int CServer::GetClientDefaultScoreMode(int ClientID)
{
	return m_aClients[ClientID].m_DefaultScoreMode;
}

void CServer::SetClientDefaultScoreMode(int ClientID, int Value)
{
	m_aClients[ClientID].m_DefaultScoreMode = Value;
}

const char* CServer::GetClientLanguage(int ClientID)
{
	return m_aClients[ClientID].m_aLanguage;
}

void CServer::SetClientLanguage(int ClientID, const char* pLanguage)
{
	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

	dbg_msg("lang", "set_language ClientID=%d lang=%s addr=%s", ClientID, pLanguage, aAddrStr);
	str_copy(m_aClients[ClientID].m_aLanguage, pLanguage, sizeof(m_aClients[ClientID].m_aLanguage));
}

int CServer::GetFireDelay(INFWEAPON WID)
{
	return m_InfFireDelay[static_cast<int>(WID)];
}

void CServer::SetFireDelay(INFWEAPON WID, int Time)
{
	m_InfFireDelay[static_cast<int>(WID)] = Time;
}

int CServer::GetAmmoRegenTime(INFWEAPON WID)
{
	return m_InfAmmoRegenTime[static_cast<int>(WID)];
}

void CServer::SetAmmoRegenTime(INFWEAPON WID, int Time)
{
	m_InfAmmoRegenTime[static_cast<int>(WID)] = Time;
}

int CServer::GetMaxAmmo(INFWEAPON WID)
{
	return m_InfMaxAmmo[static_cast<int>(WID)];
}

void CServer::SetMaxAmmo(INFWEAPON WID, int n)
{
	m_InfMaxAmmo[static_cast<int>(WID)] = n;
}

int CServer::GetClientNbRound(int ClientID)
{
	return m_aClients[ClientID].m_NbRound;
}

void CServer::SetPlayerClassEnabled(int PlayerClass, bool Enabled)
{
	const int Value = Enabled ? 1 : 0;
	switch (PlayerClass)
	{
		case PLAYERCLASS_MERCENARY:
			g_Config.m_InfEnableMercenary = Value;
			break;
		case PLAYERCLASS_MEDIC:
			g_Config.m_InfEnableMedic = Value;
			break;
		case PLAYERCLASS_HERO:
			g_Config.m_InfEnableHero = Value;
			break;
		case PLAYERCLASS_ENGINEER:
			g_Config.m_InfEnableEngineer = Value;
			break;
		case PLAYERCLASS_SOLDIER:
			g_Config.m_InfEnableSoldier = Value;
			break;
		case PLAYERCLASS_NINJA:
			g_Config.m_InfEnableNinja = Value;
			break;
		case PLAYERCLASS_SNIPER:
			g_Config.m_InfEnableSniper = Value;
			break;
		case PLAYERCLASS_SCIENTIST:
			g_Config.m_InfEnableScientist = Value;
			break;
		case PLAYERCLASS_BIOLOGIST:
			g_Config.m_InfEnableBiologist = Value;
			break;
		case PLAYERCLASS_LOOPER:
			g_Config.m_InfEnableLooper = Value;
			break;
		default:
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "WARNING: Invalid SetPlayerClassEnabled() call");
			return;
	}
}

void CServer::SetPlayerClassProbability(int PlayerClass, int Probability)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_SMOKER:
			g_Config.m_InfProbaSmoker = Probability;
			break;
		case PLAYERCLASS_BOOMER:
			g_Config.m_InfProbaBoomer = Probability;
			break;
		case PLAYERCLASS_HUNTER:
			g_Config.m_InfProbaHunter = Probability;
			break;
		case PLAYERCLASS_BAT:
			g_Config.m_InfProbaBat = Probability;
			break;
		case PLAYERCLASS_GHOST:
			g_Config.m_InfProbaGhost = Probability;
			break;
		case PLAYERCLASS_SPIDER:
			g_Config.m_InfProbaSpider = Probability;
			break;
		case PLAYERCLASS_GHOUL:
			g_Config.m_InfProbaGhoul = Probability;
			break;
		case PLAYERCLASS_SLUG:
			g_Config.m_InfProbaSlug = Probability;
			break;
		case PLAYERCLASS_VOODOO:
			g_Config.m_InfProbaVoodoo = Probability;
			break;
		case PLAYERCLASS_WITCH:
			g_Config.m_InfProbaWitch = Probability;
			break;
		case PLAYERCLASS_UNDEAD:
			g_Config.m_InfProbaUndead = Probability;
			break;
		default:
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "WARNING: Invalid SetPlayerClassProbability() call");
			return;
	}
}

bool CServer::IsClientLogged(int ClientID)
{
	return m_aClients[ClientID].m_UserID >= 0;
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
	int m_ClientID;
	char m_aText[512];
	
public:
	CGameServerCmd_SendChatMOTD(int ClientID, const char* pText)
	{
		m_ClientID = ClientID;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendMOTD(m_ClientID, m_aText);
	}
};

class CGameServerCmd_SendChatTarget : public CServer::CGameServerCmd
{
private:
	int m_ClientID;
	char m_aText[128];
	
public:
	CGameServerCmd_SendChatTarget(int ClientID, const char* pText)
	{
		m_ClientID = ClientID;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendChatTarget(m_ClientID, m_aText);
	}
};

class CGameServerCmd_SendChatTarget_Language : public CServer::CGameServerCmd
{
private:
	int m_ClientID;
	int m_ChatCategory;
	char m_aText[128];
	
public:
	CGameServerCmd_SendChatTarget_Language(int ClientID, int ChatCategory, const char* pText)
	{
		m_ClientID = ClientID;
		m_ChatCategory = ChatCategory;
		str_copy(m_aText, pText, sizeof(m_aText));
	}

	virtual void Execute(IGameServer* pGameServer)
	{
		pGameServer->SendChatTarget_Localization(m_ClientID, m_ChatCategory, m_aText, NULL);
	}
};

class CSqlJob_Server_Login : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sName;
	CSqlString<64> m_sPasswordHash;
	
public:
	CSqlJob_Server_Login(CServer* pServer, int ClientID, const char* pName, const char* pPasswordHash)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
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
				if(m_pServer->m_aClients[m_ClientID].m_LogInstance == GetInstance() && m_pServer->m_aClients[m_ClientID].m_UserID == -1)
				{
					int UserID = (int)pSqlServer->GetResults()->getInt("UserId");
					int UserLevel = (int)pSqlServer->GetResults()->getInt("Level");
					m_pServer->m_aClients[m_ClientID].m_UserID = UserID;
					m_pServer->m_aClients[m_ClientID].m_UserLevel = UserLevel;

					char aOldName[MAX_NAME_LENGTH];
					str_copy(aOldName, m_pServer->m_aClients[m_ClientID].m_aName, sizeof(aOldName));
					str_copy(m_pServer->m_aClients[m_ClientID].m_aUsername, m_sName.Str(), sizeof(m_pServer->m_aClients[m_ClientID].m_aUsername));

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "change_name previous='%s' now='%s'", aOldName, m_pServer->m_aClients[m_ClientID].m_aUsername);
					m_pServer->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

					//If we are really unlucky, the client can deconnect and another one connect during this small code
					if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
					{
						m_pServer->m_aClients[m_ClientID].m_UserID = -1;
						m_pServer->m_aClients[m_ClientID].m_UserLevel = SQL_USERLEVEL_NORMAL;
					}
					else
					{
						str_format(aBuf, sizeof(aBuf), "%s logged in (id: %d)", m_pServer->m_aClients[m_ClientID].m_aUsername,
							m_pServer->m_aClients[m_ClientID].m_UserID);
						CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(-1, aBuf);
						m_pServer->AddGameServerCmd(pCmd);
					}
				}
				else {
					str_format(aBuf, sizeof(aBuf), "You are already logged in.");
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientID, aBuf);
					m_pServer->AddGameServerCmd(pCmd);
				}
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("Wrong username/password."));
				m_pServer->AddGameServerCmd(pCmd);
			}
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, "An error occured during the logging.");
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't check username/password (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
	
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};

void CServer::Login(int ClientID, const char* pUsername, const char* pPassword)
{
	if(m_aClients[ClientID].m_LogInstance >= 0)
		return;
	
	char aHash[64]; //Result
	mem_zero(aHash, sizeof(aHash));
	Crypt(pPassword, (const unsigned char*) "d9", 1, 16, aHash);
	
	CSqlJob* pJob = new CSqlJob_Server_Login(this, ClientID, pUsername, aHash);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

void CServer::Logout(int ClientID)
{
	m_aClients[ClientID].m_UserID = -1;
	m_aClients[ClientID].m_UserLevel = SQL_USERLEVEL_NORMAL;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "change_name previous='%s' now='%s'", m_aClients[ClientID].m_aUsername, m_aClients[ClientID].m_aName);
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

class CSqlJob_Server_SetEmail : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	int m_UserID;
	CSqlString<64> m_sEmail;
	
public:
	CSqlJob_Server_SetEmail(CServer* pServer, int ClientID, int UserID, const char* pEmail)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_UserID = UserID;
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
				, pSqlServer->GetPrefix(), m_sEmail.ClrStr(), m_UserID);
			
			pSqlServer->executeSqlQuery(aBuf);
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("An error occured during the operation."));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't change email (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
};

void CServer::SetEmail(int ClientID, const char* pEmail)
{
	if(m_aClients[ClientID].m_UserID < 0 && m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("You must be logged for this operation"), NULL);
	}
	else
	{
		CSqlJob* pJob = new CSqlJob_Server_SetEmail(this, ClientID, m_aClients[ClientID].m_UserID, pEmail);
		pJob->Start();
	}
}

class CSqlJob_Server_Register : public CSqlJob
{
private:
	CServer* m_pServer;
	int m_ClientID;
	CSqlString<64> m_sName;
	CSqlString<64> m_sPasswordHash;
	CSqlString<64> m_sEmail;
	
public:
	CSqlJob_Server_Register(CServer* pServer, int ClientID, const char* pName, const char* pPasswordHash, const char* pEmail)
	{
		m_pServer = pServer;
		m_ClientID = ClientID;
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
		if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
			return true;
		
		net_addr_str(m_pServer->m_NetServer.ClientAddr(m_ClientID), aAddrStr, sizeof(aAddrStr), false);
		
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
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("Please wait 5 minutes before creating another account"));
				m_pServer->AddGameServerCmd(pCmd);
				
				return true;
			}
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
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
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("This username is already taken by an existing account"));
				m_pServer->AddGameServerCmd(pCmd);
				
				return true;
			}
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
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
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
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
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("Your account has been created and you are now logged."));
				m_pServer->AddGameServerCmd(pCmd);
							
				//The client is still the same
				if(m_pServer->m_aClients[m_ClientID].m_LogInstance == GetInstance())
				{
					int UserID = (int)pSqlServer->GetResults()->getInt("UserId");
					m_pServer->m_aClients[m_ClientID].m_UserID = UserID;
					str_copy(m_pServer->m_aClients[m_ClientID].m_aUsername, m_sName.Str(), sizeof(m_pServer->m_aClients[m_ClientID].m_aUsername));
					
					//If we are really unlucky, the client can deconnect and another one connect during this small code
					if(m_pServer->m_aClients[m_ClientID].m_LogInstance != GetInstance())
					{
						m_pServer->m_aClients[m_ClientID].m_UserID = -1;
					}
				}
				
				return true;
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
				m_pServer->AddGameServerCmd(pCmd);
				
				return false;
			}
		}
		catch (sql::SQLException &e)
		{
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget_Language(m_ClientID, CHATCATEGORY_DEFAULT, _("An error occured during the creation of your account."));
			m_pServer->AddGameServerCmd(pCmd);
			dbg_msg("sql", "Can't get the ID of the new user (MySQL Error: %s)", e.what());
			
			return false;
		}
		
		return true;
	}
	
	virtual void CleanInstanceRef()
	{
		m_pServer->m_aClients[m_ClientID].m_LogInstance = -1;
	}
};

void CServer::Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail)
{
	if(m_aClients[ClientID].m_LogInstance >= 0)
		return;
	
	char aHash[64]; //Result
	mem_zero(aHash, sizeof(aHash));
	Crypt(pPassword, (const unsigned char*) "d9", 1, 16, aHash);
	
	CSqlJob* pJob = new CSqlJob_Server_Register(this, ClientID, pUsername, aHash, pEmail);
	m_aClients[ClientID].m_LogInstance = pJob->GetInstance();
	pJob->Start();
}

class CSqlJob_Server_ShowTop10 : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientID;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowTop10(CServer* pServer, const char* pMapName, int ClientID, int ScoreType)
	{
		m_pServer = pServer;
		m_sMapName = CSqlString<64>(pMapName);
		m_ClientID = ClientID;
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
			
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatMOTD(m_ClientID, aBuf);
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

void CServer::ShowTop10(int ClientID, int ScoreType)
{
	CSqlJob* pJob = new CSqlJob_Server_ShowTop10(this, m_aCurrentMap, ClientID, ScoreType);
	pJob->Start();
}

class CSqlJob_Server_ShowChallenge : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientID;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowChallenge(CServer* pServer, const char* pMapName, int ClientID, int ChallengeType)
	{
		m_pServer = pServer;
		m_ScoreType = ChallengeTypeToScoreType(ChallengeType);
		m_ClientID = ClientID;
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
			
			CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatMOTD(m_ClientID, aMotdBuf);
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

void CServer::ShowChallenge(int ClientID)
{
	if(g_Config.m_InfChallenge)
	{
		int ChallengeType;
		lock_wait(m_ChallengeLock);
		ChallengeType = m_ChallengeType;
		lock_release(m_ChallengeLock);
		
		CSqlJob* pJob = new CSqlJob_Server_ShowChallenge(this, m_aCurrentMap, ClientID, ChallengeType);
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
	int m_ClientID;
	int m_UserID;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowRank(CServer* pServer, const char* pMapName, int ClientID, int UserID, int ScoreType)
	{
		m_pServer = pServer;
		m_sMapName = CSqlString<64>(pMapName);
		m_ClientID = ClientID;
		m_UserID = UserID;
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
				if(pSqlServer->GetResults()->getInt("UserId") == m_UserID)
				{
					int Score = pSqlServer->GetResults()->getInt("AccumulatedScore")/10;
					int Rounds = pSqlServer->GetResults()->getInt("NbRounds");
					str_format(aBuf, sizeof(aBuf), "You are rank %d in %s (%d pts in %d rounds)", Rank, m_sMapName.Str(), Score, Rounds);
					CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientID, aBuf);
					m_pServer->AddGameServerCmd(pCmd);
					
					RankFound = true;
				}
			}
			
			if(!RankFound)
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientID, "You must gain at least one point to see your rank");
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

void CServer::ShowRank(int ClientID, int ScoreType)
{
	if(m_aClients[ClientID].m_UserID >= 0)
	{
		CSqlJob* pJob = new CSqlJob_Server_ShowRank(this, m_aCurrentMap, ClientID, m_aClients[ClientID].m_UserID, ScoreType);
		pJob->Start();
	}
	else if(m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("You must be logged to see your rank"), NULL);
	}
}

class CSqlJob_Server_ShowGoal : public CSqlJob
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientID;
	int m_UserID;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowGoal(CServer* pServer, const char* pMapName, int ClientID, int UserID, int ScoreType)
	{
		m_pServer = pServer;
		m_sMapName = CSqlString<64>(pMapName);
		m_ClientID = ClientID;
		m_UserID = UserID;
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
				, m_UserID
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
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientID, aBuf);
				m_pServer->AddGameServerCmd(pCmd);
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientID, "Gain at least one point to increase your score");
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

void CServer::ShowGoal(int ClientID, int ScoreType)
{
	if(m_aClients[ClientID].m_UserID >= 0)
	{
		CSqlJob* pJob = new CSqlJob_Server_ShowGoal(this, m_aCurrentMap, ClientID, m_aClients[ClientID].m_UserID, ScoreType);
		pJob->Start();
	}
	else if(m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("You must be logged to see your goal"), NULL);
	}
}

class CSqlJob_Server_ShowStats : public CSqlJob // under konstruktion (copypasted draft)
{
private:
	CServer* m_pServer;
	CSqlString<64> m_sMapName;
	int m_ClientID;
	int m_UserID;
	int m_ScoreType;
	
public:
	CSqlJob_Server_ShowStats(CServer* pServer, const char* pMapName, int ClientID, int UserID, int ScoreType)
	{
		m_pServer = pServer;
		m_sMapName = CSqlString<64>(pMapName);
		m_ClientID = ClientID;
		m_UserID = UserID;
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
				, m_UserID
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
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientID, aBuf);
				m_pServer->AddGameServerCmd(pCmd);
			}
			else
			{
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientID, "stats Gain at least one point to increase your score");
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

void CServer::ShowStats(int ClientID, int UserId)
{
	if(m_aClients[ClientID].m_UserID >= 0)
	{
		CSqlJob* pJob = new CSqlJob_Server_ShowStats(this, m_aCurrentMap, ClientID, m_aClients[ClientID].m_UserID, UserId);
		pJob->Start();
	}
	else if(m_pGameServer)
	{
		m_pGameServer->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("You must be logged to see stats"), NULL);
	}
}

#endif

void CServer::Register(int ClientID, const char* pUsername, const char* pPassword, const char* pEmail)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "register request=%d login='%s' password='%s'", m_LastRegistrationRequestId, pUsername, pPassword);
	++m_LastRegistrationRequestId;

	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "registration", aBuf);
}

void CServer::Login(int ClientID, const char *pUsername, const char *pPassword)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "login request=%d login='%s' password='%s'", m_LastRegistrationRequestId, pUsername, pPassword);
	++m_LastRegistrationRequestId;

	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "registration", aBuf);
}

void CServer::Logout(int ClientID)
{
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "registration", "logout");
}

void CServer::Ban(int ClientID, int Seconds, const char* pReason)
{
	m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientID), Seconds, pReason);
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
	int m_ClientID;
	int m_UserID;
	int m_RoundId;
	CSqlString<64> m_sMapName;
	CRoundStatistics::CPlayer m_PlayerStatistics;
	
public:
	CSqlJob_Server_SendPlayerStatistics(CServer* pServer, const CRoundStatistics::CPlayer* pPlayerStatistics, const char* pMapName, int UserID, int ClientID)
	{
		m_RoundId = -1;
		m_pServer = pServer;
		m_ClientID = ClientID;
		m_UserID = UserID;
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
			, pSqlServer->GetPrefix(), m_UserID, m_RoundId, m_sMapName.ClrStr(), ScoreType, Score);
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
				, pSqlServer->GetPrefix(), m_UserID, m_sMapName.ClrStr(), 0, SQL_SCORE_NUMROUND);
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
				, pSqlServer->GetPrefix(), m_UserID, m_sMapName.ClrStr(), 0, SQL_SCORE_NUMROUND);
			pSqlServer->executeSqlQuery(aBuf);

			int NewScore = 0;
			if(pSqlServer->GetResults()->next())
			{
				NewScore += (int)pSqlServer->GetResults()->getInt("Score");
			}
			
			if(OldScore < NewScore)
			{
				str_format(aBuf, sizeof(aBuf), "You increased your score: +%d", (NewScore-OldScore)/10);
				CServer::CGameServerCmd* pCmd = new CGameServerCmd_SendChatTarget(m_ClientID, aBuf);
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
			if(m_aClients[i].m_UserID >= 0 && RoundStatistics()->IsValidePlayer(i))
			{
				CSqlJob* pJob = new CSqlJob_Server_SendPlayerStatistics(this, RoundStatistics()->PlayerStatistics(i), m_aCurrentMap, m_aClients[i].m_UserID, i);
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
	
void CServer::SetClientMemory(int ClientID, int Memory, bool Value)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || Memory < 0 || Memory >= NUM_CLIENTMEMORIES)
		return;
	
	m_aClients[ClientID].m_Memory[Memory] = Value;
}

bool CServer::GetClientMemory(int ClientID, int Memory)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || Memory < 0 || Memory >= NUM_CLIENTMEMORIES)
		return false;
	
	return m_aClients[ClientID].m_Memory[Memory];
}

void CServer::ResetClientMemoryAboutGame(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	
	m_aClients[ClientID].m_Memory[CLIENTMEMORY_TOP10] = false;
}

IServer::CClientSession* CServer::GetClientSession(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return 0;
	
	return &m_aClients[ClientID].m_Session;
}

// returns how many players are currently playing and not spectating
int CServer::GetActivePlayerCount()
{
	int PlayerCount = 0;
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

bool CServer::ClientShouldBeBanned(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	
	return (m_aClients[ClientID].m_Accusation.m_Num >= g_Config.m_InfAccusationThreshold);
}

void CServer::RemoveAccusations(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	
	m_aClients[ClientID].m_Accusation.m_Num = 0;
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

void CServer::RemoveMapVotesForID(int ClientID)
{
	NETADDR Addr = *m_NetServer.ClientAddr(ClientID);
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
int CServer::GetUserLevel(int ClientID)
{
	return m_aClients[ClientID].m_UserLevel;
}
#endif

/* INFECTION MODIFICATION END *****************************************/

int *CServer::GetIdMap(int ClientID)
{
	return m_aIdMap + VANILLA_MAX_CLIENTS * ClientID;
}

bool CServer::SetTimedOut(int ClientID, int OrigID)
{
	if(!m_NetServer.SetTimedOut(ClientID, OrigID))
	{
		return false;
	}
	m_aClients[ClientID].m_Sixup = m_aClients[OrigID].m_Sixup;

	if(m_aClients[OrigID].m_Authed != AUTHED_NO)
	{
		LogoutClient(ClientID, "Timeout Protection");
	}
	DelClientCallback(OrigID, CLIENTDROPTYPE_TIMEOUT_PROTECTION_USED, "Timeout Protection used", this);
	m_aClients[ClientID].m_Authed = AUTHED_NO;
	m_aClients[ClientID].m_Flags = m_aClients[OrigID].m_Flags;
	m_aClients[ClientID].m_DDNetVersion = m_aClients[OrigID].m_DDNetVersion;
	m_aClients[ClientID].m_InfClassVersion = m_aClients[OrigID].m_InfClassVersion;
	m_aClients[ClientID].m_GotDDNetVersionPacket = m_aClients[OrigID].m_GotDDNetVersionPacket;
	m_aClients[ClientID].m_DDNetVersionSettled = m_aClients[OrigID].m_DDNetVersionSettled;
	return true;
}

void CServer::SetErrorShutdown(const char *pReason)
{
	str_copy(m_aErrorShutdownReason, pReason);
}
