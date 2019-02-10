// Strongly modified version of ddnet Plasma. Source: Shereef Marzouk
#ifndef GAME_SERVER_ENTITIES_PLASMA_H
#define GAME_SERVER_ENTITIES_PLASMA_H

#include <game/server/entity.h>

class CPlasma: public CEntity
{
	
public:
	CPlasma(CGameWorld *pGameWorld, vec2 Pos, int Owner,int TrackedPlayer, vec2 Direction, bool Freeze, bool Explosive);
	
	int GetOwner() const;
	
	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);
	
private:
	void Explode();
	
public:
	int m_Owner;
private:
	int m_StartTick;
	int m_LifeSpan;
	vec2 m_Dir;
	float m_Speed;
	int m_Freeze;
	bool m_Explosive;
	int m_TrackedPlayer;
	float m_InitialAmount;
};

#endif // GAME_SERVER_ENTITIES_PLASMA_H
