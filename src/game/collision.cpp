/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/system.h>
#include <base/math.h>
#include <base/vmath.h>

#include <math.h>
#include <engine/map.h>
#include <engine/kernel.h>

#include <game/mapitems.h>
#include <game/layers.h>
#include <game/collision.h>

#include <game/gamecore.h>
#include <game/animation.h>

vec2 ClampVel(int MoveRestriction, vec2 Vel)
{
	if(Vel.x > 0 && (MoveRestriction & CANTMOVE_RIGHT))
	{
		Vel.x = 0;
	}
	if(Vel.x < 0 && (MoveRestriction & CANTMOVE_LEFT))
	{
		Vel.x = 0;
	}
	if(Vel.y > 0 && (MoveRestriction & CANTMOVE_DOWN))
	{
		Vel.y = 0;
	}
	if(Vel.y < 0 && (MoveRestriction & CANTMOVE_UP))
	{
		Vel.y = 0;
	}
	return Vel;
}

CCollision::CCollision()
{
	m_pTiles = 0;
	m_Width = 0;
	m_Height = 0;
	
	m_pLayers = 0;

	m_pTele = 0;
	m_pSpeedup = 0;

	m_Time = 0.0;
}

CCollision::~CCollision()
{
	Dest();
}

void CCollision::Init(class CLayers *pLayers)
{
	Dest();
	m_pLayers = pLayers;
	m_Width = m_pLayers->GameLayer()->m_Width;
	m_Height = m_pLayers->GameLayer()->m_Height;
	m_pTiles = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->GameLayer()->m_Data));

	InitTeleports();

	if(m_pLayers->SpeedupLayer())
	{
		unsigned int Size = m_pLayers->Map()->GetDataSize(m_pLayers->SpeedupLayer()->m_Speedup);
		if(Size >= (size_t)m_Width * m_Height * sizeof(CSpeedupTile))
			m_pSpeedup = static_cast<CSpeedupTile *>(m_pLayers->Map()->GetData(m_pLayers->SpeedupLayer()->m_Speedup));
	}
}

void CCollision::InitTeleports()
{
	if(!m_pLayers->TeleLayer())
		return;

	// Init tele tiles
	unsigned int Size = m_pLayers->Map()->GetDataSize(m_pLayers->TeleLayer()->m_Tele);
	if(Size >= (size_t)m_Width * m_Height * sizeof(CTeleTile))
		m_pTele = static_cast<CTeleTile *>(m_pLayers->Map()->GetData(m_pLayers->TeleLayer()->m_Tele));

	// Init tele outs
	for(int i = 0; i < m_Width * m_Height; i++)
	{
		int Number = TeleLayer()[i].m_Number;
		int Type = TeleLayer()[i].m_Type;
		if(Number > 0)
		{
			int x = i % m_Width;
			int y = i / m_Width;

			if(Type == TILE_TELEOUT)
			{
				m_TeleOuts[Number].push_back(
					vec2(i % m_Width * 32 + 16, i / m_Width * 32 + 16));
			}
		}
	}
}

enum
{
	MR_DIR_HERE = 0,
	MR_DIR_RIGHT,
	MR_DIR_DOWN,
	MR_DIR_LEFT,
	MR_DIR_UP,
	NUM_MR_DIRS
};

