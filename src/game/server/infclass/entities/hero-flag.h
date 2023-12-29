/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_HEROFLAG_H
#define GAME_SERVER_ENTITIES_HEROFLAG_H

#include "infcentity.h"

class CInfClassCharacter;

class CHeroFlag : public CInfCEntity
{
public:
	static constexpr int ms_SHIELD_COUNT = 4;

	static const int ms_PhysSize = 14;

	CHeroFlag(CGameContext *pGameContext, int Owner);
	~CHeroFlag() override;

	void FindPosition();
	void GiveGift(CInfClassCharacter *pHero);

	bool IsAvailable() const { return m_HasSpawnPosition; }
	int GetSpawnTick() const { return m_SpawnTick; }
	void ResetCooldown();

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

private:
	int m_SpawnTick = 0;
	int m_IDs[ms_SHIELD_COUNT];
	bool m_HasSpawnPosition = false;
};

#endif
