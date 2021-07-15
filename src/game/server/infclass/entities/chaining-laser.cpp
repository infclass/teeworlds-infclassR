#include "chaining-laser.h"

#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>

int CChainingLaser::EntityId = CGameWorld::ENTTYPE_CHAINING_LASER;

static const vec2 DamageForce(0, 0);
static const int LinkAnimationSteps = 4;
static const float LinkAnimationStepDuration = 0.08;

CChainingLaser::CChainingLaser(CGameContext *pGameContext, const vec2 &Pos, vec2 Direction, float StartEnergy, int Owner)
	: CInfCEntity(pGameContext, EntityId, Pos, Owner)
{
	m_Direction = Direction;
	m_Energy = StartEnergy;

	for(LaserSnapItem &SnapItem : m_LasersForSnap)
	{
		SnapItem.SnapID = Server()->SnapNewID();
	}

	GameWorld()->InsertEntity(this);
}

CChainingLaser::~CChainingLaser()
{
	for(const LaserSnapItem &SnapItem : m_LasersForSnap)
	{
		Server()->SnapFreeID(SnapItem.SnapID);
	}
}

void CChainingLaser::Tick()
{
	if(m_Links.IsEmpty())
	{
		bool HasHit = GenerateThePath();

		const int TotalSteps = LinkAnimationSteps + m_Links.Size() - 1;
		m_DecayRemainingTicks = TotalSteps * std::round(LinkAnimationStepDuration * Server()->TickSpeed());
		if(!HasHit)
		{
			m_DecayRemainingTicks /= 2;
		}
	}
	else
	{
		if(m_DecayRemainingTicks)
		{
			--m_DecayRemainingTicks;
		}
		UpdateThePath();
	}

	if(m_Links.IsEmpty())
	{
		Reset();
	}
	PrepareSnapItems();
}

void CChainingLaser::Snap(int SnappingClient)
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

void CChainingLaser::AddSnapItem(const vec2 &From, const vec2 &To, int SnapTick)
{
	m_LasersForSnap[m_ActiveSnapItems].From.x = From.x;
	m_LasersForSnap[m_ActiveSnapItems].From.y = From.y;
	m_LasersForSnap[m_ActiveSnapItems].To.x = To.x;
	m_LasersForSnap[m_ActiveSnapItems].To.y = To.y;
	m_LasersForSnap[m_ActiveSnapItems].StartTick = SnapTick;
	++m_ActiveSnapItems;
}

void CChainingLaser::PrepareSnapItems()
{
	const int OneStepTicks = std::round(LinkAnimationStepDuration * Server()->TickSpeed());

	m_ActiveSnapItems = 0;

	const int TotalSteps = LinkAnimationSteps + m_Links.Size() - 1;
	const int RemainingDecaySteps = m_DecayRemainingTicks / OneStepTicks;
	const int CurrentDecayStep = TotalSteps - RemainingDecaySteps;
	const int CurrentTick = Server()->Tick();

	vec2 PreviousPoint = GetPos();
	for(int LinkIndex = 0; LinkIndex < m_Links.Size(); ++LinkIndex)
	{
		int Value = LinkIndex > CurrentDecayStep ? LinkIndex - CurrentDecayStep : CurrentDecayStep - LinkIndex;
		if(Value <= 6)
		{
			AddSnapItem(PreviousPoint, m_Links[LinkIndex].Endpoint, CurrentTick - Value);
		}
		PreviousPoint = m_Links[LinkIndex].Endpoint;
	}
}

static bool OnlyInfectedCharacterFilter(const CCharacter *pCharacter)
{
	const CInfClassCharacter *pInfCharacter = CInfClassCharacter::fromCharacter(pCharacter);
	return pInfCharacter->GetClass()->IsZombie();
}

bool CChainingLaser::GenerateThePath()
{
	int Tick = Server()->Tick();
	const int BaseDamage = Config()->m_InfChainingLaserBaseDamage;
	const int MaxBounceDistance = Config()->m_InfChainingLaserBounceMaxDistance;

	m_Links.Clear();

	vec2 To = GetPos() + m_Direction * m_Energy;

	// Limit the 'To' value.
	GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To);

	vec2 At;
	CInfClassCharacter *pOwnerChar = GameController()->GetCharacter(GetOwner());
	CInfClassCharacter *pHit = static_cast<CInfClassCharacter*>(GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, OnlyInfectedCharacterFilter));

	if(!pHit)
	{
		m_Links.Add(Link(To, Tick));
		return false;
	}

	int Damage = BaseDamage;

	ClientsArray LinkedClientIDs;

	vec2 LastLinkedPoint = At;

	// Add the exact hit point to Links for the correct gfx
	m_Links.Add(Link(LastLinkedPoint, Tick));
	LinkedClientIDs.Add(pHit->GetCID());
	pHit->TakeDamage(DamageForce, Damage, GetOwner(), WEAPON_SHOTGUN, TAKEDAMAGEMODE_NOINFECTION);
	Damage -= 1;

	while(Damage > 0)
	{
		// Bounce from the center of the last hit character (the charge comes from the very heart of tee)
		LastLinkedPoint = pHit->GetPos();

		ClientsArray ClientsInRange;
		GameController()->GetSortedTargetsInRange(LastLinkedPoint, MaxBounceDistance, LinkedClientIDs, &ClientsInRange);

		// 1. Find the nearest
		// 2. Try to hit.
		// 3. If can't hit - go on the list
		// 3. If no hits - abort
		pHit = nullptr;
		for(int i = 0; i < ClientsInRange.Size(); ++i)
		{
			const int ClientID = ClientsInRange.At(i);

			CInfClassCharacter *pCharacterNearby = GameController()->GetCharacter(ClientID);
			if(!pCharacterNearby->GetClass()->CanBeLinked())
			{
				continue;
			}

			const vec2 TargetPosition = pCharacterNearby->GetPos();
			float Distance = distance(LastLinkedPoint, TargetPosition);
			if(Distance > 0.1)
			{
				bool Intersected = GameServer()->Collision()->IntersectLine(LastLinkedPoint, TargetPosition, 0x0, &To);

				if(Intersected)
				{
					// Unable to hit through the walls
					continue;
				}
			}

			pHit = pCharacterNearby;
			m_Links.Add(Link(TargetPosition, Tick));
			LinkedClientIDs.Add(pHit->GetCID());
			pHit->TakeDamage(DamageForce, Damage, GetOwner(), WEAPON_SHOTGUN, TAKEDAMAGEMODE_NOINFECTION);
			Damage -= 1;
			break;
		}
		if(!pHit)
		{
			break;
		}
	}

	return true;
}

void CChainingLaser::UpdateThePath()
{
	if(!m_Links.IsEmpty())
	{
		if(m_DecayRemainingTicks == 0)
		{
			m_Links.Clear();
		}
	}
}