static int GetMoveRestrictionsRaw(int Direction, int Tile, int Flags)
{
	Flags = Flags & (TILEFLAG_VFLIP | TILEFLAG_HFLIP | TILEFLAG_ROTATE);
	switch(Tile)
	{
	case TILE_STOP:
		switch(Flags)
		{
		case ROTATION_0: return CANTMOVE_DOWN;
		case ROTATION_90: return CANTMOVE_LEFT;
		case ROTATION_180: return CANTMOVE_UP;
		case ROTATION_270: return CANTMOVE_RIGHT;

		case TILEFLAG_HFLIP ^ ROTATION_0: return CANTMOVE_UP;
		case TILEFLAG_HFLIP ^ ROTATION_90: return CANTMOVE_RIGHT;
		case TILEFLAG_HFLIP ^ ROTATION_180: return CANTMOVE_DOWN;
		case TILEFLAG_HFLIP ^ ROTATION_270: return CANTMOVE_LEFT;
		}
		break;
	case TILE_STOPS:
		switch(Flags)
		{
		case ROTATION_0:
		case ROTATION_180:
		case TILEFLAG_HFLIP ^ ROTATION_0:
		case TILEFLAG_HFLIP ^ ROTATION_180:
			return CANTMOVE_DOWN | CANTMOVE_UP;
		case ROTATION_90:
		case ROTATION_270:
		case TILEFLAG_HFLIP ^ ROTATION_90:
		case TILEFLAG_HFLIP ^ ROTATION_270:
			return CANTMOVE_LEFT | CANTMOVE_RIGHT;
		}
		break;
	case TILE_STOPA:
		return CANTMOVE_LEFT | CANTMOVE_RIGHT | CANTMOVE_UP | CANTMOVE_DOWN;
	}
	return 0;
}

static int GetMoveRestrictionsMask(int Direction)
{
	switch(Direction)
	{
	case MR_DIR_HERE: return 0;
	case MR_DIR_RIGHT: return CANTMOVE_RIGHT;
	case MR_DIR_DOWN: return CANTMOVE_DOWN;
	case MR_DIR_LEFT: return CANTMOVE_LEFT;
	case MR_DIR_UP: return CANTMOVE_UP;
	default: dbg_assert(false, "invalid dir");
	}
	return 0;
}

static int GetMoveRestrictions(int Direction, int Tile, int Flags)
{
	int Result = GetMoveRestrictionsRaw(Direction, Tile, Flags);
	// Generally, stoppers only have an effect if they block us from moving
	// *onto* them. The one exception is one-way blockers, they can also
	// block us from moving if we're on top of them.
	if(Direction == MR_DIR_HERE && Tile == TILE_STOP)
	{
		return Result;
	}
	return Result & GetMoveRestrictionsMask(Direction);
}

int CCollision::GetMoveRestrictions(CALLBACK_SWITCHACTIVE pfnSwitchActive, void *pUser, vec2 Pos, float Distance, int OverrideCenterTileIndex)
{
	static const vec2 DIRECTIONS[NUM_MR_DIRS] =
		{
			vec2(0, 0),
			vec2(1, 0),
			vec2(0, 1),
			vec2(-1, 0),
			vec2(0, -1)};
	dbg_assert(0.0f <= Distance && Distance <= 32.0f, "invalid distance");
	int Restrictions = 0;
	for(int d = 0; d < NUM_MR_DIRS; d++)
	{
		vec2 ModPos = Pos + DIRECTIONS[d] * Distance;
		int ModMapIndex = GetPureMapIndex(ModPos);
		if(d == MR_DIR_HERE && OverrideCenterTileIndex >= 0)
		{
			ModMapIndex = OverrideCenterTileIndex;
		}
		for(int Front = 0; Front < 2; Front++)
		{
			int Tile;
			int Flags;
			if(!Front)
			{
				Tile = GetTileIndex(ModMapIndex);
				Flags = GetTileFlags(ModMapIndex);
			}
			else
			{
				// Tile = GetFTileIndex(ModMapIndex);
				// Flags = GetFTileFlags(ModMapIndex);
			}
			Restrictions |= ::GetMoveRestrictions(d, Tile, Flags);
		}
	}
	return Restrictions;
}

int CCollision::GetTile(int x, int y) const
{
	if(!m_pTiles)
		return 0;

	int Nx = clamp(x / 32, 0, m_Width - 1);
	int Ny = clamp(y / 32, 0, m_Height - 1);
	int pos = Ny * m_Width + Nx;

	int Index = m_pTiles[pos].m_Index;
	if(Index >= TILE_SOLID && Index <= TILE_NOLASER)
		return Index;
	return 0;
}

