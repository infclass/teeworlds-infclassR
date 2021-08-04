/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "voltage-box.h"

#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/growingexplosion.h>
#include <game/server/infclass/infcgamecontroller.h>

#include "infccharacter.h"

int CVoltageBox::EntityId = CGameWorld::ENTTYPE_VOLTAGE_BOX;
static constexpr int BoxProximityRadius = 24;
static constexpr int ElectricDamage = 10;
static constexpr int LinkFieldRadius = 16;
static constexpr float ElectricityFreezeDuration = 1;

static constexpr int InvalidClientID = -1;
static const vec2 DamageForce(0, 0);

static const float DischargeAnimationDuration = 0.3;

CVoltageBox::CVoltageBox(CGameContext *pGameContext, vec2 CenterPos, int Owner)
	: CInfCEntity(pGameContext, EntityId, CenterPos, Owner, BoxProximityRadius)
{
	GameWorld()->InsertEntity(this);

	m_Charges = Config()->m_InfVoltageBoxCharges;

	for(CLaserSnapItem &SnapItem : m_LasersForSnap)
	{
		SnapItem.SnapID = Server()->SnapNewID();
	}

	m_Links.Clear();
	AddLink(Owner);
}

CVoltageBox::~CVoltageBox()
{
	for(const CLaserSnapItem &SnapItem : m_LasersForSnap)
	{
		Server()->SnapFreeID(SnapItem.SnapID);
	}
}

void CVoltageBox::AddLink(int ClientID)
{
	const CEntity *pCharacter = GameController()->GetCharacter(ClientID);
	if(!pCharacter)
	{
		// TODO: Warning Invalid ClientID
		return;
	}

	for(int i = 0; i < m_Links.Size(); ++i)
	{
		if(m_Links.At(i).ClientID == ClientID)
		{
			// Already linked
			return;
		}
	}

	if (m_Links.Capacity() == m_Links.Size())
	{
		// TODO: Warning
		return;
	}

	m_Links.Add(CLink(pCharacter->GetPos(), ClientID));
}

void CVoltageBox::RemoveLink(int ClientID)
{
	for(int i = 0; i < m_Links.Size(); ++i)
	{
		if(m_Links[i].ClientID != ClientID)
			continue;

		m_Links.RemoveAt(i);
	}
}

void CVoltageBox::ScheduleDischarge(DISCHARGE_TYPE Type)
{
	// The same type, do nothing
	if(m_ScheduledDischarge == Type)
		return;

	// If the final was scheduled then it's too late to do anything
	if(m_ScheduledDischarge == DISCHARGE_TYPE_FINAL)
		return;

	// If we need to set the final then set the final
	if(Type == DISCHARGE_TYPE_FINAL)
	{
		m_ScheduledDischarge = Type;
		return;
	}

	// We have a free discharge. Ignore NORMAL or further FREE discharging calls.
	if(m_ScheduledDischarge == DISCHARGE_TYPE_FREE)
		return;

	// If was NORMAL then it's OK to set it to NORMAL or FREE.
	m_ScheduledDischarge = Type;
}

void CVoltageBox::Tick()
{
	if(IsMarkedForDestroy())
		return;

	if(m_DischargeFadingTick > 0)
	{
		m_DischargeFadingTick--;
	}

	if(m_DischargeFadingTick == 0)
	{
		m_DischargedLinks.Clear();

		if(m_Charges <= 0)
		{
			Reset();
		}
	}

	UpdateActiveLinks();
	UpdateDischargedLinks();

	if(m_ScheduledDischarge != DISCHARGE_TYPE_INVALID)
	{
		DoDischarge();
	}

	PrepareSnapItems();
}

void CVoltageBox::TickPaused()
{
}

