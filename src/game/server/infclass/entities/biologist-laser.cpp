/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "biologist-laser.h"

#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>

CBiologistLaser::CBiologistLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, int Owner, int Dmg) :
	CInfClassLaser(pGameContext, Pos, Direction, 400.0f, Owner, Dmg, CGameWorld::ENTTYPE_LASER)
{
	m_DamageType = EDamageType::BIOLOGIST_MINE;
	m_MaxBounces = 4;
	m_BounceCost = -100;

	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CBiologistLaser::HitCharacter(vec2 From, vec2 To)
{
	for(TEntityPtr<CInfClassCharacter> p = GameWorld()->FindFirst<CInfClassCharacter>(); p; ++p)
	{
		if(p->IsHuman())
			continue;

		vec2 IntersectPos;
		if(!closest_point_on_line(From, To, p->m_Pos, IntersectPos))
			continue;

		float Len = distance(p->m_Pos, IntersectPos);
		if(Len < p->m_ProximityRadius)
		{
			p->TakeDamage(vec2(0.f, 0.f), m_Dmg, m_Owner, EDamageType::BIOLOGIST_MINE);
			// Always return false to continue hits
			return false;
		}
	}

	return false;
}
