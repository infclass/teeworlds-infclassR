/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <game/mapitems.h>

#include <game/generated/protocol.h>
#include <game/server/player.h>

#include <engine/shared/network.h>

#include "gamecontroller.h"
#include "gamecontext.h"

class CMapInfo
{
public:
	int MinimumPlayers = 0;
	int MaximumPlayers = 0;
	int RecommendedPlayers = 0;
};

class CMapInfoEx : public CMapInfo
{
public:
	int Timestamp = 0;

	const char *Name() const { return aMapName; }
	void SetName(const char *pMapName);

	void AddTimestamp(int Timestamp);
	void ResetData();

protected:
	char aMapName[64];
};

void CMapInfoEx::SetName(const char *pMapName)
{
	str_copy(aMapName, pMapName, sizeof(aMapName));
}

void CMapInfoEx::AddTimestamp(int Timestamp)
{
	this->Timestamp = Timestamp;
}

void CMapInfoEx::ResetData()
{
	Timestamp = 0;
}

constexpr int MaxMapsNumber = 256;

static icArray<CMapInfoEx, MaxMapsNumber> s_aMapInfo;
static int s_CachedMapIndex = 0;

int GetMapIndex(const char *pMapName)
{
	for(int i = 0; i < s_aMapInfo.Size(); ++i)
	{
		if(str_comp(pMapName, s_aMapInfo.At(i).Name()) == 0)
		{
			return i;
		}
	}

	return -1;
}

CMapInfoEx *GetMapInfo(const char *pMapName)
{
	if(s_CachedMapIndex >= s_aMapInfo.Size())
		s_CachedMapIndex = -1;

	if((s_CachedMapIndex < 0) || (str_comp(pMapName, s_aMapInfo.At(s_CachedMapIndex).Name()) != 0))
		s_CachedMapIndex = GetMapIndex(pMapName);

	if(s_CachedMapIndex < 0)
		return nullptr;

	return &s_aMapInfo[s_CachedMapIndex];
}

static float GetMapTimeScore(const CMapInfoEx &Info, int CurrentTimestamp, int MinTimestamp)
{
	int V1 = Info.Timestamp - MinTimestamp; // Range from zero to something
	float V2 = CurrentTimestamp - MinTimestamp; // Range from something to zero

	float TimeScore = 0;
	if(V1 < 0)
	{
		TimeScore = 1;
	}
	else if(V2 <= 0)
	{
		TimeScore = 0;
	}
	else
	{
		TimeScore = clamp<float>(1 - V1 / V2, 0, 1);
	}

	return TimeScore;
}

static float GetMapFitPlayersScore(const CMapInfoEx &Info, int CurrentActivePlayers)
{
	float FitPlayersScore = 1;

	int SaneMinimumPlayers = clamp<int>(Info.MinimumPlayers, 2, MAX_CLIENTS - 1);
	int SaneMaximumPlayers = Info.MaximumPlayers == 0 ? MAX_CLIENTS - 1 : Info.MaximumPlayers;
	SaneMaximumPlayers = clamp<int>(SaneMaximumPlayers, SaneMinimumPlayers, MAX_CLIENTS - 1);

	if(CurrentActivePlayers == SaneMinimumPlayers)
	{
		FitPlayersScore -= 0.5f;
	}
	else if(CurrentActivePlayers == SaneMinimumPlayers + 1)
	{
		FitPlayersScore -= 0.25f;
	}

	if(CurrentActivePlayers == SaneMaximumPlayers)
	{
		FitPlayersScore -= 0.5f;
	}
	else if(CurrentActivePlayers == SaneMaximumPlayers - 1)
	{
		FitPlayersScore -= 0.25f;
	}

	return FitPlayersScore;
}

CConfig *IGameController::Config() const
{
	return GameServer()->Config();
}

IConsole *IGameController::Console()
{
	return GameServer()->Console();
}

IGameController::IGameController(class CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = m_pGameServer->Server();
	m_pGameType = "unknown";

	//
	DoWarmup(g_Config.m_SvWarmup);
	m_GameOverTick = -1;
	m_SuddenDeath = 0;
	m_RoundStartTick = Server()->Tick();
	m_RoundCount = 0;
	m_GameFlags = 0;
	m_aTeamscore[TEAM_RED] = 0;
	m_aTeamscore[TEAM_BLUE] = 0;
	m_aMapWish[0] = 0;
	m_aQueuedMap[0] = 0;
	m_aPreviousMap[0] = 0;

	m_UnbalancedTick = -1;
	m_ForceBalanced = false;

	m_RoundId = -1;
}

