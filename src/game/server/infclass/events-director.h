#ifndef GAME_SERVER_INFCLASS_EVENTS_DIRECTOR_H
#define GAME_SERVER_INFCLASS_EVENTS_DIRECTOR_H

class CWeakSkinInfo;
class CSkinContext;

class EventsDirector
{
public:
	static const char *GetMapConverterId(const char *pConverterId);

	static void SetPreloadedMapName(const char *pName);
	static void SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion);
	static const char *GetEventMapName(const char *pMapName);

	static bool IsWinter();
};

#endif // GAME_SERVER_INFCLASS_EVENTS_DIRECTOR_H
