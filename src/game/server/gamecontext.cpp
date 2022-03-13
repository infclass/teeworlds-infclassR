/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include <engine/map.h>
#include <engine/console.h>
#include <engine/storage.h>
#include <engine/server/roundstatistics.h>
#include <engine/server/sql_server.h>
#include <engine/shared/linereader.h>
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <iostream>
#include <algorithm>

#include <game/server/infclass/infcgamecontroller.h>

#include <game/server/player.h>

#ifdef CONF_GEOLOCATION
#include <infclassr/geolocation.h>
#endif

enum
{
	RESET,
	NO_RESET
};

/* INFECTION MODIFICATION START ***************************************/
bool CGameContext::m_ClientMuted[MAX_CLIENTS][MAX_CLIENTS];
array_on_stack<string, 256> CGameContext::m_aChangeLogEntries;
array_on_stack<int, 16> CGameContext::m_aChangeLogPageIndices;

void CGameContext::OnSetAuthed(int ClientID, int Level)
{
	if(m_apPlayers[ClientID])
		m_apPlayers[ClientID]->m_Authed = Level;
}

/* INFECTION MODIFICATION END *****************************************/

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;
	m_pConfig = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_apPlayers[i] = 0;
		m_aHitSoundState[i] = 0;
	}

	if(Resetting==NO_RESET) // first init
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
			for(int j = 0; j < MAX_CLIENTS; j++)
				CGameContext::m_ClientMuted[i][j] = false;
	}

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_HeroGiftCooldown = 0;
	
	m_ChatResponseTargetID = -1;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();
	
	m_FunRound = false;
	m_FunRoundsPassed = 0;
}

CGameContext::CGameContext(int Resetting)
{
	fout.open(g_Config.m_PlayerLogfile, std::ios_base::app);
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	fout.open(g_Config.m_PlayerLogfile, std::ios_base::app);
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < m_LaserDots.size(); i++)
		Server()->SnapFreeID(m_LaserDots[i].m_SnapID);
	for(int i = 0; i < m_HammerDots.size(); i++)
		Server()->SnapFreeID(m_HammerDots[i].m_SnapID);
	
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
	
#ifdef CONF_GEOLOCATION
	if(!m_Resetting)
	{
		Geolocation::Shutdown();
	}
#endif
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
	mem_zero(this, sizeof(*this));
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

CPlayer *CGameContext::GetPlayer(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return nullptr;

	return m_apPlayers[ClientID];
}

class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

int CGameContext::GetZombieCount() {
	int count = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if (!m_apPlayers[i])
			continue;
		if (m_apPlayers[i]->IsActuallyZombie())
			count++;
	}
	return count;
}

int CGameContext::GetZombieCount(int zombie_class) {
	int count = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if (!m_apPlayers[i])
			continue;
		if (m_apPlayers[i]->IsActuallyZombie() && m_apPlayers[i]->GetClass() == zombie_class)
			count++;
	}
	return count;
}

void CGameContext::SetAvailabilities(std::vector<int> value)
{
	static const int ValuesNumber = NB_HUMANCLASS;
	if (value.size() < ValuesNumber)
		value.resize(ValuesNumber, 0);

	for (int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; PlayerClass++)
	{
		const int ClassEnabledValue = value.at(PlayerClass - START_HUMANCLASS - 1);
		Server()->SetPlayerClassEnabled(PlayerClass, ClassEnabledValue);
	}
}

void CGameContext::SetProbabilities(std::vector<int> value)
{
	int *extraConfigValues[] =
	{
		// The order is still important!
		&g_Config.m_InfGhoulThreshold,
		&g_Config.m_InfGhoulStomachSize,
	};
	static const int ExtraValuesNumber = sizeof(extraConfigValues) / sizeof(extraConfigValues[0]);
	static const int ValuesNumber = NB_INFECTEDCLASS + ExtraValuesNumber;
	if (value.size() < ValuesNumber)
		value.resize(ValuesNumber, 0);

	for (int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; PlayerClass++)
	{
		const int ClassProbability = value.at(PlayerClass - START_INFECTEDCLASS - 1);
		Server()->SetPlayerClassProbability(PlayerClass, ClassProbability);
	}
	for (int i = 0; i < ExtraValuesNumber; ++i)
	{
		const int newValue = value.at(NB_INFECTEDCLASS + i);
		*extraConfigValues[i] = newValue;
	}
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
	State.m_SnapID = Server()->SnapNewID();
	
	m_LaserDots.add(State);
}

void CGameContext::CreateHammerDotEvent(vec2 Pos, int LifeSpan)
{
	CGameContext::HammerDotState State;
	State.m_Pos = Pos;
	State.m_LifeSpan = LifeSpan;
	State.m_SnapID = Server()->SnapNewID();
	
	m_HammerDots.add(State);
}

void CGameContext::CreateLoveEvent(vec2 Pos)
{
	CGameContext::LoveDotState State;
	State.m_Pos = Pos;
	State.m_LifeSpan = Server()->TickSpeed();
	State.m_SnapID = Server()->SnapNewID();
	
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

void CGameContext::CreateDeath(vec2 Pos, int ClientID, int64_t Mask)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
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
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if(Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	if(Target == -2)
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	else
	{
		int Flag = MSGFLAG_VITAL;
		if(Target != -1)
			Flag |= MSGFLAG_NORECORD;
		Server()->SendPackMsg(&Msg, Flag, Target);
	}
}

void CGameContext::CallVote(int ClientID, const char *pDesc, const char *pCmd, const char *pReason, const char *pChatmsg)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	int64 Now = Server()->Tick();
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pPlayer)
		return;

	SendChatTarget(-1, pChatmsg);

	m_VoteCreator = ClientID;
	StartVote(pDesc, pCmd, pReason);
	pPlayer->m_Vote = 1;
	pPlayer->m_VotePos = m_VotePos = 1;
	pPlayer->m_LastVoteCall = Now;
}

void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	// only for demo record
	if(To < 0)
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, To);
}

/* INFECTION MODIFICATION START ***************************************/
void CGameContext::SendChatTarget_Localization(int To, int Category, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Buffer.clear();
			Buffer.append(GetChatCategoryPrefix(Category));
			if(To < 0 && i == 0)
			{
				// one message for record
				dynamic_string tmpBuf;
				tmpBuf.copy(Buffer);
				Server()->Localization()->Format_VL(tmpBuf, "en", pText, VarArgs);
				Msg.m_pMessage = tmpBuf.buffer();
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);
			}
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);
			
			Msg.m_pMessage = Buffer.buffer();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
	
	va_end(VarArgs);
}

void CGameContext::SendChatTarget_Localization_P(int To, int Category, int Number, const char* pText, ...)
{
	int Start = (To < 0 ? 0 : To);
	int End = (To < 0 ? MAX_CLIENTS : To+1);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	
	dynamic_string Buffer;
	
	va_list VarArgs;
	va_start(VarArgs, pText);
	
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			Buffer.clear();
			Buffer.append(GetChatCategoryPrefix(Category));
			Server()->Localization()->Format_VLP(Buffer, m_apPlayers[i]->GetLanguage(), Number, pText, VarArgs);
			
			Msg.m_pMessage = Buffer.buffer();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
		}
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

void CGameContext::AddBroadcast(int ClientID, const char* pText, int Priority, int LifeSpan)
{
	if(LifeSpan > 0)
	{
		if(m_BroadcastStates[ClientID].m_TimedPriority > Priority)
			return;
			
		str_copy(m_BroadcastStates[ClientID].m_TimedMessage, pText, sizeof(m_BroadcastStates[ClientID].m_TimedMessage));
		m_BroadcastStates[ClientID].m_LifeSpanTick = LifeSpan;
		m_BroadcastStates[ClientID].m_TimedPriority = Priority;
	}
	else
	{
		if(m_BroadcastStates[ClientID].m_Priority > Priority)
			return;
			
		str_copy(m_BroadcastStates[ClientID].m_NextMessage, pText, sizeof(m_BroadcastStates[ClientID].m_NextMessage));
		m_BroadcastStates[ClientID].m_Priority = Priority;
	}
}

void CGameContext::SetClientLanguage(int ClientID, const char *pLanguage)
{
	Server()->SetClientLanguage(ClientID, pLanguage);
	if(m_apPlayers[ClientID])
	{
		m_apPlayers[ClientID]->SetLanguage(pLanguage);
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
	for(string & Entry : m_aChangeLogEntries)
	{
		Entry = nullptr;
	}
	m_aChangeLogEntries.Clear();
	m_aChangeLogPageIndices.Clear();

	const char *pChangelogFilename = Config()->m_SvChangeLogFile;
	if(!pChangelogFilename || pChangelogFilename[0] == 0)
	{
		dbg_msg("ChangeLog", "ChangeLog file is not set");
		return;
	}

	IOHANDLE File = Storage()->OpenFile(pChangelogFilename, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
	{
		dbg_msg("ChangeLog", "unable to open '%s'", pChangelogFilename);
		return;
	}

	CLineReader LineReader;
	LineReader.Init(File);
	char *pLine = nullptr;
	const int MaxLinesPerPage = Config()->m_SvChangeLogMaxLinesPerPage;
	int AddedLines = 0;

	array_on_stack<char, 8> SamePageItemStartChars = {
		' ',
		'-',
	};

	while((pLine = LineReader.Get()))
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
				dbg_msg("ChangeLog", "ChangeLog truncated: only %d pages allowed", m_aChangeLogPageIndices.Capacity());
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
			dbg_msg("ChangeLog", "ChangeLog truncated: only %d lines allowed", m_aChangeLogEntries.Capacity());
			break;
		}
		m_aChangeLogEntries.Add(pLine);
		++AddedLines;
	}
	io_close(File);
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
		if(m_apPlayers[i])
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
		if(m_apPlayers[i])
		{
			Server()->Localization()->Format_VLP(Buffer, m_apPlayers[i]->GetLanguage(), Number, pText, VarArgs);
			AddBroadcast(i, Buffer.buffer(), Priority, LifeSpan);
		}
	}
	
	va_end(VarArgs);
}

void CGameContext::SendBroadcast_ClassIntro(int ClientID, int Class)
{
	const char *pClassName = CInfClassGameController::GetClassDisplayName(Class);
	const char *pTranslated = Server()->Localization()->Localize(m_apPlayers[ClientID]->GetLanguage(), pClassName);
	
	if(Class < END_HUMANCLASS)
		SendBroadcast_Localization(ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE,
			_("You are a human: {str:ClassName}"), "ClassName", pTranslated, NULL);
	else
		SendBroadcast_Localization(ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE,
			_("You are an infected: {str:ClassName}"), "ClassName", pTranslated, NULL);
}

/* INFECTION MODIFICATION END *****************************************/