IGameController::~IGameController()
{
}

void IGameController::DoActivityCheck()
{
	if(g_Config.m_SvInactiveKickTime == 0)
		return;

	int HumanMaxInactiveTimeSecs = Config()->m_InfInactiveHumansKickTime ? Config()->m_InfInactiveHumansKickTime : Config()->m_SvInactiveKickTime * 60;
	int InfectedMaxInactiveTimeSecs = Config()->m_InfInactiveInfectedKickTime ? Config()->m_InfInactiveInfectedKickTime : Config()->m_SvInactiveKickTime * 60;

	unsigned int nbPlayers=0;
	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		nbPlayers++;
	}

	if(nbPlayers < 2)
	{
		// Do not kick players when they are the only (non-spectating) player
		return;
	}

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
#ifdef CONF_DEBUG
		if(g_Config.m_DbgDummies)
		{
			if(i >= MAX_CLIENTS - g_Config.m_DbgDummies)
				break;
		}
#endif
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && (Server()->GetAuthedState(i) == IServer::AUTHED_NO))
		{
			CPlayer *pPlayer = GameServer()->m_apPlayers[i];
			float PlayerMaxInactiveTimeSecs = pPlayer->IsHuman() ? HumanMaxInactiveTimeSecs : InfectedMaxInactiveTimeSecs;
			if(PlayerMaxInactiveTimeSecs < 20)
			{
				PlayerMaxInactiveTimeSecs = 20;
			}

			int WarningTick = pPlayer->m_LastActionTick + (PlayerMaxInactiveTimeSecs - 10) * Server()->TickSpeed();
			int KickingTick = pPlayer->m_LastActionTick + PlayerMaxInactiveTimeSecs * Server()->TickSpeed();

			if(Server()->Tick() > KickingTick)
			{
				switch(g_Config.m_SvInactiveKick)
				{
				case 0:
				{
					// move player to spectator
					DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
				}
				break;
				case 1:
				{
					// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
					int Spectators = 0;
					for(auto &pPlayer : GameServer()->m_apPlayers)
						if(pPlayer && pPlayer->GetTeam() == TEAM_SPECTATORS)
							++Spectators;
					if(Spectators >= g_Config.m_SvSpectatorSlots)
						Server()->Kick(i, "Kicked for inactivity");
					else
						DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
				}
				break;
				case 2:
				{
					// kick the player
					Server()->Kick(i, "Kicked for inactivity");
				}
				}
			}
			else if(Server()->Tick() >= WarningTick)
			{
				// Warn
				const char *pText = Config()->m_SvInactiveKick == 0
					? _C("Inactive kick broadcast message", "Warning: {sec:RemainingTime} until a move to spec for inactivity")
					: _C("Inactive kick broadcast message", "Warning: {sec:RemainingTime} until a kick for inactivity");
				int Seconds = (KickingTick - Server()->Tick()) / Server()->TickSpeed() + 1;
				GameServer()->SendBroadcast_Localization(pPlayer->GetCID(),
					BROADCAST_PRIORITY_INTERFACE,
					BROADCAST_DURATION_REALTIME,
					pText,
					"RemainingTime", &Seconds,
					nullptr
				);
			}
		}
	}
}

bool IGameController::OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv)
{
	vec2 Pos = (P0 + P1 + P2 + P3)/4.0f;
	
	if(str_comp(pName, "icInfected") == 0)
		m_SpawnPoints[0].add(Pos);
	else if(str_comp(pName, "icHuman") == 0)
		m_SpawnPoints[1].add(Pos);
	
	return false;
}

double IGameController::GetTime()
{
	return static_cast<double>(Server()->Tick() - m_RoundStartTick)/Server()->TickSpeed();
}

