#ifndef GAME_SERVER_ENTITIES_LOOPER_WALL_H
#define GAME_SERVER_ENTITIES_LOOPER_WALL_H

#include "infc-placed-object.h"

class CLooperWall : public CPlacedObject
{
public:
	enum
	{
		THICKNESS = 17,
		NUM_PARTICLES = 18,
	};
public:
	CLooperWall(CGameContext *pGameContext, vec2 Pos, vec2 Direction, int Owner);
	virtual ~CLooperWall();
	
	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);
	int GetTick() { return m_LifeSpan; }

private:
	int m_LifeSpan;

	int m_IDs[2];
	int m_EndPointIDs[2];
	const float g_BarrierMaxLength = 400.0;
	const float g_BarrierRadius = 0.0;
	int m_ParticleIDs[NUM_PARTICLES];
};

#endif
