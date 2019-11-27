/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include "portal.h"
#include "character.h"
#include "growingexplosion.h"

CPortal::CPortal(CGameWorld *pGameWorld, vec2 CenterPos, int OwnerClientID, PortalType Type)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PORTAL)
{
	m_Pos = CenterPos;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();
	m_Owner = OwnerClientID;
	m_Radius = 30.0f;
	m_ProximityRadius = m_Radius;
	m_PortalType = Type;

	for(int i = 0; i < NUM_IDS; i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}

	StartMeridiansVisualEffect();
}

CPortal::~CPortal()
{
	for(int i = 0; i < NUM_IDS; i++)
	{
		Server()->SnapFreeID(m_IDs[i]);
	}

	Disconnect();
}

void CPortal::Reset()
{
	GameServer()->m_World.DestroyEntity(this);
}

int CPortal::GetOwner() const
{
	return m_Owner;
}

int CPortal::GetNewEntitySound() const
{
	return SOUND_RIFLE_FIRE;
}

CPortal::PortalType CPortal::GetPortalType() const
{
	return m_PortalType;
}

void CPortal::ConnectPortal(CPortal *anotherPortal)
{
	if (m_AnotherPortal == anotherPortal)
	{
		return;
	}

	if (anotherPortal)
	{
		if (anotherPortal->m_PortalType == m_PortalType)
		{
			// Invalid call
			return;
		}
		anotherPortal->m_AnotherPortal = this;
		anotherPortal->m_ConnectedTick = Server()->Tick();
		m_ConnectedTick = Server()->Tick();
	}
	m_AnotherPortal = anotherPortal;
}

void CPortal::Disconnect()
{
	if (!m_AnotherPortal)
		return;

	m_AnotherPortal->m_AnotherPortal = nullptr;
	m_AnotherPortal = nullptr;
}

CPortal *CPortal::GetAnotherPortal() const
{
	return m_AnotherPortal;
}

void CPortal::TakeDamage(int Dmg, int From, int Weapon, int Mode)
{
	if (m_MarkedForDestroy)
	{
		return;
	}
	Explode(From);
}

void CPortal::Explode(int DetonatedBy)
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(GetOwner());
	if (pOwner)
	{
		pOwner->OnPortalDestroy(this);
	}

	new CGrowingExplosion(GameWorld(), m_Pos, vec2(0.0, -1.0), m_Owner, 6, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
	GameServer()->m_World.DestroyEntity(this);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "A portal destroyed by %s", Server()->ClientName(DetonatedBy));
	GameServer()->SendChatTarget(-1, aBuf);
}

void CPortal::StartParallelsVisualEffect()
{
	MoveParallelsParticles();
}

void CPortal::StartMeridiansVisualEffect()
{
	float Radius = 30;
	float RandomRadius, RandomAngle;
	float VecX, VecY;
	for(int i = 0; i < NUM_HINT; i++)
	{
		RandomRadius = random_float()*(Radius-4.0f);
		RandomAngle = 2.0f * pi * random_float();
		VecX = cos(RandomAngle);
		VecY = sin(RandomAngle);
		m_ParticlePos[i] = m_Pos + vec2(RandomRadius * VecX, RandomRadius * VecY);
		m_ParticleVec[i] = vec2(-VecX, -VecY);
	}
	// find out how long it takes for a particle to reach the mid
	RandomRadius = random_float()*(Radius-4.0f);
	RandomAngle = 2.0f * pi * random_float();
	VecX = cos(RandomAngle);
	VecY = sin(RandomAngle);
	vec2 ParticlePos = m_Pos + vec2(Radius * VecX, Radius * VecY);
	vec2 ParticleVec = vec2(-VecX, -VecY);
	vec2 VecMid;
	float Speed;
	int i=0;
	for ( ; i<500; i++) {
		VecMid = m_Pos - ParticlePos;
		Speed = m_ParticleStartSpeed * clamp(1.0f-length(VecMid)/Radius+0.5f, 0.0f, 1.0f);
		Speed *= GetSpeedMultiplier();
		ParticlePos += vec2(ParticleVec.x*Speed, ParticleVec.y*Speed);
		if (dot(VecMid, ParticleVec) <= 0)
			break;
		ParticleVec *= m_ParticleAcceleration;
	}
	m_ParticleStopTickTime = i;
}

void CPortal::Snap(int SnappingClient)
{
	for(int i = 0; i < NUM_IDS; i++)
	{
		CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDs[i], sizeof(CNetObj_Projectile)));
		if(pObj)
		{
			pObj->m_X = m_ParticlePos[i].x;
			pObj->m_Y = m_ParticlePos[i].y;
			pObj->m_VelX = 0;
			pObj->m_VelY = 0;
			pObj->m_StartTick = Server()->Tick();
			pObj->m_Type = WEAPON_HAMMER;
		}
	}
}

