#include "infected.h"

#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassInfected, MAX_CLIENTS)

CInfClassInfected::CInfClassInfected()
	: CInfClassPlayerClass()
{
}

void CInfClassInfected::GiveClassAttributes()
{
	CInfClassPlayerClass::GiveClassAttributes();

	m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
	m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);

	if(m_pCharacter->CanOpenPortals())
	{
		m_pCharacter->GiveWeapon(WEAPON_RIFLE, -1);
	}
}
