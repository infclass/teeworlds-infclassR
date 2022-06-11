/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_COLLISION_H
#define GAME_COLLISION_H

#include <base/vmath.h>
#include <base/tl/array.h>

#include <map>
#include <vector>

enum
{
	CANTMOVE_LEFT = 1 << 0,
	CANTMOVE_RIGHT = 1 << 1,
	CANTMOVE_UP = 1 << 2,
	CANTMOVE_DOWN = 1 << 3,
};

vec2 ClampVel(int MoveRestriction, vec2 Vel);

typedef bool (*CALLBACK_SWITCHACTIVE)(int Number, void *pUser);

struct ZoneData
{
	int Index = -1;
	int ExtraData = -1;
};

class CCollision
{
	class CTile *m_pTiles;
	int m_Width;
	int m_Height;
	class CLayers *m_pLayers;
	
	double m_Time;
	
	array< array<int> > m_Zones;

	bool IsSolid(int x, int y) const;
	int GetTile(int x, int y) const;

public:
	enum
	{
		ZONEFLAG_DEATH=1,
		ZONEFLAG_INFECTION=2,
		ZONEFLAG_NOSPAWN=4,
	};

	CCollision();
	~CCollision();
	void Init(class CLayers *pLayers);
	void InitTeleports();

	bool CheckPoint(float x, float y) const { return IsSolid(round_to_int(x), round(y)); }
	bool CheckPoint(vec2 Pos) const { return CheckPoint(Pos.x, Pos.y); }
	int GetCollisionAt(float x, float y) const { return GetTile(round(x), round(y)); }
	int GetWidth() const { return m_Width; };
	int GetHeight() const { return m_Height; };
	int IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const;
	void MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces) const;
	void MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity) const;
	bool TestBox(vec2 Pos, vec2 Size) const;

	void Dest();

	int GetMoveRestrictions(CALLBACK_SWITCHACTIVE pfnSwitchActive, void *pUser, vec2 Pos, float Distance = 18.0f, int OverrideCenterTileIndex = -1);
	int GetMoveRestrictions(vec2 Pos, float Distance = 18.0f)
	{
		return GetMoveRestrictions(0, 0, Pos, Distance);
	}

	void SetTime(double Time) { m_Time = Time; }
	
	//This function return an Handle to access all zone layers with the name "pName"
	int GetZoneHandle(const char* pName);
	int GetZoneValueAt(int ZoneHandle, float x, float y, ZoneData *pData = nullptr);
	int GetZoneValueAt(int ZoneHandle, vec2 Pos, ZoneData *pData = nullptr) { return GetZoneValueAt(ZoneHandle, Pos.x, Pos.y, pData); }
	
/* INFECTION MODIFICATION START ***************************************/
	bool AreConnected(vec2 Pos1, vec2 Pos2, float Radius);
/* INFECTION MODIFICATION END *****************************************/

	int GetPureMapIndex(float x, float y) const;
	int GetPureMapIndex(vec2 Pos) const { return GetPureMapIndex(Pos.x, Pos.y); }
	int GetMapIndex(vec2 Pos) const;
	bool TileExists(int Index) const;
	bool TileExistsNext(int Index) const;
	vec2 GetPos(int Index) const;
	int GetTileIndex(int Index) const;
	int GetTileFlags(int Index) const;

	int IsSpeedup(int Index) const;
	void GetSpeedup(int Index, vec2 *Dir, int *Force, int *MaxSpeed) const;

	class CTeleTile *TeleLayer() { return m_pTele; }
	class CLayers *Layers() { return m_pLayers; }

	const std::map<int, std::vector<vec2>> &GetTeleOuts() const { return m_TeleOuts; }

private:
	class CTeleTile *m_pTele;
	std::map<int, std::vector<vec2>> m_TeleOuts;

	class CSpeedupTile *m_pSpeedup;
};

#endif
