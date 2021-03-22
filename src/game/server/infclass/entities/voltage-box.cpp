/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "voltage-box.h"

#include <game/server/gamecontext.h>

int CVoltageBox::EntityId = CGameWorld::ENTTYPE_VOLTAGE_BOX;
static constexpr int BoxProximityRadius = 24;

CVoltageBox::CVoltageBox(CGameContext *pGameContext, vec2 CenterPos, int Owner)
	: CInfCEntity(pGameContext, EntityId, CenterPos, Owner, BoxProximityRadius)
{
}

void CVoltageBox::Snap(int SnappingClient)
{
}
