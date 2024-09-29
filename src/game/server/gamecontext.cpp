/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include <new>

#include <base/logger.h>
#include <base/math.h>
#include <engine/shared/config.h>
#include <engine/map.h>
#include <engine/console.h>
#include <engine/storage.h>
#include <engine/server/roundstatistics.h>
#include <engine/server/sql_server.h>
#include <engine/shared/json.h>
#include <engine/shared/linereader.h>
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <iostream>
#include <algorithm>

#include <game/server/entities/character.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/player.h>

#ifdef CONF_GEOLOCATION
#include <infclassr/geolocation.h>
#endif

// Not thread-safe!
class CClientChatLogger : public ILogger
{
	CGameContext *m_pGameServer;
	int m_ClientId;
	ILogger *m_pOuterLogger;

public:
	CClientChatLogger(CGameContext *pGameServer, int ClientId, ILogger *pOuterLogger) :
		m_pGameServer(pGameServer),
		m_ClientId(ClientId),
		m_pOuterLogger(pOuterLogger)
	{
	}
	void Log(const CLogMessage *pMessage) override;
};

void CClientChatLogger::Log(const CLogMessage *pMessage)
{
	if(str_comp(pMessage->m_aSystem, "chatresp") == 0)
	{
		if(m_Filter.Filters(pMessage))
		{
			return;
		}
		m_pGameServer->SendChatTarget(m_ClientId, pMessage->Message());
	}
	else
	{
		m_pOuterLogger->Log(pMessage);
	}
}

enum
{
	RESET,
	NO_RESET
};

/* INFECTION MODIFICATION START ***************************************/
bool CGameContext::m_ClientMuted[MAX_CLIENTS][MAX_CLIENTS];
icArray<std::string, 256> CGameContext::m_aChangeLogEntries;
icArray<uint32_t, 16> CGameContext::m_aChangeLogPageIndices;

/* INFECTION MODIFICATION END *****************************************/

bool CheckClientId(int ClientId)
{
	return ClientId >= 0 && ClientId < MAX_CLIENTS;
}

void CGameContext::Construct(int Resetting)
{
	m_Resetting = false;
	m_pServer = 0;
	m_pConfig = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_apPlayers[i] = 0;
		m_aHitSoundState[i] = 0;
	}

	mem_zero(&m_aLastPlayerInput, sizeof(m_aLastPlayerInput));
	mem_zero(&m_aPlayerHasInput, sizeof(m_aPlayerHasInput));

	if(Resetting==NO_RESET) // first init
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
			for(int j = 0; j < MAX_CLIENTS; j++)
				CGameContext::m_ClientMuted[i][j] = false;
	}

	m_pController = 0;
	m_aVoteCommand[0] = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LastMapVote = 0;
	m_VoteBanClientId = -1;

	if(Resetting == NO_RESET)
	{
		m_NonEmptySince = 0;
		m_pVoteOptionHeap = new CHeap();
	}
}

void CGameContext::Destruct(int Resetting)
{
	for(int i = 0; i < m_LaserDots.size(); i++)
		Server()->SnapFreeId(m_LaserDots[i].m_SnapId);
	for(int i = 0; i < m_HammerDots.size(); i++)
		Server()->SnapFreeId(m_HammerDots[i].m_SnapId);

	for(auto &pPlayer : m_apPlayers)
		delete pPlayer;

	if(Resetting == NO_RESET)
		delete m_pVoteOptionHeap;

#ifdef CONF_GEOLOCATION
	if(Resetting == NO_RESET)
	{
		Geolocation::Shutdown();
	}
#endif
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	Destruct(m_Resetting ? RESET : NO_RESET);
}

void CGameContext::Clear()
{	
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
	
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		m_BroadcastStates[i].m_NoChangeTick = 0;
		m_BroadcastStates[i].m_LifeSpanTick = 0;
		m_BroadcastStates[i].m_Priority = BROADCAST_PRIORITY_LOWEST;
		m_BroadcastStates[i].m_PrevMessage[0] = 0;
		m_BroadcastStates[i].m_NextMessage[0] = 0;
	}
}

CNetObj_PlayerInput CGameContext::GetLastPlayerInput(int ClientId) const
{
	dbg_assert(0 <= ClientId && ClientId < MAX_CLIENTS, "invalid ClientId");
	return m_aLastPlayerInput[ClientId];
}

CPlayer *CGameContext::GetPlayer(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;

	return m_apPlayers[ClientId];
}

class CCharacter *CGameContext::GetPlayerChar(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_apPlayers[ClientId])
		return 0;
	return m_apPlayers[ClientId]->GetCharacter();
}

void CGameContext::FillAntibot(CAntibotRoundData *pData)
{
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount, int64_t Mask)
{
	float a = 3 * 3.14159f / 2 + Angle;
	//float a = get_angle(dir);
	float s = a - pi / 3;
	float e = a + pi / 3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i + 1) / float(Amount + 2));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd), Mask);
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f * 256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos, int64_t Mask)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateLaserDotEvent(vec2 Pos0, vec2 Pos1, int LifeSpan)
{
	CGameContext::LaserDotState State;
	State.m_Pos0 = Pos0;
	State.m_Pos1 = Pos1;
	State.m_LifeSpan = LifeSpan;
	State.m_SnapId = Server()->SnapNewId();
	
	m_LaserDots.add(State);
}

void CGameContext::CreateHammerDotEvent(vec2 Pos, int LifeSpan)
{
	CGameContext::HammerDotState State;
	State.m_Pos = Pos;
	State.m_LifeSpan = LifeSpan;
	State.m_SnapId = Server()->SnapNewId();
	
	m_HammerDots.add(State);
}

void CGameContext::CreateLoveEvent(vec2 Pos)
{
	CGameContext::LoveDotState State;
	State.m_Pos = Pos;
	State.m_LifeSpan = Server()->TickSpeed();
	State.m_SnapId = Server()->SnapNewId();
	
	m_LoveDots.add(State);
}

void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, int64_t Mask)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

/*
void create_smoke(vec2 Pos)
{
	// create the event
	EV_EXPLOSION *pEvent = (EV_EXPLOSION *)events.create(EVENT_SMOKE, sizeof(EV_EXPLOSION));
	if(pEvent)
	{
		pEvent->x = (int)Pos.x;
		pEvent->y = (int)Pos.y;
	}
}*/

void CGameContext::CreatePlayerSpawn(vec2 Pos, int64_t Mask)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn), Mask);
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientId, int64_t Mask)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientId = ClientId;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int64_t Mask)
{
	if(Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundId = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if(Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundId = Sound;
	if(Target == -2)
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	else
	{
		if(Target == -1)
		{
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);
		}

		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, Target);
	}
}

bool CGameContext::SnapLaserObject(const CSnapContext &Context, int SnapId, const vec2 &To, const vec2 &From, int StartTick, int Owner, int LaserType, int Subtype, int SwitchNumber)
{
	if(Context.GetClientVersion() >= VERSION_DDNET_MULTI_LASER)
	{
		CNetObj_DDNetLaser *pObj = Server()->SnapNewItem<CNetObj_DDNetLaser>(SnapId);
		if(!pObj)
			return false;

		pObj->m_ToX = (int)To.x;
		pObj->m_ToY = (int)To.y;
		pObj->m_FromX = (int)From.x;
		pObj->m_FromY = (int)From.y;
		pObj->m_StartTick = StartTick;
		pObj->m_Owner = Owner;
		pObj->m_Type = LaserType;
		pObj->m_Subtype = Subtype;
		pObj->m_SwitchNumber = SwitchNumber;
	}
	else
	{
		CNetObj_Laser *pObj = Server()->SnapNewItem<CNetObj_Laser>(SnapId);
		if(!pObj)
			return false;

		pObj->m_X = (int)To.x;
		pObj->m_Y = (int)To.y;
		pObj->m_FromX = (int)From.x;
		pObj->m_FromY = (int)From.y;
		pObj->m_StartTick = StartTick;
	}

	return true;
}

bool CGameContext::SnapPickup(const CSnapContext &Context, int SnapId, const vec2 &Pos, int Type, int SubType, int SwitchNumber)
{
	if(Context.IsSixup())
	{
		protocol7::CNetObj_Pickup *pPickup = Server()->SnapNewItem<protocol7::CNetObj_Pickup>(SnapId);
		if(!pPickup)
			return false;

		pPickup->m_X = (int)Pos.x;
		pPickup->m_Y = (int)Pos.y;

		if(Type == POWERUP_WEAPON)
			pPickup->m_Type = SubType == WEAPON_SHOTGUN ? protocol7::PICKUP_SHOTGUN : SubType == WEAPON_GRENADE ? protocol7::PICKUP_GRENADE : protocol7::PICKUP_LASER;
		else if(Type == POWERUP_NINJA)
			pPickup->m_Type = protocol7::PICKUP_NINJA;
	}
	else if(Context.GetClientVersion() >= VERSION_DDNET_ENTITY_NETOBJS)
	{
		CNetObj_DDNetPickup *pPickup = Server()->SnapNewItem<CNetObj_DDNetPickup>(SnapId);
		if(!pPickup)
			return false;

		pPickup->m_X = (int)Pos.x;
		pPickup->m_Y = (int)Pos.y;
		pPickup->m_Type = Type;
		pPickup->m_Subtype = SubType;
		pPickup->m_SwitchNumber = SwitchNumber;
	}
	else
	{
		CNetObj_Pickup *pPickup = Server()->SnapNewItem<CNetObj_Pickup>(SnapId);
		if(!pPickup)
			return false;

		pPickup->m_X = (int)Pos.x;
		pPickup->m_Y = (int)Pos.y;

		pPickup->m_Type = Type;
		if(Context.GetClientVersion() < VERSION_DDNET_WEAPON_SHIELDS)
		{
			if(Type >= POWERUP_ARMOR_SHOTGUN && Type <= POWERUP_ARMOR_LASER)
			{
				pPickup->m_Type = POWERUP_ARMOR;
			}
		}
		pPickup->m_Subtype = SubType;
	}

	return true;
}

void CGameContext::CallVote(int ClientId, const char *pDesc, const char *pCmd, const char *pReason, const char *pChatmsg)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	int64_t Now = Server()->Tick();
	CPlayer *pPlayer = m_apPlayers[ClientId];

	if(!pPlayer)
		return;

	SendChatTarget(-1, pChatmsg);

	m_VoteCreator = ClientId;
	StartVote(pDesc, pCmd, pReason);
	pPlayer->m_Vote = 1;
	pPlayer->m_VotePos = m_VotePos = 1;
	pPlayer->m_LastVoteCall = Now;
}

void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientId = -1;
	Msg.m_pMessage = pText;
	// only for demo record
	if(To < 0)
	{
		if(g_Config.m_SvDemoChat)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
	}

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, To);
}

/* INFECTION MODIFICATION START ***************************************/
void CGameContext::SendChatTarget_Localization(int To, int Category, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientId = -1;
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);

	bool Sent = false;
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i] && !m_apPlayers[i]->IsBot())
		{
			Buffer.clear();
			Buffer.append(GetChatCategoryPrefix(Category));
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);
			
			Msg.m_pMessage = Buffer.buffer();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
			Sent = true;
		}
	}

	if(To < 0 && Sent)
	{
		Buffer.clear();
		Buffer.append(GetChatCategoryPrefix(Category));
		// one message for record
		dynamic_string tmpBuf;
		tmpBuf.copy(Buffer);
		Server()->Localization()->Format_VL(tmpBuf, "en", pText, VarArgs);
		Msg.m_pMessage = tmpBuf.buffer();
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, -1);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "*** %s", Msg.m_pMessage);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
	}

	va_end(VarArgs);
}

void CGameContext::SendChatTarget_Localization_P(int To, int Category, int Number, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientId = -1;
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);

	bool Sent = false;
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i] && !m_apPlayers[i]->IsBot())
		{
			Buffer.clear();
			Buffer.append(GetChatCategoryPrefix(Category));
			Server()->Localization()->Format_VLP(Buffer, m_apPlayers[i]->GetLanguage(), Number, pText, VarArgs);
			
			Msg.m_pMessage = Buffer.buffer();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
			Sent = true;
		}
	}

	if(To < 0 && Sent)
	{
		Buffer.clear();
		Buffer.append(GetChatCategoryPrefix(Category));
		// one message for record
		dynamic_string tmpBuf;
		tmpBuf.copy(Buffer);
		Server()->Localization()->Format_VLP(tmpBuf, "en", Number, pText, VarArgs);
		Msg.m_pMessage = tmpBuf.buffer();
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, -1);
	}

	va_end(VarArgs);
}

void CGameContext::SendMOTD(int To, const char* pText)
{
	if(m_apPlayers[To])
	{
		CNetMsg_Sv_Motd Msg;
		
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::SendMOTD_Localization(int To, const char* pText, ...)
{
	if(m_apPlayers[To])
	{
		dynamic_string Buffer;
		
		CNetMsg_Sv_Motd Msg;
		
		va_list VarArgs;
		va_start(VarArgs, pText);
		
		Server()->Localization()->Format_VL(Buffer, m_apPlayers[To]->GetLanguage(), pText, VarArgs);
	
		va_end(VarArgs);
		
		Msg.m_pMessage = Buffer.buffer();
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::AddBroadcast(int ClientId, const char* pText, int Priority, int LifeSpan)
{
	if(LifeSpan > 0)
	{
		if(m_BroadcastStates[ClientId].m_TimedPriority > Priority)
			return;
			
		str_copy(m_BroadcastStates[ClientId].m_TimedMessage, pText, sizeof(m_BroadcastStates[ClientId].m_TimedMessage));
		m_BroadcastStates[ClientId].m_LifeSpanTick = LifeSpan;
		m_BroadcastStates[ClientId].m_TimedPriority = Priority;
	}
	else
	{
		if(m_BroadcastStates[ClientId].m_Priority > Priority)
			return;
			
		str_copy(m_BroadcastStates[ClientId].m_NextMessage, pText, sizeof(m_BroadcastStates[ClientId].m_NextMessage));
		m_BroadcastStates[ClientId].m_Priority = Priority;
	}
}

void CGameContext::SetClientLanguage(int ClientId, const char *pLanguage)
{
	Server()->SetClientLanguage(ClientId, pLanguage);
	if(m_apPlayers[ClientId])
	{
		m_apPlayers[ClientId]->SetLanguage(pLanguage);
	}
}

void CGameContext::InitChangelog()
{
	if(m_aChangeLogEntries.IsEmpty())
	{
		ReloadChangelog();
	}
}

void CGameContext::ReloadChangelog()
{
	for(std::string &Entry : m_aChangeLogEntries)
	{
		Entry.clear();
	}
	m_aChangeLogEntries.Clear();
	m_aChangeLogPageIndices.Clear();

	const char *pChangelogFilename = Config()->m_SvChangeLogFile;
	if(!pChangelogFilename || pChangelogFilename[0] == 0)
	{
		dbg_msg("ChangeLog", "ChangeLog file is not set");
		return;
	}

	CLineReader LineReader;
	if (!LineReader.OpenFile(m_pStorage->OpenFile(pChangelogFilename, IOFLAG_READ, IStorage::TYPE_ALL)))
	{
		dbg_msg("ChangeLog", "unable to open '%s'", pChangelogFilename);
		return;
	}
	const uint32_t MaxLinesPerPage = Config()->m_SvChangeLogMaxLinesPerPage;
	uint32_t AddedLines = 0;

	icArray<char, 8> SamePageItemStartChars = {
		' ',
		'-',
	};

	while(const char *pLine = LineReader.Get())
	{
		if(pLine[0] == 0)
			continue;

		bool ThisLineIsPartOfPrevious = pLine[0] == ' ';
		bool ImplicitNewPage = str_comp(pLine, "<page>") == 0;
		bool ExplicitNewPage = m_aChangeLogPageIndices.IsEmpty() || (AddedLines >= MaxLinesPerPage);
		ExplicitNewPage = ExplicitNewPage || !SamePageItemStartChars.Contains(pLine[0]);
		if(ImplicitNewPage || ExplicitNewPage)
		{
			if(m_aChangeLogPageIndices.Size() == m_aChangeLogPageIndices.Capacity())
			{
				dbg_msg("ChangeLog", "ChangeLog truncated: only %zu pages allowed", m_aChangeLogPageIndices.Capacity());
				break;
			}
			if(ThisLineIsPartOfPrevious && !m_aChangeLogEntries.IsEmpty())
			{
				m_aChangeLogPageIndices.Add(m_aChangeLogEntries.Size() - 1);
			}
			else
			{
				m_aChangeLogPageIndices.Add(m_aChangeLogEntries.Size());
			}
			AddedLines = 0;
		}
		if(ImplicitNewPage)
		{
			continue;
		}

		if(m_aChangeLogEntries.Size() == m_aChangeLogEntries.Capacity())
		{
			dbg_msg("ChangeLog", "ChangeLog truncated: only %zu lines allowed", m_aChangeLogEntries.Capacity());
			break;
		}
		m_aChangeLogEntries.Add(pLine);
		++AddedLines;
	}
}

bool CGameContext::IsPaused() const
{
	return m_World.m_Paused;
}

void CGameContext::SetPaused(bool Paused)
{
	if(m_pController->IsGameOver())
		return;

	m_World.m_Paused = Paused;
}

bool CGameContext::MapExists(const char *pMapName) const
{
	char aMapFilename[128];
	str_format(aMapFilename, sizeof(aMapFilename), "%s.map", pMapName);

	char aBuf[512];
	return Storage()->FindFile(aMapFilename, "maps", IStorage::TYPE_ALL, aBuf, sizeof(aBuf));
}

void CGameContext::SendBroadcast(int To, const char *pText, int Priority, int LifeSpan)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	// only for server demo record
	if(To < 0)
	{
		CNetMsg_Sv_Broadcast Msg;
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);
	}

	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
			AddBroadcast(i, pText, Priority, LifeSpan);
	}
}

void CGameContext::ClearBroadcast(int To, int Priority)
{
	SendBroadcast(To, "", Priority, BROADCAST_DURATION_REALTIME);
}

const char *CGameContext::GetChatCategoryPrefix(int Category)
{
	switch(Category)
	{
		case CHATCATEGORY_INFECTION:
			return "☣ | ";
		case CHATCATEGORY_SCORE:
			return "★ | ";
		case CHATCATEGORY_PLAYER:
			return "♟ | ";
		case CHATCATEGORY_INFECTED:
			return "⛃ | ";
		case CHATCATEGORY_HUMANS:
			return "⛁ | ";
		case CHATCATEGORY_ACCUSATION:
			return "☹ | ";
		default:
			break;
	}

	return "";
}

void CGameContext::SendBroadcast_Localization(int To, int Priority, int LifeSpan, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	// only for server demo record
	if(To < 0)
	{
		CNetMsg_Sv_Broadcast Msg;
		Server()->Localization()->Format_VL(Buffer, "en", pText, VarArgs);
		Msg.m_pMessage = Buffer.buffer();
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);
	}

	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i] && !m_apPlayers[i]->IsBot())
		{
			Buffer.clear();
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);
			AddBroadcast(i, Buffer.buffer(), Priority, LifeSpan);
		}
	}
	
	va_end(VarArgs);
}

