// Strongly modified version of ddnet Plasma. Source: Shereef Marzouk
#include "plasma.h"

#include <engine/server.h>
#include <engine/config.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>

CPlasma::CPlasma(CGameContext *pGameContext, vec2 Pos, int Owner, int TrackedPlayer, vec2 Direction, bool Freeze, bool Explosive)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_PLASMA, Pos, Owner)
{
	m_Freeze = Freeze;
	m_DamageType = DAMAGE_TYPE::NO_DAMAGE;
	m_TrackedPlayer = TrackedPlayer;
	m_Dir = Direction;
	m_Explosive = Explosive;
	m_StartTick = Server()->Tick();
	m_LifeSpan = Server()->TickSpeed()*Config()->m_InfTurretPlasmaLifeSpan;
	m_InitialAmount = 1.0f;
	GameWorld()->InsertEntity(this);
}

void CPlasma::Tick()
{
	//reduce lifespan
	if (m_LifeSpan < 0)
	{
		Reset();
		return;
	}
	m_LifeSpan--;
	
	// tracking, position and collision calculation
	CInfClassCharacter *pTarget = GameController()->GetCharacter(m_TrackedPlayer);
	if(pTarget)
	{
		float Dist = distance(GetPos(), pTarget->GetPos());
		if(Dist < 24.0f)
		{
			//freeze or explode
			if (m_Freeze) 
			{
				pTarget->Freeze(3.0f, m_Owner, FREEZEREASON_FLASH);
			}
			
			Explode();
		}
		else
		{
			m_Dir = normalize(pTarget->GetPos() - GetPos());
			m_Speed = clamp(Dist, 0.0f, 16.0f) * (1.0f - m_InitialAmount);
			m_Pos += m_Dir*m_Speed;
			
			m_InitialAmount *= 0.98f;
			
			//collision detection
			if(GameServer()->Collision()->CheckPoint(m_Pos.x, m_Pos.y)) // this only works as long as the projectile is not moving too fast
			{
				Explode();
			}
		}
	} 
	else //Target died before impact -> explode
	{
		Explode();
	}
	
}

void CPlasma::SetDamageType(DAMAGE_TYPE Type)
{
	m_DamageType = Type;
}

void CPlasma::Explode() 
{
	//GameServer()->CreateSound(CurPos, m_SoundImpact);
	if (m_Explosive) 
	{
		GameController()->CreateExplosion(m_Pos, m_Owner, m_DamageType, Config()->m_InfTurretDmgFactor*0.1f);
	}
	Reset();
}

void CPlasma::Snap(int SnappingClient)
{
	if (NetworkClipped(SnappingClient))
		return;
	
	
	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(
		NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
	
	if(!pObj)
		return;
	
	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_Pos.x;
	pObj->m_FromY = (int)m_Pos.y;
	pObj->m_StartTick = m_StartTick;
}
