/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "turret.h"

#include <engine/config.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <engine/server/roundstatistics.h>

#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

#include "infc-laser.h"
#include "infccharacter.h"
#include "plasma.h"

#include <iterator> // std::size

int CTurret::EntityId = CGameWorld::ENTTYPE_TURRET;

CTurret::CTurret(CGameContext *pGameContext, vec2 Pos, int Owner, vec2 Direction, CTurret::Type Type) :
	CPlacedObject(pGameContext, EntityId, Pos, Owner)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_TURRET;
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
	for(int &ID : m_IDs)
	{
		ID = Server()->SnapNewID();
	}
	Reload();

	GameWorld()->InsertEntity(this);
}

CTurret::~CTurret()
{
	for(int SnapId : m_IDs)
	{
		Server()->SnapFreeID(SnapId);
	}
}

void CTurret::Tick()
{
	//marked for destroy
	if(IsMarkedForDestroy())
		return;

	if(m_LifeSpan < 0)
		Reset();

	CInfClassCharacter *pKiller = nullptr;
	float ClosestLength = CCharacterCore::PhysicalSize() + HitRadius();
	ClosestLength *= ClosestLength;

	for(TEntityPtr<CInfClassCharacter> pChr = GameWorld()->FindFirst<CInfClassCharacter>(); pChr; ++pChr)
	{
		if(!pChr->IsZombie() || !pChr->CanDie())
			continue;

		float Len2 = distance2(pChr->GetPos(), GetPos());

		// selfdestruction
		if(Len2 < ClosestLength)
		{
			ClosestLength = Len2;
			pKiller = pChr;
		}
	}

	if(pKiller)
	{
		Die(pKiller);
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
					new CInfClassLaser(GameServer(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_Owner, Config()->m_InfTurretDmgHealthLaser, DAMAGE_TYPE::TURRET_LASER);
					m_ammunition--;
					break;
				case PLASMA:
				{
					CPlasma *pPlasma = new CPlasma(GameServer(), m_Pos, m_Owner, pChr->GetCID() , Direction, 0, 1);
					pPlasma->SetDamageType(DAMAGE_TYPE::TURRET_PLASMA);
				}
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
	if(!DoSnapForClient(SnappingClient))
		return;

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;
	}

	const CInfClassPlayer *pPlayer = GameController()->GetPlayer(SnappingClient);
	const bool AntiPing = pPlayer && pPlayer->GetAntiPingEnabled();

	float time = (Server()->Tick()-m_StartTick)/(float)Server()->TickSpeed();
	float angle = fmodf(time*pi/2, 2.0f*pi);

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));

	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_FromX = (int)m_Pos.x;
	pObj->m_FromY = (int)m_Pos.y;
	pObj->m_StartTick = Server()->Tick();

	int Dots = AntiPing ? 2 : std::size(m_IDs);

	for(int i = 0; i < Dots; i++)
	{
		float shiftedAngle = angle + 2.0 * pi * i / static_cast<float>(Dots);
		vec2 Direction = vec2(cos(shiftedAngle), sin(shiftedAngle));
		GameController()->SendHammerDot(m_Pos + Direction * m_Radius, m_IDs[i]);
	}
}

void CTurret::Die(CInfClassCharacter *pKiller)
{
	pKiller->TakeDamage(vec2(0.f, 0.f), Config()->m_InfTurretSelfDestructDmg, m_Owner, DAMAGE_TYPE::TURRET_DESTRUCTION);
	GameServer()->CreateSound(m_Pos, SOUND_LASER_FIRE);
	int ClientID = pKiller->GetCID();
	GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_SCORE, _("You destroyed {str:PlayerName}'s turret!"),
		"PlayerName", Server()->ClientName(m_Owner),
		nullptr);
	GameServer()->SendChatTarget_Localization(m_Owner, CHATCATEGORY_SCORE, _("{str:PlayerName} has destroyed your turret!"),
		"PlayerName", Server()->ClientName(ClientID),
		nullptr);

	// increase score
	Server()->RoundStatistics()->OnScoreEvent(ClientID, SCOREEVENT_DESTROY_TURRET, pKiller->GetPlayerClass(), Server()->ClientName(ClientID), GameServer()->Console());
	GameServer()->SendScoreSound(pKiller->GetCID());
	Reset();
}
