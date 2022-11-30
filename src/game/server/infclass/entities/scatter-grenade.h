/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_SCATTERGRENADE_H
#define GAME_SERVER_ENTITIES_SCATTERGRENADE_H

#include "infcentity.h"

class CScatterGrenade : public CInfCEntity
{
public:
	static int EntityId;

	CScatterGrenade(CGameContext *pGameContext, int Owner, vec2 Pos, vec2 Dir);

	vec2 GetPos(float Time);
	void FillInfo(CNetObj_Projectile *pProj);

	virtual void Tick();
	virtual void TickPaused();
	virtual void Explode();
	virtual void Snap(int SnappingClient);
	virtual void FlashGrenade();
	void ExplodeOnContact();

private:
	vec2 m_ActualPos;
	vec2 m_ActualDir;
	vec2 m_Direction;
	int m_StartTick;
	bool m_IsFlashGrenade;
	bool m_ExplodeOnContact = false;
};

#endif
