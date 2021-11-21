#include "fking-CK.h"

#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include "growingexplosion.h"

CFCK::CFCK(CGameContext *pGameContext, int Owner, vec2 Pos, vec2 Dir)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_FK_CK, Pos, Owner)
{
	m_ActualPos = Pos;
	m_ActualDir = Dir;
	m_Direction = Dir;
	m_StartTick = Server()->Tick();

	GameWorld()->InsertEntity(this);
}

vec2 CFCK::GetPos(float Time)
{
	float Curvature = GameServer()->Tuning()->m_GrenadeCurvature;
	float Speed = GameServer()->Tuning()->m_GrenadeSpeed;

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
    new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 1, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
}

void CFCK::TickPaused()
{
	m_StartTick++;
}

void CFCK::Tick()
{
    new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 0.9f, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
	float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	
	m_ActualPos = CurPos;
	m_ActualDir = normalize(CurPos - PrevPos);

	if(GameLayerClipped(CurPos))
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}
	
	vec2 LastPos;
	int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, NULL, &LastPos);
	if(Collide)
	{
        new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
		//Thanks to TeeBall 0.6
		vec2 CollisionPos;
		CollisionPos.x = LastPos.x;
		CollisionPos.y = CurPos.y;
		int CollideY = GameServer()->Collision()->IntersectLine(PrevPos, CollisionPos, NULL, NULL);
		CollisionPos.x = CurPos.x;
		CollisionPos.y = LastPos.y;
		int CollideX = GameServer()->Collision()->IntersectLine(PrevPos, CollisionPos, NULL, NULL);

		m_Pos = LastPos;
		m_ActualPos = m_Pos;
		vec2 vel;
		vel.x = m_Direction.x;
		vel.y = m_Direction.y + 2*GameServer()->Tuning()->m_GrenadeCurvature/10000*Ct*GameServer()->Tuning()->m_GrenadeSpeed;
		
		if (CollideX && !CollideY)
		{
			m_Direction.x = -vel.x;
			m_Direction.y = vel.y;
            new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
		}
		else if (!CollideX && CollideY)
		{
			m_Direction.x = vel.x;
			m_Direction.y = -vel.y;
            new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
		}
		else
		{
			m_Direction.x = -vel.x;
			m_Direction.y = -vel.y;
            new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
		}
		
		m_Direction.x *= (100 - 50) / 100.0;
		m_Direction.y *= (100 - 50) / 100.0;
		m_StartTick = Server()->Tick();
		
		m_ActualDir = normalize(m_Direction);
	}
	
}

void CFCK::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	pProj->m_VelX = (int)(m_Direction.x*100.0f);
	pProj->m_VelY = (int)(m_Direction.y*100.0f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = WEAPON_GRENADE;
}

void CFCK::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();

	if(NetworkClipped(SnappingClient, GetPos(Ct)))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
	if(pProj)
		FillInfo(pProj);
    new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 1, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
}
	
void CFCK::Explode()
{
	new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
    new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, GROWINGEXPLOSIONEFFECT_BOOM_INFECTED);
    new CGrowingExplosion(GameServer(), m_ActualPos, m_ActualDir, m_Owner, 4, GROWINGEXPLOSIONEFFECT_LOVE_INFECTED);
	GameServer()->m_World.DestroyEntity(this);
}
