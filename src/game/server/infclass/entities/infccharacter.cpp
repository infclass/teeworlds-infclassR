#include "infccharacter.h"

#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassCharacter, MAX_CLIENTS)

CInfClassCharacter::CInfClassCharacter(CGameContext *pContext)
	: CCharacter(pContext->GameWorld(), pContext->Console())
	, m_pContext(pContext)
{
}

void CInfClassCharacter::Tick()
{
	CCharacter::Tick();

	if(m_pClass)
		m_pClass->Tick();
}

void CInfClassCharacter::SetActiveWeapon(int Weapon)
{
	m_ActiveWeapon = Weapon;
}

void CInfClassCharacter::SetLastWeapon(int Weapon)
{
	m_LastWeapon = Weapon;
}

void CInfClassCharacter::TakeAllWeapons()
{
	for (WeaponStat &weapon : m_aWeapons)
	{
		weapon.m_Got = false;
		weapon.m_Ammo = 0;
	}
}

void CInfClassCharacter::SetClass(CInfClassPlayerClass *pClass)
{
	m_pClass = pClass;
}
