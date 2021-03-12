/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_COLLISION_H
#define GAME_COLLISION_H

#include <base/vmath.h>
#include <base/tl/array.h>

#include <map>
#include <vector>

class CCollision
{
	int *m_pTiles;
	int m_Width;
	int m_Height;
	
	class CLayers *m_pLayers;
	
	double m_Time;
	
	array< array<int> > m_Zones;

	bool IsTileSolid(int x, int y) const;
	int GetTile(int x, int y) const;

public:
	enum
	{
		COLFLAG_SOLID=1,
		COLFLAG_NOHOOK=2,
		COLFLAG_WATER=4,
		
		ZONEFLAG_DEATH=1,
		ZONEFLAG_INFECTION=2,
		ZONEFLAG_NOSPAWN=4,
	};

	CCollision();
	~CCollision();
	void Init(class CLayers *pLayers);
	void InitTeleports();

	bool CheckPoint(float x, float y) const { return IsTileSolid(round(x), round(y)); }
	bool CheckPoint(vec2 Pos) const { return CheckPoint(Pos.x, Pos.y); }
	int GetCollisionAt(float x, float y) const { return GetTile(round(x), round(y)); }
	int GetWidth() const { return m_Width; };
	int GetHeight() const { return m_Height; };
	int IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const;
	void MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces) const;
	void MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity) const;
	bool TestBox(vec2 Pos, vec2 Size) const;

	void SetTime(double Time) { m_Time = Time; }
	
	//This function return an Handle to access all zone layers with the name "pName"
	int GetZoneHandle(const char* pName);
	int GetZoneValueAt(int ZoneHandle, float x, float y);
	int GetZoneValueAt(int ZoneHandle, vec2 Pos) { return GetZoneValueAt(ZoneHandle, Pos.x, Pos.y); }
	
/* INFECTION MODIFICATION START ***************************************/
	bool CheckPhysicsFlag(vec2 Pos, int Flag);
	
	bool AreConnected(vec2 Pos1, vec2 Pos2, float Radius);
/* INFECTION MODIFICATION END *****************************************/

	int GetPureMapIndex(float x, float y);
	int GetPureMapIndex(vec2 Pos) { return GetPureMapIndex(Pos.x, Pos.y); }

	class CTeleTile *TeleLayer() { return m_pTele; }
	class CLayers *Layers() { return m_pLayers; }

	const std::map<int, std::vector<vec2>> &GetTeleOuts() const { return m_TeleOuts; }

private:
	class CTeleTile *m_pTele;
	std::map<int, std::vector<vec2>> m_TeleOuts;
};

#endif
