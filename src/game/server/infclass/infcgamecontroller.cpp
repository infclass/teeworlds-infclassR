/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include "infcgamecontroller.h"

#include <game/server/infclass/entities/flyingpoint.h>
#include <game/server/player.h>
#include <engine/shared/config.h>
#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/network.h>
#include <game/mapitems.h>
#include <time.h>

#include <algorithm>
#include <iostream>
#include <map>

CInfClassGameController::CInfClassGameController(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
	m_pGameType = "InfClassR";
	
	m_GrowingMap = 0;
	
	m_ExplosionStarted = false;
	m_MapWidth = GameServer()->Collision()->GetWidth();
	m_MapHeight = GameServer()->Collision()->GetHeight();
	m_GrowingMap = new int[m_MapWidth*m_MapHeight];
	
	m_InfectedStarted = false;
	
	for(int j=0; j<m_MapHeight; j++)
	{
		for(int i=0; i<m_MapWidth; i++)
		{
			vec2 TilePos = vec2(16.0f, 16.0f) + vec2(i*32.0f, j*32.0f);
			if(GameServer()->Collision()->CheckPoint(TilePos))
			{
				m_GrowingMap[j*m_MapWidth+i] = 4;
			}
			else
			{
				m_GrowingMap[j*m_MapWidth+i] = 1;
			}
		}
	}
}

CInfClassGameController::~CInfClassGameController()
{
	if(m_GrowingMap) delete[] m_GrowingMap;
}

void CInfClassGameController::OnClientDrop(int ClientID, int Type)
{
	if(Type == CLIENTDROPTYPE_BAN) return;
	if(Type == CLIENTDROPTYPE_KICK) return;
	if(Type == CLIENTDROPTYPE_SHUTDOWN) return;	
	
	CPlayer* pPlayer = GameServer()->m_apPlayers[ClientID];
	if(pPlayer && pPlayer->IsZombie() && m_InfectedStarted)
	{
		int NumHumans;
		int NumInfected;
		int NumFirstInfected;
		GetPlayerCounter(ClientID, NumHumans, NumInfected, NumFirstInfected);
		
		if(NumInfected < NumFirstInfected)
		{
			Server()->Ban(ClientID, 60*g_Config.m_InfLeaverBanTime, "Leaver");
		}
    }
}

void CInfClassGameController::OnPlayerInfected(CPlayer *pPlayer, CPlayer *pInfectiousPlayer)
{
	if (!pInfectiousPlayer || pInfectiousPlayer->GetTeam() == TEAM_SPECTATORS || pPlayer->GetCID() == pInfectiousPlayer->GetCID()) {
		if (pPlayer->GetCharacter())
		{
			// Still send a kill message to notify other players about the infection
			GameServer()->SendKillMessage(pPlayer->GetCID(), pPlayer->GetCID(), WEAPON_WORLD, 0);
			GameServer()->CreateSound(pPlayer->GetCharacter()->m_Pos, SOUND_PLAYER_DIE);
		}

		return;
	}

	const int InfectedByCID = pInfectiousPlayer->GetCID();
	GameServer()->SendChatTarget_Localization(InfectedByCID, CHATCATEGORY_SCORE, _("You have infected {str:VictimName}, +3 points"), "VictimName", Server()->ClientName(pPlayer->GetCID()), NULL);
	Server()->RoundStatistics()->OnScoreEvent(InfectedByCID, SCOREEVENT_INFECTION, pInfectiousPlayer->GetClass(), Server()->ClientName(InfectedByCID), GameServer()->Console());
	GameServer()->SendScoreSound(InfectedByCID);

	//Search for hook
	for(CCharacter *pHook = (CCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHook; pHook = (CCharacter *)pHook->TypeNext())
	{
		if(
			pHook->GetPlayer() &&
			pHook->m_Core.m_HookedPlayer == pPlayer->GetCID() &&
			pHook->GetPlayer()->GetCID() != InfectedByCID
		)
		{
			Server()->RoundStatistics()->OnScoreEvent(pHook->GetPlayer()->GetCID(), SCOREEVENT_HELP_HOOK_INFECTION, pHook->GetClass(), Server()->ClientName(pHook->GetPlayer()->GetCID()), GameServer()->Console());
			GameServer()->SendScoreSound(pHook->GetPlayer()->GetCID());
		}
	}
}

bool CInfClassGameController::OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv)
{
	bool res = IGameController::OnEntity(pName, Pivot, P0, P1, P2, P3, PosEnv);

	if(str_comp(pName, "icInfected") == 0)
	{
		vec2 Pos = (P0 + P1 + P2 + P3)/4.0f;
		int SpawnX = static_cast<int>(Pos.x)/32.0f;
		int SpawnY = static_cast<int>(Pos.y)/32.0f;
		
		if(SpawnX >= 0 && SpawnX < m_MapWidth && SpawnY >= 0 && SpawnY < m_MapHeight)
		{
			m_GrowingMap[SpawnY*m_MapWidth+SpawnX] = 6;
		}
	}
	else if(str_comp(pName, "icHeroFlag") == 0)
	{
		//Add hero flag spawns
		vec2 Pos = (P0 + P1 + P2 + P3)/4.0f;
		m_HeroFlagPositions.add(Pos);
	}

	return res;
}

void CInfClassGameController::ResetFinalExplosion()
{
	m_ExplosionStarted = false;
	
	for(int j=0; j<m_MapHeight; j++)
	{
		for(int i=0; i<m_MapWidth; i++)
		{
			if(!(m_GrowingMap[j*m_MapWidth+i] & 4))
			{
				m_GrowingMap[j*m_MapWidth+i] = 1;
			}
		}
	}
}

bool CInfClassGameController::IsDefenderClass(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_ENGINEER:
		case PLAYERCLASS_SOLDIER:
		case PLAYERCLASS_SCIENTIST:
		case PLAYERCLASS_BIOLOGIST:
		case PLAYERCLASS_LOOPER:
			return true;
		default:
			return false;
	}
}

bool CInfClassGameController::IsSupportClass(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_NINJA:
		case PLAYERCLASS_MERCENARY:
		case PLAYERCLASS_SNIPER:
			return true;
		default:
			return false;
	}
}

