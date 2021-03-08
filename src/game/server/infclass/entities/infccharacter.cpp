#include "infccharacter.h"

#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>

#include <game/server/entities/projectile.h>
#include <game/server/entities/laser.h>

#include <game/server/infclass/entities/biologist-mine.h>
#include <game/server/infclass/entities/bouncing-bullet.h>
#include <game/server/infclass/entities/engineer-wall.h>
#include <game/server/infclass/entities/growingexplosion.h>
#include <game/server/infclass/entities/hero-flag.h>
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
		m_pClass->Tick();
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

	if(m_ActiveWeapon == WEAPON_GUN || m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
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

	if (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_WITCH_PORTAL_RIFLE && FindPortalInTarget())
	{
		// Give the ammo in advance for portal taking
		GiveWeapon(m_ActiveWeapon, m_aWeapons[m_ActiveWeapon].m_Ammo + 1);
	}
	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo && (GetInfWeaponID(m_ActiveWeapon) != INFWEAPON_MERCENARY_GRENADE)
										  && (GetInfWeaponID(m_ActiveWeapon) != INFWEAPON_MEDIC_GRENADE))
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound+Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}
	if(GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_BIOLOGIST_RIFLE && m_aWeapons[m_ActiveWeapon].m_Ammo < 10)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound+Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}

	bool FireAccepted = true;
	OnWeaponFired(m_ActiveWeapon, &FireAccepted);

	if(!FireAccepted)
	{
		return;
	}

	m_AttackTick = Server()->Tick();

	if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	if(!m_ReloadTimer)
	{
		m_ReloadTimer = Server()->GetFireDelay(GetInfWeaponID(m_ActiveWeapon)) * Server()->TickSpeed() / 1000;
	}
}

void CInfClassCharacter::OnWeaponFired(int Weapon, bool *pFireAccepted)
{
	switch(Weapon)
	{
		case WEAPON_HAMMER:
			OnHammerFired(pFireAccepted);
			break;
		case WEAPON_GUN:
			OnGunFired(pFireAccepted);
			break;
		case WEAPON_SHOTGUN:
			OnShotgunFired(pFireAccepted);
			break;
		case WEAPON_GRENADE:
			OnGrenadeFired(pFireAccepted);
			break;
		case WEAPON_RIFLE:
			OnLaserFired(pFireAccepted);
			break;
		case WEAPON_NINJA:
			OnNinjaFired(pFireAccepted);
			break;
		default:
			break;
	}
}

