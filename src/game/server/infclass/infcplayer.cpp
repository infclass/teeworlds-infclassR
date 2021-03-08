#include "infcplayer.h"

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
	CInfClassCharacter *pCharacter = new(m_ClientID) CInfClassCharacter(GameServer());
	pCharacter->SetClass(m_pInfcPlayerClass);

	m_pCharacter = pCharacter;
	m_pCharacter->Spawn(this, SpawnPos);
	m_pInfcPlayerClass->OnCharacterSpawned();
	if(GetClass() != PLAYERCLASS_NONE)
		GameServer()->CreatePlayerSpawn(SpawnPos);
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
