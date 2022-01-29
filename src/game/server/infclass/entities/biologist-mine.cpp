/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include "biologist-mine.h"
#include "biologist-laser.h"
#include "infccharacter.h"

CBiologistMine::CBiologistMine(CGameContext *pGameContext, vec2 Pos, vec2 EndPos, int Owner)
	: CPlacedObject(pGameContext, CGameWorld::ENTTYPE_BIOLOGIST_MINE, Pos, Owner)
{
	m_EndPos = EndPos;
	GameWorld()->InsertEntity(this);
	
	for(int i=0; i<NUM_IDS; i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}
}

CBiologistMine::~CBiologistMine()
{
	for(int i=0; i<NUM_IDS; i++)
	{
		Server()->SnapFreeID(m_IDs[i]);
	}
}

void CBiologistMine::Explode()
{
	float AngleStep = 2.0f * pi / 15.0f;
	float RandomShift = random_float() * 2.0f * pi;
	for(int i=0; i<15; i++)
	{
		new CBiologistLaser(GameServer(), m_Pos, vec2(cos(RandomShift + AngleStep*i), sin(RandomShift + AngleStep*i)), m_Owner, 10);
	}
	
	GameServer()->m_World.DestroyEntity(this);
}

void CBiologistMine::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;
	}

	float AngleStep = 2.0f * pi / CBiologistMine::NUM_SIDE;
	float Radius = 32.0f;
	for(int i=0; i<CBiologistMine::NUM_SIDE; i++)
	{
		vec2 VertexPos = m_Pos + vec2(Radius * cos(AngleStep*i), Radius * sin(AngleStep*i));
		
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[i], sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)VertexPos.x;
		pObj->m_Y = (int)VertexPos.y;
		pObj->m_FromX = (int)m_Pos.x;
		pObj->m_FromY = (int)m_Pos.y;
		pObj->m_StartTick = Server()->Tick()-4;
	}
	
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[CBiologistMine::NUM_SIDE], sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)m_EndPos.x;
		pObj->m_Y = (int)m_EndPos.y;
		pObj->m_FromX = (int)m_Pos.x;
		pObj->m_FromY = (int)m_Pos.y;
		pObj->m_StartTick = Server()->Tick()-4;
	}
}

void CBiologistMine::Tick()
{
	if(m_MarkedForDestroy) return;
	
	
	// Find other players
	for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
	{
		if(p->IsHuman()) continue;
		if(!p->CanDie()) continue;

		vec2 IntersectPos;
		if(!closest_point_on_line(m_Pos, m_EndPos, p->m_Pos, IntersectPos))
			continue;

		float Len = distance(p->m_Pos, IntersectPos);
		if(Len < p->m_ProximityRadius)
		{
			Explode();
			break;
		}
	}
}
