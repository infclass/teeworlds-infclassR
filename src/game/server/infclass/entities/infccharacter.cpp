#include "infccharacter.h"

#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>

#include <game/server/entities/projectile.h>

#include <game/server/infclass/entities/biologist-mine.h>
#include <game/server/infclass/entities/bouncing-bullet.h>
#include <game/server/infclass/entities/engineer-wall.h>
#include <game/server/infclass/entities/growingexplosion.h>
#include <game/server/infclass/entities/hero-flag.h>
#include <game/server/infclass/entities/infc-laser.h>
#include <game/server/infclass/entities/laser-teleport.h>
#include <game/server/infclass/entities/looper-wall.h>
#include <game/server/infclass/entities/medic-grenade.h>
#include <game/server/infclass/entities/merc-bomb.h>
#include <game/server/infclass/entities/plasma.h>
#include <game/server/infclass/entities/portal.h>
#include <game/server/infclass/entities/scatter-grenade.h>
#include <game/server/infclass/entities/scientist-laser.h>
#include <game/server/infclass/entities/scientist-mine.h>
#include <game/server/infclass/entities/slug-slime.h>
#include <game/server/infclass/entities/soldier-bomb.h>
#include <game/server/infclass/entities/superweapon-indicator.h>
#include <game/server/infclass/entities/turret.h>
#include <game/server/infclass/entities/white-hole.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassCharacter, MAX_CLIENTS)

CInfClassCharacter::CInfClassCharacter(CInfClassGameController *pGameController)
	: CCharacter(pGameController->GameWorld(), pGameController->Console())
	, m_pGameController(pGameController)
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
		m_pClass->OnCharacterTick();
}

void CInfClassCharacter::SpecialSnapForClient(int SnappingClient, bool *pDoSnap)
{
	CInfClassPlayer* pDestClient = GameController()->GetPlayer(SnappingClient);

	if(GetPlayerClass() == PLAYERCLASS_GHOST)
	{
		if(pDestClient && !pDestClient->IsZombie() && m_IsInvisible)
		{
			*pDoSnap = false;
			return;
		}
	}
	if(GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, m_FlagID, sizeof(CNetObj_Flag));
		if(!pFlag)
			return;
	
		pFlag->m_X = (int)m_Pos.x;
		pFlag->m_Y = (int)m_Pos.y;
		pFlag->m_Team = TEAM_RED;
	}

	if(m_Armor < 10 && SnappingClient != m_pPlayer->GetCID() && IsHuman() && GetPlayerClass() != PLAYERCLASS_HERO)
	{
		if(pDestClient && pDestClient->GetClass() == PLAYERCLASS_MEDIC)
		{
			CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
			if(!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			if(m_Health < 10 && m_Armor == 0)
				pP->m_Type = POWERUP_HEALTH;
			else
				pP->m_Type = POWERUP_ARMOR;
			pP->m_Subtype = 0;
		}
	}
	else if((m_Armor + m_Health) < 10 && SnappingClient != m_pPlayer->GetCID() && IsZombie() && pDestClient && pDestClient->IsZombie())
	{
		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
		if(!pP)
			return;

		pP->m_X = (int)m_Pos.x;
		pP->m_Y = (int)m_Pos.y - 60.0;
		pP->m_Type = POWERUP_HEALTH;
		pP->m_Subtype = 0;
	}

	bool ShowFirstShot = (SnappingClient == -1) || (pDestClient && pDestClient->IsHuman());
	if(ShowFirstShot && GetPlayerClass() == PLAYERCLASS_ENGINEER && !m_FirstShot)
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

		if(!pCurrentWall)
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_BarrierHintID, sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = (int)m_FirstShotCoord.x;
			pObj->m_Y = (int)m_FirstShotCoord.y;
			pObj->m_FromX = (int)m_FirstShotCoord.x;
			pObj->m_FromY = (int)m_FirstShotCoord.y;
			pObj->m_StartTick = Server()->Tick();
			
		}
	}
	if(ShowFirstShot && GetPlayerClass() == PLAYERCLASS_LOOPER && !m_FirstShot)
	{
		CLooperWall* pCurrentWall = NULL;
		for(CLooperWall *pWall = (CLooperWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(!pCurrentWall)
		{
			for(int i=0; i<2; i++)
			{

				CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_BarrierHintIDs[i], sizeof(CNetObj_Laser)));

				if(!pObj)
					return;

				pObj->m_X = (int)m_FirstShotCoord.x-CLooperWall::THICKNESS*i+(CLooperWall::THICKNESS*0.5);
				pObj->m_Y = (int)m_FirstShotCoord.y;
				pObj->m_FromX = (int)m_FirstShotCoord.x-CLooperWall::THICKNESS*i+(CLooperWall::THICKNESS*0.5);
				pObj->m_FromY = (int)m_FirstShotCoord.y;
				pObj->m_StartTick = Server()->Tick();
			}
		}
	}
}

