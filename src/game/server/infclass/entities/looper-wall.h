#ifndef GAME_SERVER_ENTITIES_LOOPER_WALL_H
#define GAME_SERVER_ENTITIES_LOOPER_WALL_H

#include "infc-placed-object.h"

class CLooperWall : public CPlacedObject
{
public:
	static int EntityId;

	static constexpr int NUM_PARTICLES = 18;

public:
	CLooperWall(CGameContext *pGameContext, vec2 Pos, int Owner);
	~CLooperWall() override;

	void SetEndPosition(vec2 EndPosition);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	int GetEndTick() const { return m_EndTick; }

private:
	void OnHitInfected(CInfClassCharacter *pCharacter);

	void PrepareSnapData();

	int m_EndTick{};

	int m_IDs[2]{};
	int m_EndPointIDs[2]{};
	int m_ParticleIDs[NUM_PARTICLES]{};
	int m_SnapStartTick{};
};

#endif
