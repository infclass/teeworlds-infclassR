/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_MERCENARY_BOMB_H
#define GAME_SERVER_ENTITIES_MERCENARY_BOMB_H

#include "infc-placed-object.h"

class CMercenaryBomb : public CPlacedObject
{
public:
	enum
	{
		NUM_SIDE = 12,
		NUM_HINT = 12,
		NUM_IDS = NUM_SIDE + NUM_HINT,
	};

public:
	static int EntityId;

	CMercenaryBomb(CGameContext *pGameContext, vec2 Pos, int Owner);
	~CMercenaryBomb();

	void Snap(int SnappingClient) override;
	void Tick() override;

	void Explode(int TriggeredBy);
	bool IsReadyToExplode() const;
	void SetLoad(float Load);
	float GetLoad() const { return m_Load; }

private:
	int m_Ids[NUM_IDS];

	int m_LoadingTick;
	float m_Load;
};

#endif