void CInfClassCharacter::FireWeapon()
{
/* INFECTION MODIFICATION START ***************************************/
	if(m_AntiFireTime > 0)
		return;

	if(IsFrozen())
		return;
/* INFECTION MODIFICATION END *****************************************/
	
	if(m_ReloadTimer != 0)
		return;

/* INFECTION MODIFICATION START ***************************************/
	if(GetPlayerClass() == PLAYERCLASS_NONE)
		return;
/* INFECTION MODIFICATION END *****************************************/

	DoWeaponSwitch();

	bool FullAuto = false;

	if(m_ActiveWeapon == WEAPON_GUN || m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_LASER)
		FullAuto = true;

	if(GetPlayerClass() == PLAYERCLASS_SLUG && m_ActiveWeapon == WEAPON_HAMMER)
		FullAuto = true;

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;
	else if(FullAuto && (m_LatestInput.m_Fire&1) && (m_aWeapons[m_ActiveWeapon].m_Ammo || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MERCENARY_GRENADE)
																					   || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MEDIC_GRENADE)))
	{
		WillFire = true;
	}

	if(!WillFire || m_pPlayer->MapMenu() > 0)
		return;

	WeaponFireContext FireContext;
	FireContext.Weapon = m_ActiveWeapon;
	FireContext.FireAccepted = true;
	FireContext.AmmoConsumed = 1;
	FireContext.AmmoAvailable = m_aWeapons[m_ActiveWeapon].m_Ammo;
	FireContext.NoAmmo = FireContext.AmmoAvailable == 0;

	OnWeaponFired(&FireContext);

	if(FireContext.NoAmmo)
	{
		NoAmmo();
		return;
	}

	if(!FireContext.FireAccepted)
	{
		return;
	}

	m_AttackTick = Server()->Tick();

	int &Ammo = m_aWeapons[m_ActiveWeapon].m_Ammo;
	if(Ammo > 0) // -1 == unlimited
	{
		Ammo = maximum(0, Ammo - FireContext.AmmoConsumed);
	}

	if(!m_ReloadTimer)
	{
		m_ReloadTimer = Server()->GetFireDelay(GetInfWeaponID(m_ActiveWeapon)) * Server()->TickSpeed() / 1000;
	}
}

