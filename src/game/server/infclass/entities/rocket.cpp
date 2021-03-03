/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "rocket.h"

#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include "growingexplosion.h"
#include "portal.h"

CRocket::CRocket(CGameContext *pGameContext, int Owner, vec2 Pos, vec2 Dir)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_PROJECTILE, Pos, Owner, g_Config.m_InfRocketRadius)
{
	m_Direction = Dir;
	m_LifeSpan = Server()->TickSpeed() * 2.5f;
	m_StartTick = Server()->Tick();
	m_Explosive = true;
	m_StartPos = Pos;

	GameWorld()->InsertEntity(this);
}

void CRocket::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CRocket::ProcessExplosion()
{
	vec2 Dir = normalize(m_StartPos - GetPos());

	// Do visuals:
	int ExplosionRadius = 4;
	new CGrowingExplosion(GameServer(), GetPos(), Dir, m_Owner, ExplosionRadius, GROWINGEXPLOSIONEFFECT_HIT_EFFECT);

	// deal damage
	CCharacter *apEnts[MAX_CLIENTS];
	float Radius = 250.0f;
	float InnerRadius = 120.0f;
	int Num = GameWorld()->FindEntities(GetPos(), Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; i++)
	{
		CCharacter *pCharacter = apEnts[i];
		if(!g_Config.m_InfShockwaveAffectHumans){
			if(pCharacter->GetPlayer() && pCharacter->GetPlayer()->GetCID() == GetOwner())
			{
				//owner selfharm
			}
			else if(pCharacter->IsHuman())
			{
				continue;// humans are not affected by force
			}
		}
		vec2 Diff = apEnts[i]->m_Pos - GetPos();
		vec2 ForceDir(0,1);
		float l = length(Diff);
		if(l)
			ForceDir = normalize(Diff);
		l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
		float Dmg = g_Config.m_InfRocketDamage * l;
		float ForceValue = g_Config.m_InfRocketForce * l;
		if(pCharacter->IsHuman())
		{
			Dmg = 0;
		}

		apEnts[i]->TakeDamage(ForceDir * ForceValue, Dmg, GetOwner(), WEAPON_GRENADE, TAKEDAMAGEMODE_NOINFECTION);
	}

	GameServer()->m_World.DestroyEntity(this);
}

vec2 CRocket::GetPosAtTime(float Time)
{
	float Curvature = 0;
	float Speed = 0;

	Curvature = 0;
	Speed = g_Config.m_InfRocketSpeed;

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}

void CRocket::Tick()
{
	float Pt = (Server()->Tick()-m_StartTick-1)/(float)Server()->TickSpeed();
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	vec2 PrevPos = GetPosAtTime(Pt);
	vec2 CurPos = GetPosAtTime(Ct);
	int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, &CurPos, 0);
	const float ProjectileRadius = GetProximityRadius();
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *TargetChr = GameServer()->m_World.IntersectCharacter(PrevPos, CurPos, ProjectileRadius, CurPos, OwnerChar);
	vec2 WitchPortalAt;
	CEntity *TargetWitchPortal = GameServer()->m_World.IntersectEntity(PrevPos, CurPos, ProjectileRadius, &WitchPortalAt, CGameWorld::ENTTYPE_PORTAL);
	if (TargetChr && TargetWitchPortal)
	{
		if (distance(PrevPos, TargetWitchPortal->m_Pos) < distance(PrevPos, TargetChr->m_Pos))
		{
			TargetChr = nullptr;
		}
		else
		{
			TargetWitchPortal = nullptr;
		}
	}

	m_Pos = CurPos;

	m_LifeSpan--;

	if(TargetWitchPortal || TargetChr || Collide || m_LifeSpan < 0 || GameLayerClipped(CurPos))
	{
		if(m_LifeSpan >= 0)
			GameServer()->CreateSound(CurPos, SOUND_GRENADE_EXPLODE);
		
		ProcessExplosion();
	}
}

void CRocket::TickPaused()
{
	++m_StartTick;
}

void CRocket::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = (int)m_Pos.x;
	pProj->m_Y = (int)m_Pos.y;
	pProj->m_VelX = (m_Direction.x*20.0f);
	pProj->m_VelY = (m_Direction.y*20.0f);
	pProj->m_StartTick = Server()->Tick();
	pProj->m_Type = WEAPON_GRENADE;
}

void CRocket::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();

	if(NetworkClipped(SnappingClient, GetPosAtTime(Ct)))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
	if(pProj)
		FillInfo(pProj);
}
