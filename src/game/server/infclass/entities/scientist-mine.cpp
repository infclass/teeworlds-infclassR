/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>

#include "scientist-mine.h"

#include "infccharacter.h"
#include "growingexplosion.h"

CScientistMine::CScientistMine(CGameContext *pGameContext, vec2 Pos, int Owner)
	: CPlacedObject(pGameContext, CGameWorld::ENTTYPE_SCIENTIST_MINE, Pos, Owner)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_SCIENTIST_MINE;
	GameWorld()->InsertEntity(this);
	m_DetectionRadius = 60.0f;
	m_StartTick = Server()->Tick();
	
	for(int i=0; i<NUM_IDS; i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}
}

CScientistMine::~CScientistMine()
{
	for(int i=0; i<NUM_IDS; i++)
	{
		Server()->SnapFreeID(m_IDs[i]);
	}
}

void CScientistMine::Explode(int DetonatedBy)
{
	new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), m_Owner, 6, DAMAGE_TYPE::SCIENTIST_MINE);
	GameWorld()->DestroyEntity(this);
	
	//Self damage
	CInfClassCharacter *OwnerChar = GetOwnerCharacter();
	if(OwnerChar)
	{
		float Dist = distance(m_Pos, OwnerChar->GetPos());
		if(Dist < OwnerChar->GetProximityRadius()+Config()->m_InfMineRadius)
		{
			OwnerChar->TakeDamage(vec2(0.0f, 0.0f), 4, DetonatedBy, DAMAGE_TYPE::SCIENTIST_MINE);
		}
		else if(Dist < OwnerChar->GetProximityRadius()+2*Config()->m_InfMineRadius)
		{
			float Alpha = (Dist - Config()->m_InfMineRadius-OwnerChar->GetProximityRadius())/Config()->m_InfMineRadius;
			OwnerChar->TakeDamage(vec2(0.0f, 0.0f), 4*Alpha, DetonatedBy, DAMAGE_TYPE::SCIENTIST_MINE);
		}
	}
}

void CScientistMine::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	float Radius = Config()->m_InfMineRadius;

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;
	}

	int NumSide = CScientistMine::NUM_SIDE;
	if(Server()->GetClientAntiPing(SnappingClient))
		NumSide = std::min(6, NumSide);
	
	float AngleStep = 2.0f * pi / NumSide;
	
	for(int i=0; i<NumSide; i++)
	{
		vec2 PartPosStart = m_Pos + vec2(Radius * cos(AngleStep*i), Radius * sin(AngleStep*i));
		vec2 PartPosEnd = m_Pos + vec2(Radius * cos(AngleStep*(i+1)), Radius * sin(AngleStep*(i+1)));
		
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[i], sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)PartPosStart.x;
		pObj->m_Y = (int)PartPosStart.y;
		pObj->m_FromX = (int)PartPosEnd.x;
		pObj->m_FromY = (int)PartPosEnd.y;
		pObj->m_StartTick = Server()->Tick();
	}
	
	if(!Server()->GetClientAntiPing(SnappingClient))
	{
		for(int i=0; i<CScientistMine::NUM_PARTICLES; i++)
		{
			float RandomRadius = random_float()*(Radius-4.0f);
			float RandomAngle = 2.0f * pi * random_float();
			vec2 ParticlePos = m_Pos + vec2(RandomRadius * cos(RandomAngle), RandomRadius * sin(RandomAngle));
			GameController()->SendHammerDot(ParticlePos, m_IDs[CScientistMine::NUM_SIDE+i]);
		}
	}
}

void CScientistMine::Tick()
{
	if(m_MarkedForDestroy) return;

	// Find other players
	bool MustExplode = false;
	int DetonatedBy;
	for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
	{
		if(p->IsHuman()) continue;
		if(!p->CanDie()) continue;

		float Len = distance(p->GetPos(), m_Pos);
		if(Len < p->GetProximityRadius()+Config()->m_InfMineRadius)
		{
			MustExplode = true;
			DetonatedBy = p->GetCID();
			if(DetonatedBy < 0)
			{
				DetonatedBy = m_Owner;
			}
			break;
		}
	}
	
	if(MustExplode)
		Explode(DetonatedBy);
}

void CScientistMine::TickPaused()
{
	++m_StartTick;
}
