/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_TURRET_H
#define GAME_SERVER_ENTITIES_TURRET_H

#include "infcentity.h"

class CTurret : public CInfCEntity
{
public:
	enum Type
	{
		LASER,
		PLASMA,
	};

	CTurret(CGameContext *pGameContext, vec2 Pos, int Owner, vec2 Direction, Type Type);
	virtual ~CTurret();

	virtual void Tick();
	virtual void Snap(int SnappingClient);

protected:
	void AttackTargets();
	void Reload();

private:
	vec2 m_Vel;
	vec2 m_Dir;
	int m_StartTick;
	int m_Bounces;
	int m_Radius;
	int m_EvalTick;
	Type m_Type;
	const float m_RadiusGrowthRate = 4.0f;
	int m_WarmUpCounter;
	int m_ReloadCounter;
	int m_ammunition;
	bool m_foundTarget;
	
	array<int> m_IDs;

	int m_LifeSpan;

};

#endif