void IGameController::OnPlayerDisconnect(CPlayer *pPlayer, int Type, const char *pReason)
{
	pPlayer->OnDisconnect();

	int ClientID = pPlayer->GetCID();
	if(Server()->ClientIngame(ClientID))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", ClientID, Server()->ClientName(ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

		if(Type == CLIENTDROPTYPE_BAN)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} has been banned ({str:Reason})"),
				"PlayerName", Server()->ClientName(ClientID),
				"Reason", pReason,
				NULL);
		}
		else if(Type == CLIENTDROPTYPE_KICK)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} has been kicked ({str:Reason})"),
				"PlayerName", Server()->ClientName(ClientID),
				"Reason", pReason,
				NULL);
		}
		else if(pReason && *pReason)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} has left the game ({str:Reason})"),
				"PlayerName", Server()->ClientName(ClientID),
				"Reason", pReason,
				NULL);
		}
		else
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} has left the game"),
				"PlayerName", Server()->ClientName(ClientID),
				NULL);
		}
	}
}

void IGameController::EndRound()
{
	if(m_Warmup) // game can't end when we are running warmup
		return;

	GameServer()->m_World.m_Paused = true;
	m_GameOverTick = Server()->Tick();
	m_SuddenDeath = 0;
}

void IGameController::IncreaseCurrentRoundCounter()
{
	m_RoundCount++;
}

void IGameController::ResetGame()
{
	GameServer()->m_World.m_ResetRequested = true;
}

void IGameController::RotateMapTo(const char *pMapName)
{
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "rotating map to %s", pMapName);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	Server()->ChangeMap(pMapName);

	if(Server()->GetMapReload())
	{
		m_RoundCount = 0;
	}
}

void IGameController::DoTeamChange(CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	pPlayer->SetTeam(Team);
	int ClientID = pPlayer->GetCID();

	char aBuf[128];
	DoChatMsg = false;
	if(DoChatMsg)
	{
		str_format(aBuf, sizeof(aBuf), "'%s' joined the %s", Server()->ClientName(ClientID), GameServer()->m_pController->GetTeamName(Team));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}

	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", ClientID, Server()->ClientName(ClientID), Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// OnPlayerInfoChange(pPlayer);
}

const char *IGameController::GetTeamName(int Team)
{
	if(IsTeamplay())
	{
		if(Team == TEAM_RED)
			return "red team";
		else if(Team == TEAM_BLUE)
			return "blue team";
	}
	else
	{
		if(Team == 0)
			return "game";
	}

	return "spectators";
}

int IGameController::GetRoundCount() {
	return m_RoundCount;
}

bool IGameController::IsRoundEndTime() 
{
	return m_GameOverTick > 0;
}

