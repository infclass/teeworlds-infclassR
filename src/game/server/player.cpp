/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include "player.h"
#include "engine/server.h"
#include "entities/character.h"

#include <engine/shared/config.h>
#include <engine/server/roundstatistics.h>

#include <game/server/gamecontext.h>

MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, int Team)
{
	m_pGameServer = pGameServer;
	m_ClientID = ClientID;
	m_Team = GameServer()->m_pController->ClampTeam(Team);
	Reset();
}

CPlayer::~CPlayer()
{
	delete m_pCharacter;
	m_pCharacter = 0;
}

void CPlayer::Reset()
{
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();
	m_pCharacter = 0;
	m_SpectatorID = SPEC_FREEVIEW;
	m_LastActionTick = Server()->Tick();
	m_LastActionMoveTick = Server()->Tick();
	m_TeamChangeTick = Server()->Tick();

	int *idMap = Server()->GetIdMap(m_ClientID);
	for(int i = 1; i < VANILLA_MAX_CLIENTS; i++)
	{
		idMap[i] = -1;
	}
	idMap[0] = m_ClientID;

	// DDRace

	m_LastCommandPos = 0;

	m_LastVoteCall = 0;
	m_LastVoteTry = 0;
	m_LastChat = 0;

	m_LastEyeEmote = 0;
	m_DefEmote = EMOTE_NORMAL;
	m_Afk = true;
	m_LastKill = 0;
	m_LastWhisperTo = -1;
	m_aTimeoutCode[0] = '\0';

	m_SendVoteIndex = -1;

	m_OverrideEmote = 0;
	m_OverrideEmoteReset = -1;

	m_ShowOthers = g_Config.m_SvShowOthersDefault;
	m_ShowAll = g_Config.m_SvShowAllDefault;
	m_ShowDistance = vec2(1200, 800);
	m_SpecTeam = false;

	m_Paused = PAUSE_NONE;
	m_DND = false;

	m_LastPause = 0;

	int64_t Now = Server()->Tick();
	int64_t TickSpeed = Server()->TickSpeed();
	// If the player joins within ten seconds of the server becoming
	// non-empty, allow them to vote immediately. This allows players to
	// vote after map changes or when they join an empty server.
	//
	// Otherwise, block voting in the beginning after joining.
	if(Now > GameServer()->m_NonEmptySince + 10 * TickSpeed)
		m_FirstVoteTick = Now + g_Config.m_SvJoinVoteDelay * TickSpeed;
	else
		m_FirstVoteTick = Now;

/* INFECTION MODIFICATION START ***************************************/
	m_Afk = false;

	m_ClientNameLocked = false;
	m_aOriginalName[0] = 0;

	m_class = PLAYERCLASS_NONE;
	m_InfectionTick = -1;
	SetLanguage(Server()->GetClientLanguage(m_ClientID));

	m_PrevTuningParams = *m_pGameServer->Tuning();
	m_NextTuningParams = m_PrevTuningParams;
	m_IsInGame = false;
	m_IsReady = false;
/* INFECTION MODIFICATION END *****************************************/
}

void CPlayer::HandleAutoRespawn()
{
	if(!m_pCharacter && m_DieTick+Server()->TickSpeed()*3 <= Server()->Tick())
		Respawn();
}