int CInfClassGameController::GetClassByName(const char *pClassName, bool *pOk)
{
	struct ExtraName
	{
		ExtraName(const char *pN, int Class)
			: pName(pN)
			, PlayerClass(Class)
		{
		}

		const char *pName = nullptr;
		int PlayerClass = 0;
	};

	static const ExtraName extraNames[] =
	{
		ExtraName("merc", PLAYERCLASS_MERCENARY),
		ExtraName("mercs", PLAYERCLASS_MERCENARY),
		ExtraName("engi", PLAYERCLASS_ENGINEER),
		ExtraName("bio", PLAYERCLASS_BIOLOGIST),
		ExtraName("bios", PLAYERCLASS_BIOLOGIST),
	};
	if(pOk)
	{
		*pOk = true;
	}
	for(const ExtraName &Extra : extraNames)
	{
		if(str_comp(pClassName, Extra.pName) == 0)
		{
			return Extra.PlayerClass;
		}
	}

	for (int PlayerClass = 0; PlayerClass < NB_PLAYERCLASS; ++PlayerClass)
	{
		const char *pSingularName = CInfClassGameController::GetClassName(PlayerClass);
		const char *pPluralName = CInfClassGameController::GetClassPluralName(PlayerClass);
		if((str_comp(pClassName, pSingularName) == 0) || (str_comp(pClassName, pPluralName) == 0)) {
			return PlayerClass;
		}
	}

	if(pOk)
	{
		*pOk = false;
	}
	return PLAYERCLASS_NONE;
}

const char *CInfClassGameController::GetClassName(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_NONE:
			return "none";

		case PLAYERCLASS_MERCENARY:
			return "mercenary";
		case PLAYERCLASS_MEDIC:
			return "medic";
		case PLAYERCLASS_HERO:
			return "hero";
		case PLAYERCLASS_ENGINEER:
			return "engineer";
		case PLAYERCLASS_SOLDIER:
			return "soldier";
		case PLAYERCLASS_NINJA:
			return "ninja";
		case PLAYERCLASS_SNIPER:
			return "sniper";
		case PLAYERCLASS_SCIENTIST:
			return "scientist";
		case PLAYERCLASS_BIOLOGIST:
			return "biologist";
		case PLAYERCLASS_LOOPER:
			return "looper";

		case PLAYERCLASS_SMOKER:
			return "smoker";
		case PLAYERCLASS_BOOMER:
			return "boomer";
		case PLAYERCLASS_HUNTER:
			return "hunter";
		case PLAYERCLASS_BAT:
			return "bat";
		case PLAYERCLASS_GHOST:
			return "ghost";
		case PLAYERCLASS_SPIDER:
			return "spider";
		case PLAYERCLASS_GHOUL:
			return "ghoul";
		case PLAYERCLASS_SLUG:
			return "slug";
		case PLAYERCLASS_VOODOO:
			return "voodoo";
		case PLAYERCLASS_WITCH:
			return "witch";
		case PLAYERCLASS_UNDEAD:
			return "undead";

		default:
			return "unknown";
	}
}

const char *CInfClassGameController::GetClassPluralName(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_MERCENARY:
			return "mercenaries";
		case PLAYERCLASS_MEDIC:
			return "medics";
		case PLAYERCLASS_HERO:
			return "heroes";
		case PLAYERCLASS_ENGINEER:
			return "engineers";
		case PLAYERCLASS_SOLDIER:
			return "soldiers";
		case PLAYERCLASS_NINJA:
			return "ninjas";
		case PLAYERCLASS_SNIPER:
			return "snipers";
		case PLAYERCLASS_SCIENTIST:
			return "scientists";
		case PLAYERCLASS_BIOLOGIST:
			return "biologists";
		case PLAYERCLASS_LOOPER:
			return "loopers";

		case PLAYERCLASS_SMOKER:
			return "smokers";
		case PLAYERCLASS_BOOMER:
			return "boomers";
		case PLAYERCLASS_HUNTER:
			return "hunters";
		case PLAYERCLASS_BAT:
			return "bats";
		case PLAYERCLASS_GHOST:
			return "ghosts";
		case PLAYERCLASS_SPIDER:
			return "spiders";
		case PLAYERCLASS_GHOUL:
			return "ghouls";
		case PLAYERCLASS_SLUG:
			return "slugs";
		case PLAYERCLASS_VOODOO:
			return "voodoos";
		case PLAYERCLASS_WITCH:
			return "witches";
		case PLAYERCLASS_UNDEAD:
			return "undeads";

		default:
			return "unknown";
	}
}

const char *CInfClassGameController::GetClassDisplayName(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_NONE:
			return "None";

		case PLAYERCLASS_MERCENARY:
			return "Mercenary";
		case PLAYERCLASS_MEDIC:
			return "Medic";
		case PLAYERCLASS_HERO:
			return "Hero";
		case PLAYERCLASS_ENGINEER:
			return "Engineer";
		case PLAYERCLASS_SOLDIER:
			return "Soldier";
		case PLAYERCLASS_NINJA:
			return "Ninja";
		case PLAYERCLASS_SNIPER:
			return "Sniper";
		case PLAYERCLASS_SCIENTIST:
			return "Scientist";
		case PLAYERCLASS_BIOLOGIST:
			return "Biologist";
		case PLAYERCLASS_LOOPER:
			return "Looper";

		case PLAYERCLASS_SMOKER:
			return "Smoker";
		case PLAYERCLASS_BOOMER:
			return "Boomer";
		case PLAYERCLASS_HUNTER:
			return "Hunter";
		case PLAYERCLASS_BAT:
			return "Bat";
		case PLAYERCLASS_GHOST:
			return "Ghost";
		case PLAYERCLASS_SPIDER:
			return "Spider";
		case PLAYERCLASS_GHOUL:
			return "Ghoul";
		case PLAYERCLASS_SLUG:
			return "Slug";
		case PLAYERCLASS_VOODOO:
			return "Voodoo";
		case PLAYERCLASS_WITCH:
			return "Witch";
		case PLAYERCLASS_UNDEAD:
			return "Undead";

		default:
			return "unknown";
	}
}

