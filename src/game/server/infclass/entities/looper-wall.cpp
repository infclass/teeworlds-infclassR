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

static const float g_BarrierMaxLength = 400.0;
static const float g_BarrierRadius = 0.0;

static constexpr float g_Thickness = 17.0f;

int CLooperWall::EntityId = CGameWorld::ENTTYPE_LOOPER_WALL;

CLooperWall::CLooperWall(CGameContext *pGameContext, vec2 Pos1, int Owner) :
	CPlacedObject(pGameContext, EntityId, Pos1, Owner)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_LOOPER_WALL;

	for(int &Id : m_Ids)
	{
		Id = Server()->SnapNewId();
	}
	for(int &Id : m_EndPointIds)
	{
		Id = Server()->SnapNewId();
	}
	for(int &Id : m_ParticleIds)
	{
		Id = Server()->SnapNewId();
	}

	GameWorld()->InsertEntity(this);
}

CLooperWall::~CLooperWall()
{
	for(int Id : m_ParticleIds)
	{
		Server()->SnapFreeId(Id);
	}
	for(int Id : m_EndPointIds)
	{
		Server()->SnapFreeId(Id);
	}
	for(int Id : m_Ids)
	{
		Server()->SnapFreeId(Id);
	}
}

void CLooperWall::SetEndPosition(vec2 EndPosition)
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

	int LifeSpan = Server()->TickSpeed() * Config()->m_InfLooperBarrierLifeSpan;
	if(GameController()->GetRoundType() == ERoundType::Survival)
	{
		LifeSpan *= 0.5f;
	}
	m_EndTick = Server()->Tick() + LifeSpan;
}

void CLooperWall::Tick()
{
	if(IsMarkedForDestroy())
		return;

	if(!HasSecondPosition())
	{
		m_SnapStartTick = Server()->Tick();
		return;
	}

	if(Server()->Tick() >= m_EndTick)
	{
		GameWorld()->DestroyEntity(this);
	}
	else
	{
		// Find other players
		for(TEntityPtr<CInfClassCharacter> p = GameWorld()->FindFirst<CInfClassCharacter>(); p; ++p)
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

	const CInfClassPlayer *pDestPlayer = GameController()->GetPlayer(SnappingClient);
	if(!HasSecondPosition())
	{
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

	if(!HasSecondPosition())
	{
		for(int i = 0; i < 2; i++)
		{
			// draws the first two dots + the lasers
			vec2 Pos = m_Pos;
			Pos.x += g_Thickness * 0.5 - g_Thickness * i;
			GameServer()->SnapLaserObject(Context, m_Ids[i], Pos, Pos, m_SnapStartTick);
		}
		return;
	}

	const bool AntiPing = pDestPlayer && pDestPlayer->GetAntiPingEnabled();
	vec2 dirVec = vec2(m_Pos.x-m_Pos2.x, m_Pos.y-m_Pos2.y);
	vec2 dirVecN = normalize(dirVec);
	vec2 dirVecT = vec2(dirVecN.y * g_Thickness * 0.5f, -dirVecN.x * g_Thickness * 0.5f);

	for(int i = 0; i < 2; i++)
	{
		if(i == 1)
		{
			dirVecT.x = -dirVecT.x;
			dirVecT.y = -dirVecT.y;
		}

		// draws the first two dots + the lasers
		GameServer()->SnapLaserObject(Context, m_Ids[i], m_Pos + dirVecT, m_Pos2 + dirVecT, m_SnapStartTick);

		// draws one dot at the end of each laser
		if(!AntiPing)
		{
			GameServer()->SnapLaserObject(Context, m_EndPointIds[i], m_Pos2 + dirVecT, m_Pos2 + dirVecT, Server()->Tick());
		}
	}

	// draw particles inside wall
	if(!AntiPing)
	{
		vec2 startPos = vec2(m_Pos2.x+dirVecT.x, m_Pos2.y+dirVecT.y);
		dirVecT.x = -dirVecT.x*2.0f;
		dirVecT.y = -dirVecT.y*2.0f;

		int particleCount = length(dirVec) / g_BarrierMaxLength * static_cast<float>(NUM_PARTICLES);
		for(int i=0; i<particleCount; i++)
		{
			float fRandom1 = random_float();
			float fRandom2 = random_float();
			GameController()->SendHammerDot(startPos + dirVec * fRandom1 + dirVecT * fRandom2, m_ParticleIds[i]);
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
			if(pCharacter->GetPlayerClass() == EPlayerClass::Ghoul)
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
		GameServer()->SendEmoticon(pCharacter->GetCid(), EMOTICON_EXCLAMATION);
	}

	int LifeSpanReducer = Server()->TickSpeed() * Reduction * AddedDuration / FullEffectDuration;

	if(GameController()->GetRoundType() == ERoundType::Survival)
	{
		LifeSpanReducer = LifeSpanReducer / 3.0f;
	}

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