void CInfClassCharacter::OnHammerFired(bool *pFireAccepted)
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
	else if(FullAuto && (m_LatestInput.m_Fire&1) && (m_aWeapons[m_ActiveWeapon].m_Ammo))
	{
		AutoFire = true;
	}

	if(GetPlayerClass() == PLAYERCLASS_ENGINEER)
	{
		for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
				GameServer()->m_World.DestroyEntity(pWall);
		}

		if(m_FirstShot)
		{
			m_FirstShot = false;
			m_FirstShotCoord = m_Pos;
		}
		else if(distance(m_FirstShotCoord, m_Pos) > 10.0)
		{
			//Check if the barrier is in toxic gases
			bool isAccepted = true;
			for(int i=0; i<15; i++)
			{
				vec2 TestPos = m_FirstShotCoord + (m_Pos - m_FirstShotCoord)*(static_cast<float>(i)/14.0f);
				if(GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Damage, TestPos) == ZONE_DAMAGE_INFECTION)
				{
					isAccepted = false;
				}
			}

			if(isAccepted)
			{
				m_FirstShot = true;
				new CEngineerWall(GameServer(), m_FirstShotCoord, m_Pos, m_pPlayer->GetCID());
				GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_LOOPER)
	{
		//Potential variable name conflicts with engineers wall (for example *pWall is used twice for both Looper and Engineer)
		for(CLooperWall *pWall = (CLooperWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
				GameServer()->m_World.DestroyEntity(pWall);
		}

		if(m_FirstShot)
		{
			m_FirstShot = false;
			m_FirstShotCoord = m_Pos;
		}
		else if(distance(m_FirstShotCoord, m_Pos) > 10.0)
		{
			//Check if the barrier is in toxic gases
			bool isAccepted = true;
			for(int i=0; i<15; i++)
			{
				vec2 TestPos = m_FirstShotCoord + (m_Pos - m_FirstShotCoord)*(static_cast<float>(i)/14.0f);
				if(GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_Damage, TestPos) == ZONE_DAMAGE_INFECTION)
				{
					isAccepted = false;
				}
			}

			if(isAccepted)
			{
				m_FirstShot = true;
				
				new CLooperWall(GameServer(), m_FirstShotCoord, m_Pos, m_pPlayer->GetCID());
				
				GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		if(g_Config.m_InfTurretEnable && m_TurretCount)
		{
			if (g_Config.m_InfTurretEnableLaser)
			{
				new CTurret(GameServer(), m_Pos, m_pPlayer->GetCID(), Direction, CTurret::LASER);
			}
			else if (g_Config.m_InfTurretEnablePlasma)
			{
				new CTurret(GameServer(), m_Pos, m_pPlayer->GetCID(), Direction, CTurret::PLASMA);
			}

			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			m_TurretCount--;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "placed turret, %i left", m_TurretCount);
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, aBuf, NULL);
		}
		else
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SOLDIER)
	{
		FireSoldierBomb();
	}
	else if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		if(m_Pos.y > -600.0)
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
			if(pCurrentBomb->ReadyToExplode() || distance(pCurrentBomb->m_Pos, m_Pos) > 80.0f)
				pCurrentBomb->Explode();
			else
			{
				pCurrentBomb->IncreaseDamage(WEAPON_HAMMER);
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
			}
		}
		else
		{
			new CMercenaryBomb(GameServer(), m_Pos, m_pPlayer->GetCID());
			GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
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
			float d = distance(p->m_Pos, ProjStartPos);
			
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
				GameServer()->m_World.DestroyEntity(pIntersectMine);
			else if(NbMine >= g_Config.m_InfMineLimit && pOlderMine)
				GameServer()->m_World.DestroyEntity(pOlderMine);
			
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

			GameServer()->CreateSound(m_Pos, SOUND_NINJA_HIT);
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
			int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];

				if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
					continue;

				vec2 Dir;
				if (length(pTarget->m_Pos - m_Pos) > 0.0f)
					Dir = normalize(pTarget->m_Pos - m_Pos);
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
				if(length(pTarget->m_Pos-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*m_ProximityRadius*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);
			}

			for(CPortal* pPortal = (CPortal*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_PORTAL); pPortal; pPortal = (CPortal*) pPortal->TypeNext())
			{
				if(m_pPlayer->IsZombie())
					continue;

				if(pPortal->GetOwner() == m_pPlayer->GetCID())
					continue;

				if(distance(m_Pos, pPortal->m_Pos) > (pPortal->m_ProximityRadius + m_ProximityRadius*0.5f))
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
			vec2 CheckPos = m_Pos + Direction * 64.0f;
			if(GameServer()->Collision()->IntersectLine(m_Pos, CheckPos, 0x0, &CheckPos))
			{
				static const float MinDistance = 84.0f;
				float DistanceToTheNearestSlime = MinDistance * 2;
				for(CSlugSlime* pSlime = (CSlugSlime*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SLUG_SLIME); pSlime; pSlime = (CSlugSlime*) pSlime->TypeNext())
				{
					const float d = distance(pSlime->m_Pos, m_Pos);
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
			*pFireAccepted = false;
			return;
		}

		GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);
	}
}

void CInfClassCharacter::OnGunFired(bool *pFireAccepted)
{
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

		GameServer()->CreateSound(m_Pos, SOUND_HOOK_LOOP);
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

		GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
	}
}

void CInfClassCharacter::OnShotgunFired(bool *pFireAccepted)
{
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
		a += Spreading[i+3] * 2.0f*(0.25f + 0.75f*static_cast<float>(10-m_aWeapons[WEAPON_SHOTGUN].m_Ammo)/10.0f);
		float v = 1-(absolute(i)/(float)ShotSpread);
		float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);

		float LifeTime = GameServer()->Tuning()->m_ShotgunLifetime + 0.1f*static_cast<float>(m_aWeapons[WEAPON_SHOTGUN].m_Ammo)/10.0f;

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

	GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
}