void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);

	if(pText && pText[0] == '!' && Config()->m_SvFilterChatCommands)
	{
		return;
	}

	if(Team == CGameContext::CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, DemoClientID);

		// send to the clients that did not mute chatter
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && !CGameContext::m_ClientMuted[i][ChatterClientID])
			{
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
			}
		}
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, DemoClientID);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
/* INFECTION MODIFICATION START ***************************************/
			if(m_apPlayers[i])
			{
				int PlayerTeam = (m_apPlayers[i]->IsZombie() ? CGameContext::CHAT_RED : CGameContext::CHAT_BLUE );
				if(m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS) PlayerTeam = CGameContext::CHAT_SPEC;
				
				if(PlayerTeam == Team && !CGameContext::m_ClientMuted[i][ChatterClientID])
				{
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
				}
			}
/* INFECTION MODIFICATION END *****************************************/
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendMotd(int ClientID)
{
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = Config()->m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
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
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
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
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	std::cout << "SendVoteStatus" << std::endl;
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::AbortVoteKickOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientID) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientID)))
		m_VoteCloseTime = -1;
	
	if(m_VoteCloseTime && m_VoteBanClientID == ClientID)
	{
		m_VoteCloseTime = -1;
		m_VoteBanClientID = -1;
	}
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

void CGameContext::SendTuningParams(int ClientID)
{
	if(ClientID == -1)
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

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;

	unsigned int Last = sizeof(m_Tuning) / sizeof(int);
	if(m_apPlayers[ClientID])
	{
		int ClientVersion = m_apPlayers[ClientID]->GetClientVersion();
		if(ClientVersion < VERSION_DDNET_EXTRATUNES)
			Last = 33;
		else if(ClientVersion < VERSION_DDNET_HOOKDURATION_TUNE)
			Last = 37;
		else if(ClientVersion < VERSION_DDNET_FIREDELAY_TUNE)
			Last = 38;
	}

	for(unsigned i = 0; i < Last; i++)
	{
		if(i == 31) // collision
		{
			// inverted to avoid client collision prediction
			// (keep behavior introduced by commit 11c408e5dd8f3672b658ad0581f016be85a46011)
			Msg.AddInt(0);
		}
		else if(i == 33) // jetpack
		{
			Msg.AddInt(0);
		}
		else
		{
			Msg.AddInt(pParams[i]);
		}
	}
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendHitSound(int ClientID)
{
	if(m_aHitSoundState[ClientID] < 1)
	{
		m_aHitSoundState[ClientID] = 1;
	}
}

void CGameContext::SendScoreSound(int ClientID)
{
	m_aHitSoundState[ClientID] = 2;
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

	if(m_HeroGiftCooldown > 0)
		m_HeroGiftCooldown--;

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
				m_VoteBanClientID = i;
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

	//update core properties important for hook
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetCharacter())
		{
			m_apPlayers[i]->GetCharacter()->m_Core.m_Infected = m_apPlayers[i]->IsZombie();
			m_apPlayers[i]->GetCharacter()->m_Core.m_InLove = m_apPlayers[i]->GetCharacter()->IsInLove();
			m_apPlayers[i]->GetCharacter()->m_Core.m_HookProtected = m_apPlayers[i]->HookProtectionEnabled();
		}
	}
	
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
					if(m_apPlayers[j] && m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS && m_apPlayers[j]->m_SpectatorID == i)
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
			Server()->SnapFreeID(m_LaserDots[DotIter].m_SnapID);
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
			Server()->SnapFreeID(m_HammerDots[DotIter].m_SnapID);
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
			Server()->SnapFreeID(m_LoveDots[DotIter].m_SnapID);
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
			SendChatTarget(-1, "Vote aborted");
			EndVote();
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
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i])	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
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
				if(m_VoteBanClientID >= 0)
				{
					Server()->RemoveAccusations(m_VoteBanClientID);
					m_VoteBanClientID = -1;
				}
				
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand, -1, false);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
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
				if(m_VoteBanClientID >= 0)
				{
					Server()->RemoveAccusations(m_VoteBanClientID);
					m_VoteBanClientID = -1;
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

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
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

void CGameContext::ProgressVoteOptions(int ClientID)
{
	CPlayer *pPl = m_apPlayers[ClientID];

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
	OptionMsg.m_NumOptions = NumVotesToSend;
	Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);

	pPl->m_SendVoteIndex += NumVotesToSend;
}

void CGameContext::OnClientEnter(int ClientID)
{
	//world.insert_entity(&players[client_id]);
	m_apPlayers[ClientID]->m_IsInGame = true;
	m_apPlayers[ClientID]->Respawn();
	
/* INFECTION MODIFICATION START ***************************************/
	SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} entered and joined the game"), "PlayerName", Server()->ClientName(ClientID), NULL);

	SendChatTarget(ClientID, "InfectionClass Mod. Version: " GAME_VERSION);
	SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT,
		_("See also: /help, /changelog, /about"), nullptr);

	if(Config()->m_AboutContactsDiscord[0])
	{
		SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT,
			_("Join our Discord server: {str:Url}"), "Url",
			Config()->m_AboutContactsDiscord, nullptr);
	}
	if(Config()->m_AboutContactsTelegram[0])
	{
		SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT,
			_("Join our Telegram: {str:Url}"), "Url",
			Config()->m_AboutContactsTelegram, nullptr);
	}
	if(Config()->m_AboutContactsMatrix[0])
	{
		SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT,
			_("Join our Matrix room: {str:Url}"), "Url",
			Config()->m_AboutContactsMatrix, nullptr);
	}

	char output[512];
	str_format(output, sizeof(output), "[%08x][%s][%s]", (int)time(0), Server()->GetClientIP(ClientID).c_str(), Server()->ClientName(ClientID));
	fout << output << std::endl;
/* INFECTION MODIFICATION END *****************************************/

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), m_apPlayers[ClientID]->GetTeam());
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	m_VoteUpdate = true;
}

void CGameContext::OnClientConnected(int ClientID)
{
	dbg_assert(!m_apPlayers[ClientID], "non-free player slot");
	m_apPlayers[ClientID] = m_pController->CreatePlayer(ClientID);
	
	//players[client_id].init(client_id);
	//players[client_id].client_id = client_id;

	(void)m_pController->CheckTeamBalance();

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		if(ClientID >= MAX_CLIENTS-g_Config.m_DbgDummies)
			return;
	}
#endif

/* INFECTION MODIFICATION START ***************************************/	
	Server()->RoundStatistics()->ResetPlayer(ClientID);
/* INFECTION MODIFICATION END *****************************************/	

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	// send motd
	if(!Server()->GetClientMemory(ClientID, CLIENTMEMORY_MOTD))
	{
		SendMotd(ClientID);
		Server()->SetClientMemory(ClientID, CLIENTMEMORY_MOTD, true);
	}

	m_BroadcastStates[ClientID].m_NoChangeTick = 0;
	m_BroadcastStates[ClientID].m_LifeSpanTick = 0;
	m_BroadcastStates[ClientID].m_Priority = BROADCAST_PRIORITY_LOWEST;
	m_BroadcastStates[ClientID].m_PrevMessage[0] = 0;
	m_BroadcastStates[ClientID].m_NextMessage[0] = 0;
}

void CGameContext::OnClientDrop(int ClientID, int Type, const char *pReason)
{
	AbortVoteKickOnDisconnect(ClientID);
	m_pController->OnPlayerDisconnect(m_apPlayers[ClientID], Type, pReason);

	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	Server()->RoundStatistics()->ResetPlayer(ClientID);

	(void)m_pController->CheckTeamBalance();
	m_VoteUpdate = true;

	// update spectator modes
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_SpectatorID == ClientID)
			m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
	}
	// InfClassR remove spectators
	RemoveSpectatorCID(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		// remove everyone this player had muted
		CGameContext::m_ClientMuted[ClientID][i] = false;
		
		// reset mutes for everyone that muted this player
		CGameContext::m_ClientMuted[i][ClientID] = false;
	}
	// InfClassR end
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_LastWhisperTo == ClientID)
			m_apPlayers[i]->m_LastWhisperTo = -1;
	}
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

// will be called when a player wants to start a vote
void CGameContext::OnCallVote(void *pRawMsg, int ClientID)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if (!pPlayer)
		return;

	if(RateLimitPlayerVote(ClientID))
		return;

	char aChatmsg[512] = {0};
	char aDesc[VOTE_DESC_LENGTH] = {0};
	char aCmd[VOTE_CMD_LENGTH] = {0};
	char aReason[VOTE_REASON_LENGTH] = "No reason given";
	CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
	if(!str_utf8_check(pMsg->m_Type) || !str_utf8_check(pMsg->m_Reason) || !str_utf8_check(pMsg->m_Value))
	{
		return;
	}
	if(pMsg->m_Reason[0])
	{
		str_copy(aReason, pMsg->m_Reason, sizeof(aReason));
	}

	if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
	{
		int KickID = str_toint(pMsg->m_Value);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
		{
			SendChatTarget(ClientID, "Invalid client id to kick");
			return;
		}
		if(KickID == ClientID)
		{
			SendChatTarget(ClientID, "You can't kick yourself");
			return;
		}
		if(Server()->GetAuthedState(KickID) != IServer::AUTHED_NO)
		{
			SendChatTarget(ClientID, "You can't kick admins");
			char aBufKick[128];
			str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientID));
			SendChatTarget(KickID, aBufKick);
			return;
		}

		Server()->AddAccusation(ClientID, KickID, aReason);
	}
	else
	{
		if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		{
			SendChatTarget(ClientID, "Spectators aren't allowed to start a vote.");
			return;
		}

		if(str_comp_nocase(pMsg->m_Type, "option") == 0)
		{
			// this vote is not a kick/ban or spectate vote

			int Authed = Server()->GetAuthedState(ClientID);
			CVoteOptionServer *pOption = m_pVoteOptionFirst;
			while(pOption) // loop through all option votes to find out which vote it is
			{
				if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0) // found out which vote it is
				{
					if(!Console()->LineIsValid(pOption->m_aCommand))
					{
						SendChatTarget(ClientID, "Invalid option");
						return;
					}
					OPTION_VOTE_TYPE OptionVoteType = GetOptionVoteType(pOption->m_aCommand);
					if (OptionVoteType & MAP_VOTE_BITS) // this is a map vote
					{
						if (OptionVoteType == SV_MAP || OptionVoteType == CHANGE_MAP)
						{
							// check if we are already playing on the map the user wants to vote
							char MapName[VOTE_CMD_LENGTH] = {0};
							GetMapNameFromCommand(MapName, pOption->m_aCommand);
							if (str_comp_nocase(MapName, g_Config.m_SvMap) == 0)
							{
								char aBufVoteMap[128];
								str_format(aBufVoteMap, sizeof(aBufVoteMap), "Server is already on map %s", MapName);
								SendChatTarget(ClientID, aBufVoteMap);
								return;
							}
						}

						int RoundCount = m_pController->GetRoundCount();
						if (m_pController->IsRoundEndTime())
							RoundCount++;
						if (g_Config.m_InfMinRoundsForMapVote > RoundCount && Server()->GetActivePlayerCount() > 1)
						{
							char aBufVoteMap[128];
							str_format(aBufVoteMap, sizeof(aBufVoteMap), "Each map must be played at least %i rounds before calling a map vote", g_Config.m_InfMinRoundsForMapVote);
							SendChatTarget(ClientID, aBufVoteMap);
							return;
						}
					}
					if((OptionVoteType == PLAY_MORE_VOTE_TYPE) || (OptionVoteType == QUEUED_VOTE))
					{
						// copy information to start a vote
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
								pOption->m_aDescription, aReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						break;
					}
					if (g_Config.m_InfMinPlayerNumberForMapVote <= 1 || OptionVoteType == OTHER_OPTION_VOTE_TYPE)
					{
						// (this is not a map vote) or ("InfMinPlayerNumberForMapVote <= 1" and we keep default behaviour)
						if(!m_pController->CanVote())
						{
							SendChatTarget(ClientID, "Votes are only allowed when the round start.");
							return;
						}
						
						// copy information to start a vote 
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
								pOption->m_aDescription, aReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						break;
					}

					if(OptionVoteType & MAP_VOTE_BITS)
					{
						// this vote is a map vote
						Server()->AddMapVote(ClientID, pOption->m_aCommand, aReason, pOption->m_aDescription);
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
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
					SendChatTarget(ClientID, aChatmsg);
					return;
				}
				else
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s'", Server()->ClientName(ClientID), pMsg->m_Value);
					str_format(aDesc, sizeof(aDesc), "%s", pMsg->m_Value);
					str_format(aCmd, sizeof(aCmd), "%s", pMsg->m_Value);
				}
			}
		}
		else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
		{
			if(!g_Config.m_SvVoteSpectate)
			{
				SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
				return;
			}

			int SpectateID = str_toint(pMsg->m_Value);
			if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
			{
				SendChatTarget(ClientID, "Invalid client id to move");
				return;
			}
			if(SpectateID == ClientID)
			{
				SendChatTarget(ClientID, "You can't move yourself");
				return;
			}
			if(!Server()->ReverseTranslate(SpectateID, ClientID))
			{
				return;
			}

			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), aReason);
			str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
			str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		}

		// Start a vote
		if(aCmd[0])
		{
			CallVote(ClientID, aDesc, aCmd, aReason, aChatmsg);
		}
	}
}