void CVoltageBox::Snap(int SnappingClient)
{
	if (NetworkClipped(SnappingClient))
		return;

	for(int i=0; i < m_ActiveSnapItems; i++)
	{
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(
			NETOBJTYPE_LASER, m_LasersForSnap[i].SnapID, sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = m_LasersForSnap[i].To.x;
		pObj->m_Y = m_LasersForSnap[i].To.y;
		pObj->m_FromX = m_LasersForSnap[i].From.x;
		pObj->m_FromY = m_LasersForSnap[i].From.y;
		pObj->m_StartTick = m_LasersForSnap[i].StartTick;
	}
}

void CVoltageBox::AddSnapItem(const vec2 &From, const vec2 &To, int SnapTick)
{
	m_LasersForSnap[m_ActiveSnapItems].From.x = From.x;
	m_LasersForSnap[m_ActiveSnapItems].From.y = From.y;
	m_LasersForSnap[m_ActiveSnapItems].To.x = To.x;
	m_LasersForSnap[m_ActiveSnapItems].To.y = To.y;
	m_LasersForSnap[m_ActiveSnapItems].StartTick = SnapTick;
	++m_ActiveSnapItems;
}

void CVoltageBox::PrepareSnapItems()
{
	m_ActiveSnapItems = 0;
	PrepareBoxSnapItems();
	PrepareActiveLinksSnapItems();
	PrepareDischargedLinksSnapItems();
}

void CVoltageBox::PrepareBoxSnapItems()
{
	static const vec2 Vertices[BoxEdges] =
	{
		vec2(-BoxProximityRadius, -BoxProximityRadius),
		vec2( BoxProximityRadius, -BoxProximityRadius),
		vec2( BoxProximityRadius,  BoxProximityRadius),
		vec2(-BoxProximityRadius,  BoxProximityRadius),
	};

	const int AnimationTicks = Server()->TickSpeed() * DischargeAnimationDuration;

	for(int i=0; i<BoxEdges; i++)
	{
		const bool Last = i == BoxEdges - 1;
		vec2 PartPosStart = Last ? m_Pos + Vertices[0] : m_Pos + Vertices[i + 1];
		vec2 PartPosEnd = m_Pos + Vertices[i];

		AddSnapItem(PartPosStart, PartPosEnd, Server()->Tick() - 4 + 4 * m_DischargeFadingTick / AnimationTicks);
	}

	AddSnapItem(m_Pos, m_Pos, Server()->Tick());
}

void CVoltageBox::PrepareActiveLinksSnapItems()
{
	const float MaxLength = Config()->m_InfVoltageBoxRange;

	ClientsArray DischargedClients;
	for(const CLink &Link : m_DischargedLinks)
	{
		if(Link.ClientID >= 0)
		{
			DischargedClients.Add(Link.ClientID);
		}
	}

	for(int i = 0; i < m_Links.Size(); ++i)
	{
		if(DischargedClients.Contains(m_Links.At(i).ClientID))
		{
			// We're already snapping the gfx for that client
			continue;
		}

		const vec2 &Endpoint = m_Links.At(i).Endpoint;
		float Distance = distance(Endpoint, GetPos());
		if (Distance > MaxLength)
		{
			Distance = MaxLength;
		}

		AddSnapItem(GetPos(), Endpoint, GetStartTickForDistance(Distance / MaxLength));
	}
}

void CVoltageBox::PrepareDischargedLinksSnapItems()
{
	if(m_DischargeFadingTick <= 0)
	{
		return;
	}

	const float MaxLength = Config()->m_InfVoltageBoxRange;
	const int TotalAnimationTicks = Server()->TickSpeed() * DischargeAnimationDuration;
	const float RemainingAnimation = m_DischargeFadingTick * 1.0 / TotalAnimationTicks;
	const float Thickness = LinkFieldRadius * 2 * RemainingAnimation;

	// Snap discharged links
	for(int i = 0; i < m_DischargedLinks.Size(); ++i)
	{
		const vec2 &Endpoint = m_DischargedLinks.At(i).Endpoint;

		float Distance = distance(Endpoint, GetPos());
		if (Distance > MaxLength)
		{
			Distance = MaxLength;
		}

		// direction
		vec2 Normalized = normalize(Endpoint - GetPos());
		vec2 dirVecT = vec2(Normalized.y, -Normalized.x);
		// Rotated by 90 degrees clockwise

		AddSnapItem(GetPos() + dirVecT * Thickness / 2,
					Endpoint + dirVecT * Thickness / 2,
					GetStartTickForDistance(0.2 + Distance / MaxLength));

		AddSnapItem(GetPos() - dirVecT * Thickness / 2,
					Endpoint - dirVecT * Thickness / 2,
					GetStartTickForDistance(0.2 + Distance / MaxLength));
	}
}

void CVoltageBox::UpdateActiveLinks()
{
	const float MaxLength = Config()->m_InfVoltageBoxRange;

	for(int i = 0; i < m_Links.Size(); ++i)
	{
		int ClientID = m_Links[i].ClientID;

		CInfClassCharacter *pCharacter = GameController()->GetCharacter(ClientID);
		if(!pCharacter || !pCharacter->IsAlive())
		{
			ScheduleDischarge(DISCHARGE_TYPE_FREE);
			continue;
		}

		if(pCharacter->IsHuman() && (pCharacter->GetCID() != GetOwner()))
		{
			// The character became a human (e.g. revived)
			RemoveLink(ClientID);
			continue;
		}

		vec2 NewPos = pCharacter->GetPos();
		float Distance = distance(NewPos, GetPos());

		if(Distance > MaxLength)
		{
			if(ClientID == GetOwner())
			{
				ScheduleDischarge(DISCHARGE_TYPE_FINAL);
			}
			else
			{
				ScheduleDischarge(DISCHARGE_TYPE_NORMAL);
			}

			// Continue to update the other links position
			continue;
		}

		m_Links[i].Endpoint = NewPos;
	}

	// Reveal the ghosts
	for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
	{
		if(p->GetPlayerClass() == PLAYERCLASS_GHOST)
		{
			for(const CLink &Link : m_Links)
			{
				const vec2 IntersectPos = closest_point_on_line(GetPos(), Link.Endpoint, p->GetPos());
				float Len = distance(p->GetPos(), IntersectPos);
				if(Len < (p->GetProximityRadius() + LinkFieldRadius))
				{
					p->MakeVisible();
				}
			}
		}
	}
}

void CVoltageBox::UpdateDischargedLinks()
{
	const float MaxLength = Config()->m_InfVoltageBoxRange;

	for(int i = 0; i < m_DischargedLinks.Size(); ++i)
	{
		int ClientID = m_DischargedLinks[i].ClientID;

		CInfClassCharacter *pCharacter = GameController()->GetCharacter(ClientID);
		if(!pCharacter || !pCharacter->IsAlive())
		{
			m_DischargedLinks[i].ClientID = -1; // Invalidate the link
			continue;
		}

		if(pCharacter->IsHuman() && (pCharacter->GetCID() != GetOwner()))
		{
			// The character became a human (e.g. revived)
			m_DischargedLinks[i].ClientID = -1; // Invalidate the link
			continue;
		}

		vec2 NewPos = pCharacter->GetPos();
		float Distance = distance(NewPos, GetPos());

		if(Distance > MaxLength)
		{
			m_DischargedLinks[i].ClientID = -1; // Invalidate the link
			continue;
		}

		m_DischargedLinks[i].Endpoint = NewPos;
	}
}

void CVoltageBox::DoDischarge()
{
	m_DischargeFadingTick = Server()->TickSpeed() * DischargeAnimationDuration;

	for(const CLink &Link : m_Links)
	{
		CInfClassCharacter *pCharacter = GameController()->GetCharacter(Link.ClientID);
		if(pCharacter && pCharacter->IsAlive())
		{
			GameServer()->CreateSound(Link.Endpoint, SOUND_LASER_FIRE);
		}
		else
		{
			GameServer()->CreateSound(Link.Endpoint, SOUND_LASER_BOUNCE);
		}
	}

	const int FreezeTicks = ElectricityFreezeDuration * Server()->TickSpeed();
	static const int BoxLightningRadiusTiles = 2;
	const float BoxDamageRadius = 32 * BoxLightningRadiusTiles;

	// Find other players on the links
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CInfClassCharacter *p = GameController()->GetCharacter(i);
		if(!p || !p->IsAlive())
			continue;

		if(p->IsHuman())
			continue;

		int Hits = 0;
		bool Animation = false;

		if(distance(GetPos(), p->GetPos()) < BoxDamageRadius + p->GetProximityRadius())
		{
			Hits = m_Links.Size();
		}
		else
		{
			for(const CLink &Link : m_Links)
			{
				const vec2 IntersectPos = closest_point_on_line(GetPos(), Link.Endpoint, p->GetPos());
				float Len = distance(p->GetPos(), IntersectPos);
				if(Len < (p->GetProximityRadius() + LinkFieldRadius))
				{
					Hits++;
				}
			}
			Animation = Hits;
		}

		if(Hits)
		{
			p->ElectricShock(ElectricityFreezeDuration, GetOwner());
			p->TakeDamage(DamageForce, ElectricDamage * Hits, GetOwner(), WEAPON_LASER, TAKEDAMAGEMODE_NOINFECTION);
		}

		if(Animation)
		{
			CGrowingExplosion *pExplosion = new CGrowingExplosion(GameServer(), p->GetPos(), vec2(0.0, -1.0), GetOwner(), 2, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
			pExplosion->SetDamage(0);
		}
	}

	CGrowingExplosion *pExplosion = new CGrowingExplosion(GameServer(), GetPos(), vec2(0.0, -1.0),
		GetOwner(), BoxLightningRadiusTiles, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
	pExplosion->SetDamage(0);

	switch(m_ScheduledDischarge)
	{
		case DISCHARGE_TYPE_INVALID:
			// TODO: Warning
			break;
		case DISCHARGE_TYPE_NORMAL:
			--m_Charges;
			break;
		case DISCHARGE_TYPE_FINAL:
			m_Charges = 0;
			break;
		case DISCHARGE_TYPE_FREE:
			break;
	}

	m_DischargedLinks = m_Links;
	m_Links.Clear();

	if(m_Charges > 0)
	{
		AddLink(GetOwner());
	}

	m_ScheduledDischarge = DISCHARGE_TYPE_INVALID;
}

int CVoltageBox::GetStartTickForDistance(float Progress)
{
	int Tick = Server()->Tick();
	int Correction = 0;

	Correction = 1; // Make the 1 step reduction the minimum
	if(Progress > 0.2)
	{
		Correction = 1;
	}
	if(Progress > 0.5)
	{
		Correction = 2;
	}
	if(Progress > 0.75)
	{
		Correction = 3;
	}
	if(Progress > 0.85)
	{
		Correction = 4;
	}
	if(Progress > 0.93)
	{
		Correction = 5;
	}

	return Tick - Correction;
}