void IGameController::StartRound()
{
	ResetGame();

	m_RoundId = rand();
	m_RoundStartTick = Server()->Tick();
	m_SuddenDeath = 0;
	m_GameOverTick = -1;
	GameServer()->m_World.m_Paused = false;
	m_aTeamscore[TEAM_RED] = 0;
	m_aTeamscore[TEAM_BLUE] = 0;
	m_ForceBalanced = false;
	Server()->DemoRecorder_HandleAutoStart();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start round type='%s' teamplay='%d' id='%d'", m_pGameType, m_GameFlags&GAMEFLAG_TEAMS, m_RoundId);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

void IGameController::ChangeMap(const char *pToMap)
{
	str_copy(m_aMapWish, pToMap, sizeof(m_aMapWish));
	m_aQueuedMap[0] = 0;
	EndRound();
}

void IGameController::QueueMap(const char *pToMap)
{
	str_copy(m_aQueuedMap, pToMap, sizeof(m_aQueuedMap));
}

bool IGameController::IsWordSeparator(char c)
{
	return c == ';' || c == ' ' || c == ',' || c == '\t';
}

void IGameController::GetWordFromList(char *pNextWord, const char *pList, int ListIndex)
{
	pList += ListIndex;
	int i = 0;
	while(*pList)
	{
		if (IsWordSeparator(*pList)) break;
		pNextWord[i] = *pList;
		pList++;
		i++;
	}
	pNextWord[i] = 0;
}

void IGameController::GetMapRotationInfo(CMapRotationInfo *pMapRotationInfo)
{
	pMapRotationInfo->m_MapCount = 0;

	if(!str_length(g_Config.m_SvMaprotation))
		return;

	int PreviousMapNumber = -1;
	const char *pNextMap = g_Config.m_SvMaprotation;
	const char *pCurrentMap = g_Config.m_SvMap;
	const char *pPreviousMap = Server()->GetPreviousMapName();
	bool insideWord = false;
	char aBuf[128];
	int i = 0;
	while(*pNextMap)
	{
		if (IsWordSeparator(*pNextMap))
		{
			if (insideWord)
				insideWord = false;
		}
		else // current char is not a seperator
		{
			if (!insideWord)
			{
				insideWord = true;
				pMapRotationInfo->m_MapNameIndices[pMapRotationInfo->m_MapCount] = i;
				GetWordFromList(aBuf, g_Config.m_SvMaprotation, i);
				if (str_comp(aBuf, pCurrentMap) == 0)
					pMapRotationInfo->m_CurrentMapNumber = pMapRotationInfo->m_MapCount;
				if(pPreviousMap[0] && str_comp(aBuf, pPreviousMap) == 0)
					PreviousMapNumber = pMapRotationInfo->m_MapCount;
				pMapRotationInfo->m_MapCount++;
			}
		}
		pNextMap++;
		i++;
	}
	if((pMapRotationInfo->m_CurrentMapNumber < 0) && (PreviousMapNumber >= 0))
	{
		// The current map not found in the list (probably because this map is a custom one)
		// Try to restore the rotation using the name of the previous map
		pMapRotationInfo->m_CurrentMapNumber = PreviousMapNumber;
	}
}

void IGameController::SyncSmartMapRotationData()
{
	s_aMapInfo.Clear();

	// handle maprotation
	const char *pMapRotation = Config()->m_SvMaprotation;

	const char *pNextMap = pMapRotation;
	char aMapNameBuffer[64];
	while(*pNextMap)
	{
		int WordLen = 0;
		while(pNextMap[WordLen] && !IGameController::IsWordSeparator(pNextMap[WordLen]))
		{
			aMapNameBuffer[WordLen] = pNextMap[WordLen];
			WordLen++;
		}
		aMapNameBuffer[WordLen] = 0;

		OnMapAdded(aMapNameBuffer);

		pNextMap += WordLen + 1;
	}
}

void IGameController::ConSmartMapRotationStatus()
{
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Smart maprotation: %d", Config()->m_InfSmartMapRotation);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	str_format(aBuf, sizeof(aBuf), "Maps in the rotation: %d", s_aMapInfo.Size());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	int CurrentActivePlayers = Server()->GetActivePlayerCount();

	str_format(aBuf, sizeof(aBuf), "Active players: %d", CurrentActivePlayers);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	if(s_aMapInfo.IsEmpty())
	{
		return;
	}

	int CurrentTimestamp = time_timestamp();

	int MinTimestamp = CurrentTimestamp;
	int MaxMapNameLength = 0;

	for(const CMapInfoEx &Info : s_aMapInfo)
	{
		int NameLength = strlen(Info.Name());
		if(NameLength > MaxMapNameLength)
			MaxMapNameLength = NameLength;

		if(CurrentActivePlayers < Info.MinimumPlayers)
			continue;

		if(Info.MaximumPlayers && (CurrentActivePlayers > Info.MaximumPlayers))
			continue;

		if(Info.Timestamp < MinTimestamp)
		{
			MinTimestamp = Info.Timestamp;
		}
	}

	for(int i = 0; i < s_aMapInfo.Size(); ++i)
	{
		const CMapInfoEx &Info = s_aMapInfo.At(i);

		bool Skipped = false;

		if(CurrentActivePlayers < Info.MinimumPlayers)
			Skipped = true;

		if(Info.MaximumPlayers && (CurrentActivePlayers > Info.MaximumPlayers))
			Skipped = true;

		float TimeScore = GetMapTimeScore(Info, CurrentTimestamp, MinTimestamp);
		float FitPlayersScore = GetMapFitPlayersScore(Info, CurrentActivePlayers);

		int EstimatedScore = TimeScore * 100 + FitPlayersScore * 30;
		int MapScore = Skipped ? 0 : EstimatedScore;

		str_format(aBuf, sizeof(aBuf), "- %2d %-*s Score: %3d (time: %.2f, fit players: %.2f, estimated score: %3d) | players min: %2d / max: %2d | ts: %d", i, MaxMapNameLength, Info.Name(),
			MapScore, TimeScore, FitPlayersScore, EstimatedScore, Info.MinimumPlayers, Info.MaximumPlayers, Info.Timestamp);

		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void IGameController::LoadMapRotationData()
{

}

void IGameController::SaveMapRotationData(const char *pFileName)
{
	char aBuf[256];
	IOHANDLE File = GameServer()->Storage()->OpenFile(pFileName, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		str_format(aBuf, sizeof(aBuf), "failed to save map rotation state to '%s'", pFileName);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	PrintMapRotationData(File);

	io_close(File);
	str_format(aBuf, sizeof(aBuf), "map rotation data saved to '%s'", pFileName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void IGameController::PrintMapRotationData(IOHANDLE Output)
{
	char aBuf[256];
	for(const CMapInfoEx &Info : s_aMapInfo)
	{
		str_format(aBuf, sizeof(aBuf), "add_map_data %s %d", Info.Name(), Info.Timestamp);

		if(Output)
		{
			io_write(Output, aBuf, str_length(aBuf));
			io_write_newline(Output);
		}
		else
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		}

		str_format(aBuf, sizeof(aBuf), "set_map_min_max_players %s %d %d", Info.Name(), Info.MinimumPlayers, Info.MaximumPlayers);

		if(Output)
		{
			io_write(Output, aBuf, str_length(aBuf));
			io_write_newline(Output);
		}
		else
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		}
	}
}

void IGameController::ResetMapInfo(const char *pMapName)
{
	CMapInfoEx *pMapInfo = GetMapInfo(pMapName);
	if(!pMapInfo)
	{
		return;
	}

	pMapInfo->ResetData();
}

void IGameController::AddMapTimestamp(const char *pMapName, int Timestamp)
{
	CMapInfoEx *pMapInfo = GetMapInfo(pMapName);
	if(!pMapInfo)
	{
		return;
	}

	pMapInfo->AddTimestamp(Timestamp);
}

bool IGameController::SetMapMinMaxPlayers(const char *pMapName, int MinPlayers, int MaxPlayers)
{
	CMapInfoEx *pMapInfo = GetMapInfo(pMapName);
	if(!pMapInfo)
	{
		return false;
	}

	pMapInfo->MinimumPlayers = MinPlayers;
	pMapInfo->MaximumPlayers = MaxPlayers;

	return true;
}

void IGameController::OnMapAdded(const char *pMapName)
{
	if(!GameServer()->MapExists(pMapName))
	{
		return;
	}

	s_aMapInfo.Add({});
	CMapInfoEx &Info = s_aMapInfo.Last();
	Info.SetName(pMapName);
	LoadMapConfig(pMapName, &Info);
}

void IGameController::InitSmartMapRotation()
{
	if(s_aMapInfo.IsEmpty())
	{
		SyncSmartMapRotationData();
		LoadMapRotationData();
	}
}

bool IGameController::LoadMapConfig(const char *pMapName, CMapInfo *pInfo)
{
	pInfo->MinimumPlayers = 0;
	pInfo->MaximumPlayers = 0;

	char MapInfoFilename[256];
	str_format(MapInfoFilename, sizeof(MapInfoFilename), "maps/%s.cfg", pMapName);
	IOHANDLE File = GameServer()->Storage()->OpenFile(MapInfoFilename, IOFLAG_READ, IStorage::TYPE_ALL);

	if(!File)
		return false;

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
		static const char MaxPlayersKey[] = "# mapinfo: maxplayers ";
		if(str_comp_nocase_num(MapInfoLine, MinPlayersKey, sizeof(MinPlayersKey) - 1) == 0)
		{
			pInfo->MinimumPlayers = str_toint(MapInfoLine+sizeof(MinPlayersKey) - 1);
		}
		if(str_comp_nocase_num(MapInfoLine, MaxPlayersKey, sizeof(MaxPlayersKey) - 1) == 0)
		{
			pInfo->MaximumPlayers = str_toint(MapInfoLine+sizeof(MaxPlayersKey) - 1);
		}
	}

	io_close(File);

	return true;
}

void IGameController::CycleMap(bool Forced)
{
	if(m_aMapWish[0] != 0)
	{
		RotateMapTo(m_aMapWish);
		m_aMapWish[0] = 0;
		return;
	}
	if(!Forced && m_RoundCount < g_Config.m_SvRoundsPerMap-1)
		return;

	if(m_aQueuedMap[0] != 0)
	{
		RotateMapTo(m_aQueuedMap);
		m_aQueuedMap[0] = 0;
		return;
	}

	if(!str_length(g_Config.m_SvMaprotation))
		return;

	if(Config()->m_InfSmartMapRotation)
	{
		SmartMapCycle();
	}
	else
	{
		DefaultMapCycle();
	}
}

void IGameController::DefaultMapCycle()
{
	int PlayerCount = Server()->GetActivePlayerCount();

	CMapRotationInfo pMapRotationInfo;
	GetMapRotationInfo(&pMapRotationInfo);

	if (pMapRotationInfo.m_MapCount == 0)
		return;

	char aBuf[256] = {0};
	int i=0;
	CMapInfo Info;
	if (g_Config.m_InfMaprotationRandom)
	{
		// handle random maprotation
		int RandInt;
		for ( ; i<32; i++)
		{
			RandInt = random_int(0, pMapRotationInfo.m_MapCount-1);
			GetWordFromList(aBuf, g_Config.m_SvMaprotation, pMapRotationInfo.m_MapNameIndices[RandInt]);
			LoadMapConfig(aBuf, &Info);

			if(Info.MaximumPlayers && (PlayerCount > Info.MaximumPlayers))
				continue;

			if(RandInt == pMapRotationInfo.m_CurrentMapNumber)
				continue;

			if(PlayerCount < Info.MinimumPlayers)
				continue;

			break;
		}
		i = RandInt;
	}
	else
	{
		// handle normal maprotation
		i = pMapRotationInfo.m_CurrentMapNumber + 1;
		for ( ; i != pMapRotationInfo.m_CurrentMapNumber; i++)
		{
			if (i >= pMapRotationInfo.m_MapCount)
			{
				i = 0;
				if (i == pMapRotationInfo.m_CurrentMapNumber)
					break;
			}
			GetWordFromList(aBuf, g_Config.m_SvMaprotation, pMapRotationInfo.m_MapNameIndices[i]);
			LoadMapConfig(aBuf, &Info);

			if(Info.MaximumPlayers && (PlayerCount > Info.MaximumPlayers))
				continue;

			if(PlayerCount < Info.MinimumPlayers)
				continue;

			break;
		}
	}

	if (i == pMapRotationInfo.m_CurrentMapNumber)
	{
		// couldnt find map with small enough minplayers number
		i++;
		if (i >= pMapRotationInfo.m_MapCount)
			i = 0;
		GetWordFromList(aBuf, g_Config.m_SvMaprotation, pMapRotationInfo.m_MapNameIndices[i]);
	}

	RotateMapTo(aBuf);
}

void IGameController::SmartMapCycle()
{
	if(s_aMapInfo.IsEmpty())
		return;

	const char *pCurrentMap = g_Config.m_SvMap;
	int CurrentActivePlayers = Server()->GetActivePlayerCount();

	int BestMapIndex = 0;
	int BestMapScore = 0;
	int CurrentTimestamp = time_timestamp();

	int MinTimestamp = CurrentTimestamp;

	for(const CMapInfoEx &Info : s_aMapInfo)
	{
		if(CurrentActivePlayers < Info.MinimumPlayers)
			continue;

		if(Info.MaximumPlayers && (CurrentActivePlayers > Info.MaximumPlayers))
			continue;

		if(Info.Timestamp < MinTimestamp)
		{
			MinTimestamp = Info.Timestamp;
		}
	}

	for(int i = 0; i < s_aMapInfo.Size(); ++i)
	{
		const CMapInfoEx &Info = s_aMapInfo.At(i);

		if(CurrentActivePlayers < Info.MinimumPlayers)
			continue;

		if(Info.MaximumPlayers && (CurrentActivePlayers > Info.MaximumPlayers))
			continue;

		if(str_comp(Info.Name(), pCurrentMap) == 0)
			continue;

		float TimeScore = GetMapTimeScore(Info, CurrentTimestamp, MinTimestamp);
		float FitPlayersScore = GetMapFitPlayersScore(Info, CurrentActivePlayers);

		int MapScore = TimeScore * 100 + FitPlayersScore * 30;

		if(MapScore <= BestMapScore)
			continue;

		BestMapScore = MapScore;
		BestMapIndex = i;
	}

	const CMapInfoEx &Info = s_aMapInfo.At(BestMapIndex);
	s_CachedMapIndex = BestMapIndex;

	dbg_msg("smart-rotation", "rotating to index %d (name %s)", BestMapIndex, Info.Name());
	RotateMapTo(Info.Name());
}

void IGameController::SkipMap()
{
	CycleMap(true);
	EndRound();
}
	
bool IGameController::CanVote()
{
	return true;
}

void IGameController::PostReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->Respawn();
/* INFECTION MODIFICATION START ***************************************/
			//~ GameServer()->m_apPlayers[i]->m_Score = 0;
			//~ GameServer()->m_apPlayers[i]->m_ScoreStartTick = Server()->Tick();
/* INFECTION MODIFICATION END *****************************************/
			GameServer()->m_apPlayers[i]->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
		}
	}
}

