/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_LAYERS_H
#define GAME_LAYERS_H

class IKernel;
class IMap;

struct CMapItemGroup;
struct CMapItemGroupEx;
struct CMapItemLayer;
struct CMapItemLayerTilemap;

class CLayers
{
	int m_GroupsNum;
	int m_GroupsStart;
	int m_LayersNum;
	int m_LayersStart;
	CMapItemGroup *m_pGameGroup;
	CMapItemLayerTilemap *m_pGameLayer;
	class IMap *m_pMap;

public:
	CLayers();
	void Init(class IKernel *pKernel);
	int NumGroups() const { return m_GroupsNum; }
	int NumLayers() const { return m_LayersNum; }
	class IMap *Map() const { return m_pMap; }
	CMapItemGroup *GameGroup() const { return m_pGameGroup; }
	CMapItemLayerTilemap *GameLayer() const { return m_pGameLayer; }
	CMapItemGroup *GetGroup(int Index) const;
	CMapItemLayer *GetLayer(int Index) const;

	// DDRace

	CMapItemLayerTilemap *TeleLayer() const { return m_pTeleLayer; }
	CMapItemLayerTilemap *SpeedupLayer() const { return m_pSpeedupLayer; }

private:
	CMapItemLayerTilemap *m_pTeleLayer;
	CMapItemLayerTilemap *m_pSpeedupLayer;

	// InfClass

public:
	void Init(class IMap *pMap);

	CMapItemGroup *ZoneGroup() const { return m_pZoneGroup; }
	CMapItemGroup *EntityGroup() const { return m_pEntityGroup; }

private:
	CMapItemGroup *m_pZoneGroup;
	CMapItemGroup *m_pEntityGroup;
};

#endif