void CGameContext::CensorMessage(char *pCensoredMessage, const char *pMessage, int Size)
{
	str_copy(pCensoredMessage, pMessage, Size);
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];
	
	if(pPlayer && MsgID == NETMSGTYPE_CL_ISDDNETLEGACY)
	{
		int DDNetVersion = pUnpacker->GetInt();

		if(g_Config.m_SvBannedVersions[0] != '\0' && IsVersionBanned(DDNetVersion))
		{
			Server()->Kick(ClientID, "unsupported client");
		}

		Server()->SetClientDDNetVersion(ClientID, DDNetVersion);
	}

	//HACK: DDNet Client did something wrong that we can detect
	//Round and Score conditions are here only to prevent false-positif
	if(!pPlayer && Server()->GetClientNbRound(ClientID) == 0)
	{
		Server()->Kick(ClientID, "Kicked (is probably a dummy)");
		return;
	}

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed() > Server()->Tick())
				return;

/* INFECTION MODIFICATION START ***************************************/
			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;
			int Team = CGameContext::CHAT_ALL;
			if(pMsg->m_Team)
			{
				if(pPlayer->GetTeam() == TEAM_SPECTATORS) Team = CGameContext::CHAT_SPEC;
				else Team = (pPlayer->IsZombie() ? CGameContext::CHAT_RED : CGameContext::CHAT_BLUE);
			}
/* INFECTION MODIFICATION END *****************************************/
			
			// trim right and set maximum length to 271 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
 			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(Code > 0x20 && Code != 0xA0 && Code != 0x034F && (Code < 0x2000 || Code > 0x200F) && (Code < 0x2028 || Code > 0x202F) &&
					(Code < 0x205F || Code > 0x2064) && (Code < 0x206A || Code > 0x206F) && (Code < 0xFE00 || Code > 0xFE0F) &&
					Code != 0xFEFF && (Code < 0xFFF9 || Code > 0xFFFC))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 270)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
 			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 16 characters per second)
			if(Length == 0 || (g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed()*((15+Length)/16) > Server()->Tick()))
				return;

			pPlayer->m_LastChat = Server()->Tick();
			
			
/* INFECTION MODIFICATION START ***************************************/
			if(pMsg->m_pMessage[0] == '/')
			{
				if(str_comp_nocase_num(pMsg->m_pMessage + 1, "w ", 2) == 0)
				{
					char aWhisperMsg[256];
					str_copy(aWhisperMsg, pMsg->m_pMessage + 3, 256);
					Whisper(pPlayer->GetCID(), aWhisperMsg);
				}
				else if(str_comp_nocase_num(pMsg->m_pMessage + 1, "whisper ", 8) == 0)
				{
					char aWhisperMsg[256];
					str_copy(aWhisperMsg, pMsg->m_pMessage + 9, 256);
					Whisper(pPlayer->GetCID(), aWhisperMsg);
				}
				else if(str_comp_nocase_num(pMsg->m_pMessage + 1, "c ", 2) == 0)
				{
					char aWhisperMsg[256];
					str_copy(aWhisperMsg, pMsg->m_pMessage + 3, 256);
					Converse(pPlayer->GetCID(), aWhisperMsg);
				}
				else if(str_comp_nocase_num(pMsg->m_pMessage + 1, "converse ", 9) == 0)
				{
					char aWhisperMsg[256];
					str_copy(aWhisperMsg, pMsg->m_pMessage + 10, 256);
					Converse(pPlayer->GetCID(), aWhisperMsg);
				}
				else if(str_comp_num(pMsg->m_pMessage + 1, "msg ", 4) == 0)
				{
					PrivateMessage(pMsg->m_pMessage + 5, ClientID, (Team != CGameContext::CHAT_ALL));
				}
				else if(str_comp_num(pMsg->m_pMessage + 1, "mute ", 5) == 0)
				{
					MutePlayer(pMsg->m_pMessage + 6, ClientID);
				}
				else
				{
					switch(m_apPlayers[ClientID]->m_Authed)
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
					m_ChatResponseTargetID = ClientID;

					Console()->ExecuteLineFlag(pMsg->m_pMessage + 1, ClientID, (Team != CGameContext::CHAT_ALL), CFGFLAG_CHAT);

					m_ChatResponseTargetID = -1;
					Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				}
			}
			else
			{
				if(Server()->GetClientSession(ClientID) && Server()->GetClientSession(ClientID)->m_MuteTick > 0)
				{
					int Time = Server()->GetClientSession(ClientID)->m_MuteTick/Server()->TickSpeed();
					SendChatTarget_Localization(ClientID, CHATCATEGORY_ACCUSATION, _("You are muted for {sec:Duration}"), "Duration", &Time, NULL);
				}
				else
				{
					//Inverse order and add ligature for arabic
					dynamic_string Buffer;
					Buffer.copy(pMsg->m_pMessage);
					Server()->Localization()->ArabicShaping(Buffer);
					SendChat(ClientID, Team, Buffer.buffer());
				}
			}
/* INFECTION MODIFICATION END *****************************************/
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			OnCallVote(pRawMsg, ClientID);
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			if(m_VoteLanguageTick[ClientID] > 0)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;

				if(pMsg->m_Vote)
				{
					if(pMsg->m_Vote > 0)
					{
						SetClientLanguage(ClientID, m_VoteLanguage[ClientID]);
					}
					
					m_VoteLanguageTick[ClientID] = 0;
					
					CNetMsg_Sv_VoteSet Msg;
					Msg.m_Timeout = 0;
					Msg.m_pDescription = "";
					Msg.m_pReason = "";
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
				}
			}
			else if(m_VoteCloseTime && pPlayer->m_Vote == 0)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;

				pPlayer->m_Vote = pMsg->m_Vote;
				pPlayer->m_VotePos = ++m_VotePos;
				m_VoteUpdate = true;
			}
			else if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_PlayerFlags&PLAYERFLAG_SCOREBOARD)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;
				
				int ScoreMode = pPlayer->GetScoreMode();
				if(pMsg->m_Vote < 0) ScoreMode++;
				else ScoreMode--;
				
				if(ScoreMode < 0) ScoreMode = NB_PLAYERSCOREMODE-1;
				if(ScoreMode >= NB_PLAYERSCOREMODE) ScoreMode = 0;
				
				Server()->SetClientDefaultScoreMode(ClientID, ScoreMode);
				m_apPlayers[ClientID]->SetScoreMode(ScoreMode);
			}
			else
			{
				m_apPlayers[ClientID]->HookProtection(!m_apPlayers[ClientID]->HookProtectionEnabled(), false);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETTEAM && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

			if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam+Server()->TickSpeed()*3 > Server()->Tick()))
				return;

			if(pPlayer->m_TeamChangeTick > Server()->Tick())
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick())/Server()->TickSpeed();
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Time to wait before changing team: %02d:%02d", TimeLeft/60, TimeLeft%60);
				SendBroadcast(ClientID, aBuf, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
				return;
			}
			
/* INFECTION MODIFICATION START ***************************************/
			if(pMsg->m_Team == TEAM_SPECTATORS && !m_pController->CanJoinTeam(TEAM_SPECTATORS, ClientID))
			{
				SendBroadcast_Localization(ClientID, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("You can't join the spectators right now"), NULL);
				return;
			}