void CPlayer::Tick()
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = maximum(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = minimum(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick()%Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum/Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(m_pCharacter && !m_pCharacter->IsAlive())
	{
		delete m_pCharacter;
		m_pCharacter = 0;
	}

	if(!GameServer()->m_World.m_Paused)
	{
		if(!m_pCharacter && m_Team == TEAM_SPECTATORS && m_SpectatorID == SPEC_FREEVIEW)
			m_ViewPos -= vec2(clamp(m_ViewPos.x-m_LatestActivity.m_TargetX, -500.0f, 500.0f), clamp(m_ViewPos.y-m_LatestActivity.m_TargetY, -400.0f, 400.0f));

		HandleAutoRespawn();

		if(m_pCharacter)
		{
			if(m_pCharacter->IsAlive())
			{
				m_ViewPos = m_pCharacter->m_Pos;
			}
		}
		else if(m_Spawning && m_RespawnTick <= Server()->Tick())
			TryRespawn();
	}
	else
	{
		++m_RespawnTick;
		++m_DieTick;
		++m_ScoreStartTick;
		++m_LastActionTick;
		++m_LastActionMoveTick;
		++m_TeamChangeTick;
 	}
}

void CPlayer::PostTick()
{
}

void CPlayer::HandleTuningParams()
{
	if(!(m_PrevTuningParams == m_NextTuningParams))
	{
		if(m_IsReady)
		{
			GameServer()->SendTuningParams(GetCID(), m_NextTuningParams);
		}
		
		m_PrevTuningParams = m_NextTuningParams;
	}
	
	m_NextTuningParams = *GameServer()->Tuning();
}

void CPlayer::Snap(int SnappingClient)
{
#ifdef CONF_DEBUG
	if(!g_Config.m_DbgDummies || m_ClientID < MAX_CLIENTS-g_Config.m_DbgDummies)
#endif
	if(!Server()->ClientIngame(m_ClientID))
		return;

	int id = m_ClientID;
	if(SnappingClient != SERVER_DEMO_CLIENT && !Server()->Translate(id, SnappingClient))
		return;

	SnapClientInfo(SnappingClient, id);

	int Latency = SnappingClient == SERVER_DEMO_CLIENT ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aCurLatency[m_ClientID];
	int PlayerInfoScore = GetScore(SnappingClient);

	CNetObj_PlayerInfo *pPlayerInfo = Server()->SnapNewItem<CNetObj_PlayerInfo>(id);
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_Latency = Latency;
	pPlayerInfo->m_Local = 0;
	pPlayerInfo->m_ClientID = id;
/* INFECTION MODIFICATION START ***************************************/
	pPlayerInfo->m_Score = PlayerInfoScore;
/* INFECTION MODIFICATION END *****************************************/
	pPlayerInfo->m_Team = m_Team;

	if(m_ClientID == SnappingClient)
		pPlayerInfo->m_Local = 1;

	if(m_ClientID == SnappingClient && m_Team == TEAM_SPECTATORS)
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = Server()->SnapNewItem<CNetObj_SpectatorInfo>(m_ClientID);
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CPlayer::SnapClientInfo(int SnappingClient, int SnappingClientMappedId)
{
	CNetObj_ClientInfo *pClientInfo = Server()->SnapNewItem<CNetObj_ClientInfo>(SnappingClientMappedId);
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, GetName(SnappingClient));
	StrToInts(&pClientInfo->m_Clan0, 3, GetClan(SnappingClient));
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);
	StrToInts(&pClientInfo->m_Skin0, 6, m_TeeInfos.m_aSkinName);
	pClientInfo->m_UseCustomColor = m_TeeInfos.m_UseCustomColor;
	pClientInfo->m_ColorBody = m_TeeInfos.m_ColorBody;
	pClientInfo->m_ColorFeet = m_TeeInfos.m_ColorFeet;
}

void CPlayer::OnDisconnect()
{
	KillCharacter();
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags&PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING))
		return;

	if(m_pCharacter)
		m_pCharacter->OnPredictedInput(NewInput);
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	if(NewInput->m_PlayerFlags&PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if(m_PlayerFlags&PLAYERFLAG_CHATTING)
			return;

		// reset input
		if(m_pCharacter)
			m_pCharacter->ResetInput();

		m_PlayerFlags = NewInput->m_PlayerFlags;
 		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(m_pCharacter)
		m_pCharacter->OnDirectInput(NewInput);

	bool AcceptInput = Server()->Tick() > m_DieTick + Server()->TickSpeed() * 0.2f;
	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && AcceptInput && (NewInput->m_Fire&1))
		Respawn();

	// check for activity
	if(NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire&1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
		if (NewInput->m_Direction || NewInput->m_Jump || NewInput->m_Hook)
			m_LastActionMoveTick = Server()->Tick();
	}
}

