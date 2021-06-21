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
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/gamecore.h>
#include <iostream>
#include <algorithm>

#include <game/server/infclass/entities/portal.h>
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
	m_TargetToKill = -1;
	m_TargetToKillCoolDown = 0;
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

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount)
{
	float a = 3 * 3.14159f / 2 + Angle;
	//float a = get_angle(dir);
	float s = a-pi/3;
	float e = a+pi/3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i+1)/float(Amount+2));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd));
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f*256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
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

void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, TAKEDAMAGEMODE TakeDamageMode, float DamageFactor)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	if (!NoDamage)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			if(!g_Config.m_InfShockwaveAffectHumans){
				if(apEnts[i]->GetPlayer() && apEnts[i]->GetPlayer()->GetCID() == Owner) {} //owner selfharm
				else if(apEnts[i]->IsHuman()) continue;// humans are not affected by force
			}
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			if(l)
				ForceDir = normalize(Diff);
			l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Dmg = 6 * l * DamageFactor;
			if((int)Dmg)
				apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, Owner, Weapon, TakeDamageMode);
		}

		for(CPortal* pPortal = (CPortal*) m_World.FindFirst(CGameWorld::ENTTYPE_PORTAL); pPortal; pPortal = (CPortal*) pPortal->TypeNext())
		{
			const float d = distance(Pos, pPortal->m_Pos);
			if(d > (pPortal->m_ProximityRadius + Radius))
				continue;

			const float l = 1-clamp((d-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			int Damage = 6 * l;
			if(Damage)
				pPortal->TakeDamage(Damage, Owner, Weapon, TakeDamageMode);
		}
	}
}

// Thanks to Stitch for the idea
void CGameContext::CreateExplosionDisk(vec2 Pos, float InnerRadius, float DamageRadius, int Damage, float Force, int Owner, int Weapon, TAKEDAMAGEMODE TakeDamageMode)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
	if(Damage > 0)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		int Num = m_World.FindEntities(Pos, DamageRadius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			if (Diff.x == 0.0f && Diff.y == 0.0f)
				Diff.y = -0.5f;
			vec2 ForceDir(0,1);
			float len = length(Diff);
			len = 1-clamp((len-InnerRadius)/(DamageRadius-InnerRadius), 0.0f, 1.0f);
			
			if(len)
				ForceDir = normalize(Diff);
			
			float DamageToDeal = 1 + ((Damage - 1) * len);
			apEnts[i]->TakeDamage(ForceDir*Force*len, DamageToDeal, Owner, Weapon, TakeDamageMode);
		}
	}
	
	float CircleLength = 2.0*pi*maximum(DamageRadius-135.0f, 0.0f);
	int NumSuroundingExplosions = CircleLength/32.0f;
	float AngleStart = random_float()*pi*2.0f;
	float AngleStep = pi*2.0f/static_cast<float>(NumSuroundingExplosions);
	for(int i=0; i<NumSuroundingExplosions; i++)
	{
		CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x + (DamageRadius-135.0f) * cos(AngleStart + i*AngleStep);
			pEvent->m_Y = (int)Pos.y + (DamageRadius-135.0f) * sin(AngleStart + i*AngleStep);
		}
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

