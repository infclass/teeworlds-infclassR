/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/

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
#include <engine/shared/snapshot.h>
#include <game/mapitems.h>
#include <game/gamecore.h>

#include <mastersrv/mastersrv.h>

#include "register.h"
#include "server.h"

#include <cstring>
/* INFECTION MODIFICATION START ***************************************/
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <engine/server/mapconverter.h>
#include <engine/server/sql_job.h>
#include <engine/server/crypt.h>

#include <teeuniverses/components/localization.h>
/* INFECTION MODIFICATION END *****************************************/

#if defined(CONF_FAMILY_WINDOWS)
	#define _WIN32_WINNT 0x0501
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

extern const char *GIT_SHORTREV_HASH;

static const char *StrLtrim(const char *pStr)
{
	while(*pStr && *pStr >= 0 && *pStr <= 32)
		pStr++;
	return pStr;
}

static void StrRtrim(char *pStr)
{
	int i = str_length(pStr);
	while(i >= 0)
	{
		if(pStr[i] < 0 || pStr[i] > 32)
			break;
		pStr[i] = 0;
		i--;
	}
}

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
		m_aIDs[i].m_Next = i+1;
		m_aIDs[i].m_State = 0;
	}

	m_aIDs[MAX_IDS-1].m_Next = -1;
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
	m_aIDs[m_FirstTimed].m_State = 0;
	m_FirstFree = m_FirstTimed;

	// remove it from the timed list
	m_FirstTimed = NextTimed;
	if(m_FirstTimed == -1)
		m_LastTimed = -1;

	m_Usage--;
}

int CSnapIDPool::NewID()
{
	int64 Now = time_get();

	// process timed ids
	while(m_FirstTimed != -1 && m_aIDs[m_FirstTimed].m_Timeout < Now)
		RemoveFirstTimeout();

	int ID = m_FirstFree;
	dbg_assert(ID != -1, "id error");
	if(ID == -1)
		return ID;
	m_FirstFree = m_aIDs[m_FirstFree].m_Next;
	m_aIDs[ID].m_State = 1;
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
	dbg_assert(m_aIDs[ID].m_State == 1, "id is not alloced");

	m_InUsage--;
	m_aIDs[ID].m_State = 2;
	m_aIDs[ID].m_Timeout = time_get()+time_freq()*5;
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

void CServerBan::InitServerBan(IConsole *pConsole, IStorage *pStorage, CServer* pServer)
{
	CNetBan::Init(pConsole, pStorage);

	m_pServer = pServer;

	// overwrites base command, todo: improve this
	Console()->Register("ban", "s<clientid> ?i<minutes> ?r<reason>", CFGFLAG_SERVER|CFGFLAG_STORE, ConBanExt, this, "Ban player with ip/client id for x minutes for any reason");
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

			if(Server()->m_aClients[i].m_Authed != CServer::AUTHED_NO && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
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

	// don't drop it like that. just kick the desired guy
	typename T::CDataType Data = *pData;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
			continue;

		if(m_BanID != i) // don't drop it like that. just kick the desired guy
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

bool CServerBan::ConBanExt(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pThis = static_cast<CServerBan*>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments()>1 ? clamp(pResult->GetInteger(1), 0, 44640) : 30;
	const char *pReason = pResult->NumArguments()>2 ? pResult->GetString(2) : "No reason given";
	pThis->m_BanID = -1;

	if(StrAllnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->Server()->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
		{
			pThis->m_BanID = ClientID; //to ban the right guy, not his brother or so :P
			if(pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientID), Minutes*60, pReason) != 0) //error occured
				pThis->Server()->Kick(ClientID, pReason);
		}
	}
	else
		ConBan(pResult, pUser);
	
	return true;
}

/* INFECTION MODIFICATION START ***************************************/

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
	m_SnapRate = CClient::SNAPRATE_INIT;
	m_NextMapChunk = 0;
	
	if(ResetScore)
	{
		m_NbRound = 0;
		m_WaitingTime = 0;
		m_WasInfected = 0;
		
		m_UserID = -1;
#ifdef CONF_SQL
		m_UserLevel = SQL_USERLEVEL_NORMAL;
#endif
		m_LogInstance = -1;
		
		m_AntiPing = 0;
		m_CustomSkin = 0;
		m_AlwaysRandom = 0;
		m_DefaultScoreMode = PLAYERSCOREMODE_SCORE;
		str_copy(m_aLanguage, "en", sizeof(m_aLanguage));

		m_WaitingTime = 0;
		m_WasInfected = 0;
		
		mem_zero(m_Memory, sizeof(m_Memory));
		
		m_Session.m_RoundId = -1;
		m_Session.m_Class = PLAYERCLASS_NONE;
		m_Session.m_MuteTick = 0;
		
		m_Accusation.m_Num = 0;
	}
}
/* INFECTION MODIFICATION END *****************************************/

CServer::CServer() : m_DemoRecorder(&m_SnapshotDelta)
{
	m_TickSpeed = SERVER_TICK_SPEED;

	m_pGameServer = 0;

	m_CurrentGameTick = 0;
	m_RunServer = 1;

	str_copy(m_aShutdownReason, "Server shutdown", sizeof(m_aShutdownReason));

	m_pCurrentMapData = 0;
	m_CurrentMapSize = 0;

	m_MapReload = 0;

	m_RconClientID = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;

	m_ServerInfoFirstRequest = 0;
	m_ServerInfoNumRequests = 0;
	m_ServerInfoHighLoad = false;

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
#ifdef CONF_SQL
	lock_destroy(m_GameServerCmdLock);
	lock_destroy(m_ChallengeLock);
#endif
}

int CServer::TrySetClientName(int ClientID, const char *pName)
{
	char aTrimmedName[64];
	char aTrimmedName2[64];

	// trim the name
	str_copy(aTrimmedName, StrLtrim(pName), sizeof(aTrimmedName));
	StrRtrim(aTrimmedName);

	// check for empty names
	if(!aTrimmedName[0])
		return -1;
		
	// name not allowed to start with '/'
	if(aTrimmedName[0] == '/')
		return -1;

	pName = aTrimmedName;

	// make sure that two clients doesn't have the same name
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i != ClientID && m_aClients[i].m_State >= CClient::STATE_READY)
		{
			str_copy(aTrimmedName2, ClientName(i), sizeof(aTrimmedName2));
			StrRtrim(aTrimmedName2);
			
			if(str_comp(pName, aTrimmedName2) == 0)
				return -1;
		}
	}

	// check if new and old name are the same
	if(m_aClients[ClientID].m_aName[0] && str_comp(m_aClients[ClientID].m_aName, pName) == 0)
		return 0;
	
	// set the client name
	str_copy(m_aClients[ClientID].m_aName, pName, MAX_NAME_LENGTH);
	return 0;
}

