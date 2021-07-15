#ifndef GAME_SERVER_ENTITIES_CHAINING_LASER_H
#define GAME_SERVER_ENTITIES_CHAINING_LASER_H

#include "infcentity.h"

#include "base/tl/array_on_stack.h"

class CChainingLaser : public CInfCEntity
{
public:
	static int EntityId;

	static const int MaxTargets = 20;
	CChainingLaser(CGameContext *pGameContext, const vec2 &Pos, vec2 Direction, float StartEnergy, int Owner);
	~CChainingLaser() override;

	void Tick() override;
	void Snap(int SnappingClient) override;

protected:
	void AddSnapItem(const vec2 &From, const vec2 &To, int SnapTick);
	void PrepareSnapItems();

	bool GenerateThePath();
	void UpdateThePath();

	struct LaserSnapItem
	{
		ivec2 From;
		ivec2 To;
		int StartTick;
		int SnapID;
	};

	struct Link
	{
		Link() = default;
		Link(const vec2 &Pos, int Tick)
			: Endpoint(Pos)
			, StartTick(Tick)
		{
		}

		vec2 Endpoint;
		int StartTick;
	};

	static const int MaxSnapItems = MaxTargets + 1;
	LaserSnapItem m_LasersForSnap[MaxSnapItems];
	int m_ActiveSnapItems = 0;

	array_on_stack<Link, MaxTargets + 1> m_Links;
	vec2 m_Direction;
	float m_Energy = 0;
	int m_DecayRemainingTicks = 0;

};

#endif // GAME_SERVER_ENTITIES_CHAINING_LASER_H
