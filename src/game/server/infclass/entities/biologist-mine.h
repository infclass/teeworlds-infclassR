/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_BIOLOGIST_MINE_H
#define GAME_SERVER_ENTITIES_BIOLOGIST_MINE_H

#include "infc-placed-object.h"

class CBiologistMine : public CPlacedObject
{
public:
	static int EntityId;

	enum
	{
		NUM_SIDE = 10,
		NUM_PARTICLES = 1,
		NUM_IDS = NUM_SIDE + NUM_PARTICLES,
	};
	
public:
	CBiologistMine(CGameContext *pGameContext, vec2 Pos, vec2 EndPos, int Owner, int Lasers, int Damage);
	virtual ~CBiologistMine();

	virtual void Snap(int SnappingClient);
	virtual void Tick();

	void Explode();

private:
	int m_IDs[NUM_IDS];
	int m_Lasers = 0;
	int m_PerLaserDamage = 0;

public:
	vec2 m_EndPos;
};

#endif