void CGameContext::SendBroadcast_Localization_P(int To, int Priority, int LifeSpan, int Number, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i] && !m_apPlayers[i]->IsBot())
		{
			Server()->Localization()->Format_VLP(Buffer, m_apPlayers[i]->GetLanguage(), Number, pText, VarArgs);
			AddBroadcast(i, Buffer.buffer(), Priority, LifeSpan);
		}
	}

	va_end(VarArgs);
}

/* INFECTION MODIFICATION END *****************************************/

void CGameContext::SendChat(int ChatterClientId, int Team, const char *pText, int SpamProtectionClientId)
{
	if(SpamProtectionClientId >= 0 && SpamProtectionClientId < MAX_CLIENTS)
		if(ProcessSpamProtection(SpamProtectionClientId))
			return;

	char aBuf[256], aText[256];
	str_copy(aText, pText, sizeof(aText));
	if(ChatterClientId >= 0 && ChatterClientId < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientId, Team, Server()->ClientName(ChatterClientId), pText);
	else if(ChatterClientId == -2)
	{
		str_format(aBuf, sizeof(aBuf), "### %s", aText);
		str_copy(aText, aBuf, sizeof(aText));
		ChatterClientId = -1;
	}
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", aText);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, Team != CHAT_ALL ? "teamchat" : "chat", aBuf);

	if(aText[0] == '!' && Config()->m_SvFilterChatCommands)
	{
		return;
	}

	if(SpamProtectionClientId < 0)
		SpamProtectionClientId = ChatterClientId;

	if(Team == CGameContext::CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientId = ChatterClientId;
		Msg.m_pMessage = aText;

		// pack one for the recording only
		if(g_Config.m_SvDemoChat)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);

		// send to the clients that did not mute chatter
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && !CGameContext::m_ClientMuted[i][SpamProtectionClientId])
			{
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
			}
		}
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientId = ChatterClientId;
		Msg.m_pMessage = aText;

		// pack one for the recording only
		if(g_Config.m_SvDemoChat)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);

		// send to the clients
		for(int i = 0; i < Server()->MaxClients(); i++)
		{
			if(m_apPlayers[i] != 0)
			{
				if(Team == CHAT_SPEC)
				{
					if(m_apPlayers[i]->GetTeam() == CHAT_SPEC)
					{
						Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
					}
				}
				else
				{
					if(m_pController->GetPlayerTeam(i) == Team && m_apPlayers[i]->GetTeam() != CHAT_SPEC)
					{
						Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
					}
				}
			}
		}
	}
}

void CGameContext::SendEmoticon(int ClientId, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientId = ClientId;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientId, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CGameContext::SendMotd(int ClientId)
{
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = Config()->m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CGameContext::SendKillMessage(int Killer, int Victim, int Weapon, int ModeSpecial)
{
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = Victim;
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Starting vote \"%s\" for command \"%s\"", pDesc, pCommand);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vote", aBuf);

	// start vote
	m_VoteCloseTime = time_get() + time_freq() * g_Config.m_SvVoteTime;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(-1);
	m_VoteUpdate = true;
}


void CGameContext::EndVote()
{
	if (m_VoteCloseTime == 0)
		return;

	{
		char aBuf[256];
		const auto GetVoteDisplayChar = [](int Vote) -> char {
			if(Vote > 0)
				return 'y';
			if(Vote < 0)
				return 'n';

			return 'i';
		};

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!m_apPlayers[i] || m_apPlayers[i]->IsBot())
				continue;

			if(m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS) // don't count in votes by spectators
				continue;

			if(m_VoteEnforce != VOTE_ENFORCE_UNKNOWN)
			{
				// If the vote led to a decision then skip those who abstain
				if(m_apPlayers[i]->m_Vote == 0)
					continue;
			}
			str_format(aBuf, sizeof(aBuf), "cid=%d vote=%c name=\"%s\"", i, GetVoteDisplayChar(m_apPlayers[i]->m_Vote), Server()->ClientName(i));
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "vote", aBuf);
		}
	}

	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientId)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Timeout = 0;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CGameContext::SendVoteStatus(int ClientId, int Total, int Yes, int No)
{
	if(ClientId == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(Server()->ClientIngame(i))
				SendVoteStatus(i, Total, Yes, No);
		return;
	}

	if(Total > VANILLA_MAX_CLIENTS && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetClientVersion() <= VERSION_DDRACE)
	{
		Yes = (Yes * VANILLA_MAX_CLIENTS) / (float)Total;
		No = (No * VANILLA_MAX_CLIENTS) / (float)Total;
		Total = VANILLA_MAX_CLIENTS;
	}

	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes + No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CGameContext::AbortVoteKickOnDisconnect(int ClientId)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientId) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientId)))
		m_VoteCloseTime = -1;
	
	if(m_VoteCloseTime && m_VoteBanClientId == ClientId)
	{
		m_VoteCloseTime = -1;
		m_VoteBanClientId = -1;
	}
}

void CGameContext::RequestVotesUpdate()
{
	m_VoteUpdate = true;
}

bool CGameContext::HasActiveVote() const
{
	return m_VoteCloseTime;
}

void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->m_pGameType, "DM")==0 ||
		str_comp(m_pController->m_pGameType, "TDM")==0 ||
		str_comp(m_pController->m_pGameType, "CTF")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientId)
{
	if(ClientId == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(m_apPlayers[i])
			{
				SendTuningParams(i);
			}
		}
		return;
	}

	CheckPureTuning();

	SendTuningParams(ClientId, m_Tuning);
}

void CGameContext::SendTuningParams(int ClientId, const CTuningParams &params)
{
	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	const int *pParams = (const int *)&params;

	for(unsigned i = 0; i < sizeof(m_Tuning) / sizeof(int); i++)
	{
		if(m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
		{
			if((i == 30) // laser_damage is removed from 0.7
				&& (Server()->IsSixup(ClientId)))
			{
				continue;
			}
			else if((i == 31) // collision
					&& (m_apPlayers[ClientId]->GetCharacter()->NeededFaketuning() & FAKETUNE_SOLO || m_apPlayers[ClientId]->GetCharacter()->NeededFaketuning() & FAKETUNE_NOCOLL))
			{
				Msg.AddInt(0);
			}
			else if((i == 32) // hooking
					&& (m_apPlayers[ClientId]->GetCharacter()->NeededFaketuning() & FAKETUNE_SOLO || m_apPlayers[ClientId]->GetCharacter()->NeededFaketuning() & FAKETUNE_NOHOOK))
			{
				Msg.AddInt(0);
			}
			else if((i == 3) // ground jump impulse
					&& m_apPlayers[ClientId]->GetCharacter()->NeededFaketuning() & FAKETUNE_NOJUMP)
			{
				Msg.AddInt(0);
			}
			else if((i == 33) // jetpack
					&& !(m_apPlayers[ClientId]->GetCharacter()->NeededFaketuning() & FAKETUNE_JETPACK))
			{
				Msg.AddInt(0);
			}
			else if((i == 36) // hammer hit
					&& m_apPlayers[ClientId]->GetCharacter()->NeededFaketuning() & FAKETUNE_NOHAMMER)
			{
				Msg.AddInt(0);
			}
			else
			{
				Msg.AddInt(pParams[i]);
			}
		}
		else
			Msg.AddInt(pParams[i]); // if everything is normal just send true tunings
	}
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CGameContext::SendHitSound(int ClientId)
{
	if(m_aHitSoundState[ClientId] < 1)
	{
		m_aHitSoundState[ClientId] = 1;
	}
}

void CGameContext::SendScoreSound(int ClientId)
{
	m_aHitSoundState[ClientId] = 2;
}

void CGameContext::OnPreTickTeehistorian()
{
}

bool CGameContext::OnClientDDNetVersionKnown(int ClientId)
{
	IServer::CClientInfo Info;
	dbg_assert(Server()->GetClientInfo(ClientId, &Info), "failed to get client info");
	int ClientVersion = Info.m_DDNetVersion;
	int InfClassVersion = ClientVersion ? Server()->GetClientInfclassVersion(ClientId) : 0;
	dbg_msg("ddnet", "cid=%d version=%d inf=%d", ClientId, ClientVersion, InfClassVersion);

	const int MaxVersion = Config()->m_SvMaxDDNetVersion;
	if(MaxVersion && (ClientVersion > MaxVersion))
	{
		constexpr int BanDuration = 60 * 60 * 24;
		Server()->Ban(ClientId, BanDuration, "unsupported client");
		return true;
	}

	// Autoban known bot versions.
	if(g_Config.m_SvBannedVersions[0] != '\0' && IsVersionBanned(ClientVersion))
	{
		Server()->Kick(ClientId, "unsupported client");
		return true;
	}

	return false;
}

void CGameContext::OnTick()
{
	for(int i=0; i<MAX_CLIENTS; i++)
	{		
		if(m_apPlayers[i])
		{
			//Show top10
			if(!Server()->GetClientMemory(i, CLIENTMEMORY_TOP10))
			{
				if(!g_Config.m_SvMotd[0] || Server()->GetClientMemory(i, CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE))
				{
#ifdef CONF_SQL
					Server()->ShowChallenge(i);
#endif
					Server()->SetClientMemory(i, CLIENTMEMORY_TOP10, true);
				}
			}
		}
	}

	//Check for banvote
	if(!m_VoteCloseTime)
	{
		for(int i=0; i<MAX_CLIENTS; i++)
		{
			if(Server()->ClientShouldBeBanned(i))
			{
				char aDesc[VOTE_DESC_LENGTH] = {0};
				char aCmd[VOTE_CMD_LENGTH] = {0};
				str_format(aCmd, sizeof(aCmd), "ban %d %d Banned by vote", i, g_Config.m_SvVoteKickBantime*3);
				str_format(aDesc, sizeof(aDesc), "Ban \"%s\"", Server()->ClientName(i));
				m_VoteBanClientId = i;
				StartVote(aDesc, aCmd, "");
				continue;
			}
		}
	}

	//Check for mapVote
	if(!m_VoteCloseTime && m_pController->CanVote()) // there is currently no vote && its the start of a round
	{
		IServer::CMapVote* mapVote = Server()->GetMapVote();
		if (mapVote)
		{
			char aChatmsg[512] = {0};
			str_format(aChatmsg, sizeof(aChatmsg), "Starting vote '%s'", mapVote->m_pDesc);
			SendChatTarget(-1, aChatmsg);
			StartVote(mapVote->m_pDesc, mapVote->m_pCommand, mapVote->m_pReason);
		}
	}
	
	// check tuning
	CheckPureTuning();
	
	m_Collision.SetTime(m_pController->GetTime());

	m_pController->TickBeforeWorld();

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	int NumActivePlayers = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			// send vote options
			ProgressVoteOptions(i);

			if(m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				NumActivePlayers++;
			
			Server()->RoundStatistics()->UpdatePlayer(i, m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS);
			
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
			
			if(m_VoteLanguageTick[i] > 0)
			{
				if(m_VoteLanguageTick[i] == 1)
				{
					m_VoteLanguageTick[i] = 0;
					
					CNetMsg_Sv_VoteSet Msg;
					Msg.m_Timeout = 0;
					Msg.m_pDescription = "";
					Msg.m_pReason = "";
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
					
					str_copy(m_VoteLanguage[i], "en", sizeof(m_VoteLanguage[i]));				
					
				}
				else
				{
					m_VoteLanguageTick[i]--;
				}
			}
		}
	}
	
	//Check for new broadcast
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			if(m_BroadcastStates[i].m_LifeSpanTick > 0 && m_BroadcastStates[i].m_TimedPriority > m_BroadcastStates[i].m_Priority)
			{
				str_copy(m_BroadcastStates[i].m_NextMessage, m_BroadcastStates[i].m_TimedMessage, sizeof(m_BroadcastStates[i].m_NextMessage));
			}
			
			//Send broadcast only if the message is different, or to fight auto-fading
			if(
				str_comp(m_BroadcastStates[i].m_PrevMessage, m_BroadcastStates[i].m_NextMessage) != 0 ||
				m_BroadcastStates[i].m_NoChangeTick > Server()->TickSpeed()
			)
			{
				CNetMsg_Sv_Broadcast Msg;
				Msg.m_pMessage = m_BroadcastStates[i].m_NextMessage;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
				
				str_copy(m_BroadcastStates[i].m_PrevMessage, m_BroadcastStates[i].m_NextMessage, sizeof(m_BroadcastStates[i].m_PrevMessage));
				
				m_BroadcastStates[i].m_NoChangeTick = 0;
			}
			else
				m_BroadcastStates[i].m_NoChangeTick++;
			
			//Update broadcast state
			if(m_BroadcastStates[i].m_LifeSpanTick > 0)
				m_BroadcastStates[i].m_LifeSpanTick--;
			
			if(m_BroadcastStates[i].m_LifeSpanTick <= 0)
			{
				m_BroadcastStates[i].m_TimedMessage[0] = 0;
				m_BroadcastStates[i].m_TimedPriority = BROADCAST_PRIORITY_LOWEST;
			}
			m_BroadcastStates[i].m_NextMessage[0] = 0;
			m_BroadcastStates[i].m_Priority = BROADCAST_PRIORITY_LOWEST;
		}
		else
		{
			m_BroadcastStates[i].m_NoChangeTick = 0;
			m_BroadcastStates[i].m_LifeSpanTick = 0;
			m_BroadcastStates[i].m_Priority = BROADCAST_PRIORITY_LOWEST;
			m_BroadcastStates[i].m_TimedPriority = BROADCAST_PRIORITY_LOWEST;
			m_BroadcastStates[i].m_PrevMessage[0] = 0;
			m_BroadcastStates[i].m_NextMessage[0] = 0;
			m_BroadcastStates[i].m_TimedMessage[0] = 0;
		}
	}
	
	//Send score and hit sound
	for(int i=0; i<MAX_CLIENTS; i++)
	{		
		if(m_apPlayers[i])
		{
			int Sound = -1;
			if(m_aHitSoundState[i] == 1)
				Sound = SOUND_HIT;
			else if(m_aHitSoundState[i] == 2)
				Sound = SOUND_CTF_GRAB_PL;
			
			if(Sound >= 0)
			{
				int Mask = CmaskOne(i);
				for(int j = 0; j < MAX_CLIENTS; j++)
				{
					if(m_apPlayers[j] && m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS && m_apPlayers[j]->m_SpectatorId == i)
						Mask |= CmaskOne(j);
				}
				CreateSound(m_apPlayers[i]->m_ViewPos, Sound, Mask);
			}
		}
		
		m_aHitSoundState[i] = 0;
	}
	
	Server()->RoundStatistics()->UpdateNumberOfPlayers(NumActivePlayers);
	
/* INFECTION MODIFICATION START ***************************************/
	//Clean old dots
	int DotIter;
	
	DotIter = 0;
	while(DotIter < m_LaserDots.size())
	{
		m_LaserDots[DotIter].m_LifeSpan--;
		if(m_LaserDots[DotIter].m_LifeSpan <= 0)
		{
			Server()->SnapFreeId(m_LaserDots[DotIter].m_SnapId);
			m_LaserDots.remove_index(DotIter);
		}
		else
			DotIter++;
	}
	
	DotIter = 0;
	while(DotIter < m_HammerDots.size())
	{
		m_HammerDots[DotIter].m_LifeSpan--;
		if(m_HammerDots[DotIter].m_LifeSpan <= 0)
		{
			Server()->SnapFreeId(m_HammerDots[DotIter].m_SnapId);
			m_HammerDots.remove_index(DotIter);
		}
		else
			DotIter++;
	}
	
	DotIter = 0;
	while(DotIter < m_LoveDots.size())
	{
		m_LoveDots[DotIter].m_LifeSpan--;
		m_LoveDots[DotIter].m_Pos.y -= 5.0f;
		if(m_LoveDots[DotIter].m_LifeSpan <= 0)
		{
			Server()->SnapFreeId(m_LoveDots[DotIter].m_SnapId);
			m_LoveDots.remove_index(DotIter);
		}
		else
			DotIter++;
	}
/* INFECTION MODIFICATION END *****************************************/

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			EndVote();
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || aVoteChecked[i])
						continue;

					if(m_apPlayers[i]->IsBot())
						continue;

					if(m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS) // don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i + 1; j < MAX_CLIENTS; j++)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]) != 0)
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}

				if(Yes >= Total/2+1)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= (Total+1)/2)
					m_VoteEnforce = VOTE_ENFORCE_NO;
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				if(m_VoteBanClientId >= 0)
				{
					Server()->RemoveAccusations(m_VoteBanClientId);
					m_VoteBanClientId = -1;
				}
				
				Server()->SetRconCid(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCid(IServer::RCON_CID_SERV);
				EndVote();
				SendChatTarget(-1, "Vote passed");
				if(GetOptionVoteType(m_aVoteCommand) & MAP_VOTE_BITS)
					Server()->ResetMapVotes();

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChatTarget(-1, "Vote failed");
				if(GetOptionVoteType(m_aVoteCommand) & MAP_VOTE_BITS)
					Server()->ResetMapVotes();
				
				//Remove accusation if needed
				if(m_VoteBanClientId >= 0)
				{
					Server()->RemoveAccusations(m_VoteBanClientId);
					m_VoteBanClientId = -1;
				}
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[MAX_CLIENTS-i-1]->OnPredictedInput(&Input);
		}
	}