void CGameContext::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int64 Mask)
{
	if (Sound < 0)
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
	if (Sound < 0)
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

	SendChat(-1, CGameContext::CHAT_ALL, pChatmsg);

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
			switch(Category)
			{
				case CHATCATEGORY_INFECTION:
					Buffer.append("☣ | ");
					break;
				case CHATCATEGORY_SCORE:
					Buffer.append("★ | ");
					break;
				case CHATCATEGORY_PLAYER:
					Buffer.append("♟ | ");
					break;
				case CHATCATEGORY_INFECTED:
					Buffer.append("⛃ | ");
					break;
				case CHATCATEGORY_HUMANS:
					Buffer.append("⛁ | ");
					break;
				case CHATCATEGORY_ACCUSATION:
					Buffer.append("☹ | ");
					break;
			}
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
			switch(Category)
			{
				case CHATCATEGORY_INFECTION:
					Buffer.append("☣ | ");
					break;
				case CHATCATEGORY_SCORE:
					Buffer.append("★ | ");
					break;
				case CHATCATEGORY_PLAYER:
					Buffer.append("♟ | ");
					break;
				case CHATCATEGORY_INFECTED:
					Buffer.append("⛃ | ");
					break;
				case CHATCATEGORY_HUMANS:
					Buffer.append("⛁ | ");
					break;
				case CHATCATEGORY_ACCUSATION:
					Buffer.append("☹ | ");
					break;
			}
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
	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
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
	
	//Target to kill
	if(m_TargetToKill >= 0 && (!m_apPlayers[m_TargetToKill] || !m_apPlayers[m_TargetToKill]->GetCharacter()))
	{
		m_TargetToKill = -1;
	}
	
	int LastTarget = -1;
	// Zombie is in InfecZone too long -> change target
	if(m_TargetToKill >= 0 && m_apPlayers[m_TargetToKill] && m_apPlayers[m_TargetToKill]->GetCharacter() && (m_apPlayers[m_TargetToKill]->GetCharacter()->GetInfZoneTick()*Server()->TickSpeed()) > 1000*g_Config.m_InfNinjaTargetAfkTime) 
	{
		LastTarget = m_TargetToKill;
		m_TargetToKill = -1;
	}
	
	if(m_HeroGiftCooldown > 0)
		m_HeroGiftCooldown--;

	if(m_TargetToKillCoolDown > 0)
		m_TargetToKillCoolDown--;
	
	if((m_TargetToKillCoolDown == 0 && m_TargetToKill == -1))
	{
		int m_aTargetList[MAX_CLIENTS];
		int NbTargets = 0;
		int infectedCount = 0;
		for(int i=0; i<MAX_CLIENTS; i++)
		{		
			if(m_apPlayers[i] && m_apPlayers[i]->IsZombie() && m_apPlayers[i]->GetClass() != PLAYERCLASS_UNDEAD)
			{
				if (m_apPlayers[i]->GetCharacter() && (m_apPlayers[i]->GetCharacter()->GetInfZoneTick()*Server()->TickSpeed()) < 1000*g_Config.m_InfNinjaTargetAfkTime) // Make sure zombie is not camping in InfZone
				{
					m_aTargetList[NbTargets] = i;
					NbTargets++;
				} 
				infectedCount++;
			}
		}
		
		if(NbTargets > 0)
			m_TargetToKill = m_aTargetList[random_int(0, NbTargets-1)];
			
		if(m_TargetToKill == -1)
		{
			if (LastTarget >= 0)
				m_TargetToKill = LastTarget; // Reset Target if no new targets were found
		}
		
		if (infectedCount < g_Config.m_InfNinjaMinInfected)
		{
			m_TargetToKill = -1; // disable target system
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
			SendChat(-1, CGameContext::CHAT_ALL, aChatmsg);
			StartVote(mapVote->m_pDesc, mapVote->m_pCommand, mapVote->m_pReason);
		}
	}
	
	// check tuning
	CheckPureTuning();
	
	m_Collision.SetTime(m_pController->GetTime());

	//update hook protection in core
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetCharacter())
		{
			m_apPlayers[i]->GetCharacter()->m_Core.m_Infected = m_apPlayers[i]->IsZombie();
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
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
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
				SendChat(-1, CGameContext::CHAT_ALL, "Vote passed");
				if(GetOptionVoteType(m_aVoteCommand) & MAP_VOTE_BITS)
					Server()->ResetMapVotes();

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote failed");
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

void CGameContext::OnClientEnter(int ClientID)
{
	//world.insert_entity(&players[client_id]);
	m_apPlayers[ClientID]->m_IsInGame = true;
	m_apPlayers[ClientID]->Respawn();
	
/* INFECTION MODIFICATION START ***************************************/
	SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} entered and joined the game"), "PlayerName", Server()->ClientName(ClientID), NULL);

	SendChatTarget(ClientID, "InfectionClass Mod. Version: " GAME_VERSION);
	SendChatTarget(ClientID, "See also: /info");

	SendChatTarget(ClientID, "Join our discord server: https://discord.gg/Sxk5ssv");
	SendChatTarget(ClientID, "Join our matrix.org room: https://matrix.to/#/#teeworlds-infclass:matrix.org");

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
	m_apPlayers[ClientID] = m_pController->CreatePlayer(ClientID);
	
	//Thanks to Stitch
	if(m_pController->IsInfectionStarted())
		m_apPlayers[ClientID]->StartInfection();
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
	m_pController->OnClientDrop(ClientID, Type);
	
	AbortVoteKickOnDisconnect(ClientID);
	m_apPlayers[ClientID]->OnDisconnect(Type, pReason);
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

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", ClientID, Server()->ClientName(ClientID));
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

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
		if(m_apPlayers[i] && m_apPlayers[i]->m_LastWhisperId == ClientID)
			m_apPlayers[i]->m_LastWhisperId = -1;
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

	CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
	const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";

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
		if(Server()->IsAuthed(KickID))
		{
			SendChatTarget(ClientID, "You can't kick admins");
			char aBufKick[128];
			str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you", Server()->ClientName(ClientID));
			SendChatTarget(KickID, aBufKick);
			return;
		}

		Server()->AddAccusation(ClientID, KickID, pReason);
	}
	else
	{
		if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		{
			SendChatTarget(ClientID, "Spectators aren't allowed to start a vote.");
			return;
		}

		char aChatmsg[512] = {0};
		char aDesc[VOTE_DESC_LENGTH] = {0};
		char aCmd[VOTE_CMD_LENGTH] = {0};

		if(str_comp_nocase(pMsg->m_Type, "option") == 0)
		{
			// this vote is not a kick/ban or spectate vote

			CVoteOptionServer *pOption = m_pVoteOptionFirst;
			while(pOption) // loop through all option votes to find out which vote it is
			{
				if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0) // found out which vote it is
				{
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
								pOption->m_aDescription, pReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						break;
					}

					if(OptionVoteType & MAP_VOTE_BITS)
					{
						// this vote is a map vote
						Server()->AddMapVote(ClientID, pOption->m_aCommand, pReason, pOption->m_aDescription);
						return;
					}

					break;
				}

				pOption = pOption->m_pNext;
			}

			if(!pOption)
			{
				str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
				SendChatTarget(ClientID, aChatmsg);
				return;
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

			str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
			str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
			str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		}

		// Start a vote
		if(aCmd[0])
		{
			CallVote(ClientID, aDesc, aCmd, pReason, aChatmsg);
		}
	}
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];
	
	if (pPlayer && MsgID == (NETMSGTYPE_CL_CALLVOTE + 1)) 
	{
		int Version = pUnpacker->GetInt();

		if(g_Config.m_SvBannedVersions[0] != '\0' && IsVersionBanned(Version))
		{
			Server()->Kick(ClientID, "unsupported client");
		}

		pPlayer->m_ClientVersion = Version;
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
			if(str_comp_num(pMsg->m_pMessage, "/msg ", 5) == 0)
			{
				PrivateMessage(pMsg->m_pMessage+5, ClientID, (Team != CGameContext::CHAT_ALL));
			}
			else if(str_comp_num(pMsg->m_pMessage, "/whisper ", 9) == 0)
			{
				PrivateMessage(pMsg->m_pMessage+9, ClientID, (Team != CGameContext::CHAT_ALL));
			}
			else if(str_comp_num(pMsg->m_pMessage, "/w ", 3) == 0)
			{
				PrivateMessage(pMsg->m_pMessage+3, ClientID, (Team != CGameContext::CHAT_ALL));
			}
			else if(str_comp_num(pMsg->m_pMessage, "/converse ", 10) == 0)
			{
				Converse(ClientID, pMsg->m_pMessage + 10, Team);
			}
			else if(str_comp_num(pMsg->m_pMessage, "/c ", 3) == 0)
			{
				Converse(ClientID, pMsg->m_pMessage + 3, Team);
			}
			else if(str_comp_num(pMsg->m_pMessage, "/mute ", 6) == 0)
			{
				MutePlayer(pMsg->m_pMessage+6, ClientID);
			}
			else if(pMsg->m_pMessage[0] == '/' || pMsg->m_pMessage[0] == '\\')
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
			if(pMsg->m_Team == TEAM_SPECTATORS && !CanJoinSpec(ClientID))
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
					pPlayer->SetTeam(pMsg->m_Team);
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
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*5 > Server()->Tick())
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
			