void CServer::SetClientName(int ClientID, const char *pName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	if(!pName)
		return;

	char aCleanName[MAX_NAME_LENGTH];
	str_copy(aCleanName, pName, sizeof(aCleanName));

	// clear name
	//~ for(char *p = aCleanName; *p; ++p)
	//~ {
		//~ if(*p < 32)
			//~ *p = ' ';
	//~ }

	if(TrySetClientName(ClientID, aCleanName))
	{
		// auto rename
		for(int i = 1;; i++)
		{
			char aNameTry[MAX_NAME_LENGTH];
			str_format(aNameTry, sizeof(aCleanName), "(%d)%s", i, aCleanName);
			if(TrySetClientName(ClientID, aNameTry) == 0)
				break;
		}
	}
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

	m_NetServer.Drop(ClientID, CLIENTDROPTYPE_KICK, pReason);
}

/*int CServer::Tick()
{
	return m_CurrentGameTick;
}*/

int64 CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq()*Tick)/SERVER_TICK_SPEED;
}

/*int CServer::TickSpeed()
{
	return SERVER_TICK_SPEED;
}*/

int CServer::Init()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClients[i].m_State = CClient::STATE_EMPTY;
		m_aClients[i].m_aName[0] = 0;
		m_aClients[i].m_aClan[0] = 0;
		m_aClients[i].m_CustClt = 0;
		m_aClients[i].m_Country = -1;
		m_aClients[i].m_Snapshots.Init();
		m_aClients[i].m_WaitingTime = 0;
		m_aClients[i].m_WasInfected = 0;
		m_aClients[i].m_Accusation.m_Num = 0;
		m_aClients[i].m_Latency = 0;
	}

	m_CurrentGameTick = 0;
	m_MapVotesCounter = 0;
	
#ifdef CONF_SQL
	m_ChallengeType = 0;
	m_ChallengeRefreshTick = 0;
	m_aChallengeWinner[0] = 0;
#endif
		
/* INFECTION MODIFICATION START ***************************************/
	m_aPreviousMap[0] = 0;
	m_aCurrentMap[0] = 0;

	SetFireDelay(INFWEAPON_NONE, 0);
	SetFireDelay(INFWEAPON_HAMMER, 125);
	SetFireDelay(INFWEAPON_GUN, 125);
	SetFireDelay(INFWEAPON_SHOTGUN, 500);
	SetFireDelay(INFWEAPON_GRENADE, 500);
	SetFireDelay(INFWEAPON_LASER, 800);
	SetFireDelay(INFWEAPON_NINJA, 800);
	SetFireDelay(INFWEAPON_ENGINEER_LASER, GetFireDelay(INFWEAPON_LASER));
	SetFireDelay(INFWEAPON_SOLDIER_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_SCIENTIST_LASER, GetFireDelay(INFWEAPON_LASER));
	SetFireDelay(INFWEAPON_SCIENTIST_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_MEDIC_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_MEDIC_LASER, GetFireDelay(INFWEAPON_LASER));
	SetFireDelay(INFWEAPON_MEDIC_SHOTGUN, 250);
	SetFireDelay(INFWEAPON_HERO_SHOTGUN, 250);
	SetFireDelay(INFWEAPON_BIOLOGIST_SHOTGUN, 250);
	SetFireDelay(INFWEAPON_BIOLOGIST_LASER, GetFireDelay(INFWEAPON_LASER));
	SetFireDelay(INFWEAPON_LOOPER_LASER, 250);
	SetFireDelay(INFWEAPON_LOOPER_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_HERO_LASER, GetFireDelay(INFWEAPON_LASER));
	SetFireDelay(INFWEAPON_HERO_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_SNIPER_LASER, GetFireDelay(INFWEAPON_LASER));
	SetFireDelay(INFWEAPON_NINJA_HAMMER, GetFireDelay(INFWEAPON_NINJA));
	SetFireDelay(INFWEAPON_NINJA_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_MERCENARY_GRENADE, GetFireDelay(INFWEAPON_GRENADE));
	SetFireDelay(INFWEAPON_MERCENARY_GUN, 50);
	SetFireDelay(INFWEAPON_MERCENARY_LASER, 250);
	SetFireDelay(INFWEAPON_FKING_GRENADE, 400);
	SetFireDelay(INFWEAPON_FKING_LASER, 400);
	SetFireDelay(INFWEAPON_FKING_SHOTGUN, 400);
	
	SetAmmoRegenTime(INFWEAPON_NONE, 0);
	SetAmmoRegenTime(INFWEAPON_HAMMER, 0);
	SetAmmoRegenTime(INFWEAPON_GUN, 500);
	SetAmmoRegenTime(INFWEAPON_SHOTGUN, 0);
	SetAmmoRegenTime(INFWEAPON_GRENADE, 0);
	SetAmmoRegenTime(INFWEAPON_LASER, 0);
	SetAmmoRegenTime(INFWEAPON_NINJA, 0);
	
	SetAmmoRegenTime(INFWEAPON_ENGINEER_LASER, 6000);
	SetAmmoRegenTime(INFWEAPON_SOLDIER_GRENADE, 7000);
	SetAmmoRegenTime(INFWEAPON_SCIENTIST_LASER, 6000);
	SetAmmoRegenTime(INFWEAPON_SCIENTIST_GRENADE, 10000);
	SetAmmoRegenTime(INFWEAPON_MEDIC_GRENADE, 0);
	SetAmmoRegenTime(INFWEAPON_MEDIC_LASER, 6000);
	SetAmmoRegenTime(INFWEAPON_MEDIC_SHOTGUN, 750);
	SetAmmoRegenTime(INFWEAPON_HERO_SHOTGUN, 750);
	SetAmmoRegenTime(INFWEAPON_HERO_LASER, 3000);
	SetAmmoRegenTime(INFWEAPON_HERO_GRENADE, 3000);
	SetAmmoRegenTime(INFWEAPON_SNIPER_LASER, 2000);
	SetAmmoRegenTime(INFWEAPON_MERCENARY_GRENADE, 5000);
	SetAmmoRegenTime(INFWEAPON_MERCENARY_GUN, 125);
	SetAmmoRegenTime(INFWEAPON_MERCENARY_LASER, 4000);
	SetAmmoRegenTime(INFWEAPON_NINJA_HAMMER, 0);
	SetAmmoRegenTime(INFWEAPON_NINJA_GRENADE, 15000);
	SetAmmoRegenTime(INFWEAPON_BIOLOGIST_LASER, 175);
	SetAmmoRegenTime(INFWEAPON_BIOLOGIST_SHOTGUN, 675);
	SetAmmoRegenTime(INFWEAPON_LOOPER_LASER, 500);
	SetAmmoRegenTime(INFWEAPON_LOOPER_GRENADE, 5000);
	SetAmmoRegenTime(INFWEAPON_FKING_GRENADE, 4000);
	SetAmmoRegenTime(INFWEAPON_FKING_LASER, 50000);
	SetAmmoRegenTime(INFWEAPON_FKING_SHOTGUN, 750);
	
	SetMaxAmmo(INFWEAPON_NONE, -1);
	SetMaxAmmo(INFWEAPON_HAMMER, -1);
	SetMaxAmmo(INFWEAPON_GUN, 10);
	SetMaxAmmo(INFWEAPON_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON_GRENADE, 10);
	SetMaxAmmo(INFWEAPON_LASER, 10);
	SetMaxAmmo(INFWEAPON_NINJA, 10);
	SetMaxAmmo(INFWEAPON_ENGINEER_LASER, 10);
	SetMaxAmmo(INFWEAPON_SCIENTIST_LASER, 10);
	SetMaxAmmo(INFWEAPON_SCIENTIST_GRENADE, 3);
	SetMaxAmmo(INFWEAPON_SOLDIER_GRENADE, 10);
	SetMaxAmmo(INFWEAPON_MEDIC_GRENADE, 10);
	SetMaxAmmo(INFWEAPON_MEDIC_LASER, 1);
	SetMaxAmmo(INFWEAPON_MEDIC_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON_HERO_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON_HERO_LASER, 10);
	SetMaxAmmo(INFWEAPON_HERO_GRENADE, 10);
	SetMaxAmmo(INFWEAPON_SNIPER_LASER, 10);
	SetMaxAmmo(INFWEAPON_NINJA_HAMMER, -1);
	SetMaxAmmo(INFWEAPON_NINJA_GRENADE, 5);
	SetMaxAmmo(INFWEAPON_MERCENARY_GRENADE, 8);
	SetMaxAmmo(INFWEAPON_MERCENARY_GUN, 40);
	SetMaxAmmo(INFWEAPON_MERCENARY_LASER, 10);
	SetMaxAmmo(INFWEAPON_BIOLOGIST_LASER, 10);
	SetMaxAmmo(INFWEAPON_BIOLOGIST_SHOTGUN, 10);
	SetMaxAmmo(INFWEAPON_LOOPER_LASER, 20);
	SetMaxAmmo(INFWEAPON_LOOPER_GRENADE, 10);
	SetMaxAmmo(INFWEAPON_FKING_GRENADE, 10);
	SetMaxAmmo(INFWEAPON_FKING_LASER, 1);
	SetMaxAmmo(INFWEAPON_FKING_SHOTGUN, 10);
	
	SetClassAvailability(PLAYERCLASS_ENGINEER, 2);
	SetClassAvailability(PLAYERCLASS_SOLDIER, 2);
	SetClassAvailability(PLAYERCLASS_MERCENARY, 2);
	SetClassAvailability(PLAYERCLASS_SNIPER, 2);
	SetClassAvailability(PLAYERCLASS_NINJA, 2);
	SetClassAvailability(PLAYERCLASS_MEDIC, 2);
	SetClassAvailability(PLAYERCLASS_HERO, 2);
	SetClassAvailability(PLAYERCLASS_SCIENTIST, 2);
	SetClassAvailability(PLAYERCLASS_BIOLOGIST, 2);
	SetClassAvailability(PLAYERCLASS_LOOPER, 2);
	SetClassAvailability(PLAYERCLASS_FKING, 2);
	
	SetClassAvailability(PLAYERCLASS_SMOKER, 1);
	SetClassAvailability(PLAYERCLASS_HUNTER, 1);
	SetClassAvailability(PLAYERCLASS_BAT, 1);
	SetClassAvailability(PLAYERCLASS_GHOST, 1);
	SetClassAvailability(PLAYERCLASS_SPIDER, 1);
	SetClassAvailability(PLAYERCLASS_GHOUL, 1);
	SetClassAvailability(PLAYERCLASS_SLUG, 1);
	SetClassAvailability(PLAYERCLASS_BOOMER, 1);
	SetClassAvailability(PLAYERCLASS_VOODOO, 1);
	SetClassAvailability(PLAYERCLASS_UNDEAD, 1);
	SetClassAvailability(PLAYERCLASS_WITCH, 1);
	
	m_InfClassChooser = 1;
