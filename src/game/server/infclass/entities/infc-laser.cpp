/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "infc-laser.h"

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <engine/shared/config.h>
#include <engine/server/roundstatistics.h>

#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/entities/portal.h>
#include <game/server/infclass/infcgamecontroller.h>

CInfClassLaser::CInfClassLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, int ObjType)
	: CInfCEntity(pGameContext, ObjType, Pos, Owner)
{
	m_Dmg = Dmg;
	m_Energy = StartEnergy;
	m_Dir = Direction;
}

CInfClassLaser::CInfClassLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, Dmg, CGameWorld::ENTTYPE_LASER)
{
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CInfClassLaser::HitCharacter(vec2 From, vec2 To)
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

	if (pOwnerChar && pOwnerChar->GetPlayerClass() == PLAYERCLASS_MEDIC) { // Revive zombie
		CInfClassCharacter *medic = pOwnerChar;
		CInfClassCharacter *zombie = pHit;
		if (!zombie)
		{
			// Medic hits something else (not a zombie)
			return true;
		}
		const int MIN_ZOMBIES = 4;
		const int DAMAGE_ON_REVIVE = 17;
		int LastHumanClass = pHit->GetPlayer()->LastHumanClass();

		char aBuf[256];
		if (medic->GetPlayer()->GetCharacter() && medic->GetPlayer()->GetCharacter()->GetHealthArmorSum() <= DAMAGE_ON_REVIVE) {
			str_format(aBuf, sizeof(aBuf), "You need at least %d hp", DAMAGE_ON_REVIVE + 1);
			GameServer()->SendBroadcast(m_Owner, aBuf, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
		}
		else if (GameServer()->GetZombieCount() <= MIN_ZOMBIES) {
			str_format(aBuf, sizeof(aBuf), "Too few zombies (less than %d)", MIN_ZOMBIES+1);
			GameServer()->SendBroadcast(m_Owner, aBuf, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
		}
		else {
			zombie->GetPlayer()->SetClass(LastHumanClass);
			if (zombie->GetPlayer()->GetCharacter()) {
				zombie->GetPlayer()->GetCharacter()->SetHealthArmor(1, 0);
				zombie->Unfreeze();
				medic->TakeDamage(vec2(0.f, 0.f), DAMAGE_ON_REVIVE * 2, m_Owner, WEAPON_LASER, TAKEDAMAGEMODE_SELFHARM);
				str_format(aBuf, sizeof(aBuf), "Medic %s revived %s",
						Server()->ClientName(medic->GetPlayer()->GetCID()),
						Server()->ClientName(zombie->GetPlayer()->GetCID()));
				GameServer()->SendChatTarget(-1, aBuf);
				int ClientID = medic->GetPlayer()->GetCID();
				Server()->RoundStatistics()->OnScoreEvent(ClientID, SCOREEVENT_MEDIC_REVIVE, medic->GetPlayerClass(), Server()->ClientName(ClientID), GameServer()->Console());
			}
		}
		return true;
	}
	else if (pOwnerChar && pOwnerChar->GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		if(pMercenaryEntity)
		{
			pOwnerChar->m_BombHit = true;
			return true;
		}
	}

	if (pPortalEntity)
	{
		CPortal *pPortal = static_cast<CPortal*>(pPortalEntity);
		pPortal->TakeDamage(m_Dmg, m_Owner, WEAPON_LASER, TAKEDAMAGEMODE_NOINFECTION);
	}
	else
	{
		pHit->TakeDamage(vec2(0.f, 0.f), m_Dmg, m_Owner, WEAPON_LASER, TAKEDAMAGEMODE_NOINFECTION);

		if(pOwnerChar && pOwnerChar->GetPlayerClass() == PLAYERCLASS_LOOPER)
		{
			pHit->SlowMotionEffect(g_Config.m_InfSlowMotionGunDuration);
			if(Config()->m_InfSlowMotionGunDuration != 0)
				GameServer()->SendEmoticon(pHit->GetPlayer()->GetCID(), EMOTICON_EXCLAMATION);
		}
	}
	return true;
}

void CInfClassLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0 || m_BouncesStop)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;

			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameServer()->Tuning()->m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum)
				m_Energy = -1;

			GameServer()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);
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

void CInfClassLaser::Tick()
{
	if(Server()->Tick() > m_EvalTick+(Server()->TickSpeed()*GameServer()->Tuning()->m_LaserBounceDelay)/1000.0f)
		DoBounce();
}

void CInfClassLaser::TickPaused()
{
	++m_EvalTick;
}

void CInfClassLaser::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_From.x;
	pObj->m_FromY = (int)m_From.y;
	pObj->m_StartTick = m_EvalTick;
}