void CInfClassCharacter::OnGrenadeFired(bool *pFireAccepted)
{
	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;
	
	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		//Find bomb
		bool BombFound = false;
		for(CScatterGrenade *pGrenade = (CScatterGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCATTER_GRENADE); pGrenade; pGrenade = (CScatterGrenade*) pGrenade->TypeNext())
		{
			if(pGrenade->GetOwner() != m_pPlayer->GetCID()) continue;
			pGrenade->Explode();
			BombFound = true;
		}

		if(!BombFound && m_aWeapons[m_ActiveWeapon].m_Ammo)
		{
			int ShotSpread = 2;

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float a = GetAngle(Direction) + random_float()/3.0f;

				CScatterGrenade *pProj = new CScatterGrenade(GameServer(), m_pPlayer->GetCID(), m_Pos, vec2(cosf(a), sinf(a)));

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);

				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
				Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
			}

			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);

			m_ReloadTimer = Server()->TickSpeed()/4;
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_Ammo++;
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_MEDIC)
	{
		//Find bomb
		bool BombFound = false;
		for(CMedicGrenade *pGrenade = (CMedicGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MEDIC_GRENADE); pGrenade; pGrenade = (CMedicGrenade*) pGrenade->TypeNext())
		{
			if(pGrenade->GetOwner() != m_pPlayer->GetCID()) continue;
			pGrenade->Explode();
			BombFound = true;
		}

		if(!BombFound && m_aWeapons[m_ActiveWeapon].m_Ammo)
		{
			int ShotSpread = 0;

			CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
			Msg.AddInt(ShotSpread*2+1);

			for(int i = -ShotSpread; i <= ShotSpread; ++i)
			{
				float a = GetAngle(Direction) + random_float()/5.0f;

				CMedicGrenade *pProj = new CMedicGrenade(GameServer(), m_pPlayer->GetCID(), m_Pos, vec2(cosf(a), sinf(a)));

				// pack the Projectile and send it to the client Directly
				CNetObj_Projectile p;
				pProj->FillInfo(&p);
				
				for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
					Msg.AddInt(((int *)&p)[i]);
				Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
			}
			
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			
			m_ReloadTimer = Server()->TickSpeed()/4;
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_Ammo++;
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		vec2 PortalShift = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
		vec2 PortalDir = normalize(PortalShift);
		if(length(PortalShift) > 500.0f)
			PortalShift = PortalDir * 500.0f;
		vec2 PortalPos;

		if(FindPortalPosition(m_Pos + PortalShift, PortalPos))
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
				
				CScatterGrenade *pProj = new CScatterGrenade(GameServer(), m_pPlayer->GetCID(), m_Pos, vec2(cosf(a), sinf(a)));
				
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
			
			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
			
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

			GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
		}
	}
}

void CInfClassCharacter::OnLaserFired(bool *pFireAccepted)
{
	vec2 Direction = GetDirection();

	if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
	{
		for(CBiologistMine* pMine = (CBiologistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*) pMine->TypeNext())
		{
			if(pMine->GetOwner() != m_pPlayer->GetCID()) continue;
				GameServer()->m_World.DestroyEntity(pMine);
		}

		vec2 To = m_Pos + Direction*400.0f;
		if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
		{
			new CBiologistMine(GameServer(), m_Pos, To, m_pPlayer->GetCID());
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
			m_aWeapons[WEAPON_RIFLE].m_Ammo = 0;
		}
		else
		{
			*pFireAccepted = false;
			return;
		}
	}
	else if(CanOpenPortals())
	{
		if(!IsFrozen() && !IsInLove())
		{
			PlacePortal();
			m_ReloadTimer = Server()->TickSpeed() / 4;
		}
	}
	else
	{
		int Damage = GameServer()->Tuning()->m_LaserDamage;
		
		if(GetPlayerClass() == PLAYERCLASS_SNIPER)
		{
			if(m_PositionLocked)
				Damage = 30;
			else
				Damage = random_int(10, 13);
			new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
		}
		else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
		{
			//white hole activation in scientist-laser
			
			new CScientistLaser(GameServer(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach*0.6f, m_pPlayer->GetCID(), Damage);
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
		}
		else if (GetPlayerClass() == PLAYERCLASS_LOOPER) 
		{
			Damage = 5;
			new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach*0.7f, m_pPlayer->GetCID(), Damage);
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
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
				*pFireAccepted = false;
				return;
			}
			else
			{
				new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
				GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
				if(m_BombHit && distance(pCurrentBomb->m_Pos, m_AtMercBomb) <= 80.0f)
				{
					pCurrentBomb->IncreaseDamage(WEAPON_RIFLE);
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
				}
			}
		}
		else
		{
			new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
			GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
		}
	}
}