/* INFECTION MODIFICATION END *****************************************/

	return 0;
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
		pInfo->m_CustClt = m_aClients[ClientID].m_CustClt;
		return 1;
	}
	return 0;
}

void CServer::GetClientAddr(int ClientID, char *pAddrStr, int Size) const
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientID), pAddrStr, Size, false);
}

std::string CServer::GetClientIP(int ClientID) const
{
	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), false);
	std::string ip(aAddrStr);
	return ip;
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

int CServer::MaxClients() const
{
	return m_NetServer.MaxClients();
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientID)
{
	return SendMsgEx(pMsg, Flags, ClientID, false);
}

int CServer::SendMsgEx(CMsgPacker *pMsg, int Flags, int ClientID, bool System)
{
	CNetChunk Packet;
	if(!pMsg)
		return -1;

	// drop packet to dummy client
	if(0 <= ClientID && ClientID < MAX_CLIENTS && GameServer()->IsClientBot(ClientID))
		return 0;

	mem_zero(&Packet, sizeof(CNetChunk));

	Packet.m_ClientID = ClientID;
	Packet.m_pData = pMsg->Data();
	Packet.m_DataSize = pMsg->Size();

	// HACK: modify the message id in the packet and store the system flag
	*((unsigned char*)Packet.m_pData) <<= 1;
	if(System)
		*((unsigned char*)Packet.m_pData) |= 1;

	if(Flags&MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags&MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	// write message to demo recorder
	if(!(Flags&MSGFLAG_NORECORD))
		m_DemoRecorder.RecordMessage(pMsg->Data(), pMsg->Size());

	if(!(Flags&MSGFLAG_NOSEND))
	{
		if(ClientID == -1)
		{
			// broadcast
			int i;
			for(i = 0; i < MAX_CLIENTS; i++)
				if(m_aClients[i].m_State == CClient::STATE_INGAME)
				{
					Packet.m_ClientID = i;
					m_NetServer.Send(&Packet);
				}
		}
		else
			m_NetServer.Send(&Packet);
	}
	return 0;
}

void CServer::DoSnapshot()
{
	GameServer()->OnPreSnap();

	// create snapshot for demo recording
	if(m_DemoRecorder.IsRecording())
	{
		char aData[CSnapshot::MAX_SIZE];
		int SnapshotSize;

		// build snap and possibly add some messages
		m_SnapshotBuilder.Init();
		GameServer()->OnSnap(-1);
		SnapshotSize = m_SnapshotBuilder.Finish(aData);

		// write snapshot
		m_DemoRecorder.RecordSnapshot(Tick(), aData, SnapshotSize);
	}

	// create snapshots for all clients
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// client must be ingame to recive snapshots
		if(m_aClients[i].m_State != CClient::STATE_INGAME)
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

			// save it the snapshot
			m_aClients[i].m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0);

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
						CMsgPacker Msg(NETMSG_SNAPSINGLE);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick-DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n*MaxSize], Chunk);
						SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick-DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n*MaxSize], Chunk);
						SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick-DeltaTick);
				SendMsgEx(&Msg, MSGFLAG_FLUSH, i, true);
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

