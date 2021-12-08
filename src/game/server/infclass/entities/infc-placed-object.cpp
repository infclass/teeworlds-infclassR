#include "infc-placed-object.h"

#include "infccharacter.h"

#include <game/server/infclass/infcgamecontroller.h>

CPlacedObject::CPlacedObject(CGameContext *pGameContext, int ObjectType, vec2 Pos, int Owner, int ProximityRadius)
	: CInfCEntity(pGameContext, ObjectType, Pos, Owner, ProximityRadius)
{
}

bool CPlacedObject::DoSnapForClient(int SnappingClient)
{
	if(!CInfCEntity::DoSnapForClient(SnappingClient))
		return false;

	CInfClassCharacter *pCharacter = GameController()->GetCharacter(SnappingClient);
	if(pCharacter && pCharacter->IsBlind())
		return false;

	return true;
}
