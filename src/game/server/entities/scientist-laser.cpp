/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "scientist-laser.h"


#include "white-hole.h"
#include "growingexplosion.h"

CScientistLaser::CScientistLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg)
: CLaser(pGameWorld, Pos, Direction, StartEnergy, Owner, Dmg, CGameWorld::ENTTYPE_LASER)
{
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CScientistLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar);
	if(!pHit)
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	
	return true;
}

void CScientistLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameServer()->m_World.DestroyEntity(this);
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
	
	GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_RIFLE, false, TAKEDAMAGEMODE_NOINFECTION);
	
	//Create a white hole entity
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(pOwnerChar && pOwnerChar->m_HasWhiteHole)
	{
		new CGrowingExplosion(GameWorld(), m_Pos, vec2(0.0, -1.0), m_Owner, 5, GROWINGEXPLOSIONEFFECT_BOOM_INFECTED);
		new CWhiteHole(GameWorld(), To, m_Owner);
		
		//Make it unavailable
		pOwnerChar->m_HasWhiteHole = false;
		pOwnerChar->m_HasIndicator = false;
		pOwnerChar->GetPlayer()->ResetNumberKills();
	}
}
