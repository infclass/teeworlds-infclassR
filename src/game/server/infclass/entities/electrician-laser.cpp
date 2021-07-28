#include "electrician-laser.h"

#include <engine/shared/config.h>

#include <game/server/infclass/classes/infcplayerclass.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/entities/voltage-box.h>
#include <game/server/infclass/infcgamecontroller.h>

CElectricianLaser::CElectricianLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, 0, CGameWorld::ENTTYPE_LASER)
{
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CElectricianLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CInfClassCharacter *pOwnerChar = GameController()->GetCharacter(GetOwner());
	CInfClassCharacter *pHit = static_cast<CInfClassCharacter*>(GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar));

	if(!pHit)
	{
		return false;
	}

	if(pHit->IsHuman())
	{
		return false;
	}

	m_From = From;
	m_Pos = At;
	m_Energy = -1;

	if(!pOwnerChar)
	{
		return true;
	}

	CVoltageBox *pBox = pOwnerChar->GetVoltageBox();

	if(!pBox)
	{
		return true;
	}

	static constexpr float Threshold = 16;
	if(distance(pHit->GetPos(), pBox->GetPos()) + Threshold >= Config()->m_InfVoltageBoxRange)
	{
		// TODO: say to the owner that the target is out of the hvbox range
		return true;
	}

	if(pHit->GetClass()->CanBeLinked())
	{
		pBox->AddLink(pHit->GetCID());
	}
	else
	{
		GameServer()->SendBroadcast_Localization(GetOwner(), BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE,
			_("The target can not be linked"), nullptr);
		return true;
	}

	return true;
}