const char *CInfClassGameController::GetClassPluralDisplayName(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_MERCENARY:
			return "Mercenaries";
		case PLAYERCLASS_MEDIC:
			return "Medics";
		case PLAYERCLASS_HERO:
			return "Heroes";
		case PLAYERCLASS_ENGINEER:
			return "Engineers";
		case PLAYERCLASS_SOLDIER:
			return "Soldiers";
		case PLAYERCLASS_NINJA:
			return "Ninjas";
		case PLAYERCLASS_SNIPER:
			return "Snipers";
		case PLAYERCLASS_SCIENTIST:
			return "Scientists";
		case PLAYERCLASS_BIOLOGIST:
			return "Biologists";
		case PLAYERCLASS_LOOPER:
			return "Loopers";

		case PLAYERCLASS_SMOKER:
			return "Smokers";
		case PLAYERCLASS_BOOMER:
			return "Boomers";
		case PLAYERCLASS_HUNTER:
			return "Hunters";
		case PLAYERCLASS_BAT:
			return "Bats";
		case PLAYERCLASS_GHOST:
			return "Ghosts";
		case PLAYERCLASS_SPIDER:
			return "Spiders";
		case PLAYERCLASS_GHOUL:
			return "Ghouls";
		case PLAYERCLASS_SLUG:
			return "Slugs";
		case PLAYERCLASS_VOODOO:
			return "Voodoos";
		case PLAYERCLASS_WITCH:
			return "Witches";
		case PLAYERCLASS_UNDEAD:
			return "Undeads";

		default:
			return "unknown";
	}
}

void CInfClassGameController::RegisterChatCommands(IConsole *pConsole)
{
	pConsole->Register("inf_set_class", "is", CFGFLAG_SERVER, ConSetClass, this, "Set the class of a player");

	pConsole->Register("witch", "", CFGFLAG_CHAT|CFGFLAG_USER, ChatWitch, this, "Call Witch");
}

bool CInfClassGameController::ConSetClass(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	return pSelf->ConSetClass(pResult);
}

bool CInfClassGameController::ConSetClass(IConsole::IResult *pResult)
{
	int PlayerID = pResult->GetInteger(0);
	const char *pClassName = pResult->GetString(1);

	CPlayer* pPlayer = GameServer()->m_apPlayers[PlayerID];

	if(!pPlayer)
		return true;

	bool Ok = false;
	int PlayerClass = GetClassByName(pClassName, &Ok);
	if(Ok)
	{
		pPlayer->SetClass(PlayerClass);
		char aBuf[256];
		const char *pClassDisplayName = GetClassDisplayName(PlayerClass);
		str_format(aBuf, sizeof(aBuf), "The admin change the class of %s to %s", GameServer()->Server()->ClientName(PlayerID), pClassDisplayName);
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

		return true;
	}

	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "inf_set_class", "Unknown class");
	return true;
}

bool CInfClassGameController::ChatWitch(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	return pSelf->ChatWitch(pResult);
}

bool CInfClassGameController::ChatWitch(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetClientID();
	int callers_count = m_WitchCallers.size();
	const int REQUIRED_CALLERS_COUNT = 5;
	const int MIN_ZOMBIES = 2;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "ConWitch() called");
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "conwitch", aBuf);

	if (GameServer()->GetZombieCount(PLAYERCLASS_WITCH) >= Server()->GetClassPlayerLimit(PLAYERCLASS_WITCH)) {
		str_format(aBuf, sizeof(aBuf), "All witches are already here");
		GameServer()->SendChatTarget(ClientID, aBuf);
		return true;
	}
	if (GameServer()->GetZombieCount() <= MIN_ZOMBIES) {
		str_format(aBuf, sizeof(aBuf), "Too few zombies");
		GameServer()->SendChatTarget(ClientID, aBuf);
		return true;
	}

	if (callers_count < REQUIRED_CALLERS_COUNT) {
		auto& wc = m_WitchCallers;
		if(!(std::find(wc.begin(), wc.end(), ClientID) != wc.end()))
		{
			wc.push_back(ClientID); // add to witch callers vector
			callers_count += 1;
			if (callers_count == 1)
				str_format(aBuf, sizeof(aBuf), "%s is calling for Witch! (%d/%d) To call witch write: /witch",
						Server()->ClientName(ClientID), callers_count, REQUIRED_CALLERS_COUNT);
			else
				str_format(aBuf, sizeof(aBuf), "Witch (%d/%d)", callers_count, REQUIRED_CALLERS_COUNT);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "You can't call witch twice");
			GameServer()->SendChatTarget(ClientID, aBuf);
			return true;
		}
	}

	if (callers_count >= REQUIRED_CALLERS_COUNT)
	{
		int WitchId = RandomZombieToWitch();
		if(WitchId < 0)
		{
			str_format(aBuf, sizeof(aBuf), "All witches are already here");
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Witch %s has arrived!", Server()->ClientName(WitchId));
		}
	}

	GameServer()->SendChatTarget(-1, aBuf);
	return true;
}

CGameWorld *CInfClassGameController::GameWorld()
{
	return &GameServer()->m_World;
}

void CInfClassGameController::EndRound()
{	
	m_InfectedStarted = false;
	ResetFinalExplosion();
	IGameController::EndRound();
}

void CInfClassGameController::GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected, int& NumFirstInfected)
{
	NumHumans = 0;
	NumInfected = 0;
	
	//Count type of players
	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if(Iter.ClientID() == ClientException) continue;
		
		if(Iter.Player()->IsZombie()) NumInfected++;
		else NumHumans++;
	}
	
	const int NumPlayers = NumHumans + NumInfected;

	if(NumPlayers > 20)
		NumFirstInfected = 4;
	else if(NumPlayers > 8)
		NumFirstInfected = 3;
	else if(NumPlayers > 3)
		NumFirstInfected = 2;
	else if(NumPlayers > 1)
		NumFirstInfected = 1;
	else
		NumFirstInfected = 0;

	int FirstInfectedLimit = g_Config.m_InfFirstInfectedLimit;
	if(FirstInfectedLimit && NumFirstInfected > FirstInfectedLimit)
	{
		NumFirstInfected = FirstInfectedLimit;
	}
}