void CPlayer::OnPredictedEarlyInput(CNetObj_PlayerInput *pNewInput)
{
	m_PlayerFlags = pNewInput->m_PlayerFlags;

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (pNewInput->m_Fire & 1))
		m_Spawning = true;

	// skip the input if chat is active
	if(m_PlayerFlags & PLAYERFLAG_CHATTING)
		return;

	if(m_pCharacter)
		m_pCharacter->OnDirectInput(pNewInput);
}

int CPlayer::GetClientVersion() const
{
	return m_pGameServer->GetClientVersion(m_ClientID);
}

int CPlayer::GetScore(int SnappingClient) const
{
	return 0;
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

void CPlayer::KillCharacter(int Weapon)
{
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);
		delete m_pCharacter;
		m_pCharacter = 0;
	}
}

void CPlayer::Respawn()
{
	if(m_Team != TEAM_SPECTATORS)
	{
		if(!m_Spawning)
		{
			m_Spawning = true;
		}
	}
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	m_LastActionMoveTick = Server()->Tick();
	m_SpectatorID = SPEC_FREEVIEW;
	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
				GameServer()->m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		}
	}
}

void CPlayer::TryRespawn()
{
}

int CPlayer::GetDefaultEmote() const
{
	if(m_OverrideEmoteReset >= 0)
		return m_OverrideEmote;

	return m_DefEmote;
}

void CPlayer::OverrideDefaultEmote(int Emote, int Tick)
{
	m_OverrideEmote = Emote;
	m_OverrideEmoteReset = Tick;
	m_LastEyeEmote = Server()->Tick();
}

bool CPlayer::CanOverrideDefaultEmote() const
{
	return true;
}

void CPlayer::ProcessPause()
{
}

int CPlayer::Pause(int State, bool Force)
{
	if(State < PAUSE_NONE || State > PAUSE_SPEC) // Invalid pause state passed
		return 0;

	if(!m_pCharacter)
		return 0;

	return m_Paused;
}

int CPlayer::ForcePause(int Time)
{
	m_ForcePauseTime = Server()->Tick() + Server()->TickSpeed() * Time;

	if(g_Config.m_SvPauseMessages)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "'%s' was force-paused for %ds", Server()->ClientName(m_ClientID), Time);
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}

	return Pause(PAUSE_SPEC, true);
}

int CPlayer::IsPaused()
{
	return m_ForcePauseTime ? m_ForcePauseTime : -1 * m_Paused;
}

bool CPlayer::IsPlaying()
{
	return m_pCharacter && m_pCharacter->IsAlive();
}

bool CPlayer::IsInGame() const
{
	return m_IsInGame && m_IsReady && !IsSpectator();
}

/* INFECTION MODIFICATION START ***************************************/
PLAYERCLASS CPlayer::GetClass() const
{
	return static_cast<PLAYERCLASS>(m_class);
}

bool CPlayer::IsInfected() const
{
	return (m_class > END_HUMANCLASS);
}

bool CPlayer::IsHuman() const
{
	return !(m_class > END_HUMANCLASS);
}

bool CPlayer::IsSpectator() const
{
	return GetTeam() == TEAM_SPECTATORS;
}

const char *CPlayer::GetName(int SnappingClient) const
{
	return Server()->ClientName(m_ClientID);
}

const char *CPlayer::GetClan(int SnappingClient) const
{
	return Server()->ClientClan(m_ClientID);
}

const char *CPlayer::GetLanguage() const
{
	return m_aLanguage;
}

void CPlayer::SetLanguage(const char* pLanguage)
{
	str_copy(m_aLanguage, pLanguage, sizeof(m_aLanguage));
}

void CPlayer::SetOriginalName(const char *pName)
{
	str_copy(m_aOriginalName, pName, sizeof(m_aOriginalName));
}

/* INFECTION MODIFICATION END *****************************************/
