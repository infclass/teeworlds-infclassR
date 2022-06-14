/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_MERCENARY_BOMB_H
#define GAME_SERVER_ENTITIES_MERCENARY_BOMB_H

#include "infc-placed-object.h"

class CMercenaryBomb : public CPlacedObject
{
public:
	enum
	{
		NUM_SIDE = 12,
		NUM_HINT = 12,
		NUM_IDS = NUM_SIDE + NUM_HINT,
	};

public:
	static int EntityId;

	CMercenaryBomb(CGameContext *pGameContext, vec2 Pos, int Owner);
	~CMercenaryBomb();

	virtual void Snap(int SnappingClient);
	virtual void Tick();
	void Explode();
	void Upgrade(float Points);
	bool ReadyToExplode();
	static float GetMaxRadius();

private:
	int m_IDs[NUM_IDS];
	
public:
	int m_LoadingTick;
	float m_Damage;
};

#endif