// TODO: rewrite this smarter!
int CCollision::IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const
{
	vec2 Pos1Pos0 = Pos1 - Pos0;
	float Distance = length(Pos1Pos0);
	int End(Distance+1);
	vec2 Last = Pos0;

	for(int i = 0; i < End; i++)
	{
		float a = i/Distance;
		vec2 Pos = Pos0 + Pos1Pos0 * a;
		if(CheckPoint(Pos.x, Pos.y))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return GetCollisionAt(Pos.x, Pos.y);
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

// TODO: OPT: rewrite this smarter!
void CCollision::MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces) const
{
	if(pBounces)
		*pBounces = 0;

	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;
	if(CheckPoint(Pos + Vel))
	{
		int Affected = 0;
		if(CheckPoint(Pos.x + Vel.x, Pos.y))
		{
			pInoutVel->x *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(CheckPoint(Pos.x, Pos.y + Vel.y))
		{
			pInoutVel->y *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(Affected == 0)
		{
			pInoutVel->x *= -Elasticity;
			pInoutVel->y *= -Elasticity;
		}
	}
	else
	{
		*pInoutPos = Pos + Vel;
	}
}

bool CCollision::TestBox(vec2 Pos, vec2 Size) const
{
	Size *= 0.5f;
	if(CheckPoint(Pos.x-Size.x, Pos.y-Size.y))
		return true;
	if(CheckPoint(Pos.x+Size.x, Pos.y-Size.y))
		return true;
	if(CheckPoint(Pos.x-Size.x, Pos.y+Size.y))
		return true;
	if(CheckPoint(Pos.x+Size.x, Pos.y+Size.y))
		return true;

	if(Size.x > 32)
	{
		// check with step 32
	}
	if(Size.y > 16)
	{
		int Y = 0;
		while(1)
		{
			Y += 30;
			if(Y / 2 > Size.y)
			{
				break;
			}
			if(CheckPoint(Pos.x - Size.x, Pos.y - Size.y + Y))
				return true;
			if(CheckPoint(Pos.x + Size.x, Pos.y - Size.y + Y))
				return true;
		}
	}

	return false;
}

void CCollision::MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, vec2 Elasticity, bool *pGrounded) const
{
	// do the move
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;

	float Distance = length(Vel);
	int Max = (int)Distance;

	if(Distance > 0.00001f)
	{
		float Fraction = 1.0f / (float)(Max + 1);
		float ElasticityX = clamp(Elasticity.x, -1.0f, 1.0f);
		float ElasticityY = clamp(Elasticity.y, -1.0f, 1.0f);

		for(int i = 0; i <= Max; i++)
		{
			// Early break as optimization to stop checking for collisions for
			// large distances after the obstacles we have already hit reduced
			// our speed to exactly 0.
			if(Vel == vec2(0, 0))
			{
				break;
			}

			vec2 NewPos = Pos + Vel*Fraction; // TODO: this row is not nice

			// Fraction can be very small and thus the calculation has no effect, no
			// reason to continue calculating.
			if(NewPos == Pos)
			{
				break;
			}

			if(TestBox(vec2(NewPos.x, NewPos.y), Size))
			{
				int Hits = 0;

				if(TestBox(vec2(Pos.x, NewPos.y), Size))
				{
					if(pGrounded && ElasticityY > 0 && Vel.y > 0)
						*pGrounded = true;
					NewPos.y = Pos.y;
					Vel.y *= -ElasticityY;
					Hits++;
				}

				if(TestBox(vec2(NewPos.x, Pos.y), Size))
				{
					NewPos.x = Pos.x;
					Vel.x *= -ElasticityX;
					Hits++;
				}

				// neither of the tests got a collision.
				// this is a real _corner case_!
				if(Hits == 0)
				{
					if(pGrounded && ElasticityY > 0 && Vel.y > 0)
						*pGrounded = true;
					NewPos.y = Pos.y;
					Vel.y *= -ElasticityY;
					NewPos.x = Pos.x;
					Vel.x *= -ElasticityX;
				}
			}

			Pos = NewPos;
		}
	}

	*pInoutPos = Pos;
	*pInoutVel = Vel;
}

void CCollision::Dest()
{
	m_pTiles = 0;
	m_Width = 0;
	m_Height = 0;
	m_pLayers = 0;
	m_pTele = 0;
	m_pSpeedup = 0;
}

bool CCollision::IsSolid(int x, int y) const
{
	int index = GetTile(x, y);
	return index == TILE_SOLID || index == TILE_NOHOOK;
}

int CCollision::IsSpeedup(int Index) const
{
	if(Index < 0 || !m_pSpeedup)
		return 0;

	if(m_pSpeedup[Index].m_Force > 0)
		return Index;

	return 0;
}

void CCollision::GetSpeedup(int Index, vec2 *Dir, int *Force, int *MaxSpeed) const
{
	if(Index < 0 || !m_pSpeedup)
		return;
	float Angle = m_pSpeedup[Index].m_Angle * (pi / 180.0f);
	*Force = m_pSpeedup[Index].m_Force;
	*Dir = vec2(cos(Angle), sin(Angle));
	if(MaxSpeed)
		*MaxSpeed = m_pSpeedup[Index].m_MaxSpeed;
}

int CCollision::GetPureMapIndex(float x, float y) const
{
	int Nx = clamp(round_to_int(x) / 32, 0, m_Width - 1);
	int Ny = clamp(round_to_int(y) / 32, 0, m_Height - 1);
	return Ny * m_Width + Nx;
}

bool CCollision::TileExists(int Index) const
{
	if(Index < 0)
		return false;

	if((m_pTiles[Index].m_Index >= TILE_FREEZE && m_pTiles[Index].m_Index <= TILE_TELE_LASER_DISABLE) || (m_pTiles[Index].m_Index >= TILE_LFREEZE && m_pTiles[Index].m_Index <= TILE_LUNFREEZE))
		return true;
	if(m_pTele && (m_pTele[Index].m_Type == TILE_TELEIN || m_pTele[Index].m_Type == TILE_TELEINEVIL || m_pTele[Index].m_Type == TILE_TELECHECKINEVIL || m_pTele[Index].m_Type == TILE_TELECHECK || m_pTele[Index].m_Type == TILE_TELECHECKIN))
		return true;
	if(m_pSpeedup && m_pSpeedup[Index].m_Force > 0)
		return true;
	return TileExistsNext(Index);
}

bool CCollision::TileExistsNext(int Index) const
{
	if(Index < 0)
		return false;
	int TileOnTheLeft = (Index - 1 > 0) ? Index - 1 : Index;
	int TileOnTheRight = (Index + 1 < m_Width * m_Height) ? Index + 1 : Index;
	int TileBelow = (Index + m_Width < m_Width * m_Height) ? Index + m_Width : Index;
	int TileAbove = (Index - m_Width > 0) ? Index - m_Width : Index;

	if((m_pTiles[TileOnTheRight].m_Index == TILE_STOP && m_pTiles[TileOnTheRight].m_Flags == ROTATION_270) || (m_pTiles[TileOnTheLeft].m_Index == TILE_STOP && m_pTiles[TileOnTheLeft].m_Flags == ROTATION_90))
		return true;
	if((m_pTiles[TileBelow].m_Index == TILE_STOP && m_pTiles[TileBelow].m_Flags == ROTATION_0) || (m_pTiles[TileAbove].m_Index == TILE_STOP && m_pTiles[TileAbove].m_Flags == ROTATION_180))
		return true;
	if(m_pTiles[TileOnTheRight].m_Index == TILE_STOPA || m_pTiles[TileOnTheLeft].m_Index == TILE_STOPA || ((m_pTiles[TileOnTheRight].m_Index == TILE_STOPS || m_pTiles[TileOnTheLeft].m_Index == TILE_STOPS)))
		return true;
	if(m_pTiles[TileBelow].m_Index == TILE_STOPA || m_pTiles[TileAbove].m_Index == TILE_STOPA || ((m_pTiles[TileBelow].m_Index == TILE_STOPS || m_pTiles[TileAbove].m_Index == TILE_STOPS) && m_pTiles[TileBelow].m_Flags | ROTATION_180 | ROTATION_0))
		return true;

	return false;
}

int CCollision::GetMapIndex(vec2 Pos) const
{
	int Nx = clamp((int)Pos.x / 32, 0, m_Width - 1);
	int Ny = clamp((int)Pos.y / 32, 0, m_Height - 1);
	int Index = Ny * m_Width + Nx;

	if(TileExists(Index))
		return Index;
	else
		return -1;
}

vec2 CCollision::GetPos(int Index) const
{
	if(Index < 0)
		return vec2(0, 0);

	int x = Index % m_Width;
	int y = Index / m_Width;
	return vec2(x * 32 + 16, y * 32 + 16);
}

int CCollision::GetTileIndex(int Index) const
{
	if(Index < 0)
		return 0;
	return m_pTiles[Index].m_Index;
}

int CCollision::GetTileFlags(int Index) const
{
	if(Index < 0)
		return 0;
	return m_pTiles[Index].m_Flags;
}

int CCollision::GetZoneHandle(const char* pName)
{
	if(!m_pLayers->ZoneGroup())
		return -1;
	
	int Handle = m_Zones.size();
	m_Zones.add(array<int>());
	
	array<int>& LayerList = m_Zones[Handle];
	
	char aLayerName[12];
	for(int l = 0; l < m_pLayers->ZoneGroup()->m_NumLayers; l++)
	{
		CMapItemLayer *pLayer = m_pLayers->GetLayer(m_pLayers->ZoneGroup()->m_StartLayer+l);
		
		if(pLayer->m_Type == LAYERTYPE_TILES)
		{
			CMapItemLayerTilemap *pTLayer = (CMapItemLayerTilemap *)pLayer;
			IntsToStr(pTLayer->m_aName, sizeof(aLayerName)/sizeof(int), aLayerName);
			if(str_comp(pName, aLayerName) == 0)
				LayerList.add(l);
		}
		else if(pLayer->m_Type == LAYERTYPE_QUADS)
		{
			CMapItemLayerQuads *pQLayer = (CMapItemLayerQuads *)pLayer;
			IntsToStr(pQLayer->m_aName, sizeof(aLayerName)/sizeof(int), aLayerName);
			if(str_comp(pName, aLayerName) == 0)
				LayerList.add(l);
		}
	}
	
	return Handle;
}

/* TEEUNIVERSE  BEGIN *************************************************/

inline bool SameSide(const vec2& l0, const vec2& l1, const vec2& p0, const vec2& p1)
{
	vec2 l0l1 = l1-l0;
	vec2 l0p0 = p0-l0;
	vec2 l0p1 = p1-l0;

	// This check helps in some cases but we still have fails in some others.
	// The fail is reproducible with infc_provence bells
	if((l0l1 == l0p0) || (l0l1 == l0p1) || (l0p0 == l0p1))
		return false;

	return sign(l0l1.x*l0p0.y - l0l1.y*l0p0.x) == sign(l0l1.x*l0p1.y - l0l1.y*l0p1.x);
}

//t0, t1 and t2 are position of triangle vertices
inline vec3 BarycentricCoordinates(const vec2& t0, const vec2& t1, const vec2& t2, const vec2& p)
{
    vec2 e0 = t1 - t0;
    vec2 e1 = t2 - t0;
    vec2 e2 = p - t0;
    
    float d00 = dot(e0, e0);
    float d01 = dot(e0, e1);
    float d11 = dot(e1, e1);
    float d20 = dot(e2, e0);
    float d21 = dot(e2, e1);
    float denom = d00 * d11 - d01 * d01;
    
    vec3 bary;
    bary.x = (d11 * d20 - d01 * d21) / denom;
    bary.y = (d00 * d21 - d01 * d20) / denom;
    bary.z = 1.0f - bary.x - bary.y;
    
    return bary;
}

template <typename V>
bool OutOfRange(V value, V q0, V q1, V q2, V q3)
{
	if(q0 > q1)
	{
		if(q2 > q3)
		{
			const V Min = minimum(q1, q3);
			if(value < Min)
				return true;
			const V Max = maximum(q0, q2);
			if(value > Max)
				return true;
		}
		else
		{
			const V Min = minimum(q1, q2);
			if(value < Min)
				return true;
			const V Max = maximum(q0, q3);
			if(value > Max)
				return true;
		}
	}
	else
	{
		// q1 is bigger than q0
		if(q2 > q3)
		{
			const V Min = minimum(q0, q3);
			if(value < Min)
				return true;
			const V Max = maximum(q1, q2);
			if(value > Max)
				return true;
		}
		else
		{
			// q3 is bigger than q2
			const V Min = minimum(q0, q2);
			if(value < Min)
				return true;
			const V Max = maximum(q1, q3);
			if(value > Max)
				return true;
		}
	}

	return false;
}

//t0, t1 and t2 are position of triangle vertices
inline bool InsideTriangle(const vec2& t0, const vec2& t1, const vec2& t2, const vec2& p)
{
    vec3 bary = BarycentricCoordinates(t0, t1, t2, p);
    return (bary.x >= 0.0f && bary.y >= 0.0f && bary.x + bary.y < 1.0f);
}

//q0, q1, q2 and q3 are position of quad vertices
inline bool InsideQuad(const vec2& q0, const vec2& q1, const vec2& q2, const vec2& q3, const vec2& p)
{
	return InsideTriangle(q0, q1, q2, p) || InsideTriangle(q1, q2, q3, p);
#if 0
	// SameSide() check is broken.
	if(SameSide(q1, q2, p, q0))
		return InsideTriangle(q0, q1, q2, p);
	else
		return InsideTriangle(q1, q2, q3, p);
#endif
}

/* TEEUNIVERSE END ****************************************************/

static void Rotate(vec2 *pCenter, vec2 *pPoint, float Rotation)
{
	float x = pPoint->x - pCenter->x;
	float y = pPoint->y - pCenter->y;
	pPoint->x = (x * cosf(Rotation) - y * sinf(Rotation) + pCenter->x);
	pPoint->y = (x * sinf(Rotation) + y * cosf(Rotation) + pCenter->y);
}

struct SAnimationTransformCache
{
	vec2 Position = vec2(0.0f, 0.f);
	float Angle = 0;
	int PosEnv = -1;
};

int CCollision::GetZoneValueAt(int ZoneHandle, float x, float y, ZoneData *pData)
{
	if(!m_pLayers->ZoneGroup())
		return 0;
	
	if(ZoneHandle < 0 || ZoneHandle >= m_Zones.size())
		return 0;
	
	int Index = 0;
	int ExtraData = 0;

	SAnimationTransformCache AnimationCache;
	
	for(int i = 0; i < m_Zones[ZoneHandle].size(); i++)
	{
		int l = m_Zones[ZoneHandle][i];
		
		CMapItemLayer *pLayer = m_pLayers->GetLayer(m_pLayers->ZoneGroup()->m_StartLayer+l);
		if(pLayer->m_Type == LAYERTYPE_TILES)
		{
			CMapItemLayerTilemap *pTLayer = (CMapItemLayerTilemap *)pLayer;
			
			CTile *pTiles = (CTile *) m_pLayers->Map()->GetData(pTLayer->m_Data);
			
			int Nx = clamp(round_to_int(x)/32, 0, pTLayer->m_Width-1);
			int Ny = clamp(round_to_int(y)/32, 0, pTLayer->m_Height-1);
			
			int TileIndex = (pTiles[Ny*pTLayer->m_Width+Nx].m_Index > 128 ? 0 : pTiles[Ny*pTLayer->m_Width+Nx].m_Index);
			if(TileIndex > 0)
				Index = TileIndex;
		}
		else if(pLayer->m_Type == LAYERTYPE_QUADS)
		{
			CMapItemLayerQuads *pQLayer = (CMapItemLayerQuads *)pLayer;
			
			const CQuad *pQuads = (const CQuad *) m_pLayers->Map()->GetDataSwapped(pQLayer->m_Data);

			for(int q = 0; q < pQLayer->m_NumQuads; q++)
			{
				vec2 Position(0.0f, 0.0f);
				float Angle = 0.0f;
				if(pQuads[q].m_PosEnv >= 0)
				{
					if(pQuads[q].m_PosEnv != AnimationCache.PosEnv)
					{
						AnimationCache.PosEnv = pQuads[q].m_PosEnv;
						GetAnimationTransform(m_Time, AnimationCache.PosEnv, m_pLayers, AnimationCache.Position, AnimationCache.Angle);
					}

					Position = AnimationCache.Position;
					Angle = AnimationCache.Angle;
				}
				
				vec2 p0 = Position + vec2(fx2f(pQuads[q].m_aPoints[0].x), fx2f(pQuads[q].m_aPoints[0].y));
				vec2 p1 = Position + vec2(fx2f(pQuads[q].m_aPoints[1].x), fx2f(pQuads[q].m_aPoints[1].y));
				vec2 p2 = Position + vec2(fx2f(pQuads[q].m_aPoints[2].x), fx2f(pQuads[q].m_aPoints[2].y));
				vec2 p3 = Position + vec2(fx2f(pQuads[q].m_aPoints[3].x), fx2f(pQuads[q].m_aPoints[3].y));
				
				if(Angle != 0)
				{
					vec2 center(fx2f(pQuads[q].m_aPoints[4].x), fx2f(pQuads[q].m_aPoints[4].y));
					Rotate(&center, &p0, Angle);
					Rotate(&center, &p1, Angle);
					Rotate(&center, &p2, Angle);
					Rotate(&center, &p3, Angle);
				}

				if(OutOfRange(x, p0.x, p1.x, p2.x, p3.x))
					continue;
				if(OutOfRange(y, p0.y, p1.y, p2.y, p3.y))
					continue;
				
				if(InsideQuad(p0, p1, p2, p3, vec2(x, y)))
				{
					Index = pQuads[q].m_ColorEnvOffset;
					ExtraData = pQuads[q].m_aColors[0].g;
				}
			}
		}
	}

	if(pData)
	{
		pData->Index = Index;
		pData->ExtraData = ExtraData;
	}
	
	return Index;
}

bool CCollision::AreConnected(vec2 Pos1, vec2 Pos2, float Radius) const
{
	if(distance(Pos1, Pos2) > Radius)
		return false;

	int TileRadius = (int)ceilf(Radius / 32.0f);
	int CenterX = TileRadius;
	int CenterY = TileRadius;
	int Width = 2 * TileRadius + 1;
	int Height = 2 * TileRadius + 1;
	std::vector<char> vMap(Width * Height);
	for(int j = 0; j < Height; j++)
	{
		for(int i = 0; i < Width; i++)
		{
			if(CheckPoint(Pos1.x + 32.0f * (i - CenterX), Pos1.y + 32.0f * (j - CenterY)))
				vMap[j * Width + i] = 0x0; // This tile can be checked
			else
				vMap[j * Width + i] = 0x1;
		}
	}

	vMap[CenterY * Width + CenterX] = 0x2; // This tile is checked

	int Pos2X = clamp(CenterX + (int)round((Pos2.x - Pos1.x) / 32.0f), 0, Width - 1);
	int Pos2Y = clamp(CenterY + (int)round((Pos2.y - Pos1.y) / 32.0f), 0, Height - 1);

	bool Changes = true;
	while(Changes)
	{
		Changes = false;
		for(int j = 0; j < Height; j++)
		{
			for(int i = 0; i < Width; i++)
			{
				if(vMap[j * Width + i] & 0x1 && !(vMap[j * Width + i] & 0x2))
				{
					if(i > 0 && (vMap[j * Width + (i - 1)] & 0x2))
					{
						vMap[j * Width + i] = 0x2;
						Changes = true;
					}
					if(j > 0 && (vMap[(j - 1) * Width + i] & 0x2))
					{
						vMap[j * Width + i] = 0x2;
						Changes = true;
					}
					if(i < Width - 1 && (vMap[j * Width + (i + 1)] & 0x2))
					{
						vMap[j * Width + i] = 0x2;
						Changes = true;
					}
					if(j < Height - 1 && (vMap[(j + 1) * Width + i] & 0x2))
					{
						vMap[j * Width + i] = 0x2;
						Changes = true;
					}
				}
			}
		}

		if(vMap[Pos2Y * Width + Pos2X] & 0x2)
		{
			return true;
		}
	}

	return false;
}
