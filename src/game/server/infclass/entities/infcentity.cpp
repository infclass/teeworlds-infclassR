#include "infcentity.h"

#include <game/server/gamecontext.h>
#include <game/server/infclass/infcgamecontroller.h>

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

void CInfCEntity::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

void CInfCEntity::SetPosition(const vec2 &Position)
{
	m_Pos = Position;
}
