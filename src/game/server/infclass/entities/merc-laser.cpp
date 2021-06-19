/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "merc-laser.h"

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <engine/shared/config.h>
#include <engine/server/roundstatistics.h>

#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/entities/portal.h>
#include <game/server/infclass/infcgamecontroller.h>

static const int MercLaserDamage = 0;

CMercenaryLaser::CMercenaryLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, MercLaserDamage, CGameWorld::ENTTYPE_LASER)
{
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CMercenaryLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CInfClassCharacter *pOwnerChar = GameController()->GetCharacter(GetOwner());
	CInfClassCharacter *pHit = static_cast<CInfClassCharacter*>(GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar));
	vec2 PortalHitAt;
	CEntity *pPortalEntity = GameWorld()->IntersectEntity(m_Pos, To, 0, &PortalHitAt, CGameWorld::ENTTYPE_PORTAL);
	vec2 MercenaryBombHitAt;
	CEntity *pMercenaryEntity = GameWorld()->IntersectEntity(m_Pos, To, 80.0f, &MercenaryBombHitAt, CGameWorld::ENTTYPE_MERCENARY_BOMB);

	if (pHit && pPortalEntity)
	{
		if (distance(From, pHit->m_Pos) < distance(From, pPortalEntity->m_Pos))
		{
			// The Character pHit is closer than the Portal.
			pPortalEntity = nullptr;
		}
		else
		{
			pHit = nullptr;
			At = PortalHitAt;
		}
	}

	if (pOwnerChar && pOwnerChar->GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		m_BouncesStop = true;
		if(pMercenaryEntity)
		{
			At = MercenaryBombHitAt;
			pOwnerChar->m_AtMercBomb = MercenaryBombHitAt;
		}
		else
		{
			pOwnerChar->m_AtMercBomb = MercenaryBombHitAt;
			return false;
		}
	}
	else if (!pHit && !pPortalEntity)
	{
		return false;
	}

	m_From = From;
	m_Pos = At;
	m_Energy = -1;

	if (pOwnerChar && pOwnerChar->GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		if(pMercenaryEntity)
		{
			pOwnerChar->m_BombHit = true;
			return true;
		}
	}

	return true;
}
