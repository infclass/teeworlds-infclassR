/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include "merc-bomb.h"

#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>

#include "growingexplosion.h"
#include "infccharacter.h"

int CMercenaryBomb::EntityId = CGameWorld::ENTTYPE_MERCENARY_BOMB;

CMercenaryBomb::CMercenaryBomb(CGameContext *pGameContext, vec2 Pos, int Owner)
	: CPlacedObject(pGameContext, EntityId, Pos, Owner)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_MERCENARY_BOMB;
	GameWorld()->InsertEntity(this);
	m_LoadingTick = Server()->TickSpeed();
	m_Load = 0;

	for(int &ID : m_IDs)
	{
		ID = Server()->SnapNewID();
	}

	GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);
}

CMercenaryBomb::~CMercenaryBomb()
{
	for(int SnapId : m_IDs)
	{
		Server()->SnapFreeID(SnapId);
	}
}

void CMercenaryBomb::Upgrade(float Points)
{
	float MaxDamage = Config()->m_InfMercBombs;
	float NewDamage = minimum(MaxDamage, m_Load + Points);
	if(NewDamage <= m_Load)
	{
		return;
	}

	m_Load = NewDamage;

	GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);
}

void CMercenaryBomb::Tick()
{
	if(IsMarkedForDestroy())
		return;

	if(m_Load >= Config()->m_InfMercBombs && m_LoadingTick > 0)
		m_LoadingTick--;
	
	// Find other players
	CInfClassCharacter *pTriggerCharacter = nullptr;
	float ClosestLength = CCharacterCore::PhysicalSize() + GetMaxRadius();

	for(TEntityPtr<CInfClassCharacter> pChr = GameWorld()->FindFirst<CInfClassCharacter>(); pChr; ++pChr)
	{
		if(!pChr->IsZombie() || !pChr->CanDie())
			continue;

		float Len = distance(pChr->GetPos(), GetPos());

		if(Len < ClosestLength)
		{
			ClosestLength = Len;
			pTriggerCharacter = pChr;
		}
	}

	if(pTriggerCharacter)
	{
		Explode(pTriggerCharacter->GetCID());
	}
}

void CMercenaryBomb::Explode(int TriggeredBy)
{
	float Factor = static_cast<float>(m_Load) / Config()->m_InfMercBombs;

	if(m_Load > 1)
	{
		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
		CGrowingExplosion *pExplosion = new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), m_Owner, 16.0f * Factor, DAMAGE_TYPE::MERCENARY_BOMB);
		pExplosion->SetTriggeredBy(TriggeredBy);
	}

	GameWorld()->DestroyEntity(this);
}

bool CMercenaryBomb::IsReadyToExplode() const
{
	return m_LoadingTick <= 0;
}

float CMercenaryBomb::GetMaxRadius()
{
	return 80;
}

void CMercenaryBomb::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	//CPlayer* pClient = GameServer()->m_apPlayers[SnappingClient];
	//if(pClient->IsZombie()) // invisible for zombies
	//	return;

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;
	}

	float AngleStart = (2.0f * pi * Server()->Tick()/static_cast<float>(Server()->TickSpeed()))/10.0f;
	float AngleStep = 2.0f * pi / CMercenaryBomb::NUM_SIDE;
	float R = 50.0f * static_cast<float>(m_Load) / Config()->m_InfMercBombs;
	for(int i=0; i<CMercenaryBomb::NUM_SIDE; i++)
	{
		vec2 PosStart = m_Pos + vec2(R * cos(AngleStart + AngleStep*i), R * sin(AngleStart + AngleStep*i));

		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_IDs[i], sizeof(CNetObj_Pickup)));
		if(!pP)
			return;

		pP->m_X = (int)PosStart.x;
		pP->m_Y = (int)PosStart.y;
		pP->m_Type = POWERUP_HEALTH;
		pP->m_Subtype = 0;
	}

	if(SnappingClient == m_Owner && m_LoadingTick > 0)
	{
		R = GetMaxRadius();
		AngleStart = AngleStart*2.0f;
		for(int i=0; i<CMercenaryBomb::NUM_SIDE; i++)
		{
			vec2 PosStart = m_Pos + vec2(R * cos(AngleStart + AngleStep*i), R * sin(AngleStart + AngleStep*i));
			GameController()->SendHammerDot(PosStart, m_IDs[CMercenaryBomb::NUM_SIDE+i]);
		}
	}
}
