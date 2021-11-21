#include "human.h"

#include <engine/shared/config.h>
#include <game/server/classes.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/biologist-mine.h>
#include <game/server/infclass/entities/engineer-wall.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/entities/looper-wall.h>
#include <game/server/infclass/entities/merc-bomb.h>
#include <game/server/infclass/entities/scientist-mine.h>
#include <game/server/infclass/entities/soldier-bomb.h>
#include <game/server/infclass/entities/white-hole.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>
#include <game/server/infclass/entities/fking-power.h>
#include <game/server/teeinfo.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassHuman, MAX_CLIENTS)

CInfClassHuman::CInfClassHuman(CInfClassPlayer *pPlayer)
	: CInfClassPlayerClass(pPlayer)
{
}

void CInfClassHuman::GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams)
{
	int InfWID = m_pCharacter->GetInfWeaponID(Weapon);
	pParams->MaxAmmo = Server()->GetMaxAmmo(InfWID);
	pParams->RegenInterval = Server()->GetAmmoRegenTime(InfWID);

	if(InfWID == INFWEAPON_NINJA_GRENADE)
	{
		pParams->MaxAmmo = minimum(pParams->MaxAmmo + m_pCharacter->m_NinjaAmmoBuff, 10);
	}

	if(InfWID == INFWEAPON_MERCENARY_GUN)
	{
		if(m_pCharacter->GetInAirTick() > Server()->TickSpeed()*4)
		{
			pParams->RegenInterval = 0;
		}
	}
}

void CInfClassHuman::OnCharacterPreCoreTick()
{
	CInfClassPlayerClass::OnCharacterPreCoreTick();

	switch (GetPlayerClass())
	{
		case PLAYERCLASS_SNIPER:
		{
			if(m_pCharacter->m_PositionLocked)
			{
				if(m_pCharacter->m_Input.m_Jump && !m_pCharacter->m_PrevInput.m_Jump)
				{
					m_pCharacter->UnlockPosition();
				}
				else
				{
					m_pCharacter->ResetMovementsInput();
				}
			}

			if(m_pCharacter->GetInAirTick() <= Server()->TickSpeed())
			{
				// Allow to re-lock in during the first second
				m_pCharacter->m_PositionLockAvailable = true;
			}
		}
			break;
		case PLAYERCLASS_NINJA:
		{
			if(m_pCharacter->IsGrounded() && m_pCharacter->m_DartLifeSpan <= 0)
			{
				m_pCharacter->m_DartLeft = Config()->m_InfNinjaJump;
			}
		}
			break;
		default:
			break;
	}
}

void CInfClassHuman::OnCharacterTick()
{
	CInfClassPlayerClass::OnCharacterTick();
}

void CInfClassHuman::OnCharacterSnap(int SnappingClient)
{
	if(SnappingClient == m_pPlayer->GetCID())
	{
		switch(GetPlayerClass())
		{
			case PLAYERCLASS_SCIENTIST:
			{
				if(m_pCharacter->GetActiveWeapon() == WEAPON_GRENADE)
				{
					vec2 PortalShift = vec2(m_pCharacter->m_Input.m_TargetX, m_pCharacter->m_Input.m_TargetY);
					vec2 PortalDir = normalize(PortalShift);
					if(length(PortalShift) > 500.0f)
						PortalShift = PortalDir * 500.0f;
					vec2 PortalPos;

					if(m_pCharacter->FindPortalPosition(GetPos() + PortalShift, PortalPos))
					{
						const int CursorID = GameController()->GetPlayerOwnCursorID(GetCID());
						CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, CursorID, sizeof(CNetObj_Projectile)));
						if(!pObj)
							return;

						pObj->m_X = (int)PortalPos.x;
						pObj->m_Y = (int)PortalPos.y;
						pObj->m_VelX = 0;
						pObj->m_VelY = 0;
						pObj->m_StartTick = Server()->Tick();
						pObj->m_Type = WEAPON_HAMMER;
					}
				}
			}
				break;
			default:
				break;
		}
	}
}