int CServer::NewClientCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	// Remove non human player on same slot
	if(pThis->GameServer()->IsClientBot(ClientID))
	{
		pThis->GameServer()->OnClientDrop(ClientID, CLIENTDROPTYPE_KICK, "removing dummy");
	}

	pThis->m_aClients[ClientID].m_State = CClient::STATE_AUTH;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_CustClt = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_Quitting = false;
	
	memset(&pThis->m_aClients[ClientID].m_Addr, 0, sizeof(NETADDR));
	pThis->m_aClients[ClientID].m_CustClt = 0;
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
	
	pThis->m_aClients[ClientID].m_Quitting = true;

	char aAddrStr[NETADDR_MAXSTRSIZE];

	// remove map votes for the dropped client
	pThis->RemoveMapVotesForID(ClientID);

	net_addr_str(pThis->m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "client dropped. cid=%d addr=%s reason='%s'", ClientID, aAddrStr,	pReason);
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
	pThis->m_aClients[ClientID].m_Snapshots.PurgeAll();
	pThis->m_aClients[ClientID].m_WaitingTime = 0;
	pThis->m_aClients[ClientID].m_UserID = -1;
#ifdef CONF_SQL
	pThis->m_aClients[ClientID].m_UserLevel = SQL_USERLEVEL_NORMAL;
#endif
	pThis->m_aClients[ClientID].m_LogInstance = -1;
	pThis->m_aClients[ClientID].m_Quitting = false;
	
	//Keep information about client for 10 minutes
	pThis->m_NetSession.AddSession(pThis->m_NetServer.ClientAddr(ClientID), 10*60, &pThis->m_aClients[ClientID].m_Session);
	dbg_msg("infclass", "session created for the client %d", ClientID);
	
	//Keep accusation for 30 minutes
	pThis->m_NetAccusation.AddSession(pThis->m_NetServer.ClientAddr(ClientID), 30*60, &pThis->m_aClients[ClientID].m_Accusation);
	dbg_msg("infclass", "accusation created for the client %d", ClientID);
	
	return 0;
}

void CServer::SendMap(int ClientID)
{
	CMsgPacker Msg(NETMSG_MAP_CHANGE);
	Msg.AddString(GetMapName(), 0);
	Msg.AddInt(m_CurrentMapCrc);
	Msg.AddInt(m_CurrentMapSize);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true);

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
 
 	CMsgPacker Msg(NETMSG_MAP_DATA);
 	Msg.AddInt(Last);
 	Msg.AddInt(m_CurrentMapCrc);
 	Msg.AddInt(Chunk);
 	Msg.AddInt(ChunkSize);
 	Msg.AddRaw(&m_pCurrentMapData[Offset], ChunkSize);
 	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true);
 
 	if(g_Config.m_Debug)
 	{
 		char aBuf[256];
 		str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
 		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
 	}
 }

void CServer::SendConnectionReady(int ClientID)
{
	CMsgPacker Msg(NETMSG_CON_READY);
	SendMsgEx(&Msg, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID, true);
}

void CServer::SendRconLine(int ClientID, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE);
	Msg.AddString(pLine, 512);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);
}

void CServer::SendRconLineAuthed(const char *pLine, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	static volatile int ReentryGuard = 0;
	int i;

	if(ReentryGuard) return;
	ReentryGuard++;

	for(i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY && pThis->m_aClients[i].m_Authed >= pThis->m_RconAuthLevel)
			pThis->SendRconLine(i, pLine);
	}

	ReentryGuard--;
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM);
	Msg.AddString(pCommandInfo->m_pName, 256);
	SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);
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
	//The map format of InfectionClass is different from the vanilla format.
	//We need to convert the map to something that the client can use
	//First, try to find if the client map is already generated

	CDataFileReader dfServerMap;
	dfServerMap.Open(Storage(), pMapFilePath, IStorage::TYPE_ALL);
	unsigned ServerMapCrc = dfServerMap.Crc();
	dfServerMap.Close();

	char aClientMapDir[256];
	char aClientMapName[256];
	static const char *pConverterId = CMapConverter::GetConverterVersionId();
	str_format(aClientMapDir, sizeof(aClientMapDir), "clientmaps/%s", pConverterId);
	str_format(aClientMapName, sizeof(aClientMapName), "%s/%s_%08x.map", aClientMapDir, pMapName, ServerMapCrc);

	CMapConverter MapConverter(Storage(), m_pMap, Console());
	if(!MapConverter.Load())
		return false;

	m_TimeShiftUnit = MapConverter.GetTimeShiftUnit();

	CDataFileReader dfClientMap;
	//The map is already converted
	if(dfClientMap.Open(Storage(), aClientMapName, IStorage::TYPE_ALL))
	{
		m_CurrentMapCrc = dfClientMap.Crc();
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
		dfGeneratedMap.Close();
	}

	char aBufMsg[128];
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

	// unpack msgid and system flag
	int Msg = Unpacker.GetInt();
	int Sys = Msg&1;
	Msg >>= 1;

	if(Unpacker.Error())
		return;
	
	if(Sys)
	{
		// system message
		if(Msg == NETMSG_INFO)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_AUTH)
			{
				const char *pVersion = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(str_comp(pVersion, "0.6 626fce9a778df4d4") != 0 && str_comp(pVersion, GameServer()->NetVersion()) != 0)
				{
					// wrong version
					char aReason[256];
					str_format(aReason, sizeof(aReason), "Wrong version. Server is running '%s' and client '%s'", GameServer()->NetVersion(), pVersion);
					m_NetServer.Drop(ClientID, CLIENTDROPTYPE_WRONG_VERSION, aReason);
					return;
				}

				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(!g_Config.m_InfCaptcha && g_Config.m_Password[0] != 0 && str_comp(g_Config.m_Password, pPassword) != 0)
				{
					// wrong password
					m_NetServer.Drop(ClientID, CLIENTDROPTYPE_WRONG_PASSWORD, "Wrong password");
					return;
				}

				m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
				SendMap(ClientID);
			}
		}
		else if(Msg == NETMSG_REQUEST_MAP_DATA)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) == 0 || m_aClients[ClientID].m_State < CClient::STATE_CONNECTING)
				return;

			int Chunk = Unpacker.GetInt();
			if(Chunk != m_aClients[ClientID].m_NextMapChunk || !g_Config.m_InfFastDownload)
			{
				SendMapData(ClientID, Chunk);
				return;
			}

			if(Chunk == 0)
			{
				for(int i = 0; i < g_Config.m_InfMapWindow; i++)
				{
					SendMapData(ClientID, i);
				}
			}
			SendMapData(ClientID, g_Config.m_InfMapWindow + m_aClients[ClientID].m_NextMapChunk);
			m_aClients[ClientID].m_NextMapChunk++;
		}
		else if(Msg == NETMSG_READY)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_CONNECTING)
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player is ready. ClientID=%d addr=%s secure=%s", ClientID, aAddrStr, m_NetServer.HasSecurityToken(ClientID)?"yes":"no");
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_aClients[ClientID].m_State = CClient::STATE_READY;
				m_aClients[ClientID].m_WaitingTime = TickSpeed()*g_Config.m_InfConWaitingTime;
			}
		}
		else if(Msg == NETMSG_ENTERGAME)
		{
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_READY && GameServer()->IsClientReady(ClientID))
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
			int64 TagTime;

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

				CMsgPacker Msg(NETMSG_INPUTTIMING);
				Msg.AddInt(IntendedTick);
				Msg.AddInt(TimeLeft);
				SendMsgEx(&Msg, 0, ClientID, true);
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
			if(Unpacker.Error() == 0 && !str_comp(pCmd, "crashmeplx"))
			{
				SetCustClt(ClientID);
			} else
			if((pPacket->m_Flags&NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0 && m_aClients[ClientID].m_Authed)
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
				Console()->ExecuteLineFlag(pCmd, ClientID, false, CFGFLAG_SERVER);
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
						CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
						Msg.AddInt(1);	//authed
						Msg.AddInt(1);	//cmdlist
						SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);

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
						CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
						Msg.AddInt(1);	//authed
						Msg.AddInt(1);	//cmdlist
						SendMsgEx(&Msg, MSGFLAG_VITAL, ClientID, true);

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
			CMsgPacker Msg(NETMSG_PING_REPLY);
			SendMsgEx(&Msg, 0, ClientID, true);
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
	int64 Now = Tick();
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

	bool SendResponse = m_ServerInfoNumRequests <= MaxRequests && !m_ServerInfoHighLoad;
	if(!SendResponse) {
		char aBuf[256];
		char aAddrStr[256];
		net_addr_str(pAddr, aAddrStr, sizeof(aAddrStr), true);
		str_format(aBuf, sizeof(aBuf), "Too many info requests from %s: %d > %d (Now = %lld, mSIFR = %lld)",
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
			if(GameServer()->IsClientBot(i))
				continue;

			if(GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	p.Reset();

	#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
	#define ADD_INT(p, x) do { str_format(aBuf, sizeof(aBuf), "%d", x); (p).AddString(aBuf, 0); } while(0)

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
			str_format(aBuf, sizeof(aBuf), "%s [%d/%d]", aBuf, ClientCount, m_NetServer.MaxClients());
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
			if(GameServer()->IsClientBot(i))
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
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			SendServerInfo(m_NetServer.ClientAddr(i), -1, SERVERINFO_INGAME, false);
		}
	}
}

