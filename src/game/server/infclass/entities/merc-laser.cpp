/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "merc-laser.h"

#include <game/server/gamecontext.h>

#include <engine/shared/config.h>

#include <game/server/infclass/classes/humans/human.h>
#include <game/server/infclass/entities/merc-bomb.h>
#include <game/server/infclass/infcgamecontroller.h>

static const int MercLaserDamage = 0;

CMercenaryLaser::CMercenaryLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, float UpgradePoints)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, MercLaserDamage, CGameWorld::ENTTYPE_LASER)
	, m_UpgradePoints(UpgradePoints)
{
	GameWorld()->InsertEntity(this);
	CInfClassLaser::DoBounce();
}

bool CMercenaryLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CEntity *pHitMercBomb = GameWorld()->IntersectEntity(m_Pos, To, 80.0f, &At, CMercenaryBomb::EntityId, GetOwnerFilterFunction());
	if(pHitMercBomb)
	{
		CMercenaryBomb *pBomb = static_cast<CMercenaryBomb*>(pHitMercBomb);
		CInfClassHuman *pMercClass = CInfClassHuman::GetInstance(GetOwnerClass());
		pMercClass->UpgradeMercBomb(pBomb, m_UpgradePoints);

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