void IGameController::DoTeamBalance()
{
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "Balancing teams");

	int aT[2] = {0,0};
	float aTeamScore[2] = {0,0};
	float aPlayerScore[MAX_CLIENTS] = {0.0f};

	// gather stats
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
		{
			aT[GameServer()->m_apPlayers[i]->GetTeam()]++;
			aPlayerScore[i] = 0.0;
			aTeamScore[GameServer()->m_apPlayers[i]->GetTeam()] += aPlayerScore[i];
		}
	}

	// are teams unbalanced?
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		int BiggerTeam = (aT[0] > aT[1]) ? 0 : 1;
		int NumBalance = absolute(aT[0]-aT[1]) / 2;

		do
		{
			CPlayer *pPlayer = 0;
			float ScoreDiff = aTeamScore[BiggerTeam];
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!GameServer()->m_apPlayers[i] || !CanBeMovedOnBalance(i))
					continue;

				// remember the player who would cause lowest score-difference
				if(GameServer()->m_apPlayers[i]->GetTeam() == BiggerTeam && (!pPlayer || absolute((aTeamScore[BiggerTeam^1]+aPlayerScore[i]) - (aTeamScore[BiggerTeam]-aPlayerScore[i])) < ScoreDiff))
				{
					pPlayer = GameServer()->m_apPlayers[i];
					ScoreDiff = absolute((aTeamScore[BiggerTeam^1]+aPlayerScore[i]) - (aTeamScore[BiggerTeam]-aPlayerScore[i]));
				}
			}

			// move the player to the other team
			if(pPlayer)
			{
				int Temp = pPlayer->m_LastActionTick;
				DoTeamChange(pPlayer, BiggerTeam^1);
				pPlayer->m_LastActionTick = Temp;

				pPlayer->Respawn();
				pPlayer->m_ForceBalanced = true;
			}
		} while (--NumBalance);

		m_ForceBalanced = true;
	}

	m_UnbalancedTick = -1;
}

