#include "events-director.h"

#include <base/system.h>

#include <engine/shared/config.h>

#include <game/infclass/classes.h>
#include <game/server/skininfo.h>

#include <vector>

enum class EventType
{
	None,
	Generic,
	Winter,
};

static EventType PreloadedMapEventType = EventType::None;

const char *EventsDirector::GetMapConverterId(const char *pConverterId)
{
	static char CustomConverterId[32] = {0};
	switch(PreloadedMapEventType)
	{
	case EventType::None:
	case EventType::Generic:
		break;
	case EventType::Winter:
		str_format(&CustomConverterId[0], sizeof(CustomConverterId), "%s_%s", pConverterId, g_Config.m_InfEvent);
		return CustomConverterId;
	}

	return pConverterId;
}

void EventsDirector::SetPreloadedMapName(const char *pName)
{
	const char *pEvent = g_Config.m_InfEvent;
	PreloadedMapEventType = EventType::None;

	if(pEvent[0])
	{
		if(str_endswith(pName, pEvent))
		{
			if(str_comp(pEvent, "winter") == 0)
			{
				PreloadedMapEventType = EventType::Winter;
			}
			else
			{
				PreloadedMapEventType = EventType::Generic;
			}
		}
	}
}

void EventsDirector::SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion)
{
	if(IsWinter())
	{
		// The skins added in PR: https://github.com/ddnet/ddnet/pull/1218
		bool ClientHasSantaSkins = DDNetVersion >= 11031;

		const bool Customize = IsHumanClass(Context.PlayerClass) || (Context.PlayerClass == EPlayerClass::Witch);

		if(ClientHasSantaSkins && Customize)
		{
			static const std::vector<const char *> SkinsWithSanta = {
				"santa_bluekitty",
				"santa_bluestripe",
				"santa_brownbear",
				"santa_cammo",
				"santa_cammostripes",
				"santa_coala",
				"santa_default",
				"santa_limekitty",
				"santa_pinky",
				"santa_redbopp",
				"santa_redstripe",
				"santa_saddo",
				"santa_toptri",
				"santa_twinbop",
				"santa_twintri",
				"santa_warpaint",
			};

			for(const char *pSkin : SkinsWithSanta)
			{
				const char *pSkinBaseName = &pSkin[6];
				if(str_comp(pOutput->pSkinName, pSkinBaseName) == 0)
				{
					pOutput->pSkinName = pSkin;
					break;
				}
			}
		}
	}
}

const char *EventsDirector::GetEventMapName(const char *pMapName)
{
	const char *pEvent = g_Config.m_InfEvent;
	if(pEvent[0])
	{
		static char MapName[128];
		str_format(MapName, sizeof(MapName), "%s_%s", pMapName, pEvent);
		return MapName;
	}

	return nullptr;
}

bool EventsDirector::IsWinter()
{
	return PreloadedMapEventType == EventType::Winter;
}
