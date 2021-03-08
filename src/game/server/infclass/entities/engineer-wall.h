/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_ENGINEER_WALL_H
#define GAME_SERVER_ENTITIES_ENGINEER_WALL_H

#include "infcentity.h"

class CInfClassCharacter;

class CEngineerWall : public CInfCEntity
{
public:
	CEngineerWall(CGameContext *pGameContext, vec2 Pos, vec2 Direction, int Owner);
	virtual ~CEngineerWall();

	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);
	int GetTick() { return m_LifeSpan; }
	void OnZombieHit(CInfClassCharacter *pZombie);

private:
	vec2 m_Pos2;
	int m_LifeSpan;
	int m_EndPointID;
	int m_WallFlashTicks;
};

#endif