int IGameController::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	return 0;
}

void IGameController::OnStartRound()
{
	CMapInfoEx *pMapInfo = GetMapInfo(Config()->m_SvMap);
	if(!pMapInfo)
		return;

	int Timestamp = time_timestamp();
	pMapInfo->AddTimestamp(Timestamp);
	dbg_msg("smart-rotation", "OnStartRound: Sync timestamp of %s to %d",
		pMapInfo->Name(), Timestamp);
}

void IGameController::OnCharacterSpawn(class CCharacter *pChr)
{
}

void IGameController::DoWarmup(int Seconds)
{
	if(Seconds < 0)
		m_Warmup = 0;
	else
		m_Warmup = Seconds * Server()->TickSpeed();
}

bool IGameController::IsForceBalanced()
{
	return false;
}

bool IGameController::CanBeMovedOnBalance(int ClientID)
{
	return true;
}

void IGameController::TickBeforeWorld()
{
}

void IGameController::Tick()
{
	// do warmup
	if(m_Warmup)
	{
		m_Warmup--;
		if(!m_Warmup)
			StartRound();
	}

	if(m_GameOverTick != -1)
	{
		// game over.. wait for restart
		if(Server()->Tick() > m_GameOverTick+Server()->TickSpeed()*g_Config.m_InfShowScoreTime)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameServer()->m_apPlayers[i])
				{
					GameServer()->m_apPlayers[i]->SetScoreMode(Server()->GetClientDefaultScoreMode(i));
				}
			}
			
			CycleMap();
			if(!Server()->GetMapReload())
			{
				StartRound();
				IncreaseCurrentRoundCounter();
			}
		}
		else
		{
			int ScoreMode = PLAYERSCOREMODE_SCORE;
			if((Server()->Tick() - m_GameOverTick) > Server()->TickSpeed() * (g_Config.m_InfShowScoreTime/2.0f))
			{
				ScoreMode = PLAYERSCOREMODE_TIME;
			}
			
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameServer()->m_apPlayers[i])
				{
					GameServer()->m_apPlayers[i]->SetScoreMode(ScoreMode);
				}
			}
		}
	}

	// game is Paused
	if(GameServer()->m_World.m_Paused)
		++m_RoundStartTick;

	// do team-balancing
	if(IsTeamplay() && m_UnbalancedTick != -1 && Server()->Tick() > m_UnbalancedTick+g_Config.m_SvTeambalanceTime*Server()->TickSpeed()*60)
	{
		DoTeamBalance();
	}

	DoActivityCheck();
}