/* INFECTION MODIFICATION END *****************************************/

			// Switch team on given client and kill/respawn him
			if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
			{
				if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
				{
					pPlayer->m_LastSetTeam = Server()->Tick();
					if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
						m_VoteUpdate = true;
					m_pController->DoTeamChange(pPlayer, pMsg->m_Team);
					if (pPlayer->GetTeam() == TEAM_SPECTATORS) {
						AddSpectatorCID(ClientID);
					} else {
						RemoveSpectatorCID(ClientID);
					}
					(void)m_pController->CheckTeamBalance();
					pPlayer->m_TeamChangeTick = Server()->Tick();
				}
				else
					SendBroadcast(ClientID, "Teams must be balanced, please join other team", BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Server()->MaxClients()-g_Config.m_SvSpectatorSlots);
				SendBroadcast(ClientID, aBuf, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

			if(pPlayer->GetTeam() != TEAM_SPECTATORS || pPlayer->m_SpectatorID == pMsg->m_SpectatorID || ClientID == pMsg->m_SpectatorID ||
				(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()*3 > Server()->Tick()))
				return;

			pPlayer->m_LastSetSpectatorMode = Server()->Tick();
			if(pMsg->m_SpectatorID != SPEC_FREEVIEW && (!m_apPlayers[pMsg->m_SpectatorID] || m_apPlayers[pMsg->m_SpectatorID]->GetTeam() == TEAM_SPECTATORS))
				SendChatTarget(ClientID, "Invalid spectator id used");
			else
				pPlayer->m_SpectatorID = pMsg->m_SpectatorID;
		}
		else if (MsgID == NETMSGTYPE_CL_CHANGEINFO)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo + Server()->TickSpeed() * g_Config.m_SvInfoChangeDelay > Server()->Tick())
				return;

			CNetMsg_Cl_ChangeInfo *pMsg = (CNetMsg_Cl_ChangeInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set infos
			char aOldName[MAX_NAME_LENGTH];
			str_copy(aOldName, Server()->ClientName(ClientID), sizeof(aOldName));
			Server()->SetClientName(ClientID, pMsg->m_pName);
			if(str_comp(aOldName, Server()->ClientName(ClientID)) != 0)
			{
				SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} changed their name to {str:NewName}"), "PlayerName", aOldName, "NewName", Server()->ClientName(ClientID), NULL);
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "change_name previous='%s' now='%s'", aOldName, Server()->ClientName(ClientID));
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
			}
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
#ifndef CONF_FORCE_COUNTRY_BY_IP
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
#endif
		}
		else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastEmote = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
		}
		else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
		{
			if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastKill = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
		}
	}
	else
	{
		if(MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReady)
				return;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set start infos
/* INFECTION MODIFICATION START ***************************************/
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);

#ifdef CONF_GEOLOCATION
			std::string ip = Server()->GetClientIP(ClientID);
			int LocatedCountry = Geolocation::get_country_iso_numeric_code(ip);
#ifdef CONF_FORCE_COUNTRY_BY_IP
			Server()->SetClientCountry(ClientID, LocatedCountry);
#endif // CONF_FORCE_COUNTRY_BY_IP
#else
			int LocatedCountry = -1;
#endif // CONF_GEOLOCATION
			
			if(!Server()->GetClientMemory(ClientID, CLIENTMEMORY_LANGUAGESELECTION))
			{
				const char * const pLangFromClient = CLocalization::LanguageCodeByCountryCode(pMsg->m_Country);
				const char * const pLangForIp = CLocalization::LanguageCodeByCountryCode(LocatedCountry);

				const char * const pDefaultLang = "en";
				const char *pLangForVote = "";

				if(pLangFromClient[0] && (str_comp(pLangFromClient, pDefaultLang) != 0))
					pLangForVote = pLangFromClient;
				else if(pLangForIp[0] && (str_comp(pLangForIp, pDefaultLang) != 0))
					pLangForVote = pLangForIp;

				dbg_msg("lang", "init_language ClientID=%d, lang from flag: \"%s\", lang for IP: \"%s\"", ClientID, pLangFromClient, pLangForIp);

				SetClientLanguage(ClientID, pDefaultLang);

				if(pLangForVote[0])
				{
					CNetMsg_Sv_VoteSet Msg;
					Msg.m_Timeout = 10;
					Msg.m_pReason = "";
					str_copy(m_VoteLanguage[ClientID], pLangForVote, sizeof(m_VoteLanguage[ClientID]));
					Msg.m_pDescription = Server()->Localization()->Localize(m_VoteLanguage[ClientID], _("Switch language to english?"));
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
					m_VoteLanguageTick[ClientID] = 10*Server()->TickSpeed();
				}
				else
				{
					SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("You can change the language of this mod using the command /language."), NULL);
					SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("If your language is not available, you can help with translation (/help translate)."), NULL);
				}
				
				Server()->SetClientMemory(ClientID, CLIENTMEMORY_LANGUAGESELECTION, true);
			}
			
/* INFECTION MODIFICATION END *****************************************/

			// send vote options
			CNetMsg_Sv_VoteClearOptions ClearMsg;
			Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

			// begin sending vote options
			pPlayer->m_SendVoteIndex = 0;

			// send tuning parameters to client
			SendTuningParams(ClientID);

			// client is ready to enter
			pPlayer->m_IsReady = true;
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
		}
	}
}

/* DDNET MODIFICATION START *******************************************/
void CGameContext::ChatConsolePrintCallback(const char *pLine, void *pUser)
{
	CGameContext *pSelf = (CGameContext *)pUser;
	int ClientID = pSelf->m_ChatResponseTargetID;

	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	const char *pLineOrig = pLine;

	static volatile int ReentryGuard = 0;

	if(ReentryGuard)
		return;
	ReentryGuard++;

	if(*pLine == '[')
	do
		pLine++;
	while((pLine - 2 < pLineOrig || *(pLine - 2) != ':') && *pLine != 0); // remove the category (e.g. [Console]: No Such Command)

	pSelf->SendChatTarget(ClientID, pLine);

	ReentryGuard--;
}
/* DDNET MODIFICATION END *********************************************/

bool CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
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
	
	return true;
}

bool CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	//~ pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
	
	return true;
}

bool CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->ms_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
	
	return true;
}

bool CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pSelf->m_pController->IsGameOver())
		return true;

	pSelf->m_World.m_Paused ^= 1;
	
	return true;
}

bool CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
	
	return true;
}

bool CGameContext::ConSkipMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->SkipMap();
	
	return true;
}

bool CGameContext::ConQueueMap(IConsole::IResult *pResult, void *pUserData)
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
	return true;
}

bool CGameContext::ConAddMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pResult->NumArguments() != 1)
		return false;

	const char *pMapName = pResult->GetString(0);
	if(!str_utf8_check(pMapName))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid (non UTF-8) filename");
		return true;
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
					return true;
				}
			}
		}
	}

	char aBuf[256];
	if(!pSelf->MapExists(pMapName))
	{
		str_format(aBuf, sizeof(aBuf), "Unable to find map %s", pMapName);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

		return true;
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
		return true;
	}
	pData[i] = ' ';
	++i;
	str_copy(pData + i, pMapName, MaxSize - i);

	{
		str_format(aBuf, sizeof(aBuf), "Map %s added to the rotation list", pMapName);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

	return true;
}

bool CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments())
		pSelf->m_pController->DoWarmup(pResult->GetInteger(0));
	else
		pSelf->m_pController->StartRound();
	
	return true;
}

bool CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(-1, pResult->GetString(0), BROADCAST_PRIORITY_SERVERANNOUNCE, pSelf->Server()->TickSpeed()*3);
	
	return true;
}

bool CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChatTarget(-1, pResult->GetString(0));
	
	return true;
}

bool CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID])
		return true;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[ClientID], Team);
	(void)pSelf->m_pController->CheckTeamBalance();
	
	return true;
}

bool CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChatTarget(-1, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[i], Team, false);

	// TODO: Remove
	(void)pSelf->m_pController->CheckTeamBalance();
	
	return true;
}

bool CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return true;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return true;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return true;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return true;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
	
	return true;
}

bool CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return true;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
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
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
	
	return true;
}

bool CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
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
				str_format(aBuf, sizeof(aBuf), "admin forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand, -1, false);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return true;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return true;
		}

		if (!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf, -1, false);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf, -1, false);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return true;
		}

		str_format(aBuf, sizeof(aBuf), "admin moved '%s' to spectator (%s)", pSelf->Server()->ClientName(SpectateID), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf, -1, false);
	}
	
	return true;
}

bool CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
	
	return true;
}

bool CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return true;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "admin forced vote %s", pResult->GetString(0));
	pSelf->SendChatTarget(-1, aBuf);
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	
	return true;
}

bool CGameContext::ConStartFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->m_FunRoundConfigurations.empty())
	{
		int ClientID = pResult->GetClientID();
		const char *pErrorMessage = "Unable to start a fun round: rounds configuration is empty";
		if(ClientID >= 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", pErrorMessage);
		}
		else
		{
			pSelf->SendChatTarget(-1, pErrorMessage);
		}
		return false;
	}
	const int type = random_int(0, pSelf->m_FunRoundConfigurations.size() - 1);
	const FunRoundConfiguration &Configuration = pSelf->m_FunRoundConfigurations[type];
	return pSelf->StartFunRound(Configuration);
}

bool CGameContext::ConStartSpecialFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	FunRoundConfiguration Configuration;

	for (int argN = 0; argN < pResult->NumArguments(); ++argN)
	{
		const char *argument = pResult->GetString(argN);
		const int PlayerClass = CInfClassGameController::GetClassByName(argument);
		if ((PlayerClass > START_HUMANCLASS) && (PlayerClass < END_HUMANCLASS))
		{
			Configuration.HumanClass = PlayerClass;
		}
		if ((PlayerClass > START_INFECTEDCLASS) && (PlayerClass < END_INFECTEDCLASS))
		{
			Configuration.InfectedClass = PlayerClass;
		}
	}

	if (!Configuration.HumanClass || !Configuration.InfectedClass)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid special fun round configuration");
		return false;
	}
	return pSelf->StartFunRound(Configuration);
}

bool CGameContext::StartFunRound(const FunRoundConfiguration &Configuration)
{
	const char* title = g_Config.m_FunRoundTitle;
	char aBuf[256];

	if (m_FunRound) {
		str_format(aBuf, sizeof(aBuf), "%s is not over yet", title);
		SendChatTarget(-1, aBuf);
		return true;
	}
	else if (m_FunRoundsPassed >= g_Config.m_FunRoundLimit) {
		switch (g_Config.m_FunRoundLimit) {
			case 1: str_format(aBuf, sizeof(aBuf), "%s can be played only once per map", title); break;
			case 2: str_format(aBuf, sizeof(aBuf), "%s can be played only twice per map", title); break;
			default: str_format(aBuf, sizeof(aBuf), "%s can be played only %d times per map", title, g_Config.m_FunRoundLimit);
		}
		SendChatTarget(-1, aBuf);
		return true;
	}

	// Ugly cast for the sake of IServer cleanup. This method should be moved
	//  to the GameController so there will be no cast in some future.
	const CInfClassGameController *pInfGameController = static_cast<CInfClassGameController*>(m_pController);

	// zombies
	std::vector<int> infectedProbabilities;
	for (int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; PlayerClass++)
	{
		infectedProbabilities.push_back(pInfGameController->GetPlayerClassProbability(PlayerClass));
	}
	const auto extraConfigValues =
	{
		// The order is still important!
		g_Config.m_InfGhoulThreshold,
		g_Config.m_InfGhoulStomachSize,
	};
	for (const int &extraValue : extraConfigValues)
	{
		infectedProbabilities.push_back(extraValue);
	}

	// humans
	std::vector<int> humanAvailabilities;
	for (int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; PlayerClass++)
	{
		humanAvailabilities.push_back(pInfGameController->GetPlayerClassEnabled(PlayerClass));
	}

	std::vector<const char*> phrases = {
		", glhf!",
		", not ez!",
		" c:",
		" xd",
		", that's gg",
		", good luck!"
	};
	const char* random_phrase = phrases[random_int(0, phrases.size()-1)];
	SetProbabilities(std::vector<int>());
	SetAvailabilities(std::vector<int>());
	g_Config.m_InfGhoulStomachSize = g_Config.m_FunRoundGhoulStomachSize;

	Server()->SetPlayerClassEnabled(Configuration.HumanClass, true);
	Server()->SetPlayerClassProbability(Configuration.InfectedClass, 100);
	const char *HumanClassText = CInfClassGameController::GetClassPluralDisplayName(Configuration.HumanClass);
	const char *InfectedClassText = CInfClassGameController::GetClassPluralDisplayName(Configuration.InfectedClass);

	str_format(aBuf, sizeof(aBuf), "%s! %s vs %s%s", title, InfectedClassText, HumanClassText, random_phrase);

	m_FunRound = true;
	m_pController->StartRound();
	CreateSoundGlobal(SOUND_CTF_CAPTURE);
	SendChatTarget(-1, aBuf);
	m_DefaultAvailabilities = humanAvailabilities;
	m_DefaultProbabilities = infectedProbabilities;
	
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CPlayer *pPlayer = m_apPlayers[i];
		if(!pPlayer)
			continue;

		pPlayer->SetClass(Configuration.HumanClass);
	}

	return true;
}

