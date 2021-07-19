#include "medic-laser.h"

#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

#include "infccharacter.h"

CMedicLaser::CMedicLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, 0, CGameWorld::ENTTYPE_LASER)
{
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CMedicLaser::OnCharacterHit(CInfClassCharacter *pHit)
{
	CInfClassCharacter *pInfected = pHit;
	CInfClassCharacter *pMedic = GetOwnerCharacter();

	int MinimumHP = Config()->m_InfRevivalDamage + 1;
	int MinimumInfected = 5;

	if(pMedic->GetHealthArmorSum() < MinimumHP)
	{
		GameServer()->SendBroadcast_Localization(m_Owner, BROADCAST_PRIORITY_GAMEANNOUNCE,
			BROADCAST_DURATION_GAMEANNOUNCE,
			_("You need at least {int:MinimumHP} HP"),
			"MinimumHP", &MinimumHP,
			nullptr
		);
	}
	else if(GameServer()->GetZombieCount() < MinimumInfected)
	{
		GameServer()->SendBroadcast_Localization(m_Owner, BROADCAST_PRIORITY_GAMEANNOUNCE,
			BROADCAST_DURATION_GAMEANNOUNCE,
			_("Too few infected (less than {int:MinimumInfected})"),
			"MinimumInfected", &MinimumInfected,
			nullptr
		);
	}
	else
	{
		int LastHumanClass = pHit->GetPlayer()->LastHumanClass();
		pInfected->GetPlayer()->SetClass(LastHumanClass);
		pInfected->Unfreeze();
		pInfected->CancelSlowMotion();
		pInfected->SetHealthArmor(1, 0);
		const float ReviverHelperDuration = 45;
		pInfected->AddHelper(pMedic->GetCID(), ReviverHelperDuration);
		pMedic->TakeDamage(vec2(0.f, 0.f), Config()->m_InfRevivalDamage * 2, m_Owner, DAMAGE_TYPE::MEDIC_REVIVAL);

		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_HUMANS,
			_("Medic {str:MedicName} revived {str:RevivedName}"),
			"MedicName", Server()->ClientName(pMedic->GetCID()),
			"RevivedName", Server()->ClientName(pInfected->GetCID()),
			nullptr
		);
		int ClientID = pMedic->GetCID();
		Server()->RoundStatistics()->OnScoreEvent(ClientID, SCOREEVENT_MEDIC_REVIVE, pMedic->GetPlayerClass(), Server()->ClientName(ClientID), GameServer()->Console());
	}
	return true;
}