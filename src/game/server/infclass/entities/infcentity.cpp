#include "infcentity.h"

#include <game/server/gamecontext.h>

CInfCEntity::CInfCEntity(CGameContext *pGameContext, int ObjectType, vec2 Pos, int Owner,
                         int ProximityRadius)
	: CEntity(pGameContext->GameWorld(), ObjectType, Pos, ProximityRadius)
	, m_Owner(Owner)
{
}

void CInfCEntity::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CInfCEntity::SetPosition(const vec2 &Position)
{
	m_Pos = Position;
}
