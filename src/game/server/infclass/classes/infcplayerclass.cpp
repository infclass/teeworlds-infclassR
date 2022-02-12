#include "infcplayerclass.h"

#include <base/system.h>
#include <game/gamecore.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcplayer.h>
#include <game/server/teeinfo.h>

CInfClassPlayerClass::CInfClassPlayerClass(CInfClassPlayer *pPlayer)
	: m_pPlayer(pPlayer)
{
}

CGameContext *CInfClassPlayerClass::GameContext() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer();

	return nullptr;
}

// A lot of code use GameServer() as CGameContext* getter, so let it be.
CGameContext *CInfClassPlayerClass::GameServer() const
{
	return GameContext();
}

CGameWorld *CInfClassPlayerClass::GameWorld() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer()->GameWorld();

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
	if(m_pPlayer)
		return m_pPlayer->GameServer()->Config();

	return nullptr;
}

const CConfig *CInfClassPlayerClass::Config() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer()->Config();

	return nullptr;
}

IServer *CInfClassPlayerClass::Server() const
{
	if(m_pPlayer)
		return m_pPlayer->Server();

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
	if(m_pCharacter == character)
	{
		return;
	}

	if(m_pCharacter)
	{
		DestroyChildEntities();
		m_pCharacter->SetClass(nullptr);
	}

	m_pCharacter = character;

	if(m_pCharacter)
	{
		m_pCharacter->SetClass(this);
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

void CInfClassPlayerClass::GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams)
{
	int InfWID = m_pCharacter->GetInfWeaponID(Weapon);
	pParams->RegenInterval = Server()->GetAmmoRegenTime(InfWID);
	pParams->MaxAmmo = Server()->GetMaxAmmo(InfWID);
}

bool CInfClassPlayerClass::CanDie() const
{
	return true;
}

float CInfClassPlayerClass::GetGhoulPercent() const
{
	return 0;
}

int CInfClassPlayerClass::GetPlayerClass() const
{
	if(m_pPlayer)
		return m_pPlayer->GetClass();

	return PLAYERCLASS_NONE;
}

void CInfClassPlayerClass::OnPlayerClassChanged()
{
	UpdateSkin();

	// Enable hook protection by default for both infected and humans on class changed
	m_pPlayer->HookProtection(true);
}

void CInfClassPlayerClass::PrepareToDie(int Killer, DAMAGE_TYPE DamageType, bool *pRefusedToDie)
{
}

void CInfClassPlayerClass::Poison(int Count, int From, DAMAGE_TYPE DamageType)
{
	if(Count > m_Poison)
	{
		m_Poison = Count;
		m_PoisonFrom = From;
		m_PoisonDamageType = DamageType;
	}
}

void CInfClassPlayerClass::DisableHealing(float Duration, int From, DAMAGE_TYPE DamageType)
{
	m_HealingDisabledTicks = maximum<int>(m_HealingDisabledTicks, Duration * Server()->TickSpeed());
}

bool CInfClassPlayerClass::IsHealingDisabled() const
{
	return m_HealingDisabledTicks > 0;
}

void CInfClassPlayerClass::OnCharacterPreCoreTick()
{
	if(m_pCharacter->IsPassenger())
	{
		if(m_pCharacter->m_Input.m_Jump && !m_pCharacter->m_PrevInput.m_Jump)
		{
			// Jump off is still in CCharacterCore::UpdateTaxiPassengers()
		}
		else
		{
			m_pCharacter->ResetMovementsInput();
		}
	}
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
			m_pCharacter->TakeDamage(Force, PoisonDamage, m_PoisonFrom, m_PoisonDamageType);
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

	if(m_HealingDisabledTicks > 0)
	{
		m_HealingDisabledTicks--;
	}

	BroadcastWeaponState();
}

void CInfClassPlayerClass::OnCharacterSnap(int SnappingClient)
{
}

void CInfClassPlayerClass::OnCharacterSpawned(const SpawnContext &Context)
{
	m_Poison = 0;
	m_HealingDisabledTicks = 0;

	UpdateSkin();
	GiveClassAttributes();
}

void CInfClassPlayerClass::OnCharacterDeath(DAMAGE_TYPE DamageType)
{
	if(m_pCharacter->m_Core.m_Passenger)
	{
		m_pCharacter->m_Core.SetPassenger(nullptr);
	}

	if(m_pPlayer)
	{
		m_pPlayer->ResetTheTargetToFollow();
	}

	DestroyChildEntities();
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
	if(!m_pCharacter)
	{
		return;
	}

	m_pCharacter->TakeAllWeapons();
}

void CInfClassPlayerClass::DestroyChildEntities()
{
}

void CInfClassPlayerClass::BroadcastWeaponState()
{
}

void CInfClassPlayerClass::UpdateSkin()
{
	if(!m_pPlayer)
		return;

	m_pPlayer->UpdateSkin();
}
