#include <engine/server/mapconverter.h>

#include <base/color.h>

#include <engine/gfx/image_loader.h>
#include <engine/graphics.h>

#include <game/server/infclass/events-director.h>

#include <game/server/entities/character.h>

#include <game/collision.h>
#include <game/infclass/classes.h>
#include <game/layers.h>
#include <game/server/infclass/classes/humans/human.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/teeinfo.h>

#include <limits>

class CClientGameTileGetter
{
public:
	explicit CClientGameTileGetter(CCollision *pCollision);

	int GetClientGameTileIndex(int TileX, int TileY) const;

protected:
	CCollision *m_pCollision = nullptr;
	int m_ZoneHandle_icDamage = -1;
	int m_ZoneHandle_icBonus = -1;
};

CClientGameTileGetter::CClientGameTileGetter(CCollision *pCollision)
	: m_pCollision(pCollision)
{
	m_ZoneHandle_icDamage = m_pCollision->GetZoneHandle("icDamage");
	m_ZoneHandle_icBonus = m_pCollision->GetZoneHandle("icBonus");
}

int CClientGameTileGetter::GetClientGameTileIndex(int TileX, int TileY) const
{
	int W = m_pCollision->GetWidth();
	int GameLayerIndex = m_pCollision->GetTileIndex(TileY * W + TileX);
	switch(GameLayerIndex)
	{
	case TILE_SOLID:
	case TILE_NOHOOK:
	case TILE_ENTITIES_OFF_1:
	case TILE_ENTITIES_OFF_2:
		return GameLayerIndex;
	default:
		break;
	}

	float X = TileX * 32 + 16;
	float Y = TileY * 32 + 16;

	int icDamageIndex = m_pCollision->GetZoneValueAt(m_ZoneHandle_icDamage, X, Y);

	switch(icDamageIndex)
	{
	case ZONE_DAMAGE_DEATH:
		return TILE_DEATH;
	case ZONE_DAMAGE_DEATH_NOUNDEAD:
		return TILE_HIT_DISABLE;
	case ZONE_DAMAGE_DEATH_INFECTED:
		return TILE_DUNFREEZE;
	case ZONE_DAMAGE_INFECTION:
		return TILE_TELEINWEAPON;
	default:
		break;
	}

	int icBonusIndex = m_pCollision->GetZoneValueAt(m_ZoneHandle_icBonus, X, Y);
	switch(icBonusIndex)
	{
	case ZONE_BONUS_BONUS:
		return TILE_THROUGH_CUT;
	default:
		break;
	}

	return TILE_AIR;
}

int LoadPNG(CImageInfo *pImg, const char *pFilename)
{
	char aCompleteFilename[IO_MAX_PATH_LENGTH];
	IOHANDLE File = io_open(pFilename, IOFLAG_READ);
	if(File)
	{
		io_seek(File, 0, IOSEEK_END);
		unsigned int FileSize = io_tell(File);
		io_seek(File, 0, IOSEEK_START);

		TImageByteBuffer ByteBuffer;
		SImageByteBuffer ImageByteBuffer(&ByteBuffer);

		ByteBuffer.resize(FileSize);
		io_read(File, &ByteBuffer.front(), FileSize);

		io_close(File);

		uint8_t *pImgBuffer = NULL;
		EImageFormat ImageFormat;
		int PngliteIncompatible;
		if(::LoadPNG(ImageByteBuffer, pFilename, PngliteIncompatible, pImg->m_Width, pImg->m_Height, pImgBuffer, ImageFormat))
		{
			pImg->m_pData = pImgBuffer;

			if(ImageFormat == IMAGE_FORMAT_RGB) // ignore_convention
				pImg->m_Format = CImageInfo::FORMAT_RGB;
			else if(ImageFormat == IMAGE_FORMAT_RGBA) // ignore_convention
				pImg->m_Format = CImageInfo::FORMAT_RGBA;
			else
			{
				free(pImgBuffer);
				return 0;
			}

			if(PngliteIncompatible != 0)
			{
				dbg_msg("game/png", "\"%s\" is not compatible with pnglite and cannot be loaded by old DDNet versions: ", pFilename);
			}
		}
		else
		{
			dbg_msg("game/png", "image had unsupported image format. filename='%s'", pFilename);
			return 0;
		}
	}
	else
	{
		dbg_msg("game/png", "failed to open file. filename='%s'", pFilename);
		return 0;
	}

	return 1;
}

void FreePNG(CImageInfo *pImg)
{
	free(pImg->m_pData);
	pImg->m_pData = nullptr;
}

void MakeGrayScale(CImageInfo *pImg)
{
	static const int BodySize = 96;
	// From CSkins::LoadSkin()
	// game/client/components/skins.cpp:216
	unsigned char *d = (unsigned char *)pImg->m_pData;
	int Pitch = pImg->m_Width * 4;

	// create colorless version
	int Step = pImg->m_Format == CImageInfo::FORMAT_RGBA ? 4 : 3;

	// make the texture gray scale
	for(int i = 0; i < pImg->m_Width * pImg->m_Height; i++)
	{
		int v = (d[i * Step] + d[i * Step + 1] + d[i * Step + 2]) / 3;
		d[i * Step] = v;
		d[i * Step + 1] = v;
		d[i * Step + 2] = v;
	}

	int Freq[256] = {0};
	int OrgWeight = 0;
	int NewWeight = 192;

	// find most common frequence
	for(int y = 0; y < BodySize; y++)
		for(int x = 0; x < BodySize; x++)
		{
			if(d[y * Pitch + x * 4 + 3] > 128)
				Freq[d[y * Pitch + x * 4]]++;
		}

	for(int i = 1; i < 256; i++)
	{
		if(Freq[OrgWeight] < Freq[i])
			OrgWeight = i;
	}

	// reorder
	int InvOrgWeight = 255 - OrgWeight;
	int InvNewWeight = 255 - NewWeight;
	for(int y = 0; y < BodySize; y++)
	{
		for(int x = 0; x < BodySize; x++)
		{
			int v = d[y * Pitch + x * 4];
			if(v <= OrgWeight && OrgWeight == 0)
				v = 0;
			else if(v <= OrgWeight)
				v = (int)(((v / (float)OrgWeight) * NewWeight));
			else if(InvOrgWeight == 0)
				v = NewWeight;
			else
				v = (int)(((v - OrgWeight) / (float)InvOrgWeight) * InvNewWeight + NewWeight);
			d[y * Pitch + x * 4] = v;
			d[y * Pitch + x * 4 + 1] = v;
			d[y * Pitch + x * 4 + 2] = v;
		}
	}
}

