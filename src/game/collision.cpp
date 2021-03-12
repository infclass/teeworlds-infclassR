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

CCollision::CCollision()
{
	m_pTiles = 0;
	m_Width = 0;
	m_Height = 0;
	
	m_pLayers = 0;
	
	m_Time = 0.0;
}

CCollision::~CCollision()
{
	if(m_pTiles)
		delete[] m_pTiles;
	
	m_pTiles = 0;
}

void CCollision::Init(class CLayers *pLayers)
{
	m_pLayers = pLayers;
	
	m_Width = m_pLayers->PhysicsLayer()->m_Width;
	m_Height = m_pLayers->PhysicsLayer()->m_Height;
	CTile* pPhysicsTiles = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->PhysicsLayer()->m_Data));
	if(m_pTiles)
		delete[] m_pTiles;
	m_pTiles = new int[m_Width*m_Height];

	for(int i = 0; i < m_Width*m_Height; i++)
	{
		switch(pPhysicsTiles[i].m_Index)
		{
		case TILE_PHYSICS_SOLID:
			m_pTiles[i] = COLFLAG_SOLID;
			break;
		case TILE_PHYSICS_NOHOOK:
			m_pTiles[i] = COLFLAG_SOLID|COLFLAG_NOHOOK;
			break;
		default:
			m_pTiles[i] = 0x0;
			break;
		}
	}

	InitTeleports();
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

int CCollision::GetTile(int x, int y) const
{
	int Nx = clamp(x/32, 0, m_Width-1);
	int Ny = clamp(y/32, 0, m_Height-1);

	return m_pTiles[Ny*m_Width+Nx];
}

bool CCollision::IsTileSolid(int x, int y) const
{
	return GetTile(x, y)&COLFLAG_SOLID;
}

// TODO: rewrite this smarter!
int CCollision::IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const
{
	float Distance = distance(Pos0, Pos1);
	int End(Distance+1);
	vec2 Last = Pos0;

	for(int i = 0; i < End; i++)
	{
		float a = i/Distance;
		vec2 Pos = mix(Pos0, Pos1, a);
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
	return false;
}

void CCollision::MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity) const
{
	// do the move
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;

	float Distance = length(Vel);
	int Max = (int)Distance;

	if(Distance > 0.00001f)
	{
		float Fraction = 1.0f/(float)(Max+1);
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
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					Hits++;
				}

				if(TestBox(vec2(NewPos.x, Pos.y), Size))
				{
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
					Hits++;
				}

				// neither of the tests got a collision.
				// this is a real _corner case_!
				if(Hits == 0)
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
				}
			}

			Pos = NewPos;
		}
	}

	*pInoutPos = Pos;
	*pInoutVel = Vel;
}

/* INFECTION MODIFICATION START ***************************************/
bool CCollision::CheckPhysicsFlag(vec2 Pos, int Flag)
{
	return GetTile(Pos.x, Pos.y)&Flag;
}

/* INFECTION MODIFICATION END *****************************************/

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

int CCollision::GetZoneValueAt(int ZoneHandle, float x, float y)
{
	if(!m_pLayers->ZoneGroup())
		return 0;
	
	if(ZoneHandle < 0 || ZoneHandle >= m_Zones.size())
		return 0;
	
	int Index = 0;
	
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
					GetAnimationTransform(m_Time, pQuads[q].m_PosEnv, m_pLayers, Position, Angle);
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
				
				if(InsideQuad(p0, p1, p2, p3, vec2(x, y)))
				{
					Index = pQuads[q].m_ColorEnvOffset;
				}
			}
		}
	}
	
	return Index;
}

bool CCollision::AreConnected(vec2 Pos1, vec2 Pos2, float Radius)
{
	if(distance(Pos1, Pos2) > Radius)
		return false;
	
	int TileRadius = std::ceil(Radius/32.0f);
	int CenterX = TileRadius;
	int CenterY = TileRadius;
	int Width = 2*TileRadius+1;
	int Height = 2*TileRadius+1;
	char* pMap = new char[Width*Height];
	for(int j=0; j<Height; j++)
	{
		for(int i=0; i<Width; i++)
		{
			if(CheckPoint(Pos1.x + 32.0f*(i-CenterX), Pos1.y + 32.0f*(j-CenterY)))
				pMap[j*Width+i] = 0x0; //This tile can be checked
			else
				pMap[j*Width+i] = 0x1;
		}
	}
	
	pMap[CenterY*Width+CenterX] = 0x2; //This tile is checked
	
	int Pos2X = clamp(CenterX + (int)round((Pos2.x - Pos1.x)/32.0f), 0, Width-1);
	int Pos2Y = clamp(CenterY + (int)round((Pos2.y - Pos1.y)/32.0f), 0, Height-1);
	
	bool Changes = true;
	while(Changes)
	{
		Changes = false;
		for(int j=0; j<Height; j++)
		{
			for(int i=0; i<Width; i++)
			{
				if(pMap[j*Width+i]&0x1 && !(pMap[j*Width+i]&0x2))
				{
					if(i>0 && (pMap[j*Width+(i-1)]&0x2))
					{
						pMap[j*Width+i] = 0x2;
						Changes = true;
					}
					if(j>0 && (pMap[(j-1)*Width+i]&0x2))
					{
						pMap[j*Width+i] = 0x2;
						Changes = true;
					}
					if(i<Width-1 && (pMap[j*Width+(i+1)]&0x2))
					{
						pMap[j*Width+i] = 0x2;
						Changes = true;
					}
					if(j<Height-1 && (pMap[(j+1)*Width+i]&0x2))
					{
						pMap[j*Width+i] = 0x2;
						Changes = true;
					}
				}
			}
		}
		
		if(pMap[Pos2Y*Width+Pos2X]&0x2)
		{
			delete[] pMap;
			return true;
		}
	}	
	
	delete[] pMap;
	return false;
}

int CCollision::GetPureMapIndex(float x, float y)
{
	int Nx = clamp(round_to_int(x) / 32, 0, m_Width - 1);
	int Ny = clamp(round_to_int(y) / 32, 0, m_Height - 1);
	return Ny * m_Width + Nx;
}