int CInfClassGameController::RandomZombieToWitch() {
	std::vector<int> zombies_id;

	m_WitchCallers.clear();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameServer()->m_apPlayers[i])
			continue;
		if(GameServer()->m_apPlayers[i]->GetClass() == PLAYERCLASS_WITCH)
			continue;
		if(GameServer()->m_apPlayers[i]->IsActuallyZombie()) {
			zombies_id.push_back(i);
		}
	}

	if(zombies_id.empty())
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "witch", "Unable to find any suitable player");
		return -1;
	}

	int id = random_int(0, zombies_id.size() - 1);
	char aBuf[512];
	/* debug */
	str_format(aBuf, sizeof(aBuf), "going through MAX_CLIENTS=%d, zombie_count=%d, random_int=%d, id=%d", MAX_CLIENTS, zombies_id.size(), id, zombies_id[id]);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "witch", aBuf);
	/* /debug */
	GameServer()->m_apPlayers[zombies_id[id]]->SetClass(PLAYERCLASS_WITCH);
	return zombies_id[id];
}

void CInfClassGameController::Tick()
{
	IGameController::Tick();
	
	//Check session
	{
		CPlayerIterator<PLAYERITER_ALL> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			//Update session
			IServer::CClientSession* pSession = Server()->GetClientSession(Iter.ClientID());
			if(pSession)
			{
				if(!Server()->GetClientMemory(Iter.ClientID(), CLIENTMEMORY_SESSION_PROCESSED))
				{
					//The client already participated to this round,
					//and he exit the game as infected.
					//To avoid cheating, we assign to him the same class again.
					if(
						m_InfectedStarted &&
						pSession->m_RoundId == m_RoundId &&
						pSession->m_Class > END_HUMANCLASS
					)
					{
						Iter.Player()->SetClass(pSession->m_Class);
					}
					
					Server()->SetClientMemory(Iter.ClientID(), CLIENTMEMORY_SESSION_PROCESSED, true);
				}
				
				pSession->m_Class = Iter.Player()->GetClass();
				pSession->m_RoundId = GameServer()->m_pController->GetRoundId();
			}
		}
	}
	
	int NumHumans = 0;
	int NumInfected = 0;
	int NumFirstInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected, NumFirstInfected);
	
	m_InfectedStarted = false;

	//If the game can start ...
	if(m_GameOverTick == -1 && NumHumans + NumInfected >= g_Config.m_InfMinPlayers)
	{
		//If the infection started
		if(IsInfectionStarted())
		{
			bool StartInfectionTrigger = (m_RoundStartTick + Server()->TickSpeed()*10 == Server()->Tick());
			
			GameServer()->EnableTargetToKill();
			
			m_InfectedStarted = true;
	
			CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
			while(Iter.Next())
			{
				if(Iter.Player()->GetClass() == PLAYERCLASS_NONE)
				{
					if(StartInfectionTrigger)
					{
						Iter.Player()->SetClass(ChooseHumanClass(Iter.Player()));
						if(Iter.Player()->GetCharacter())
							Iter.Player()->GetCharacter()->IncreaseArmor(10);
					}
					else
					{
						Iter.Player()->StartInfection();
						NumInfected++;
						NumHumans--;
					}
				}
			}
			
			int NumNeededInfection = NumFirstInfected;

			while(NumInfected < NumNeededInfection)
			{
				// before infecting those who play, mark spectators as
				// already infected. It will prevent issue causing a
				// player getting infected several times in a row
				CPlayerIterator<PLAYERITER_SPECTATORS> IterSpec(GameServer()->m_apPlayers);
				while(IterSpec.Next())
				{
					IterSpec.Player()->SetClass(PLAYERCLASS_NONE);
					Server()->InfecteClient(IterSpec.ClientID());
				}

				float InfectionProb = 1.0/static_cast<float>(NumHumans);
				float random = random_float();
				
				//Fair infection
				bool FairInfectionFound = false;
				
				Iter.Reset();
				while(Iter.Next())
				{
					if(Iter.Player()->IsZombie()) continue;
					
					if(!Server()->IsClientInfectedBefore(Iter.ClientID()))
					{
						Server()->InfecteClient(Iter.ClientID());
						Iter.Player()->StartInfection();
						NumInfected++;
						NumHumans--;
						
						GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION, _("{str:VictimName} has been infected"),
							"VictimName", Server()->ClientName(Iter.ClientID()),
							NULL
						);
						FairInfectionFound = true;
						break;
					}
				}
				
				//Unfair infection
				if(!FairInfectionFound)
				{
					Iter.Reset();
					while(Iter.Next())
					{
						if(Iter.Player()->IsZombie()) continue;
						
						if(random < InfectionProb)
						{
							Server()->InfecteClient(Iter.ClientID());
							Iter.Player()->StartInfection();
							NumInfected++;
							NumHumans--;
							
							GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION, _("{str:VictimName} has been infected"), "VictimName", Server()->ClientName(Iter.ClientID()), NULL);
							
							break;
						}
						else
						{
							random -= InfectionProb;
						}
					}
				}
			}
		}
		else
		{			
			GameServer()->DisableTargetToKill();
			
			CPlayerIterator<PLAYERITER_SPECTATORS> IterSpec(GameServer()->m_apPlayers);
			while(IterSpec.Next())
			{
				IterSpec.Player()->SetClass(PLAYERCLASS_NONE);
			}
		}

		DoWincheck();
	}
	else
	{
		GameServer()->DisableTargetToKill();
		
		m_RoundStartTick = Server()->Tick();
	}
}

bool CInfClassGameController::IsInfectionStarted()
{
	return (m_RoundStartTick + Server()->TickSpeed()*10 <= Server()->Tick());
}

bool CInfClassGameController::PortalsAvailableForCharacter(class CCharacter *pCharacter)
{
	if (!g_Config.m_InfEnableWitchPortals)
		return false;

	if (GameServer()->m_FunRound)
		return false;

	if (pCharacter->GetClass() != PLAYERCLASS_WITCH)
		return false;

	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if (!Iter.Player()->GetCharacter())
			continue;

		if (Iter.Player()->GetCharacter() == pCharacter)
			continue;

		if (Iter.Player()->GetCharacter()->CanOpenPortals())
			return false;
	}

	return true;
}

