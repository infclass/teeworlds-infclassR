/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_IC_ENTITIES_PICKUP_H
#define GAME_SERVER_IC_ENTITIES_PICKUP_H

#include "infcentity.h"

const int PickupPhysSize = 20;

enum class IC_PICKUP_TYPE
{
	INVALID,
	HEALTH,
	ARMOR,
};

class CIcPickup : public CInfCEntity
{
public:
	CIcPickup(CGameContext *pGameContext, IC_PICKUP_TYPE Type, vec2 Pos, int Owner = -1);

	void Reset() final;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	
	void Spawn(float Delay = 0);
	void SetRespawnInterval(float Seconds);

private:
	IC_PICKUP_TYPE m_Type = IC_PICKUP_TYPE::INVALID;
	int m_SpawnTick = 0;
	float m_SpawnInterval = -1;
};

#endif