void CServer::PumpNetwork()
{
	CNetChunk Packet;

	m_NetServer.Update();

	// process packets
	while(m_NetServer.Recv(&Packet))
	{
		if(Packet.m_ClientID == -1)
		{
			// stateless
			if(!m_Register.RegisterProcessPacket(&Packet))
			{
				int ExtraToken = 0;
				int Type = -1;
				if(Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO)+1 &&
					mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO)) == 0)
				{
					if(Packet.m_Flags&NETSENDFLAG_EXTENDED)
					{
						Type = SERVERINFO_EXTENDED;
						ExtraToken = (Packet.m_aExtraData[0] << 8) | Packet.m_aExtraData[1];
					}
					else
						Type = SERVERINFO_VANILLA;
				}
				else if(Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO_64_LEGACY)+1 &&
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

	m_ServerBan.Update();
	m_NetSession.Update();
	m_NetAccusation.Update();
	m_Econ.Update();
}

char *CServer::GetMapName()
{
	// get the name of the map without his path
	char *pMapShortName = &g_Config.m_SvMap[0];
	for(int i = 0; i < str_length(g_Config.m_SvMap)-1; i++)
	{
		if(g_Config.m_SvMap[i] == '/' || g_Config.m_SvMap[i] == '\\')
			pMapShortName = &g_Config.m_SvMap[i+1];
	}
	return pMapShortName;
}

int CServer::LoadMap(const char *pMapName)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);

	if(!m_pMap->Load(aBuf))
		return 0;
			
/* INFECTION MODIFICATION START ***************************************/
	if(!GenerateClientMap(aBuf, pMapName))
		return 0;
/* INFECTION MODIFICATION END *****************************************/

	// stop recording when we change map
	m_DemoRecorder.Stop();
	
	// reinit snapshot ids
	m_IDPool.TimeoutIDs();

/* INFECTION MODIFICATION START ***************************************/
	str_copy(m_aPreviousMap, m_aCurrentMap, sizeof(m_aPreviousMap));
	str_copy(m_aCurrentMap, pMapName, sizeof(m_aCurrentMap));
	ResetMapVotes();

/* INFECTION MODIFICATION END *****************************************/
	
	return 1;
}

int CServer::GetMinPlayersForMap(const char* pMapName)
{
	int MinPlayers = 0;
	char MapInfoFilename[256];
	str_format(MapInfoFilename, sizeof(MapInfoFilename), "maps/%s.cfg", pMapName);
	IOHANDLE File = Storage()->OpenFile(MapInfoFilename, IOFLAG_READ, IStorage::TYPE_ALL);

	if(!File)
		return 0;

	char MapInfoLine[512];
	bool isEndOfFile = false;
	while(!isEndOfFile)
	{
		isEndOfFile = true;
		
		//Load one line
		int MapInfoLineLength = 0;
		char c;
		while(io_read(File, &c, 1))
		{
			isEndOfFile = false;
			
			if(c == '\n') break;
			else
			{
				MapInfoLine[MapInfoLineLength] = c;
				MapInfoLineLength++;
			}
		}
		
		MapInfoLine[MapInfoLineLength] = 0;
		
		//Get the key
		static const char MinPlayersKey[] = "# mapinfo: minplayers ";
		if(str_comp_nocase_num(MapInfoLine, MinPlayersKey, sizeof(MinPlayersKey) - 1) == 0)
		{
			MinPlayers = str_toint(MapInfoLine+sizeof(MinPlayersKey) - 1);
		}
	}
	io_close(File);

	return MinPlayers;
}

void CServer::InitRegister(CNetServer *pNetServer, IEngineMasterServer *pMasterServer, IConsole *pConsole)
{
	m_Register.Init(pNetServer, pMasterServer, pConsole);
}

static bool IsSeparator(char c) { return c == ';' || c == ' ' || c == ',' || c == '\t'; }

