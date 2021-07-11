#include "human.h"

#include <engine/shared/config.h>
#include <game/server/classes.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/teeinfo.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassHuman, MAX_CLIENTS)

CInfClassHuman::CInfClassHuman(CInfClassPlayer *pPlayer)
	: CInfClassPlayerClass(pPlayer)
{
}

void CInfClassHuman::OnCharacterPreCoreTick()
{
	if(PlayerClass() == PLAYERCLASS_SNIPER && m_pCharacter->m_PositionLocked)
	{
		if(m_pCharacter->m_Input.m_Jump && !m_pCharacter->m_PrevInput.m_Jump)
		{
			m_pCharacter->m_PositionLocked = false;
			m_pCharacter->m_PositionLockTick = 0;
		}
	}
}

void CInfClassHuman::GiveClassAttributes()
{
	CInfClassPlayerClass::GiveClassAttributes();

	switch(PlayerClass())
	{
		case PLAYERCLASS_ENGINEER:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_LASER);
			break;
		case PLAYERCLASS_SOLDIER:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_GRENADE);
			break;
		case PLAYERCLASS_MERCENARY:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			if(!GameServer()->m_FunRound)
				m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_GUN);
			break;
		case PLAYERCLASS_SNIPER:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_LASER);
			break;
		case PLAYERCLASS_SCIENTIST:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_LASER);
			break;
		case PLAYERCLASS_BIOLOGIST:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->GiveWeapon(WEAPON_SHOTGUN, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_SHOTGUN);
			break;
		case PLAYERCLASS_LOOPER:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_LASER);
			break;
		case PLAYERCLASS_MEDIC:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_SHOTGUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_SHOTGUN);
			break;
		case PLAYERCLASS_HERO:
			if(GameController()->AreTurretsEnabled())
				m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_SHOTGUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_GRENADE);
			break;
		case PLAYERCLASS_NINJA:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
			break;
		case PLAYERCLASS_NONE:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
			break;
	}
}

bool CInfClassHuman::SetupSkin(int PlayerClass, CTeeInfo *output)
{
	switch(PlayerClass)
	{
		case PLAYERCLASS_ENGINEER:
			output->m_UseCustomColor = 0;
			output->SetSkinName("limekitty");
			break;
		case PLAYERCLASS_SOLDIER:
			output->SetSkinName("brownbear");
			output->m_UseCustomColor = 0;
			break;
		case PLAYERCLASS_SNIPER:
			output->SetSkinName("warpaint");
			output->m_UseCustomColor = 0;
			break;
		case PLAYERCLASS_MERCENARY:
			output->SetSkinName("bluestripe");
			output->m_UseCustomColor = 0;
			break;
		case PLAYERCLASS_SCIENTIST:
			output->SetSkinName("toptri");
			output->m_UseCustomColor = 0;
			break;
		case PLAYERCLASS_BIOLOGIST:
			output->SetSkinName("twintri");
			output->m_UseCustomColor = 0;
			break;
		case PLAYERCLASS_LOOPER:
			output->SetSkinName("bluekitty");
			output->m_UseCustomColor = 1;
			output->m_ColorBody = 255;
			output->m_ColorFeet = 0;
			break;
		case PLAYERCLASS_MEDIC:
			output->SetSkinName("twinbop");
			output->m_UseCustomColor = 0;
			break;
		case PLAYERCLASS_HERO:
			output->SetSkinName("redstripe");
			output->m_UseCustomColor = 0;
			break;
		case PLAYERCLASS_NINJA:
			output->SetSkinName("default");
			output->m_UseCustomColor = 1;
			output->m_ColorBody = 255;
			output->m_ColorFeet = 0;
			break;
		default:
			output->SetSkinName("default");
			output->m_UseCustomColor = 0;
			return false;
	}

	return true;
}

void CInfClassHuman::SetupSkin(CTeeInfo *output)
{
	SetupSkin(PlayerClass(), output);
}

void CInfClassHuman::OnSlimeEffect(int Owner)
{
	int Count = Config()->m_InfSlimePoisonDuration;
	Poison(Count, Owner);
}