void CInfClassCharacter::OnWeaponFired(WeaponFireContext *pFireContext)
{
	GetClass()->OnWeaponFired(pFireContext);

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

void CInfClassCharacter::OnHammerFired(WeaponFireContext *pFireContext)
{
	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;

	bool AutoFire = false;
	bool FullAuto = false;

	if(GetPlayerClass() == PLAYERCLASS_SLUG)
		FullAuto = true;

	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
	{
	}
	else if(FullAuto && (m_LatestInput.m_Fire&1) && (pFireContext->AmmoAvailable))
	{
		AutoFire = true;
	}

	if(GetPlayerClass() == PLAYERCLASS_ENGINEER)
	{
		for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
				GameWorld()->DestroyEntity(pWall);
		}

		if(m_FirstShot)
		{
			m_FirstShot = false;
			m_FirstShotCoord = GetPos();
		}
		else if(distance(m_FirstShotCoord, GetPos()) > 10.0)
		{
			//Check if the barrier is in toxic gases
			bool isAccepted = true;
			for(int i=0; i<15; i++)
			{
				vec2 TestPos = m_FirstShotCoord + (GetPos() - m_FirstShotCoord)*(static_cast<float>(i)/14.0f);
				if(GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_icDamage, TestPos) == ZONE_DAMAGE_INFECTION)
				{
					isAccepted = false;
				}
			}

			if(isAccepted)
			{
				m_FirstShot = true;
				new CEngineerWall(GameServer(), m_FirstShotCoord, GetPos(), m_pPlayer->GetCID());
				GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_LOOPER)
	{
		//Potential variable name conflicts with engineers wall (for example *pWall is used twice for both Looper and Engineer)
		for(CLooperWall *pWall = (CLooperWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
				GameWorld()->DestroyEntity(pWall);
		}

		if(m_FirstShot)
		{
			m_FirstShot = false;
			m_FirstShotCoord = GetPos();
		}
		else if(distance(m_FirstShotCoord, GetPos()) > 10.0)
		{
			//Check if the barrier is in toxic gases
			bool isAccepted = true;
			for(int i=0; i<15; i++)
			{
				vec2 TestPos = m_FirstShotCoord + (GetPos() - m_FirstShotCoord)*(static_cast<float>(i)/14.0f);
				if(GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_icDamage, TestPos) == ZONE_DAMAGE_INFECTION)
				{
					isAccepted = false;
				}
			}

			if(isAccepted)
			{
				m_FirstShot = true;
				
				new CLooperWall(GameServer(), m_FirstShotCoord, GetPos(), m_pPlayer->GetCID());
				
				GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		if(g_Config.m_InfTurretEnable && m_TurretCount)
		{
			if (g_Config.m_InfTurretEnableLaser)
			{
				new CTurret(GameServer(), GetPos(), m_pPlayer->GetCID(), Direction, CTurret::LASER);
			}
			else if (g_Config.m_InfTurretEnablePlasma)
			{
				new CTurret(GameServer(), GetPos(), m_pPlayer->GetCID(), Direction, CTurret::PLASMA);
			}

			GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
			m_TurretCount--;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "placed turret, %i left", m_TurretCount);
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, aBuf, NULL);
		}
		else
		{
			GameServer()->CreateSound(GetPos(), SOUND_WEAPON_NOAMMO);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SOLDIER)
	{
		FireSoldierBomb();
	}
	else if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		if(GetPos().y > -600.0)
		{
			if(m_PositionLockTick <= 0 && m_PositionLockAvailable)
			{
				m_PositionLockTick = Server()->TickSpeed()*15;
				m_PositionLocked = true;
				m_PositionLockAvailable = false;
			}
			else
			{
				m_PositionLockTick = 0;
				m_PositionLocked = false;
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_MERCENARY && g_Config.m_InfMercLove && !GameServer()->m_FunRound)
	{
		CMercenaryBomb* pCurrentBomb = NULL;
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
			if(pCurrentBomb->ReadyToExplode() || distance(pCurrentBomb->GetPos(), GetPos()) > 80.0f)
				pCurrentBomb->Explode();
			else
			{
				pCurrentBomb->IncreaseDamage(WEAPON_HAMMER);
				GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);
			}
		}
		else
		{
			new CMercenaryBomb(GameServer(), GetPos(), m_pPlayer->GetCID());
			GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);
		}

		m_ReloadTimer = Server()->TickSpeed()/4;
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		bool FreeSpace = true;
		int NbMine = 0;
		
		int OlderMineTick = Server()->Tick()+1;
		CScientistMine* pOlderMine = 0;
		CScientistMine* pIntersectMine = 0;
		
		CScientistMine* p = (CScientistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE);
		while(p)
		{
			float d = distance(p->GetPos(), ProjStartPos);
			
			if(p->GetOwner() == m_pPlayer->GetCID())
			{
				if(OlderMineTick > p->m_StartTick)
				{
					OlderMineTick = p->m_StartTick;
					pOlderMine = p;
				}
				NbMine++;

				if(d < 2.0f*g_Config.m_InfMineRadius)
				{
					if(pIntersectMine)
						FreeSpace = false;
					else
						pIntersectMine = p;
				}
			}
			else if(d < 2.0f*g_Config.m_InfMineRadius)
				FreeSpace = false;
			
			p = (CScientistMine *)p->TypeNext();
		}

		if(FreeSpace)
		{
			if(pIntersectMine) //Move the mine
				GameWorld()->DestroyEntity(pIntersectMine);
			else if(NbMine >= g_Config.m_InfMineLimit && pOlderMine)
				GameWorld()->DestroyEntity(pOlderMine);
			
			new CScientistMine(GameServer(), ProjStartPos, m_pPlayer->GetCID());
			
			m_ReloadTimer = Server()->TickSpeed()/2;
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_NINJA)
	{
		if(m_DartLeft || m_InWater)
		{
			if(!m_InWater)
				m_DartLeft--;

			// reset Hit objects
			m_NumObjectsHit = 0;

			m_DartDir = Direction;
			m_DartLifeSpan = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			m_DartOldVelAmount = length(m_Core.m_Vel);

			GameServer()->CreateSound(GetPos(), SOUND_NINJA_HIT);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_BOOMER)
	{
		if(!IsFrozen() && !IsInLove())
		{
			Die(m_pPlayer->GetCID(), WEAPON_SELF);
		}
	}
	else
	{
/* INFECTION MODIFICATION END *****************************************/
		// reset objects Hit
		int Hits = 0;
		bool ShowAttackAnimation = false;

		// make sure that the slug will not auto-fire to attack
		if(!AutoFire)
		{
			ShowAttackAnimation = true;

			m_NumObjectsHit = 0;

			if(GetPlayerClass() == PLAYERCLASS_GHOST)
			{
				m_IsInvisible = false;
				m_InvisibleTick = Server()->Tick();
			}

			CCharacter *apEnts[MAX_CLIENTS];
			int Num = GameWorld()->FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];

				if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), NULL, NULL))
					continue;

				vec2 Dir;
				if (length(pTarget->GetPos() - GetPos()) > 0.0f)
					Dir = normalize(pTarget->GetPos() - GetPos());
				else
					Dir = vec2(0.f, -1.f);

/* INFECTION MODIFICATION START ***************************************/
				if(IsZombie())
				{
					if(pTarget->IsZombie())
					{
						if(pTarget->IsFrozen())
						{
							pTarget->Unfreeze();
							GameServer()->ClearBroadcast(pTarget->GetPlayer()->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE);
						}
						else
						{
							if(pTarget->IncreaseOverallHp(4))
							{
								IncreaseOverallHp(1);
								pTarget->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
							}

							if(!pTarget->GetPlayer()->HookProtectionEnabled())
								pTarget->m_Core.m_Vel += vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
						}
					}
					else if(GetPlayerClass() == PLAYERCLASS_BAT) {
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_Config.m_InfBatDamage,
							m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
					}
					else if(GameServer()->m_pController->IsInfectionStarted())
					{
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
							m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_INFECTION);
					}
				}
				else if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST || GetPlayerClass() == PLAYERCLASS_MERCENARY)
				{
					/* affects mercenary only if love bombs are disabled. */
					if (pTarget->IsZombie())
					{
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20, 
								m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
					}
				}
				else if(GetPlayerClass() == PLAYERCLASS_MEDIC)
				{
					if (pTarget->IsZombie())
					{
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20, 
								m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
					}
					else
					{
						if(pTarget->GetPlayerClass() != PLAYERCLASS_HERO)
						{
							pTarget->IncreaseArmor(4);
							if(pTarget->m_Armor == 10 && pTarget->m_NeedFullHeal)
							{
								Server()->RoundStatistics()->OnScoreEvent(GetPlayer()->GetCID(), SCOREEVENT_HUMAN_HEALING, GetPlayerClass(), Server()->ClientName(GetPlayer()->GetCID()), Console());
								GameServer()->SendScoreSound(GetPlayer()->GetCID());
								pTarget->m_NeedFullHeal = false;
								m_aWeapons[WEAPON_GRENADE].m_Ammo++;
							}
							pTarget->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
						}
					}
				}
				else
				{
					pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
						m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
				}
