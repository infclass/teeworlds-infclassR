/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_SCIENTIST_MINE_H
#define GAME_SERVER_ENTITIES_SCIENTIST_MINE_H

#include "infcentity.h"

class CScientistMine : public CInfCEntity
{
public:
	enum
	{
		NUM_SIDE = 12,
		NUM_PARTICLES = 12,
		NUM_IDS = NUM_SIDE + NUM_PARTICLES,
	};
	
public:
	CScientistMine(CGameContext *pGameContext, vec2 Pos, int Owner);
	virtual ~CScientistMine();

	virtual void Snap(int SnappingClient);
	virtual void TickPaused();
	virtual void Tick();

	void Explode(int DetonatedBy);

private:
	int m_IDs[NUM_IDS];
	
public:
	int m_StartTick;
	float m_DetectionRadius;
};

#endif