void CInfClassGameController::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = m_GameFlags;
	pGameInfoObj->m_GameStateFlags = 0;
	if(m_GameOverTick != -1)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	if(m_SuddenDeath)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
	if(GameServer()->m_World.m_Paused)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
	pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
	pGameInfoObj->m_WarmupTimer = m_Warmup;

	pGameInfoObj->m_ScoreLimit = g_Config.m_SvScorelimit;
	pGameInfoObj->m_TimeLimit = g_Config.m_SvTimelimit;

	pGameInfoObj->m_RoundNum = (str_length(g_Config.m_SvMaprotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;
	pGameInfoObj->m_RoundCurrent = m_RoundCount+1;

	//Generate class mask
	int ClassMask = 0;
	{
		int Defender = 0;
		int Medic = 0;
		int Hero = 0;
		int Support = 0;
		
		CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			switch(Iter.Player()->GetClass())
			{
				case PLAYERCLASS_NINJA:
				case PLAYERCLASS_MERCENARY:
				case PLAYERCLASS_SNIPER:
					Support++;
					break;
				case PLAYERCLASS_ENGINEER:
				case PLAYERCLASS_SOLDIER:
				case PLAYERCLASS_SCIENTIST:
				case PLAYERCLASS_BIOLOGIST:
					Defender++;
					break;
				case PLAYERCLASS_MEDIC:
					Medic++;
					break;
				case PLAYERCLASS_HERO:
					Hero++;
					break;
				case PLAYERCLASS_LOOPER:
					Defender++;
					break;
					
			}
		}
		
		if(Defender < g_Config.m_InfDefenderLimit)
			ClassMask |= CMapConverter::MASK_DEFENDER;
		if(Medic < g_Config.m_InfMedicLimit)
			ClassMask |= CMapConverter::MASK_MEDIC;
		if(Hero < g_Config.m_InfHeroLimit)
			ClassMask |= CMapConverter::MASK_HERO;
		if(Support < g_Config.m_InfSupportLimit)
			ClassMask |= CMapConverter::MASK_SUPPORT;
	}
	
	if(GameServer()->m_apPlayers[SnappingClient])
	{
		int Page = -1;
		
		if(GameServer()->m_apPlayers[SnappingClient]->MapMenu() == 1)
		{
			int Item = GameServer()->m_apPlayers[SnappingClient]->m_MapMenuItem;
			Page = CMapConverter::TIMESHIFT_MENUCLASS + 3*((Item+1) + ClassMask*CMapConverter::TIMESHIFT_MENUCLASS_MASK) + 1;
		}
		
		if(Page >= 0)
		{
			double PageShift = static_cast<double>(Page * Server()->GetTimeShiftUnit())/1000.0f;
			double CycleShift = fmod(static_cast<double>(Server()->Tick() - pGameInfoObj->m_RoundStartTick)/Server()->TickSpeed(), Server()->GetTimeShiftUnit()/1000.0);
			int TimeShift = (PageShift + CycleShift)*Server()->TickSpeed();
			
			pGameInfoObj->m_RoundStartTick = Server()->Tick() - TimeShift;
			pGameInfoObj->m_TimeLimit += (TimeShift/Server()->TickSpeed())/60;
		}
	}

	CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
	if(!pGameDataObj)
		return;

	//Search for witch
	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if(Iter.Player()->GetClass() == PLAYERCLASS_WITCH)
		{
			pGameDataObj->m_FlagCarrierRed = Iter.ClientID();
		}
	}
	
	pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
}

void CInfClassGameController::RewardTheKiller(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return;

	if(pKiller->IsHuman())
	{
		if(pKiller == pVictim->GetPlayer())
		{
			Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_HUMAN_SUICIDE, pKiller->GetClass(), Server()->ClientName(pKiller->GetCID()), GameServer()->Console());
		}
		else
		{
			if(pVictim->GetClass() == PLAYERCLASS_WITCH)
			{
				GameServer()->SendChatTarget_Localization(pKiller->GetCID(), CHATCATEGORY_SCORE, _("You have killed a witch, +5 points"), NULL);
				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_KILL_WITCH, pKiller->GetClass(), Server()->ClientName(pKiller->GetCID()), GameServer()->Console());
				GameServer()->SendScoreSound(pKiller->GetCID());
			}
			else if(pVictim->GetClass() == PLAYERCLASS_UNDEAD)
			{
				GameServer()->SendChatTarget_Localization(pKiller->GetCID(), CHATCATEGORY_SCORE, _("You have killed an undead! +5 points"), NULL);
				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_KILL_UNDEAD, pKiller->GetClass(), Server()->ClientName(pKiller->GetCID()), GameServer()->Console());
				GameServer()->SendScoreSound(pKiller->GetCID());
			}
			else if(pVictim->IsZombie())
			{
				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_KILL_INFECTED, pKiller->GetClass(), Server()->ClientName(pKiller->GetCID()), GameServer()->Console());
				GameServer()->SendScoreSound(pKiller->GetCID());
			}
		
			if(pKiller->GetClass() == PLAYERCLASS_NINJA && pVictim->GetPlayer()->GetCID() == GameServer()->GetTargetToKill())
			{
				GameServer()->SendChatTarget_Localization(pKiller->GetCID(), CHATCATEGORY_SCORE, _("You have eliminated your target, +2 points"), NULL);
				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_KILL_TARGET, pKiller->GetClass(), Server()->ClientName(pKiller->GetCID()), GameServer()->Console());
				GameServer()->TargetKilled();
				
				if(pKiller->GetCharacter())
				{
					pKiller->GetCharacter()->GiveNinjaBuf();
					pKiller->GetCharacter()->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
					GameServer()->SendEmoticon(pKiller->GetCID(), EMOTICON_MUSIC);
					pKiller->GetCharacter()->IncreaseHealth(4);
				}
			}
		}
	}
}

