/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>
#include "fking-power.h"
#include "growingexplosion.h"
#include <cmath>

CFKingPower::CFKingPower(CGameContext *pGameContext, vec2 Pos, int Owner)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_FKING_POWER, Pos, Owner)
{
	GameWorld()->InsertEntity(this);
	m_DetectionRadius = 60.0f;
	m_StartTick = Server()->Tick();

	m_nbP = Config()->m_InfFKingPowers;
	charged_P = Config()->m_InfFKingPowers;

	m_IDP.set_size(Config()->m_InfFKingPowers);
	for(int i=0; i<m_IDP.size(); i++)
	{
		m_IDP[i] = Server()->SnapNewID();
	}
}

CFKingPower::~CFKingPower()
{
	for(int i=0; i<m_IDP.size(); i++)
		Server()->SnapFreeID(m_IDP[i]);
}

void CFKingPower::Explode()
{
	CCharacter *OwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(!OwnerChar)
		return;
		
	vec2 dir = normalize(OwnerChar->m_Pos - m_Pos);
	
	new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), m_Owner, 6, GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED);
    new CGrowingExplosion(GameServer(), m_Pos, vec2(0.0, -1.0), m_Owner, 5, GROWINGEXPLOSIONEFFECT_LOVE_INFECTED);
	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameServer()->CreateExplosion(m_Pos, m_Owner, WEAPON_HAMMER, false, TAKEDAMAGEMODE_SELFHARM);
	if (charged_P <= m_nbP) {
		for(int i=0; i<6; i++)
		{
			float angle = static_cast<float>(i)*2.0*pi/6.0;
			vec2 expPos = m_Pos + vec2(90.0*cos(angle), 90.0*sin(angle));
			GameServer()->CreateExplosion(expPos, m_Owner, WEAPON_HAMMER, false, TAKEDAMAGEMODE_SELFHARM);
		}
		for(int i=0; i<12; i++)
		{
			float angle = static_cast<float>(i)*2.0*pi/12.0;
			vec2 expPos = vec2(180.0*cos(angle), 180.0*sin(angle));
			if(dot(expPos, dir) <= 0)
			{
				GameServer()->CreateExplosion(m_Pos + expPos, m_Owner, WEAPON_HAMMER, false, TAKEDAMAGEMODE_SELFHARM);
			}
		}
	}
	
	m_nbP--;
	
	if(m_nbP == 0)
	{
		GameServer()->m_World.DestroyEntity(this);
	}
}

void CFKingPower::ChargeP(float time)
{
	if (charged_P > 1) {
		// time is multiplied by N, bombs will get charged every 1/N sec
		if (std::floor(time * 1.4) >
				Config()->m_InfFKingPowers - charged_P) {
			charged_P--;
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		}
	}
}

void CFKingPower::Snap(int SnappingClient)
{
	float time = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	float angle = fmodf(time*pi/2, 2.0f*pi);
	ChargeP(time);

	for(int i=0; i<m_nbP; i++)
	{
		if(NetworkClipped(SnappingClient))
			return;
		
		float shiftedAngle = angle + 2.0*pi*static_cast<float>(i)/static_cast<float>(m_IDP.size());
		
		CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDP[i], sizeof(CNetObj_Projectile)));
		pProj->m_X = (int)(m_Pos.x + m_DetectionRadius*cos(shiftedAngle));
		pProj->m_Y = (int)(m_Pos.y + m_DetectionRadius*sin(shiftedAngle));
		pProj->m_VelX = (int)(0.0f);
		pProj->m_VelY = (int)(0.0f);
		pProj->m_StartTick = Server()->Tick();
		pProj->m_Type = WEAPON_LASER;
	}
}

void CFKingPower::TickPaused()
{
	++m_StartTick;
}

bool CFKingPower::AddP()
{
	if(m_nbP < m_IDP.size())
	{
		m_nbP++;
		return true;
	}
	else return false;
}