int CServer::Run()
{
	m_PrintCBIndex = Console()->RegisterPrintCallback(g_Config.m_ConsoleOutputLevel, SendRconLineAuthed, this);

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
	if(!LoadMap(g_Config.m_SvMap))
	{
		dbg_msg("server", "failed to load map. mapname='%s'", g_Config.m_SvMap);
		return -1;
	}

	// start server
	NETADDR BindAddr;
	if(g_Config.m_Bindaddr[0] && net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) == 0)
	{
		// sweet!
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}
	else
	{
		mem_zero(&BindAddr, sizeof(BindAddr));
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}

	if(!m_NetServer.Open(BindAddr, &m_ServerBan, g_Config.m_SvMaxClients, g_Config.m_SvMaxClientsPerIP, 0))
	{
		dbg_msg("server", "couldn't open socket. port %d might already be in use", g_Config.m_SvPort);
		return -1;
	}

	if(g_Config.m_InfCaptcha)
	{
		InitCaptcha();
		if(!m_NetServer.IsCaptchaInitialized())
		{
			dbg_msg("server", "failed to create captcha list -> disable captcha");
			g_Config.m_InfCaptcha = 0;
		}
	}

	m_NetServer.SetCallbacks(NewClientCallback, ClientRejoinCallback, DelClientCallback, this);

	m_Econ.Init(Console(), &m_ServerBan);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "server name is '%s'", g_Config.m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	GameServer()->OnInit();
	str_format(aBuf, sizeof(aBuf), "version %s", GameServer()->NetVersion());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	str_format(aBuf, sizeof(aBuf), "game version %s", GameServer()->Version());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

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
		int64 ReportTime = time_get();
		int ReportInterval = 3;

		m_Lastheartbeat = 0;
		m_GameStartTime = time_get();

		while(m_RunServer)
		{
			set_new_tick();
			int64 t = time_get();
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
					GameServer()->OnShutdown();

					for(int c = 0; c < MAX_CLIENTS; c++)
					{
						if(m_aClients[c].m_State <= CClient::STATE_AUTH)
							continue;

						SendMap(c);
/* INFECTION MODIFICATION START ***************************************/
						m_aClients[c].Reset(false);
/* INFECTION MODIFICATION END *****************************************/
						m_aClients[c].m_State = CClient::STATE_CONNECTING;
						SetClientMemory(c, CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE, true);
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
					
					if(m_aClients[i].m_State >= CClient::STATE_READY && m_aClients[i].m_UserID < 0)
					{
						if(TrySetClientName(i, m_aClients[i].m_aName))
						{
							// auto rename
							for(int j = 1;; j++)
							{
								char aNameTry[MAX_NAME_LENGTH];
								str_format(aNameTry, sizeof(aNameTry), "(%d)%s", j, m_aClients[i].m_aName);
								if(TrySetClientName(i, aNameTry) == 0)
									break;
							}
						}
					}
				}
				
				for(int i=0; i<MAX_CLIENTS; i++)
				{
					if(m_aClients[i].m_WaitingTime > 0)
					{
						m_aClients[i].m_WaitingTime--;
						if(m_aClients[i].m_WaitingTime <= 0)
						{
							if(m_aClients[i].m_State == CClient::STATE_READY)
							{
								GameServer()->OnClientConnected(i);	
								SendConnectionReady(i);
							}
							else if(m_aClients[i].m_State == CClient::STATE_INGAME)
							{
								GameServer()->OnClientEnter(i);
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

			PumpNetwork();

			if(ReportTime < time_get())
			{
				if(g_Config.m_Debug)
				{
					/*
					static NETSTATS prev_stats;
					NETSTATS stats;
					netserver_stats(net, &stats);

					perf_next();

					if(config.dbg_pref)
						perf_dump(&rootscope);

					dbg_msg("server", "send=%8d recv=%8d",
						(stats.send_bytes - prev_stats.send_bytes)/reportinterval,
						(stats.recv_bytes - prev_stats.recv_bytes)/reportinterval);

					prev_stats = stats;
					*/
				}

				ReportTime += time_freq()*ReportInterval;
			}

			bool NonActive = true;

			for(int c = 0; c < MAX_CLIENTS; c++)
				if(m_aClients[c].m_State != CClient::STATE_EMPTY)
					NonActive = false;

			// wait for incoming data
			if (NonActive)
			{
				if(g_Config.m_SvShutdownWhenEmpty)
					m_RunServer = false;
				else
					net_socket_read_wait(m_NetServer.Socket(), 1000000);
			}
			else
			{
				set_new_tick();
				int64 t = time_get();
				int x = TickStartTime(m_CurrentGameTick+1) - t + 1;
				if(x > 0)
					net_socket_read_wait(m_NetServer.Socket(), x);
			}
		}
	}
	// disconnect all clients on shutdown
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			m_NetServer.Drop(i, CLIENTDROPTYPE_SHUTDOWN, m_aShutdownReason);

		m_Econ.Shutdown();
	}

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

bool CServer::ConUnmute(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	const char *pStr = pResult->GetString(0);
	
	if(CNetDatabase::StrAllnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
			pThis->m_aClients[ClientID].m_Session.m_MuteTick = 0;
	}
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
	
	return true;
}

bool CServer::ConMute(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments()>1 ? clamp(pResult->GetInteger(1), 0, 44640) : 5;
	const char *pReason = pResult->NumArguments()>2 ? pResult->GetString(2) : "No reason given";
	
	if(CNetDatabase::StrAllnum(pStr))
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
	
	return true;
}

bool CServer::ConWhisper(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	const char *pStrClientID = pResult->GetString(0);
	const char *pText = pResult->GetString(1);

	if(CNetDatabase::StrAllnum(pStrClientID))
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
	

	return true;
}

bool CServer::ConKick(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = (CServer *)pUser;
	
	char aBuf[128];
	const char *pStr = pResult->GetString(0);
	const char *pReason = pResult->NumArguments()>1 ? pResult->GetString(1) : "No reason given";
	str_format(aBuf, sizeof(aBuf), "Kicked (%s)", pReason);

	if(CNetDatabase::StrAllnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		else
			pThis->Kick(ClientID, aBuf);
	}
	else
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
	
	return true;
}

/* INFECTION MODIFICATION START ***************************************/
bool CServer::ConOptionStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	CServer* pThis = static_cast<CServer *>(pUser);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State == CClient::STATE_INGAME)
		{
			str_format(aBuf, sizeof(aBuf), "(#%02i) %s: [lang=%s] [antiping=%d] [alwaysrandom=%d] [customskin=%d]",
				i,
				pThis->ClientName(i),
				pThis->m_aClients[i].m_aLanguage,
				pThis->GetClientAntiPing(i),
				pThis->GetClientAlwaysRandom(i),
				pThis->GetClientCustomSkin(i)
			);
			
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		}
	}
	
	return true;
}
/* INFECTION MODIFICATION END *****************************************/

bool CServer::ConStatusExtended(IConsole::IResult *pResult, void *pUser)
{
	return ConStatus(pResult, pUser);
}

bool CServer::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	char aAddrStr[NETADDR_MAXSTRSIZE];
	CServer* pThis = static_cast<CServer *>(pUser);

/* INFECTION MODIFICATION START ***************************************/
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY)
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
				
				str_format(aBuf, sizeof(aBuf), "(#%02i) %s: [antispoof=%d] [login=%d] [level=%d] [ip=%s] [version=%d]",
					i,
					aBufName,
					pThis->m_NetServer.HasSecurityToken(i),
					pThis->IsClientLogged(i),
					AuthLevel,
					aAddrStr,
					pThis->GameServer()->GetClientVersion(i)
				);
			}
			else
				str_format(aBuf, sizeof(aBuf), "id=%d addr=%s connecting", i, aAddrStr);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		}
	}
	
	return true;
/* INFECTION MODIFICATION END *****************************************/
}

bool CServer::ConShutdown(IConsole::IResult *pResult, void *pUser)
{
	CServer* pThis = static_cast<CServer *>(pUser);
	pThis->m_RunServer = 0;
	const char *pReason = pResult->GetString(0);
	if(pReason[0])
	{
		str_copy(pThis->m_aShutdownReason, pReason, sizeof(pThis->m_aShutdownReason));
	}

	return true;
}

