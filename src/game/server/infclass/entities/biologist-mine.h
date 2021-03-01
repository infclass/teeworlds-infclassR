/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_BIOLOGIST_MINE_H
#define GAME_SERVER_ENTITIES_BIOLOGIST_MINE_H

#include "infcentity.h"

class CBiologistMine : public CInfCEntity
{
public:
	enum
	{
		NUM_SIDE = 10,
		NUM_PARTICLES = 1,
		NUM_IDS = NUM_SIDE + NUM_PARTICLES,
	};
	
public:
	CBiologistMine(CGameContext *pGameContext, vec2 Pos, vec2 EndPos, int Owner);
	virtual ~CBiologistMine();

	virtual void Snap(int SnappingClient);
	virtual void Tick();

	void Explode();

private:
	int m_IDs[NUM_IDS];
	
public:
	vec2 m_EndPos;
};

#endif
