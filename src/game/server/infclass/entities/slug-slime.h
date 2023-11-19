/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_SLUG_SLIME_H
#define GAME_SERVER_ENTITIES_SLUG_SLIME_H

#include "infc-placed-object.h"

class CSlugSlime : public CPlacedObject
{
public:
	static int EntityId;

	CSlugSlime(CGameContext *pGameContext, vec2 Pos, int Owner);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	int GetEndTick() const { return m_EndTick; }

	bool Replenish(int PlayerID, int EndTick);

private:
	int m_StartTick = 0;
	int m_EndTick = 0;
};

#endif
