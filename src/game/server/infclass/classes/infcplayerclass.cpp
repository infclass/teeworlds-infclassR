#include "infcplayerclass.h"

#include <base/system.h>
#include <game/gamecore.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcplayer.h>

CInfClassPlayerClass::CInfClassPlayerClass()
{
}

CGameContext *CInfClassPlayerClass::GameContext() const
{
	if(m_pCharacter)
		return m_pCharacter->GameContext();

	return nullptr;
}

// A lot of code use GameServer() as CGameContext* getter, so let it be.
CGameContext *CInfClassPlayerClass::GameServer() const
{
	return GameContext();
}

CGameWorld *CInfClassPlayerClass::GameWorld() const
{
	if(m_pCharacter)
		return m_pCharacter->GameWorld();

	return nullptr;
}

CConfig *CInfClassPlayerClass::Config()
{
	if(m_pCharacter)
		return m_pCharacter->Config();

	return nullptr;
}

IServer *CInfClassPlayerClass::Server()
{
	if(m_pCharacter)
		return m_pCharacter->Server();

	return nullptr;
}

CInfClassPlayer *CInfClassPlayerClass::GetPlayer()
{
	if(m_pCharacter)
		return static_cast<CInfClassPlayer*>(m_pCharacter->GetPlayer());

	return nullptr;
}

int CInfClassPlayerClass::GetCID()
{
	const CInfClassPlayer *pPlayer = GetPlayer();
	if(pPlayer)
		return pPlayer->GetCID();

	return -1;
}

vec2 CInfClassPlayerClass::GetPos() const
{
	if(m_pCharacter)
		return m_pCharacter->GetPos();

	return vec2(0, 0);
}

vec2 CInfClassPlayerClass::GetDirection() const
{
	if(m_pCharacter)
		return m_pCharacter->GetDirection();

	return vec2(0, 0);
}

float CInfClassPlayerClass::GetProximityRadius() const
{
	if(m_pCharacter)
		return m_pCharacter->GetProximityRadius();

	return 0;
}

void CInfClassPlayerClass::SetCharacter(CInfClassCharacter *character)
{
	m_pCharacter = character;
	m_pCharacter->SetClass(this);

	GiveClassAttributes();
}

bool CInfClassPlayerClass::IsZombie() const
{
	return !IsHuman();
}

int CInfClassPlayerClass::PlayerClass() const
{
	if(m_pCharacter)
		return m_pCharacter->CCharacter::GetClass();

	return PLAYERCLASS_NONE;
}

void CInfClassPlayerClass::OnPlayerClassChanged()
{
	GiveClassAttributes();
}

void CInfClassPlayerClass::Poison(int Count, int From)
{
	if(m_Poison <= 0)
	{
		m_PoisonTick = 0;
		m_Poison = Count;
		m_PoisonFrom = From;
	}
}

void CInfClassPlayerClass::Tick()
{
	if(m_Poison > 0)
	{
		if(m_PoisonTick == 0)
		{
			m_Poison--;
			vec2 Force(0, 0);
			static const int PoisonDamage = 1;
			m_pCharacter->TakeDamage(Force, PoisonDamage, m_PoisonFrom, WEAPON_HAMMER, TAKEDAMAGEMODE_NOINFECTION);
			if(m_Poison > 0)
			{
				m_PoisonTick = Server()->TickSpeed()/2;
			}
		}
		else
		{
			m_PoisonTick--;
		}
	}
}

void CInfClassPlayerClass::OnCharacterSpawned()
{
	m_Poison = 0;

	GiveClassAttributes();
}

void CInfClassPlayerClass::GiveClassAttributes()
{
	m_pCharacter->TakeAllWeapons();
}