void CServer::DemoRecorder_HandleAutoStart()
{
	if(g_Config.m_SvAutoDemoRecord)
	{
		m_DemoRecorder.Stop();
		char aFilename[128];
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", "auto/autorecord", aDate);
		m_DemoRecorder.Start(Storage(), m_pConsole, aFilename, GameServer()->NetVersion(), m_aCurrentMap, m_CurrentMapCrc, "server");
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
	return m_DemoRecorder.IsRecording();
}

bool CServer::ConRecord(IConsole::IResult *pResult, void *pUser)
{
	CServer* pServer = (CServer *)pUser;
	char aFilename[128];

	if(pResult->NumArguments())
		str_format(aFilename, sizeof(aFilename), "demos/%s.demo", pResult->GetString(0));
	else
	{
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/demo_%s.demo", aDate);
	}
	pServer->m_DemoRecorder.Start(pServer->Storage(), pServer->Console(), aFilename, pServer->GameServer()->NetVersion(), pServer->m_aCurrentMap, pServer->m_CurrentMapCrc, "server");
	
	return true;
}

bool CServer::ConStopRecord(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_DemoRecorder.Stop();
	
	return true;
}

bool CServer::ConMapReload(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_MapReload = 1;
	
	return true;
}

bool CServer::ConLogout(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if(pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS);
		Msg.AddInt(0);	//authed
		Msg.AddInt(0);	//cmdlist
		pServer->SendMsgEx(&Msg, MSGFLAG_VITAL, pServer->m_RconClientID, true);

		pServer->m_aClients[pServer->m_RconClientID].m_Authed = AUTHED_NO;
		pServer->m_aClients[pServer->m_RconClientID].m_AuthTries = 0;
		pServer->m_aClients[pServer->m_RconClientID].m_pRconCmdToSend = 0;
		pServer->SendRconLine(pServer->m_RconClientID, "Logout successful.");
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "ClientID=%d logged out", pServer->m_RconClientID);
		pServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
	
	return true;
}

bool CServer::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->UpdateServerInfo();
	
	return true;
}

bool CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *)pUserData)->m_NetServer.SetMaxClientsPerIP(pResult->GetInteger(0));
	
	return true;
}

bool CServer::ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
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
		pfnCallback(pResult, pCallbackUserData);
	
	return true;
}

bool CServer::ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->Console()->SetPrintOutputLevel(pThis->m_PrintCBIndex, pResult->GetInteger(0));
	}
	
	return true;
}

/* DDNET MODIFICATION START *******************************************/
#ifdef CONF_SQL
bool CServer::ConAddSqlServer(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;

	if (pResult->NumArguments() != 7 && pResult->NumArguments() != 8)
		return false;

	bool ReadOnly;
	if (str_comp_nocase(pResult->GetString(0), "w") == 0)
		ReadOnly = false;
	else if (str_comp_nocase(pResult->GetString(0), "r") == 0)
		ReadOnly = true;
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "choose either 'r' for SqlReadServer or 'w' for SqlWriteServer");
		return true;
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

	return true;
}

bool CServer::ConDumpSqlServers(IConsole::IResult *pResult, void *pUserData)
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
		return true;
	}

	CSqlServer** apSqlServers = ReadOnly ? pSelf->m_apSqlReadServers : pSelf->m_apSqlWriteServers;

	for (int i = 0; i < MAX_SQLSERVERS; i++)
		if (apSqlServers[i])
		{
			char aBuf[512];
			str_format(aBuf, sizeof(aBuf), "SQL-%s %d: DB: '%s' Prefix: '%s' User: '%s' Pass: '%s' IP: '%s' Port: %d", ReadOnly ? "Read" : "Write", i, apSqlServers[i]->GetDatabase(), apSqlServers[i]->GetPrefix(), apSqlServers[i]->GetUser(), apSqlServers[i]->GetPass(), apSqlServers[i]->GetIP(), apSqlServers[i]->GetPort());
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		}
	
	return true;
}

void CServer::CreateTablesThread(void *pData)
{
	((CSqlServer *)pData)->CreateTables();
}
#endif

/* DDNET MODIFICATION END *********************************************/

bool CServer::ConSetWeaponFireDelay(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;
	if(pResult->NumArguments() != 2)
		return false;

	int WeaponID = pResult->GetInteger(0);
	if((WeaponID < 0) || (WeaponID >= NB_INFWEAPON))
	{
		return false;
	}
	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return false;
	}

	pSelf->SetFireDelay(WeaponID, Interval);

	return true;
}

bool CServer::ConSetWeaponAmmoRegen(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;
	if(pResult->NumArguments() != 2)
		return false;

	int WeaponID = pResult->GetInteger(0);
	if((WeaponID < 0) || (WeaponID >= NB_INFWEAPON))
	{
		return false;
	}
	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return false;
	}

	pSelf->SetAmmoRegenTime(WeaponID, Interval);

	return true;
}

bool CServer::ConSetWeaponMaxAmmo(IConsole::IResult *pResult, void *pUserData)
{
	CServer *pSelf = (CServer *)pUserData;
	if(pResult->NumArguments() != 2)
		return false;

	int WeaponID = pResult->GetInteger(0);
	if((WeaponID < 0) || (WeaponID >= NB_INFWEAPON))
	{
		return false;
	}
	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return false;
	}

	pSelf->SetMaxAmmo(WeaponID, Interval);

	return true;
}

void CServer::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGameServer = Kernel()->RequestInterface<IGameServer>();
	m_pMap = Kernel()->RequestInterface<IEngineMap>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	// register console commands
	Console()->Register("kick", "s<username or uid> ?r<reason>", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "", CFGFLAG_SERVER, ConStatus, this, "List players");
	Console()->Register("status_extended", "", CFGFLAG_SERVER, ConStatusExtended, this, "List players");
	Console()->Register("option_status", "", CFGFLAG_SERVER, ConOptionStatus, this, "List player options");
	Console()->Register("shutdown", "?r", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("logout", "", CFGFLAG_SERVER, ConLogout, this, "Logout of rcon");

	Console()->Register("record", "?s", CFGFLAG_SERVER|CFGFLAG_STORE, ConRecord, this, "Record to a file");
	Console()->Register("stoprecord", "", CFGFLAG_SERVER, ConStopRecord, this, "Stop recording");

	Console()->Register("reload", "", CFGFLAG_SERVER, ConMapReload, this, "Reload the map");

	Console()->Chain("sv_name", ConchainSpecialInfoupdate, this);
	Console()->Chain("password", ConchainSpecialInfoupdate, this);

	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("mod_command", ConchainModCommandUpdate, this);
	Console()->Chain("console_output_level", ConchainConsoleOutputLevelUpdate, this);

	Console()->Register("mute", "s<clientid> ?i<minutes> ?r<reason>", CFGFLAG_SERVER, ConMute, this, "Mute player with specified id for x minutes for any reason");
	Console()->Register("unmute", "s<clientid>", CFGFLAG_SERVER, ConUnmute, this, "Unmute player with specified id");
	Console()->Register("whisper", "s<id> r<txt>", CFGFLAG_SERVER, ConWhisper, this, "Analogous to 'Say' but sent to a single client only");
	
/* INFECTION MODIFICATION START ***************************************/
#ifdef CONF_SQL
	Console()->Register("inf_add_sqlserver", "ssssssi?i", CFGFLAG_SERVER, ConAddSqlServer, this, "add a sqlserver");
	Console()->Register("inf_list_sqlservers", "s", CFGFLAG_SERVER, ConDumpSqlServers, this, "list all sqlservers readservers = r, writeservers = w");
#endif

	Console()->Register("inf_set_weapon_fire_delay", "i<weapon>i<msec>", CFGFLAG_SERVER, ConSetWeaponFireDelay, this,
	                    "Set InfClass weapon fire delay");
	Console()->Register("inf_set_weapon_ammo_regen", "i<weapon>i<msec>", CFGFLAG_SERVER, ConSetWeaponAmmoRegen, this,
	                    "Set InfClass weapon ammo regen interval");
	Console()->Register("inf_set_weapon_max_ammo", "i<weapon>i<ammo>", CFGFLAG_SERVER, ConSetWeaponMaxAmmo, this,
	                    "Set InfClass weapon max ammo");
/* INFECTION MODIFICATION END *****************************************/

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
	dbg_assert(Type >= 0 && Type <=0xffff, "incorrect type");
	dbg_assert(ID >= 0 && ID <=0xffff, "incorrect id");
	return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

static CServer *CreateServer() { return new CServer(); }

int main(int argc, const char **argv) // ignore_convention
{
#if defined(CONF_FAMILY_WINDOWS)
	for(int i = 1; i < argc; i++) // ignore_convention
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0) // ignore_convention
		{
			ShowWindow(GetConsoleWindow(), SW_HIDE);
			break;
		}
	}