#endif
}

static int PlayerFlags_SevenToSix(int Flags)
{
	int Six = 0;
	if(Flags & protocol7::PLAYERFLAG_CHATTING)
		Six |= PLAYERFLAG_CHATTING;
	if(Flags & protocol7::PLAYERFLAG_SCOREBOARD)
		Six |= PLAYERFLAG_SCOREBOARD;
	if(Flags & protocol7::PLAYERFLAG_AIM)
		Six |= PLAYERFLAG_AIM;
	return Six;
}

// Server hooks
void CGameContext::OnClientPrepareInput(int ClientId, void *pInput)
{
	auto *pPlayerInput = (CNetObj_PlayerInput *)pInput;
	if(Server()->IsSixup(ClientId))
		pPlayerInput->m_PlayerFlags = PlayerFlags_SevenToSix(pPlayerInput->m_PlayerFlags);
}

void CGameContext::OnClientDirectInput(int ClientId, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientId]->OnDirectInput((CNetObj_PlayerInput *)pInput);

	int Flags = ((CNetObj_PlayerInput *)pInput)->m_PlayerFlags;
	if((Flags & 256) || (Flags & 512))
	{
		Server()->Kick(ClientId, "please update your client or use DDNet client");
	}
}

void CGameContext::OnClientPredictedInput(int ClientId, void *pInput)
{
	// early return if no input at all has been sent by a player
	if(pInput == nullptr && !m_aPlayerHasInput[ClientId])
		return;

	// set to last sent input when no new input has been sent
	const CNetObj_PlayerInput *pApplyInput = (CNetObj_PlayerInput *)pInput;
	if(pApplyInput == nullptr)
	{
		pApplyInput = &m_aLastPlayerInput[ClientId];
	}

	if(!m_World.m_Paused)
		m_apPlayers[ClientId]->OnPredictedInput(pApplyInput);
}

void CGameContext::OnClientPredictedEarlyInput(int ClientId, void *pInput)
{
	// early return if no input at all has been sent by a player
	if(pInput == nullptr && !m_aPlayerHasInput[ClientId])
		return;

	// set to last sent input when no new input has been sent
	CNetObj_PlayerInput *pApplyInput = (CNetObj_PlayerInput *)pInput;
	if(pApplyInput == nullptr)
	{
		pApplyInput = &m_aLastPlayerInput[ClientId];
	}
	else
	{
		// Store input in this function and not in `OnClientPredictedInput`,
		// because this function is called on all inputs, while
		// `OnClientPredictedInput` is only called on the first input of each
		// tick.
		mem_copy(&m_aLastPlayerInput[ClientId], pApplyInput, sizeof(m_aLastPlayerInput[ClientId]));
		m_aPlayerHasInput[ClientId] = true;
	}

	if(!m_World.m_Paused)
		m_apPlayers[ClientId]->OnPredictedEarlyInput(pApplyInput);
}

struct CVoteOptionServer *CGameContext::GetVoteOption(int Index)
{
	CVoteOptionServer *pCurrent;
	for(pCurrent = m_pVoteOptionFirst;
		Index > 0 && pCurrent;
		Index--, pCurrent = pCurrent->m_pNext)
		;

	if(Index > 0)
		return 0;
	return pCurrent;
}

void CGameContext::ProgressVoteOptions(int ClientId)
{
	CPlayer *pPl = m_apPlayers[ClientId];

	if(pPl->m_SendVoteIndex == -1)
		return; // we didn't start sending options yet

	if(pPl->m_SendVoteIndex > m_NumVoteOptions)
		return; // shouldn't happen / fail silently

	int VotesLeft = m_NumVoteOptions - pPl->m_SendVoteIndex;
	int NumVotesToSend = minimum(g_Config.m_SvSendVotesPerTick, VotesLeft);

	if(!VotesLeft)
	{
		// player has up to date vote option list
		return;
	}

	// build vote option list msg
	int CurIndex = 0;

	CNetMsg_Sv_VoteOptionListAdd OptionMsg;
	OptionMsg.m_pDescription0 = "";
	OptionMsg.m_pDescription1 = "";
	OptionMsg.m_pDescription2 = "";
	OptionMsg.m_pDescription3 = "";
	OptionMsg.m_pDescription4 = "";
	OptionMsg.m_pDescription5 = "";
	OptionMsg.m_pDescription6 = "";
	OptionMsg.m_pDescription7 = "";
	OptionMsg.m_pDescription8 = "";
	OptionMsg.m_pDescription9 = "";
	OptionMsg.m_pDescription10 = "";
	OptionMsg.m_pDescription11 = "";
	OptionMsg.m_pDescription12 = "";
	OptionMsg.m_pDescription13 = "";
	OptionMsg.m_pDescription14 = "";

	// get current vote option by index
	CVoteOptionServer *pCurrent = GetVoteOption(pPl->m_SendVoteIndex);

	while(CurIndex < NumVotesToSend && pCurrent != NULL)
	{
		switch(CurIndex)
		{
		case 0: OptionMsg.m_pDescription0 = pCurrent->m_aDescription; break;
		case 1: OptionMsg.m_pDescription1 = pCurrent->m_aDescription; break;
		case 2: OptionMsg.m_pDescription2 = pCurrent->m_aDescription; break;
		case 3: OptionMsg.m_pDescription3 = pCurrent->m_aDescription; break;
		case 4: OptionMsg.m_pDescription4 = pCurrent->m_aDescription; break;
		case 5: OptionMsg.m_pDescription5 = pCurrent->m_aDescription; break;
		case 6: OptionMsg.m_pDescription6 = pCurrent->m_aDescription; break;
		case 7: OptionMsg.m_pDescription7 = pCurrent->m_aDescription; break;
		case 8: OptionMsg.m_pDescription8 = pCurrent->m_aDescription; break;
		case 9: OptionMsg.m_pDescription9 = pCurrent->m_aDescription; break;
		case 10: OptionMsg.m_pDescription10 = pCurrent->m_aDescription; break;
		case 11: OptionMsg.m_pDescription11 = pCurrent->m_aDescription; break;
		case 12: OptionMsg.m_pDescription12 = pCurrent->m_aDescription; break;
		case 13: OptionMsg.m_pDescription13 = pCurrent->m_aDescription; break;
		case 14: OptionMsg.m_pDescription14 = pCurrent->m_aDescription; break;
		}

		CurIndex++;
		pCurrent = pCurrent->m_pNext;
	}

	// send msg
	if(pPl->m_SendVoteIndex == 0)
	{
		CNetMsg_Sv_VoteOptionGroupStart StartMsg;
		Server()->SendPackMsg(&StartMsg, MSGFLAG_VITAL, ClientId);
	}

	OptionMsg.m_NumOptions = NumVotesToSend;
	Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientId);

	pPl->m_SendVoteIndex += NumVotesToSend;

	if(pPl->m_SendVoteIndex == m_NumVoteOptions)
	{
		CNetMsg_Sv_VoteOptionGroupEnd EndMsg;
		Server()->SendPackMsg(&EndMsg, MSGFLAG_VITAL, ClientId);
	}
}

void CGameContext::OnClientEnter(int ClientId)
{
	IServer::CClientInfo Info;
	if(Server()->GetClientInfo(ClientId, &Info) && Info.m_GotDDNetVersion)
	{
		if(OnClientDDNetVersionKnown(ClientId))
			return; // kicked
	}

	m_pController->OnPlayerConnect(m_apPlayers[ClientId]);

	// world.insert_entity(&players[client_id]);
	m_apPlayers[ClientId]->m_IsInGame = true;
	m_apPlayers[ClientId]->Respawn();

	{
		CNetMsg_Sv_CommandInfoGroupStart Msg;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
	}
	for(const IConsole::CCommandInfo *pCmd = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_USER, CFGFLAG_CHAT);
		pCmd; pCmd = pCmd->NextCommandInfo(IConsole::ACCESS_LEVEL_USER, CFGFLAG_CHAT))
	{
		const char *pName = pCmd->m_pName;

		if(Server()->IsSixup(ClientId))
		{
			if(!str_comp_nocase(pName, "w") || !str_comp_nocase(pName, "whisper"))
				continue;

			protocol7::CNetMsg_Sv_CommandInfo Msg;
			Msg.m_pName = pName;
			Msg.m_pArgsFormat = pCmd->m_pParams;
			Msg.m_pHelpText = pCmd->m_pHelp;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
		}
		else
		{
			CNetMsg_Sv_CommandInfo Msg;
			Msg.m_pName = pName;
			Msg.m_pArgsFormat = pCmd->m_pParams;
			Msg.m_pHelpText = pCmd->m_pHelp;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
		}
	}
	{
		CNetMsg_Sv_CommandInfoGroupEnd Msg;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
	}

	m_VoteUpdate = true;

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientId);

	Server()->ExpireServerInfo();

	CPlayer *pNewPlayer = m_apPlayers[ClientId];
	mem_zero(&m_aLastPlayerInput[ClientId], sizeof(m_aLastPlayerInput[ClientId]));
	m_aPlayerHasInput[ClientId] = false;
}

bool CGameContext::OnClientDataPersist(int ClientId, void *pData)
{
	CPersistentClientData *pPersistent = (CPersistentClientData *)pData;
	const CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
	{
		return false;
	}

	pPersistent->m_IsSpectator = pPlayer->GetTeam() == TEAM_SPECTATORS;
	pPersistent->m_ClientNameLocked = pPlayer->m_ClientNameLocked;

	return m_pController->GetClientPersistentData(ClientId, pData);
}

void CGameContext::OnClientConnected(int ClientId, void *pData)
{
	CPersistentClientData *pPersistentData = (CPersistentClientData *)pData;
	bool Spec = false;
	bool NameLocked = false;
	if(pPersistentData)
	{
		Spec = pPersistentData->m_IsSpectator;
		NameLocked = pPersistentData->m_ClientNameLocked;
	}

	{
		bool Empty = true;
		for(auto &pPlayer : m_apPlayers)
		{
			// connecting clients with spoofed ips can clog slots without being ingame
			if(pPlayer && Server()->ClientIngame(pPlayer->GetCid()))
			{
				Empty = false;
				break;
			}
		}
		if(Empty)
		{
			m_NonEmptySince = Server()->Tick();
		}
	}

	dbg_assert(!m_apPlayers[ClientId], "non-free player slot");
	m_apPlayers[ClientId] = m_pController->CreatePlayer(ClientId, Spec, pData);
	m_apPlayers[ClientId]->m_ClientNameLocked = NameLocked;
	
#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		if(ClientId >= MAX_CLIENTS-g_Config.m_DbgDummies)
			return;
	}
#endif

	// send motd
	if(!Server()->GetClientMemory(ClientId, CLIENTMEMORY_MOTD))
	{
		SendMotd(ClientId);
		Server()->SetClientMemory(ClientId, CLIENTMEMORY_MOTD, true);
	}

	m_BroadcastStates[ClientId].m_NoChangeTick = 0;
	m_BroadcastStates[ClientId].m_LifeSpanTick = 0;
	m_BroadcastStates[ClientId].m_Priority = BROADCAST_PRIORITY_LOWEST;
	m_BroadcastStates[ClientId].m_PrevMessage[0] = 0;
	m_BroadcastStates[ClientId].m_NextMessage[0] = 0;

	Server()->ExpireServerInfo();
}

void CGameContext::OnClientDrop(int ClientId, EClientDropType Type, const char *pReason)
{
	AbortVoteKickOnDisconnect(ClientId);
	if(!m_apPlayers[ClientId])
		return;

	m_pController->OnPlayerDisconnect(m_apPlayers[ClientId], Type, pReason);
	delete m_apPlayers[ClientId];
	m_apPlayers[ClientId] = 0;

	m_VoteUpdate = true;

	// update spectator modes
	for(auto &pPlayer : m_apPlayers)
	{
		if(pPlayer && pPlayer->m_SpectatorId == ClientId)
			pPlayer->m_SpectatorId = SPEC_FREEVIEW;
	}

	// update conversation targets
	for(auto &pPlayer : m_apPlayers)
	{
		if(pPlayer && pPlayer->m_LastWhisperTo == ClientId)
			pPlayer->m_LastWhisperTo = -1;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// remove everyone this player had muted
		CGameContext::m_ClientMuted[ClientId][i] = false;
		
		// reset mutes for everyone that muted this player
		CGameContext::m_ClientMuted[i][ClientId] = false;
	}

	Server()->ExpireServerInfo();
}

void CGameContext::OnClientEngineJoin(int ClientId, bool Sixup)
{
}

void CGameContext::OnClientEngineDrop(int ClientId, const char *pReason)
{
}

CGameContext::OPTION_VOTE_TYPE CGameContext::GetOptionVoteType(const char *pVoteCommand)
{
	char command[512] = {0};
	int i = 0;
	for ( ; i < 510; i++)
	{
		if (pVoteCommand[i] == 0) break;
		if (pVoteCommand[i] == ' ') break;
		command[i] = pVoteCommand[i];
	}
	command[++i] = 0;
	if(str_comp_nocase(command, "sv_map") == 0) return SV_MAP;
	if(str_comp_nocase(command, "change_map") == 0) return CHANGE_MAP;
	if(str_comp_nocase(command, "skip_map") == 0) return SKIP_MAP;
	if(str_comp_nocase(command, "adjust sv_rounds_per_map +") == 0) return PLAY_MORE_VOTE_TYPE;
	if(str_startswith(command, "queue_")) return QUEUED_VOTE;
	return OTHER_OPTION_VOTE_TYPE;
}

// copies the map name inside pCommand into pMapName
// make sure pMapName is big enough to hold the name and pCommand is null terminated 
// example: input pCommand = "change_map infc_newdust", output pMapName = "infc_newdust"
void CGameContext::GetMapNameFromCommand(char* pMapName, const char *pCommand)
{
	bool readingMapName = false;
	int k = 0;
	for (int i=0 ; i<510; i++)
	{
		if (pCommand[i] == 0) break;
		if (pCommand[i] == ' ') 
		{
			readingMapName = true;
			continue;
		}
		if (!readingMapName) continue;
		pMapName[k] = pCommand[i];
		k++;
	}
	pMapName[k] = 0;
}

void *CGameContext::PreProcessMsg(int *pMsgId, CUnpacker *pUnpacker, int ClientId)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(*pMsgId, pUnpacker);

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			int MsgId = *pMsgId;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgId), MsgId, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
	}

	return pRawMsg;
}

void CGameContext::CensorMessage(char *pCensoredMessage, const char *pMessage, int Size)
{
	str_copy(pCensoredMessage, pMessage, Size);
}

void CGameContext::OnMessage(int MsgId, CUnpacker *pUnpacker, int ClientId)
{
	void *pRawMsg = PreProcessMsg(&MsgId, pUnpacker, ClientId);

	if(!pRawMsg)
		return;

	CPlayer *pPlayer = m_apPlayers[ClientId];
	//HACK: DDNet Client did something wrong that we can detect
	//Round and Score conditions are here only to prevent false-positif
	if(!pPlayer && Server()->GetClientNbRound(ClientId) == 0)
	{
		Server()->Kick(ClientId, "Kicked (is probably a dummy)");
		return;
	}

	if(Server()->ClientIngame(ClientId))
	{
		switch(MsgId)
		{
		case NETMSGTYPE_CL_SAY:
			OnSayNetMessage(static_cast<CNetMsg_Cl_Say *>(pRawMsg), ClientId, pUnpacker);
			break;
		case NETMSGTYPE_CL_CALLVOTE:
			OnCallVoteNetMessage(static_cast<CNetMsg_Cl_CallVote *>(pRawMsg), ClientId);
			break;
		case NETMSGTYPE_CL_VOTE:
			OnVoteNetMessage(static_cast<CNetMsg_Cl_Vote *>(pRawMsg), ClientId);
			break;
		case NETMSGTYPE_CL_SETTEAM:
			OnSetTeamNetMessage(static_cast<CNetMsg_Cl_SetTeam *>(pRawMsg), ClientId);
			break;
		case NETMSGTYPE_CL_ISDDNETLEGACY:
			OnIsDDNetLegacyNetMessage(static_cast<CNetMsg_Cl_IsDDNetLegacy *>(pRawMsg), ClientId, pUnpacker);
			break;
		case NETMSGTYPE_CL_SETSPECTATORMODE:
			OnSetSpectatorModeNetMessage(static_cast<CNetMsg_Cl_SetSpectatorMode *>(pRawMsg), ClientId);
			break;
		case NETMSGTYPE_CL_CHANGEINFO:
			OnChangeInfoNetMessage(static_cast<CNetMsg_Cl_ChangeInfo *>(pRawMsg), ClientId);
			break;
		case NETMSGTYPE_CL_EMOTICON:
			OnEmoticonNetMessage(static_cast<CNetMsg_Cl_Emoticon *>(pRawMsg), ClientId);
			break;
		case NETMSGTYPE_CL_KILL:
			OnKillNetMessage(static_cast<CNetMsg_Cl_Kill *>(pRawMsg), ClientId);
			break;
		default:
			break;
		}
	}
	if(MsgId == NETMSGTYPE_CL_STARTINFO)
	{
		OnStartInfoNetMessage(static_cast<CNetMsg_Cl_StartInfo *>(pRawMsg), ClientId);
	}
}

