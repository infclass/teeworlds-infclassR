#include "infected.h"

#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassInfected, MAX_CLIENTS)

CInfClassInfected::CInfClassInfected()
	: CInfClassPlayerClass()
{
}

void CInfClassInfected::OnCharacterSpawned()
{
	CInfClassPlayerClass::OnCharacterSpawned();

	m_HealTick = 0;
}

void CInfClassInfected::GiveClassAttributes()
{
	CInfClassPlayerClass::GiveClassAttributes();

	m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
	m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);

	if (GameServer()->GetZombieCount() <= 1)
	{
		/* Lonely zombie */
		m_pCharacter->IncreaseArmor(10);
	}

	if(m_pCharacter->CanOpenPortals())
	{
		m_pCharacter->GiveWeapon(WEAPON_RIFLE, -1);
	}
}

void CInfClassInfected::OnSlimeEffect(int Owner)
{
	if(PlayerClass() == PLAYERCLASS_SLUG)
		return;

	m_pCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick());
	if(Server()->Tick() >= m_HealTick + (Server()->TickSpeed() / Config()->m_InfSlimeHealRate))
	{
		m_HealTick = Server()->Tick();
		m_pCharacter->IncreaseHealth(1);
	}
}
