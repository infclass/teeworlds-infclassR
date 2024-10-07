#ifndef ENGINE_SERVER_MAPCONVERTER_H
#define ENGINE_SERVER_MAPCONVERTER_H

#include <base/tl/array.h>
#include <engine/console.h>
#include <engine/map.h>
#include <engine/shared/datafile.h>
#include <engine/storage.h>
#include <game/gamecore.h>
#include <game/mapitems.h>

class CWeakSkinInfo;

class CMapConverter
{
public:
	enum
	{
		MENUCLASS_RANDOM = 0,
		MENUCLASS_ENGINEER,
		MENUCLASS_SOLDIER,
		MENUCLASS_SCIENTIST,
		MENUCLASS_BIOLOGIST,
		MENUCLASS_LOOPER,
		MENUCLASS_MEDIC,
		MENUCLASS_HERO,
		MENUCLASS_NINJA,
		MENUCLASS_MERCENARY,
		MENUCLASS_SNIPER,
		NUM_MENUCLASS,
	};

	enum
	{
		MASK_DEFENDER = 1,
		MASK_MEDIC = 2,
		MASK_HERO = 4,
		MASK_SUPPORT = 8,
		MASK_ALL = MASK_DEFENDER | MASK_MEDIC | MASK_HERO | MASK_SUPPORT,
	};

	enum
	{
		TIMESHIFT_MENUCLASS = 60,
		TIMESHIFT_MENUCLASS_MASK = NUM_MENUCLASS + 1,
	};

	CMapConverter(IStorage *pStorage, IEngineMap *pMap, IConsole *pConsole);
	~CMapConverter();

	bool Load();
	bool CreateMap(const char *pFilename);

	int GetTimeShiftUnit() const { return m_TimeShiftUnit; }

protected:
	IEngineMap *Map() { return m_pMap; }
	IStorage *Storage() { return m_pStorage; }
	IConsole *Console() { return m_pConsole; }

	void InitQuad(CQuad *pQuad);
	void InitQuad(CQuad *pQuad, vec2 Pos, vec2 Size);

	void CreateCircle(array<CQuad> *pQuads, vec2 Pos, float Size, vec4 Color, int Env = -1, int EnvTO = 0);

	void InitState();
	void QuantizeAnimation(int Quant);

	void CopyVersion();
	void CopyMapInfo();
	void CopyImages();
	void CopySounds();
	void CopyGameLayer();
	void CopyLayers();
	void CopyAnimations();

	void AddImageQuad(const char *pName, int ImageId, int GridX, int GridY, int X, int Y, int Width, int Height, vec2 Pos, vec2 Size, vec4 Color, int Env);
	void AddTeeLayer(const char *pName, int ImageId, vec2 Pos, float Size, int Env, bool Black, const CWeakSkinInfo &SkinInfo);
	int AddExternalImage(const char *pImageName, int Width, int Height);
	int AddEmbeddedImage(const char *pImageName, int Width, int Height, bool GrayScale);

	int Finalize();

protected:
	IStorage *m_pStorage;
	IEngineMap *m_pMap;
	IConsole *m_pConsole;
	CDataFileWriter m_DataFile;

	CTile *m_pTiles;
	CTile *m_pPhysicsLayerTiles;
	int m_Width;
	int m_Height;

	int m_NumGroups;
	int m_NumLayers;
	int m_NumImages;
	int m_NumSounds;
	int m_NumEnvs;
	array<CEnvPoint> m_lEnvPoints;

	vec2 m_MenuPosition;
	int m_AnimationCycle;
	int m_TimeShiftUnit;
};

#endif