void CGameContext::OnSayNetMessage(const CNetMsg_Cl_Say *pMsg, int ClientId, const CUnpacker *pUnpacker)
{
	if(!str_utf8_check(pMsg->m_pMessage))
	{
		return;
	}
	CPlayer *pPlayer = m_apPlayers[ClientId];
	int Team = pMsg->m_Team;

	// trim right and set maximum length to 256 utf8-characters
	int Length = 0;
	const char *p = pMsg->m_pMessage;
	const char *pEnd = 0;
	while(*p)
	{
		const char *pStrOld = p;
		int Code = str_utf8_decode(&p);

		// check if unicode is not empty
		if(!str_utf8_isspace(Code))
		{
			pEnd = 0;
		}
		else if(pEnd == 0)
			pEnd = pStrOld;

		if(++Length >= 256)
		{
			*(const_cast<char *>(p)) = 0;
			break;
		}
	}
	if(pEnd != 0)
		*(const_cast<char *>(pEnd)) = 0;

	// drop empty and autocreated spam messages (more than 32 characters per second)
	if(Length == 0 || (pMsg->m_pMessage[0] != '/' && (g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat + Server()->TickSpeed() * ((31 + Length) / 32) > Server()->Tick())))
		return;

	if(Team)
	{
		if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		{
			Team = CHAT_SPEC;
		}
		else
		{
			Team = m_pController->GetPlayerTeam(pPlayer->GetCid());
		}
	}
	else
	{
		Team = CHAT_ALL;
	}

	if(pMsg->m_pMessage[0] == '/')
	{
		if(str_startswith_nocase(pMsg->m_pMessage + 1, "w "))
		{
			char aWhisperMsg[256];
			str_copy(aWhisperMsg, pMsg->m_pMessage + 3, 256);
			Whisper(pPlayer->GetCid(), aWhisperMsg);
		}
		else if(str_startswith_nocase(pMsg->m_pMessage + 1, "whisper "))
		{
			char aWhisperMsg[256];
			str_copy(aWhisperMsg, pMsg->m_pMessage + 9, 256);
			Whisper(pPlayer->GetCid(), aWhisperMsg);
		}
		else if(str_startswith_nocase(pMsg->m_pMessage + 1, "c "))
		{
			char aWhisperMsg[256];
			str_copy(aWhisperMsg, pMsg->m_pMessage + 3, 256);
			Converse(pPlayer->GetCid(), aWhisperMsg);
		}
		else if(str_startswith_nocase(pMsg->m_pMessage + 1, "converse "))
		{
			char aWhisperMsg[256];
			str_copy(aWhisperMsg, pMsg->m_pMessage + 10, 256);
			Converse(pPlayer->GetCid(), aWhisperMsg);
		}
		/* INFECTION MODIFICATION START ***************************************/
		else if(str_comp_num(pMsg->m_pMessage + 1, "msg ", 4) == 0)
		{
			PrivateMessage(pMsg->m_pMessage + 5, ClientId, (Team != CGameContext::CHAT_ALL));
		}
		else if(str_comp_num(pMsg->m_pMessage + 1, "mute ", 5) == 0)
		{
			MutePlayer(pMsg->m_pMessage + 6, ClientId);
		}
		else
		{
			if(g_Config.m_SvSpamprotection && !str_startswith(pMsg->m_pMessage + 1, "timeout ") && pPlayer->m_aLastCommands[0] && pPlayer->m_aLastCommands[0] + Server()->TickSpeed() > Server()->Tick() && pPlayer->m_aLastCommands[1] && pPlayer->m_aLastCommands[1] + Server()->TickSpeed() > Server()->Tick() && pPlayer->m_aLastCommands[2] && pPlayer->m_aLastCommands[2] + Server()->TickSpeed() > Server()->Tick() && pPlayer->m_aLastCommands[3] && pPlayer->m_aLastCommands[3] + Server()->TickSpeed() > Server()->Tick())
				return;

			int64_t Now = Server()->Tick();
			pPlayer->m_aLastCommands[pPlayer->m_LastCommandPos] = Now;
			pPlayer->m_LastCommandPos = (pPlayer->m_LastCommandPos + 1) % 4;

			switch(Server()->GetAuthedState(ClientId))
			{
			case IServer::AUTHED_ADMIN:
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				break;
			case IServer::AUTHED_MOD:
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_MOD);
				break;
			default:
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_USER);
			}

			{
				CClientChatLogger Logger(this, ClientId, log_get_scope_logger());
				CLogScope Scope(&Logger);
				Console()->ExecuteLineFlag(pMsg->m_pMessage + 1, CFGFLAG_CHAT, ClientId, (Team != CGameContext::CHAT_ALL));
			}

			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "%d used %s", ClientId, pMsg->m_pMessage);
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "chat-command", aBuf);

			Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
		}
	}
	else
	{
		// Inverse order and add ligature for arabic
		dynamic_string Buffer;
		Buffer.copy(pMsg->m_pMessage);
		Server()->Localization()->ArabicShaping(Buffer);
		SendChat(ClientId, Team, Buffer.buffer(), ClientId);
	}
	/* INFECTION MODIFICATION END *****************************************/
}

void CGameContext::OnCallVoteNetMessage(const CNetMsg_Cl_CallVote *pMsg, int ClientId)
{
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(RateLimitPlayerVote(ClientId))
		return;

	char aChatmsg[512] = {0};
	char aDesc[VOTE_DESC_LENGTH] = {0};
	char aCmd[VOTE_CMD_LENGTH] = {0};
	char aReason[VOTE_REASON_LENGTH] = "No reason given";
	if(!str_utf8_check(pMsg->m_pType) || !str_utf8_check(pMsg->m_pReason) || !str_utf8_check(pMsg->m_pValue))
	{
		return;
	}
	if(pMsg->m_pReason[0])
	{
		str_copy(aReason, pMsg->m_pReason, sizeof(aReason));
	}

	if(str_comp_nocase(pMsg->m_pType, "kick") == 0)
	{
		int KickId = str_toint(pMsg->m_pValue);
		if(KickId < 0 || KickId >= MAX_CLIENTS || !m_apPlayers[KickId])
		{
			SendChatTarget(ClientId, "Invalid client id to kick");
			return;
		}
		if(KickId == ClientId)
		{
			SendChatTarget(ClientId, "You can't kick yourself");
			return;
		}
		if(m_apPlayers[KickId]->IsBot())
		{
			SendChatTarget(ClientId, "Unable to kick a server-side bot");
			return;
		}
		if(Server()->GetAuthedState(KickId) != IServer::AUTHED_NO)
		{
			SendChatTarget(ClientId, "You can't kick admins");
			char aBufKick[128];
			str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientId));
			SendChatTarget(KickId, aBufKick);
			return;
		}

		Server()->AddAccusation(ClientId, KickId, aReason);
	}
	else
	{
		const int Authed = Server()->GetAuthedState(ClientId);
		if(pPlayer->GetTeam() == TEAM_SPECTATORS && (Authed != IServer::AUTHED_ADMIN))
		{
			SendChatTarget(ClientId, "Spectators aren't allowed to start a vote.");
			return;
		}

		if(str_comp_nocase(pMsg->m_pType, "option") == 0)
		{
			// this vote is not a kick/ban or spectate vote
			CVoteOptionServer *pOption = m_pVoteOptionFirst;
			while(pOption) // loop through all option votes to find out which vote it is
			{
				if(str_comp_nocase(pMsg->m_pValue, pOption->m_aDescription) == 0) // found out which vote it is
				{
					if(!Console()->LineIsValid(pOption->m_aCommand))
					{
						SendChatTarget(ClientId, "Invalid option");
						return;
					}
					OPTION_VOTE_TYPE OptionVoteType = GetOptionVoteType(pOption->m_aCommand);
					if(OptionVoteType & MAP_VOTE_BITS) // this is a map vote
					{
						if(OptionVoteType == SV_MAP || OptionVoteType == CHANGE_MAP)
						{
							// check if we are already playing on the map the user wants to vote
							char MapName[VOTE_CMD_LENGTH] = {0};
							GetMapNameFromCommand(MapName, pOption->m_aCommand);
							if(str_comp_nocase(MapName, g_Config.m_SvMap) == 0)
							{
								char aBufVoteMap[128];
								str_format(aBufVoteMap, sizeof(aBufVoteMap), "Server is already on map %s", MapName);
								SendChatTarget(ClientId, aBufVoteMap);
								return;
							}
						}

						int RoundCount = m_pController->GetRoundCount();
						if(m_pController->IsRoundEndTime())
							RoundCount++;
						if(g_Config.m_InfMinRoundsForMapVote > RoundCount && Server()->GetActivePlayerCount() > 1)
						{
							char aBufVoteMap[128];
							str_format(aBufVoteMap, sizeof(aBufVoteMap), "Each map must be played at least %i rounds before calling a map vote", g_Config.m_InfMinRoundsForMapVote);
							SendChatTarget(ClientId, aBufVoteMap);
							return;
						}
					}
					if((OptionVoteType == PLAY_MORE_VOTE_TYPE) || (OptionVoteType == QUEUED_VOTE))
					{
						// copy information to start a vote
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientId),
							pOption->m_aDescription, aReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						break;
					}
					if(g_Config.m_InfMinPlayerNumberForMapVote <= 1 || OptionVoteType == OTHER_OPTION_VOTE_TYPE)
					{
						// (this is not a map vote) or ("InfMinPlayerNumberForMapVote <= 1" and we keep default behaviour)
						if(!m_pController->CanVote() && (Authed != IServer::AUTHED_ADMIN))
						{
							SendChatTarget(ClientId, "Votes are only allowed when the round start.");
							return;
						}

						// copy information to start a vote
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientId),
							pOption->m_aDescription, aReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						break;
					}

					if(OptionVoteType & MAP_VOTE_BITS)
					{
						// this vote is a map vote
						Server()->AddMapVote(ClientId, pOption->m_aCommand, aReason, pOption->m_aDescription);
						return;
					}

					break;
				}

				pOption = pOption->m_pNext;
			}

			if(!pOption)
			{
				if(Authed != IServer::AUTHED_ADMIN) // allow admins to call any vote they want
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_pValue);
					SendChatTarget(ClientId, aChatmsg);
					return;
				}
				else
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s'", Server()->ClientName(ClientId), pMsg->m_pValue);
					str_format(aDesc, sizeof(aDesc), "%s", pMsg->m_pValue);
					str_format(aCmd, sizeof(aCmd), "%s", pMsg->m_pValue);
				}
			}
		}
		else if(str_comp_nocase(pMsg->m_pType, "spectate") == 0)
		{
			if(!g_Config.m_SvVoteSpectate)
			{
				SendChatTarget(ClientId, "Server does not allow voting to move players to spectators");
				return;
			}

			int SpectateId = str_toint(pMsg->m_pValue);
			if(SpectateId < 0 || SpectateId >= MAX_CLIENTS || !m_apPlayers[SpectateId] || m_apPlayers[SpectateId]->GetTeam() == TEAM_SPECTATORS)
			{
				SendChatTarget(ClientId, "Invalid client id to move");
				return;
			}
			if(SpectateId == ClientId)
			{
				SendChatTarget(ClientId, "You can't move yourself");
				return;
			}
			if(m_apPlayers[SpectateId]->IsBot())
			{
				SendChatTarget(ClientId, "Unable to move a server-side bot to spectators");
				return;
			}
			if(!Server()->ReverseTranslate(SpectateId, ClientId))
			{
				return;
			}

			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientId), Server()->ClientName(SpectateId), aReason);
			str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateId));
			str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateId, g_Config.m_SvVoteSpectateRejoindelay);
		}

		// Start a vote
		if(aCmd[0])
		{
			CallVote(ClientId, aDesc, aCmd, aReason, aChatmsg);
		}
	}
}

void CGameContext::OnVoteNetMessage(const CNetMsg_Cl_Vote *pMsg, int ClientId)
{
	if(!pMsg->m_Vote)
		return;

	if(m_VoteLanguageTick[ClientId] > 0)
	{
		if(pMsg->m_Vote)
		{
			if(pMsg->m_Vote > 0)
			{
				SetClientLanguage(ClientId, m_VoteLanguage[ClientId]);
			}

			m_VoteLanguageTick[ClientId] = 0;

			CNetMsg_Sv_VoteSet Msg;
			Msg.m_Timeout = 0;
			Msg.m_pDescription = "";
			Msg.m_pReason = "";
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);
		}
		return;
	}

	CPlayer *pPlayer = m_apPlayers[ClientId];

	if(!m_VoteCloseTime || pPlayer->m_Vote)
	{
		m_pController->OnPlayerVoteCommand(ClientId, pMsg->m_Vote);
	}

	int64_t Now = Server()->Tick();

	pPlayer->m_LastVoteTry = Now;
	pPlayer->m_Vote = pMsg->m_Vote;
	pPlayer->m_VotePos = ++m_VotePos;
	m_VoteUpdate = true;

	CNetMsg_Sv_YourVote Msg = {pMsg->m_Vote};
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);
}

void CGameContext::OnSetTeamNetMessage(const CNetMsg_Cl_SetTeam *pMsg, int ClientId)
{
	if(m_World.m_Paused)
		return;

	CPlayer *pPlayer = m_apPlayers[ClientId];

	if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam + Server()->TickSpeed() * 3 > Server()->Tick()))
		return;

	if(pPlayer->m_TeamChangeTick > Server()->Tick())
	{
		pPlayer->m_LastSetTeam = Server()->Tick();
		int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick()) / Server()->TickSpeed();
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Time to wait before changing team: %02d:%02d", TimeLeft / 60, TimeLeft % 60);
		SendBroadcast(ClientId, aBuf, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
		return;
	}

	/* INFECTION MODIFICATION START ***************************************/
	if(pMsg->m_Team == TEAM_SPECTATORS && !m_pController->CanJoinTeam(TEAM_SPECTATORS, ClientId))
	{
		SendBroadcast_Localization(ClientId, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("You can't join the spectators right now"), NULL);
		return;
	}
	/* INFECTION MODIFICATION END *****************************************/

	m_pController->OnTeamChangeRequested(ClientId, pMsg->m_Team);
}

void CGameContext::OnIsDDNetLegacyNetMessage(const CNetMsg_Cl_IsDDNetLegacy *pMsg, int ClientId, CUnpacker *pUnpacker)
{
	IServer::CClientInfo Info;
	if(Server()->GetClientInfo(ClientId, &Info) && Info.m_GotDDNetVersion)
	{
		return;
	}
	int DDNetVersion = pUnpacker->GetInt();
	if(pUnpacker->Error() || DDNetVersion < 0)
	{
		DDNetVersion = VERSION_DDRACE;
	}
	Server()->SetClientDDNetVersion(ClientId, DDNetVersion);
	OnClientDDNetVersionKnown(ClientId);
}

void CGameContext::OnSetSpectatorModeNetMessage(const CNetMsg_Cl_SetSpectatorMode *pMsg, int ClientId)
{
	if(m_World.m_Paused)
		return;

	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(pPlayer->GetTeam() != TEAM_SPECTATORS || pPlayer->m_SpectatorId == pMsg->m_SpectatorId || ClientId == pMsg->m_SpectatorId ||
		(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode + Server()->TickSpeed() * 3 > Server()->Tick()))
		return;

	pPlayer->m_LastSetSpectatorMode = Server()->Tick();
	if(pMsg->m_SpectatorId != SPEC_FREEVIEW && (!m_apPlayers[pMsg->m_SpectatorId] || m_apPlayers[pMsg->m_SpectatorId]->GetTeam() == TEAM_SPECTATORS))
		SendChatTarget(ClientId, "Invalid spectator id used");
	else
		pPlayer->m_SpectatorId = pMsg->m_SpectatorId;
}

void CGameContext::OnChangeInfoNetMessage(const CNetMsg_Cl_ChangeInfo *pMsg, int ClientId)
{
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo + Server()->TickSpeed() * g_Config.m_SvInfoChangeDelay > Server()->Tick())
		return;

	if(!str_utf8_check(pMsg->m_pName) || !str_utf8_check(pMsg->m_pClan) || !str_utf8_check(pMsg->m_pSkin))
	{
		return;
	}
	pPlayer->m_LastChangeInfo = Server()->Tick();

	// set infos
	if(!pPlayer->m_ClientNameLocked && Server()->WouldClientNameChange(ClientId, pMsg->m_pName))
	{
		char aOldName[MAX_NAME_LENGTH];
		str_copy(aOldName, Server()->ClientName(ClientId), sizeof(aOldName));

		Server()->SetClientName(ClientId, pMsg->m_pName);

		SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} changed their name to {str:NewName}"), "PlayerName", aOldName, "NewName", Server()->ClientName(ClientId), NULL);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "change_name previous='%s' now='%s'", aOldName, Server()->ClientName(ClientId));
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}
	Server()->SetClientClan(ClientId, pMsg->m_pClan);
#ifndef CONF_FORCE_COUNTRY_BY_IP
	Server()->SetClientCountry(ClientId, pMsg->m_Country);
#endif
	Server()->ExpireServerInfo();
}

void CGameContext::OnEmoticonNetMessage(const CNetMsg_Cl_Emoticon *pMsg, int ClientId)
{
	if(m_World.m_Paused)
		return;

	CPlayer *pPlayer = m_apPlayers[ClientId];

	// Still apply a reasonable limit: 1-2 emotes per tick
	if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote &&
		pPlayer->m_LastEmote + maximum(Server()->TickSpeed() * g_Config.m_SvEmoticonDelay, g_Config.m_SvHighBandwidth ? 1 : 2) > Server()->Tick())
		return;

	CCharacter *pChr = pPlayer->GetCharacter();

	// player needs a character to send emotes
	if(!pChr)
		return;

	pPlayer->m_LastEmote = Server()->Tick();

	SendEmoticon(ClientId, pMsg->m_Emoticon);
}

void CGameContext::OnKillNetMessage(const CNetMsg_Cl_Kill *pMsg, int ClientId)
{
	if(m_World.m_Paused)
		return;

	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(pPlayer->m_LastKill && pPlayer->m_LastKill + Server()->TickSpeed() * 3 > Server()->Tick())
		return;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	pPlayer->m_LastKill = Server()->Tick();
	pPlayer->KillCharacter(WEAPON_SELF);
}

