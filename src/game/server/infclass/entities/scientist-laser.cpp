/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "scientist-laser.h"

#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>

#include "white-hole.h"
#include "growingexplosion.h"
#include "infccharacter.h"

CScientistLaser::CScientistLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, Dmg, CGameWorld::ENTTYPE_LASER)
{
	m_DamageType = DAMAGE_TYPE::SCIENTIST_LASER;
	
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CScientistLaser::OnCharacterHit(CInfClassCharacter *pHit)
{
	return true;
}

void CScientistLaser::DoBounce()
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
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}

	GameController()->CreateExplosion(m_Pos, m_Owner, m_DamageType);
	CreateWhiteHole(GetPos(), To);
}

void CScientistLaser::CreateWhiteHole(const vec2 &CenterPos, const vec2 &To)
{
	// Create a white hole entity
	CInfClassCharacter *pOwnerChar = GetOwnerCharacter();
	if(!pOwnerChar || !pOwnerChar->m_HasWhiteHole)
	{
		return;
	}
	new CGrowingExplosion(GameServer(), CenterPos, vec2(0.0, -1.0), m_Owner, 5, DAMAGE_TYPE::WHITE_HOLE);
	new CWhiteHole(GameServer(), To, m_Owner);

	// Make it unavailable
	pOwnerChar->m_HasWhiteHole = false;
	pOwnerChar->m_HasIndicator = false;
}