void CGameContext::EndFunRound()
{
	SetAvailabilities(m_DefaultAvailabilities);
	SetProbabilities(m_DefaultProbabilities);
	m_FunRound = false;
	m_FunRoundsPassed++;
}

bool CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
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
	
	return true;
}


/* INFECTION MODIFICATION START ***************************************/

bool CGameContext::ConVersion(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info",
		"InfectionClass Mod. Version: " GAME_VERSION);

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info",
		"Compiled: " LAST_COMPILE_DATE);

	if(GIT_SHORTREV_HASH)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Git revision hash: %s", GIT_SHORTREV_HASH);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", aBuf);
	}

	return true;
}

bool CGameContext::ConCredits(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int ClientID = pResult->GetClientID();
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();

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
	pSelf->SendMOTD(ClientID, Buffer.buffer());

	return true;
}

bool CGameContext::ConAbout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	return pSelf->ConAbout(pResult);
}

bool CGameContext::ConAbout(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetClientID();
	const char* pLanguage = m_apPlayers[ClientID]->GetLanguage();

	dynamic_string Buffer;
	Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass, by necropotame (version {str:VersionCode})"), "VersionCode", GAME_VERSION, NULL);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
	Buffer.clear();

	Server()->Localization()->Format_L(Buffer, pLanguage, _("Server version from {str:ServerCompileDate} "), "ServerCompileDate", LAST_COMPILE_DATE, NULL);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
	Buffer.clear();

	if(GIT_SHORTREV_HASH)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Git revision hash: %s", GIT_SHORTREV_HASH);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", aBuf);
	}

	const char *pSourceUrl = Config()->m_AboutSourceUrl;
	if(pSourceUrl[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Sources: {str:SourceUrl} "), "SourceUrl",
			pSourceUrl, NULL
		);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
		Buffer.clear();
	}

	if(Config()->m_AboutContactsDiscord[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Discord: {str:Url}"), "Url",
			Config()->m_AboutContactsDiscord, nullptr);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
		Buffer.clear();
	}
	if(Config()->m_AboutContactsTelegram[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Telegram: {str:Url}"), "Url",
			Config()->m_AboutContactsTelegram, nullptr);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
		Buffer.clear();
	}
	if(Config()->m_AboutContactsMatrix[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Matrix room: {str:Url}"), "Url",
			Config()->m_AboutContactsMatrix, nullptr);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
		Buffer.clear();
	}
	if(Config()->m_AboutTranslationUrl[0])
	{
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Translation project: {str:Url}"), "Url",
			Config()->m_AboutTranslationUrl, nullptr);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
		Buffer.clear();
	}

	Server()->Localization()->Format_L(Buffer, pLanguage, _("See also: /credits"), nullptr);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
	Buffer.clear();

	return true;
}

bool CGameContext::PrivateMessage(const char* pStr, int ClientID, bool TeamChat)
{	
	if(Server()->GetClientSession(ClientID) && Server()->GetClientSession(ClientID)->m_MuteTick > 0)
	{
		int Time = Server()->GetClientSession(ClientID)->m_MuteTick/Server()->TickSpeed();
		SendChatTarget_Localization(ClientID, CHATCATEGORY_ACCUSATION, _("You are muted for {sec:Duration}"), "Duration", &Time, NULL);
		return false;
	}
	
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
		SendChatTarget(ClientID, "Usage: /msg <username or group> <message>");
		SendChatTarget(ClientID, "Send a private message to a player or a group of players");
		SendChatTarget(ClientID, "Available groups: !near, !engineer, !soldier, ...");
		return true;
	}
	
	dynamic_string FinalMessage;
	int TextIter = 0;
	
	
	bool CheckDistance = false;
	vec2 CheckDistancePos = vec2(0.0f, 0.0f);
	
	int CheckTeam = -1;
	int CheckClass = -1;
#ifdef CONF_SQL
	int CheckLevel = SQL_USERLEVEL_NORMAL;
