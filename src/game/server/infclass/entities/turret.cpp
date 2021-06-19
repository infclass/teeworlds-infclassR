/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "turret.h"

#include <engine/config.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <engine/server/roundstatistics.h>

#include "infc-laser.h"
#include "infccharacter.h"
#include "plasma.h"

CTurret::CTurret(CGameContext *pGameContext, vec2 Pos, int Owner, vec2 Direction, CTurret::Type Type)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_TURRET, Pos, Owner)
{
	m_Dir = Direction;
	m_StartTick = Server()->Tick();
	m_Bounces = 0;
	m_Radius = 15.0f;
	m_foundTarget = false;
	m_ammunition = Config()->m_InfTurretAmmunition;
	m_EvalTick = Server()->Tick();
	m_LifeSpan = Server()->TickSpeed()*Config()->m_InfTurretDuration;
	m_WarmUpCounter = Server()->TickSpeed()*Config()->m_InfTurretWarmUpDuration;
	m_Type = Type;
	m_IDs.set_size(9);
	for(int i = 0; i < m_IDs.size(); i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}
	Reload();

	GameWorld()->InsertEntity(this);
}

CTurret::~CTurret()
{
	for(int i = 0; i < m_IDs.size(); i++)
	{
		Server()->SnapFreeID(m_IDs[i]);
	}
}

void CTurret::Tick()
{
	//marked for destroy
	if(m_MarkedForDestroy)
		return;

	if(m_LifeSpan < 0)
		Reset();

	for(CInfClassCharacter *pChr = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pChr; pChr = (CInfClassCharacter *)pChr->TypeNext())
	{
		if(!pChr->IsZombie() || !pChr->CanDie())
			continue;

		float Len = distance(pChr->m_Pos, m_Pos);

		// selfdestruction
		if(Len < pChr->GetProximityRadius() + 4.0f )
		{
			pChr->TakeDamage(vec2(0.f, 0.f), Config()->m_InfTurretSelfDestructDmg, m_Owner, WEAPON_LASER, TAKEDAMAGEMODE_NOINFECTION);
			GameServer()->CreateSound(m_Pos, SOUND_LASER_FIRE);
			int ClientID = pChr->GetPlayer()->GetCID();
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "You destroyed %s's turret!", Server()->ClientName(m_Owner));
			GameServer()->SendChatTarget(ClientID, aBuf);
			GameServer()->SendChatTarget(m_Owner, "A zombie has destroyed your turret!");

			//increase score
			Server()->RoundStatistics()->OnScoreEvent(ClientID, SCOREEVENT_DESTROY_PORTAL, pChr->GetPlayerClass(), Server()->ClientName(ClientID), GameServer()->Console());
			GameServer()->SendScoreSound(pChr->GetPlayer()->GetCID());
			Reset();
		}
	}

	//reduce lifespan
	m_LifeSpan--;

	//reloading in progress
	if(m_ReloadCounter > 0)
	{
		m_ReloadCounter--;

		if(m_Radius > 15.0f) //shrink radius
		{
			m_Radius -= m_RadiusGrowthRate;
			if(m_Radius < 15.0f)
			{
				m_Radius = 15.0f;
			}
		}
		return; //some reload tick-cycles necessary
	}

	//Reloading finished, warm up in progress
	if ( m_WarmUpCounter > 0 )
	{
			m_WarmUpCounter--;

			if(m_Radius < 45.0f)
			{
				m_Radius += m_RadiusGrowthRate;
				if(m_Radius > 45.0f)
					m_Radius = 45.0f;
			}

			return; //some warmup tick-cycles necessary
	}

	AttackTargets();
}

