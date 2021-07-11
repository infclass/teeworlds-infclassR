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

	m_pCharacter = pCharacter;
	m_pCharacter->Spawn(this, SpawnPos);
	pCharacter->SetClass(m_pInfcPlayerClass);
	pCharacter->OnCharacterSpawned();
	if(GetClass() != PLAYERCLASS_NONE)
		GameServer()->CreatePlayerSpawn(SpawnPos);
}

void CInfClassPlayer::Tick()
{
	if(!Server()->ClientIngame(m_ClientID))
		return;

	if(m_DoInfection)
	{

		if(IsHuman())
		{
			m_InfectionTick = Server()->Tick();
		}

		int c = GameController()->ChooseInfectedClass(this);
		CInfClassPlayer *pInfectiousPlayer = GameController()->GetPlayer(m_InfectiousPlayerCID);

		m_DoInfection = false;
		m_InfectiousPlayerCID = -1;

		SetClass(c);
		GameServer()->m_pController->OnPlayerInfected(this, pInfectiousPlayer);
	}

	CPlayer::Tick();

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

int CInfClassPlayer::GetDefaultEmote() const
{
	if(m_pInfcPlayerClass)
		return m_pInfcPlayerClass->GetDefaultEmote();

	return CPlayer::GetDefaultEmote();
}

CInfClassCharacter *CInfClassPlayer::GetCharacter()
{
	return static_cast<CInfClassCharacter*>(m_pCharacter);
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

void CInfClassPlayer::SetClass(int newClass)
{
	if(m_class == newClass)
		return;

	if(newClass > START_HUMANCLASS && newClass < END_HUMANCLASS)
	{
		bool ClassFound = false;
		for(unsigned int i=0; i<sizeof(m_LastHumanClasses)/sizeof(int); i++)
		{
			if(m_LastHumanClasses[i] == newClass)
				ClassFound = true;
		}
		if(!ClassFound)
		{
			for(unsigned int i=0; i<sizeof(m_LastHumanClasses)/sizeof(int)-1; i++)
			{
				m_LastHumanClasses[i] = m_LastHumanClasses[i+1];
			}
			m_LastHumanClasses[sizeof(m_LastHumanClasses)/sizeof(int)-1] = newClass;
		}
	}

	if(newClass < END_HUMANCLASS)
	{
		m_LastHumanClass = newClass;
	}

	m_GhoulLevel = 0;
	m_GhoulLevelTick = 0;

	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->SetCharacter(nullptr);
	}

	m_class = newClass;

	if(m_class < END_HUMANCLASS)
		HookProtection(true);
	else
		HookProtection(true); // true = hook protection for zombies by default

	if(IsHuman() && !GetCharacterClass()->IsHuman())
	{
		SetCharacterClass(new(m_ClientID) CInfClassHuman(this));
	}
	else if (IsZombie() && !GetCharacterClass()->IsZombie())
	{
		SetCharacterClass(new(m_ClientID) CInfClassInfected(this));
	}
	else if(m_pCharacter)
	{
		GetCharacter()->SetClass(m_pInfcPlayerClass);
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
		const char *ClassName = CInfClassGameController::GetClanForClass(GetClass(), "?????");
		str_format(aBuf, sizeof(aBuf), "%s%s", Server()->IsClientLogged(GetCID()) ? "@" : " ", ClassName);
	}

	// This is not thread-safe but we don't have threads.
	return aBuf;
}