void SetQuadColor(CQuad *Quad, int Color)
{
	ColorRGBA BodyColor = color_cast<ColorRGBA>(ColorHSLA(Color).UnclampLighting());
	CColor TypedColor;
	TypedColor.r = BodyColor.r * 255;
	TypedColor.g = BodyColor.g * 255;
	TypedColor.b = BodyColor.b * 255;
	TypedColor.a = BodyColor.a * 255;
	Quad->m_aColors[0] = TypedColor;
	Quad->m_aColors[1] = TypedColor;
	Quad->m_aColors[2] = TypedColor;
	Quad->m_aColors[3] = TypedColor;
}

CMapConverter::CMapConverter(IStorage *pStorage, IEngineMap *pMap, IConsole* pConsole) :
	m_pStorage(pStorage),
	m_pMap(pMap),
	m_pConsole(pConsole),
	m_pTiles(0)
{
}

CMapConverter::~CMapConverter()
{
	if(m_pTiles)
		delete[] m_pTiles;
}

// Work with 'long long' to properly handle overflows (later)
static inline long long gcd_long(long long a, long long b)
{
	while(b != 0)
	{
		long long c = a % b;
		a = b;
		b = c;
	}
	return a;
}

// Function to return LCM of two numbers
static inline long long lcm(long long a, long long b)
{
	return (a / gcd_long(a, b)) * b;
}

bool CMapConverter::Load()
{
	m_AnimationCycle = 1;
	m_MenuPosition = vec2(0.0f, 0.0f);
	
	//Find GameLayer	
	int LayersStart;
	int LayersNum;
	int GroupsStart;
	int GroupsNum;
	Map()->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	Map()->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
	
	CMapItemLayerTilemap *pPhysicsLayer = 0;
				
	//Find the gamelayer
	for(int g = 0; g < GroupsNum; g++)
	{
		CMapItemGroup* pGroup = (CMapItemGroup*) Map()->GetItem(GroupsStart+g, 0, 0);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = (CMapItemLayer*) Map()->GetItem(LayersStart+pGroup->m_StartLayer+l, 0, 0);
							
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTileLayer = reinterpret_cast<CMapItemLayerTilemap *>(pLayer);
				
				if(pTileLayer->m_Flags & TILESLAYERFLAG_GAME)
				{
					pPhysicsLayer = pTileLayer;
					break;
				}
			}
		}
		if(pPhysicsLayer)
			break;
	}
	
	if(!pPhysicsLayer)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "infclass", "no physics layer in loaded map");
		return false;
	}
		
	m_Width = pPhysicsLayer->m_Width;
	m_Height = pPhysicsLayer->m_Height;
	m_pPhysicsLayerTiles = (CTile*) m_pMap->GetData(pPhysicsLayer->m_Data);
	
	if(m_pTiles)
		delete[] m_pTiles;
		
	m_pTiles = new CTile[m_Width*m_Height];
	
	for(int j=0; j<m_Height; j++)
	{
		for(int i=0; i<m_Width; i++)
		{
			m_pTiles[j*m_Width+i].m_Flags = 0;
			m_pTiles[j*m_Width+i].m_Reserved = 0;
			
			int Skip = m_pPhysicsLayerTiles[j*m_Width+i].m_Skip;
			m_pTiles[j*m_Width+i].m_Skip = Skip;
			i += Skip;
		}
	}
	
	//Get the animation cycle
	CEnvPoint* pEnvPoints = NULL;
	{
		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVPOINTS, &Start, &Num);
		pEnvPoints = (CEnvPoint *)Map()->GetItem(Start, 0, 0);
	}
					
	const int MenuItemSyncMagic = TIMESHIFT_MENUCLASS * 1000;
	long long AnimationCircle = m_AnimationCycle;
	long long TimeShiftUnit = 0;
	if(pEnvPoints)
	{
		static const int Quant = 100;
		QuantizeAnimation(Quant);

		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
		for(int i = 0; i < Num; i++)
		{
			CMapItemEnvelope *pItem = (CMapItemEnvelope *)Map()->GetItem(Start+i, 0, 0);
			if(pItem->m_NumPoints > 0)
			{
				int Duration = pEnvPoints[pItem->m_StartPoint + pItem->m_NumPoints - 1].m_Time - pEnvPoints[pItem->m_StartPoint].m_Time;
				if(Duration)
				{
					dbg_msg("MapConverter", "Duration found: %d", Duration);
					AnimationCircle = lcm(AnimationCircle, Duration);
					dbg_msg("MapConverter", "New AnimationCycle: %lld", AnimationCircle);
				}
			}
		}

		dbg_msg("MapConverter", "Final AnimationCycle: %lld", AnimationCircle);
		if(AnimationCircle)
			TimeShiftUnit = AnimationCircle*((MenuItemSyncMagic / AnimationCircle)+1);
		else
			TimeShiftUnit = MenuItemSyncMagic;
	}

	int Multiplier = 1 + TIMESHIFT_MENUCLASS + 3 * ((MASK_ALL + 1) * TIMESHIFT_MENUCLASS_MASK);
	long long MaxUsedAnimationTime = Multiplier * TimeShiftUnit;
	long long MaxAvailable = std::numeric_limits<int>::max();

	if(MaxUsedAnimationTime > MaxAvailable)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "WARNING! The animation duration is off the limit: %lld/%lld", MaxUsedAnimationTime, MaxAvailable);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "MapConverter", aBuf);
	}
	else
	{
		dbg_msg("MapConverter", "The animation duration is within the limits: %lld/%lld", MaxUsedAnimationTime, MaxAvailable);
	}
	m_TimeShiftUnit = TimeShiftUnit;

	return true;
}

