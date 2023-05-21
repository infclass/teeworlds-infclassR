//slightly modified from engineer-wall.cpp
#include <base/math.h>
#include <base/vmath.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/server/infclass/classes/infcplayerclass.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include "looper-wall.h"
#include "infccharacter.h"

int CLooperWall::EntityId = CGameWorld::ENTTYPE_LOOPER_WALL;

CLooperWall::CLooperWall(CGameContext *pGameContext, vec2 Pos1, vec2 Pos2, int Owner)
	: CPlacedObject(pGameContext, EntityId, Pos1, Owner)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_LOOPER_WALL;
	m_InfClassObjectFlags = INFCLASS_OBJECT_FLAG_HAS_SECOND_POSITION;

	if(distance(Pos1, Pos2) > g_BarrierMaxLength)
	{
		m_Pos2 = Pos1 + normalize(Pos2 - Pos1)*g_BarrierMaxLength;
	}
	else
	{
		m_Pos2 = Pos2;
	}

	m_EndTick = Server()->Tick() + Server()->TickSpeed() * Config()->m_InfLooperBarrierLifeSpan;
	GameWorld()->InsertEntity(this);

	for(int &ID : m_IDs)
	{
		ID = Server()->SnapNewID();
	}
	for(int &ID : m_EndPointIDs)
	{
		ID = Server()->SnapNewID();
	}
	for(int &ID : m_ParticleIDs)
	{
		ID = Server()->SnapNewID();
	}
}

CLooperWall::~CLooperWall()
{
	for(int ID : m_ParticleIDs)
	{
		Server()->SnapFreeID(ID);
	}
	for(int ID : m_EndPointIDs)
	{
		Server()->SnapFreeID(ID);
	}
	for(int ID : m_IDs)
	{
		Server()->SnapFreeID(ID);
	}
}

void CLooperWall::Tick()
{
	if(m_MarkedForDestroy)
		return;

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

void CLooperWall::TickPaused()
{
	++m_EndTick;
}

void CLooperWall::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;

		pInfClassObject->m_EndTick = m_EndTick;
	}

	const CInfClassPlayer *pPlayer = GameController()->GetPlayer(SnappingClient);
	const bool AntiPing = pPlayer && pPlayer->GetAntiPingEnabled();

	vec2 dirVec = vec2(m_Pos.x-m_Pos2.x, m_Pos.y-m_Pos2.y);
	vec2 dirVecN = normalize(dirVec);
	vec2 dirVecT = vec2(dirVecN.y*THICKNESS*0.5f, -dirVecN.x*THICKNESS*0.5f);

	for(int i=0; i<2; i++) 
	{
		if (i == 1)
		{
			dirVecT.x = -dirVecT.x;
			dirVecT.y = -dirVecT.y;
		}
		
		// draws the first two dots + the lasers
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[i], sizeof(CNetObj_Laser))); //removed m_ID
			if(!pObj)
				return;

			pObj->m_X = (int)m_Pos.x+dirVecT.x; 
			pObj->m_Y = (int)m_Pos.y+dirVecT.y;
			pObj->m_FromX = (int)m_Pos2.x+dirVecT.x; 
			pObj->m_FromY = (int)m_Pos2.y+dirVecT.y;

			pObj->m_StartTick = m_SnapStartTick;
		}
		
		// draws one dot at the end of each laser
		if(!AntiPing)
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_EndPointIDs[i], sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = (int)m_Pos2.x+dirVecT.x; 
			pObj->m_Y = (int)m_Pos2.y+dirVecT.y;
			pObj->m_FromX = (int)m_Pos2.x+dirVecT.x; 
			pObj->m_FromY = (int)m_Pos2.y+dirVecT.y;

			pObj->m_StartTick = Server()->Tick();
		}
	}

	// draw particles inside wall
	if(!AntiPing)
	{
		vec2 startPos = vec2(m_Pos2.x+dirVecT.x, m_Pos2.y+dirVecT.y);
		dirVecT.x = -dirVecT.x*2.0f;
		dirVecT.y = -dirVecT.y*2.0f;
		
		int particleCount = length(dirVec)/g_BarrierMaxLength*NUM_PARTICLES;
		for(int i=0; i<particleCount; i++)
		{
			float fRandom1 = random_float();
			float fRandom2 = random_float();
			GameController()->SendHammerDot(startPos + dirVec * fRandom1 + dirVecT * fRandom2, m_ParticleIDs[i]);
		}
	}
}

void CLooperWall::OnHitInfected(CInfClassCharacter *pCharacter)
{
	float Reduction = Config()->m_InfLooperBarrierTimeReduce * 0.01f;

	if(pCharacter->GetPlayer())
	{
		if(!pCharacter->IsInSlowMotion())
		{
			if(pCharacter->GetPlayerClass() == PLAYERCLASS_GHOUL)
			{
				float Factor = pCharacter->GetClass()->GetGhoulPercent();
				Reduction += 5.0f * Factor;
			}
		}
	}

	// Slow-Motion modification here
	const float FullEffectDuration = Config()->m_InfSlowMotionWallDuration * 0.1f;
	const float AddedDuration = pCharacter->SlowMotionEffect(FullEffectDuration, GetOwner());
	if(AddedDuration > 1.0f)
	{
		GameServer()->SendEmoticon(pCharacter->GetCID(), EMOTICON_EXCLAMATION);
	}

	int LifeSpanReducer = Server()->TickSpeed() * Reduction * AddedDuration / FullEffectDuration;
	m_EndTick -= LifeSpanReducer;
}

void CLooperWall::PrepareSnapData()
{
	const int RemainingTicks = m_EndTick - Server()->Tick();

	// Laser dieing animation
	int LifeDiff = 0;
	if(RemainingTicks < 1 * Server()->TickSpeed())
		LifeDiff = 6;
	else if(RemainingTicks < 2 * Server()->TickSpeed())
		LifeDiff = random_int(4, 6);
	else if(RemainingTicks < 5 * Server()->TickSpeed())
		LifeDiff = random_int(3, 5);
	else
		LifeDiff = 3;

	m_SnapStartTick = Server()->Tick() - LifeDiff;
}