#endif

	if(secure_random_init() != 0)
	{
		dbg_msg("secure", "could not initialize secure RNG");
		return -1;
	}

	CServer *pServer = CreateServer();
	IKernel *pKernel = IKernel::Create();

	// create the components
	IEngine *pEngine = CreateEngine("Teeworlds");
	IEngineMap *pEngineMap = CreateEngineMap();
	IGameServer *pGameServer = CreateGameServer();
	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER|CFGFLAG_ECON);
	IEngineMasterServer *pEngineMasterServer = CreateEngineMasterServer();
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_SERVER, argc, argv); // ignore_convention
	IConfig *pConfig = CreateConfig();
	
	pServer->m_pLocalization = new CLocalization(pStorage);
	pServer->m_pLocalization->InitConfig(0, NULL);
	if(!pServer->m_pLocalization->Init())
	{
		dbg_msg("localization", "could not initialize localization");
		return -1;
	}
	
	pServer->InitRegister(&pServer->m_NetServer, pEngineMasterServer, pConsole);

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pServer); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineMap*>(pEngineMap)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMap*>(pEngineMap));
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pGameServer);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfig);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineMasterServer*>(pEngineMasterServer)); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMasterServer*>(pEngineMasterServer));

		if(RegisterFail)
			return -1;
	}
	
	pEngine->Init();
	pConfig->Init();
	pEngineMasterServer->Init();
	pEngineMasterServer->Load();

	// register all console commands
	pServer->RegisterCommands();

	// execute autoexec file
	pConsole->ExecuteFile("autoexec.cfg");

	// parse the command line arguments
	if(argc > 1) // ignore_convention
		pConsole->ParseArguments(argc-1, &argv[1]); // ignore_convention

	// restore empty config strings to their defaults
	pConfig->RestoreStrings();

	pEngine->InitLogfile();

	// run the server
	dbg_msg("server", "starting...");
	int Ret = pServer->Run();

	delete pServer->m_pLocalization;

	// free
	delete pServer;
	delete pKernel;
	delete pEngineMap;
	delete pGameServer;
	delete pConsole;
	delete pEngineMasterServer;
	delete pStorage;
	delete pConfig;

	return Ret;
}

// DDRace

void CServer::GetClientAddr(int ClientID, NETADDR *pAddr)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		*pAddr = *m_NetServer.ClientAddr(ClientID);
	}
}

/* INFECTION MODIFICATION START ***************************************/
int CServer::IsClientInfectedBefore(int ClientID)
{
	return m_aClients[ClientID].m_WasInfected;
}

void CServer::InfecteClient(int ClientID)
{
	m_aClients[ClientID].m_WasInfected = 1;
	bool NonInfectedFound = false;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State == CServer::CClient::STATE_INGAME && m_aClients[i].m_WasInfected == 0)
		{
			NonInfectedFound = true;
			break;
		}
	}
	
	if(!NonInfectedFound)
	{
		for(int i=0; i<MAX_CLIENTS; i++)
		{
			m_aClients[i].m_WasInfected = 0;
		}
	}
}

int CServer::GetClientAntiPing(int ClientID)
{
	return m_aClients[ClientID].m_AntiPing;
}

void CServer::SetClientAntiPing(int ClientID, int Value)
{
	dbg_msg("server", "set_antiping ClientID=%d antiping=%d", ClientID, Value);
	m_aClients[ClientID].m_AntiPing = Value;
}

int CServer::GetClientCustomSkin(int ClientID)
{
	return m_aClients[ClientID].m_CustomSkin;
}

void CServer::SetClientCustomSkin(int ClientID, int Value)
{
	m_aClients[ClientID].m_CustomSkin = Value;
}

int CServer::GetClientAlwaysRandom(int ClientID)
{
	return m_aClients[ClientID].m_AlwaysRandom;
}

void CServer::SetClientAlwaysRandom(int ClientID, int Value)
{
	m_aClients[ClientID].m_AlwaysRandom = Value;
}

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
	dbg_msg("server", "set_language ClientID=%d lang=%s", ClientID, pLanguage);
	str_copy(m_aClients[ClientID].m_aLanguage, pLanguage, sizeof(m_aClients[ClientID].m_aLanguage));
}
	
int CServer::GetFireDelay(int WID)
{
	return m_InfFireDelay[WID];
}

void CServer::SetFireDelay(int WID, int Time)
{
	m_InfFireDelay[WID] = Time;
}

int CServer::GetAmmoRegenTime(int WID)
{
	return m_InfAmmoRegenTime[WID];
}

void CServer::SetAmmoRegenTime(int WID, int Time)
{
	m_InfAmmoRegenTime[WID] = Time;
}

int CServer::GetMaxAmmo(int WID)
{
	return m_InfMaxAmmo[WID];
}

void CServer::SetMaxAmmo(int WID, int n)
{
	m_InfMaxAmmo[WID] = n;
}

int CServer::GetClassAvailability(int CID)
{
	return m_InfClassAvailability[CID];
}

void CServer::SetClassAvailability(int CID, int n)
{
	m_InfClassAvailability[CID] = n;
}

int CServer::GetClientNbRound(int ClientID)
{
	return m_aClients[ClientID].m_NbRound;
}

int CServer::IsClassChooserEnabled()
{
	return m_InfClassChooser;
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
		case PLAYERCLASS_FKING:
			g_Config.m_InfEnableFKing = Value;
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
	auto& vec = spectators_id;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State == CClient::STATE_INGAME)
		{
			if (std::find(vec.begin(), vec.end(), i) == vec.end())
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

int* CServer::GetIdMap(int ClientID)
{
	return (int*)(IdMap + VANILLA_MAX_CLIENTS * ClientID);
}

void CServer::SetCustClt(int ClientID)
{
	m_aClients[ClientID].m_CustClt = 1;
}