#endif
	
	if(TeamChat && m_apPlayers[ClientID])
	{
		CheckTeam = true;
		if(m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
			CheckTeam = TEAM_SPECTATORS;
		if(m_apPlayers[ClientID]->IsZombie())
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
				if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
				{
					CheckDistance = true;
					CheckDistancePos = m_apPlayers[ClientID]->GetCharacter()->m_Pos;
					str_copy(aChatTitle, "near", sizeof(aChatTitle));
				}
			}
#ifdef CONF_SQL
			else if(str_comp(aNameFound, "!mod") == 0)
			{
				if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
				{
					CheckLevel = SQL_USERLEVEL_MOD;
					CheckDistancePos = m_apPlayers[ClientID]->GetCharacter()->m_Pos;
					str_copy(aChatTitle, "moderators", sizeof(aChatTitle));
				}
			}
#endif
			else if(str_comp(aNameFound, "!engineer") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_ENGINEER;
				str_copy(aChatTitle, "engineer", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!soldier ") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SOLDIER;
				str_copy(aChatTitle, "soldier", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!scientist") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SCIENTIST;
				str_copy(aChatTitle, "scientist", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!biologist") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_BIOLOGIST;
				str_copy(aChatTitle, "biologist", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!looper") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_LOOPER;
				str_copy(aChatTitle, "looper", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!medic") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_MEDIC;
				str_copy(aChatTitle, "medic", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!hero") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_HERO;
				str_copy(aChatTitle, "hero", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ninja") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_NINJA;
				str_copy(aChatTitle, "ninja", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!mercenary") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_MERCENARY;
				str_copy(aChatTitle, "mercenary", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!sniper") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SNIPER;
				str_copy(aChatTitle, "sniper", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!smoker") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SMOKER;
				str_copy(aChatTitle, "smoker", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!hunter") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_HUNTER;
				str_copy(aChatTitle, "hunter", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!bat") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_BAT;
				str_copy(aChatTitle, "bat", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!boomer") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_BOOMER;
				str_copy(aChatTitle, "boomer", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!spider") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SPIDER;
				str_copy(aChatTitle, "spider", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ghost") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_GHOST;
				str_copy(aChatTitle, "ghost", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!ghoul") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_GHOUL;
				str_copy(aChatTitle, "ghoul", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!slug") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_SLUG;
				str_copy(aChatTitle, "slug", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!undead") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_UNDEAD;
				str_copy(aChatTitle, "undead", sizeof(aChatTitle));
			}
			else if(str_comp(aNameFound, "!witch") == 0 && m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetCharacter())
			{
				CheckClass = PLAYERCLASS_WITCH;
				str_copy(aChatTitle, "witch", sizeof(aChatTitle));
			}
			else
			{
				for(int i=0; i<MAX_CLIENTS; i++)
				{
					if(m_apPlayers[i] && str_comp(Server()->ClientName(i), aNameFound) == 0)
					{
						const char *pMessage = pStr[c] == 0 ? &pStr[c] : &pStr[c + 1];
						WhisperID(ClientID, i, pMessage);
						return true;
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
		SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("No player was found with this name"));
		return true;
	}
	
	pStr += c;
	while(*pStr == ' ')
		pStr++;
	
	dynamic_string Buffer;
	Buffer.copy(pStr);
	Server()->Localization()->ArabicShaping(Buffer);
	
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = (TeamChat ? 1 : 0);
	Msg.m_ClientID = ClientID;
	
	int NumPlayerFound = 0;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && !CGameContext::m_ClientMuted[i][ClientID])
		{
			if(i != ClientID)
			{
				if(CheckTeam >= 0)
				{
					if(CheckTeam == TEAM_SPECTATORS && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
						continue;
					else if(CheckTeam == TEAM_RED && m_apPlayers[i]->IsHuman())
						continue;
					else if(CheckTeam == TEAM_BLUE && m_apPlayers[i]->IsZombie())
						continue;
				}
				
#ifdef CONF_SQL
				if(Server()->GetUserLevel(i) < CheckLevel)
					continue;
#endif
				
				if(CheckClass >= 0 && !(m_apPlayers[i]->GetClass() == CheckClass))
					continue;
				
				if(CheckDistance && !(m_apPlayers[i]->GetCharacter() && distance(m_apPlayers[i]->GetCharacter()->m_Pos, CheckDistancePos) < 1000.0f))
					continue;
			}
			
			FinalMessage.clear();
			TextIter = 0;
			if(i == ClientID)
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
	
	return true;
}

void CGameContext::MutePlayer(const char* pStr, int ClientID)
{
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && str_comp(Server()->ClientName(i), pStr) == 0)
		{
			CGameContext::m_ClientMuted[ClientID][i] = !CGameContext::m_ClientMuted[ClientID][i];
			
			if(CGameContext::m_ClientMuted[ClientID][i])
				SendChatTarget(ClientID, "Player muted. Mute will persist until you or the muted player disconnects.");
			else
				SendChatTarget(ClientID, "Player unmuted. You can see their messages again.");
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

bool CGameContext::ConRegister(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	const char *pLogin = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);
	const char *pEmail = 0;
	
	if(pResult->NumArguments()>2)
		pEmail = pResult->GetString(2);
	
	pSelf->Server()->Register(ClientID, pLogin, pPassword, pEmail);
	
	return true;
}

bool CGameContext::ConLogin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	const char *pLogin = pResult->GetString(0);
	const char *pPassword = pResult->GetString(1);
	pSelf->Server()->Login(ClientID, pLogin, pPassword);
	
	return true;
}

bool CGameContext::ConLogout(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	pSelf->Server()->Logout(ClientID);
	
	return true;
}

#ifdef CONF_SQL

bool CGameContext::ConSetEmail(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	const char *pEmail = pResult->GetString(0);
	
	pSelf->Server()->SetEmail(ClientID, pEmail);
	
	return true;
}

bool CGameContext::ConChallenge(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	pSelf->Server()->ShowChallenge(ClientID);
	
	return true;
}

bool CGameContext::ConTop10(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "looper") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_LOOPER_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_HERO_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_SLUG_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_WITCH_SCORE);
	}
	else
		pSelf->Server()->ShowTop10(ClientID, SQL_SCORETYPE_ROUND_SCORE);
	
	return true;
}

bool CGameContext::ConRank(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "looper") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_LOOPER_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_HERO_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_SLUG_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_WITCH_SCORE);
	
	}
	else
		pSelf->Server()->ShowRank(ClientID, SQL_SCORETYPE_ROUND_SCORE);
	
	return true;
}

bool CGameContext::ConGoal(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	if(pResult->NumArguments()>0)
	{
		const char* pArg = pResult->GetString(0);
		
		if(str_comp_nocase(pArg, "engineer") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_ENGINEER_SCORE);
		else if(str_comp_nocase(pArg, "soldier") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SOLDIER_SCORE);
		else if(str_comp_nocase(pArg, "scientist") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SCIENTIST_SCORE);
		else if(str_comp_nocase(pArg, "biologist") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_BIOLOGIST_SCORE);
		else if(str_comp_nocase(pArg, "looper") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_LOOPER_SCORE);
		else if(str_comp_nocase(pArg, "medic") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_MEDIC_SCORE);
		else if(str_comp_nocase(pArg, "hero") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_HERO_SCORE);
		else if(str_comp_nocase(pArg, "ninja") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_NINJA_SCORE);
		else if(str_comp_nocase(pArg, "mercenary") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_MERCENARY_SCORE);
		else if(str_comp_nocase(pArg, "sniper") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SNIPER_SCORE);
		else if(str_comp_nocase(pArg, "smoker") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SMOKER_SCORE);
		else if(str_comp_nocase(pArg, "hunter") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_HUNTER_SCORE);
		else if(str_comp_nocase(pArg, "boomer") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_BOOMER_SCORE);
		else if(str_comp_nocase(pArg, "ghost") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_GHOST_SCORE);
		else if(str_comp_nocase(pArg, "spider") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SPIDER_SCORE);
		else if(str_comp_nocase(pArg, "ghoul") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_GHOUL_SCORE);
		else if(str_comp_nocase(pArg, "slug") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_SLUG_SCORE);
		else if(str_comp_nocase(pArg, "undead") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_UNDEAD_SCORE);
		else if(str_comp_nocase(pArg, "witch") == 0)
			pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_WITCH_SCORE);
	}
	else
		pSelf->Server()->ShowGoal(ClientID, SQL_SCORETYPE_ROUND_SCORE);
	
	return true;
}

bool CGameContext::ConStats(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	if(pResult->NumArguments()>0)
	{
		int arg = pResult->GetInteger(0);
		pSelf->Server()->ShowStats(ClientID, arg);
	}
	else
		pSelf->Server()->ShowStats(ClientID, -1);
	
	return true;
}

#endif

bool CGameContext::ConHelp(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	return pSelf->ConHelp(pResult);
}

bool CGameContext::ConHelp(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetClientID();
	const char *pLanguage = m_apPlayers[ClientID]->GetLanguage();
	const char *pHelpPage = (pResult->NumArguments()>0) ? pResult->GetString(0) : nullptr;

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
		int PlayerClass = CInfClassGameController::GetClassByName(pHelpPage, &Ok);
		if(Ok)
		{
			WriteClassHelpPage(&Buffer, pLanguage, static_cast<PLAYERCLASS>(PlayerClass));
		}
	}

	if(Buffer.empty())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", Server()->Localization()->Localize(pLanguage, _("Choose a help page with /help <page>")));
		
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Available help pages: {str:PageList}"),
			"PageList", "game, translate, msg, taxi",
			NULL
		);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", Buffer.buffer());
		
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", "engineer, soldier, scientist, biologist, looper, medic, hero, ninja, mercenary, sniper, whitehole");
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", "smoker, hunter, bat, boomer, ghost, spider, ghoul, voodoo, undead, witch.");
	}
	else
	{
		SendMOTD(ClientID, Buffer.buffer());
	}

	return true;
}

bool CGameContext::WriteClassHelpPage(dynamic_string *pOutput, const char *pLanguage, PLAYERCLASS PlayerClass)
{
	dynamic_string &Buffer = *pOutput;

	Buffer.append("~~ ");
	Server()->Localization()->Format_L(Buffer, pLanguage, CInfClassGameController::GetClassDisplayName(PlayerClass), nullptr);
	Buffer.append(" ~~");

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

	switch(PlayerClass)
	{
	case PLAYERCLASS_INVALID:
	case PLAYERCLASS_NONE:
	case NB_PLAYERCLASS:
	case START_INFECTEDCLASS:
	case END_INFECTEDCLASS:
	// case NB_INFECTEDCLASS:
	case START_HUMANCLASS:
	case END_HUMANCLASS:
	// case NB_HUMANCLASS:
		return false;
		break;

	case PLAYERCLASS_MERCENARY:
		AddLine(_C("Mercenary", "The Mercenary can fly in the air using their machine gun."));
		AddLine(_C("Mercenary", "They can also create a powerful bomb with their hammer that can"
								" be charged by hitting it or with a laser rifle."));
		AddLine_Plural(g_Config.m_InfPoisonDamage,
					_CP("Mercenary",
						   "Mercenary can also throw poison grenades that deal one damage point and prevent the infected from healing.",
						   "Mercenary can also throw poison grenades that deal {int:NumDamagePoints} damage points and prevent the infected from healing."),
					   "NumDamagePoints", &g_Config.m_InfPoisonDamage);
		break;
	case PLAYERCLASS_MEDIC:
		AddLine(_C("Medic", "The Medic can protect humans with the hammer by giving them armor."));
		AddLine(_C("Medic", "Grenades with medicine give armor to everybody in their range,"
							" including Heroes and the Medic themself."));
		AddLine(_C("Medic", "Laser rifle revives the infected, but at the cost of 17 hp and armor."));
		AddLine(_C("Medic", "Medic also has a powerful shotgun that can knock back the infected."));
		break;
	case PLAYERCLASS_HERO:
		AddLine(_C("Hero", "The Hero has all standard weapons."));
		AddLine(_C("Hero", "The Hero has to find a flag only visible to them. Stand still to be pointed towards it."));
		ConLine(_C("Hero", "The flag gifts a health point, {int:NumArmorGift} armor and full ammo to all humans."),
				"NumArmorGift", &HeroNumArmorGift);
		ConLine(_C("Hero", "It fully heals the Hero and it can grant a turret which you can place down with the hammer."));
		ConLine(_C("Hero", "The gift to all humans is only applied when the flag is surrounded by hearts and armor."));
		ConLine(_C("Hero", "The Hero cannot be healed by a Medic, but it can withstand a hit from an infected."));
		break;
	case PLAYERCLASS_ENGINEER:
		AddLine(_C("Engineer", "The Engineer can build walls with the hammer to block the infected."));
		AddLine(_C("Engineer", "When an infected touches the wall, it dies."));
		AddLine(_C("Engineer", "The lifespan of a wall is {sec:LifeSpan}, and walls are limited to"
							   " one per player at the same time."),
				"LifeSpan", &g_Config.m_InfBarrierLifeSpan);
		break;
	case PLAYERCLASS_SOLDIER:
		AddLine(_C("Soldier", "The Soldier creates floating bombs with the hammer."));
		AddLine_Plural(g_Config.m_InfSoldierBombs,
					   _CP("Soldier",
						   "Each bomb can explode one time.",
						   "Each bomb can explode {int:NumBombs} times."),
					   "NumBombs", &g_Config.m_InfSoldierBombs);

		AddLine(_("Use the hammer to place the bomb and explode it multiple times."));
		break;
	case PLAYERCLASS_NINJA:
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
	case PLAYERCLASS_SNIPER:
		AddLine(_C("Sniper", "The Sniper can lock the position in mid-air for 15 seconds with the"
							 " hammer."));
		AddLine(_C("Sniper", "The locked position increases the Sniper's rifle damage from usual"
							 " 10-13 to 20 damage points."));
		AddLine(_C("Sniper", "They can also jump two times in the air."));
		break;
	case PLAYERCLASS_SCIENTIST:
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
	case PLAYERCLASS_BIOLOGIST:
		AddLine(_C("Biologist", "The Biologist has a shotgun with bouncing bullets and can create a"
								" spring laser trap by shooting with the laser rifle."));
		break;
	case PLAYERCLASS_LOOPER:
		AddLine(_C("Looper", "The Looper has a laser wall that slows down the infected and a"
							 " low-range laser rifle with a high fire rate."));
		AddLine(_C("Looper", "They can also jump two times in the air."));
		break;
	case PLAYERCLASS_SMOKER:
		AddLine(_C("Smoker", "Smoker has a powerful hook that hurts humans and sucks their blood,"
							 " restoring the Smoker's health."));
		AddLine(_C("Smoker", "It can also infect humans and heal the infected with the hammer."));
		break;
	case PLAYERCLASS_BOOMER:
		AddLine(_C("Boomer", "The Boomer explodes when it attacks."));
		AddLine(_C("Boomer", "All humans affected by the explosion become infected."));
		AddLine(_C("Boomer", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case PLAYERCLASS_HUNTER:
		AddLine(_C("Hunter", "The Hunter can jump two times in the air and has some resistance to"
							 " knock-backs."));
		AddLine(_C("Hunter", "It can infect humans and heal the infected with the hammer."));
		AddLine(_C("Hunter", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case PLAYERCLASS_BAT:
		AddLine(_C("Bat", "Bat can jump endlessly in the air but it cannot infect humans."));
		AddLine(_C("Bat", "Instead, it can hammer humans to reduce their health or even kill them."));
		AddLine(_C("Bat", "The hammer is also useful for healing the infected."));
		AddLine(_C("Bat", "It can also inflict 1 damage point per second by hooking humans, which"
						  " sucks their blood, restoring the Bat's health."));
		break;
	case PLAYERCLASS_GHOST:
		AddLine(_C("Ghost", "The Ghost is invisible until a human comes nearby, it takes damage,"
							" or it uses the hammer."));
		AddLine(_C("Ghost", "It can infect humans and heal the infected with the hammer."));
		AddLine(_C("Ghost", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case PLAYERCLASS_SPIDER:
		AddLine(_C("Spider", "The Spider has a web hook that automatically grabs any human touching it."));
		AddLine(_C("Spider", "The web hook mode can be toggled by switching the weapon."));
		AddLine(_C("Spider", "In both modes the hook inflicts 1 damage point per second and can"
							 " grab a human for longer."));
		break;
	case PLAYERCLASS_GHOUL:
		AddLine(_C("Ghoul", "The Ghoul can devour anything that has died nearby, which makes it"
							" stronger, faster and more resistant."));
		AddLine(_C("Ghoul", "It digests the fodder over time, going back to the normal state."
							" Some nourishment is also lost on death."));
		AddLine(_C("Ghoul", "Ghoul can infect humans and heal the infected with the hammer."));
		AddLine(_C("Ghoul", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case PLAYERCLASS_SLUG:
		AddLine(_C("Slug", "The Slug can make the ground and walls toxic by spreading slime with the hammer."));
		AddLine(_C("Slug", "The slime heals the infected and deals damage to humans."));
		AddLine(_C("Slug", "Slug can infect humans and heal the infected with the hammer."));
		AddLine(_C("Slug", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case PLAYERCLASS_VOODOO:
		AddLine(_C("Voodoo", "The Voodoo does not die immediately when killed but instead enters"
							 " Spirit mode for a short time."));
		AddLine(_C("Voodoo", "While in Spirit mode it cannot be killed. When the time is up it finally dies."));
		AddLine(_C("Voodoo", "Voodoo can infect humans and heal the infected with the hammer."));
		AddLine(_C("Voodoo", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case PLAYERCLASS_WITCH:
		AddLine(_C("Witch", "The Witch can provide a spawn point for the infected."));
		AddLine(_C("Witch", "If the Witch dies, it disappears and is replaced by another class."));
		AddLine(_C("Witch", "Witch can infect humans and heal the infected with the hammer."));
		AddLine(_C("Witch", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case PLAYERCLASS_UNDEAD:
		AddLine(_C("Undead", "The Undead cannot die. Instead of dying, it gets frozen for 10 seconds."));
		AddLine(_C("Undead", "If an infected heals it, the freeze effect disappears."));
		AddLine(_C("Undead", "Undead can infect humans and heal the infected with the hammer."));
		AddLine(_C("Undead", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	}

	return true;
}

bool CGameContext::ConAntiPing(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	int Arg = pResult->GetInteger(0);

	if(Arg > 0)
		pSelf->Server()->SetClientAntiPing(ClientID, 1);
	else
		pSelf->Server()->SetClientAntiPing(ClientID, 0);
	
	return true;
}

bool CGameContext::ConAlwaysRandom(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	int Arg = pResult->GetInteger(0);

	if(Arg > 0)
	{
		pSelf->Server()->SetClientAlwaysRandom(ClientID, 1);
		const char* pTxtAlwaysRandomOn = pSelf->Server()->Localization()->Localize(pLanguage, _("A random class will be automatically attributed to you when rounds start"));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "alwaysrandom", pTxtAlwaysRandomOn);		
	}
	else
	{
		pSelf->Server()->SetClientAlwaysRandom(ClientID, 0);
		const char* pTxtAlwaysRandomOff = pSelf->Server()->Localization()->Localize(pLanguage, _("The class selector will be displayed when rounds start"));
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "alwaysrandom", pTxtAlwaysRandomOff);		
	}
	
	return true;
}

bool CGameContext::ConLanguage(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	
	int ClientID = pResult->GetClientID();
	
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
		pSelf->SetClientLanguage(ClientID, aFinalLanguageCode);
	}
	else
	{
		const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
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
	
	return true;
}

bool CGameContext::ConCmdList(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
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
			
	pSelf->SendMOTD(ClientID, Buffer.buffer());
	
	return true;
}

bool CGameContext::ConChangeLog(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	return pSelf->ConChangeLog(pResult);
}

bool CGameContext::ConChangeLog(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetClientID();

	if(m_aChangeLogEntries.Size() == 0)
	{
		SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("ChangeLog is not provided"), nullptr);
		return true;
	}

	int PageNumber = pResult->GetInteger(0);
	if(PageNumber <= 0)
	{
		PageNumber = 1;
	}
	int PagesInTotal = m_aChangeLogPageIndices.Size();
	if(PageNumber > PagesInTotal)
	{
		SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("ChangeLog page {int:PageNumber} is not available"),
			"PageNumber", &PageNumber,
			nullptr);
		return true;
	}

	int PageIndex = PageNumber - 1;
	int From = m_aChangeLogPageIndices.At(PageIndex);
	int To = (PageIndex + 1) < m_aChangeLogPageIndices.Size() ? m_aChangeLogPageIndices.At(PageIndex + 1) : m_aChangeLogEntries.Size();

	for(int i = From; i < To; ++i)
	{
		const char *pText = m_aChangeLogEntries.At(i);
		SendChatTarget(ClientID, pText);
	}

	if(PageNumber != PagesInTotal)
	{
		int NextPage = PageNumber + 1;

		SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT,
			_("(page {int:PageNumber}/{int:PagesInTotal}, see /changelog {int:NextPage})"),
			"PageNumber", &PageNumber,
			"PagesInTotal", &PagesInTotal,
			"NextPage", &NextPage,
			nullptr);
	}

	return true;
}

bool CGameContext::ConReloadChangeLog(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->ReloadChangelog();
	return true;
}

bool CGameContext::ConClearFunRounds(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_FunRoundConfigurations.clear();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "fun rounds cleared");
	return true;
}

bool CGameContext::ConAddFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	FunRoundConfiguration Settings;

	for (int argN = 0; argN < pResult->NumArguments(); ++argN)
	{
		const char *argument = pResult->GetString(argN);
		const int PlayerClass = CInfClassGameController::GetClassByName(argument);
		if ((PlayerClass > START_HUMANCLASS) && (PlayerClass < END_HUMANCLASS))
		{
			Settings.HumanClass = PlayerClass;
		}
		if ((PlayerClass > START_INFECTEDCLASS) && (PlayerClass < END_INFECTEDCLASS))
		{
			Settings.InfectedClass = PlayerClass;
		}
	}

	if (!Settings.HumanClass || !Settings.InfectedClass)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid special fun round configuration");
		return false;
	}
	else
	{
		char aBuf[256];
		const char *HumanClassText = CInfClassGameController::GetClassPluralDisplayName(Settings.HumanClass);
		const char *InfectedClassText = CInfClassGameController::GetClassPluralDisplayName(Settings.InfectedClass);
		str_format(aBuf, sizeof(aBuf), "Added fun round: %s vs %s", InfectedClassText, HumanClassText);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

	pSelf->m_FunRoundConfigurations.push_back(Settings);

	return true;
}
/* INFECTION MODIFICATION END *****************************************/

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = &g_Config;
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	m_ChatPrintCBIndex = Console()->RegisterPrintCallback(IConsole::OUTPUT_LEVEL_STANDARD, ChatConsolePrintCallback, this);

	Console()->Register("tune", "s<param> i<value>", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");

	Console()->Register("pause", "", CFGFLAG_SERVER, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("skip_map", "", CFGFLAG_SERVER|CFGFLAG_STORE, ConSkipMap, this, "Change map to the next in the rotation");
	Console()->Register("queue_map", "s", CFGFLAG_SERVER|CFGFLAG_STORE, ConQueueMap, this, "Set the next map");
	Console()->Register("add_map", "s", CFGFLAG_SERVER|CFGFLAG_STORE, ConAddMap, this, "Add a map to the maps rotation list");
	Console()->Register("restart", "?i<sec>", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r<message>", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "ss?r", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");
	Console()->Register("start_fun_round", "", CFGFLAG_SERVER, ConStartFunRound, this, "Start fun round");
	Console()->Register("start_special_fun_round", "ss?s", CFGFLAG_SERVER, ConStartSpecialFunRound, this, "Start fun round");
	Console()->Register("clear_fun_rounds", "", CFGFLAG_SERVER, ConClearFunRounds, this, "Start fun round");
	Console()->Register("add_fun_round", "ss?s", CFGFLAG_SERVER, ConAddFunRound, this, "Start fun round");
	
/* INFECTION MODIFICATION START ***************************************/
	
	//Chat Command
	Console()->Register("version", "", CFGFLAG_SERVER, ConVersion, this, "Display information about the server version and build");

	Console()->Register("credits", "", CFGFLAG_CHAT | CFGFLAG_USER, ConCredits, this, "Shows the credits of the mod");
	Console()->Register("about", "", CFGFLAG_CHAT|CFGFLAG_USER, ConAbout, this, "Display information about the mod");
	Console()->Register("info", "", CFGFLAG_CHAT|CFGFLAG_USER, ConAbout, this, "Display information about the mod");
	Console()->Register("register", "s<username> s<password> ?s<email>", CFGFLAG_CHAT|CFGFLAG_USER, ConRegister, this, "Create an account");
	Console()->Register("login", "s<username> s<password>", CFGFLAG_CHAT|CFGFLAG_USER, ConLogin, this, "Login to an account");
	Console()->Register("logout", "", CFGFLAG_CHAT|CFGFLAG_USER, ConLogout, this, "Logout");
#ifdef CONF_SQL
	Console()->Register("setemail", "s<email>", CFGFLAG_CHAT|CFGFLAG_USER, ConSetEmail, this, "Change your email");
	
	Console()->Register("top10", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConTop10, this, "Show the top 10 on the current map");
	Console()->Register("challenge", "", CFGFLAG_CHAT|CFGFLAG_USER, ConChallenge, this, "Show the current winner of the challenge");
	Console()->Register("rank", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConRank, this, "Show your rank");
	Console()->Register("goal", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConGoal, this, "Show your goal");
	Console()->Register("stats", "i", CFGFLAG_CHAT|CFGFLAG_USER, ConStats, this, "Show stats by id");
#endif
	Console()->Register("help", "?s<page>", CFGFLAG_CHAT|CFGFLAG_USER, ConHelp, this, "Display help");
	Console()->Register("reload_changelog", "?i<page>", CFGFLAG_SERVER, ConReloadChangeLog, this, "Reload the changelog file");
	Console()->Register("changelog", "?i<page>", CFGFLAG_CHAT|CFGFLAG_USER, ConChangeLog, this, "Display a changelog page");
	Console()->Register("alwaysrandom", "i<0|1>", CFGFLAG_CHAT|CFGFLAG_USER, ConAlwaysRandom, this, "Display information about the mod");
	Console()->Register("antiping", "i<0|1>", CFGFLAG_CHAT|CFGFLAG_USER, ConAntiPing, this, "Try to improve your ping");
	Console()->Register("language", "s<en|fr|nl|de|bg|sr-Latn|hr|cs|pl|uk|ru|el|la|it|es|pt|hu|ar|tr|sah|fa|tl|zh-CN|ja>", CFGFLAG_CHAT|CFGFLAG_USER, ConLanguage, this, "Set the language");
	Console()->Register("lang", "s<en|fr|nl|de|bg|sr-Latn|hr|cs|pl|uk|ru|el|la|it|es|pt|hu|ar|tr|sah|fa|tl|zh-CN|ja>", CFGFLAG_CHAT|CFGFLAG_USER, ConLanguage, this, "Set the language");
	Console()->Register("cmdlist", "", CFGFLAG_CHAT|CFGFLAG_USER, ConCmdList, this, "List of commands");
/* INFECTION MODIFICATION END *****************************************/

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);

	InitGeolocation();
}

void CGameContext::OnInit(/*class IKernel *pKernel*/)
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = &g_Config;
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);
	
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		Server()->ResetClientMemoryAboutGame(i);
	}

	//if(!data) // only load once
		//data = load_data_from_memory(internal_data);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));
	
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_VoteLanguageTick[i] = 0;
		str_copy(m_VoteLanguage[i], "en", sizeof(m_VoteLanguage[i]));				
	}

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);
	
	//Get zones
	m_ZoneHandle_icDamage = m_Collision.GetZoneHandle("icDamage");
	m_ZoneHandle_icTeleport = m_Collision.GetZoneHandle("icTele");
	m_ZoneHandle_icBonus = m_Collision.GetZoneHandle("icBonus");

	// reset everything here
	//world = new GAMEWORLD;
	//players = new CPlayer[MAX_CLIENTS];

	Console()->ExecuteFile(g_Config.m_SvResetFile);

	// select gametype
	m_pController = new CInfClassGameController(this);

	m_pController->RegisterChatCommands(Console());

	// setup core world
	//for(int i = 0; i < MAX_CLIENTS; i++)
	//	game.players[i].core.world = &game.world.core;

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

	{
		//Open file
		char *pMapShortName = &g_Config.m_SvMap[0];
		char MapCfgFilename[512];
		str_format(MapCfgFilename, sizeof(MapCfgFilename), "maps/%s.cfg", pMapShortName);
		Console()->ExecuteFile(MapCfgFilename);
	}

	//game.world.insert_entity(game.Controller);

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS-i-1);
		}
	}
#endif

	InitChangelog();
}

void CGameContext::OnStartRound()
{
	m_HeroGiftCooldown = 0;
}

void CGameContext::OnShutdown()
{
	//reset votes.
	EndVote();

	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
		int *pParams = (int *)&m_Tuning;
		for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
			Msg.AddInt(pParams[i]);
		Server()->SendMsg(&Msg, MSGFLAG_RECORD|MSGFLAG_NOSEND, ClientID);
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

/* INFECTION MODIFICATION START ***************************************/
	//Snap laser dots
	for(int i=0; i < m_LaserDots.size(); i++)
	{
		if(ClientID >= 0)
		{
			vec2 CheckPos = (m_LaserDots[i].m_Pos0 + m_LaserDots[i].m_Pos1)*0.5f;
			float dx = m_apPlayers[ClientID]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientID]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientID]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}
		
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_LaserDots[i].m_SnapID, sizeof(CNetObj_Laser)));
		if(pObj)
		{
			pObj->m_X = (int)m_LaserDots[i].m_Pos1.x;
			pObj->m_Y = (int)m_LaserDots[i].m_Pos1.y;
			pObj->m_FromX = (int)m_LaserDots[i].m_Pos0.x;
			pObj->m_FromY = (int)m_LaserDots[i].m_Pos0.y;
			pObj->m_StartTick = Server()->Tick();
		}
	}
	for(int i=0; i < m_HammerDots.size(); i++)
	{
		if(ClientID >= 0)
		{
			vec2 CheckPos = m_HammerDots[i].m_Pos;
			float dx = m_apPlayers[ClientID]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientID]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientID]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}
		
		CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_HammerDots[i].m_SnapID, sizeof(CNetObj_Projectile)));
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
		if(ClientID >= 0)
		{
			vec2 CheckPos = m_LoveDots[i].m_Pos;
			float dx = m_apPlayers[ClientID]->m_ViewPos.x-CheckPos.x;
			float dy = m_apPlayers[ClientID]->m_ViewPos.y-CheckPos.y;
			if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
				continue;
			if(distance(m_apPlayers[ClientID]->m_ViewPos, CheckPos) > 1100.0f)
				continue;
		}
		
		CNetObj_Pickup *pObj = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_LoveDots[i].m_SnapID, sizeof(CNetObj_Pickup)));
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
			m_apPlayers[i]->Snap(ClientID);
	}
}