void CMapConverter::InitQuad(CQuad* pQuad)
{
	for (int i=0; i<5; i++) {
		pQuad->m_aPoints[i].x = 0;
		pQuad->m_aPoints[i].y = 0;
	}
	pQuad->m_aColors[0].r = pQuad->m_aColors[1].r = 255;
	pQuad->m_aColors[0].g = pQuad->m_aColors[1].g = 255;
	pQuad->m_aColors[0].b = pQuad->m_aColors[1].b = 255;
	pQuad->m_aColors[0].a = pQuad->m_aColors[1].a = 255;
	pQuad->m_aColors[2].r = pQuad->m_aColors[3].r = 255;
	pQuad->m_aColors[2].g = pQuad->m_aColors[3].g = 255;
	pQuad->m_aColors[2].b = pQuad->m_aColors[3].b = 255;
	pQuad->m_aColors[2].a = pQuad->m_aColors[3].a = 255;
	pQuad->m_aTexcoords[0].x = 0;
	pQuad->m_aTexcoords[0].y = 0;
	pQuad->m_aTexcoords[1].x = 1<<10;
	pQuad->m_aTexcoords[1].y = 0;
	pQuad->m_aTexcoords[2].x = 0;
	pQuad->m_aTexcoords[2].y = 1<<10;
	pQuad->m_aTexcoords[3].x = 1<<10;
	pQuad->m_aTexcoords[3].y = 1<<10;
	pQuad->m_PosEnv = -1;
	pQuad->m_PosEnvOffset = 0;
	pQuad->m_ColorEnv = -1;
	pQuad->m_ColorEnvOffset = 0;
}

void CMapConverter::InitQuad(CQuad* pQuad, vec2 Pos, vec2 Size)
{
	int X0 = f2fx(Pos.x-Size.x/2.0f);
	int X1 = f2fx(Pos.x+Size.x/2.0f);
	int XC = f2fx(Pos.x);
	int Y0 = f2fx(Pos.y-Size.y/2.0f);
	int Y1 = f2fx(Pos.y+Size.y/2.0f);
	int YC = f2fx(Pos.y);
	
	InitQuad(pQuad);
	pQuad->m_aPoints[0].x = pQuad->m_aPoints[2].x = X0;
	pQuad->m_aPoints[1].x = pQuad->m_aPoints[3].x = X1;
	pQuad->m_aPoints[0].y = pQuad->m_aPoints[1].y = Y0;
	pQuad->m_aPoints[2].y = pQuad->m_aPoints[3].y = Y1;
	pQuad->m_aPoints[4].x = XC;
	pQuad->m_aPoints[4].y = YC;
}

void CMapConverter::CreateCircle(array<CQuad>* pQuads, vec2 CenterPos, float Size, vec4 Color, int Env, int EnvTO)
{
	CQuad Quad;
	InitQuad(&Quad);
	Quad.m_aPoints[0].x = f2fx(CenterPos.x);
	Quad.m_aPoints[0].y = f2fx(CenterPos.y);
	Quad.m_aColors[0].r = Quad.m_aColors[1].r = Quad.m_aColors[2].r = Quad.m_aColors[3].r = Color.r*255.0f;
	Quad.m_aColors[0].g = Quad.m_aColors[1].g = Quad.m_aColors[2].g = Quad.m_aColors[3].g = Color.g*255.0f;
	Quad.m_aColors[0].b = Quad.m_aColors[1].b = Quad.m_aColors[2].b = Quad.m_aColors[3].b = Color.b*255.0f;
	Quad.m_aColors[0].a = Quad.m_aColors[1].a = Quad.m_aColors[2].a = Quad.m_aColors[3].a = Color.a*255.0f;
	
	Quad.m_ColorEnv = Env;
	Quad.m_ColorEnvOffset = EnvTO;
	
	float AngleStep = 360.0f / 32.0f;
	float AngleIter = AngleStep;
	vec2 RadiusVect = vec2(Size/2.0f, 0.0f);
	vec2 LastPos = CenterPos+RadiusVect;
	for(int i=0; i<32; i++)
	{
		vec2 PosMid = CenterPos+rotate(RadiusVect, AngleIter - AngleStep/2.0f);
		vec2 Pos = CenterPos+rotate(RadiusVect, AngleIter);
		
		Quad.m_aPoints[1].x = f2fx(LastPos.x);
		Quad.m_aPoints[1].y = f2fx(LastPos.y);
		Quad.m_aPoints[2].x = f2fx(Pos.x);
		Quad.m_aPoints[2].y = f2fx(Pos.y);
		Quad.m_aPoints[3].x = f2fx(PosMid.x);
		Quad.m_aPoints[3].y = f2fx(PosMid.y);
		Quad.m_aPoints[4].x = f2fx((PosMid.x+CenterPos.x)/2.0f);
		Quad.m_aPoints[4].y = f2fx((PosMid.y+CenterPos.y)/2.0f);
		
		pQuads->add(Quad);
		
		LastPos = Pos;
		AngleIter += AngleStep;
	}
}

void CMapConverter::AddImageQuad(const char* pName, int ImageId, int GridX, int GridY, int X, int Y, int Width, int Height, vec2 Pos, vec2 Size, vec4 Color, int Env)
{
	array<CQuad> aQuads;
	CQuad Quad;
	
	float StepX = 1.0f/GridX;
	float StepY = 1.0f/GridY;
	
	InitQuad(&Quad, Pos, Size);
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = f2fx(StepX * X);
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = f2fx(StepX * (X + Width));
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = f2fx(StepY * Y);
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = f2fx(StepY * (Y + Height));
	Quad.m_aColors[0].r = Quad.m_aColors[1].r = Quad.m_aColors[2].r = Quad.m_aColors[3].r = Color.r*255.0f;
	Quad.m_aColors[0].g = Quad.m_aColors[1].g = Quad.m_aColors[2].g = Quad.m_aColors[3].g = Color.g*255.0f;
	Quad.m_aColors[0].b = Quad.m_aColors[1].b = Quad.m_aColors[2].b = Quad.m_aColors[3].b = Color.b*255.0f;
	Quad.m_aColors[0].a = Quad.m_aColors[1].a = Quad.m_aColors[2].a = Quad.m_aColors[3].a = Color.a*255.0f;
	aQuads.add(Quad);
	
	CMapItemLayerQuads Item;
	Item.m_Version = 2;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_QUADS;
	Item.m_Image = ImageId;
	Item.m_NumQuads = aQuads.size();
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), pName);
	Item.m_Data = m_DataFile.AddDataSwapped(aQuads.size()*sizeof(CQuad), aQuads.base_ptr());
	
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
}

