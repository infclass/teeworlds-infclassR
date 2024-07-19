/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_ENGINEER_WALL_H
#define GAME_SERVER_ENTITIES_ENGINEER_WALL_H

#include "infc-placed-object.h"

class CInfClassCharacter;

class CEngineerWall : public CPlacedObject
{
public:
	static int EntityId;

	CEngineerWall(CGameContext *pGameContext, vec2 Pos, int Owner);
	~CEngineerWall() override;

	void SetEndPosition(vec2 EndPosition);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	int GetEndTick() const { return m_EndTick; }
	void OnHitInfected(CInfClassCharacter *pCharacter);

private:
	void PrepareSnapData();

	int m_EndTick{};
	int m_EndPointId{};
	int m_WallFlashTicks{};
	int m_SnapStartTick{};
};

#endif
