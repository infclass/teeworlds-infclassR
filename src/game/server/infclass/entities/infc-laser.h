/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INFCLASS_ENTITIES_LASER_H
#define GAME_SERVER_INFCLASS_ENTITIES_LASER_H

#include "infcentity.h"

class CInfClassLaser : public CInfCEntity
{
public:
	CInfClassLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

protected:
	CInfClassLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, int ObjType);

	virtual bool HitCharacter(vec2 From, vec2 To);
	virtual void DoBounce();

protected:
	vec2 m_From;
	vec2 m_Dir;
	float m_Energy;
	int m_Bounces = 0;
	int m_EvalTick = 0;
	int m_Dmg = 0;
	bool m_BouncesStop = false;
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_LASER_H
