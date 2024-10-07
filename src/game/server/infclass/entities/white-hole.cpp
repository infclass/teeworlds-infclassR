/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include "white-hole.h"

#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include <game/server/infclass/classes/humans/human.h>
#include <game/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

#include "growingexplosion.h"
#include "infccharacter.h"

int CWhiteHole::EntityId = CGameWorld::ENTTYPE_WHITE_HOLE;

CWhiteHole::CWhiteHole(CGameContext *pGameContext, vec2 CenterPos, int Owner)
	: CInfCEntity(pGameContext, EntityId, CenterPos, Owner)
{
	GameWorld()->InsertEntity(this);
	m_PlayerPullStrength = Config()->m_InfWhiteHolePullStrength/10.0f;

	m_NumParticles = Config()->m_InfWhiteHoleNumParticles;
	m_Ids = new int[m_NumParticles];
	m_ParticlePos = new vec2[m_NumParticles];
	m_ParticleVec = new vec2[m_NumParticles];
	for(int i=0; i<m_NumParticles; i++)
	{
		m_Ids[i] = Server()->SnapNewId();
	}

	CInfClassCharacter *pOwner = GetOwnerCharacter();
	if(pOwner)
	{
		CInfClassHuman *pHuman = CInfClassHuman::GetInstance(pOwner);
		pHuman->OnWhiteHoleSpawned(this);
	}

	StartVisualEffect();
}

CWhiteHole::~CWhiteHole()
{
	for(int i=0; i<m_NumParticles; i++)
	{
		Server()->SnapFreeId(m_Ids[i]);
	}
	delete[] m_Ids;
	delete[] m_ParticlePos;
	delete[] m_ParticleVec;
}

void CWhiteHole::StartVisualEffect()
{
	float Radius = Config()->m_InfWhiteHoleRadius;
	float RandomRadius, RandomAngle;
	float VecX, VecY;
	for(int i=0; i<m_NumParticles; i++)
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
		ParticlePos += vec2(ParticleVec.x*Speed, ParticleVec.y*Speed); 
		if (dot(VecMid, ParticleVec) <= 0)
			break;
		ParticleVec *= m_ParticleAcceleration; 
	}
	//if (i > 499) dbg_msg("CWhiteHole::StartVisualEffect()", "Problem in finding out how long a particle needs to reach the mid"); // this should never happen
	m_ParticleStopTickTime = i;
}

// Draw ParticleEffect
void CWhiteHole::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	const CInfClassPlayer *pPlayer = GameController()->GetPlayer(SnappingClient);
	const bool AntiPing = pPlayer && pPlayer->GetAntiPingEnabled();
	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	CSnapContext Context(SnappingClientVersion);

	// Draw AntiPing white hole effect
	if(AntiPing)
	{
		int NumSide = 6;
		float AngleStep = 2.0f * pi / NumSide;
		float Radius = Config()->m_InfWhiteHoleRadius;
		for(int i=0; i<NumSide; i++)
		{
			vec2 PartPosStart = m_Pos + vec2(Radius * cos(AngleStep*i), Radius * sin(AngleStep*i));
			vec2 PartPosEnd = m_Pos + vec2(Radius * cos(AngleStep*(i+1)), Radius * sin(AngleStep*(i+1)));
			GameServer()->SnapLaserObject(Context, m_Ids[i], PartPosStart, PartPosEnd, Server()->Tick());
		}
		return;
	}

	// Draw full particle effect - if anti ping is not set to true
	for(int i=0; i<m_NumParticles; i++)
	{
		if(!m_IsDieing && distance(m_ParticlePos[i], m_Pos) > m_Radius)
			continue; // start animation

		GameController()->SendHammerDot(m_ParticlePos[i], m_Ids[i]);
	}
}

void CWhiteHole::MoveParticles()
{
	const int CurrentTick = Server()->Tick();
	int LifeSpan = m_EndTick - CurrentTick;

	float Radius = Config()->m_InfWhiteHoleRadius;
	float RandomAngle, Speed;
	float VecX, VecY;
	vec2 VecMid;
	for(int i=0; i<m_NumParticles; i++)
	{
		VecMid = m_Pos - m_ParticlePos[i];
		Speed = m_ParticleStartSpeed * clamp(1.0f-length(VecMid)/Radius+0.5f, 0.0f, 1.0f);
		m_ParticlePos[i] += vec2(m_ParticleVec[i].x*Speed, m_ParticleVec[i].y*Speed); 
		if (dot(VecMid, m_ParticleVec[i]) <= 0)
		{
			if (LifeSpan < m_ParticleStopTickTime)
			{
				// make particles disappear
				m_ParticlePos[i] = vec2(-99999.0f, -99999.0f);
				m_ParticleVec[i] = vec2(0.0f, 0.0f);
				continue;
			}
			RandomAngle = 2.0f * pi * random_float();
			VecX = cos(RandomAngle);
			VecY = sin(RandomAngle);
			m_ParticlePos[i] = m_Pos + vec2(Radius * VecX, Radius * VecY);
			m_ParticleVec[i] = vec2(-VecX, -VecY);
			continue;
		}
		m_ParticleVec[i] *= m_ParticleAcceleration; 
	}
}

void CWhiteHole::MoveCharacters()
{
	vec2 Dir;
	float Distance, Intensity;
	// Find a player to pull
	for(CInfClassCharacter *pCharacter = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pCharacter; pCharacter = (CInfClassCharacter *)pCharacter->TypeNext())
	{
		if(!Config()->m_InfWhiteHoleAffectsHumans && pCharacter->IsHuman())
			continue; // stops humans from being sucked in, if config var is set

		Dir = m_Pos - pCharacter->m_Pos;
		Distance = length(Dir);
		if(Distance < m_Radius)
		{
			Intensity = clamp(1.0f-Distance/m_Radius+0.5f, 0.0f, 1.0f)*m_PlayerPullStrength;
			pCharacter->AddVelocity(normalize(Dir) * Intensity);
			pCharacter->SetVelocity(pCharacter->Velocity() * m_PlayerDrag);
			pCharacter->UpdateLastEnforcer(GetOwner(), Intensity, EDamageType::WHITE_HOLE, Server()->Tick());
		}
	}
}

void CWhiteHole::Tick()
{
	if(m_MarkedForDestroy)
		return;

	const int CurrentTick = Server()->Tick();
	int LifeSpan = m_EndTick - CurrentTick;
	if(Server()->Tick() >= m_EndTick)
	{
		new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), m_Owner, 20, EDamageType::WHITE_HOLE);
		Reset();
	}
	else 
	{
		if (LifeSpan < m_ParticleStopTickTime) // shrink radius
		{
			m_Radius = LifeSpan/(float)m_ParticleStopTickTime * Config()->m_InfWhiteHoleRadius;
			m_IsDieing = true;
		}
		else if (m_Radius < Config()->m_InfWhiteHoleRadius) // grow radius
		{
			m_Radius += m_RadiusGrowthRate;
			if (m_Radius > Config()->m_InfWhiteHoleRadius)
				m_Radius = Config()->m_InfWhiteHoleRadius;
		}

		MoveParticles();
		MoveCharacters();
	}
}

void CWhiteHole::SetLifeSpan(float Seconds)
{
	m_EndTick = Server()->Tick() + Server()->TickSpeed() * Seconds;
}

void CWhiteHole::TickPaused()
{
	++m_EndTick;
}