void CMapConverter::AddTeeLayer(const char* pName, int ImageId, vec2 Pos, float Size, int Env, bool Black, const CWeakSkinInfo &SkinInfo)
{
	array<CQuad> aQuads;
	CQuad Quad;
	
	//Body, Shadow
	InitQuad(&Quad, Pos+vec2(0.0f, -4.0f), vec2(Size, Size));
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = f2fx(96.0f/256.0f);
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = f2fx(192.0f/256.0f);
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 0;
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = f2fx(96.0f/128.0f);
	aQuads.add(Quad);
	
	//BackFeet, Shadow
	InitQuad(&Quad, Pos+vec2(-7.0f, 10.0f), vec2(Size, Size/2.0f));
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*192.0f/256.0f;
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f;
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*64.0f/128.0f;
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*96.0f/128.0f;
	aQuads.add(Quad);
	
	//FrontFeet, Shadow
	InitQuad(&Quad, Pos+vec2(7.0f, 10.0f), vec2(Size, Size/2.0f));
	Quad.m_ColorEnv = Env;
	Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*192.0f/256.0f;
	Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f;
	Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*64.0f/128.0f;
	Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*96.0f/128.0f;
	aQuads.add(Quad);
	
	//BackFeet, Color
	if(!Black)
	{
		InitQuad(&Quad, Pos+vec2(-7.0f, 10.0f), vec2(Size, Size/2.0f));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*192.0f/256.0f;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*32.0f/128.0f;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*64.0f/128.0f;
		aQuads.add(Quad);
		
		//Body, Color
		InitQuad(&Quad, Pos+vec2(0.0f, -4.0f), vec2(Size, Size));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 0;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f*96.0f/256.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 0;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*96.0f/128.0f;
		if(SkinInfo.UseCustomColor)
		{
			SetQuadColor(&Quad, SkinInfo.ColorBody);
		}
		aQuads.add(Quad);
		
		//FrontFeet, Color
		InitQuad(&Quad, Pos+vec2(7.0f, 10.0f), vec2(Size, Size/2.0f));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*192.0f/256.0f;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*32.0f/128.0f;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*64.0f/128.0f;
		if(SkinInfo.UseCustomColor)
		{
			SetQuadColor(&Quad, SkinInfo.ColorFeet);
		}
		aQuads.add(Quad);
		
		//Eyes
		vec2 Direction = normalize(vec2(1.0f, -0.5f));
		float EyeSeparation = (0.075f - 0.010f*absolute(Direction.x))*Size;
		vec2 Offset = vec2(Direction.x*0.125f, -0.05f+Direction.y*0.10f)*Size;
		
			//Left
		InitQuad(&Quad, Pos+vec2(Offset.x-EyeSeparation, Offset.y-4.0f), vec2(Size*0.40f, Size*0.40f));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*64.0f/256.0f;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f*96.0f/256.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*96.0f/128.0f;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*128.0f/128.0f;
		aQuads.add(Quad);
		
			//Right
		InitQuad(&Quad, Pos+vec2(Offset.x+EyeSeparation, Offset.y-4.0f), vec2(-Size*0.40f, Size*0.40f));
		Quad.m_ColorEnv = Env;
		Quad.m_aTexcoords[0].x = Quad.m_aTexcoords[2].x = 1024.0f*64.0f/256.0f;
		Quad.m_aTexcoords[1].x = Quad.m_aTexcoords[3].x = 1024.0f*96.0f/256.0f;
		Quad.m_aTexcoords[0].y = Quad.m_aTexcoords[1].y = 1024.0f*96.0f/128.0f;
		Quad.m_aTexcoords[2].y = Quad.m_aTexcoords[3].y = 1024.0f*128.0f/128.0f;
		aQuads.add(Quad);
	}
	
	CMapItemLayerQuads Item;
	Item.m_Version = Item.m_Layer.m_Version = 2;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_QUADS;
	Item.m_Image = ImageId;
	Item.m_NumQuads = aQuads.size();
	StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), pName);
	Item.m_Data = m_DataFile.AddDataSwapped(aQuads.size()*sizeof(CQuad), aQuads.base_ptr());
	
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
}

void CMapConverter::InitState()
{
	m_NumGroups = 0;
	m_NumLayers = 0;
	m_NumImages = 0;
	m_NumSounds = 0;
	m_NumEnvs = 0;
	m_lEnvPoints.clear();
}

void CMapConverter::QuantizeAnimation(int Quant)
{
	//Get the animation cycle
	CEnvPoint* pEnvPoints = NULL;
	{
		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVPOINTS, &Start, &Num);
		pEnvPoints = (CEnvPoint *)Map()->GetItem(Start, 0, 0);
	}

	if(!pEnvPoints)
		return;

	int Start, Num;
	Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
	for(int i = 0; i < Num; i++)
	{
		CMapItemEnvelope *pItem = (CMapItemEnvelope *)Map()->GetItem(Start+i, 0, 0);
		if(pItem->m_NumPoints > 0)
		{
			// Quant the points:
			for(int p = 0; p < pItem->m_NumPoints; ++p)
			{
				int PointOffest = pItem->m_StartPoint + p;
				int Time = pEnvPoints[PointOffest].m_Time;
				int S = Time / Quant;
				if((Time % Quant) >= Quant / 2)
				{
					++S;
				}
				int QuantedTime = S * Quant;
				if(Time != QuantedTime)
				{
					pEnvPoints[PointOffest].m_Time = QuantedTime;
				}
			}
		}
	}
}

void CMapConverter::CopyVersion()
{
	CMapItemVersion *pItem = (CMapItemVersion *)Map()->FindItem(MAPITEMTYPE_VERSION, 0);
	m_DataFile.AddItem(MAPITEMTYPE_VERSION, 0, sizeof(CMapItemVersion), pItem);
}