void CGameContext::OnStartInfoNetMessage(const CNetMsg_Cl_StartInfo *pMsg, int ClientId)
{
	CPlayer *pPlayer = m_apPlayers[ClientId];

	if(pPlayer->m_IsReady)
		return;

	if(!str_utf8_check(pMsg->m_pName))
	{
		Server()->Kick(ClientId, "name is not valid utf8");
		return;
	}
	if(!str_utf8_check(pMsg->m_pClan))
	{
		Server()->Kick(ClientId, "clan is not valid utf8");
		return;
	}
	if(!str_utf8_check(pMsg->m_pSkin))
	{
		Server()->Kick(ClientId, "skin is not valid utf8");
		return;
	}

	pPlayer->m_LastChangeInfo = Server()->Tick();

	// set start infos
	Server()->SetClientName(ClientId, pMsg->m_pName);
	// trying to set client name can delete the player object, check if it still exists
	if(!m_apPlayers[ClientId])
	{
		return;
	}
	Server()->SetClientClan(ClientId, pMsg->m_pClan);
	Server()->SetClientCountry(ClientId, pMsg->m_Country);

	/* INFECTION MODIFICATION START ***************************************/
	if(!Server()->GetClientMemory(ClientId, CLIENTMEMORY_LANGUAGESELECTION))
	{
#ifdef CONF_GEOLOCATION
		char aAddrStr[NETADDR_MAXSTRSIZE]{};
		Server()->GetClientAddr(ClientId, aAddrStr, sizeof(aAddrStr));
		std::string ip(aAddrStr);

		int LocatedCountry = Geolocation::get_country_iso_numeric_code(ip);
#ifdef CONF_FORCE_COUNTRY_BY_IP
		Server()->SetClientCountry(ClientId, LocatedCountry);
#endif // CONF_FORCE_COUNTRY_BY_IP
#else
		int LocatedCountry = -1;
#endif // CONF_GEOLOCATION

		const char *const pLangFromClient = CLocalization::LanguageCodeByCountryCode(pMsg->m_Country);
		const char *const pLangForIp = CLocalization::LanguageCodeByCountryCode(LocatedCountry);

		const char *const pDefaultLang = "en";
		const char *pLangForVote = "";

		if(pLangFromClient[0] && (str_comp(pLangFromClient, pDefaultLang) != 0))
			pLangForVote = pLangFromClient;
		else if(pLangForIp[0] && (str_comp(pLangForIp, pDefaultLang) != 0))
			pLangForVote = pLangForIp;

		dbg_msg("lang", "init_language ClientId=%d, lang from flag: \"%s\", lang for IP: \"%s\"", ClientId, pLangFromClient, pLangForIp);

		SetClientLanguage(ClientId, pDefaultLang);

		if(pLangForVote[0])
		{
			CNetMsg_Sv_VoteSet Msg;
			Msg.m_Timeout = 10;
			Msg.m_pReason = "";
			str_copy(m_VoteLanguage[ClientId], pLangForVote, sizeof(m_VoteLanguage[ClientId]));
			Msg.m_pDescription = Server()->Localization()->Localize(m_VoteLanguage[ClientId], _("Switch language to english?"));
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientId);
			m_VoteLanguageTick[ClientId] = 10 * Server()->TickSpeed();
		}
		else
		{
			SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("You can change the language of this mod using the command /language."), NULL);
			SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("If your language is not available, you can help with translation (/help translate)."), NULL);
		}

		Server()->SetClientMemory(ClientId, CLIENTMEMORY_LANGUAGESELECTION, true);
	}
	/* INFECTION MODIFICATION END *****************************************/

	// send clear vote options
	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientId);

	// begin sending vote options
	pPlayer->m_SendVoteIndex = 0;

	// send tuning parameters to client
	SendTuningParams(ClientId);

	// client is ready to enter
	pPlayer->m_IsReady = true;
	CNetMsg_Sv_ReadyToEnter m;
	Server()->SendPackMsg(&m, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientId);

	Server()->ExpireServerInfo();
}

void CGameContext::ConConverse(IConsole::IResult *pResult, void *pUserData)
{
	// This will never be called
}

void CGameContext::ConShowOthers(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;
	if(pSelf->Server()->GetAuthedState(pResult->m_ClientId))
	{
		if(pResult->NumArguments())
			pPlayer->m_ShowOthers = pResult->GetInteger(0);
		else
			pPlayer->m_ShowOthers = !pPlayer->m_ShowOthers;
	}
	else
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"Custom 'show others' is disabled");
}

void CGameContext::ConShowAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	CPlayer *pPlayer = pSelf->m_apPlayers[pResult->m_ClientId];
	if(!pPlayer)
		return;

	if(pSelf->Server()->GetAuthedState(pResult->m_ClientId) == 0)
	{
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"Custom 'show all' is disabled");
	}

	if(pResult->NumArguments())
	{
		if(pPlayer->m_ShowAll == (bool)pResult->GetInteger(0))
			return;

		pPlayer->m_ShowAll = pResult->GetInteger(0);
	}
	else
	{
		pPlayer->m_ShowAll = !pPlayer->m_ShowAll;
	}

	if(pPlayer->m_ShowAll)
		pSelf->SendChatTarget(pResult->m_ClientId, "You will now see all tees on this server, no matter the distance");
	else
		pSelf->SendChatTarget(pResult->m_ClientId, "You will no longer see all tees on this server");
}

void CGameContext::ConTimeout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	int ClientId = pResult->m_ClientId;
	CPlayer *pPlayer = pSelf->m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	const char *pTimeout = pResult->NumArguments() > 0 ? pResult->GetString(0) : pPlayer->m_aTimeoutCode;
	const char *pClientName = pSelf->Server()->ClientName(ClientId);
	char aAddress[NETADDR_MAXSTRSIZE];
	pSelf->Server()->GetClientAddr(ClientId, &aAddress[0], sizeof(aAddress));

	dbg_msg("timeout", "Used with code %s by (#%02i) '%s' (id=%s)", pTimeout, ClientId, pClientName, aAddress);

	if(!pSelf->Server()->IsSixup(ClientId))
	{
		for(int i = 0; i < pSelf->Server()->MaxClients(); i++)
		{
			if(i == pResult->m_ClientId)
				continue;
			if(!pSelf->m_apPlayers[i])
				continue;
			if(str_comp(pSelf->m_apPlayers[i]->m_aTimeoutCode, pTimeout))
				continue;
			if(pSelf->Server()->SetTimedOut(i, pResult->m_ClientId))
			{
				return;
			}
		}
	}
	else
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"Your timeout code has been set. 0.7 clients can not reclaim their tees on timeout; however, a 0.6 client can claim your tee ");
	}

	pSelf->Server()->SetTimeoutProtected(pResult->m_ClientId);
	str_copy(pPlayer->m_aTimeoutCode, pResult->GetString(0), sizeof(pPlayer->m_aTimeoutCode));
}

void CGameContext::ConMe(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!CheckClientId(pResult->m_ClientId))
		return;

	char aBuf[256 + 24];

	str_format(aBuf, 256 + 24, "'%s' %s",
		pSelf->Server()->ClientName(pResult->m_ClientId),
		pResult->GetString(0));
	if(g_Config.m_SvSlashMe)
		pSelf->SendChat(-2, CGameContext::CHAT_ALL, aBuf, pResult->m_ClientId);
	else
		pSelf->Console()->Print(
			IConsole::OUTPUT_LEVEL_STANDARD,
			"chatresp",
			"/me is disabled on this server");
}

void CGameContext::ConWhisper(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pThis = (CGameContext *)pUserData;

	const char *pStrClientId = pResult->GetString(0);
	const char *pText = pResult->GetString(1);

	if(!str_isallnum(pStrClientId))
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		return;
	}

	int ToClientId = str_toint(pStrClientId);
	const CPlayer *pPlayer = pThis->GetPlayer(ToClientId);
	if(!pPlayer)
	{
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Invalid client id");
		return;
	}

	pThis->SendChatTarget(ToClientId, pText);

	// Confirm message sent
	char aBuf[1024];
	str_format(aBuf, sizeof(aBuf), "Whisper '%s' sent to %s",
		pText,
		pThis->Server()->ClientName(ToClientId));
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		//~ pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConToggleTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float OldValue;

	char aBuf[256];
	if(!pSelf->Tuning()->Get(pParamName, &OldValue))
	{
		str_format(aBuf, sizeof(aBuf), "No such tuning parameter: %s", pParamName);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		return;
	}

	float NewValue = fabs(OldValue - pResult->GetFloat(1)) < 0.0001f ? pResult->GetFloat(2) : pResult->GetFloat(1);

	pSelf->Tuning()->Set(pParamName, NewValue);
	pSelf->Tuning()->Get(pParamName, &NewValue);

	str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	pSelf->SendTuningParams(-1);
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	//~ pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < CTuningParams::Num(); i++)
	{
		float Value;
		pSelf->Tuning()->Get(i, &Value);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", CTuningParams::Name(i), Value);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SetPaused(!pSelf->IsPaused());
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConSkipMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->SkipMap();
}

void CGameContext::ConQueueMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	const char *pMapName = pResult->GetString(0);

	char aBuf[256];
	if(pSelf->MapExists(pMapName))
	{
		str_format(aBuf, sizeof(aBuf), "Map '%s' will be the next map", pMapName);
		pSelf->m_pController->QueueMap(pResult->GetString(0));
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "Unable to find map '%s'", pMapName);
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConAddMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments() != 1)
		return;

	const char *pMapName = pResult->GetString(0);
	if(!str_utf8_check(pMapName))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid (non UTF-8) filename");
		return;
	}

	{
		const char *pMapInList = pSelf->Config()->m_SvMaprotation;
		const int Length = str_length(pMapName);
		while(pMapInList)
		{
			pMapInList = str_find(pMapInList, pMapName);

			if(pMapInList)
			{
				pMapInList += Length;
				const char nextC = pMapInList[0];
				if((nextC == 0) || IGameController::IsWordSeparator(nextC))
				{
					pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "The map is already in the rotation list");
					return;
				}
			}
		}
	}

	char aBuf[256];
	if(!pSelf->MapExists(pMapName))
	{
		str_format(aBuf, sizeof(aBuf), "Unable to find map %s", pMapName);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

		return;
	}

	char *pData = g_Config.m_SvMaprotation;
	int MaxSize = sizeof(g_Config.m_SvMaprotation);
	int i = 0;
	for(i = 0; i < MaxSize; ++i)
	{
		if(pData[i] == 0)
			break;
	}
	if(i + 1 + str_length(pMapName) >= MaxSize)
	{
		// Overflow
		return;
	}
	pData[i] = ' ';
	++i;
	str_copy(pData + i, pMapName, MaxSize - i);

	{
		str_format(aBuf, sizeof(aBuf), "Map %s added to the rotation list", pMapName);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

	if(pSelf->m_pController)
	{
		pSelf->m_pController->OnMapAdded(pMapName);
	}
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(pResult->GetInteger(0));
	else
		pSelf->m_pController->StartRound();
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	char aBuf[1024];
	str_copy(aBuf, pResult->GetString(0), sizeof(aBuf));

	int i, j;
	for(i = 0, j = 0; aBuf[i]; i++, j++)
	{
		if(aBuf[i] == '\\' && aBuf[i + 1] == 'n')
		{
			aBuf[j] = '\n';
			i++;
		}
		else if(i != j)
		{
			aBuf[j] = aBuf[i];
		}
	}
	aBuf[j] = '\0';

	pSelf->SendBroadcast(-1, aBuf, BROADCAST_PRIORITY_SERVERANNOUNCE, pSelf->Server()->TickSpeed() * 3);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS - 1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments() > 2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientId])
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientId, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientId]->m_TeamChangeTick = pSelf->Server()->Tick() + pSelf->Server()->TickSpeed() * Delay * 60;
	pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[ClientId], Team);
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChatTarget(-1, aBuf);

	for(auto &pPlayer : pSelf->m_apPlayers)
		if(pPlayer)
			pSelf->m_pController->DoTeamChange(pPlayer, Team, false);
}

void CGameContext::ConInsertVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Index = pResult->GetInteger(0);
	const char *pDescription = pResult->GetString(1);
	const char *pCommand = pResult->GetString(2);

	pSelf->InsertVote(Index, pDescription, pCommand);
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	pSelf->AddVote(pDescription, pCommand);
}

bool CGameContext::InsertVote(int Position, const char *pDescription, const char *pCommand)
{
	if((Position < 0) || (Position > m_NumVoteOptions))
		Position = m_NumVoteOptions;

	if(m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return false;
	}

	// check for valid option
	if(!Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return false;
	}
	while(*pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return false;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return false;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len, alignof(CVoteOptionServer));
	if(Position == m_NumVoteOptions)
	{
		// Append
		pOption->m_pNext = 0;
		pOption->m_pPrev = m_pVoteOptionLast;
		if(pOption->m_pPrev)
			pOption->m_pPrev->m_pNext = pOption;
		m_pVoteOptionLast = pOption;
	}
	else
	{
		// Insert
		pOption->m_pPrev = nullptr;
		pOption->m_pNext = m_pVoteOptionFirst;
		if(Position == 0)
		{
			m_pVoteOptionFirst = pOption;
		}
		else
		{
			int CurrentPos = 1;
			CVoteOptionServer *pPrevOption = m_pVoteOptionFirst;
			while (CurrentPos < Position)
			{
				pPrevOption = pPrevOption->m_pNext;
				++CurrentPos;
			}

			pOption->m_pPrev = pPrevOption;
			pOption->m_pNext = pPrevOption->m_pNext;

			pOption->m_pPrev->m_pNext = pOption;
			pOption->m_pNext->m_pPrev = pOption;
		}
	}

	if(!m_pVoteOptionFirst)
		m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len + 1);
	++m_NumVoteOptions;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	if(pOption->m_pNext)
	{
		// Inserted

		CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
		Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);

		// reset sending of vote options
		for(auto &pPlayer : m_apPlayers)
		{
			if(pPlayer)
				pPlayer->m_SendVoteIndex = 0;
		}
	}

	return true;
}

void CGameContext::AddVote(const char *pDescription, const char *pCommand)
{
	InsertVote(m_NumVoteOptions, pDescription, pCommand);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	pSelf->RemoveVote(pDescription);
}

void CGameContext::RemoveVote(const char *pVoteOption)
{
	// check for valid option
	CVoteOptionServer *pOption = m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pVoteOption, pOption->m_aDescription) == 0)
			break;
		if(str_comp_nocase(pVoteOption, pOption->m_aCommand) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pVoteOption);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// start reloading vote option list
	// clear vote options
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);

	// reset sending of vote options
	for(auto &pPlayer : m_apPlayers)
	{
		if(pPlayer)
			pPlayer->m_SendVoteIndex = 0;
	}

	// TODO: improve this
	// remove the option
	--m_NumVoteOptions;

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len + 1);
	}

	// clean up
	delete m_pVoteOptionHeap;
	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ClearVotes()
{
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	m_pVoteOptionHeap->Reset();
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;

	// reset sending of vote options
	for(auto &pPlayer : m_apPlayers)
	{
		if(pPlayer)
			pPlayer->m_SendVoteIndex = 0;
	}
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "authorized player forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickId = str_toint(pValue);
		if(KickId < 0 || KickId >= MAX_CLIENTS || !pSelf->m_apPlayers[KickId])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if(!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickId, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickId, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateId = str_toint(pValue);
		if(SpectateId < 0 || SpectateId >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateId] || pSelf->m_apPlayers[SpectateId]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}

		str_format(aBuf, sizeof(aBuf), "'%s' was moved to spectator (%s)", pSelf->Server()->ClientName(SpectateId), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateId, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf);
	}
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ClearVotes();
}

struct CMapNameItem
{
	char m_aName[IO_MAX_PATH_LENGTH - 4];

	bool operator<(const CMapNameItem &Other) const { return str_comp_nocase(m_aName, Other.m_aName) < 0; }
};

void CGameContext::ConAddMapVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	std::vector<CMapNameItem> vMapList;
	pSelf->Storage()->ListDirectory(IStorage::TYPE_ALL, "maps", MapScan, &vMapList);
	std::sort(vMapList.begin(), vMapList.end());

	for(auto &Item : vMapList)
	{
		char aDescription[64];
		str_format(aDescription, sizeof(aDescription), "Map: %s", Item.m_aName);

		char aCommand[IO_MAX_PATH_LENGTH * 2 + 10];
		char aMapEscaped[IO_MAX_PATH_LENGTH * 2];
		char *pDst = aMapEscaped;
		str_escape(&pDst, Item.m_aName, aMapEscaped + sizeof(aMapEscaped));
		str_format(aCommand, sizeof(aCommand), "change_map \"%s\"", aMapEscaped);

		pSelf->AddVote(aDescription, aCommand);
	}

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "added maps to votes");
}

int CGameContext::MapScan(const char *pName, int IsDir, int DirType, void *pUserData)
{
	if(IsDir || !str_endswith(pName, ".map"))
		return 0;

	CMapNameItem Item;
	str_truncate(Item.m_aName, sizeof(Item.m_aName), pName, str_length(pName) - str_length(".map"));
	static_cast<std::vector<CMapNameItem> *>(pUserData)->push_back(Item);

	return 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "admin forced vote %s", pResult->GetString(0));
	pSelf->SendChatTarget(-1, aBuf);
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::ConchainSyncMapRotation(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);

	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *)pUserData;
		if(pSelf->m_pController)
		{
			pSelf->m_pController->SyncSmartMapRotationData();
		}
	}
}

/* INFECTION MODIFICATION START ***************************************/

void CGameContext::ConVersion(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"InfectionClass Mod. Version: " GAME_VERSION);

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
		"Compiled: " LAST_COMPILE_DATE);

	if(GIT_SHORTREV_HASH)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Git revision hash: %s", GIT_SHORTREV_HASH);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
	}
}

void CGameContext::ConCredits(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int ClientId = pResult->GetClientId();
	const char* pLanguage = pSelf->m_apPlayers[ClientId]->GetLanguage();

	dynamic_string Buffer;

	const char aThanks[] = "guenstig werben, Defeater, Orangus, BlinderHeld, Warpaint, Serena, FakeDeath, tee_to_F_U_UP!, Denis, NanoSlime_, tria, pinkieval…";
	const char aContributors[] = "necropotame, Stitch626, yavl, Socialdarwinist"
	                             ", bretonium, duralakun, FluffyTee, ResamVi"
	                             ", Kaffeine"
	                             ;

	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass, by necropotame (version {str:VersionCode})"), "VersionCode", "InfectionDust", NULL);
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Based on the concept of Infection mod by Gravity"), NULL);
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Main contributors: {str:ListOfContributors}"), "ListOfContributors", aContributors, NULL);
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Thanks to {str:ListOfContributors}"), "ListOfContributors", aThanks, NULL);
	Buffer.append("\n\n");
	pSelf->SendMOTD(ClientId, Buffer.buffer());
}

