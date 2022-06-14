/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "infc-laser.h"

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <engine/shared/config.h>

#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>

CInfClassLaser::CInfClassLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, int ObjType)
	: CInfCEntity(pGameContext, ObjType, Pos, Owner)
{
	m_Dmg = Dmg;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_DamageType = DAMAGE_TYPE::NO_DAMAGE;
}

CInfClassLaser::CInfClassLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, DAMAGE_TYPE DamageType)
	: CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, Dmg, CGameWorld::ENTTYPE_LASER)
{
	m_DamageType = DamageType;
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CInfClassLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CInfClassCharacter *pOwnerChar = GameController()->GetCharacter(GetOwner());
	CCharacter *pIntersect = GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, pOwnerChar);
	CInfClassCharacter *pHit = CInfClassCharacter::GetInstance(pIntersect);

	if(!pHit)
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;

	return OnCharacterHit(pHit);
}

bool CInfClassLaser::OnCharacterHit(CInfClassCharacter *pHit)
{
	pHit->TakeDamage(vec2(0.f, 0.f), m_Dmg, m_Owner, m_DamageType);

	if(m_DamageType == DAMAGE_TYPE::LOOPER_LASER)
	{
		pHit->SlowMotionEffect(g_Config.m_InfSlowMotionGunDuration, GetOwner());
		if(Config()->m_InfSlowMotionGunDuration != 0)
			GameServer()->SendEmoticon(pHit->GetCID(), EMOTICON_EXCLAMATION);
	}

	return true;
}

void CInfClassLaser::DoBounce()
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
	if(NetworkClipped(SnappingClient) && NetworkClipped(SnappingClient, m_From))
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