/* INFECTION MODIFICATION END *****************************************/
				Hits++;

				// set his velocity to fast upward (for now)
				if(length(pTarget->GetPos()-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->GetPos()-normalize(pTarget->GetPos()-ProjStartPos)*m_ProximityRadius*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);
			}

			for(CPortal* pPortal = (CPortal*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_PORTAL); pPortal; pPortal = (CPortal*) pPortal->TypeNext())
			{
				if(m_pPlayer->IsZombie())
					continue;

				if(pPortal->GetOwner() == m_pPlayer->GetCID())
					continue;

				if(distance(GetPos(), pPortal->GetPos()) > (pPortal->m_ProximityRadius + m_ProximityRadius*0.5f))
					continue;

				pPortal->TakeDamage(g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage, m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
				Hits++;
			}
		}

		// if we Hit anything, we have to wait for the reload
		if(Hits)
		{
			m_ReloadTimer = Server()->TickSpeed()/3;
		}
		else if(GetPlayerClass() == PLAYERCLASS_SLUG)
		{
			vec2 CheckPos = GetPos() + Direction * 64.0f;
			if(GameServer()->Collision()->IntersectLine(GetPos(), CheckPos, 0x0, &CheckPos))
			{
				static const float MinDistance = 84.0f;
				float DistanceToTheNearestSlime = MinDistance * 2;
				for(CSlugSlime* pSlime = (CSlugSlime*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SLUG_SLIME); pSlime; pSlime = (CSlugSlime*) pSlime->TypeNext())
				{
					const float d = distance(pSlime->GetPos(), GetPos());
					if(d < DistanceToTheNearestSlime)
					{
						DistanceToTheNearestSlime = d;
					}
					if (d <= MinDistance / 2)
					{
						// Replenish the slime
						if(pSlime->GetMaxLifeSpan() - pSlime->GetLifeSpan() > Server()->TickSpeed())
						{
							pSlime->Replenish(m_pPlayer->GetCID());
							ShowAttackAnimation = true;
							break;
						}
					}
				}

				if(DistanceToTheNearestSlime > MinDistance)
				{
					ShowAttackAnimation = true;
					new CSlugSlime(GameServer(), CheckPos, m_pPlayer->GetCID());
				}
			}
		}

		if(!ShowAttackAnimation)
		{
			pFireContext->FireAccepted = false;
			return;
		}

		GameServer()->CreateSound(GetPos(), SOUND_HAMMER_FIRE);
	}
}

void CInfClassCharacter::OnGunFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
		return;

	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;
	
	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
			m_pPlayer->GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
			1, 0, 0, -1, WEAPON_GUN);

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
		Msg.AddInt(1);
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);

		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
		
		float MaxSpeed = GameServer()->Tuning()->m_GroundControlSpeed*1.7f;
		vec2 Recoil = Direction*(-MaxSpeed/5.0f);
		SaturateVelocity(Recoil, MaxSpeed);

		GameServer()->CreateSound(GetPos(), SOUND_HOOK_LOOP);
	}
	else
	{
		CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
			m_pPlayer->GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
			1, 0, 0, -1, WEAPON_GUN);

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
		Msg.AddInt(1);
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);

		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

		GameServer()->CreateSound(GetPos(), SOUND_GUN_FIRE);
	}
}

void CInfClassCharacter::OnShotgunFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
		return;

	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;

	int ShotSpread = 3;
	if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
		ShotSpread = 1;

	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(ShotSpread*2+1);

	float Force = 2.0f;
	if(GetPlayerClass() == PLAYERCLASS_MEDIC)
		Force = 10.0f;

	for(int i = -ShotSpread; i <= ShotSpread; ++i)
	{
		float Spreading[] = {-0.21f, -0.14f, -0.070f, 0, 0.070f, 0.14f, 0.21f};
		float a = GetAngle(Direction);
		a += Spreading[i+3] * 2.0f*(0.25f + 0.75f*static_cast<float>(10 - pFireContext->AmmoAvailable)/10.0f);
		float v = 1-(absolute(i)/(float)ShotSpread);
		float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);

		float LifeTime = GameServer()->Tuning()->m_ShotgunLifetime + 0.1f*static_cast<float>(pFireContext->AmmoAvailable)/10.0f;

		if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
		{
			CBouncingBullet *pProj = new CBouncingBullet(GameServer(), m_pPlayer->GetCID(), ProjStartPos, vec2(cosf(a), sinf(a))*Speed);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
		}
		else
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				vec2(cosf(a), sinf(a))*Speed,
				(int)(Server()->TickSpeed()*LifeTime),
				1, 0, Force, -1, WEAPON_SHOTGUN);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
		}
	}

	Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

	GameServer()->CreateSound(GetPos(), SOUND_SHOTGUN_FIRE);
}

void CInfClassCharacter::OnGrenadeFired(WeaponFireContext *pFireContext)
{
	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		OnMercGrenadeFired(pFireContext);
		return;
	}

	if(GetPlayerClass() == PLAYERCLASS_MEDIC)
	{
		OnMedicGrenadeFired(pFireContext);
		return;
	}

	if(pFireContext->NoAmmo)
	{
		return;
	}

	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;

	if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		vec2 PortalShift = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
		vec2 PortalDir = normalize(PortalShift);
		if(length(PortalShift) > 500.0f)
			PortalShift = PortalDir * 500.0f;
		vec2 PortalPos;

		if(FindPortalPosition(GetPos() + PortalShift, PortalPos))
		{
			vec2 OldPos = m_Core.m_Pos;
			m_Core.m_Pos = PortalPos;
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_HookPos = m_Core.m_Pos;
			if(g_Config.m_InfScientistTpSelfharm > 0) {
				auto pScientist = GetPlayer()->GetCharacter();
				pScientist->TakeDamage(vec2(0.0f, 0.0f), g_Config.m_InfScientistTpSelfharm * 2, GetPlayer()->GetCID(), WEAPON_HAMMER, TAKEDAMAGEMODE_SELFHARM);
			}
			GameServer()->CreateDeath(OldPos, GetPlayer()->GetCID());
			GameServer()->CreateDeath(PortalPos, GetPlayer()->GetCID());
			GameServer()->CreateSound(PortalPos, SOUND_CTF_RETURN);
			new CLaserTeleport(GameServer(), PortalPos, OldPos);
		}
	}
	else
	{
		if(m_HasStunGrenade)
		{
			int ShotSpread = 2;

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float a = GetAngle(Direction) + random_float()/3.0f;
				
				CScatterGrenade *pProj = new CScatterGrenade(GameServer(), m_pPlayer->GetCID(), GetPos(), vec2(cosf(a), sinf(a)));
				
				if (m_HasStunGrenade)
				{
					//Make them flash grenades
					pProj->FlashGrenade();
				}

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);
				
				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
				Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
			}
			
			GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
			
			m_HasStunGrenade=false;
			GetPlayer()->ResetNumberKills();
			return;
		}
		else
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
												 m_pPlayer->GetCID(),
												 ProjStartPos,
												 Direction,
												 (int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
												 1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

			if(GetPlayerClass() == PLAYERCLASS_NINJA)
			{
				pProj->FlashGrenade();
			}

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(1);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
			Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

			GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
		}
	}
}

