/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/vmath.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>
#include <game/server/infclass/classes/infected/infected.h>
#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

#include "engineer-wall.h"
#include "game/server/infclass/entities/infcentity.h"
#include "infccharacter.h"

const float g_BarrierMaxLength = 300.0;
const float g_BarrierRadius = 0.0;

int CEngineerWall::EntityId = CGameWorld::ENTTYPE_ENGINEER_WALL;

CEngineerWall::CEngineerWall(CGameContext *pGameContext, vec2 Pos1, vec2 Pos2, int Owner)
	: CPlacedObject(pGameContext, EntityId, Pos1, Owner)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_LASER_WALL;
	m_InfClassObjectFlags = INFCLASS_OBJECT_FLAG_HAS_SECOND_POSITION;

	if(distance(Pos1, Pos2) > g_BarrierMaxLength)
	{
		m_Pos2 = Pos1 + normalize(Pos2 - Pos1)*g_BarrierMaxLength;
	}
	else
	{
		m_Pos2 = Pos2;
	}
	m_LifeSpan = Server()->TickSpeed()*Config()->m_InfBarrierLifeSpan;
	GameWorld()->InsertEntity(this);
	m_EndPointID = Server()->SnapNewID();
	m_WallFlashTicks = 0;
}

CEngineerWall::~CEngineerWall()
{
	Server()->SnapFreeID(m_EndPointID);
}

void CEngineerWall::Tick()
{
	if(m_MarkedForDestroy) return;

	if (m_WallFlashTicks > 0) 
		m_WallFlashTicks--;

	m_LifeSpan--;
	
	if(m_LifeSpan < 0)
	{
		GameWorld()->DestroyEntity(this);
	}
	else
	{
		// Find other players
		for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
		{
			if(p->IsHuman())
				continue;

			vec2 IntersectPos;
			if(!closest_point_on_line(m_Pos, m_Pos2, p->m_Pos, IntersectPos))
				continue;

			float Len = distance(p->m_Pos, IntersectPos);
			if(Len < p->m_ProximityRadius+g_BarrierRadius)
			{
				OnZombieHit(p);
			}
		}
	}
}

void CEngineerWall::TickPaused()
{
	//~ ++m_EvalTick;
}

void CEngineerWall::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;

		pInfClassObject->m_LifeSpan = m_LifeSpan;
	}

	const CInfClassPlayer *pPlayer = GameController()->GetPlayer(SnappingClient);
	const bool AntiPing = pPlayer && pPlayer->GetAntiPingEnabled();

	// Laser dieing animation
	int LifeDiff = 0;
	if (m_WallFlashTicks > 0) // flash laser for a few ticks when zombie jumps
		LifeDiff = 5;
	else if (m_LifeSpan < 1*Server()->TickSpeed())
		LifeDiff = random_int(4, 5);
	else if (m_LifeSpan < 2*Server()->TickSpeed())
		LifeDiff = random_int(3, 5);
	else if (m_LifeSpan < 3*Server()->TickSpeed())
		LifeDiff = random_int(2, 4);
	else if (m_LifeSpan < 4*Server()->TickSpeed())
		LifeDiff = random_int(1, 3);
	else if (m_LifeSpan < 5*Server()->TickSpeed())
		LifeDiff = random_int(0, 2);
	else if (m_LifeSpan < 6*Server()->TickSpeed())
		LifeDiff = random_int(0, 1);
	else if (m_LifeSpan < 7*Server()->TickSpeed())
		LifeDiff = (random_prob(3.0f/4.0f)) ? 1 : 0;
	else if (m_LifeSpan < 8*Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f/6.0f)) ? 1 : 0;
	else if (m_LifeSpan < 9*Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f/6.0f)) ? 0 : -1;
	else if (m_LifeSpan < 10*Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f/6.0f)) ? 0 : -1;
	else if (m_LifeSpan < 11*Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f/6.0f)) ? -1 : -Server()->TickSpeed()*2;
	else
		LifeDiff = -Server()->TickSpeed()*2;

	SSnapContext Context;
	Context.Version = GameServer()->GetClientVersion(SnappingClient);

	GameController()->SnapLaserObject(Context, GetID(), m_Pos, m_Pos2, Server()->Tick() - LifeDiff, m_Owner);

	if(!AntiPing)
	{
		vec2 Pos = m_Pos2;
		GameController()->SnapLaserObject(Context, m_EndPointID, Pos, Pos, Server()->Tick(), m_Owner);
	}
}

void CEngineerWall::OnZombieHit(CInfClassCharacter *pZombie)
{
	CInfClassInfected *pInfected = CInfClassInfected::GetInstance(pZombie);

	if(pZombie->GetPlayer() && pInfected)
	{
		pInfected->OnLaserWall();

		if(!pZombie->CanDie())
		{
			return;
		}

		int LifeSpanReducer = ((Server()->TickSpeed()*Config()->m_InfBarrierTimeReduce)/100);
		m_WallFlashTicks = 10;

		if(pZombie->GetPlayerClass() == PLAYERCLASS_GHOUL)
		{
			float Factor = pInfected->GetGhoulPercent();
			LifeSpanReducer += Server()->TickSpeed() * 5.0f * Factor;
		}

		m_LifeSpan -= LifeSpanReducer;
	}

	pZombie->Die(m_Owner, DAMAGE_TYPE::LASER_WALL);
}