void CInfClassCharacter::OnNinjaFired(bool *pFireAccepted)
{
	// The design of ninja supposes different implementation (not via FireWeapon)
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

	CInfClassCharacter *pKillerCharacter = nullptr;
	if (Weapon == WEAPON_WORLD && Killer == m_pPlayer->GetCID()) {
		//Search for the real killer (if somebody hooked this player)
		for(CInfClassCharacter *pHooker = (CInfClassCharacter*) GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER); pHooker; pHooker = (CInfClassCharacter *)pHooker->TypeNext())
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
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());

/* INFECTION MODIFICATION START ***************************************/

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

	if(pKillerCharacter && (pKillerCharacter != this))
	{
		// set attacker's face to happy (taunt!)
		pKillerCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		pKillerCharacter->CheckSuperWeaponAccess();

		if(pKillerCharacter->GetPlayerClass() == PLAYERCLASS_MERCENARY)
		{
			pKillerCharacter->GiveWeapon(WEAPON_RIFLE, m_aWeapons[WEAPON_RIFLE].m_Ammo + 3);
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
						new CSuperWeaponIndicator(GameServer(), m_Pos, m_pPlayer->GetCID());
					}
				} 
			} 
		}
	}
	
	//Only looper and soldier can receive stun grenades
	if(GetPlayerClass() == PLAYERCLASS_LOOPER || GetPlayerClass() == PLAYERCLASS_SOLDIER)
	{
		if (!m_HasStunGrenade)
		{
			// enable white hole probabilities
			if (kills > g_Config.m_InfStunGrenadeMinimalKills) 
			{
				if (random_int(0,100) < g_Config.m_InfStunGrenadeProbability) 
				{
						//grenade launcher usage will make it unavailable and reset player kills
					
						m_HasStunGrenade = true;
						GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("stun grenades found..."), NULL);
				} 
			} 
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
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
}

void CInfClassCharacter::PlacePortal()
{
	vec2 TargetPos = m_Pos;

	if (GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		if(!FindWitchSpawnPosition(TargetPos))
		{
			// Witch can't place the portal here
			return;
		}
	}

	CPortal *PortalToTake = FindPortalInTarget();
	if (PortalToTake)
	{
		PortalToTake->Disconnect();
		GameServer()->m_World.DestroyEntity(PortalToTake);

		if (PortalToTake == m_pPortalIn)
			m_pPortalIn = nullptr;
		if (PortalToTake == m_pPortalOut)
			m_pPortalOut = nullptr;

		GiveWeapon(WEAPON_RIFLE, m_aWeapons[WEAPON_RIFLE].m_Ammo + 1);
		return;
	}

	// Place new portal
	int OwnerCID = GetPlayer() ? GetPlayer()->GetCID() : -1;
	CPortal *existingPortal = m_pPortalIn ? m_pPortalIn : m_pPortalOut;
	if(existingPortal && distance(existingPortal->m_Pos, TargetPos) < g_Config.m_InfMinPortalDistance)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Unable to place portals that close to each other");
		GameServer()->SendChatTarget(OwnerCID, aBuf);
		return;
	}

	if (TargetPos.y < -20)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Unable to open a portal at this height");
		GameServer()->SendChatTarget(OwnerCID, aBuf);
		return;
	}

	if(m_pPortalIn && m_pPortalOut)
	{
		m_pPortalOut->Disconnect();
		GameServer()->m_World.DestroyEntity(m_pPortalOut);
		m_pPortalOut = nullptr;
	}

	if (m_pPortalIn)
	{
		m_pPortalOut = new CPortal(GameServer(), TargetPos, OwnerCID, CPortal::PortalType::Out);
		m_pPortalOut->ConnectPortal(m_pPortalIn);
		GameServer()->CreateSound(m_pPortalOut->m_Pos, m_pPortalOut->GetNewEntitySound());
	}
	else
	{
		m_pPortalIn = new CPortal(GameServer(), TargetPos, OwnerCID, CPortal::PortalType::In);
		m_pPortalIn->ConnectPortal(m_pPortalOut);
		GameServer()->CreateSound(m_pPortalIn->m_Pos, m_pPortalIn->GetNewEntitySound());
	}
}

CPortal *CInfClassCharacter::FindPortalInTarget()
{
	vec2 TargetPos = m_Pos;

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
	if(m_pPortalIn && (distance(m_pPortalIn->m_Pos, TargetPos) < m_ProximityRadius + m_pPortalIn->GetRadius() + displacementExtraDistance))
	{
		return m_pPortalIn;
	}

	if(m_pPortalOut && (distance(m_pPortalOut->m_Pos, TargetPos) < m_ProximityRadius + m_pPortalOut->GetRadius() + displacementExtraDistance))
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
