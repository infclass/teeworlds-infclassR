/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_IC_ENTITIES_PICKUP_H
#define GAME_SERVER_IC_ENTITIES_PICKUP_H

#include "infcentity.h"

const int PickupPhysSize = 20;

enum class EICPickupType
{
	Invalid,
	Health,
	Armor,
};

class CIcPickup : public CInfCEntity
{
public:
	static int EntityId;

	CIcPickup(CGameContext *pGameContext, EICPickupType Type, vec2 Pos, int Owner = -1);

	void Reset() final;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	
	void Spawn(float Delay = 0);
	void SetRespawnInterval(float Seconds);

private:
	EICPickupType m_Type = EICPickupType::Invalid;
	int m_SpawnTick = 0;
	float m_SpawnInterval = -1;
	int m_NetworkType = 0;
};

#endif