void CMapConverter::CopyMapInfo()
{
	CMapItemInfo *pItem = (CMapItemInfo *)Map()->FindItem(MAPITEMTYPE_INFO, 0);
	m_DataFile.AddItem(MAPITEMTYPE_INFO, 0, sizeof(CMapItemInfo), pItem);
}

void CMapConverter::CopyImages()
{
	int Start, Num;
	Map()->GetType(MAPITEMTYPE_IMAGE, &Start, &Num);
	for(int i = 0; i < Num; i++)
	{
		CMapItemImage *pItem = (CMapItemImage *)Map()->GetItem(Start+i, 0, 0);
		char *pName = (char *)Map()->GetData(pItem->m_ImageName);

		CMapItemImage ImageItem;
		ImageItem.m_Version = 1;
		ImageItem.m_Width = pItem->m_Width;
		ImageItem.m_Height = pItem->m_Height;
		ImageItem.m_External = pItem->m_External;
		ImageItem.m_ImageName = m_DataFile.AddData(str_length(pName)+1, pName);
		if(pItem->m_External)
			ImageItem.m_ImageData = -1;
		else
		{
			char *pData = (char *)Map()->GetData(pItem->m_ImageData);
			ImageItem.m_ImageData = m_DataFile.AddData(ImageItem.m_Width*ImageItem.m_Height*4, pData);
		}
		m_DataFile.AddItem(MAPITEMTYPE_IMAGE, m_NumImages++, sizeof(ImageItem), &ImageItem);

		// unload image
		Map()->UnloadData(pItem->m_ImageData);
		Map()->UnloadData(pItem->m_ImageName);
	}
}

void CMapConverter::CopySounds()
{
	int Start, Num;
	Map()->GetType(MAPITEMTYPE_SOUND, &Start, &Num);
	for(int i = 0; i < Num; i++)
	{
		CMapItemSound *pItem = (CMapItemSound *)Map()->GetItem(Start+i, 0, 0);
		char *pName = (char *)Map()->GetData(pItem->m_SoundName);

		CMapItemSound SoundItem;
		SoundItem.m_Version = 1;
		SoundItem.m_SoundName = m_DataFile.AddData(str_length(pName) + 1, pName);
		SoundItem.m_External = pItem->m_External;
		if(pItem->m_External)
			SoundItem.m_SoundData = -1;
		else
		{
			char *pData = (char *)Map()->GetData(pItem->m_SoundData);
			SoundItem.m_SoundData = m_DataFile.AddData(pItem->m_SoundDataSize, pData);
			SoundItem.m_SoundDataSize = pItem->m_SoundDataSize;
		}

		m_DataFile.AddItem(MAPITEMTYPE_SOUND, m_NumSounds++, sizeof(SoundItem), &SoundItem);

		// unload sound
		Map()->UnloadData(pItem->m_SoundData);
		Map()->UnloadData(pItem->m_SoundName);
	}
}

void CMapConverter::CopyAnimations()
{
	int NumEnvPoints = 0;
	CEnvPoint* pEnvPoints = NULL;
	{
		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVPOINTS, &Start, &Num);
		pEnvPoints = (CEnvPoint *)Map()->GetItem(Start, 0, 0);
	}
	
	if(pEnvPoints)
	{
		int Start, Num;
		Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);
		for(int i = 0; i < Num; i++)
		{
			CMapItemEnvelope *pItem = (CMapItemEnvelope *)Map()->GetItem(Start+i, 0, 0);
			CMapItemEnvelope Item = *pItem;
			m_DataFile.AddItem(MAPITEMTYPE_ENVELOPE, m_NumEnvs++, sizeof(Item), &Item);
			
			if(pItem->m_NumPoints > 0)
				NumEnvPoints = maximum(NumEnvPoints, pItem->m_StartPoint + pItem->m_NumPoints);
		}
		
		for(int i=0; i<NumEnvPoints; i++)
		{
			m_lEnvPoints.add(pEnvPoints[i]);
		}
	}
}

void CMapConverter::CopyGameLayer()
{
	CMapItemLayerTilemap Item;
	Item.m_Version = Item.m_Layer.m_Version = 3;
	Item.m_Layer.m_Flags = 0;
	Item.m_Layer.m_Type = LAYERTYPE_TILES;
	Item.m_Color.r = 255;
	Item.m_Color.g = 255;
	Item.m_Color.b = 255;
	Item.m_Color.a = 255;
	Item.m_ColorEnv = -1;
	Item.m_ColorEnvOffset = 0;
	Item.m_Width = m_Width;
	Item.m_Height = m_Height;
	Item.m_Flags = 1;
	Item.m_Image = -1;
	
	CLayers Layers;
	Layers.Init(Map());

	CCollision Collision;
	Collision.Init(&Layers);

	CClientGameTileGetter TilesGetter(&Collision);

	//Cleanup the game layer
	//This will make maps no more usable by a server, but the original ones are in the repository
	for(int j = 0; j < m_Height; j++)
	{
		for(int i = 0; i < m_Width; i++)
		{
			m_pTiles[j * m_Width + i].m_Index = TilesGetter.GetClientGameTileIndex(i, j);
		}
	}
	
	Item.m_Data = m_DataFile.AddData(m_Width * m_Height * sizeof(CTile), m_pTiles);
	StrToInts(Item.m_aName, sizeof(Item.m_aName) / sizeof(int), "Game");
	m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
}

