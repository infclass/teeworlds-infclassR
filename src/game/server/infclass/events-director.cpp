#include "events-director.h"

#include <engine/shared/config.h>

#include <game/server/classes.h>
#include <game/server/skininfo.h>

#include <vector>

static const char WinterSuffix[] = "_winter";

enum class EventType
{
	None,
	Winter,
};

static EventType PreloadedMapEventType = EventType::None;

const char *EventsDirector::GetMapConverterId(const char *pConverterId)
{
	static char CustomId[32] = { 0 };
	if(PreloadedMapEventType == EventType::Winter)
	{
		if(CustomId[0] == 0)
		{
			str_format(&CustomId[0], sizeof(CustomId), "%s%s", pConverterId, WinterSuffix);
		}
		return CustomId;
	}

	return pConverterId;
}

void EventsDirector::SetPreloadedMapName(const char *pName)
{
	PreloadedMapEventType = EventType::None;

	const int NameLength = str_length(pName);
	if(NameLength > sizeof(WinterSuffix))
	{
		int ExpectedSuffixOffset = NameLength - sizeof(WinterSuffix) + 1;
		if(str_comp(&pName[ExpectedSuffixOffset], WinterSuffix) == 0)
		{
			PreloadedMapEventType = EventType::Winter;
		}
	}
}

void EventsDirector::SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion)
{
	if(IsWinter())
	{
		// The skins added in PR: https://github.com/ddnet/ddnet/pull/1218
		bool ClientHasSantaSkins = DDNetVersion >= 11031;

		const bool IsHumanSkin = Context.PlayerClass < END_HUMANCLASS;
		const bool Customize = IsHumanSkin || (Context.PlayerClass == PLAYERCLASS_WITCH);

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

			for (const char *pSkin : SkinsWithSanta)
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
	bool CurrentEventWinter = str_comp(g_Config.m_InfEvent, "winter") == 0;
	if(CurrentEventWinter)
	{
		static char MapName[128];
		str_format(MapName, sizeof(MapName), "%s%s", pMapName, WinterSuffix);
		return MapName;
	}

	return pMapName;
}

bool EventsDirector::IsWinter()
{
	return PreloadedMapEventType == EventType::Winter;
}
