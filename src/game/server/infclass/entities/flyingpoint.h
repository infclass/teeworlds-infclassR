/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_FLYINGPOINT_H
#define GAME_SERVER_ENTITIES_FLYINGPOINT_H

#include "infcentity.h"

class CFlyingPoint : public CInfCEntity
{
public:
	static int EntityId;

	CFlyingPoint(CGameContext *pGameContext, vec2 Pos, int TrackedPlayer, int Points, vec2 InitialVel);
	
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	vec2 m_InitialVel;
	float m_InitialAmount;
	int m_Points;

};

#endif