/* INFECTION MODIFICATION START ***************************************/
			str_copy(pPlayer->m_TeeInfos.m_CustomSkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_CustomSkinName));
			//~ pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			//~ pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			//~ pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
/* INFECTION MODIFICATION END *****************************************/

			m_pController->OnPlayerInfoChange(pPlayer);
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

			str_copy(pPlayer->m_TeeInfos.m_CustomSkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_CustomSkinName));
			//~ pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			//~ pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			//~ pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			m_pController->OnPlayerInfoChange(pPlayer);
			
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

				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "From client: \"%s\", for IP: \"%s\"", pLangFromClient, pLangForIp);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "lang", aBuf);

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

			CNetMsg_Sv_VoteOptionListAdd OptionMsg;
			int NumOptions = 0;
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
			CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
			while(pCurrent)
			{
				switch(NumOptions++)
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
				case 14:
					{
						OptionMsg.m_pDescription14 = pCurrent->m_aDescription;
						OptionMsg.m_NumOptions = NumOptions;
						Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
						OptionMsg = CNetMsg_Sv_VoteOptionListAdd();
						NumOptions = 0;
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
					}
				}
				pCurrent = pCurrent->m_pNext;
			}
			if(NumOptions > 0)
			{
				OptionMsg.m_NumOptions = NumOptions;
				Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
			}

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
	pSelf->m_pController->QueueMap(pResult->GetString(0));

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

	{
		char aMapFilename[128];
		str_format(aMapFilename, sizeof(aMapFilename), "%s.map", pMapName);

		char aBuf[512];
		if(!pSelf->Storage()->FindFile(aMapFilename, "maps", IStorage::TYPE_ALL, aBuf, sizeof(aBuf)))
		{
			str_format(aBuf, sizeof(aBuf), "Unable to find map %s", pMapName);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

			return true;
		}
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
		char aBuf[256];
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
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
	
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
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
	(void)pSelf->m_pController->CheckTeamBalance();
	
	return true;
}

