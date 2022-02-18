#include "human.h"

#include <engine/shared/config.h>
#include <game/server/classes.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/entities/biologist-mine.h>
#include <game/server/infclass/entities/blinding-laser.h>
#include <game/server/infclass/entities/engineer-wall.h>
#include <game/server/infclass/entities/hero-flag.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/entities/laser-teleport.h>
#include <game/server/infclass/entities/looper-wall.h>
#include <game/server/infclass/entities/merc-bomb.h>
#include <game/server/infclass/entities/scientist-mine.h>
#include <game/server/infclass/entities/soldier-bomb.h>
#include <game/server/infclass/entities/white-hole.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>
#include <game/server/teeinfo.h>

static const int SniperPositionLockTimeLimit = 15;

MACRO_ALLOC_POOL_ID_IMPL(CInfClassHuman, MAX_CLIENTS)

CInfClassHuman::CInfClassHuman(CInfClassPlayer *pPlayer)
	: CInfClassPlayerClass(pPlayer)
{
}

SkinGetter CInfClassHuman::SetupSkin(CSkinContext *pOutput) const
{
	pOutput->PlayerClass = GetPlayerClass();
	pOutput->ExtraData1 = 0;

	return CInfClassHuman::SetupSkin;
}

bool CInfClassHuman::SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion)
{
	switch(Context.PlayerClass)
	{
	case PLAYERCLASS_ENGINEER:
		pOutput->UseCustomColor = 0;
		pOutput->pSkinName = "limekitty";
		break;
	case PLAYERCLASS_SOLDIER:
		pOutput->pSkinName = "brownbear";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_SNIPER:
		pOutput->pSkinName = "warpaint";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_MERCENARY:
		pOutput->pSkinName = "bluestripe";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_SCIENTIST:
		pOutput->pSkinName = "toptri";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_BIOLOGIST:
		pOutput->pSkinName = "twintri";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_LOOPER:
		pOutput->pSkinName = "bluekitty";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 255;
		pOutput->ColorFeet = 0;
		break;
	case PLAYERCLASS_MEDIC:
		pOutput->pSkinName = "twinbop";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_HERO:
		pOutput->pSkinName = "redstripe";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_NINJA:
		pOutput->pSkinName = "default";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 255;
		pOutput->ColorFeet = 0;
		break;
	case PLAYERCLASS_NONE:
		pOutput->pSkinName = "default";
		pOutput->UseCustomColor = 0;
		break;
	default:
		return false;
	}

	return true;
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
			if(m_pCharacter->PositionIsLocked())
			{
				--m_PositionLockTicksRemaining;
				if((m_PositionLockTicksRemaining <= 0) || m_pCharacter->m_Core.m_IsPassenger)
				{
					m_pCharacter->UnlockPosition();
				}
			}

			if(!m_pCharacter->PositionIsLocked())
			{
				if(m_pCharacter->IsGrounded())
				{
					m_PositionLockTicksRemaining = Server()->TickSpeed() * SniperPositionLockTimeLimit;
				}
			}

			if(m_pCharacter->PositionIsLocked())
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
		case PLAYERCLASS_HERO:
		{
			if(m_pHeroFlag && Config()->m_InfHeroFlagIndicator)
			{
				long TickLimit = m_pPlayer->m_LastActionMoveTick + Config()->m_InfHeroFlagIndicatorTime * Server()->TickSpeed();

				// Guide hero to flag
				if(m_pHeroFlag->GetCoolDown() <= 0 && Server()->Tick() > TickLimit)
				{
					CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_pCharacter->GetCursorID(), sizeof(CNetObj_Laser)));
					if(!pObj)
						return;

					float Angle = atan2f(m_pHeroFlag->GetPos().y-GetPos().y, m_pHeroFlag->GetPos().x-GetPos().x);
					vec2 vecDir = vec2(cos(Angle), sin(Angle));
					vec2 Indicator = GetPos() + vecDir * 84.0f;
					vec2 IndicatorM = GetPos() - vecDir * 84.0f;

					// display laser beam for 0.5 seconds
					int TickShowBeamTime = Server()->TickSpeed() * 0.5;
					long TicksInactive = TickShowBeamTime - (Server()->Tick()-TickLimit);
					if(g_Config.m_InfHeroFlagIndicatorTime > 0 && TicksInactive > 0)
					{
						// TODO: Probably it is incorrect to use Character->GetID() here
						CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_pCharacter->GetID(), sizeof(CNetObj_Laser)));
						if(!pObj)
							return;

						Indicator = IndicatorM + vecDir * 168.0f * (1.0f-(TicksInactive/(float)TickShowBeamTime));

						pObj->m_X = (int)Indicator.x;
						pObj->m_Y = (int)Indicator.y;
						pObj->m_FromX = (int)IndicatorM.x;
						pObj->m_FromY = (int)IndicatorM.y;
						if(TicksInactive < 4)
							pObj->m_StartTick = Server()->Tick()-(6-TicksInactive);
						else
							pObj->m_StartTick = Server()->Tick()-3;
					}

					pObj->m_X = (int)Indicator.x;
					pObj->m_Y = (int)Indicator.y;
					pObj->m_FromX = pObj->m_X;
					pObj->m_FromY = pObj->m_Y;
					pObj->m_StartTick = Server()->Tick();
				}
			}
		}
			break;
		case PLAYERCLASS_SCIENTIST:
		{
			if(m_pCharacter->GetActiveWeapon() == WEAPON_GRENADE)
			{
				vec2 PortalPos;

				if(FindPortalPosition(&PortalPos))
				{
					const int CursorID = GameController()->GetPlayerOwnCursorID(GetCID());
					GameController()->SendHammerDot(PortalPos, CursorID);
				}
			}
		}
			break;
		default:
			break;
		}
	}
}

