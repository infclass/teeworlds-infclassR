/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_HEROFLAG_H
#define GAME_SERVER_ENTITIES_HEROFLAG_H

#include "infcentity.h"

class CInfClassCharacter;

class CHeroFlag : public CInfCEntity
{
public:
	enum
	{
		SHIELD_COUNT = 4,
	};

	static const int ms_PhysSize = 14;

	CHeroFlag(CGameContext *pGameContext, int Owner);
	~CHeroFlag() override;

	void FindPosition();
	void GiveGift(CInfClassCharacter *pHero);

	int GetCoolDown() const { return m_CoolDownTick; }

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void SetCoolDown();

	int m_CoolDownTick = 0;
	int m_IDs[SHIELD_COUNT];
};

#endif
