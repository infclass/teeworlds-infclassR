/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "merc-laser.h"

#include <game/server/gamecontext.h>

#include <engine/shared/config.h>

#include <game/server/infclass/entities/merc-bomb.h>
#include <game/server/infclass/infcgamecontroller.h>

static const int MercLaserDamage = 0;

static int FilterOwnerID = -1;

static bool OwnerFilter(const CEntity *pEntity)
{
	const CInfCEntity *pInfEntity = static_cast<const CInfCEntity *>(pEntity);
	return pInfEntity->GetOwner() == FilterOwnerID;
}

static CGameWorld::EntityFilter GetOwnerFilter(int Owner)
{
	FilterOwnerID = Owner;
	return OwnerFilter;
}

CMercenaryLaser::CMercenaryLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, MercLaserDamage, CGameWorld::ENTTYPE_LASER)
{
	GameWorld()->InsertEntity(this);
	CInfClassLaser::DoBounce();
}

bool CMercenaryLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CEntity *pHitMercBomb = GameWorld()->IntersectEntity(m_Pos, To, 80.0f, &At, CGameWorld::ENTTYPE_MERCENARY_BOMB, GetOwnerFilter(GetOwner()));
	if(pHitMercBomb)
	{
		CMercenaryBomb *pBomb = static_cast<CMercenaryBomb*>(pHitMercBomb);
		pBomb->Upgrade(1.5);

		m_From = From;
		m_Pos = At;
		m_Energy = -1;
		return true;
	}

	return false;
}

void CMercenaryLaser::DoBounce()
{
	GameWorld()->DestroyEntity(this);
}