void CInfClassCharacter::OnLaserFired(WeaponFireContext *pFireContext)
{
	if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
	{
		OnBiologistLaserFired(pFireContext);
		return;
	}

	if(CanOpenPortals())
	{
		PlacePortal(pFireContext);
		return;
	}

	if(pFireContext->NoAmmo)
	{
		return;
	}

	vec2 Direction = GetDirection();
	int Damage = GameServer()->Tuning()->m_LaserDamage;

	if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		if(m_PositionLocked)
			Damage = 30;
		else
			Damage = random_int(10, 13);
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		//white hole activation in scientist-laser
		
		new CScientistLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach*0.6f, m_pPlayer->GetCID(), Damage);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if (GetPlayerClass() == PLAYERCLASS_LOOPER) 
	{
		Damage = 5;
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach*0.7f, m_pPlayer->GetCID(), Damage);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		Damage = 0;
		m_BombHit = false;

		CMercenaryBomb* pCurrentBomb = NULL;
		for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
		{
			if(pBomb->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentBomb = pBomb;
				break;
			}
		}

		if(!pCurrentBomb)
		{
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, 60, "Bomb needed");
			pFireContext->FireAccepted = false;
			return;
		}
		else
		{
			new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
			GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
			if(m_BombHit && distance(pCurrentBomb->GetPos(), m_AtMercBomb) <= 80.0f)
			{
				pCurrentBomb->IncreaseDamage(WEAPON_LASER);
				GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);
			}
		}
	}
	else
	{
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
}

void CInfClassCharacter::OnNinjaFired(WeaponFireContext *pFireContext)
{
	// The design of ninja supposes different implementation (not via FireWeapon)
}

void CInfClassCharacter::OnMercGrenadeFired(WeaponFireContext *pFireContext)
{
	float BaseAngle = GetAngle(GetDirection());

	//Find bomb
	bool BombFound = false;
	for(CScatterGrenade *pGrenade = (CScatterGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCATTER_GRENADE); pGrenade; pGrenade = (CScatterGrenade*) pGrenade->TypeNext())
	{
		if(pGrenade->GetOwner() != m_pPlayer->GetCID())
			continue;
		pGrenade->Explode();
		BombFound = true;
	}

	if(BombFound)
	{
		pFireContext->AmmoConsumed = 0;
		pFireContext->NoAmmo = false;
		return;
	}

	if(pFireContext->NoAmmo)
		return;

	int ShotSpread = 2;

	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(ShotSpread*2+1);

	for(int i = -ShotSpread; i <= ShotSpread; ++i)
	{
		float a = BaseAngle + random_float()/3.0f;

		CScatterGrenade *pProj = new CScatterGrenade(GameServer(), m_pPlayer->GetCID(), GetPos(), vec2(cosf(a), sinf(a)));

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
	}

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);

	m_ReloadTimer = Server()->TickSpeed()/4;
}

void CInfClassCharacter::OnMedicGrenadeFired(WeaponFireContext *pFireContext)
{
	float BaseAngle = GetAngle(GetDirection());

	//Find bomb
	bool BombFound = false;
	for(CMedicGrenade *pGrenade = (CMedicGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MEDIC_GRENADE); pGrenade; pGrenade = (CMedicGrenade*) pGrenade->TypeNext())
	{
		if(pGrenade->GetOwner() != m_pPlayer->GetCID()) continue;
		pGrenade->Explode();
		BombFound = true;
	}

	if(BombFound)
	{
		pFireContext->AmmoConsumed = 0;
		pFireContext->NoAmmo = false;
		return;
	}

	if(pFireContext->NoAmmo)
		return;

	int ShotSpread = 0;

	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(ShotSpread*2+1);

	for(int i = -ShotSpread; i <= ShotSpread; ++i)
	{
		float a = BaseAngle + random_float()/5.0f;

		CMedicGrenade *pProj = new CMedicGrenade(GameServer(), m_pPlayer->GetCID(), GetPos(), vec2(cosf(a), sinf(a)));

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
	}

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);

	m_ReloadTimer = Server()->TickSpeed()/4;
}

void CInfClassCharacter::OnBiologistLaserFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->AmmoAvailable < 10)
	{
		pFireContext->NoAmmo = true;
		pFireContext->AmmoConsumed = 0;
		return;
	}

	for(CBiologistMine* pMine = (CBiologistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*) pMine->TypeNext())
	{
		if(pMine->GetOwner() != m_pPlayer->GetCID()) continue;
			GameWorld()->DestroyEntity(pMine);
	}

	const float BigLaserMaxLength = 400.0f;
	vec2 To = GetPos() + GetDirection() * BigLaserMaxLength;
	if(GameServer()->Collision()->IntersectLine(GetPos(), To, 0x0, &To))
	{
		new CBiologistMine(GameServer(), GetPos(), To, m_pPlayer->GetCID());
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
		pFireContext->AmmoConsumed = pFireContext->AmmoAvailable;
	}
	else
	{
		pFireContext->FireAccepted = false;
	}
}