void CInfClassHuman::OnHammerFired(WeaponFireContext *pFireContext)
{
	switch(GetPlayerClass())
	{
		case PLAYERCLASS_SNIPER:
			if(m_pCharacter->PositionIsLocked())
			{
				m_pCharacter->UnlockPosition();
			}
			else if(PositionLockAvailable())
			{
				m_pCharacter->LockPosition();
			}
		default:
			break;
	}
}

void CInfClassHuman::OnGrenadeFired(WeaponFireContext *pFireContext)
{
	switch(GetPlayerClass())
	{
	case PLAYERCLASS_SCIENTIST:
	{
		vec2 PortalPos;
		if(FindPortalPosition(&PortalPos))
		{
			vec2 OldPos = GetPos();
			m_pCharacter->m_Core.m_Pos = PortalPos;
			m_pCharacter->m_Core.m_HookedPlayer = -1;
			m_pCharacter->m_Core.m_HookState = HOOK_RETRACTED;
			m_pCharacter->m_Core.m_HookPos = PortalPos;
			if(g_Config.m_InfScientistTpSelfharm > 0) {
				m_pCharacter->TakeDamage(vec2(0.0f, 0.0f), g_Config.m_InfScientistTpSelfharm * 2, GetCID(), DAMAGE_TYPE::SCIENTIST_TELEPORT);
			}
			GameServer()->CreateDeath(OldPos, GetCID());
			GameServer()->CreateDeath(PortalPos, GetCID());
			GameServer()->CreateSound(PortalPos, SOUND_CTF_RETURN);
			new CLaserTeleport(GameServer(), PortalPos, OldPos);
		}
		else
		{
			pFireContext->FireAccepted = false;
		}
	}
	default:
		break;
	}
}

void CInfClassHuman::OnLaserFired(WeaponFireContext *pFireContext)
{
	switch(GetPlayerClass())
	{
		case PLAYERCLASS_NINJA:
			OnBlindingLaserFired(pFireContext);
			break;
		default:
			break;
	}
}

