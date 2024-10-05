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
	m_DamageType = EDamageType::NO_DAMAGE;
	m_MaxBounces = GameServer()->Tuning()->m_LaserBounceNum;
	m_BounceCost = GameServer()->Tuning()->m_LaserBounceCost;
}

CInfClassLaser::CInfClassLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg, EDamageType DamageType, bool Bounce) :
	CInfClassLaser(pGameContext, Pos, Direction, StartEnergy, Owner, Dmg, CGameWorld::ENTTYPE_LASER)
{
	m_DamageType = DamageType;
	GameWorld()->InsertEntity(this);

	if(Bounce)
	{
		DoBounce();
	}
}

bool CInfClassLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CInfClassCharacter *pOwnerChar = GameController()->GetCharacter(GetOwner());
	icArray<const CEntity*, 10> IgnoreHits;
	IgnoreHits.Add(pOwnerChar);
	CCharacter *pIntersect = GameWorld()->IntersectCharacter(m_Pos, To, 0.f, At, GetExceptEntitiesFilterFunction(IgnoreHits), m_Owner);
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

	if(m_DamageType == EDamageType::LOOPER_LASER)
	{
		const float EffectDurationInSeconds = Config()->m_InfSlowMotionGunDuration * 0.1f;
		pHit->SlowMotionEffect(EffectDurationInSeconds, GetOwner());
		GameServer()->SendEmoticon(pHit->GetCid(), EMOTICON_EXCLAMATION);
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

	if(GameServer()->Collision()->IntersectLineWeapon(m_Pos, To, 0x0, &To))
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

			m_Energy -= distance(m_From, m_Pos) + m_BounceCost;
			m_Bounces++;

			if(m_Bounces > m_MaxBounces)
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

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	CSnapContext Context(SnappingClientVersion);
	GameServer()->SnapLaserObject(Context, GetId(), m_Pos, m_From, m_EvalTick, GetOwner());
}

void CInfClassLaser::SetExplosive(bool Explosive)
{
	m_Explosive = Explosive;
}