void CInfClassCharacter::OpenClassChooser()
{
	if(GameServer()->m_FunRound)
	{
		IncreaseArmor(10);
		m_pPlayer->CloseMapMenu();
		return;
	}

	if(!Server()->IsClassChooserEnabled() || Server()->GetClientAlwaysRandom(m_pPlayer->GetCID()))
	{
		m_pPlayer->SetClass(GameController()->ChooseHumanClass(m_pPlayer));
		if(Server()->IsClassChooserEnabled())
			GiveRandomClassSelectionBonus();
	}
	else
	{
		m_pPlayer->OpenMapMenu(1);
	}
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
			if(m_pPlayer->m_MapMenuItem == CMapConverter::MENUCLASS_RANDOM)
			{
				GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
					BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Random choice"), NULL);
				Broadcast = true;
			}
			else
			{
				int MenuClass = m_pPlayer->m_MapMenuItem;
				int NewClass = CInfClassGameController::MenuClassToPlayerClass(MenuClass);
				if(GameController()->IsChoosableClass(NewClass))
				{
					const char *pClassName = CInfClassGameController::GetClassDisplayName(NewClass);
					GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
						BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, pClassName, NULL);
					Broadcast = true;
				}
			}
		}

		if(!Broadcast)
		{
			m_pPlayer->m_MapMenuItem = -1;
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
				BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Choose your class"), NULL);

			return;
		}

		if(m_pPlayer->MapMenuClickable() && m_Input.m_Fire&1)
		{
			bool Bonus = false;

			int MenuClass = m_pPlayer->m_MapMenuItem;
			int NewClass = CInfClassGameController::MenuClassToPlayerClass(MenuClass);
			if(NewClass == PLAYERCLASS_NONE)
			{
				NewClass = GameController()->ChooseHumanClass(m_pPlayer);
				Bonus = true;
			}
			if(NewClass == PLAYERCLASS_INVALID)
			{
				return;
			}

			if(GameController()->IsChoosableClass(NewClass))
			{
				SetAntiFire();
				m_pPlayer->m_MapMenuItem = 0;
				m_pPlayer->SetClass(NewClass);
				
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "choose_class player='%s' class='%d' random='%d'",
					Server()->ClientName(m_pPlayer->GetCID()), NewClass, Bonus);
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);

				if(Bonus)
					GiveRandomClassSelectionBonus();
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

	bool RefusedToDie = false;
	GetClass()->PrepareToDie(Killer, Weapon, &RefusedToDie);
	if (RefusedToDie)
	{
		return;
	}

	if(GetPlayerClass() == PLAYERCLASS_GHOUL)
	{
		m_pPlayer->IncreaseGhoulLevel(-20);
		GetClass()->UpdateSkin();
	}

	DestroyChildEntities();
/* INFECTION MODIFICATION END *****************************************/

	CInfClassCharacter *pKillerCharacter = nullptr;
	if (Weapon == WEAPON_WORLD && Killer == m_pPlayer->GetCID()) {
		//Search for the real killer (if somebody hooked this player)
		for(CInfClassCharacter *pHooker = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHooker; pHooker = (CInfClassCharacter *)pHooker->TypeNext())
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
		}
	}

	pKillerCharacter = GameController()->GetCharacter(Killer);

	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	CInfClassPlayer* pKillerPlayer = GameController()->GetPlayer(Killer);
	int ModeSpecial = GameController()->OnCharacterDeath(this, pKillerPlayer, Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
		Server()->ClientName(Killer),
		Server()->ClientName(m_pPlayer->GetCID()), Weapon);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	GameServer()->SendKillMessage(Killer, m_pPlayer->GetCID(), Weapon, ModeSpecial);

	// a nice sound
	GameServer()->CreateSound(GetPos(), SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameWorld()->RemoveEntity(this);
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(GetPos(), m_pPlayer->GetCID());

/* INFECTION MODIFICATION START ***************************************/

	if(GetPlayerClass() == PLAYERCLASS_BOOMER && !IsFrozen() && Weapon != WEAPON_GAME && !(IsInLove() && Weapon == WEAPON_SELF) )
	{
		GameServer()->CreateSound(GetPos(), SOUND_GRENADE_EXPLODE);
		GameServer()->CreateExplosionDisk(GetPos(), 60.0f, 80.5f, 14, 52.0f, m_pPlayer->GetCID(), WEAPON_HAMMER, TAKEDAMAGEMODE_INFECTION);
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

	if(pKillerCharacter && (pKillerCharacter != this))
	{
		// set attacker's face to happy (taunt!)
		pKillerCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		pKillerCharacter->CheckSuperWeaponAccess();

		if(pKillerCharacter->GetPlayerClass() == PLAYERCLASS_MERCENARY)
		{
			pKillerCharacter->GiveWeapon(WEAPON_LASER, m_aWeapons[WEAPON_LASER].m_Ammo + 3);
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

int CInfClassCharacter::GetCID() const
{
	if(m_pPlayer)
	{
		return m_pPlayer->GetCID();
	}

	return -1;
}

void CInfClassCharacter::SetClass(CInfClassPlayerClass *pClass)
{
	m_pClass = pClass;
	m_pClass->SetCharacter(this);
}

CInputCount CInfClassCharacter::CountFireInput() const
{
	return CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire);
}

bool CInfClassCharacter::FireJustPressed() const
{
	return m_LatestInput.m_Fire & 1;
}

CGameContext *CInfClassCharacter::GameContext() const
{
	return m_pGameController->GameServer();
}

bool CInfClassCharacter::CanDie() const
{
	return m_pClass && m_pClass->CanDie();
}

bool CInfClassCharacter::IsInvisible() const
{
	return m_IsInvisible;
}

bool CInfClassCharacter::HasHallucination() const
{
	return m_HallucinationTick > 0;
}

void CInfClassCharacter::CheckSuperWeaponAccess()
{
	// check kills of player
	int kills = m_pPlayer->GetNumberKills();

	//Only scientists can receive white holes
	if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		if (!m_HasWhiteHole) // Can't receive a white hole while having one available
		{
			// enable white hole probabilities
			if (kills > g_Config.m_InfWhiteHoleMinimalKills) 
			{
				if (random_int(0,100) < g_Config.m_InfWhiteHoleProbability) 
				{
					//Scientist-laser.cpp will make it unavailable after usage and reset player kills
					
					//create an indicator object
					if (m_HasIndicator == false) {
						m_HasIndicator = true;
						GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("white hole found, adjusting scientific parameters..."), NULL);
						new CSuperWeaponIndicator(GameServer(), GetPos(), m_pPlayer->GetCID());
					}
				} 
			} 
		}
	}
	
	if(GetPlayerClass() == PLAYERCLASS_LOOPER)
	{
		MaybeGiveStunGrenades();
	}
	
	if(GetPlayerClass() == PLAYERCLASS_SOLDIER)
	{
		MaybeGiveStunGrenades();
	}
}

void CInfClassCharacter::MaybeGiveStunGrenades()
{
	if(m_HasStunGrenade)
		return;

	if(m_pPlayer->GetNumberKills() > Config()->m_InfStunGrenadeMinimalKills)
	{
		if(random_int(0,100) < Config()->m_InfStunGrenadeProbability)
		{
				//grenade launcher usage will make it unavailable and reset player kills
			
				m_HasStunGrenade = true;
				GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("stun grenades found..."), NULL);
		}
	}
}