void CInfClassHuman::GiveClassAttributes()
{
	if(!m_pCharacter)
	{
		return;
	}

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
			if(GameController()->MercBombsEnabled())
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
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
			break;
		case PLAYERCLASS_NONE:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
			break;
	}

	if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		m_PositionLockTicksRemaining = Server()->TickSpeed() * SniperPositionLockTimeLimit;;
	}
	else
	{
		m_PositionLockTicksRemaining = 0;
	}

	if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		if(!m_pHeroFlag)
			m_pHeroFlag = new CHeroFlag(GameServer(), m_pPlayer->GetCID());
	}

	m_pCharacter->UnlockPosition();
}

void CInfClassHuman::DestroyChildEntities()
{
	if(!m_pCharacter)
	{
		return;
	}

	m_PositionLockTicksRemaining = 0;
	if(m_pHeroFlag)
	{
		// The flag removed in CInfClassCharacter::DestroyChildEntities()
		// delete m_pHeroFlag;
		m_pHeroFlag = nullptr;
	}
	m_pCharacter->UnlockPosition();
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
				_CP("Soldier", "One bomb left", "{int:NumBombs} bombs left"),
				"NumBombs", &NumBombs,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
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
				_C("Biologist", "Mine activated"),
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
				_C("Ninja", "Next target in {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
		else if(TargetID >= 0)
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_C("Ninja", "Target to eliminate: {str:PlayerName}"),
				"PlayerName", Server()->ClientName(TargetID),
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		if(m_pCharacter->PositionIsLocked())
		{
			int Seconds = 1+m_PositionLockTicksRemaining/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_C("Sniper", "Position lock: {sec:RemainingTime}"),
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
						_C("Mercenary", "Use the laser to upgrade the bomb"), NULL);

					dynamic_string Line2;
					Server()->Localization()->Format(Line2, GetPlayer()->GetLanguage(),
						_C("Mercenary", "Explosive yield: {percent:BombLevel}"), "BombLevel", &BombLevel, NULL);

					Line1.append("\n");
					Line1.append(Line2);

					GameServer()->AddBroadcast(GetPlayer()->GetCID(), Line1.buffer(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
				}
				else
				{
					GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
						_C("Mercenary", "The bomb is fully upgraded.\n"
						  "There is nothing to do with the laser."), NULL
					);
				}
			}
			else
			{
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_C("Mercenary", "Explosive yield: {percent:BombLevel}"),
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
					_C("Mercenary", "Use the hammer to place a bomb and\n"
					  "then use the laser to upgrade it"),
					NULL
				);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		//Search for flag
		int CoolDown = m_pHeroFlag ? m_pHeroFlag->GetCoolDown() : 0;

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

void CInfClassHuman::OnBlindingLaserFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
	{
		return;
	}

	new CBlindingLaser(GameContext(), GetPos(), GetDirection(), GetCID());
}

bool CInfClassHuman::PositionLockAvailable() const
{
	const int TickSpeed = GameContext()->Server()->TickSpeed();
	if(m_PositionLockTicksRemaining < TickSpeed * (SniperPositionLockTimeLimit - 1))
	{
		return false;
	}

	if(GetPos().y <= -600)
	{
		return false;
	}

	if(m_pCharacter->m_Core.m_IsPassenger)
	{
		return false;
	}

	return true;
}

bool CInfClassHuman::FindPortalPosition(vec2 *pPosition)
{
	vec2 PortalShift = vec2(m_pCharacter->m_Input.m_TargetX, m_pCharacter->m_Input.m_TargetY);
	vec2 PortalDir = normalize(PortalShift);
	if(length(PortalShift) > 500.0f)
		PortalShift = PortalDir * 500.0f;

	float Iterator = length(PortalShift);
	while(Iterator > 0.0f)
	{
		PortalShift = PortalDir * Iterator;
		vec2 PortalPos = GetPos() + PortalShift;

		if(GameServer()->m_pController->IsSpawnable(PortalPos, ZONE_TELE_NOSCIENTIST))
		{
			*pPosition = PortalPos;
			return true;
		}

		Iterator -= 4.0f;
	}

	return false;
}

void CInfClassHuman::OnSlimeEffect(int Owner)
{
	int Count = Config()->m_InfSlimePoisonDuration;
	Poison(Count, Owner, DAMAGE_TYPE::SLUG_SLIME);
}