int CInfClassGameController::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	RewardTheKiller(pVictim, pKiller, Weapon);

	//Add bonus point for ninja
	if(pVictim->IsZombie() && pVictim->IsFrozen() && pVictim->m_LastFreezer >= 0 && pVictim->m_LastFreezer)
	{
		CPlayer* pFreezer = GameServer()->m_apPlayers[pVictim->m_LastFreezer];
		if(pFreezer && pFreezer != pKiller)
		{
			if (pFreezer->GetClass() == PLAYERCLASS_NINJA)
			{
				Server()->RoundStatistics()->OnScoreEvent(pFreezer->GetCID(), SCOREEVENT_HELP_FREEZE, pFreezer->GetClass(), Server()->ClientName(pFreezer->GetCID()), GameServer()->Console());
				GameServer()->SendScoreSound(pFreezer->GetCID());
				
				if(pVictim->GetPlayer()->GetCID() == GameServer()->GetTargetToKill())
				{
					GameServer()->SendChatTarget_Localization(pFreezer->GetCID(), CHATCATEGORY_SCORE, _("You have eliminated your target, +2 points"), NULL);
					Server()->RoundStatistics()->OnScoreEvent(pFreezer->GetCID(), SCOREEVENT_KILL_TARGET, pFreezer->GetClass(), Server()->ClientName(pFreezer->GetCID()), GameServer()->Console());
					GameServer()->TargetKilled();
					
					if(pFreezer->GetCharacter())
					{
						pFreezer->GetCharacter()->GiveNinjaBuf();
						pFreezer->GetCharacter()->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
						GameServer()->SendEmoticon(pFreezer->GetCID(), EMOTICON_MUSIC);
					}
				}
			}
		}
	}
	
	//Find the nearest ghoul
	{
		for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			if(p->GetClass() != PLAYERCLASS_GHOUL || p == pVictim) continue;
			if(p->GetPlayer() && p->GetPlayer()->GetGhoulPercent() >= 1.0f) continue;

			float Len = distance(p->m_Pos, pVictim->m_Pos);
			
			if(p && Len < 800.0f)
			{
				int Points = (pVictim->IsZombie() ? 8 : 14);
				new CFlyingPoint(GameServer(), pVictim->m_Pos, p->GetPlayer()->GetCID(), Points, pVictim->m_Core.m_Vel);
			}
		}
	}
	
	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*3.0f;
		
	return 0;
}

void CInfClassGameController::OnCharacterSpawn(class CCharacter *pChr)
{
	// default health
	pChr->IncreaseHealth(10);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);

	if(GameServer()->m_FunRound && !IsInfectionStarted() && pChr->GetClass() == PLAYERCLASS_NONE)
	{
		CPlayer *pPlayer = pChr->GetPlayer();
		if(pPlayer)
		{
			pPlayer->SetClass(ChooseHumanClass(pPlayer));
		}
	}
}

void CInfClassGameController::OnPlayerInfoChange(class CPlayer *pP)
{
	//~ std::cout << "SkinName : " << pP->m_TeeInfos.m_SkinName << std::endl;
	//~ std::cout << "ColorBody : " << pP->m_TeeInfos.m_ColorBody << std::endl;
	//~ std::cout << "ColorFeet : " << pP->m_TeeInfos.m_ColorFeet << std::endl;
	//~ pP->SetClassSkin(pP->GetClass());
}

void CInfClassGameController::DoWincheck()
{
	int NumHumans = 0;
	int NumInfected = 0;
	int NumFirstInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected, NumFirstInfected);

	static const char *ClassicRound = "classic";
	static const char *WitchPortalsRound = "witch_portals";
	const char *RoundType = ClassicRound;

	if (g_Config.m_InfEnableWitchPortals)
	{
		RoundType = WitchPortalsRound;
	}

	//Win check
	const int Seconds = (Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed());
	if(m_InfectedStarted && NumHumans == 0 && NumInfected > 1)
	{

		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTED, _("Infected won the round in {sec:RoundDuration}"), "RoundDuration", &Seconds, NULL);

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "round_end winner='zombies' survivors='0' duration='%d' round='%d of %d' type='%s'", Seconds, m_RoundCount+1, g_Config.m_SvRoundsPerMap, RoundType);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

		EndRound();
	}

	//Start the final explosion if the time is over
	if(m_InfectedStarted && !m_ExplosionStarted && g_Config.m_SvTimelimit > 0 && Seconds >= g_Config.m_SvTimelimit*60)
	{
		for(CCharacter *p = (CCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			if(p->IsZombie())
			{
				GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_GHOST);
			}
			else
			{
				GameServer()->SendEmoticon(p->GetPlayer()->GetCID(), EMOTICON_EYES);
			}
		}
		m_ExplosionStarted = true;
	}

	//Do the final explosion
	if(m_ExplosionStarted)
	{
		bool NewExplosion = false;

		for(int j=0; j<m_MapHeight; j++)
		{
			for(int i=0; i<m_MapWidth; i++)
			{
				if((m_GrowingMap[j*m_MapWidth+i] & 1) && (
					(i > 0 && m_GrowingMap[j*m_MapWidth+i-1] & 2) ||
					(i < m_MapWidth-1 && m_GrowingMap[j*m_MapWidth+i+1] & 2) ||
					(j > 0 && m_GrowingMap[(j-1)*m_MapWidth+i] & 2) ||
					(j < m_MapHeight-1 && m_GrowingMap[(j+1)*m_MapWidth+i] & 2)
				))
				{
					NewExplosion = true;
					m_GrowingMap[j*m_MapWidth+i] |= 8;
					m_GrowingMap[j*m_MapWidth+i] &= ~1;
					if(random_prob(0.1f))
					{
						vec2 TilePos = vec2(16.0f, 16.0f) + vec2(i*32.0f, j*32.0f);
						GameServer()->CreateExplosion(TilePos, -1, WEAPON_GAME, true);
						GameServer()->CreateSound(TilePos, SOUND_GRENADE_EXPLODE);
					}
				}
			}
		}

		for(int j=0; j<m_MapHeight; j++)
		{
			for(int i=0; i<m_MapWidth; i++)
			{
				if(m_GrowingMap[j*m_MapWidth+i] & 8)
				{
					m_GrowingMap[j*m_MapWidth+i] &= ~8;
					m_GrowingMap[j*m_MapWidth+i] |= 2;
				}
			}
		}

		for(CCharacter *p = (CCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
		{
			if(p->IsHuman())
				continue;

			int tileX = static_cast<int>(round(p->m_Pos.x))/32;
			int tileY = static_cast<int>(round(p->m_Pos.y))/32;

			if(tileX < 0) tileX = 0;
			if(tileX >= m_MapWidth) tileX = m_MapWidth-1;
			if(tileY < 0) tileY = 0;
			if(tileY >= m_MapHeight) tileY = m_MapHeight-1;

			if(m_GrowingMap[tileY*m_MapWidth+tileX] & 2 && p->GetPlayer())
			{
				p->Die(p->GetPlayer()->GetCID(), WEAPON_GAME);
			}
		}

		//If no more explosions, game over, decide who win
		if(!NewExplosion)
		{
			if(NumHumans)
			{
				GameServer()->SendChatTarget_Localization_P(-1, CHATCATEGORY_HUMANS, NumHumans, _P("One human won the round", "{int:NumHumans} humans won the round"), "NumHumans", &NumHumans, NULL);

				char aBuf[512];
				str_format(aBuf, sizeof(aBuf), "round_end winner='humans' survivors='%d' duration='%d' round='%d of %d' type='%s'", NumHumans, Seconds, m_RoundCount+1, g_Config.m_SvRoundsPerMap, RoundType);
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

					CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
				while(Iter.Next())
				{
					if(Iter.Player()->IsHuman())
					{
						//TAG_SCORE
						Server()->RoundStatistics()->OnScoreEvent(Iter.ClientID(), SCOREEVENT_HUMAN_SURVIVE, Iter.Player()->GetClass(), Server()->ClientName(Iter.ClientID()), GameServer()->Console());
						Server()->RoundStatistics()->SetPlayerAsWinner(Iter.ClientID());
						GameServer()->SendScoreSound(Iter.ClientID());
						Iter.Player()->m_WinAsHuman++;

						GameServer()->SendChatTarget_Localization(Iter.ClientID(), CHATCATEGORY_SCORE, _("You have survived, +5 points"), NULL);
							char aBuf[256];
						str_format(aBuf, sizeof(aBuf), "survived player='%s'", Server()->ClientName(Iter.ClientID()));
						GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
						}
				}
			}
			else
			{
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTED, _("Infected won the round in {sec:RoundDuration}"), "RoundDuration", &Seconds, NULL);
			}

			EndRound();
		}
	}
}