bool CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->SetTeam(Team, false);

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

	// zombies
	std::vector<int> infectedProbabilities;
	for (int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; PlayerClass++)
	{
		infectedProbabilities.push_back(Server()->GetPlayerClassProbability(PlayerClass));
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
		humanAvailabilities.push_back(Server()->GetPlayerClassEnabled(PlayerClass));
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
	m_DefaultTimelimit = g_Config.m_SvTimelimit;
	if (g_Config.m_SvTimelimit > g_Config.m_FunRoundDuration)
		g_Config.m_SvTimelimit = g_Config.m_FunRoundDuration;

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
	g_Config.m_SvTimelimit = m_DefaultTimelimit;
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

bool CGameContext::ConInfo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int ClientID = pResult->GetClientID();
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();

	dynamic_string Buffer;
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass, by necropotame (version {str:VersionCode})"), "VersionCode", GAME_VERSION, NULL);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
	Buffer.clear();

	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Server version from {str:ServerCompileDate} "), "ServerCompileDate", LAST_COMPILE_DATE, NULL); 
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
	Buffer.clear();

	if(GIT_SHORTREV_HASH)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Git revision hash: %s", GIT_SHORTREV_HASH);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", aBuf);
	}

	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Sources: {str:SourceUrl} "), "SourceUrl",
	                                          "https://github.com/InfectionDust/teeworlds-infclassR", NULL);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info", Buffer.buffer());
	Buffer.clear();

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "info",
		"See also: /credits");

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
		SendChatTarget(ClientID, "Available groups: #near, #engineer, #soldier, ...");
		return true;
	}
	
	dynamic_string FinalMessage;
	int TextIter = 0;
	
	
	bool CheckDistance = false;
	vec2 CheckDistancePos = vec2(0.0f, 0.0f);
	
	int CheckID = -1;
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
						CheckID = i;
						str_copy(aChatTitle, "private", sizeof(aChatTitle));						
						m_apPlayers[ClientID]->m_LastWhisperId = i;
						CheckTeam = -1;
						break;
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
				
				if(CheckID >= 0 && !(i == CheckID))
					continue;
				
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