void CTurret::AttackTargets()
{
	//warmup finished, ready to find target
	for(CInfClassCharacter *pChr = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pChr; pChr = (CInfClassCharacter *)pChr->TypeNext())
	{
		if(!m_ammunition) break;

		if(!pChr->IsZombie() || !pChr->CanDie())
			continue;

		float Len = distance(pChr->m_Pos, m_Pos);

		// attack zombie
		if (Len < (float)Config()->m_InfTurretRadarRange) //800
		{
			if(GameServer()->Collision()->IntersectLine(m_Pos, pChr->m_Pos, nullptr, nullptr))
			{
				continue;
			}

			vec2 Direction = normalize(pChr->m_Pos - m_Pos);
			m_foundTarget = true;

			switch(m_Type)
			{
				case LASER:
					new CInfClassLaser(GameServer(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_Owner, Config()->m_InfTurretDmgHealthLaser);
					m_ammunition--;
					break;
				case PLASMA:
					new CPlasma(GameServer(), m_Pos, m_Owner, pChr->GetPlayer()->GetCID() , Direction, 0, 1);
					m_ammunition--;
					break;
			}

			GameServer()->CreateSound(m_Pos, SOUND_LASER_FIRE);
		}
	}

	// either the turret found one target (single projectile) or it is out of ammo due to fire at different targets (multi projectile)
	if(!m_ammunition || m_foundTarget)
	{
		//Reload ammo
		Reload();

		m_WarmUpCounter = Server()->TickSpeed()*Config()->m_InfTurretWarmUpDuration;
		m_ammunition = Config()->m_InfTurretAmmunition;
		m_foundTarget = false;
	}
}

void CTurret::Reload()
{
	switch (m_Type)
	{
		case LASER:
			m_ReloadCounter = Server()->TickSpeed()*Config()->m_InfTurretLaserReloadDuration;
			break;
		case PLASMA:
			m_ReloadCounter = Server()->TickSpeed()*Config()->m_InfTurretPlasmaReloadDuration;
			break;
	}
}

void CTurret::Snap(int SnappingClient)
{
	// Draw AntiPing  effect
	if(Server()->GetClientAntiPing(SnappingClient))
	{
		float time = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
		float angle = fmodf(time*pi/2, 2.0f*pi);

		for(int i=0; i<m_IDs.size()-7; i++)
		{
			float shiftedAngle = angle + 2.0*pi*static_cast<float>(i)/static_cast<float>(m_IDs.size()-7);

			CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDs[i], sizeof(CNetObj_Projectile)));

			if(!pObj)
				continue;

			pObj->m_X = (int)(m_Pos.x + m_Radius*cos(shiftedAngle));
			pObj->m_Y = (int)(m_Pos.y + m_Radius*sin(shiftedAngle));
			pObj->m_VelX = 0;
			pObj->m_VelY = 0;

			pObj->m_StartTick = Server()->Tick();
		}

		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[m_IDs.size()-7], sizeof(CNetObj_Laser)));

		if(!pObj)
			return;

		pObj->m_X = (int)m_Pos.x;
		pObj->m_Y = (int)m_Pos.y;
		pObj->m_FromX = (int)m_Pos.x;
		pObj->m_FromY = (int)m_Pos.y;
		pObj->m_StartTick = Server()->Tick();

		return;
	}

	float time = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	float angle = fmodf(time*pi/2, 2.0f*pi);

	for(int i=0; i<m_IDs.size()-1; i++)
	{
		float shiftedAngle = angle + 2.0*pi*static_cast<float>(i)/static_cast<float>(m_IDs.size()-1);

		CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDs[i], sizeof(CNetObj_Projectile)));

		if(!pObj)
			continue;

		pObj->m_X = (int)(m_Pos.x + m_Radius*cos(shiftedAngle));
		pObj->m_Y = (int)(m_Pos.y + m_Radius*sin(shiftedAngle));
		pObj->m_VelX = 0;
		pObj->m_VelY = 0;
		pObj->m_StartTick = Server()->Tick();
	}

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDs[m_IDs.size()-1], sizeof(CNetObj_Laser)));

	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_Pos.x;
	pObj->m_FromY = (int)m_Pos.y;
	pObj->m_StartTick = Server()->Tick();
}
