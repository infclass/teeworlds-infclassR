#ifndef GAME_SERVER_ENTITIES_MEDICGRENADE_H
#define GAME_SERVER_ENTITIES_MEDICGRENADE_H

#include "infcentity.h"

class CMedicGrenade : public CInfCEntity
{
public:
	CMedicGrenade(CGameContext *pGameContext, int Owner, vec2 Pos, vec2 Dir);

	vec2 GetPos(float Time);
	void FillInfo(CNetObj_Projectile *pProj);

	virtual void Tick();
	virtual void TickPaused();
	virtual void Explode();
	virtual void Snap(int SnappingClient);

private:
	vec2 m_ActualPos;
	vec2 m_ActualDir;
	vec2 m_Direction;
	int m_StartTick;
};

#endif