bool CInfClassGameController::IsSpawnable(vec2 Pos, int TeleZoneIndex)
{
	//First check if there is a tee too close
	CCharacter *aEnts[MAX_CLIENTS];
	int Num = GameServer()->m_World.FindEntities(Pos, 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	
	for(int c = 0; c < Num; ++c)
	{
		if(distance(aEnts[c]->m_Pos, Pos) <= 60)
			return false;
	}
	
	//Check the center
	int TeleIndex = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Teleport, Pos);
	if(GameServer()->Collision()->CheckPoint(Pos))
		return false;
	if(TeleZoneIndex && TeleIndex == TeleZoneIndex)
		return false;
	
	//Check the border of the tee. Kind of extrem, but more precise
	for(int i=0; i<16; i++)
	{
		float Angle = i * (2.0f * pi / 16.0f);
		vec2 CheckPos = Pos + vec2(cos(Angle), sin(Angle)) * 30.0f;
		TeleIndex = GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Teleport, CheckPos);
		if(GameServer()->Collision()->CheckPoint(CheckPos))
			return false;
		if(TeleZoneIndex && TeleIndex == TeleZoneIndex)
			return false;
	}
	
	return true;
}

bool CInfClassGameController::PreSpawn(CPlayer* pPlayer, vec2 *pOutPos)
{
	// spectators can't spawn
	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		return false;
	
	if(m_InfectedStarted)
		pPlayer->StartInfection();
		
	if(pPlayer->IsZombie() && m_ExplosionStarted)
		return false;
		
	if(m_InfectedStarted && pPlayer->IsZombie() && random_prob(g_Config.m_InfProbaSpawnNearWitch / 100.0f))
	{
		CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			if(Iter.Player()->GetCID() == pPlayer->GetCID()) continue;
			if(Iter.Player()->GetClass() != PLAYERCLASS_WITCH) continue;
			if(!Iter.Player()->GetCharacter()) continue;
			if (Iter.Player()->GetCharacter()->HasPortal()) continue;
			
			vec2 SpawnPos;
			if(Iter.Player()->GetCharacter()->FindWitchSpawnPosition(SpawnPos))
			{
				*pOutPos = SpawnPos;
				return true;
			}
		}
	}
			
	int Type = (pPlayer->IsZombie() ? 0 : 1);

	if(m_SpawnPoints[Type].size() == 0)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "The map has no spawn points");
		return false;
	}

	// get spawn point
	int RandomShift = random_int(0, m_SpawnPoints[Type].size()-1);
	for(int i = 0; i < m_SpawnPoints[Type].size(); i++)
	{
		int I = (i + RandomShift)%m_SpawnPoints[Type].size();
		if(IsSpawnable(m_SpawnPoints[Type][I], 0))
		{
			*pOutPos = m_SpawnPoints[Type][I];
			return true;
		}
	}
	
	return false;
}

bool CInfClassGameController::PickupAllowed(int Index)
{
	if(Index == TILE_ENTITY_POWERUP_NINJA) return false;
	else if(Index == TILE_ENTITY_WEAPON_SHOTGUN) return false;
	else if(Index == TILE_ENTITY_WEAPON_GRENADE) return false;
	else if(Index == TILE_ENTITY_WEAPON_RIFLE) return false;
	else return true;
}

