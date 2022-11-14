/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "biologist-laser.h"

#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>

CBiologistLaser::CBiologistLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, int Owner, int Dmg) :
	CInfClassLaser(pGameContext, Pos, Direction, 400.0f, Owner, Dmg, CGameWorld::ENTTYPE_LASER)
{
	m_DamageType = DAMAGE_TYPE::BIOLOGIST_MINE;

	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CBiologistLaser::HitCharacter(vec2 From, vec2 To)
{
	for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
	{
		if(p->IsHuman())
			continue;

		vec2 IntersectPos;
		if(!closest_point_on_line(From, To, p->m_Pos, IntersectPos))
			continue;

		float Len = distance(p->m_Pos, IntersectPos);
		if(Len < p->m_ProximityRadius)
		{
			p->TakeDamage(vec2(0.f, 0.f), m_Dmg, m_Owner, DAMAGE_TYPE::BIOLOGIST_MINE);
			// Always return false to continue hits
			return false;
		}
	}

	return false;
}

void CBiologistLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		HitCharacter(m_Pos, To);
		
		// intersected
		m_From = m_Pos;
		m_Pos = To;

		vec2 TempPos = m_Pos;
		vec2 TempDir = m_Dir * 4.0f;

		GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
		m_Pos = TempPos;
		m_Dir = normalize(TempDir);

		m_Energy += 100.0f;
		m_Energy -= maximum(0.0f, distance(m_From, m_Pos));
		m_Bounces++;

		if(m_Bounces > 4)
			m_Energy = -1;

		GameServer()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);
	}
	else
	{
		HitCharacter(m_Pos, To);
		
		m_From = m_Pos;
		m_Pos = To;
		m_Energy = -1;
	}
}