void CGameContext::ConInfo(IConsole::IResult *pResult, void *pUserData)
{
	ConAbout(pResult, pUserData);
}

void CGameContext::ConAbout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ConAbout(pResult);
}

void CGameContext::ConAbout(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetClientId();
	const char* pLanguage = m_apPlayers[ClientId]->GetLanguage();

	dynamic_string Buffer;
	Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass, by necropotame (version {str:VersionCode})"), "VersionCode", GAME_VERSION, NULL);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
	Buffer.clear();

	Server()->Localization()->Format_L(Buffer, pLanguage, _("Server version from {str:ServerCompileDate} "), "ServerCompileDate", LAST_COMPILE_DATE, NULL);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
	Buffer.clear();

	if(GIT_SHORTREV_HASH)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Git revision hash: %s", GIT_SHORTREV_HASH);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", aBuf);
	}

	const char *pSourceUrl = Config()->m_AboutSourceUrl;
	if(pSourceUrl[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Sources: {str:SourceUrl} "), "SourceUrl",
			pSourceUrl, NULL
		);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
		Buffer.clear();
	}

	if(Config()->m_AboutContactsDiscord[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Discord: {str:Url}"), "Url",
			Config()->m_AboutContactsDiscord, nullptr);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
		Buffer.clear();
	}
	if(Config()->m_AboutContactsTelegram[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Telegram: {str:Url}"), "Url",
			Config()->m_AboutContactsTelegram, nullptr);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
		Buffer.clear();
	}
	if(Config()->m_AboutContactsMatrix[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Matrix room: {str:Url}"), "Url",
			Config()->m_AboutContactsMatrix, nullptr);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
		Buffer.clear();
	}
	if(Config()->m_AboutTranslationUrl[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Translation project: {str:Url}"), "Url",
			Config()->m_AboutTranslationUrl, nullptr);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
		Buffer.clear();
	}

	Server()->Localization()->Format_L(Buffer, pLanguage, _("See also: /credits"), nullptr);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
	Buffer.clear();
}

