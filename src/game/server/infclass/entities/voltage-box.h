/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INFCLASS_ENTITIES_VOLTAGE_BOX_H
#define GAME_SERVER_INFCLASS_ENTITIES_VOLTAGE_BOX_H

#include "infcentity.h"

#include "base/tl/array_on_stack.h"

class CVoltageBox : public CInfCEntity
{
public:
	enum DISCHARGE_TYPE
	{
		DISCHARGE_TYPE_INVALID,
		DISCHARGE_TYPE_NORMAL,
		DISCHARGE_TYPE_FINAL,
		DISCHARGE_TYPE_FREE,
	};

	static int EntityId;
	static const int MaxLinks = 8;

	CVoltageBox(CGameContext *pGameContext, vec2 CenterPos, int Owner);
	~CVoltageBox() override;

	int GetCharges() const { return m_Charges; }

	void AddLink(int ClientID);
	void RemoveLink(int ClientID);
	void ScheduleDischarge(DISCHARGE_TYPE Type = DISCHARGE_TYPE_NORMAL);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

protected:
	void AddSnapItem(const vec2 &From, const vec2 &To, int SnapTick);
	void PrepareSnapItems();
	void PrepareBoxSnapItems();
	void PrepareActiveLinksSnapItems();
	void PrepareDischargedLinksSnapItems();

	void UpdateLinks();
	void DoDischarge();

	int GetStartTickForDistance(float Progress);

	enum
	{
		BoxEdges = 4,
	};

	struct CLaserSnapItem
	{
		ivec2 From;
		ivec2 To;
		int StartTick;
		int SnapID;
	};
	static const int MaxSnapItems = BoxEdges
			+ 1 // The box center
			+ MaxLinks * 2 // The links on discharge (two lasers per link)
			+ 1; // The active link to the owner showed at the same time as discharged links

	struct CLink
	{
		CLink() = default;
		CLink(const vec2 &E, int CID)
			: Endpoint(E)
			, ClientID(CID)
		{
		}

		vec2 Endpoint;
		int ClientID;
	};

	CLaserSnapItem m_LasersForSnap[MaxSnapItems];
	int m_ActiveSnapItems = 0;

	int m_Charges = 0;
	int m_DischargeFadingTick = 0;
	array_on_stack<CLink, MaxLinks> m_Links;
	array_on_stack<CLink, MaxLinks> m_DischargedLinks;
	DISCHARGE_TYPE m_ScheduledDischarge = DISCHARGE_TYPE_INVALID;
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_VOLTAGE_BOX_H
