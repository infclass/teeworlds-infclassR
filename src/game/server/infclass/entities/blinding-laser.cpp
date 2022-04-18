#include "blinding-laser.h"

#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/server/infclass/infcgamecontroller.h>

#include "infccharacter.h"

CBlindingLaser::CBlindingLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, int Owner)
	: CInfClassLaser(pGameContext, Pos, Direction, 600, Owner, 0, CGameWorld::ENTTYPE_LASER)
{
	GameWorld()->InsertEntity(this);
	GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);

	DoBounce();
}

bool CBlindingLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CCharacter *pIntersect = GameServer()->m_World.IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar);
	CInfClassCharacter *pHit = CInfClassCharacter::GetInstance(pIntersect);

	if(!pHit)
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;

	pHit->MakeBlind(GetOwner(), Config()->m_InfBlindnessDuration / 1000.0);

	return true;
}

void CBlindingLaser::DoBounce()
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
}
