/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "scatter-grenade.h"

#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/infclass/damage_type.h>

#include "growingexplosion.h"

int CScatterGrenade::EntityId = CGameWorld::ENTTYPE_SCATTER_GRENADE;

CScatterGrenade::CScatterGrenade(CGameContext *pGameContext, int Owner, vec2 Pos, vec2 Dir) :
	CInfCEntity(pGameContext, CGameWorld::ENTTYPE_SCATTER_GRENADE, Pos, Owner)
{
	m_ActualPos = Pos;
	m_ActualDir = Dir;
	m_Direction = Dir;
	m_StartTick = Server()->Tick();
	m_IsFlashGrenade = false;

	GameWorld()->InsertEntity(this);
}

vec2 CScatterGrenade::GetPos(float Time)
{
	float Curvature = GameServer()->Tuning()->m_GrenadeCurvature;
	float Speed = GameServer()->Tuning()->m_GrenadeSpeed;

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}

void CScatterGrenade::TickPaused()
{
	m_StartTick++;
}

void CScatterGrenade::Tick()
{
	float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	
	m_ActualPos = CurPos;
	m_ActualDir = normalize(CurPos - PrevPos);

	if(m_ExplodeOnContact)
	{
		CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
		CCharacter *TargetChr = GameServer()->m_World.IntersectCharacter(PrevPos, CurPos, 6.0f, CurPos, GetExceptEntitiesFilterFunction({OwnerChar}), m_Owner);
		
		if(TargetChr)
		{
			Explode();
		}
	}

	if(GameLayerClipped(CurPos))
	{
		GameWorld()->DestroyEntity(this);
		return;
	}
	
	vec2 LastPos;
	int Collide = GameServer()->Collision()->IntersectLineWeapon(PrevPos, CurPos, NULL, &LastPos);
	if(Collide)
	{
		
		if(m_IsFlashGrenade) {
			Explode();
		}
		
		//Thanks to TeeBall 0.6
		vec2 CollisionPos;
		CollisionPos.x = LastPos.x;
		CollisionPos.y = CurPos.y;
		int CollideY = GameServer()->Collision()->IntersectLineWeapon(PrevPos, CollisionPos, NULL, NULL);
		CollisionPos.x = CurPos.x;
		CollisionPos.y = LastPos.y;
		int CollideX = GameServer()->Collision()->IntersectLineWeapon(PrevPos, CollisionPos, NULL, NULL);
		
		m_Pos = LastPos;
		m_ActualPos = m_Pos;
		vec2 vel;
		vel.x = m_Direction.x;
		vel.y = m_Direction.y + 2*GameServer()->Tuning()->m_GrenadeCurvature/10000*Ct*GameServer()->Tuning()->m_GrenadeSpeed;
		
		if (CollideX && !CollideY)
		{
			m_Direction.x = -vel.x;
			m_Direction.y = vel.y;
		}
		else if (!CollideX && CollideY)
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
	}
}

void CScatterGrenade::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	pProj->m_VelX = (int)(m_Direction.x*100.0f);
	pProj->m_VelY = (int)(m_Direction.y*100.0f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = WEAPON_GRENADE;
}

void CScatterGrenade::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	
	if(NetworkClipped(SnappingClient, GetPos(Ct)))
		return;

	CNetObj_Projectile *pProj = Server()->SnapNewItem<CNetObj_Projectile>(GetId());
	if(pProj)
		FillInfo(pProj);
}
	
void CScatterGrenade::Explode()
{
	if(m_IsFlashGrenade)
	{
		new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, EDamageType::STUNNING_GRENADE);
	}
	else
	{
		new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, EDamageType::MERCENARY_GRENADE);
	}
	
	GameWorld()->DestroyEntity(this);
	
}

void CScatterGrenade::FlashGrenade()
{
	m_IsFlashGrenade = true;
	m_ExplodeOnContact = true;
}

void CScatterGrenade::ExplodeOnContact()
{
	m_ExplodeOnContact = true;
}
