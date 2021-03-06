#include "infccharacter.h"

#include <engine/server/mapconverter.h>
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

void CInfClassCharacter::HandleMapMenu()
{
	if(GetPlayerClass() != PLAYERCLASS_NONE)
	{
		SetAntiFire();
		m_pPlayer->CloseMapMenu();
	}
	else
	{
		vec2 CursorPos = vec2(m_Input.m_TargetX, m_Input.m_TargetY);

		bool Broadcast = false;

		if(length(CursorPos) > 100.0f)
		{
			float Angle = 2.0f*pi+atan2(CursorPos.x, -CursorPos.y);
			float AngleStep = 2.0f*pi/static_cast<float>(CMapConverter::NUM_MENUCLASS);
			m_pPlayer->m_MapMenuItem = ((int)((Angle+AngleStep/2.0f)/AngleStep))%CMapConverter::NUM_MENUCLASS;

			switch(m_pPlayer->m_MapMenuItem)
			{
				case CMapConverter::MENUCLASS_RANDOM:
					GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Random choice"), NULL);
					Broadcast = true;
					break;
				case CMapConverter::MENUCLASS_ENGINEER:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_ENGINEER))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Engineer"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_SOLDIER:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SOLDIER))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Soldier"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_SCIENTIST:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SCIENTIST))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Scientist"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_BIOLOGIST:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_BIOLOGIST))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Biologist"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_LOOPER:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_LOOPER))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Looper"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_MEDIC:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_MEDIC))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Medic"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_HERO:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_HERO))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Hero"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_NINJA:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_NINJA))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Ninja"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_MERCENARY:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_MERCENARY))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Mercenary"), NULL);
						Broadcast = true;
					}
					break;
				case CMapConverter::MENUCLASS_SNIPER:
					if(GameServer()->m_pController->IsChoosableClass(PLAYERCLASS_SNIPER))
					{
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Sniper"), NULL);
						Broadcast = true;
					}
					break;
			}
		}

		if(!Broadcast)
		{
			m_pPlayer->m_MapMenuItem = -1;
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Choose your class"), NULL);
		}

		if(m_pPlayer->MapMenuClickable() && m_Input.m_Fire&1 && m_pPlayer->m_MapMenuItem >= 0)
		{
			bool Bonus = false;

			int NewClass = -1;
			switch(m_pPlayer->m_MapMenuItem)
			{
				case CMapConverter::MENUCLASS_MEDIC:
					NewClass = PLAYERCLASS_MEDIC;
					break;
				case CMapConverter::MENUCLASS_HERO:
					NewClass = PLAYERCLASS_HERO;
					break;
				case CMapConverter::MENUCLASS_NINJA:
					NewClass = PLAYERCLASS_NINJA;
					break;
				case CMapConverter::MENUCLASS_MERCENARY:
					NewClass = PLAYERCLASS_MERCENARY;
					break;
				case CMapConverter::MENUCLASS_SNIPER:
					NewClass = PLAYERCLASS_SNIPER;
					break;
				case CMapConverter::MENUCLASS_RANDOM:
					NewClass = GameServer()->m_pController->ChooseHumanClass(m_pPlayer);
					Bonus = true;
					break;
				case CMapConverter::MENUCLASS_ENGINEER:
					NewClass = PLAYERCLASS_ENGINEER;
					break;
				case CMapConverter::MENUCLASS_SOLDIER:
					NewClass = PLAYERCLASS_SOLDIER;
					break;
				case CMapConverter::MENUCLASS_SCIENTIST:
					NewClass = PLAYERCLASS_SCIENTIST;
					break;
				case CMapConverter::MENUCLASS_BIOLOGIST:
					NewClass = PLAYERCLASS_BIOLOGIST;
					break;
				case CMapConverter::MENUCLASS_LOOPER:
					NewClass = PLAYERCLASS_LOOPER;
					break;
			}

			if(NewClass >= 0 && GameServer()->m_pController->IsChoosableClass(NewClass))
			{
				SetAntiFire();
				m_pPlayer->m_MapMenuItem = 0;
				m_pPlayer->SetClass(NewClass);
				m_pPlayer->SetOldClass(NewClass);
				
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "choose_class player='%s' class='%d' random='%d'", Server()->ClientName(m_pPlayer->GetCID()), NewClass, Bonus);
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);

				if(Bonus)
					IncreaseArmor(10);
			}
		}
	}
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