void CInfClassCharacter::FireSoldierBomb()
{
	vec2 ProjStartPos = GetPos()+GetDirection()*GetProximityRadius()*0.75f;

	for(CSoldierBomb *pBomb = (CSoldierBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb*) pBomb->TypeNext())
	{
		if(pBomb->GetOwner() == m_pPlayer->GetCID())
		{
			pBomb->Explode();
			return;
		}
	}

	new CSoldierBomb(GameServer(), ProjStartPos, m_pPlayer->GetCID());
	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
}

void CInfClassCharacter::PlacePortal(WeaponFireContext *pFireContext)
{
	if(IsFrozen() && IsInLove())
		return;

	vec2 TargetPos = GetPos();

	m_ReloadTimer = Server()->TickSpeed() / 4;

	if(GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		if(!FindWitchSpawnPosition(TargetPos))
		{
			// Witch can't place the portal here
			pFireContext->FireAccepted = false;
			return;
		}
	}

	CPortal *PortalToTake = FindPortalInTarget();

	if(PortalToTake)
	{
		PortalToTake->Disconnect();
		GameWorld()->DestroyEntity(PortalToTake);

		if (PortalToTake == m_pPortalIn)
			m_pPortalIn = nullptr;
		if (PortalToTake == m_pPortalOut)
			m_pPortalOut = nullptr;

		GiveWeapon(WEAPON_LASER, m_aWeapons[WEAPON_LASER].m_Ammo + 1);
		return;
	}

	if(pFireContext->NoAmmo)
	{
		return;
	}

	// Place new portal
	int OwnerCID = GetPlayer() ? GetPlayer()->GetCID() : -1;
	CPortal *existingPortal = m_pPortalIn ? m_pPortalIn : m_pPortalOut;
	if(existingPortal && distance(existingPortal->GetPos(), TargetPos) < g_Config.m_InfMinPortalDistance)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Unable to place portals that close to each other");
		GameServer()->SendChatTarget(OwnerCID, aBuf);
		return;
	}

	if(TargetPos.y < -20)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Unable to open a portal at this height");
		GameServer()->SendChatTarget(OwnerCID, aBuf);
		return;
	}

	if(m_pPortalIn && m_pPortalOut)
	{
		m_pPortalOut->Disconnect();
		GameWorld()->DestroyEntity(m_pPortalOut);
		m_pPortalOut = nullptr;
	}

	if(m_pPortalIn)
	{
		m_pPortalOut = new CPortal(GameServer(), TargetPos, OwnerCID, CPortal::PortalType::Out);
		m_pPortalOut->ConnectPortal(m_pPortalIn);
		GameServer()->CreateSound(m_pPortalOut->GetPos(), m_pPortalOut->GetNewEntitySound());
	}
	else
	{
		m_pPortalIn = new CPortal(GameServer(), TargetPos, OwnerCID, CPortal::PortalType::In);
		m_pPortalIn->ConnectPortal(m_pPortalOut);
		GameServer()->CreateSound(m_pPortalIn->GetPos(), m_pPortalIn->GetNewEntitySound());
	}
}

CPortal *CInfClassCharacter::FindPortalInTarget()
{
	vec2 TargetPos = GetPos();

	if (GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		if(!FindWitchSpawnPosition(TargetPos))
		{
			// Witch can't place the portal here
			return nullptr;
		}
	}

	// Check if unmount wanted
	const int displacementExtraDistance = 20;
	if(m_pPortalIn && (distance(m_pPortalIn->GetPos(), TargetPos) < m_ProximityRadius + m_pPortalIn->GetRadius() + displacementExtraDistance))
	{
		return m_pPortalIn;
	}

	if(m_pPortalOut && (distance(m_pPortalOut->GetPos(), TargetPos) < m_ProximityRadius + m_pPortalOut->GetRadius() + displacementExtraDistance))
	{
		return m_pPortalOut;
	}

	return nullptr;
}

void CInfClassCharacter::OnPortalDestroy(CPortal *pPortal)
{
	if (!pPortal)
		return;

	if (m_pPortalIn == pPortal)
	{
		m_pPortalIn->Disconnect();
		m_pPortalIn = nullptr;
	}
	else if (m_pPortalOut == pPortal)
	{
		m_pPortalOut->Disconnect();
		m_pPortalOut = nullptr;
	}
}

