#include "infcplayerclass.h"

#include <base/system.h>
#include <game/gamecore.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcplayer.h>
#include <game/server/teeinfo.h>

CInfClassPlayerClass::CInfClassPlayerClass(CInfClassPlayer *pPlayer)
	: m_pPlayer(pPlayer)
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

CInfClassGameController *CInfClassPlayerClass::GameController() const
{
	if(m_pPlayer)
		return m_pPlayer->GameController();

	return nullptr;
}

CConfig *CInfClassPlayerClass::Config()
{
	if(m_pCharacter)
		return m_pCharacter->Config();

	return nullptr;
}

const CConfig *CInfClassPlayerClass::Config() const
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
	return m_pPlayer;
}

const CInfClassPlayer *CInfClassPlayerClass::GetPlayer() const
{
	return m_pPlayer;
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

	if(m_pCharacter)
	{
		GiveClassAttributes();
	}
}

bool CInfClassPlayerClass::IsZombie() const
{
	return !IsHuman();
}

int CInfClassPlayerClass::GetDefaultEmote() const
{
	return EMOTE_NORMAL;
}

bool CInfClassPlayerClass::CanDie() const
{
	return true;
}

float CInfClassPlayerClass::GetGhoulPercent() const
{
	return 0;
}

int CInfClassPlayerClass::PlayerClass() const
{
	if(m_pCharacter)
		return m_pCharacter->GetPlayerClass();

	return PLAYERCLASS_NONE;
}

void CInfClassPlayerClass::OnPlayerClassChanged()
{
	UpdateSkin();

	if(m_pCharacter)
	{
		GiveClassAttributes();
	}
}

void CInfClassPlayerClass::PrepareToDie(int Killer, int Weapon, bool *pRefusedToDie)
{
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

void CInfClassPlayerClass::OnCharacterPreCoreTick()
{
}

void CInfClassPlayerClass::OnCharacterTick()
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

	UpdateSkin();
	GiveClassAttributes();
}

void CInfClassPlayerClass::OnWeaponFired(WeaponFireContext *pFireContext)
{
	switch(pFireContext->Weapon)
	{
		case WEAPON_HAMMER:
			OnHammerFired(pFireContext);
			break;
		case WEAPON_GUN:
			OnGunFired(pFireContext);
			break;
		case WEAPON_SHOTGUN:
			OnShotgunFired(pFireContext);
			break;
		case WEAPON_GRENADE:
			OnGrenadeFired(pFireContext);
			break;
		case WEAPON_LASER:
			OnLaserFired(pFireContext);
			break;
		case WEAPON_NINJA:
			OnNinjaFired(pFireContext);
			break;
		default:
			break;
	}
}

void CInfClassPlayerClass::OnHammerFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnGunFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnShotgunFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnGrenadeFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnLaserFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnNinjaFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnFloatingPointCollected(int Points)
{
}

void CInfClassPlayerClass::GiveClassAttributes()
{
	m_pCharacter->TakeAllWeapons();
}

void CInfClassPlayerClass::SetupSkin(CTeeInfo *output)
{
	output->m_UseCustomColor = 0;
	output->SetSkinName("default");
}

void CInfClassPlayerClass::UpdateSkin()
{
	SetupSkin(&m_pPlayer->m_TeeInfos);
}
