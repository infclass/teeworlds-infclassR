#include "infcplayer.h"

#include <engine/shared/config.h>
#include <game/server/infclass/infcgamecontroller.h>

#include "classes/humans/human.h"
#include "classes/infcplayerclass.h"
#include "classes/infected/infected.h"
#include "entities/infccharacter.h"

MACRO_ALLOC_POOL_ID_IMPL(CInfClassPlayer, MAX_CLIENTS)

CInfClassPlayer::CInfClassPlayer(CInfClassGameController *pGameController, int ClientID, int Team)
	: CPlayer(pGameController->GameServer(), ClientID, Team)
	, m_pGameController(pGameController)
{
	SetCharacterClass(new(m_ClientID) CInfClassHuman(this));
}

CInfClassPlayer::~CInfClassPlayer()
{
	if(m_pInfcPlayerClass)
		delete m_pInfcPlayerClass;

	m_pInfcPlayerClass = nullptr;
}

CInfClassGameController *CInfClassPlayer::GameController()
{
	return m_pGameController;
}

void CInfClassPlayer::TryRespawn()
{
	vec2 SpawnPos;

/* INFECTION MODIFICATION START ***************************************/
	if(!GameServer()->m_pController->PreSpawn(this, &SpawnPos))
		return;
/* INFECTION MODIFICATION END *****************************************/

	m_Spawning = false;
	CInfClassCharacter *pCharacter = new(m_ClientID) CInfClassCharacter(GameController());
	pCharacter->SetClass(m_pInfcPlayerClass);

	m_pCharacter = pCharacter;
	m_pCharacter->Spawn(this, SpawnPos);
	m_pInfcPlayerClass->OnCharacterSpawned();
	if(GetClass() != PLAYERCLASS_NONE)
		GameServer()->CreatePlayerSpawn(SpawnPos);
}

void CInfClassPlayer::Tick()
{
	CPlayer::Tick();

	if(!Server()->ClientIngame(m_ClientID))
		return;

	if(!GameServer()->m_World.m_Paused)
	{
		if(IsHuman())
			m_HumanTime++;
	}

	if(m_MapMenu > 0)
		m_MapMenuTick++;

	if(GetClass() == PLAYERCLASS_GHOUL)
	{
		if(m_GhoulLevel > 0)
		{
			m_GhoulLevelTick--;

			if(m_GhoulLevelTick <= 0)
			{
				m_GhoulLevelTick = (Server()->TickSpeed() * GameServer()->Config()->m_InfGhoulDigestion);
				IncreaseGhoulLevel(-1);
				GetCharacterClass()->UpdateSkin();
			}
		}
	}

	HandleTuningParams();
}

void CInfClassPlayer::SetCharacterClass(CInfClassPlayerClass *pClass)
{
	if(m_pInfcPlayerClass)
		delete m_pInfcPlayerClass;

	m_pInfcPlayerClass = pClass;

	if(m_pCharacter)
	{
		CInfClassCharacter *pCharacter = static_cast<CInfClassCharacter*>(m_pCharacter);
		pCharacter->SetClass(m_pInfcPlayerClass);
	}
}

void CInfClassPlayer::onClassChanged()
{
	if(IsHuman() && !GetCharacterClass()->IsHuman())
	{
		SetCharacterClass(new(m_ClientID) CInfClassHuman(this));
	}
	else if (IsZombie() && !GetCharacterClass()->IsZombie())
	{
		SetCharacterClass(new(m_ClientID) CInfClassInfected(this));
	}

	GetCharacterClass()->OnPlayerClassChanged();
}

const char *CInfClassPlayer::GetClan(int SnappingClient) const
{
	if(GetTeam() == TEAM_SPECTATORS)
	{
		return Server()->ClientClan(m_ClientID);
	}

	int SnapScoreMode = PLAYERSCOREMODE_SCORE;
	if(GameServer()->GetPlayer(SnappingClient))
	{
		SnapScoreMode = GameServer()->m_apPlayers[SnappingClient]->GetScoreMode();
	}
	
	static char aBuf[32];

	if(SnapScoreMode == PLAYERSCOREMODE_TIME)
	{
		float RoundDuration = static_cast<float>(m_HumanTime/((float)Server()->TickSpeed()))/60.0f;
		int Minutes = static_cast<int>(RoundDuration);
		int Seconds = static_cast<int>((RoundDuration - Minutes)*60.0f);
		
		str_format(aBuf, sizeof(aBuf), "%i:%s%i min", Minutes,((Seconds < 10) ? "0" : ""), Seconds);
	}
	else
	{
		switch(GetClass())
		{
			case PLAYERCLASS_ENGINEER:
				str_format(aBuf, sizeof(aBuf), "%sEngineer", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_SOLDIER:
				str_format(aBuf, sizeof(aBuf), "%sSoldier", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_MERCENARY:
				str_format(aBuf, sizeof(aBuf), "%sMercenary", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_SNIPER:
				str_format(aBuf, sizeof(aBuf), "%sSniper", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_SCIENTIST:
				str_format(aBuf, sizeof(aBuf), "%sScientist", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_BIOLOGIST:
				str_format(aBuf, sizeof(aBuf), "%sBiologist", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_LOOPER:
				str_format(aBuf, sizeof(aBuf), "%sLooper", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_MEDIC:
				str_format(aBuf, sizeof(aBuf), "%sMedic", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_HERO:
				str_format(aBuf, sizeof(aBuf), "%sHero", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_NINJA:
				str_format(aBuf, sizeof(aBuf), "%sNinja", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_SMOKER:
				str_format(aBuf, sizeof(aBuf), "%sSmoker", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_BOOMER:
				str_format(aBuf, sizeof(aBuf), "%sBoomer", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_HUNTER:
				str_format(aBuf, sizeof(aBuf), "%sHunter", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_BAT:
				str_format(aBuf, sizeof(aBuf), "%sBat", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_GHOST:
				str_format(aBuf, sizeof(aBuf), "%sGhost", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_SPIDER:
				str_format(aBuf, sizeof(aBuf), "%sSpider", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_GHOUL:
				str_format(aBuf, sizeof(aBuf), "%sGhoul", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_SLUG:
				str_format(aBuf, sizeof(aBuf), "%sSlug", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_VOODOO:
				str_format(aBuf, sizeof(aBuf), "%sVoodoo", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_UNDEAD:
				str_format(aBuf, sizeof(aBuf), "%sUndead", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			case PLAYERCLASS_WITCH:
				str_format(aBuf, sizeof(aBuf), "%sWitch", Server()->IsClientLogged(GetCID()) ? "@" : " ");
				break;
			default:
				str_format(aBuf, sizeof(aBuf), "%s?????", Server()->IsClientLogged(GetCID()) ? "@" : " ");
		}
	}

	// This is not thread-safe but we don't have threads.
	return aBuf;
}