bool CInfClassCharacter::ProcessCharacterOnPortal(CPortal *pPortal, CCharacter *pCharacter)
{
	switch (GetPlayerClass())
	{
		case PLAYERCLASS_WITCH:
			if (pPortal->GetPortalType() != CPortal::PortalType::In)
				return false;

			if(!pCharacter->IsZombie())
				return false;

			if (pCharacter == this)
				return false;

			break;

		default:
			return false;
	}

	// The idea here is to have a point to catch all allowed teleportations
	SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
	GameServer()->SendEmoticon(GetPlayer()->GetCID(), EMOTICON_MUSIC);

	Server()->RoundStatistics()->OnScoreEvent(m_pPlayer->GetCID(), SCOREEVENT_PORTAL_USED, GetPlayerClass(), Server()->ClientName(m_pPlayer->GetCID()), Console());
	GameServer()->SendScoreSound(m_pPlayer->GetCID());

	return true;
}

bool CInfClassCharacter::CanOpenPortals() const
{
	return m_canOpenPortals;
}

void CInfClassCharacter::GiveRandomClassSelectionBonus()
{
	IncreaseArmor(10);
}

void CInfClassCharacter::PreCoreTick()
{
	UpdateTuningParam();
}

void CInfClassCharacter::PostCoreTick()
{
	if(m_pPlayer->MapMenu() == 1)
	{
		HandleMapMenu();
	}
}

void CInfClassCharacter::UpdateTuningParam()
{
	CTuningParams* pTuningParams = &m_pPlayer->m_NextTuningParams;
	
	bool NoActions = false;
	bool FixedPosition = false;
	
	if(m_PositionLocked)
	{
		NoActions = true;
		FixedPosition = true;
	}
	if(m_IsFrozen)
	{
		NoActions = true;
	}
	
	if(m_SlowMotionTick > 0)
	{
		float Factor = 1.0f - ((float)g_Config.m_InfSlowMotionPercent / 100);
		float FactorSpeed = 1.0f - ((float)g_Config.m_InfSlowMotionHookSpeed / 100);
		float FactorAccel = 1.0f - ((float)g_Config.m_InfSlowMotionHookAccel / 100);
		pTuningParams->m_GroundControlSpeed = pTuningParams->m_GroundControlSpeed * Factor;
		pTuningParams->m_HookFireSpeed = pTuningParams->m_HookFireSpeed * FactorSpeed;
		//pTuningParams->m_GroundJumpImpulse = pTuningParams->m_GroundJumpImpulse * Factor;
		//pTuningParams->m_AirJumpImpulse = pTuningParams->m_AirJumpImpulse * Factor;
		pTuningParams->m_AirControlSpeed = pTuningParams->m_AirControlSpeed * Factor;
		pTuningParams->m_HookDragAccel = pTuningParams->m_HookDragAccel * FactorAccel;
		pTuningParams->m_HookDragSpeed = pTuningParams->m_HookDragSpeed * FactorSpeed;
		pTuningParams->m_Gravity = g_Config.m_InfSlowMotionGravity * 0.01f;

		if(g_Config.m_InfSlowMotionMaxSpeed > 0)
		{
			float MaxSpeed = g_Config.m_InfSlowMotionMaxSpeed * 0.1f;
			float diff = MaxSpeed / length(m_Core.m_Vel);
			if (diff < 1.0f) m_Core.m_Vel *= diff;
		}
	}
	
	if(m_HookMode == 1)
	{
		pTuningParams->m_HookDragSpeed = 0.0f;
		pTuningParams->m_HookDragAccel = 1.0f;
	}
	if(m_InWater == 1)
	{
		pTuningParams->m_Gravity = -0.05f;
		pTuningParams->m_GroundFriction = 0.95f;
		pTuningParams->m_GroundControlSpeed = 250.0f / Server()->TickSpeed();
		pTuningParams->m_GroundControlAccel = 1.5f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirFriction = 0.95f;
		pTuningParams->m_AirControlSpeed = 250.0f / Server()->TickSpeed();
		pTuningParams->m_AirControlAccel = 1.5f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
	}
	if(m_SlipperyTick > 0)
	{
		pTuningParams->m_GroundFriction = 1.0f;
	}
	
	if(NoActions)
	{
		pTuningParams->m_GroundControlAccel = 0.0f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
		pTuningParams->m_AirControlAccel = 0.0f;
		pTuningParams->m_HookLength = 0.0f;
	}
	if(FixedPosition || m_Core.m_IsPassenger)
	{
		pTuningParams->m_Gravity = 0.0f;
	}
	if(GetPlayer()->HookProtectionEnabled())
	{
		pTuningParams->m_PlayerHooking = 0;
	}
	
	if(GetPlayerClass() == PLAYERCLASS_GHOUL)
	{
		float Factor = GetClass()->GetGhoulPercent();
		pTuningParams->m_GroundControlSpeed = pTuningParams->m_GroundControlSpeed * (1.0f + 0.5f*Factor);
		pTuningParams->m_GroundControlAccel = pTuningParams->m_GroundControlAccel * (1.0f + 0.5f*Factor);
		pTuningParams->m_GroundJumpImpulse = pTuningParams->m_GroundJumpImpulse * (1.0f + 0.35f*Factor);
		pTuningParams->m_AirJumpImpulse = pTuningParams->m_AirJumpImpulse * (1.0f + 0.35f*Factor);
		pTuningParams->m_AirControlSpeed = pTuningParams->m_AirControlSpeed * (1.0f + 0.5f*Factor);
		pTuningParams->m_AirControlAccel = pTuningParams->m_AirControlAccel * (1.0f + 0.5f*Factor);
		pTuningParams->m_HookDragAccel = pTuningParams->m_HookDragAccel * (1.0f + 0.5f*Factor);
		pTuningParams->m_HookDragSpeed = pTuningParams->m_HookDragSpeed * (1.0f + 0.5f*Factor);
	}
}