#ifdef CONF_SQL

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
	
	int ClientID = pResult->GetClientID();
	const char* pLanguage = pSelf->m_apPlayers[ClientID]->GetLanguage();
	
	const char *pHelpPage = (pResult->NumArguments()>0) ? pResult->GetString(0) : 0x0;

	dynamic_string Buffer;
	
	if(pHelpPage)
	{
		if(str_comp_nocase(pHelpPage, "game") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Rules of the game"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass is a team game between humans and infected."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("All players start as human."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("10 seconds later, two players become infected."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The goal for humans is to survive until the army clean the map."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The goal for infected is to infect all humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "translate") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("How to translate the mod"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Create an account on Crowdin and join a translation team:"), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "https://crowdin.com/project/teeuniverse", NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("For any question about the translation process, please contact us on IRC ({str:IRCAddress})"), "IRCAddress", "QuakeNet, #infclass", NULL);

			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "engineer") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Engineer"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Engineer can build walls with the hammer to block infected."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("When an infected touch the wall, it dies."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The lifespan of a wall is {sec:LifeSpan}, and walls are limited to one per player at the same time."), "LifeSpan", &g_Config.m_InfBarrierLifeSpan, NULL); 
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "soldier") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Soldier"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Soldier creates floating bombs with the hammer."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_LP(
				Buffer, pLanguage, g_Config.m_InfSoldierBombs,
				_P("Each bomb can explode one time.", "Each bomb can explode {int:NumBombs} times."),
				"NumBombs", &g_Config.m_InfSoldierBombs,
				NULL
			);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use the hammer to place the bomb and explode it multiple times."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("A lucky Soldier, devoted to killing zombies, can get a bonus - stunning grenades."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "scientist") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Scientist"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Scientist can pose floating mines with the hammer."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_LP(
				Buffer, pLanguage, g_Config.m_InfMineLimit,
				_P("Mines are limited to one per player at the same time.", "Mines are limited to {int:NumMines} per player at the same time."),
				"NumMines", &g_Config.m_InfMineLimit,
				NULL
			);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Scientist has also grenades that teleport him."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Additionally scientist can place white holes with the laser rifle. Further information on /help whitehole"), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "biologist") == 0)
		{

			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Biologist"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The biologist has a shotgun with bouncing bullets and can create a spring laser trap by shooting with the rifle."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "looper") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Looper"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The looper has a laser wall that slows down zombies and a low-range-laser-pistol."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("A lucky Looper, devoted to killing zombies, can get a bonus - stunning grenades."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "medic") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Medic"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Medic can protect humans with the hammer by giving them armor."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Grenades with medicine give armor to everybody in their range, including heroes and medic themself"), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Laser revives infected zombies, but at great cost - 17 hp and armor."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Medic has also a powerful shotgun that can pullback infected."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "hero") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Hero"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Hero has a shotgun, a laser rifle and grenades."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Hero must find a flag only visible to them hidden in the map."), NULL);
			Buffer.append("\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The flag gifts 1 health point, 4 armor points, and full ammo to all humans, furthermore full health and armor to the hero."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The gift to all humans is only applied when the flag is surrounded by hearts and armor."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Cannot find a flag? Stand still for some seconds, laser will show you the way."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The hero cannot be healed by a medic, but can withstand a thrust by an infected."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "ninja") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Ninja"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Ninja can throw flash grenades that can freeze infected during three seconds."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_LP(
				Buffer, pLanguage, g_Config.m_InfNinjaJump,
				_P("His hammer is replaced by a katana, allowing him to jump one time before touching the ground.", "His hammer is replaced by a katana, allowing him to jump {int:NinjaJump} times before touching the ground."),
				"NinjaJump", &g_Config.m_InfNinjaJump,
				NULL
			);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Ninjas get special targets. For killing a target, extra points and abilities are awarded"), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "mercenary") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Mercenary"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Mercenaries fly in the air using their machine gun."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("They can create powerful bombs with their hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_LP(
				Buffer, pLanguage, g_Config.m_InfPoisonDamage,
				_P("Mercenary can also throw poison grenades that each deal one damage point.", "Mercenary can also throw poison grenades that each deal {int:NumDamagePoints} damage points."),
				"NumDamagePoints", &g_Config.m_InfPoisonDamage,
				NULL
			);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "sniper") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Sniper"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Sniper can lock the position in air for 15 seconds with the hammer."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Snipers can jump two times in air."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("They also have a powerful rifle that deals 20 damage points in locked position, and 10–13 otherwise."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "smoker") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Smoker"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Smoker can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Smoker has a powerful hook that hurts human."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Their hook not only hurts humans, but also sucks their blood, restoring the smokers health."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "boomer") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Boomer"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Boomer explodes when it attacks."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("All humans affected by the explosion become infected."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("It can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "hunter") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Hunter"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Hunter can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can jump two times in air."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "bat") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Bat"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Bat can heal infected with the hammer, but cannot infect humans"), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Instead, it can hammer humans to reduce their health or even kill them"), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Bat can jump multiple times in air."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("It can also inflict 1 damage point per second by hooking humans."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Their hook not only hurts humans, but also sucks their blood, restoring the bats health."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "ghost") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Ghost"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Ghost can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He is invisible, except if a human is near him, if it takes a damage or if it use the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "spider") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Spider"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Spider can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("When selecting any gun, the hook enter in web mode."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Any human that touch a hook in web mode is automatically grabbed."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The hook of the spider (in both mode) deal 1 damage point per second and can grab a human during 2 seconds."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "ghoul") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Ghoul"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Ghoul can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can devour all that has died close to him, which makes him stronger, faster and more resistant."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Thereupon it digests the fodder bit by bit going back to the normal state, and besides, death bereaves him of the nourishment."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "slug") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Slug"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Slug can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can make the ground and walls toxic by spreading slime with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Touching the slime inflicts three damage points in three seconds on a human."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "voodoo") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Voodoo"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Voodoo can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He does not die immediately when killed but instead enters Spirit mode and defies death for a brief span of time."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("While in Spirit mode it cannot be killed. When the time is up it finally dies."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "whitehole") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("White hole"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_LP(Buffer, pLanguage, g_Config.m_InfMineLimit, _P("Receive it by killing at least one zombie.","Receive it by killing at least {int:NumKills} zombies as scientist."),
					"NumKills", &g_Config.m_InfWhiteHoleMinimalKills, NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use it with your laser rifle, the indicator around your Tee will show you if it is available"), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Puts humans and zombies into a vulnerable state by pulling them into its center"), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "undead") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Undead"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Undead can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Instead of dying, it freezes during 10 seconds."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("If an infected heals him, the freeze effect disappear."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("He can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "witch") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Witch")); 
			Buffer.append(" ~~\n\n");
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The Witch can infect humans and heal infected with the hammer."), NULL);
			Buffer.append("\n\n");
			if (g_Config.m_InfEnableWitchPortals)
				pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Additionally a witch can place portals with the laser rifle. Further information on /help portals"), NULL);
			else
				pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("When an infected dies, it may re-spawn near witch."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("If the Witch dies, it disappears and is replaced by another class of infected."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("It can also inflict 1 damage point per second by hooking humans."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "portals") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("How to use witch portals"), NULL);
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("A witch can build a pair of portals to let other infected get from spawn right to the battle."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use Rifle to place or \"take\" a portal. The first portal is always Enterance and the last one is always Exit."), NULL);
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use Rifle to replace Exit if both portals already opened."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Once both portals are in place, they need extra {sec:ConnectionTime} to become operational."), "ConnectionTime", &g_Config.m_InfPortalConnectionTime, NULL);

			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("The portals destroyed when the witch die."), NULL);
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Only one witch on a map can open a portal."), NULL);

			if (g_Config.m_InfEnableWitchPortals)
				pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Portals are currently ENABLED."), NULL);
			else
				pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Portals are currently DISABLED."), NULL);

			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "msg") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Targeted chat messages")); 
			Buffer.append(" ~~\n\n");
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg <PlayerName> <My Message>” to send a private message to this player."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg !<ClassName> <My Message>” to send a private message to all players with a specific class."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Example: “/msg !medic I'm wounded!”"), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg !near” to send a private message to all players near you."), NULL);
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "mute") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Persistent player mute")); 
			Buffer.append(" ~~\n\n");
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/mute <PlayerName>” to mute this player."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Unlike a client mute this will persist between map changes and wears off when either you or the muted player disconnects."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Example: “/mute nameless tee”"), NULL);
			Buffer.append("\n\n");
			
			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else if(str_comp_nocase(pHelpPage, "taxi") == 0)
		{
			Buffer.append("~~ ");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("How to use taxi mode"), NULL); 
			Buffer.append(" ~~\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Two or more humans can form a taxi."), NULL); 
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("In order to use it, both humans have to disable hook protection (usually, with F3). The human being hooked becomes the driver."), NULL);
			Buffer.append("\n\n");
			pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("To get off the taxi, jump. To drop off your passengers, enable hook protection (usually, with F3)."), NULL);

			pSelf->SendMOTD(ClientID, Buffer.buffer());
		}
		else
			pHelpPage = 0x0;
	}
	
	if(!pHelpPage)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", pSelf->Server()->Localization()->Localize(pLanguage, _("Choose a help page with /help <page>")));
		
		dynamic_string Buffer;
		pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, _("Available help pages: {str:PageList}"),
			"PageList", "game, translate, msg, taxi",
			NULL
		);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", Buffer.buffer());
		
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", "engineer, soldier, scientist, biologist, looper, medic, hero, ninja, mercenary, sniper, whitehole");		
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "help", "smoker, hunter, bat, boomer, ghost, spider, ghoul, voodoo, undead, witch, portals.");
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

