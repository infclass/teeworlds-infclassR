#include "infc-placed-object.h"

CPlacedObject::CPlacedObject(CGameContext *pGameContext, int ObjectType, vec2 Pos, int Owner, int ProximityRadius)
	: CInfCEntity(pGameContext, ObjectType, Pos, Owner, ProximityRadius)
{
}
