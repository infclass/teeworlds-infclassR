/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_FKING_BOMB_H
#define GAME_SERVER_ENTITIES_FKING_BOMB_H

#include "infcentity.h"

#include <base/tl/array.h>

class CFKingPower : public CInfCEntity
{
public:
	CFKingPower(CGameContext *pGameContext, vec2 Pos, int Owner);
	virtual ~CFKingPower();

	virtual void Snap(int SnappingClient);
	virtual void TickPaused();
	void Explode();
	bool AddP();
	int GetNbP() { return m_nbP; }

private:
	virtual void ChargeP(float time);
	int m_StartTick;
	array<int> m_IDP;
	int m_nbP;
	int charged_P;
	
public:
	float m_DetectionRadius;
};

#endif