void CMapConverter::CopyLayers()
{
	int LayersStart, LayersNum;
	Map()->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	int Start, Num;
	Map()->GetType(MAPITEMTYPE_GROUP, &Start, &Num);
	
	//Groups
	for(int g = 0; g < Num; g++)
	{
		CMapItemGroup *pGItem = (CMapItemGroup *)Map()->GetItem(Start+g, 0, 0);

		if(pGItem->m_Version != CMapItemGroup::CURRENT_VERSION)
			continue;

		CMapItemGroup GroupItem = *pGItem;
		GroupItem.m_StartLayer = m_NumLayers;
		
		int GroupLayers = 0;

		//Layers
		for(int l = 0; l < pGItem->m_NumLayers; l++)
		{
			const CMapItemLayer *pLayerItem = (const CMapItemLayer *)Map()->GetItem(LayersStart+pGItem->m_StartLayer+l, 0, 0);
			if(!pLayerItem)
				continue;

			if(pLayerItem->m_Type == LAYERTYPE_TILES)
			{
				const CMapItemLayerTilemap *pTilemapItem = (const CMapItemLayerTilemap *)pLayerItem;

				if(pTilemapItem->m_Flags & TILESLAYERFLAG_GAME)
				{
					CopyGameLayer();
					GroupLayers++;
				}
				else
				{
					void *pData = Map()->GetData(pTilemapItem->m_Data);

					CMapItemLayerTilemap LayerItem;
					LayerItem = *pTilemapItem;

					LayerItem.m_Data = m_DataFile.AddData(LayerItem.m_Width * LayerItem.m_Height * sizeof(CTile), pData);
					if(pTilemapItem->m_Flags & TILESLAYERFLAG_TELE)
					{
						pData = Map()->GetData(pTilemapItem->m_Tele);
						LayerItem.m_Tele = m_DataFile.AddData(LayerItem.m_Width * LayerItem.m_Height * sizeof(CTeleTile), pData);
					}

					m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(LayerItem), &LayerItem);

					Map()->UnloadData(pTilemapItem->m_Data);
					
					GroupLayers++;
				}
			}
			else if(pLayerItem->m_Type == LAYERTYPE_QUADS)
			{
				const CMapItemLayerQuads *pQuadsItem = (const CMapItemLayerQuads *)pLayerItem;

				
				CMapItemLayerQuads LayerItem;
				LayerItem = *pQuadsItem;
				if(LayerItem.m_NumQuads != 0)
				{
					void *pData = Map()->GetData(pQuadsItem->m_Data);
					LayerItem.m_Data = m_DataFile.AddData(LayerItem.m_NumQuads * sizeof(CQuad), pData);
				}
				else
				{
					// add dummy data for backwards compatibility
					// this allows the layer to be loaded with an empty array since m_NumQuads is 0 while saving
					CQuad Dummy{};
					LayerItem.m_Data = m_DataFile.AddDataSwapped(sizeof(CQuad), &Dummy);
				}
				m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(LayerItem), &LayerItem);

				Map()->UnloadData(pQuadsItem->m_Data);
				
				GroupLayers++;
			}
			else if(pLayerItem->m_Type == LAYERTYPE_SOUNDS)
			{
				const CMapItemLayerSounds *pSoundsItem = (const CMapItemLayerSounds *)pLayerItem;

				void *pData = Map()->GetData(pSoundsItem->m_Data);

				CMapItemLayerSounds LayerItem;
				LayerItem = *pSoundsItem;
				LayerItem.m_Data = m_DataFile.AddData(LayerItem.m_NumSources*sizeof(CSoundSource), pData);

				m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(LayerItem), &LayerItem);

				Map()->UnloadData(pSoundsItem->m_Data);

				GroupLayers++;
			}
		}
		
		GroupItem.m_NumLayers = GroupLayers;
		
		m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(GroupItem), &GroupItem);
	}
}

int CMapConverter::AddExternalImage(const char* pImageName, int Width, int Height)
{
	CMapItemImage Item;
	Item.m_Version = 1;
	Item.m_External = 1;
	Item.m_ImageData = -1;
	Item.m_ImageName = m_DataFile.AddData(str_length((char*)pImageName)+1, (char*)pImageName);
	Item.m_Width = Width;
	Item.m_Height = Height;
	m_DataFile.AddItem(MAPITEMTYPE_IMAGE, m_NumImages++, sizeof(Item), &Item);
	
	return m_NumImages-1;
}

int CMapConverter::AddEmbeddedImage(const char *pImageName, int Width, int Height, bool GrayScale)
{
	CImageInfo img;
	CImageInfo *pImg = &img;

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "data/mapres/%s.png", pImageName);

	if (!LoadPNG(pImg, aBuf)) {
		return -1;
	}

	if(GrayScale)
	{
		MakeGrayScale(pImg);
	}

	CMapItemImage Item;
	Item.m_Version = 1;

	Item.m_External = 0;
	Item.m_Width = Width;
	Item.m_Height = Height;
	Item.m_ImageName = m_DataFile.AddData(str_length((char*)pImageName)+1, (char*)pImageName);

	Item.m_Width = pImg->m_Width;
	Item.m_Height = pImg->m_Height;

	if(pImg->m_Format == CImageInfo::FORMAT_RGB)
	{
		// Convert to RGBA
		unsigned char *pDataRGBA = (unsigned char *)malloc((size_t)Item.m_Width * Item.m_Height * 4);
		unsigned char *pDataRGB = (unsigned char *)pImg->m_pData;
		for(int i = 0; i < Item.m_Width * Item.m_Height; i++)
		{
			pDataRGBA[i * 4] = pDataRGB[i * 3];
			pDataRGBA[i * 4 + 1] = pDataRGB[i * 3 + 1];
			pDataRGBA[i * 4 + 2] = pDataRGB[i * 3 + 2];
			pDataRGBA[i * 4 + 3] = 255;
		}
		Item.m_ImageData = m_DataFile.AddData(Item.m_Width * Item.m_Height * 4, pDataRGBA);
		free(pDataRGBA);
	}
	else
	{
		Item.m_ImageData = m_DataFile.AddData(Item.m_Width * Item.m_Height * 4, pImg->m_pData);
	}
	m_DataFile.AddItem(MAPITEMTYPE_IMAGE, m_NumImages++, sizeof(Item), &Item);

	FreePNG(pImg);

	return m_NumImages-1;
}

