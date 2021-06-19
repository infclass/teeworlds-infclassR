/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "scientist-laser.h"


#include "white-hole.h"
#include "growingexplosion.h"
#include "portal.h"

CScientistLaser::CScientistLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, Dmg, CGameWorld::ENTTYPE_LASER)
{
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CScientistLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pHit = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar);
	vec2 At2;
	CEntity *pPortalEntity = GameServer()->m_World.IntersectEntity(m_Pos, To, 0, &At2, CGameWorld::ENTTYPE_PORTAL);

	if(!pHit && !pPortalEntity)
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;

	if (pPortalEntity)
	{
		if (pHit && (distance(From, pHit->m_Pos) < distance(From, pPortalEntity->m_Pos)))
		{
			// The Character pHit is closer than the Portal. Nothing to do.
		}
		else
		{
			m_Pos = At2;
			CPortal *pPortal = static_cast<CPortal*>(pPortalEntity);
			pPortal->TakeDamage(m_Dmg, m_Owner, WEAPON_LASER, TAKEDAMAGEMODE_NOINFECTION);
			return true;
		}
	}

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
	
	GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_LASER, false, TAKEDAMAGEMODE_NOINFECTION);
	
	//Create a white hole entity
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(pOwnerChar && pOwnerChar->m_HasWhiteHole)
	{
		new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), m_Owner, 5, GROWINGEXPLOSIONEFFECT_BOOM_INFECTED);
		new CWhiteHole(GameServer(), To, m_Owner);
		
		//Make it unavailable
		pOwnerChar->m_HasWhiteHole = false;
		pOwnerChar->m_HasIndicator = false;
		pOwnerChar->GetPlayer()->ResetNumberKills();
	}
}
