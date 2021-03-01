/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_HEROFLAG_H
#define GAME_SERVER_ENTITIES_HEROFLAG_H

#include <game/server/entity.h>

class CHeroFlag : public CEntity
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
	int m_OwnerID;
	int m_IDs[SHIELD_COUNT];

public:
	static const int ms_PhysSize = 14;

	CHeroFlag(CGameWorld *pGameWorld, int ClientID);
	~CHeroFlag();

	int GetOwner() const;
	inline int GetCoolDown() { return m_CoolDownTick; }

	virtual void Tick();
	virtual void FindPosition();
	virtual void Snap(int SnappingClient);
	void GiveGift(CCharacter* pHero);

private:
	void SetCoolDown();
};

#endif
