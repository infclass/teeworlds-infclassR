#include "infcentity.h"

#include <game/animation.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>

static int FilterOwnerID = -1;

static bool OwnerFilter(const CEntity *pEntity)
{
	const CInfCEntity *pInfEntity = static_cast<const CInfCEntity *>(pEntity);
	return pInfEntity->GetOwner() == FilterOwnerID;
}

CInfCEntity::CInfCEntity(CGameContext *pGameContext, int ObjectType, vec2 Pos, int Owner,
                         int ProximityRadius)
	: CEntity(pGameContext->GameWorld(), ObjectType, Pos, ProximityRadius)
	, m_Owner(Owner)
{
}

CInfClassGameController *CInfCEntity::GameController()
{
	return static_cast<CInfClassGameController*>(GameServer()->m_pController);
}

CInfClassCharacter *CInfCEntity::GetOwnerCharacter()
{
	return GameController()->GetCharacter(GetOwner());
}

CInfClassPlayerClass *CInfCEntity::GetOwnerClass()
{
	CInfClassCharacter *pCharacter = GetOwnerCharacter();
	if (pCharacter)
		return pCharacter->GetClass();

	return nullptr;
}

EntityFilter CInfCEntity::GetOwnerFilterFunction(int Owner)
{
	FilterOwnerID = Owner;
	return OwnerFilter;
}

EntityFilter CInfCEntity::GetOwnerFilterFunction()
{
	return GetOwnerFilterFunction(GetOwner());
}

void CInfCEntity::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CInfCEntity::Tick()
{
	if(m_PosEnv >= 0)
	{
		SyncPosition();
	}
}

void CInfCEntity::SetPos(const vec2 &Position)
{
	m_Pos = Position;
}

void CInfCEntity::SetAnimatedPos(const vec2 &Pivot, const vec2 &RelPosition, int PosEnv)
{
	m_Pivot = Pivot;
	m_RelPosition = RelPosition;
	m_PosEnv = PosEnv;
}

bool CInfCEntity::DoSnapForClient(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return false;

	return true;
}

void CInfCEntity::SyncPosition()
{
	vec2 Position(0.0f, 0.0f);
	float Angle = 0.0f;
	if(m_PosEnv >= 0)
	{
		GetAnimationTransform(GameController()->GetTime(), m_PosEnv, GameServer()->Layers(), Position, Angle);
	}

	float x = (m_RelPosition.x * cosf(Angle) - m_RelPosition.y * sinf(Angle));
	float y = (m_RelPosition.x * sinf(Angle) + m_RelPosition.y * cosf(Angle));

	SetPos(Position + m_Pivot + vec2(x, y));
}