void CGameContext::PrivateMessage(const char* pStr, int ClientId, bool TeamChat)
{
	if(ProcessSpamProtection(ClientId))
		return;

	bool ArgumentFound = false;
	const char* pArgumentIter = pStr;
	while(*pArgumentIter)
	{
		if(*pArgumentIter != ' ')
		{
			ArgumentFound = true;
			break;
		}
		
		pArgumentIter++;
	}
	
	if(!ArgumentFound)
	{
		SendChatTarget(ClientId, "Usage: /msg <username or group> <message>");
		SendChatTarget(ClientId, "Send a private message to a player or a group of players");
		SendChatTarget(ClientId, "Available groups: !near, !engineer, !soldier, ...");
		return;
	}
	
	dynamic_string FinalMessage;
	int TextIter = 0;
	
	
	bool CheckDistance = false;
	vec2 CheckDistancePos = vec2(0.0f, 0.0f);
	
	int CheckTeam = -1;
	EPlayerClass CheckClass = EPlayerClass::Invalid;
#ifdef CONF_SQL
	int CheckLevel = SQL_USERLEVEL_NORMAL;
#endif
	
	if(TeamChat && m_apPlayers[ClientId])
	{
		CheckTeam = true;
		if(m_apPlayers[ClientId]->GetTeam() == TEAM_SPECTATORS)
			CheckTeam = TEAM_SPECTATORS;
		if(m_apPlayers[ClientId]->IsInfected())
			CheckTeam = TEAM_RED;
		else
			CheckTeam = TEAM_BLUE;
	}
	
	char aNameFound[32];
	aNameFound[0] = 0;
	
	char aChatTitle[32];
	aChatTitle[0] = 0;
	unsigned int c = 0;
	for(; c<sizeof(aNameFound)-1; c++)
	{
		if(pStr[c] == ' ' || pStr[c] == 0)
		{
			if(str_comp(aNameFound, "!near") == 0)
			{
				if(m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
				{
					CheckDistance = true;
					CheckDistancePos = m_apPlayers[ClientId]->GetCharacter()->m_Pos;
					str_copy(aChatTitle, "near", sizeof(aChatTitle));
				}
			}
#ifdef CONF_SQL
			else if(str_comp(aNameFound, "!mod") == 0)
			{
				if(m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
				{
					CheckLevel = SQL_USERLEVEL_MOD;
					CheckDistancePos = m_apPlayers[ClientId]->GetCharacter()->m_Pos;
					str_copy(aChatTitle, "moderators", sizeof(aChatTitle));
				}
			}
#endif
			else if(str_comp(aNameFound, "!engineer") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Engineer;
				str_copy(aChatTitle, "engineer", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!soldier ") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Soldier;
				str_copy(aChatTitle, "soldier", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!scientist") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Scientist;
				str_copy(aChatTitle, "scientist", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!biologist") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Biologist;
				str_copy(aChatTitle, "biologist", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!looper") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Looper;
				str_copy(aChatTitle, "looper", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!medic") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Medic;
				str_copy(aChatTitle, "medic", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!hero") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Hero;
				str_copy(aChatTitle, "hero", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ninja") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Ninja;
				str_copy(aChatTitle, "ninja", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!mercenary") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Mercenary;
				str_copy(aChatTitle, "mercenary", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!sniper") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Sniper;
				str_copy(aChatTitle, "sniper", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!smoker") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Smoker;
				str_copy(aChatTitle, "smoker", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!hunter") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Hunter;
				str_copy(aChatTitle, "hunter", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!bat") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Bat;
				str_copy(aChatTitle, "bat", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!boomer") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Boomer;
				str_copy(aChatTitle, "boomer", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!spider") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Spider;
				str_copy(aChatTitle, "spider", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ghost") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Ghost;
				str_copy(aChatTitle, "ghost", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ghoul") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Ghoul;
				str_copy(aChatTitle, "ghoul", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!slug") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Slug;
				str_copy(aChatTitle, "slug", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!undead") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Undead;
				str_copy(aChatTitle, "undead", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!witch") == 0 && m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetCharacter())
			{
				CheckClass = EPlayerClass::Witch;
				str_copy(aChatTitle, "witch", sizeof(aChatTitle));
			}
			else
			{
				for(int i=0; i<MAX_CLIENTS; i++)
				{
					if(m_apPlayers[i] && str_comp(Server()->ClientName(i), aNameFound) == 0)
					{
						const char *pMessage = pStr[c] == 0 ? &pStr[c] : &pStr[c + 1];
						WhisperId(ClientId, i, pMessage);
						return;
					}
				}
			}
		}
		
		if(aChatTitle[0] || pStr[c] == 0)
		{
			aNameFound[c] = 0;
			break;
		}
		else
		{
			aNameFound[c] = pStr[c];
			aNameFound[c+1] = 0;
		}
	}
		
	if(!aChatTitle[0])
	{
		SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("No player was found with this name"));
		return;
	}
	
	pStr += c;
	while(*pStr == ' ')
		pStr++;
	
	dynamic_string Buffer;
	Buffer.copy(pStr);
	Server()->Localization()->ArabicShaping(Buffer);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = (TeamChat ? 1 : 0);
	Msg.m_ClientId = ClientId;
	
	int NumPlayerFound = 0;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && !CGameContext::m_ClientMuted[i][ClientId])
		{
			if(i != ClientId)
			{
				if(CheckTeam >= 0)
				{
					if(CheckTeam == TEAM_SPECTATORS && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
						continue;
					else if(CheckTeam == TEAM_RED && m_apPlayers[i]->IsHuman())
						continue;
					else if(CheckTeam == TEAM_BLUE && m_apPlayers[i]->IsInfected())
						continue;
				}
				
#ifdef CONF_SQL
				if(Server()->GetUserLevel(i) < CheckLevel)
					continue;
#endif
				
				if((CheckClass != EPlayerClass::Invalid) && !(m_apPlayers[i]->GetClass() == CheckClass))
					continue;
				
				if(CheckDistance && !(m_apPlayers[i]->GetCharacter() && distance(m_apPlayers[i]->GetCharacter()->m_Pos, CheckDistancePos) < 1000.0f))
					continue;
			}
			
			FinalMessage.clear();
			TextIter = 0;
			if(i == ClientId)
			{
				if(str_comp(aChatTitle, "private") == 0)
				{
					TextIter = FinalMessage.append_at(TextIter, aNameFound);
					TextIter = FinalMessage.append_at(TextIter, " (");
					TextIter = FinalMessage.append_at(TextIter, aChatTitle);
					TextIter = FinalMessage.append_at(TextIter, "): ");
				}
				else
				{
					TextIter = FinalMessage.append_at(TextIter, "(");
					TextIter = FinalMessage.append_at(TextIter, aChatTitle);
					TextIter = FinalMessage.append_at(TextIter, "): ");
				}
				TextIter = FinalMessage.append_at(TextIter, Buffer.buffer());
			}
			else
			{
				TextIter = FinalMessage.append_at(TextIter, Server()->ClientName(i));
				TextIter = FinalMessage.append_at(TextIter, " (");
				TextIter = FinalMessage.append_at(TextIter, aChatTitle);
				TextIter = FinalMessage.append_at(TextIter, "): ");
				TextIter = FinalMessage.append_at(TextIter, Buffer.buffer());
			}
			Msg.m_pMessage = FinalMessage.buffer();
	
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
				
			NumPlayerFound++;
		}
	}
}

void CGameContext::MutePlayer(const char* pStr, int ClientId)
{
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && str_comp(Server()->ClientName(i), pStr) == 0)
		{
			CGameContext::m_ClientMuted[ClientId][i] = !CGameContext::m_ClientMuted[ClientId][i];
			
			if(CGameContext::m_ClientMuted[ClientId][i])
				SendChatTarget(ClientId, "Player muted. Mute will persist until you or the muted player disconnects.");
			else
				SendChatTarget(ClientId, "Player unmuted. You can see their messages again.");
			break;
		}
	}
}

void CGameContext::InitGeolocation()
{
#ifdef CONF_GEOLOCATION
	const char aGeoDBFileName[] = "geo/GeoLite2-Country.mmdb";
	char aBuf[512];
	Storage()->GetDataPath(aGeoDBFileName, aBuf, sizeof(aBuf));
	if(aBuf[0])
	{
		Geolocation::Initialize(aBuf);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "Unable to find geolocation data file %s", aGeoDBFileName);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
#endif
}

void CGameContext::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	const char *pLogin = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);
	const char *pEmail = 0;
	
	if(pResult->NumArguments()>2)
		pEmail = pResult->GetString(2);
	
	pSelf->Server()->Register(ClientId, pLogin, pPassword, pEmail);
}

void CGameContext::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	const char *pLogin = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);
	pSelf->Server()->Login(ClientId, pLogin, pPassword);
}

void CGameContext::ConLogout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	pSelf->Server()->Logout(ClientId);
}

#ifdef CONF_SQL

void CGameContext::ConSetEmail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	const char *pEmail = pResult->GetString(0);
	
	pSelf->Server()->SetEmail(ClientId, pEmail);
}

void CGameContext::ConChallenge(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	pSelf->Server()->ShowChallenge(ClientId);
}

void CGameContext::ConTop10(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "looper") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_LOOPER_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_HERO_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_SLUG_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_WITCH_SCORE);
	}
	else
		pSelf->Server()->ShowTop10(ClientId, SQL_SCORETYPE_ROUND_SCORE);
}

void CGameContext::ConRank(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "looper") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_LOOPER_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_HERO_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_SLUG_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_WITCH_SCORE);
	
	}
	else
		pSelf->Server()->ShowRank(ClientId, SQL_SCORETYPE_ROUND_SCORE);
}

void CGameContext::ConGoal(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "looper") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_LOOPER_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_HERO_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_SLUG_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_WITCH_SCORE);
	}
	else
		pSelf->Server()->ShowGoal(ClientId, SQL_SCORETYPE_ROUND_SCORE);
}

void CGameContext::ConStats(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	
	if(pResult->NumArguments()>0)
	{
		int arg = pResult->GetInteger(0);
		pSelf->Server()->ShowStats(ClientId, arg);
	}
	else
		pSelf->Server()->ShowStats(ClientId, -1);
}

#endif

void CGameContext::ConHelp(IConsole::IResult *pResult, void *pUserData)
{
	int ClientId = pResult->GetClientId();
	const char *pHelpPage = (pResult->NumArguments()>0) ? pResult->GetString(0) : nullptr;

	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ChatHelp(ClientId, pHelpPage);
}

void CGameContext::ChatHelp(int ClientId, const char *pHelpPage)
{
	const char *pLanguage = m_apPlayers[ClientId]->GetLanguage();

	dynamic_string Buffer;

	if(!pHelpPage || str_comp_nocase(pHelpPage, "game") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Rules of the game"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass is a team game between humans and the infected."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("All players start as a human."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("10 seconds later, a few players become infected."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("The goal for the humans is to survive until the army cleans the map."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("The goal for the infected is to infect all humans."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("See also `/help pages`"), NULL);
	}
	else if(str_comp_nocase(pHelpPage, "translate") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("How to translate the mod"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Create an account on Crowdin and join the translation team:"), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, Config()->m_AboutTranslationUrl, NULL);
	}
	else if(str_comp_nocase(pHelpPage, "whitehole") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("White hole"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _C("White hole", "White hole pulls the infected into its center."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_LP(Buffer, pLanguage, g_Config.m_InfWhiteHoleMinimalKills,
			_CP("White hole",
				"Receive it by killing at least one infected as a Scientist.",
				"Receive it by killing at least {int:NumKills} of the infected as a Scientist."),
				"NumKills", &g_Config.m_InfWhiteHoleMinimalKills, NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _C("White hole", "Use the laser rifle to place it."), NULL);
	}
	else if(str_comp_nocase(pHelpPage, "msg") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Targeted chat messages"));
		Buffer.append(" ~~\n\n");
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/w <PlayerName> <My Message>” to send a private message to this player."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg !<ClassName> <My Message>” to send a private message to all players with a specific class."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Example: “/msg !medic I'm wounded!”"), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg !near” to send a private message to all players near you."), NULL);
	}
	else if(str_comp_nocase(pHelpPage, "mute") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Persistent player mute"));
		Buffer.append(" ~~\n\n");
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/mute <PlayerName>” to mute this player."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Unlike a client mute this will persist between map changes and wears off when either you or the muted player disconnects."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Example: “/mute nameless tee”"), NULL);
		Buffer.append("\n\n");
	}
	else if(str_comp_nocase(pHelpPage, "taxi") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("How to use taxi mode"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Two or more humans can form a taxi."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("In order to use it, both humans have to disable hook protection (usually, with F3). The human being hooked becomes the driver."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("To get off the taxi, jump. To drop off your passengers, enable hook protection (usually, with F3)."), NULL);
	}
	else if(str_comp_nocase(pHelpPage, "fast_round") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Fast round"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage,
			_("In the fast rounds *more* humans become infected initially, "
			  "the spawning rate is increased and the round time limit is decreased. "
			  "White hole is also disabled."), NULL);
	}
	else
	{
		bool Ok = true;
		EPlayerClass PlayerClass = CInfClassGameController::GetClassByName(pHelpPage, &Ok);
		if(Ok)
		{
			WriteClassHelpPage(&Buffer, pLanguage, PlayerClass);
		}
	}

	if(Buffer.empty())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Server()->Localization()->Localize(pLanguage, _("Choose a help page with /help <page>")));
		
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Available help pages: {str:PageList}"),
			"PageList", "game, translate, msg, mute, taxi",
			NULL
		);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", Buffer.buffer());
		
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "engineer, soldier, scientist, biologist, looper, medic, hero, ninja, mercenary, sniper, whitehole");
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp", "smoker, hunter, bat, boomer, ghost, spider, ghoul, slug, voodoo, undead, witch.");
	}
	else
	{
		SendMOTD(ClientId, Buffer.buffer());
	}
}

bool CGameContext::WriteClassHelpPage(dynamic_string *pOutput, const char *pLanguage, EPlayerClass PlayerClass)
{
	dynamic_string &Buffer = *pOutput;

	auto MakeHeader = [this, &Buffer, pLanguage](const char *pText) {
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, pText, nullptr);
		Buffer.append(" ~~");
	};

	auto AddText = [this, &Buffer, pLanguage](const char *pSeparator, const char *pText, const char *pArgName = nullptr, const void *pArgValue = nullptr) {
		Buffer.append(pSeparator);

		if(pArgName && pArgValue)
		{
			Server()->Localization()->Format_L(Buffer, pLanguage, pText, pArgName, pArgValue, nullptr);
		}
		else
		{
			Server()->Localization()->Format_L(Buffer, pLanguage, pText, nullptr);
		}
	};

	auto AddText_Plural = [this, &Buffer, pLanguage](const char *pSeparator, int Number, const char *pText, const char *pArgName, const void *pArgValue) {
		Buffer.append(pSeparator);

		Server()->Localization()->Format_LP(Buffer, pLanguage, Number, pText, pArgName, pArgValue, nullptr);
	};

	auto AddLine = [AddText](const char *pText, const char *pArgName = nullptr, const void *pArgValue = nullptr) {
		AddText("\n\n", pText, pArgName, pArgValue);
	};

	auto AddLine_Plural = [AddText_Plural](int Number, const char *pText, const char *pArgName, const void *pArgValue) {
		AddText_Plural("\n\n", Number, pText, pArgName, pArgValue);
	};

	auto ConLine = [AddText](const char *pText, const char *pArgName = nullptr, const void *pArgValue = nullptr) {
		AddText(" ", pText, pArgName, pArgValue);
	};

	static const int HeroNumArmorGift = 4;

	MakeHeader(CInfClassGameController::GetClassDisplayName(PlayerClass));

	switch(PlayerClass)
	{
	case EPlayerClass::Invalid:
	case EPlayerClass::None:
	case EPlayerClass::Count:
		return false;

	case EPlayerClass::Mercenary:
		AddLine(_C("Mercenary", "The Mercenary can fly in the air using their machine gun."));
		AddLine(_C("Mercenary", "They can also create a powerful bomb with their hammer that can"
								" be charged by hitting it or with a laser rifle."));
		AddLine_Plural(g_Config.m_InfPoisonDamage,
					_CP("Mercenary",
						   "Mercenary can also throw poison grenades that deal one damage point and prevent the infected from healing.",
						   "Mercenary can also throw poison grenades that deal {int:NumDamagePoints} damage points and prevent the infected from healing."),
					   "NumDamagePoints", &g_Config.m_InfPoisonDamage);
		break;
	case EPlayerClass::Medic:
		AddLine(_C("Medic", "The Medic can protect humans with the hammer by giving them armor."));
		AddLine(_C("Medic", "Grenades with medicine give armor to everybody in their range,"
							" including Heroes and the Medic themself."));
		AddLine(_C("Medic", "Laser rifle revives the infected, but at the cost of 17 hp and armor."));
		AddLine(_C("Medic", "Medic also has a powerful shotgun that can knock back the infected."));
		break;
	case EPlayerClass::Hero:
		AddLine(_C("Hero", "The Hero has all standard weapons."));
		AddLine(_C("Hero", "The Hero has to find a flag only visible to them. Stand still to be pointed towards it."));
		ConLine(_C("Hero", "The flag gifts a health point, {int:NumArmorGift} armor and full ammo to all humans."),
				"NumArmorGift", &HeroNumArmorGift);
		ConLine(_C("Hero", "It fully heals the Hero and it can grant a turret which you can place down with the hammer."));
		ConLine(_C("Hero", "The gift to all humans is only applied when the flag is surrounded by hearts and armor."));
		ConLine(_C("Hero", "The Hero cannot be healed by a Medic, but it can withstand a hit from an infected."));
		break;
	case EPlayerClass::Engineer:
		AddLine(_C("Engineer", "The Engineer can build walls with the hammer to block the infected."));
		AddLine(_C("Engineer", "When an infected touches the wall, it dies."));
		AddLine(_C("Engineer", "The lifespan of a wall is {sec:LifeSpan}, and walls are limited to"
							   " one per player at the same time."),
				"LifeSpan", &g_Config.m_InfBarrierLifeSpan);
		break;
	case EPlayerClass::Soldier:
		AddLine(_C("Soldier", "The Soldier creates floating bombs with the hammer."));
		AddLine_Plural(g_Config.m_InfSoldierBombs,
					   _CP("Soldier",
						   "Each bomb can explode one time.",
						   "Each bomb can explode {int:NumBombs} times."),
					   "NumBombs", &g_Config.m_InfSoldierBombs);

		AddLine(_("Use the hammer to place the bomb and explode it multiple times."));
		break;
	case EPlayerClass::Ninja:
		AddLine(_C("Ninja", "The Ninja can throw flash grenades that can freeze the infected for"
							" three seconds."));
		AddLine_Plural(
					g_Config.m_InfNinjaJump,
					_CP("Ninja",
						"Their hammer is replaced with a katana, allowing them to dash {int:NinjaJump}"
						" time before touching the ground.",
						"Their hammer is replaced with a katana, allowing them to dash {int:NinjaJump}"
						" times before touching the ground."),
					"NinjaJump", &g_Config.m_InfNinjaJump);
		AddLine(_("They also have a laser rifle that blinds the target for a short period of time."));
		AddLine(_("Ninja gets special targets. For killing a target, extra points and abilities"
				  " are awarded."));
		break;
	case EPlayerClass::Sniper:
		AddLine(_C("Sniper", "The Sniper can lock the position in mid-air for 15 seconds with the"
							 " hammer."));
		AddLine(_C("Sniper", "The locked position increases the Sniper's rifle damage from usual"
							 " 10-13 to 30 damage points."));
		AddLine(_C("Sniper", "They can also jump two times in the air."));
		break;
	case EPlayerClass::Scientist:
		AddLine(_C("Scientist", "The Scientist can pose floating mines with the hammer."));
		AddLine_Plural(g_Config.m_InfMineLimit,
					   _CP("Scientist",
						   "Mines are limited to one per player at the same time.",
						   "Mines are limited to {int:NumMines} per player at the same time."),
					   "NumMines", &g_Config.m_InfMineLimit);
		AddLine(_C("Scientist", "Scientist has also grenades that teleport them."));
		AddLine(_C("Scientist", "A lucky Scientist devoted to killing can get a white hole that"
								" sucks the infected in which can be placed with the laser rifle."));
		break;
	case EPlayerClass::Biologist:
		AddLine(_C("Biologist", "The Biologist has a shotgun with bouncing bullets and can create a"
								" spring laser trap by shooting with the laser rifle."));
		break;
	case EPlayerClass::Looper:
		AddLine(_C("Looper", "The Looper has a laser wall that slows down the infected and a"
							 " low-range laser rifle with a high fire rate."));
		AddLine(_C("Looper", "They can also jump two times in the air."));
		break;
	case EPlayerClass::Smoker:
		AddLine(_C("Smoker", "Smoker has a powerful hook that hurts humans and sucks their blood,"
							 " restoring the Smoker's health."));
		AddLine(_C("Smoker", "It can also infect humans and heal the infected with the hammer."));
		break;
	case EPlayerClass::Boomer:
		AddLine(_C("Boomer", "The Boomer explodes when it attacks."));
		AddLine(_C("Boomer", "All humans affected by the explosion become infected."));
		AddLine(_C("Boomer", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Hunter:
		AddLine(_C("Hunter", "The Hunter can jump two times in the air and has some resistance to"
							 " knock-backs."));
		AddLine(_C("Hunter", "It can infect humans and heal the infected with the hammer."));
		AddLine(_C("Hunter", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Bat:
		AddLine(_C("Bat", "Bat can jump endlessly in the air but it cannot infect humans."));
		AddLine(_C("Bat", "Instead, it can hammer humans to steal their health and heal itself."));
		AddLine(_C("Bat", "The hammer is also useful for healing the infected."));
		AddLine(_C("Bat", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Ghost:
		AddLine(_C("Ghost", "The Ghost is invisible until a human comes nearby, it takes damage,"
							" or it uses the hammer."));
		AddLine(_C("Ghost", "It can infect humans and heal the infected with the hammer."));
		AddLine(_C("Ghost", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Spider:
		AddLine(_C("Spider", "The Spider has a web hook that automatically grabs any human touching it."));
		AddLine(_C("Spider", "The web hook mode can be toggled by switching the weapon."));
		AddLine(_C("Spider", "In both modes the hook inflicts 1 damage point per second and can"
							 " grab a human for longer."));
		break;
	case EPlayerClass::Ghoul:
		AddLine(_C("Ghoul", "The Ghoul can devour anything that has died nearby, which makes it"
							" stronger, faster and more resistant."));
		AddLine(_C("Ghoul", "It digests the fodder over time, going back to the normal state."
							" Some nourishment is also lost on death."));
		AddLine(_C("Ghoul", "Ghoul can infect humans and heal the infected with the hammer."));
		AddLine(_C("Ghoul", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Slug:
		AddLine(_C("Slug", "The Slug can make the ground and walls toxic by spreading slime with the hammer."));
		AddLine(_C("Slug", "The slime heals the infected and deals damage to humans."));
		AddLine(_C("Slug", "Slug can infect humans and heal the infected with the hammer."));
		AddLine(_C("Slug", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Voodoo:
		AddLine(_C("Voodoo", "The Voodoo does not die immediately when killed but instead enters"
							 " Spirit mode for a short time."));
		AddLine(_C("Voodoo", "While in Spirit mode it cannot be killed. When the time is up it finally dies."));
		AddLine(_C("Voodoo", "Voodoo can infect humans and heal the infected with the hammer."));
		AddLine(_C("Voodoo", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Witch:
		AddLine(_C("Witch", "The Witch can provide a spawn point for the infected."));
		AddLine(_C("Witch", "If the Witch dies, it disappears and is replaced by another class."));
		AddLine(_C("Witch", "Witch can infect humans and heal the infected with the hammer."));
		AddLine(_C("Witch", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Undead:
		AddLine(_C("Undead", "The Undead cannot die. Instead of dying, it gets frozen for 10 seconds."));
		AddLine(_C("Undead", "If an infected heals it, the freeze effect disappears."));
		AddLine(_C("Undead", "Undead can infect humans and heal the infected with the hammer."));
		AddLine(_C("Undead", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	}

	return true;
}

void CGameContext::ConRules(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	bool Printed = false;
	if(g_Config.m_SvDDRaceRules)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"Be nice and respect others privacy.");
		Printed = true;
	}
	char *apRuleLines[] = {
		g_Config.m_SvRulesLine1,
		g_Config.m_SvRulesLine2,
		g_Config.m_SvRulesLine3,
		g_Config.m_SvRulesLine4,
		g_Config.m_SvRulesLine5,
		g_Config.m_SvRulesLine6,
		g_Config.m_SvRulesLine7,
		g_Config.m_SvRulesLine8,
		g_Config.m_SvRulesLine9,
		g_Config.m_SvRulesLine10,
	};
	for(auto &pRuleLine : apRuleLines)
	{
		if(pRuleLine[0])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD,
				"chatresp", pRuleLine);
			Printed = true;
		}
	}
	if(!Printed)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chatresp",
			"No Rules Defined, Kill em all!!");
	}
}

void CGameContext::ConLanguage(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	int ClientId = pResult->GetClientId();
	
	const char *pLanguageCode = (pResult->NumArguments()>0) ? pResult->GetString(0) : 0x0;
	char aFinalLanguageCode[8];
	aFinalLanguageCode[0] = 0;

	if(pLanguageCode)
	{
		if(str_comp_nocase(pLanguageCode, "ua") == 0)
			str_copy(aFinalLanguageCode, "uk", sizeof(aFinalLanguageCode));
		else
		{
			for(int i=0; i<pSelf->Server()->Localization()->m_pLanguages.size(); i++)
			{
				if(str_comp_nocase(pLanguageCode, pSelf->Server()->Localization()->m_pLanguages[i]->GetFilename()) == 0)
					str_copy(aFinalLanguageCode, pLanguageCode, sizeof(aFinalLanguageCode));
			}
		}
	}
	
	if(aFinalLanguageCode[0])
	{
		pSelf->SetClientLanguage(ClientId, aFinalLanguageCode);
	}
	else
	{
		const char* pLanguage = pSelf->m_apPlayers[ClientId]->GetLanguage();
		const char* pTxtUnknownLanguage = pSelf->Server()->Localization()->Localize(pLanguage, _("Unknown language"));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "language", pTxtUnknownLanguage);	
		
		dynamic_string BufferList;
		int BufferIter = 0;
		for(int i=0; i<pSelf->Server()->Localization()->m_pLanguages.size(); i++)
		{
			if(i>0)
				BufferIter = BufferList.append_at(BufferIter, ", ");
			BufferIter = BufferList.append_at(BufferIter, pSelf->Server()->Localization()->m_pLanguages[i]->GetFilename());
		}
		
		dynamic_string Buffer;
		pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Available languages: {str:ListOfLanguage}"), "ListOfLanguage", BufferList.buffer(), NULL);
		
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "language", Buffer.buffer());
	}
}

void CGameContext::ConCmdList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientId = pResult->GetClientId();
	const char* pLanguage = pSelf->m_apPlayers[ClientId]->GetLanguage();
	
	dynamic_string Buffer;
	
	Buffer.append("~~ ");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("List of commands")); 
	Buffer.append(" ~~\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/antiping, /alwaysrandom, /customskin, /help, /about, /language", NULL);
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/msg, /mute", NULL);
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/changelog", NULL);
	Buffer.append("\n\n");
#ifdef CONF_SQL
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/register, /login, /logout, /setemail", NULL);
	Buffer.append("\n\n");
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/challenge, /top10, /rank, /goal", NULL);
	Buffer.append("\n\n");
#endif
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Press <F3> or <F4> to enable or disable hook protection"), NULL);
			
	pSelf->SendMOTD(ClientId, Buffer.buffer());
}

void CGameContext::ConChangeLog(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ConChangeLog(pResult);
}

void CGameContext::ConChangeLog(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetClientId();

	if(m_aChangeLogEntries.Size() == 0)
	{
		SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("ChangeLog is not provided"), nullptr);
		return;
	}

	int PageNumber = pResult->GetInteger(0);
	if(PageNumber <= 0)
	{
		PageNumber = 1;
	}
	int PagesInTotal = m_aChangeLogPageIndices.Size();
	if(PageNumber > PagesInTotal)
	{
		SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("ChangeLog page {int:PageNumber} is not available"),
			"PageNumber", &PageNumber,
			nullptr);
		return;
	}

	uint32_t PageIndex = PageNumber - 1;
	uint32_t From = m_aChangeLogPageIndices.At(PageIndex);
	uint32_t To = (PageIndex + 1) < m_aChangeLogPageIndices.Size() ? m_aChangeLogPageIndices.At(PageIndex + 1) : m_aChangeLogEntries.Size();

	for(uint32_t i = From; i < To; ++i)
	{
		const std::string &Text = m_aChangeLogEntries.At(i);
		SendChatTarget(ClientId, Text.c_str());
	}

	if(PageNumber != PagesInTotal)
	{
		int NextPage = PageNumber + 1;

		SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT,
			_("(page {int:PageNumber}/{int:PagesInTotal}, see /changelog {int:NextPage})"),
			"PageNumber", &PageNumber,
			"PagesInTotal", &PagesInTotal,
			"NextPage", &NextPage,
			nullptr);
	}
}

void CGameContext::ConReloadChangeLog(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ReloadChangelog();
}

/* INFECTION MODIFICATION END *****************************************/

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	Console()->Register("tune", "s[tuning] i[value]", CFGFLAG_SERVER | CFGFLAG_GAME, ConTuneParam, this, "Tune variable to value");
	Console()->Register("toggle_tune", "s[tuning] i[value 1] i[value 2]", CFGFLAG_SERVER | CFGFLAG_GAME, ConToggleTuneParam, this, "Toggle tune variable");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");
	Console()->Register("pause_game", "", CFGFLAG_SERVER, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r[map]", CFGFLAG_SERVER | CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("restart", "?i[seconds]", CFGFLAG_SERVER | CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r[message]", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r[message]", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "i[id] i[team-id] ?i[delay in minutes]", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i[team-id]", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");

	Console()->Register("insert_vote", "i[position] s[name] r[command]", CFGFLAG_SERVER, ConInsertVote, this, "Insert a voting option");
	Console()->Register("add_vote", "s[name] r[command]", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "r[name]", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "s[name] s[command] ?r[reason]", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("add_map_votes", "", CFGFLAG_SERVER, ConAddMapVotes, this, "Automatically adds voting options for all maps");
	Console()->Register("vote", "r['yes'|'no']", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");

/* INFECTION MODIFICATION START ***************************************/
	Console()->Register("skip_map", "", CFGFLAG_SERVER, ConSkipMap, this, "Change map to the next in the rotation");
	Console()->Register("queue_map", "?r[map]", CFGFLAG_SERVER, ConQueueMap, this, "Set the next map");
	Console()->Register("add_map", "?r[map]", CFGFLAG_SERVER, ConAddMap, this, "Add a map to the maps rotation list");

	Console()->Register("kill_pl", "v[id]", CFGFLAG_SERVER, ConKillPlayer, this, "Kills player v and announces the kill");
	//Chat Command
	Console()->Register("version", "", CFGFLAG_SERVER, ConVersion, this, "Display information about the server version and build");

	Console()->Register("credits", "", CFGFLAG_CHAT, ConCredits, this, "Shows the credits of the mod");
	Console()->Register("about", "", CFGFLAG_CHAT, ConAbout, this, "Display information about the mod");
	Console()->Register("register", "s[username] s[password] ?s[email]", CFGFLAG_CHAT, ConRegister, this, "Create an account");
	Console()->Register("login", "s[username] s[password]", CFGFLAG_CHAT, ConLogin, this, "Login to an account");
	Console()->Register("logout", "", CFGFLAG_CHAT, ConLogout, this, "Logout");
#ifdef CONF_SQL
	Console()->Register("setemail", "s[email]", CFGFLAG_CHAT, ConSetEmail, this, "Change your email");
	
	Console()->Register("top10", "?s[classname]", CFGFLAG_CHAT, ConTop10, this, "Show the top 10 on the current map");
	Console()->Register("challenge", "", CFGFLAG_CHAT, ConChallenge, this, "Show the current winner of the challenge");
	Console()->Register("rank", "?s[classname]", CFGFLAG_CHAT, ConRank, this, "Show your rank");
	Console()->Register("goal", "?s[classname]", CFGFLAG_CHAT, ConGoal, this, "Show your goal");
	Console()->Register("stats", "i", CFGFLAG_CHAT, ConStats, this, "Show stats by id");
#endif
	Console()->Register("help", "?s[page]", CFGFLAG_CHAT, ConHelp, this, "Display help");
	Console()->Register("reload_changelog", "?i[page]", CFGFLAG_SERVER, ConReloadChangeLog, this, "Reload the changelog file");
	Console()->Register("changelog", "?i[page]", CFGFLAG_CHAT, ConChangeLog, this, "Display a changelog page");

	static char aLangs[256] = {};
	if(!aLangs[0])
	{
		dynamic_string BufferList;
		int BufferIter = 0;
		for(int i = 0; i < Server()->Localization()->m_pLanguages.size(); i++)
		{
			if(i)
				BufferIter = BufferList.append_at(BufferIter, "|");
			BufferIter = BufferList.append_at(BufferIter, Server()->Localization()->m_pLanguages[i]->GetFilename());
		}
		if(!BufferList.empty())
		{
			str_format(aLangs, sizeof(aLangs), "s[%s]", BufferList.buffer());
		}
	}

	if(aLangs[0])
	{
		Console()->Register("language", aLangs, CFGFLAG_CHAT, ConLanguage, this, "Set the language");
		Console()->Register("lang", aLangs, CFGFLAG_CHAT, ConLanguage, this, "Set the language");
	}
	Console()->Register("cmdlist", "", CFGFLAG_CHAT, ConCmdList, this, "List of commands");
/* INFECTION MODIFICATION END *****************************************/

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);

	Console()->Chain("sv_maprotation", ConchainSyncMapRotation, this);

#define CHAT_COMMAND(name, params, flags, callback, userdata, help) m_pConsole->Register(name, params, flags, callback, userdata, help);
	// From ddracechat.h
	CHAT_COMMAND("rules", "", CFGFLAG_CHAT | CFGFLAG_SERVER, ConRules, this, "Shows the server rules")
	CHAT_COMMAND("info", "", CFGFLAG_CHAT | CFGFLAG_SERVER, ConInfo, this, "Shows info about this server")
	CHAT_COMMAND("me", "r[message]", CFGFLAG_CHAT | CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConMe, this, "Like the famous irc command '/me says hi' will display '<yourname> says hi'")
	CHAT_COMMAND("w", "s[player name] r[message]", CFGFLAG_CHAT | CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConWhisper, this, "Whisper something to someone (private message)")
	CHAT_COMMAND("whisper", "s[player name] r[message]", CFGFLAG_CHAT | CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConWhisper, this, "Whisper something to someone (private message)")
	CHAT_COMMAND("c", "r[message]", CFGFLAG_CHAT | CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConConverse, this, "Converse with the last person you whispered to (private message)")
	CHAT_COMMAND("converse", "r[message]", CFGFLAG_CHAT | CFGFLAG_SERVER | CFGFLAG_NONTEEHISTORIC, ConConverse, this, "Converse with the last person you whispered to (private message)")
	CHAT_COMMAND("timeout", "?s[code]", CFGFLAG_CHAT | CFGFLAG_SERVER, ConTimeout, this, "Set timeout protection code s")

	CHAT_COMMAND("msg", "s[player or group name] r[message]", CFGFLAG_CHAT | CFGFLAG_NONTEEHISTORIC, ConConverse, this, "Check '/help msg' for details")
#undef CHAT_COMMAND

	InitGeolocation();
}

void CGameContext::OnInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pEngine = nullptr;
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);

	m_GameUuid = RandomUuid();

	for(int i=0; i<MAX_CLIENTS; i++)
	{
		Server()->ResetClientMemoryAboutGame(i);
	}

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));
	
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_VoteLanguageTick[i] = 0;
		str_copy(m_VoteLanguage[i], "en", sizeof(m_VoteLanguage[i]));				
	}

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	// select gametype
	m_pController = new CInfClassGameController(this);
	m_pController->RegisterChatCommands(Console());

	Console()->ExecuteFile(g_Config.m_SvResetFile);

	InitChangelog();
	m_pController->InitSmartMapRotation();

	{
		//Open file
		char *pMapShortName = &g_Config.m_SvMap[0];
		char MapCfgFilename[512];
		str_format(MapCfgFilename, sizeof(MapCfgFilename), "maps/%s.cfg", pMapShortName);
		Console()->ExecuteFile(MapCfgFilename);
	}

	// create all entities from the game layer
	CreateAllEntities(true);

	if(GIT_SHORTREV_HASH)
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "git-revision", GIT_SHORTREV_HASH);

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS - i - 1, nullptr);
		}
	}
#endif
}

void CGameContext::CreateAllEntities(bool Initial)
{
	// create all entities from entity layers
	if(m_Layers.EntityGroup())
	{
		char aLayerName[12];

		const CMapItemGroup* pGroup = m_Layers.EntityGroup();
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = m_Layers.GetLayer(pGroup->m_StartLayer+l);
			if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				CMapItemLayerQuads *pQLayer = (CMapItemLayerQuads *)pLayer;

				IntsToStr(pQLayer->m_aName, sizeof(aLayerName)/sizeof(int), aLayerName);

				const CQuad *pQuads = (const CQuad *) Kernel()->RequestInterface<IMap>()->GetDataSwapped(pQLayer->m_Data);

				for(int q = 0; q < pQLayer->m_NumQuads; q++)
				{
					vec2 P0(fx2f(pQuads[q].m_aPoints[0].x), fx2f(pQuads[q].m_aPoints[0].y));
					vec2 P1(fx2f(pQuads[q].m_aPoints[1].x), fx2f(pQuads[q].m_aPoints[1].y));
					vec2 P2(fx2f(pQuads[q].m_aPoints[2].x), fx2f(pQuads[q].m_aPoints[2].y));
					vec2 P3(fx2f(pQuads[q].m_aPoints[3].x), fx2f(pQuads[q].m_aPoints[3].y));
					vec2 Pivot(fx2f(pQuads[q].m_aPoints[4].x), fx2f(pQuads[q].m_aPoints[4].y));
					m_pController->OnEntity(aLayerName, Pivot, P0, P1, P2, P3, pQuads[q].m_PosEnv);
				}
			}
		}
	}
}

