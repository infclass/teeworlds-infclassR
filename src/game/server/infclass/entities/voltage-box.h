/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INFCLASS_ENTITIES_VOLTAGE_BOX_H
#define GAME_SERVER_INFCLASS_ENTITIES_VOLTAGE_BOX_H

#include "infcentity.h"

class CVoltageBox : public CInfCEntity
{
public:
	static int EntityId;

	CVoltageBox(CGameContext *pGameContext, vec2 CenterPos, int Owner);

	void Snap(int SnappingClient) override;
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_VOLTAGE_BOX_H