int CInfClassGameController::ChooseHumanClass(const CPlayer *pPlayer) const
{
	//Get information about existing humans
	int nbSupport = 0;
	int nbDefender = 0;
	std::map<int, int> nbClass;
	for (int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; ++PlayerClass)
		nbClass[PlayerClass] = 0;

	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);	
	while(Iter.Next())
	{
		const int AnotherPlayerClass = Iter.Player()->GetClass();
		if ((AnotherPlayerClass < START_HUMANCLASS + 1) || (AnotherPlayerClass > END_HUMANCLASS - 1))
			continue;
		if (IsDefenderClass(AnotherPlayerClass))
			nbDefender++;
		if (IsSupportClass(AnotherPlayerClass))
			nbSupport++;
		nbClass[AnotherPlayerClass]++;
	}
	
	double Probability[NB_HUMANCLASS];
	
	for (int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; ++PlayerClass)
	{
		double &ClassProbability = Probability[PlayerClass - START_HUMANCLASS - 1];
		ClassProbability = Server()->GetPlayerClassEnabled(PlayerClass) ? 1.0f : 0.0f;
		if (GameServer()->m_FunRound)
		{
			// We care only about the class enablement
			continue;
		}

		if (IsDefenderClass(PlayerClass) && (nbDefender >= g_Config.m_InfDefenderLimit))
			ClassProbability = 0.0f;

		if (IsSupportClass(PlayerClass) && (nbSupport >= g_Config.m_InfSupportLimit))
			ClassProbability = 0.0f;

		if (nbClass[PlayerClass] >= Server()->GetClassPlayerLimit(PlayerClass))
			ClassProbability = 0.0f;

		// Honor the players count
		int ActivePlayerCount = Server()->GetActivePlayerCount();
		int MinPlayersForClass = Server()->GetMinPlayersForClass(PlayerClass);
		if (ActivePlayerCount < MinPlayersForClass)
			ClassProbability = 0.0f;
	}

	//Random is not fair enough. We keep the last two classes took by the player, and avoid to give him those again
	if(!GameServer()->m_FunRound) { // if normal round is being played
		for(unsigned int i=0; i<sizeof(pPlayer->m_LastHumanClasses)/sizeof(int); i++)
		{
			if(pPlayer->m_LastHumanClasses[i] > START_HUMANCLASS && pPlayer->m_LastHumanClasses[i] < END_HUMANCLASS)
			{
				Probability[pPlayer->m_LastHumanClasses[i] - 1 - START_HUMANCLASS] = 0.0f;
			}
		}
	}
	
	return START_HUMANCLASS + 1 + random_distribution(Probability, Probability + NB_HUMANCLASS);
}

int CInfClassGameController::ChooseInfectedClass(const CPlayer *pPlayer) const
{
	//Get information about existing infected
	int nbInfected = 0;
	std::map<int, int> nbClass;
	for (int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; ++PlayerClass)
		nbClass[PlayerClass] = 0;

	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		const int AnotherPlayerClass = Iter.Player()->GetClass();
		if(Iter.Player()->IsZombie()) nbInfected++;
		nbClass[AnotherPlayerClass]++;
	}
	
	double Probability[NB_INFECTEDCLASS];
	for (int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; ++PlayerClass)
	{
		double &ClassProbability = Probability[PlayerClass - START_INFECTEDCLASS - 1];
		ClassProbability = Server()->GetClassAvailability(PlayerClass) ? Server()->GetPlayerClassProbability(PlayerClass) : 0;
		if (GameServer()->m_FunRound)
		{
			// We care only about the class enablement
			continue;
		}

		switch(PlayerClass)
		{
			case PLAYERCLASS_GHOUL:
				if (nbInfected < g_Config.m_InfGhoulThreshold)
					ClassProbability = 0;
				break;
			case PLAYERCLASS_WITCH:
			case PLAYERCLASS_UNDEAD:
				if ((nbInfected <= 2) || nbClass[PlayerClass] > 0)
					ClassProbability = 0;
				break;
			default:
				break;
		}
	}
	
	int Class = START_INFECTEDCLASS + 1 + random_distribution(Probability, Probability + NB_INFECTEDCLASS);

	int Seconds = (Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed());
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "infected victim='%s' duration='%d' class='%d'", 
		Server()->ClientName(pPlayer->GetCID()), Seconds, Class);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	return Class;
}

bool CInfClassGameController::IsEnabledClass(int PlayerClass) {
	switch(PlayerClass)
	{
		case PLAYERCLASS_ENGINEER:
			return g_Config.m_InfEnableEngineer;
		case PLAYERCLASS_SOLDIER:
			return g_Config.m_InfEnableSoldier;
		case PLAYERCLASS_SCIENTIST:
			return g_Config.m_InfEnableScientist;
		case PLAYERCLASS_BIOLOGIST:
			return g_Config.m_InfEnableBiologist;
		case PLAYERCLASS_MEDIC:
			return g_Config.m_InfEnableMedic;
		case PLAYERCLASS_HERO:
			return g_Config.m_InfEnableHero;
		case PLAYERCLASS_NINJA:
			return g_Config.m_InfEnableNinja;
		case PLAYERCLASS_MERCENARY:
			return g_Config.m_InfEnableMercenary;
		case PLAYERCLASS_SNIPER:
			return g_Config.m_InfEnableSniper;
		case PLAYERCLASS_LOOPER:
			return g_Config.m_InfEnableLooper;
		default:
			return false;
	}
}

bool CInfClassGameController::IsChoosableClass(int PlayerClass)
{
	if (!IsEnabledClass(PlayerClass))
		return false;

	int ActivePlayerCount = Server()->GetActivePlayerCount();
	int MinPlayersForClass = Server()->GetMinPlayersForClass(PlayerClass);
	if (ActivePlayerCount < MinPlayersForClass)
		return false;

	int nbSupport = 0;
	int nbDefender = 0;
	std::map<int, int> nbClass;
	for (int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; ++PlayerClass)
		nbClass[PlayerClass] = 0;

	CPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		const int AnotherPlayerClass = Iter.Player()->GetClass();
		if ((AnotherPlayerClass < START_HUMANCLASS + 1) || (AnotherPlayerClass > END_HUMANCLASS - 1))
			continue;
		if (IsDefenderClass(AnotherPlayerClass))
			nbDefender++;
		if (IsSupportClass(AnotherPlayerClass))
			nbSupport++;
		nbClass[AnotherPlayerClass]++;
	}
	
	if (IsDefenderClass(PlayerClass) && (nbDefender >= g_Config.m_InfDefenderLimit))
		return false;

	if (IsSupportClass(PlayerClass) && (nbSupport >= g_Config.m_InfSupportLimit))
		return false;

	if (nbClass[PlayerClass] >= Server()->GetClassPlayerLimit(PlayerClass))
		return false;
	
	return true;
}

bool CInfClassGameController::CanVote()
{
	return !m_InfectedStarted;
}