void CGameContext::OnMapChange(char *pNewMapName, int MapNameSize)
{
}

void CGameContext::OnShutdown()
{
	//reset votes.
	EndVote();

	m_pController = nullptr;
	Clear();
	delete m_pController;
}

void CGameContext::OnSnap(int ClientId)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(Server()->IsRecording(ClientId > -1 ? ClientId : MAX_CLIENTS) && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
		int *pParams = (int *)&m_Tuning;
		for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
			Msg.AddInt(pParams[i]);
		Server()->SendMsg(&Msg, MSGFLAG_RECORD|MSGFLAG_NOSEND, ClientId);
	}

	m_World.Snap(ClientId);
	m_pController->Snap(ClientId);
	m_Events.Snap(ClientId);

/* INFECTION MODIFICATION START ***************************************/
	int SnappingClientVersion = GetClientVersion(ClientId);
	CSnapContext Context(SnappingClientVersion);
	//Snap laser dots
	for(int i=0; i < m_LaserDots.size(); i++)
	{
		if(ClientId >= 0)
		{
			vec2 CheckPos = (m_LaserDots[i].m_Pos0 + m_LaserDots[i].m_Pos1)*0.5f;
			float dx = m_apPlayers[ClientId]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientId]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientId]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}

		SnapLaserObject(Context, m_LaserDots[i].m_SnapId, m_LaserDots[i].m_Pos1, m_LaserDots[i].m_Pos0, Server()->Tick());
	}
	for(int i=0; i < m_HammerDots.size(); i++)
	{
		if(ClientId >= 0)
		{
			vec2 CheckPos = m_HammerDots[i].m_Pos;
			float dx = m_apPlayers[ClientId]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientId]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientId]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}
		
		CNetObj_Projectile *pObj = Server()->SnapNewItem<CNetObj_Projectile>(m_HammerDots[i].m_SnapId);
		if(pObj)
		{
			pObj->m_X = (int)m_HammerDots[i].m_Pos.x;
			pObj->m_Y = (int)m_HammerDots[i].m_Pos.y;
			pObj->m_VelX = 0;
			pObj->m_VelY = 0;
			pObj->m_StartTick = Server()->Tick();
			pObj->m_Type = WEAPON_HAMMER;
		}
	}
	for(int i=0; i < m_LoveDots.size(); i++)
	{
		if(ClientId >= 0)
		{
			vec2 CheckPos = m_LoveDots[i].m_Pos;
			float dx = m_apPlayers[ClientId]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientId]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientId]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}

		CNetObj_Pickup *pObj = Server()->SnapNewItem<CNetObj_Pickup>(m_LoveDots[i].m_SnapId);
		if(pObj)
		{
			pObj->m_X = (int)m_LoveDots[i].m_Pos.x;
			pObj->m_Y = (int)m_LoveDots[i].m_Pos.y;
			pObj->m_Type = POWERUP_HEALTH;
			pObj->m_Subtype = 0;
		}
	}
/* INFECTION MODIFICATION END *****************************************/
	
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientId);
	}
}

void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientId) const
{
	return m_apPlayers[ClientId] && m_apPlayers[ClientId]->m_IsReady;
}

bool CGameContext::IsClientPlayer(int ClientId) const
{
	return m_apPlayers[ClientId] && m_apPlayers[ClientId]->GetTeam() != TEAM_SPECTATORS;
}

int CGameContext::PersistentClientDataSize() const
{
	dbg_assert(m_pController != nullptr, "There must be a controller to query the client persistent data size");
	return m_pController ? m_pController->PersistentClientDataSize() : 0;
}

CUuid CGameContext::GameUuid() const { return m_GameUuid; }
const char *CGameContext::GameType() const { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
const char *CGameContext::Version() const { return GAME_VERSION; }
const char *CGameContext::NetVersion() const { return GAME_NETVERSION; }

IGameServer *CreateGameServer() { return new CGameContext; }

void CGameContext::OnSetAuthed(int ClientId, int Level)
{
}

bool CGameContext::ProcessSpamProtection(int ClientId, bool RespectChatInitialDelay)
{
	if(!m_apPlayers[ClientId])
		return false;
	if(g_Config.m_SvSpamprotection && m_apPlayers[ClientId]->m_LastChat && m_apPlayers[ClientId]->m_LastChat + Server()->TickSpeed() * g_Config.m_SvChatDelay > Server()->Tick())
		return true;

	m_apPlayers[ClientId]->m_LastChat = Server()->Tick();

	int Muted = 0;
	if(Server()->GetClientSession(ClientId) && Server()->GetClientSession(ClientId)->m_MuteTick > 0)
	{
		Muted = Server()->GetClientSession(ClientId)->m_MuteTick/Server()->TickSpeed();
	}

	if(Muted > 0)
	{
		SendChatTarget_Localization(ClientId, CHATCATEGORY_ACCUSATION, _("You are muted for {sec:Duration}"), "Duration", &Muted, NULL);
		return true;
	}

	return false;
}

bool CheckClientId2(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	return true;
}

void CGameContext::Whisper(int ClientId, char *pStr)
{
	if(ProcessSpamProtection(ClientId))
		return;

	pStr = str_skip_whitespaces(pStr);

	char *pName;
	int Victim;
	bool Error = false;

	// add token
	if(*pStr == '"')
	{
		pStr++;

		pName = pStr;
		char *pDst = pStr; // we might have to process escape data
		while(true)
		{
			if(pStr[0] == '"')
			{
				break;
			}
			else if(pStr[0] == '\\')
			{
				if(pStr[1] == '\\')
					pStr++; // skip due to escape
				else if(pStr[1] == '"')
					pStr++; // skip due to escape
			}
			else if(pStr[0] == 0)
			{
				Error = true;
				break;
			}

			*pDst = *pStr;
			pDst++;
			pStr++;
		}

		if(!Error)
		{
			// write null termination
			*pDst = 0;

			pStr++;

			for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
				if(str_comp(pName, Server()->ClientName(Victim)) == 0)
					break;
		}
	}
	else
	{
		pName = pStr;
		while(true)
		{
			if(pStr[0] == 0)
			{
				Error = true;
				break;
			}
			if(pStr[0] == ' ')
			{
				pStr[0] = 0;
				for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
					if(str_comp(pName, Server()->ClientName(Victim)) == 0)
						break;

				pStr[0] = ' ';

				if(Victim < MAX_CLIENTS)
					break;
			}
			pStr++;
		}
	}

	if(pStr[0] != ' ')
	{
		Error = true;
	}

	*pStr = 0;
	pStr++;

	if(Error)
	{
		SendChatTarget(ClientId, "Invalid whisper");
		return;
	}

	if(Victim >= MAX_CLIENTS || !CheckClientId2(Victim))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "No player with name \"%s\" found", pName);
		SendChatTarget(ClientId, aBuf);
		return;
	}

	WhisperId(ClientId, Victim, pStr);
}

void CGameContext::WhisperId(int ClientId, int VictimId, const char *pMessage)
{
	if(!CheckClientId2(ClientId))
		return;

	if(!CheckClientId2(VictimId))
		return;

	if(m_apPlayers[ClientId])
	{
		m_apPlayers[ClientId]->m_LastWhisperTo = VictimId;
		m_apPlayers[ClientId]->m_LastChat = Server()->Tick();
	}

	char aCensoredMessage[256];
	CensorMessage(aCensoredMessage, pMessage, sizeof(aCensoredMessage));

	char aBuf[256];

	if(GetClientVersion(ClientId) >= VERSION_DDNET_WHISPER)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = CHAT_WHISPER_SEND;
		Msg.m_ClientId = VictimId;
		Msg.m_pMessage = aCensoredMessage;

		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "[→ %s] %s", Server()->ClientName(VictimId), aCensoredMessage);
		SendChatTarget(ClientId, aBuf);
	}

	if(ClientId == VictimId)
	{
		return;
	}

	if(CGameContext::m_ClientMuted[VictimId][ClientId])
	{
		return;
	}

	if(GetClientVersion(VictimId) >= VERSION_DDNET_WHISPER)
	{
		CNetMsg_Sv_Chat Msg2;
		Msg2.m_Team = CHAT_WHISPER_RECV;
		Msg2.m_ClientId = ClientId;
		Msg2.m_pMessage = aCensoredMessage;

		Server()->SendPackMsg(&Msg2, MSGFLAG_VITAL | MSGFLAG_NORECORD, VictimId);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "[← %s] %s", Server()->ClientName(ClientId), aCensoredMessage);
		SendChatTarget(VictimId, aBuf);
	}
}

void CGameContext::Converse(int ClientId, const char *pStr)
{
	CPlayer *pPlayer = m_apPlayers[ClientId];
	if(!pPlayer)
		return;

	if(pPlayer->m_LastWhisperTo < 0)
	{
		SendChatTarget(ClientId, "You do not have an ongoing conversation. Whisper to someone to start one");
	}
	else
	{
		WhisperId(ClientId, pPlayer->m_LastWhisperTo, pStr);
	}
}

bool CGameContext::IsVersionBanned(int Version)
{
	char aVersion[16];
	str_format(aVersion, sizeof(aVersion), "%d", Version);

	return str_in_list(g_Config.m_SvBannedVersions, ",", aVersion);
}

int CGameContext::GetClientVersion(int ClientId) const
{
	return Server()->GetClientVersion(ClientId);
}

bool CGameContext::RateLimitPlayerVote(int ClientId)
{
	int64_t Now = Server()->Tick();
	int64_t TickSpeed = Server()->TickSpeed();
	CPlayer *pPlayer = m_apPlayers[ClientId];

	if(g_Config.m_SvRconVote && !Server()->GetAuthedState(ClientId))
	{
		SendChatTarget(ClientId, "You can only vote after logging in.");
		return true;
	}

	if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry + TickSpeed * 3 > Now)
		return true;

	pPlayer->m_LastVoteTry = Now;
	if(m_VoteCloseTime)
	{
		SendChatTarget(ClientId, "Wait for current vote to end before calling a new one.");
		return true;
	}

	if(Server()->GetAuthedState(ClientId))
		return false;

	int TimeLeft = pPlayer->m_LastVoteCall + TickSpeed * g_Config.m_SvVoteDelay - Now;
	if(pPlayer->m_LastVoteCall && TimeLeft > 0)
	{
		char aChatmsg[64];
		str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote.", (int)(TimeLeft / TickSpeed) + 1);
		SendChatTarget(ClientId, aChatmsg);
		return true;
	}

	return false;
}

bool CGameContext::RateLimitPlayerMapVote(int ClientId)
{
	if(!Server()->GetAuthedState(ClientId) && time_get() < m_LastMapVote + (time_freq() * g_Config.m_SvVoteMapTimeDelay))
	{
		char aChatmsg[512] = {0};
		str_format(aChatmsg, sizeof(aChatmsg), "There's a %d second delay between map-votes, please wait %d seconds.",
			g_Config.m_SvVoteMapTimeDelay, (int)((m_LastMapVote + g_Config.m_SvVoteMapTimeDelay * time_freq() - time_get()) / time_freq()));
		SendChatTarget(ClientId, aChatmsg);
		return true;
	}
	return false;
}

void CGameContext::OnUpdatePlayerServerInfo(char *aBuf, int BufSize, int Id)
{
	if(BufSize <= 0)
		return;

	aBuf[0] = '\0';

	if(!m_apPlayers[Id])
		return;

	char aCSkinName[64];

	CTeeInfo &TeeInfo = m_apPlayers[Id]->m_TeeInfos;

	char aJsonSkin[400];
	aJsonSkin[0] = '\0';

	if(!Server()->IsSixup(Id))
	{
		// 0.6
		if(TeeInfo.m_UseCustomColor)
		{
			str_format(aJsonSkin, sizeof(aJsonSkin),
				"\"name\":\"%s\","
				"\"color_body\":%d,"
				"\"color_feet\":%d",
				EscapeJson(aCSkinName, sizeof(aCSkinName), TeeInfo.m_aSkinName),
				TeeInfo.m_ColorBody,
				TeeInfo.m_ColorFeet);
		}
		else
		{
			str_format(aJsonSkin, sizeof(aJsonSkin),
				"\"name\":\"%s\"",
				EscapeJson(aCSkinName, sizeof(aCSkinName), TeeInfo.m_aSkinName));
		}
	}
	else
	{
		const char *apPartNames[protocol7::NUM_SKINPARTS] = {"body", "marking", "decoration", "hands", "feet", "eyes"};
		char aPartBuf[64];

		for(int i = 0; i < protocol7::NUM_SKINPARTS; ++i)
		{
			str_format(aPartBuf, sizeof(aPartBuf),
				"%s\"%s\":{"
				"\"name\":\"%s\"",
				i == 0 ? "" : ",",
				apPartNames[i],
				EscapeJson(aCSkinName, sizeof(aCSkinName), TeeInfo.m_apSkinPartNames[i]));

			str_append(aJsonSkin, aPartBuf, sizeof(aJsonSkin));

			if(TeeInfo.m_aUseCustomColors[i])
			{
				str_format(aPartBuf, sizeof(aPartBuf),
					",\"color\":%d",
					TeeInfo.m_aSkinPartColors[i]);
				str_append(aJsonSkin, aPartBuf, sizeof(aJsonSkin));
			}
			str_append(aJsonSkin, "}", sizeof(aJsonSkin));
		}
	}

	str_format(aBuf, BufSize,
		",\"skin\":{"
		"%s"
		"},"
		"\"afk\":%s,"
		"\"team\":%d",
		aJsonSkin,
		JsonBool(m_apPlayers[Id]->IsAfk()),
		m_apPlayers[Id]->GetTeam());
}