int CMapConverter::Finalize()
{
	int ClassImageId[NUM_MENUCLASS];

	int DDNetVersion = 14000;
	int InfClassVersion = 0;

	for(int ClassIndex = 0; ClassIndex < NUM_MENUCLASS; ++ClassIndex)
	{
		CSkinContext SkinContext;
		SkinContext.PlayerClass = CInfClassGameController::MenuClassToPlayerClass(ClassIndex);

		CWeakSkinInfo ClassTeeInfo;
		if(ClassIndex == MENUCLASS_RANDOM)
		{
			ClassTeeInfo.pSkinName = "warpaint";
		}
		else if(ClassIndex == MENUCLASS_NINJA)
		{
			// Special case
			ClassTeeInfo.pSkinName = "x_ninja";
		}
		else
		{
			CInfClassHuman::SetupSkin(SkinContext, &ClassTeeInfo, DDNetVersion, InfClassVersion);
		}

		EventsDirector::SetupSkin(SkinContext, &ClassTeeInfo, DDNetVersion, InfClassVersion);

		char SkinPath[96];
		str_format(SkinPath, sizeof(SkinPath), "../skins/%s", ClassTeeInfo.pSkinName);

		int ImageId = 0;
		if(ClassTeeInfo.UseCustomColor)
		{
			bool GrayScale = true;
			ImageId = AddEmbeddedImage(SkinPath, 256, 128, GrayScale);
			if(ImageId < 0)
			{
				dbg_msg("MapConverter", "Unable to access a player class skin file. Make sure "
					"that the game data (including 'data/skins') is correctly installed.");
				return -1;
			}
		}
		else
		{
			ImageId = AddExternalImage(SkinPath, 256, 128);
		}

		ClassImageId[ClassIndex] = ImageId;
	}

	//Menu
	
	const float MenuRadius = 196.0f;
	const float MenuAngleStart = -90.0;
	
	{
		const float MenuAngleStep = 360.0f/static_cast<float>(NUM_MENUCLASS);
		
			//Menu Group
		{
			CMapItemGroup Item;
			Item.m_Version = CMapItemGroup::CURRENT_VERSION;
			Item.m_ParallaxX = 0;
			Item.m_ParallaxY = 0;
			Item.m_OffsetX = 0;
			Item.m_OffsetY = 0;
			Item.m_StartLayer = m_NumLayers;
			Item.m_NumLayers = NUM_MENUCLASS+1; // not sure why +1 tho
			Item.m_UseClipping = 0;
			Item.m_ClipX = 0;
			Item.m_ClipY = 0;
			Item.m_ClipW = 0;
			Item.m_ClipH = 0;
			StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "#Generated");
			
			m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);
		}
			//Menu Layers
		{
			array<CQuad> aQuads;
			
			int HiddenValues[4];
			int NormalValues[4];
			int HighlightValues[4];
			
			HiddenValues[0] = 0;
			HiddenValues[1] = 0;
			HiddenValues[2] = 0;
			HiddenValues[3] = 0;
				
			//Two passes: one for circles, one for tees
			for(int pass=0; pass<2; pass++)
			{
				if(pass == 0)
				{
					NormalValues[0] = 0;
					NormalValues[1] = 0;
					NormalValues[2] = 0;
					NormalValues[3] = 500;
					
					HighlightValues[0] = 1000;
					HighlightValues[1] = 1000;
					HighlightValues[2] = 1000;
					HighlightValues[3] = 500;
				}
				else
				{
					NormalValues[0] = 1000;
					NormalValues[1] = 1000;
					NormalValues[2] = 1000;
					NormalValues[3] = 1000;
					
					HighlightValues[0] = 1000;
					HighlightValues[1] = 1000;
					HighlightValues[2] = 1000;
					HighlightValues[3] = 1000;
				}
				
				for(int i=0; i<NUM_MENUCLASS; i++) 
				{
					int ClassMask = 0;

					EPlayerClass PlayerClass = CInfClassGameController::MenuClassToPlayerClass(i);
					if(CInfClassGameController::IsDefenderClass(PlayerClass))
						ClassMask = MASK_DEFENDER;
					else if(CInfClassGameController::IsSupportClass(PlayerClass))
						ClassMask = MASK_SUPPORT;
					else if(PlayerClass == EPlayerClass::Medic)
						ClassMask = MASK_MEDIC;
					else if(PlayerClass == EPlayerClass::Hero)
						ClassMask = MASK_HERO;
					else
						ClassMask = MASK_DEFENDER; // For some reason the random class is also defender

					//Create Animation for enable/disable simulation
					{
						int StartPoint = m_lEnvPoints.size();
						int NbPoints = 0;
						
						{
							CEnvPoint Point;
							Point.m_Time = 0;
							Point.m_Curvetype = 0;
							Point.m_aValues[0] = HiddenValues[0];
							Point.m_aValues[1] = HiddenValues[1];
							Point.m_aValues[2] = HiddenValues[2];
							Point.m_aValues[3] = HiddenValues[3];
							m_lEnvPoints.add(Point);
							NbPoints++;
						}	
						{
							CEnvPoint Point;
							Point.m_Time = TIMESHIFT_MENUCLASS*m_TimeShiftUnit;
							Point.m_Curvetype = 0;
							Point.m_aValues[0] = NormalValues[0];
							Point.m_aValues[1] = NormalValues[1];
							Point.m_aValues[2] = NormalValues[2];
							Point.m_aValues[3] = NormalValues[3];
							m_lEnvPoints.add(Point);
							NbPoints++;
						}
						
						//Iterate over all combinaisons of class availabilities
						for(int j=0; j<=MASK_ALL; j++)
						{
							if(pass == 0 && ((ClassMask & j) || (ClassMask == -1))) //Highlight
							{
								{
									CEnvPoint Point;
									Point.m_Time = (TIMESHIFT_MENUCLASS+3*((i+1)+j*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
									Point.m_Curvetype = 0;
									Point.m_aValues[0] = HighlightValues[0];
									Point.m_aValues[1] = HighlightValues[1];
									Point.m_aValues[2] = HighlightValues[2];
									Point.m_aValues[3] = HighlightValues[3];
									m_lEnvPoints.add(Point);
									NbPoints++;
								}
								{
									CEnvPoint Point;
									Point.m_Time = (TIMESHIFT_MENUCLASS+3*((i+2)+j*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
									Point.m_Curvetype = 0;
									Point.m_aValues[0] = NormalValues[0];
									Point.m_aValues[1] = NormalValues[1];
									Point.m_aValues[2] = NormalValues[2];
									Point.m_aValues[3] = NormalValues[3];
									m_lEnvPoints.add(Point);
									NbPoints++;
								}
							}
							else if((ClassMask != -1) && ((ClassMask & j) == 0)) //Hide
							{
								{
									CEnvPoint Point;
									Point.m_Time = (TIMESHIFT_MENUCLASS+3*(j*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
									Point.m_Curvetype = 0;
									Point.m_aValues[0] = HiddenValues[0];
									Point.m_aValues[1] = HiddenValues[1];
									Point.m_aValues[2] = HiddenValues[2];
									Point.m_aValues[3] = HiddenValues[3];
									m_lEnvPoints.add(Point);
									NbPoints++;
								}
								{
									CEnvPoint Point;
									Point.m_Time = (TIMESHIFT_MENUCLASS+3*((j+1)*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
									Point.m_Curvetype = 0;
									Point.m_aValues[0] = NormalValues[0];
									Point.m_aValues[1] = NormalValues[1];
									Point.m_aValues[2] = NormalValues[2];
									Point.m_aValues[3] = NormalValues[3];
									m_lEnvPoints.add(Point);
									NbPoints++;
								}
							}
						}
						{
							CEnvPoint Point;
							Point.m_Time = (TIMESHIFT_MENUCLASS+3*((MASK_ALL+1)*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
							Point.m_Curvetype = 0;
							Point.m_aValues[0] = NormalValues[0];
							Point.m_aValues[1] = NormalValues[1];
							Point.m_aValues[2] = NormalValues[2];
							Point.m_aValues[3] = NormalValues[3];
							m_lEnvPoints.add(Point);
							NbPoints++;
						}
						{
							CEnvPoint Point;
							Point.m_Time = (1+TIMESHIFT_MENUCLASS+3*((MASK_ALL+1)*TIMESHIFT_MENUCLASS_MASK))*m_TimeShiftUnit;
							Point.m_Curvetype = 0;
							Point.m_aValues[0] = HiddenValues[0];
							Point.m_aValues[1] = HiddenValues[1];
							Point.m_aValues[2] = HiddenValues[2];
							Point.m_aValues[3] = HiddenValues[3];
							m_lEnvPoints.add(Point);
							NbPoints++;
						}
						
						CMapItemEnvelope Item;
						Item.m_Version = CMapItemEnvelope::CURRENT_VERSION;
						Item.m_Channels = 4;
						Item.m_StartPoint = StartPoint;
						Item.m_NumPoints = NbPoints;
						Item.m_Synchronized = 1;
						StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Menu Class Pages");
						m_DataFile.AddItem(MAPITEMTYPE_ENVELOPE, m_NumEnvs++, sizeof(Item), &Item);
					}
				
					//Create Circle
					if(pass == 0)
					{
						CreateCircle(&aQuads, m_MenuPosition+rotate(vec2(MenuRadius, 0.0f), MenuAngleStart+MenuAngleStep*i), 96.0f, vec4(1.0f, 1.0f, 1.0f, 0.5f), m_NumEnvs-1);
					}
					else
					{
						CWeakSkinInfo SkinInfo;
						vec2 Pos = m_MenuPosition+rotate(vec2(MenuRadius, 0.0f), MenuAngleStart+MenuAngleStep*i);
						if(i == MENUCLASS_RANDOM)
						{
							AddTeeLayer("Random", ClassImageId[i], Pos, 64.0f, m_NumEnvs-1, true, SkinInfo);
						}
						else
						{
							CSkinContext SkinContext;
							SkinContext.PlayerClass = PlayerClass;
							const char *pClassName = CInfClassGameController::GetClassDisplayName(PlayerClass);
							CInfClassHuman::SetupSkin(SkinContext, &SkinInfo, DDNetVersion, InfClassVersion);
							EventsDirector::SetupSkin(SkinContext, &SkinInfo, DDNetVersion, InfClassVersion);
							bool Black = false;
							AddTeeLayer(pClassName, ClassImageId[i], Pos, 64.0f, m_NumEnvs-1, Black, SkinInfo);
						}
					}
				}
			
				if(pass == 0)
				{
					CMapItemLayerQuads Item;
					Item.m_Version = 2;
					Item.m_Layer.m_Flags = 0;
					Item.m_Layer.m_Type = LAYERTYPE_QUADS;
					Item.m_Image = -1;
					Item.m_NumQuads = aQuads.size();
					StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "UIQuads");
					Item.m_Data = m_DataFile.AddDataSwapped(aQuads.size()*sizeof(CQuad), aQuads.base_ptr());
					
					m_DataFile.AddItem(MAPITEMTYPE_LAYER, m_NumLayers++, sizeof(Item), &Item);
				}
			}
		}
	}

	return 0;
}

bool CMapConverter::CreateMap(const char* pFilename)
{
	char aBuf[512];
	if(!m_DataFile.Open(Storage(), pFilename))
	{
		str_format(aBuf, sizeof(aBuf), "failed to open file '%s'...", pFilename);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "infclass", aBuf);
		return false;
	}
	
	InitState();
	
	CopyVersion();
	CopyMapInfo();
	CopySounds();
	CopyImages();
	CopyAnimations();
	
	//Game Group
	{
		CMapItemGroup Item;
		Item.m_Version = CMapItemGroup::CURRENT_VERSION;
		Item.m_ParallaxX = 100;
		Item.m_ParallaxY = 100;
		Item.m_OffsetX = 0;
		Item.m_OffsetY = 0;
		Item.m_StartLayer = m_NumLayers;
		Item.m_NumLayers = 1;
		Item.m_UseClipping = 0;
		Item.m_ClipX = 0;
		Item.m_ClipY = 0;
		Item.m_ClipW = 0;
		Item.m_ClipH = 0;
		StrToInts(Item.m_aName, sizeof(Item.m_aName)/sizeof(int), "Game");
		
		m_DataFile.AddItem(MAPITEMTYPE_GROUP, m_NumGroups++, sizeof(Item), &Item);
	}
	
	CopyLayers();
	
	if (Finalize() < 0)
	{
		return false;
	}
	
	m_DataFile.AddItem(MAPITEMTYPE_ENVPOINTS, 0, m_lEnvPoints.size()*sizeof(CEnvPoint), m_lEnvPoints.base_ptr());
	m_DataFile.Finish();
	
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "infclass", "highres map created");
	return true;
}
