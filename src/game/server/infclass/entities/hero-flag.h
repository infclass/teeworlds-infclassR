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
		RADIUS = 50,
		SHIELD_COUNT = 4,
		SPEED = 15, // higher = slower
	};

private:
	int m_CoolDownTick;
	int m_IDs[SHIELD_COUNT];

public:
	static const int ms_PhysSize = 14;

	CHeroFlag(CGameContext *pGameContext, int Owner);
	~CHeroFlag();

	int GetCoolDown() { return m_CoolDownTick; }

	virtual void Tick();
	virtual void FindPosition();
	virtual void Snap(int SnappingClient);
	void GiveGift(CInfClassCharacter *pHero);

private:
	void SetCoolDown();
};

#endif