void CInfClassHuman::GiveClassAttributes()
{
	CInfClassPlayerClass::GiveClassAttributes();

	switch(GetPlayerClass())
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
		case PLAYERCLASS_FKING:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_SHOTGUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
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
		case PLAYERCLASS_FKING:
			output->SetSkinName("saddo");
			output->m_UseCustomColor = 1;
			output->m_ColorBody = 218321;
			output->m_ColorFeet = 218321;
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
	SetupSkin(GetPlayerClass(), output);
}

void CInfClassHuman::BroadcastWeaponState()
{
	if(GetPlayerClass() == PLAYERCLASS_ENGINEER)
	{
		CEngineerWall* pCurrentWall = NULL;
		for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(pCurrentWall)
		{
			int Seconds = 1+pCurrentWall->GetTick()/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Laser wall: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_MEDIC)
	{
		if(m_pCharacter->GetActiveWeapon() == WEAPON_LASER)
		{
			const int MIN_ZOMBIES = 4;
			const int DAMAGE_ON_REVIVE = 17;

			if(m_pCharacter->GetHealthArmorSum() <= DAMAGE_ON_REVIVE)
			{
				int MinHp = DAMAGE_ON_REVIVE + 1;
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("You need at least {int:MinHp} HP to revive a zombie"),
					"MinHp", &MinHp,
					NULL
				);
			}
			else if (GameServer()->GetZombieCount() <= MIN_ZOMBIES)
			{
				int MinZombies = MIN_ZOMBIES+1;
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("Too few zombies to revive anyone (less than {int:MinZombies})"),
					"MinZombies", &MinZombies,
					NULL
				);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_LOOPER)
	{
		//Potential variable name conflict with engineerwall with pCurrentWall
		CLooperWall* pCurrentWall = NULL;
		for(CLooperWall *pWall = (CLooperWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(pCurrentWall)
		{
			int Seconds = 1+pCurrentWall->GetTick()/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Looper laser wall: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SOLDIER)
	{
		int NumBombs = 0;
		for(CSoldierBomb *pBomb = (CSoldierBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb*) pBomb->TypeNext())
		{
			if(pBomb->GetOwner() == m_pPlayer->GetCID())
				NumBombs += pBomb->GetNbBombs();
		}

		if(NumBombs)
		{
			GameServer()->SendBroadcast_Localization_P(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				NumBombs,
				_P("One bomb left", "{int:NumBombs} bombs left"),
				"NumBombs", &NumBombs,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_FKING)
	{
		int NumP = 0;
		for(CFKingPower *pP = (CFKingPower*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_FKING_POWER); pP; pP = (CFKingPower*) pP->TypeNext())
		{
			if(pP->GetOwner() == m_pPlayer->GetCID())
				NumP += pP->GetNbP();
		}

		if(NumP)
		{
			GameServer()->SendBroadcast_Localization_P(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				NumP,
				_P("One power left", "{int:NumP} powers left"),
				"NumP", &NumP,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST || GetPlayerClass() == PLAYERCLASS_FKING)
	{
		int NumMines = 0;
		for(CScientistMine *pMine = (CScientistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE); pMine; pMine = (CScientistMine*) pMine->TypeNext())
		{
			if(pMine->GetOwner() == m_pPlayer->GetCID())
				NumMines++;
		}

		CWhiteHole* pCurrentWhiteHole = NULL;
		for(CWhiteHole *pWhiteHole = (CWhiteHole*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_WHITE_HOLE); pWhiteHole; pWhiteHole = (CWhiteHole*) pWhiteHole->TypeNext())
		{
			if(pWhiteHole->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentWhiteHole = pWhiteHole;
				break;
			}
		}

		if(m_pCharacter->m_BroadcastWhiteHoleReady+(2*Server()->TickSpeed()) > Server()->Tick())
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("The white hole is available!"),
				NULL
			);
		}
		else if(NumMines > 0 && !pCurrentWhiteHole)
		{
			GameServer()->SendBroadcast_Localization_P(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME, NumMines,
				_P("One mine is active", "{int:NumMines} mines are active"),
				"NumMines", &NumMines,
				NULL
			);
		}
		else if(NumMines <= 0 && pCurrentWhiteHole)
		{
			int Seconds = 1+pCurrentWhiteHole->LifeSpan()/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("White hole: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
		else if(NumMines > 0 && pCurrentWhiteHole)
		{
			dynamic_string Buffer;
			Server()->Localization()->Format_LP(Buffer, GetPlayer()->GetLanguage(), NumMines,
				_P("One mine is active", "{int:NumMines} mines are active"),
				"NumMines", &NumMines,
				nullptr);
			Buffer.append("\n");
			int Seconds = 1+pCurrentWhiteHole->LifeSpan()/Server()->TickSpeed();
			Server()->Localization()->Format_L(Buffer, GetPlayer()->GetLanguage(),
				_("White hole: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				nullptr);
			GameServer()->SendBroadcast(GetCID(), Buffer.buffer(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
	{
		int NumMines = 0;
		for(CBiologistMine *pMine = (CBiologistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*) pMine->TypeNext())
		{
			if(pMine->GetOwner() == m_pPlayer->GetCID())
				NumMines++;
		}

		if(NumMines > 0)
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Mine activated"),
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_NINJA)
	{
		int TargetID = GameController()->GetTargetToKill();
		int CoolDown = GameController()->GetTargetToKillCoolDown();

		if(CoolDown > 0)
		{
			int Seconds = 1+CoolDown/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Next target in {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
		else if(TargetID >= 0)
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Target to eliminate: {str:PlayerName}"),
				"PlayerName", Server()->ClientName(TargetID),
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		if(m_pCharacter->m_PositionLocked)
		{
			int Seconds = 1+m_pCharacter->m_PositionLockTick/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Position lock: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		CMercenaryBomb *pCurrentBomb = nullptr;
		for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
		{
			if(pBomb->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentBomb = pBomb;
				break;
			}
		}

		if(pCurrentBomb)
		{
			float BombLevel = pCurrentBomb->m_Damage/static_cast<float>(Config()->m_InfMercBombs);

			if(m_pCharacter->GetActiveWeapon() == WEAPON_LASER)
			{
				if(BombLevel < 1.0)
				{
					dynamic_string Line1;
					Server()->Localization()->Format(Line1, GetPlayer()->GetLanguage(),
						_("Use the laser to upgrade the bomb"), NULL);

					dynamic_string Line2;
					Server()->Localization()->Format(Line2, GetPlayer()->GetLanguage(),
						_("Explosive yield: {percent:BombLevel}"), "BombLevel", &BombLevel, NULL);

					Line1.append("\n");
					Line1.append(Line2);

					GameServer()->AddBroadcast(GetPlayer()->GetCID(), Line1.buffer(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
				}
				else
				{
					GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
						_("The bomb is fully upgraded.\n"
						  "There is nothing to do with the laser."), NULL
					);
				}
			}
			else
			{
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("Explosive yield: {percent:BombLevel}"),
					"BombLevel", &BombLevel,
					NULL
				);
			}
		}
		else
		{
			if(m_pCharacter->GetActiveWeapon() == WEAPON_LASER)
			{
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("Use the hammer to place a bomb and\n"
					  "then use the laser to upgrade it"),
					NULL
				);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		//Search for flag
		int CoolDown = m_pCharacter->GetFlagCoolDown();

		if(m_pCharacter->GetActiveWeapon() == WEAPON_HAMMER)
		{
			int Turrets = m_pCharacter->m_TurretCount;
			if(Turrets > 0)
			{
				int MaxTurrets = Config()->m_InfTurretMaxPerPlayer;
				if(MaxTurrets == 1)
				{
					GameServer()->SendBroadcast_Localization(GetCID(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
						_("You have a turret. Use the hammer to place it."),
						nullptr
					);
				}
				else
				{
					GameServer()->SendBroadcast_Localization_P(GetCID(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME, Turrets,
						_("You have {int:NumTurrets} of {int:MaxTurrets} turrets. Use the hammer to place one."),
						"NumTurrets", &Turrets,
						"MaxTurrets", &MaxTurrets,
						nullptr
					);
				}
			}
			else
			{
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("You don't have a turret to place"),
					nullptr
				);
			}
		}
		else if(CoolDown > 0)
		{
			int Seconds = 1+CoolDown/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Next flag in {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				nullptr
			);
		}
	}
}

void CInfClassHuman::OnSlimeEffect(int Owner)
{
	int Count = Config()->m_InfSlimePoisonDuration;
	Poison(Count, Owner);
}
