/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/vmath.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>
#include <game/server/infclass/classes/infected/infected.h>
#include <game/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

#include "engineer-wall.h"
#include "game/server/infclass/entities/infcentity.h"
#include "infccharacter.h"

static const float g_BarrierMaxLength = 300.0;
static const float g_BarrierRadius = 0.0;

int CEngineerWall::EntityId = CGameWorld::ENTTYPE_ENGINEER_WALL;

CEngineerWall::CEngineerWall(CGameContext *pGameContext, vec2 Pos1, int Owner)
	: CPlacedObject(pGameContext, EntityId, Pos1, Owner)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_LASER_WALL;
	m_EndPointId = Server()->SnapNewId();
	m_Pos2 = Pos1;

	GameWorld()->InsertEntity(this);
}

CEngineerWall::~CEngineerWall()
{
	Server()->SnapFreeId(m_EndPointId);
}

void CEngineerWall::SetEndPosition(vec2 EndPosition)
{
	if(distance(m_Pos, EndPosition) > g_BarrierMaxLength)
	{
		m_Pos2 = m_Pos + normalize(EndPosition - m_Pos) * g_BarrierMaxLength;
	}
	else
	{
		m_Pos2 = EndPosition;
	}

	m_InfClassObjectFlags = INFCLASS_OBJECT_FLAG_HAS_SECOND_POSITION;

	m_EndTick = Server()->Tick() + Server()->TickSpeed() * Config()->m_InfBarrierLifeSpan;
}

void CEngineerWall::Tick()
{
	if(IsMarkedForDestroy())
		return;

	if(!HasSecondPosition())
	{
		m_SnapStartTick = Server()->Tick();
		return;
	}

	if (m_WallFlashTicks > 0) 
		m_WallFlashTicks--;

	if(Server()->Tick() >= m_EndTick)
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
				OnHitInfected(p);
			}
		}
	}

	PrepareSnapData();
}

void CEngineerWall::TickPaused()
{
	++m_SnapStartTick;
	++m_EndTick;
}

void CEngineerWall::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	if(!HasSecondPosition())
	{
		const CInfClassPlayer *pDestPlayer = GameController()->GetPlayer(SnappingClient);
		if(pDestPlayer && !pDestPlayer->IsHuman())
		{
			return;
		}
	}

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;

		if(HasSecondPosition())
		{
			pInfClassObject->m_EndTick = m_EndTick;
		}
		else
		{
			// Snap fake second position to fix OwnerIcon position
			pInfClassObject->m_EndTick = -1;
			pInfClassObject->m_Flags |= INFCLASS_OBJECT_FLAG_HAS_SECOND_POSITION;
			pInfClassObject->m_X2 = pInfClassObject->m_X;
			pInfClassObject->m_Y2 = pInfClassObject->m_Y - 1;
		}
	}

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	CSnapContext Context(SnappingClientVersion);

	GameServer()->SnapLaserObject(Context, GetId(), m_Pos, m_Pos2, m_SnapStartTick, m_Owner);

	if(HasSecondPosition())
	{
		GameServer()->SnapLaserObject(Context, m_EndPointId, m_Pos2, m_Pos2, Server()->Tick(), m_Owner);
	}
}

void CEngineerWall::OnHitInfected(CInfClassCharacter *pCharacter)
{
	CInfClassInfected *pInfected = CInfClassInfected::GetInstance(pCharacter);

	if(pCharacter->GetPlayer() && pInfected)
	{
		pInfected->OnLaserWall();

		if(!pCharacter->CanDie())
		{
			return;
		}

		int LifeSpanReducer = ((Server()->TickSpeed()*Config()->m_InfBarrierTimeReduce)/100);
		m_WallFlashTicks = 10;

		if(pCharacter->GetPlayerClass() == EPlayerClass::Ghoul)
		{
			float Factor = pInfected->GetGhoulPercent();
			LifeSpanReducer += Server()->TickSpeed() * 5.0f * Factor;
		}

		m_EndTick -= LifeSpanReducer;
	}

	pCharacter->Die(m_Owner, EDamageType::LASER_WALL);
}

void CEngineerWall::PrepareSnapData()
{
	const int RemainingTicks = m_EndTick - Server()->Tick();

	// Laser dieing animation
	int LifeDiff = 0;
	if(m_WallFlashTicks > 0) // flash laser for a few ticks when zombie jumps
		LifeDiff = 5;
	else if(RemainingTicks < 1 * Server()->TickSpeed())
		LifeDiff = random_int(4, 5);
	else if(RemainingTicks < 2 * Server()->TickSpeed())
		LifeDiff = random_int(3, 5);
	else if(RemainingTicks < 3 * Server()->TickSpeed())
		LifeDiff = random_int(2, 4);
	else if(RemainingTicks < 4 * Server()->TickSpeed())
		LifeDiff = random_int(1, 3);
	else if(RemainingTicks < 5 * Server()->TickSpeed())
		LifeDiff = random_int(0, 2);
	else if(RemainingTicks < 6 * Server()->TickSpeed())
		LifeDiff = random_int(0, 1);
	else if(RemainingTicks < 7 * Server()->TickSpeed())
		LifeDiff = (random_prob(3.0f / 4.0f)) ? 1 : 0;
	else if(RemainingTicks < 8 * Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f / 6.0f)) ? 1 : 0;
	else if(RemainingTicks < 9 * Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f / 6.0f)) ? 0 : -1;
	else if(RemainingTicks < 10 * Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f / 6.0f)) ? 0 : -1;
	else if(RemainingTicks < 11 * Server()->TickSpeed())
		LifeDiff = (random_prob(5.0f / 6.0f)) ? -1 : -Server()->TickSpeed() * 2;
	else
		LifeDiff = -Server()->TickSpeed() * 2;

	m_SnapStartTick = Server()->Tick() - LifeDiff;
}
