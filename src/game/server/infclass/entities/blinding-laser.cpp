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

	DoBounce();
}

bool CBlindingLaser::OnCharacterHit(CInfClassCharacter *pHit)
{
	pHit->MakeBlind(Config()->m_InfBlindnessDuration, GetOwner());
	return true;
}

void CBlindingLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLineWeapon(m_Pos, To, 0x0, &To))
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
