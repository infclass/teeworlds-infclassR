/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_INFC_PLACED_OBJECT_H
#define GAME_SERVER_ENTITIES_INFC_PLACED_OBJECT_H

#include "infcentity.h"

class CPlacedObject : public CInfCEntity
{
public:
	CPlacedObject(CGameContext *pGameContext, int ObjectType, vec2 Pos = vec2(), int Owner = -1, int ProximityRadius=0);
	~CPlacedObject() override;

	bool HasSecondPosition() const { return m_InfClassObjectFlags & INFCLASS_OBJECT_FLAG_HAS_SECOND_POSITION; }

protected:
	bool DoSnapForClient(int SnappingClient) override;

	CNetObj_InfClassObject *SnapInfClassObject();

protected:
	vec2 m_Pos2;
	int m_InfClassObjectID = -1;
	int m_InfClassObjectType = -1;
	int m_InfClassObjectFlags = 0;
};

#endif // GAME_SERVER_ENTITIES_INFC_PLACED_OBJECT_H