bool IGameController::IsTeamplay() const
{
	return m_GameFlags&GAMEFLAG_TEAMS;
}

void IGameController::Snap(int SnappingClient)
{
}

int IGameController::GetAutoTeam(int NotThisID)
{
	// this will force the auto balancer to work overtime as well
#ifdef CONF_DEBUG
	if(g_Config.m_DbgStress)
		return 0;
#endif

	int aNumplayers[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	int Team = 0;

	if(CanJoinTeam(Team, NotThisID))
		return Team;
	return -1;
}

bool IGameController::CanJoinTeam(int Team, int NotThisID)
{
	if(Team == TEAM_SPECTATORS || (GameServer()->m_apPlayers[NotThisID] && GameServer()->m_apPlayers[NotThisID]->GetTeam() != TEAM_SPECTATORS))
		return true;

	int aNumplayers[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	bool NumbersAreOk = (aNumplayers[0] + aNumplayers[1]) < Server()->MaxClients() - g_Config.m_SvSpectatorSlots;
	if(!NumbersAreOk)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Server()->MaxClients() - g_Config.m_SvSpectatorSlots);
		GameServer()->SendBroadcast(NotThisID, aBuf, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
	}

	return NumbersAreOk;
}

bool IGameController::CheckTeamBalance()
{
	if(!IsTeamplay() || !g_Config.m_SvTeambalanceTime)
		return true;

	int aT[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
			aT[pP->GetTeam()]++;
	}

	char aBuf[256];
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		str_format(aBuf, sizeof(aBuf), "Teams are NOT balanced (red=%d blue=%d)", aT[0], aT[1]);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		if(GameServer()->m_pController->m_UnbalancedTick == -1)
			GameServer()->m_pController->m_UnbalancedTick = Server()->Tick();
		return false;
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "Teams are balanced (red=%d blue=%d)", aT[0], aT[1]);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		GameServer()->m_pController->m_UnbalancedTick = -1;
		return true;
	}
}

bool IGameController::CanChangeTeam(CPlayer *pPlayer, int JoinTeam)
{
	int aT[2] = {0, 0};

	if (!IsTeamplay() || JoinTeam == TEAM_SPECTATORS || !g_Config.m_SvTeambalanceTime)
		return true;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
			aT[pP->GetTeam()]++;
	}

	// simulate what would happen if changed team
	aT[JoinTeam]++;
	if (pPlayer->GetTeam() != TEAM_SPECTATORS)
		aT[JoinTeam^1]--;

	// there is a player-difference of at least 2
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		// player wants to join team with less players
		if ((aT[0] < aT[1] && JoinTeam == TEAM_RED) || (aT[0] > aT[1] && JoinTeam == TEAM_BLUE))
			return true;
		else
			return false;
	}
	else
		return true;
}

void IGameController::DoWincheck()
{
}

int IGameController::ClampTeam(int Team)
{
	if(Team < 0)
		return TEAM_SPECTATORS;
	return 0;
}
