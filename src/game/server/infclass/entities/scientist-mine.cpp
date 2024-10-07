/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include <game/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

#include "scientist-mine.h"

#include "infccharacter.h"
#include "growingexplosion.h"

int CScientistMine::EntityId = CGameWorld::ENTTYPE_SCIENTIST_MINE;

CScientistMine::CScientistMine(CGameContext *pGameContext, vec2 Pos, int Owner) :
	CPlacedObject(pGameContext, EntityId, Pos, Owner, pGameContext->Config()->m_InfMineRadius)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_SCIENTIST_MINE;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();
	
	for(int i=0; i<NUM_IDS; i++)
	{
		m_Ids[i] = Server()->SnapNewId();
	}
}

CScientistMine::~CScientistMine()
{
	for(int i=0; i<NUM_IDS; i++)
	{
		Server()->SnapFreeId(m_Ids[i]);
	}
}

void CScientistMine::Explode(int DetonatedBy)
{
	new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), m_Owner, 6, EDamageType::SCIENTIST_MINE);
	GameWorld()->DestroyEntity(this);
	
	//Self damage
	CInfClassCharacter *OwnerChar = GetOwnerCharacter();
	if(OwnerChar)
	{
		constexpr int MaxSelfDamage = 4;
		float Distance = distance(m_Pos, OwnerChar->GetPos());
		if(Distance < OwnerChar->GetProximityRadius() + GetProximityRadius())
		{
			OwnerChar->TakeDamage(vec2(0.0f, 0.0f), MaxSelfDamage, DetonatedBy, EDamageType::SCIENTIST_MINE);
		}
		else if(Distance < OwnerChar->GetProximityRadius() + 2 * GetProximityRadius())
		{
			float Alpha = (Distance - GetProximityRadius() - OwnerChar->GetProximityRadius()) / GetProximityRadius();
			OwnerChar->TakeDamage(vec2(0.0f, 0.0f), MaxSelfDamage * Alpha, DetonatedBy, EDamageType::SCIENTIST_MINE);
		}
	}
}

void CScientistMine::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	float Radius = GetProximityRadius();

	int InfclassVersion = Server()->GetClientInfclassVersion(SnappingClient);
	if(InfclassVersion)
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;

		pInfClassObject->m_StartTick = m_StartTick;
		if(InfclassVersion >= VERSION_INFC_180)
		{
			pInfClassObject->m_Flags |= INFCLASS_OBJECT_FLAG_RELY_ON_CLIENTSIDE_RENDERING;
			return;
		}
	}

	const CInfClassPlayer *pPlayer = GameController()->GetPlayer(SnappingClient);
	const bool AntiPing = pPlayer && pPlayer->GetAntiPingEnabled();
	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	CSnapContext Context(SnappingClientVersion);

	int NumSide = CScientistMine::NUM_SIDE;
	if(AntiPing)
		NumSide = std::min(6, NumSide);
	
	float AngleStep = 2.0f * pi / NumSide;
	
	for(int i=0; i<NumSide; i++)
	{
		vec2 PartPosStart = m_Pos + direction(AngleStep * i) * Radius;
		vec2 PartPosEnd = m_Pos + direction(AngleStep * (i + 1)) * Radius;
		GameServer()->SnapLaserObject(Context, m_Ids[i], PartPosStart, PartPosEnd, Server()->Tick(), GetOwner());
	}

	if(!AntiPing)
	{
		for(int i = 0; i < CScientistMine::NUM_PARTICLES; i++)
		{
			float RandomRadius = random_float() * (Radius - 4.0f);
			vec2 ParticlePos = m_Pos + random_direction() * RandomRadius;
			GameController()->SendHammerDot(ParticlePos, m_Ids[CScientistMine::NUM_SIDE + i]);
		}
	}
}

void CScientistMine::Tick()
{
	if(m_MarkedForDestroy) return;

	// Find other players
	bool MustExplode = false;
	int DetonatedBy;
	for(TEntityPtr<CInfClassCharacter> p = GameWorld()->FindFirst<CInfClassCharacter>(); p; ++p)
	{
		if(p->IsHuman()) continue;
		if(!p->CanDie()) continue;

		float Len = distance(p->GetPos(), m_Pos);
		if(Len < p->GetProximityRadius() + GetProximityRadius())
		{
			MustExplode = true;
			DetonatedBy = p->GetCid();
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