void CGameContext::FlagCollected()
{
	float t = (8-Server()->GetActivePlayerCount()) / 8.0f;
	if (t < 0.0f) 
		t = 0.0f;

	m_HeroGiftCooldown = Server()->TickSpeed() * (15+(120*t));
}

void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientBot(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->IsBot();
}

bool CGameContext::IsClientReady(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

const char *CGameContext::GameType() const { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
const char *CGameContext::Version() const { return GAME_VERSION; }
const char *CGameContext::NetVersion() const { return GAME_NETVERSION; }



IGameServer *CreateGameServer() { return new CGameContext; }

void CGameContext::AddSpectatorCID(int ClientID)
{
	Server()->RemoveMapVotesForID(ClientID);
	auto& vec = Server()->spectators_id;
	if(!(std::find(vec.begin(), vec.end(), ClientID) != vec.end()))
		vec.push_back(ClientID);
}

void CGameContext::RemoveSpectatorCID(int ClientID) {
	auto& vec = Server()->spectators_id;
	vec.erase(std::remove(vec.begin(), vec.end(), ClientID), vec.end());
}

bool CGameContext::IsSpectatorCID(int ClientID) {
	auto& vec = Server()->spectators_id;
	return std::find(vec.begin(), vec.end(), ClientID) != vec.end();
}

bool CGameContext::IsVersionBanned(int Version)
{
	char aVersion[16];
	str_format(aVersion, sizeof(aVersion), "%d", Version);

	return str_in_list(g_Config.m_SvBannedVersions, ",", aVersion);
}

int CGameContext::GetClientVersion(int ClientID) const
{
	IServer::CClientInfo Info = {0};
	Server()->GetClientInfo(ClientID, &Info);
	return Info.m_DDNetVersion;
}

bool CGameContext::RateLimitPlayerVote(int ClientID)
{
	int64 Now = Server()->Tick();
	int64 TickSpeed = Server()->TickSpeed();
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry + TickSpeed * 3 > Now)
		return true;

	pPlayer->m_LastVoteTry = Now;
	if(m_VoteCloseTime)
	{
		SendChatTarget(ClientID, "Wait for current vote to end before calling a new one.");
		return true;
	}

	int TimeLeft = pPlayer->m_LastVoteCall + TickSpeed * g_Config.m_SvVoteDelay - Now;
	if(pPlayer->m_LastVoteCall && TimeLeft > 0)
	{
		char aChatmsg[64];
		str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote.", (int)(TimeLeft / TickSpeed) + 1);
		SendChatTarget(ClientID, aChatmsg);
		return true;
	}

	return false;
}

bool CheckClientID2(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	return true;
}

void CGameContext::Whisper(int ClientID, char *pStr)
{
	char *pName;
	char *pMessage;
	int Error = 0;

	pStr = str_skip_whitespaces(pStr);

	int Victim;

	// add token
	if(*pStr == '"')
	{
		pStr++;

		pName = pStr;
		char *pDst = pStr; // we might have to process escape data
		while(1)
		{
			if(pStr[0] == '"')
				break;
			else if(pStr[0] == '\\')
			{
				if(pStr[1] == '\\')
					pStr++; // skip due to escape
				else if(pStr[1] == '"')
					pStr++; // skip due to escape
			}
			else if(pStr[0] == 0)
				Error = 1;

			*pDst = *pStr;
			pDst++;
			pStr++;
		}

		// write null termination
		*pDst = 0;

		pStr++;

		for(Victim = 0; Victim < MAX_CLIENTS; Victim++)
			if(str_comp(pName, Server()->ClientName(Victim)) == 0)
				break;
	}
	else
	{
		pName = pStr;
		while(1)
		{
			if(pStr[0] == 0)
			{
				Error = 1;
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
		Error = 1;
	}

	*pStr = 0;
	pStr++;

	pMessage = pStr;

	char aBuf[256];

	if(Error)
	{
		str_format(aBuf, sizeof(aBuf), "Invalid whisper");
		SendChatTarget(ClientID, aBuf);
		return;
	}

	if(Victim >= MAX_CLIENTS || !CheckClientID2(Victim))
	{
		str_format(aBuf, sizeof(aBuf), "No player with name \"%s\" found", pName);
		SendChatTarget(ClientID, aBuf);
		return;
	}

	WhisperID(ClientID, Victim, pMessage);
}

void CGameContext::WhisperID(int ClientID, int VictimID, const char *pMessage)
{
	if(!CheckClientID2(ClientID))
		return;

	if(!CheckClientID2(VictimID))
		return;

	if(m_apPlayers[ClientID])
	{
		m_apPlayers[ClientID]->m_LastWhisperTo = VictimID;
		m_apPlayers[ClientID]->m_LastChat = Server()->Tick();
	}

	char aCensoredMessage[256];
	CensorMessage(aCensoredMessage, pMessage, sizeof(aCensoredMessage));

	char aBuf[256];

	if(GetClientVersion(ClientID) >= VERSION_DDNET_WHISPER)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = CHAT_WHISPER_SEND;
		Msg.m_ClientID = VictimID;
		Msg.m_pMessage = aCensoredMessage;

		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "[→ %s] %s", Server()->ClientName(VictimID), aCensoredMessage);
		SendChatTarget(ClientID, aBuf);
	}

	if(ClientID == VictimID)
	{
		return;
	}

	if(CGameContext::m_ClientMuted[VictimID][ClientID])
	{
		return;
	}

	if(GetClientVersion(VictimID) >= VERSION_DDNET_WHISPER)
	{
		CNetMsg_Sv_Chat Msg2;
		Msg2.m_Team = CHAT_WHISPER_RECV;
		Msg2.m_ClientID = ClientID;
		Msg2.m_pMessage = aCensoredMessage;

		Server()->SendPackMsg(&Msg2, MSGFLAG_VITAL | MSGFLAG_NORECORD, VictimID);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "[← %s] %s", Server()->ClientName(ClientID), aCensoredMessage);
		SendChatTarget(VictimID, aBuf);
	}
}

void CGameContext::Converse(int ClientID, const char *pStr)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if(!pPlayer)
		return;

	if(pPlayer->m_LastWhisperTo < 0)
	{
		SendChatTarget(ClientID, "You do not have an ongoing conversation. Whisper to someone to start one");
	}
	else
	{
		WhisperID(ClientID, pPlayer->m_LastWhisperTo, pStr);
	}
}