void CPortal::MoveParallelsParticles()
{
	static const float AngleStep = 2.0f * pi / NUM_SIDE;
	for(int i = 0; i < NUM_SIDE; i++)
	{
		vec2 PosStart = m_Pos + vec2(m_Radius * cos(m_Angle + AngleStep*i), m_Radius * sin(m_Angle + AngleStep * i));
		m_ParticlePos[NUM_HINT + i] = PosStart;
	}
	float AngleDelta = AngleStep / 24;
	AngleDelta *= GetSpeedMultiplier();

	switch (m_PortalType)
	{
		case PortalType::Disconnected:
			break;
		case PortalType::In:
			m_Angle += AngleDelta;
			break;
		case PortalType::Out:
			m_Angle -= AngleDelta;
			break;
	}

	if (m_Angle > 2.0f * pi)
	{
		m_Angle = 0;
	}
}

void CPortal::MoveMeridiansParticles()
{
	float Radius = 50;
	float MaxOutRadius = 80;
	float RandomAngle, Speed;
	float VecX, VecY;
	for(int i = 0; i < NUM_HINT; i++)
	{
		const vec2 ParticleRelativePos = m_Pos - m_ParticlePos[i];

		switch (GetPortalType())
		{
			case PortalType::Disconnected:
				if (dot(ParticleRelativePos, m_ParticleVec[i]) > 0)
				{
					Speed = m_ParticleStartSpeed * clamp(1.0f-length(ParticleRelativePos)/Radius+0.5f, 0.0f, 1.0f);
					Speed *= GetSpeedMultiplier();
					m_ParticlePos[i] += vec2(m_ParticleVec[i].x*Speed, m_ParticleVec[i].y*Speed);
				}
				break;
			case PortalType::In:
				Speed = m_ParticleStartSpeed * clamp(1.0f-length(ParticleRelativePos)/Radius+0.5f, 0.0f, 1.0f);
				Speed *= GetSpeedMultiplier();
				m_ParticlePos[i] += vec2(m_ParticleVec[i].x*Speed, m_ParticleVec[i].y*Speed);
				if (dot(ParticleRelativePos, m_ParticleVec[i]) <= 0)
				{
					RandomAngle = 2.0f * pi * random_float();
					VecX = cos(RandomAngle);
					VecY = sin(RandomAngle);
					m_ParticlePos[i] = m_Pos + vec2(Radius * VecX, Radius * VecY);
					m_ParticleVec[i] = vec2(-VecX, -VecY);
					continue;
				}
				break;
			case PortalType::Out:
				Speed = m_ParticleStartSpeed * clamp(length(ParticleRelativePos)/Radius+0.5f, 0.0f, 1.0f);
				Speed *= GetSpeedMultiplier();
				m_ParticlePos[i] += vec2(m_ParticleVec[i].x*Speed, m_ParticleVec[i].y*Speed);
				if (length(ParticleRelativePos) > MaxOutRadius)
				{
					RandomAngle = 2.0f * pi * random_float();
					VecX = cos(RandomAngle);
					VecY = sin(RandomAngle);
					m_ParticlePos[i] = m_Pos;
					m_ParticleVec[i] = vec2(VecX, VecY);
					continue;
				}
				break;
		}

		m_ParticleVec[i] *= m_ParticleAcceleration;
	}
}

void CPortal::TeleportCharacters()
{
	if (!m_AnotherPortal)
	{
		return;
	}
	const int readyTick = m_ConnectedTick + g_Config.m_InfPortalConnectionTime * Server()->TickSpeed();
	if (Server()->Tick() < readyTick)
	{
		return;
	}

	CCharacter *pOwner = GameServer()->GetPlayerChar(GetOwner());
	if (!pOwner)
	{
		return;
	}

	const vec2 TargetPos = m_AnotherPortal->m_Pos;
	for(CCharacter *pCharacter = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pCharacter; pCharacter = (CCharacter *)pCharacter->TypeNext())
	{
		const float Distance = distance(pCharacter->m_Pos, m_Pos);
		if(Distance > pCharacter->m_ProximityRadius + m_Radius)
			continue;

		if (!pOwner->ProcessCharacterOnPortal(this, pCharacter))
			continue;

		// Teleport the character
		pCharacter->m_Core.m_Pos = TargetPos;
		pCharacter->m_Core.m_Vel *= 0;
		pCharacter->m_Core.m_HookedPlayer = -1;
		pCharacter->m_Core.m_HookState = HOOK_RETRACTED;
		pCharacter->m_Core.m_HookPos = TargetPos;

		GameServer()->CreateDeath(TargetPos, m_Owner);
		GameServer()->CreateSound(TargetPos, SOUND_PLAYER_JUMP);
		GameServer()->CreateSound(TargetPos, SOUND_PLAYER_SPAWN);
	}
}

float CPortal::GetSpeedMultiplier()
{
	return m_AnotherPortal ? 1 : 0.20;
}

void CPortal::Tick()
{
	if(m_MarkedForDestroy) return;

	MoveParallelsParticles();
	MoveMeridiansParticles();
	TeleportCharacters();
}

void CPortal::TickPaused()
{
	++m_StartTick;
}
