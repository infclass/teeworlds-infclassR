/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>

#include "bouncing-bullet.h"
#include "infccharacter.h"

CBouncingBullet::CBouncingBullet(CGameContext *pGameContext, int Owner, vec2 Pos, vec2 Dir)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_BOUNCING_BULLET, Pos, Owner)
{
	m_ActualPos = Pos;
	m_ActualDir = Dir;
	m_Direction = Dir;
	m_StartTick = Server()->Tick();
	m_LifeSpan = Server()->TickSpeed()*2;
	m_BounceLeft = 3; // the number of time that a bullet can bounce. It's usefull to remove bullets laying on the ground
	m_DistanceLeft = 1200; // the max distance a bullet can travel
	
	GameWorld()->InsertEntity(this);
}

vec2 CBouncingBullet::GetPos(float Time)
{
	float Curvature = GameServer()->Tuning()->m_ShotgunCurvature;
	float Speed = GameServer()->Tuning()->m_ShotgunSpeed;

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}

void CBouncingBullet::TickPaused()
{
	m_StartTick++;
}

void CBouncingBullet::Tick()
{
	float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	
	m_ActualPos = CurPos;
	m_ActualDir = normalize(CurPos - PrevPos);

	m_DistanceLeft -= distance(CurPos, PrevPos);
	
	if(GameLayerClipped(CurPos) || m_LifeSpan < 0 || m_BounceLeft < 0 || m_DistanceLeft < 0.0f)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}
	
	m_LifeSpan--;

	vec2 NewPos;
	int Collide = GameServer()->Collision()->IntersectLineWeapon(PrevPos, CurPos, nullptr, &NewPos);

	const float ProjectileRadius = 6.0f;
	CInfClassCharacter *pTargetChr = CInfClassCharacter::GetInstance(GameServer()->m_World.IntersectCharacter(PrevPos, NewPos, ProjectileRadius, NewPos, nullptr, m_Owner));

	if(pTargetChr)
	{
		const float Damage = 1.33f;
		if(pTargetChr)
		{
			pTargetChr->TakeDamage(m_Direction * 2, Damage, m_Owner, EDamageType::BIOLOGIST_SHOTGUN);
		}

		GameWorld()->DestroyEntity(this);
		return;
	}

	if(Collide)
	{
		// Thanks to TeeBall 0.6
		vec2 CollisionPos;
		CollisionPos.x = NewPos.x;
		CollisionPos.y = CurPos.y;
		int CollideY = GameServer()->Collision()->IntersectLineWeapon(PrevPos, CollisionPos, NULL, NULL);
		CollisionPos.x = CurPos.x;
		CollisionPos.y = NewPos.y;
		int CollideX = GameServer()->Collision()->IntersectLineWeapon(PrevPos, CollisionPos, NULL, NULL);

		m_Pos = NewPos;
		m_ActualPos = m_Pos;
		vec2 vel;
		vel.x = m_Direction.x;
		vel.y = m_Direction.y + 2 * GameServer()->Tuning()->m_ShotgunCurvature / 10000 * Ct * GameServer()->Tuning()->m_ShotgunSpeed;

		if(CollideX && !CollideY)
		{
			m_Direction.x = -vel.x;
			m_Direction.y = vel.y;
		}
		else if(!CollideX && CollideY)
		{
			m_Direction.x = vel.x;
			m_Direction.y = -vel.y;
		}
		else
		{
			m_Direction.x = -vel.x;
			m_Direction.y = -vel.y;
		}

		m_Direction.x *= (100 - 50) / 100.0;
		m_Direction.y *= (100 - 50) / 100.0;
		m_StartTick = Server()->Tick();

		m_ActualDir = normalize(m_Direction);
		m_BounceLeft--;
	}
}

void CBouncingBullet::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	pProj->m_VelX = (int)(m_Direction.x*100.0f);
	pProj->m_VelY = (int)(m_Direction.y*100.0f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = WEAPON_SHOTGUN;
}

void CBouncingBullet::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();

	if(NetworkClipped(SnappingClient, GetPos(Ct)))
		return;

	CNetObj_Projectile *pProj = Server()->SnapNewItem<CNetObj_Projectile>(GetId());
	if(pProj)
		FillInfo(pProj);
}