bool CGameContext::ConCustomSkin(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetClientID();
	
	const char *pArg = pResult->GetString(0);

	if(pArg)
	{
		if(str_comp_nocase(pArg, "all") == 0)
			pSelf->Server()->SetClientCustomSkin(ClientID, 2);
		else if(str_comp_nocase(pArg, "me") == 0)
			pSelf->Server()->SetClientCustomSkin(ClientID, 1);
		else if(str_comp_nocase(pArg, "none") == 0)
			pSelf->Server()->SetClientCustomSkin(ClientID, 0);
	}
	
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
	pSelf->Server()->Localization()->Format_L(Buffer, pLanguage, "/antiping, /alwaysrandom, /customskin, /help, /info, /language", NULL);
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
	Console()->Register("info", "", CFGFLAG_CHAT|CFGFLAG_USER, ConInfo, this, "Display information about the mod");
#ifdef CONF_SQL
	Console()->Register("register", "s<username> s<password> ?s<email>", CFGFLAG_CHAT|CFGFLAG_USER, ConRegister, this, "Create an account");
	Console()->Register("login", "s<username> s<password>", CFGFLAG_CHAT|CFGFLAG_USER, ConLogin, this, "Login to an account");
	Console()->Register("logout", "", CFGFLAG_CHAT|CFGFLAG_USER, ConLogout, this, "Logout");
	Console()->Register("setemail", "s<email>", CFGFLAG_CHAT|CFGFLAG_USER, ConSetEmail, this, "Change your email");
	
	Console()->Register("top10", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConTop10, this, "Show the top 10 on the current map");
	Console()->Register("challenge", "", CFGFLAG_CHAT|CFGFLAG_USER, ConChallenge, this, "Show the current winner of the challenge");
	Console()->Register("rank", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConRank, this, "Show your rank");
	Console()->Register("goal", "?s<classname>", CFGFLAG_CHAT|CFGFLAG_USER, ConGoal, this, "Show your goal");
	Console()->Register("stats", "i", CFGFLAG_CHAT|CFGFLAG_USER, ConStats, this, "Show stats by id");
#endif
	Console()->Register("help", "?s<page>", CFGFLAG_CHAT|CFGFLAG_USER, ConHelp, this, "Display help");
	Console()->Register("changelog", "?i<list>", CFGFLAG_CHAT|CFGFLAG_USER, ConChangeLog, this, "Display changelogs");
	Console()->Register("customskin", "s<all|me|none>", CFGFLAG_CHAT|CFGFLAG_USER, ConCustomSkin, this, "Display information about the mod");
	Console()->Register("alwaysrandom", "i<0|1>", CFGFLAG_CHAT|CFGFLAG_USER, ConAlwaysRandom, this, "Display information about the mod");
	Console()->Register("antiping", "i<0|1>", CFGFLAG_CHAT|CFGFLAG_USER, ConAntiPing, this, "Try to improve your ping");
	Console()->Register("language", "s<en|fr|nl|de|bg|sr-Latn|hr|cs|pl|uk|ru|el|la|it|es|pt|hu|ar|tr|sah|fa|tl|zh-Hans|ja>", CFGFLAG_CHAT|CFGFLAG_USER, ConLanguage, this, "Display information about the mod");
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

int CGameContext::GetTargetToKill()
{
	return m_TargetToKill;
}
void CGameContext::TargetKilled()
{
	m_TargetToKill = -1;
	
	int PlayerCounter = 0;
	CPlayerIterator<PLAYERITER_INGAME> Iter(m_apPlayers);
	while(Iter.Next())
		PlayerCounter++;
	
	m_TargetToKillCoolDown = Server()->TickSpeed()*(10 + 3*maximum(0, 16 - PlayerCounter));
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

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

const char *CGameContext::GameType() { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
const char *CGameContext::Version() { return GAME_VERSION; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }



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

bool CGameContext::CanJoinSpec(int ClientID)
{
	if (m_pController->IsGameOver())
		return true;

	if (!m_apPlayers[ClientID]->IsZombie())
		return true;

	int InfectedCount = 0;
	int IngamePlayersCount = 0;
	CPlayerIterator<PLAYERITER_INGAME> Iter(m_apPlayers);
	while(Iter.Next())
	{
		IngamePlayersCount++;
		if(Iter.Player()->IsZombie())
			InfectedCount++;
	}

	if (IngamePlayersCount == InfectedCount)
		return true;

	if (InfectedCount > 2)
		return true;

	return false;
}

bool CGameContext::IsVersionBanned(int Version)
{
	char aVersion[16];
	str_format(aVersion, sizeof(aVersion), "%d", Version);

	return str_in_list(g_Config.m_SvBannedVersions, ",", aVersion);
}

int CGameContext::GetClientVersion(int ClientID)
{
	return m_apPlayers[ClientID]->m_ClientVersion;
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

void CGameContext::Converse(int ClientID, const char* pStr, int team)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if (pPlayer->m_LastWhisperId < 0)
	{
		SendChatTarget(ClientID, "You do not have an ongoing conversation. Whisper to someone to start one");
	}
	else
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s %s", Server()->ClientName(pPlayer->m_LastWhisperId), pStr);
		//dbg_msg("TEST", aBuf);
		PrivateMessage(aBuf, ClientID, (team != CGameContext::CHAT_ALL));
	}
}
