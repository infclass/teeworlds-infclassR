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

CInfClassCharacter::~CInfClassCharacter()
{
	if(m_pClass)
		m_pClass->SetCharacter(nullptr);
}

void CInfClassCharacter::Tick()
{
	CCharacter::Tick();

	if(m_pClass)
		m_pClass->Tick();
}

void CInfClassCharacter::Die(int Killer, int Weapon)
{
/* INFECTION MODIFICATION START ***************************************/
	if(GetPlayerClass() == PLAYERCLASS_UNDEAD && Killer != m_pPlayer->GetCID())
	{
		Freeze(10.0, Killer, FREEZEREASON_UNDEAD);
		return;
	}

	// Start counting down, delay killer message for later
	if(GetPlayerClass() == PLAYERCLASS_VOODOO && !m_VoodooAboutToDie)
	{
		m_VoodooAboutToDie = true;
		m_VoodooKiller = Killer;
		m_VoodooWeapon = Weapon;
		m_pPlayer->SetToSpirit(true);
		return;
	// If about to die, yet killed again, dont kill him either
	} else if(GetPlayerClass() == PLAYERCLASS_VOODOO && m_VoodooAboutToDie && m_VoodooTimeAlive > 0)
	{
		return;
	}

	if(GetPlayerClass() == PLAYERCLASS_GHOUL)
	{
		m_pPlayer->IncreaseGhoulLevel(-20);
	}

	DestroyChildEntities();
/* INFECTION MODIFICATION END *****************************************/

	CCharacter *pKillerCharacter = nullptr;
	if (Weapon == WEAPON_WORLD && Killer == m_pPlayer->GetCID()) {
		//Search for the real killer (if somebody hooked this player)
		for(CCharacter *pHooker = (CCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHooker; pHooker = (CCharacter *)pHooker->TypeNext())
		{
			if (pHooker->GetPlayer() && pHooker->m_Core.m_HookedPlayer == m_pPlayer->GetCID())
			{
				if (pKillerCharacter) {
					// More than one player hooked this victim
					// We don't support cooperative killing
					pKillerCharacter = nullptr;
					break;
				}
				pKillerCharacter = pHooker;
			}
		}

		if (pKillerCharacter && pKillerCharacter->GetPlayer())
		{
			Killer = pKillerCharacter->GetPlayer()->GetCID();
			Weapon = WEAPON_NINJA;
		}

		if(!pKillerCharacter && IsFrozen())
		{
			Killer = m_LastFreezer;
			if(m_FreezeReason == FREEZEREASON_FLASH)
			{
				Weapon = WEAPON_GRENADE;
			}
			else
			{
				Weapon = WEAPON_NINJA;
			}

			pKillerCharacter = GameServer()->GetPlayerChar(Killer);
		}
	}

	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
		Server()->ClientName(Killer),
		Server()->ClientName(m_pPlayer->GetCID()), Weapon);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	GameServer()->SendKillMessage(Killer, m_pPlayer->GetCID(), Weapon, ModeSpecial);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());

/* INFECTION MODIFICATION START ***************************************/
	CPlayer* pKillerPlayer = nullptr;
	if(Killer >=0 && Killer < MAX_CLIENTS)
	{
		pKillerPlayer = GameServer()->m_apPlayers[Killer];
	}

	if(GetPlayerClass() == PLAYERCLASS_BOOMER && !IsFrozen() && Weapon != WEAPON_GAME && !(IsInLove() && Weapon == WEAPON_SELF) )
	{
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
		GameServer()->CreateExplosionDisk(m_Pos, 60.0f, 80.5f, 14, 52.0f, m_pPlayer->GetCID(), WEAPON_HAMMER, TAKEDAMAGEMODE_INFECTION);
	}
	
	if(GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		m_pPlayer->StartInfection(true);
		GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The witch is dead"), NULL);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else if(GetPlayerClass() == PLAYERCLASS_UNDEAD)
	{
		m_pPlayer->StartInfection(true);
		GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The undead is finally dead"), NULL);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else
	{
		m_pPlayer->Infect(pKillerPlayer);
	}
	if (m_Core.m_Passenger) {
		m_Core.SetPassenger(nullptr);
	}
/* INFECTION MODIFICATION END *****************************************/

	if(pKillerPlayer && (pKillerPlayer != m_pPlayer))
	{
		pKillerPlayer->IncreaseNumberKills();
		pKillerCharacter = pKillerPlayer->GetCharacter();
		// set attacker's face to happy (taunt!)
		if(pKillerCharacter)
		{
			pKillerCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
			pKillerCharacter->CheckSuperWeaponAccess();

			if(pKillerPlayer->GetClass() == PLAYERCLASS_MERCENARY)
			{
				pKillerCharacter->GiveWeapon(WEAPON_RIFLE, m_aWeapons[WEAPON_RIFLE].m_Ammo + 3);
			}
		}
	}
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
	m_pClass->SetCharacter(this);
}
