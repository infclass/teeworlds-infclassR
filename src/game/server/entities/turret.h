/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_TURRET_H
#define GAME_SERVER_ENTITIES_TURRET_H

#include <game/server/entity.h>

class CTurret : public CEntity
{
public:
	CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, vec2 Direction, float StartEnergy, int Type);
	virtual ~CTurret();
	
	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	
	int GetOwner() const;

protected:
	bool HitCharacter(vec2 From, vec2 To);

private:
	vec2 m_Vel;
	vec2 m_Dir;
	int m_StartTick;
	float m_Energy;
	int m_Bounces;
	int m_Radius;
	int m_EvalTick;
	int m_Owner;
	int m_Type;
	const float m_RadiusGrowthRate = 4.0f;
	int m_WarmUpCounter;
	int m_ReloadCounter;
	int m_ammunition;
	bool m_foundTarget;
	
	array<int> m_IDs;

	int m_LifeSpan;

};

#endif
