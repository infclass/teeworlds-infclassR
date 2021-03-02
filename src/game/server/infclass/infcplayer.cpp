#include "infcplayer.h"

#include "classes/infcplayerclass.h"
#include "entities/infccharacter.h"

MACRO_ALLOC_POOL_ID_IMPL(CInfClassPlayer, MAX_CLIENTS)

CInfClassPlayer::CInfClassPlayer(CGameContext *pGameServer, int ClientID, int Team)
	: CPlayer(pGameServer, ClientID, Team)
{
	SetCharacterClass(new(m_ClientID) CInfClassPlayerClass());
}

CInfClassPlayer::~CInfClassPlayer()
{
	if(m_pInfcPlayerClass)
		delete m_pInfcPlayerClass;
}

void CInfClassPlayer::TryRespawn()
{
	vec2 SpawnPos;

/* INFECTION MODIFICATION START ***************************************/
	if(!GameServer()->m_pController->PreSpawn(this, &SpawnPos))
		return;
/* INFECTION MODIFICATION END *****************************************/

	m_Spawning = false;
	m_pInfcCharacter = new(m_ClientID) CInfClassCharacter(GameServer());
	m_pCharacter = m_pInfcCharacter;
	m_pInfcPlayerClass->SetCharacter(m_pInfcCharacter);
	m_pCharacter->Spawn(this, SpawnPos);
	if(GetClass() != PLAYERCLASS_NONE)
		GameServer()->CreatePlayerSpawn(SpawnPos);
}

void CInfClassPlayer::SetCharacterClass(CInfClassPlayerClass *pClass)
{
	if(m_pInfcPlayerClass)
		delete m_pInfcPlayerClass;

	m_pInfcPlayerClass = pClass;

	if (m_pInfcCharacter)
	{
		m_pInfcPlayerClass->SetCharacter(m_pInfcCharacter);
	}
}
