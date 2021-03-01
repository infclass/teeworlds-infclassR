/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_SOLDIER_BOMB_H
#define GAME_SERVER_ENTITIES_SOLDIER_BOMB_H

#include "infcentity.h"

#include <base/tl/array.h>

class CSoldierBomb : public CInfCEntity
{
public:
	CSoldierBomb(CGameContext *pGameContext, vec2 Pos, int Owner);
	virtual ~CSoldierBomb();

	virtual void Snap(int SnappingClient);
	virtual void TickPaused();
	void Explode();
	bool AddBomb();
	int GetNbBombs() { return m_nbBomb; }

private:
	virtual void ChargeBomb(float time);
	int m_StartTick;
	array<int> m_IDBomb;
	int m_nbBomb;
	int charged_bomb;
	
public:
	float m_DetectionRadius;
};

#endif
