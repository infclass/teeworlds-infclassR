/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "turret.h"

#include <engine/config.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/infclass/damage_type.h>
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
	m_EndTick = m_StartTick + Server()->TickSpeed() * GameController()->InfTurretDuration();
	m_WarmUpCounter = Server()->TickSpeed() * Config()->m_InfTurretWarmUpDuration;
	m_Type = Type;
	for(int &Id : m_Ids)
	{
		Id = Server()->SnapNewId();
	}
	Reload();

	GameWorld()->InsertEntity(this);
}

CTurret::~CTurret()
{
	for(int SnapId : m_Ids)
	{
		Server()->SnapFreeId(SnapId);
	}
}

void CTurret::Tick()
{
	// marked for destroy
	if(IsMarkedForDestroy())
		return;

	if(Server()->Tick() >= m_EndTick)
		Reset();

	CInfClassCharacter *pKiller = nullptr;
	float ClosestLength = CCharacterCore::PhysicalSize() + HitRadius();
	ClosestLength *= ClosestLength;

	for(TEntityPtr<CInfClassCharacter> pChr = GameWorld()->FindFirst<CInfClassCharacter>(); pChr; ++pChr)
	{
		if(!pChr->IsInfected() || !pChr->CanDie())
			continue;

		float Len2 = distance_squared(pChr->GetPos(), GetPos());

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

	// reloading in progress
	if(m_ReloadCounter > 0)
	{
		m_ReloadCounter--;

		if(m_Radius > 15.0f) // shrink radius
		{
			m_Radius -= m_RadiusGrowthRate;
			if(m_Radius < 15.0f)
			{
				m_Radius = 15.0f;
			}
		}
		return; // some reload tick-cycles necessary
	}

	// Reloading finished, warm up in progress
	if(m_WarmUpCounter > 0)
	{
		m_WarmUpCounter--;

		if(m_Radius < 45.0f)
		{
			m_Radius += m_RadiusGrowthRate;
			if(m_Radius > 45.0f)
				m_Radius = 45.0f;
		}

		return; // some warmup tick-cycles necessary
	}

	AttackTargets();
}

void CTurret::TickPaused()
{
	++m_EndTick;
}

void CTurret::AttackTargets()
{
	// warmup finished, ready to find target
	for(TEntityPtr<CInfClassCharacter> pChr = GameWorld()->FindFirst<CInfClassCharacter>(); pChr; ++pChr)
	{
		if(!m_ammunition)
			break;

		if(!pChr->IsInfected() || !pChr->CanDie())
			continue;

		float Len = distance(pChr->m_Pos, m_Pos);

		// attack zombie
		if(Len < (float)Config()->m_InfTurretRadarRange) // 800
		{
			if(GameServer()->Collision()->IntersectLineWeapon(m_Pos, pChr->m_Pos, nullptr, nullptr))
			{
				continue;
			}

			vec2 Direction = normalize(pChr->m_Pos - m_Pos);
			m_foundTarget = true;

			switch(m_Type)
			{
			case LASER:
				new CInfClassLaser(GameServer(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_Owner, Config()->m_InfTurretDmgHealthLaser, EDamageType::TURRET_LASER);
				m_ammunition--;
				break;
			case PLASMA:
			{
				CPlasma *pPlasma = new CPlasma(GameServer(), m_Pos, m_Owner, pChr->GetCid(), Direction, 0, 1);
				pPlasma->SetDamageType(EDamageType::TURRET_PLASMA);
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
		// Reload ammo
		Reload();

		m_WarmUpCounter = Server()->TickSpeed() * Config()->m_InfTurretWarmUpDuration;
		m_ammunition = Config()->m_InfTurretAmmunition;
		m_foundTarget = false;
	}
}

void CTurret::Reload()
{
	switch(m_Type)
	{
	case LASER:
		m_ReloadCounter = Server()->TickSpeed() * Config()->m_InfTurretLaserReloadDuration;
		break;
	case PLASMA:
		m_ReloadCounter = Server()->TickSpeed() * Config()->m_InfTurretPlasmaReloadDuration;
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

		pInfClassObject->m_StartTick = m_StartTick;
		pInfClassObject->m_EndTick = m_EndTick;
	}

	const CInfClassPlayer *pPlayer = GameController()->GetPlayer(SnappingClient);
	const bool AntiPing = pPlayer && pPlayer->GetAntiPingEnabled();
	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	CSnapContext Context(SnappingClientVersion);

	float time = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	float angle = fmodf(time * pi / 2, 2.0f * pi);
	GameServer()->SnapLaserObject(Context, GetId(), m_Pos, m_Pos, Server()->Tick(), GetOwner());

	int Dots = AntiPing ? 2 : std::size(m_Ids);
	for(int i = 0; i < Dots; i++)
	{
		float shiftedAngle = angle + 2.0 * pi * i / static_cast<float>(Dots);
		vec2 Direction = vec2(cos(shiftedAngle), sin(shiftedAngle));
		GameController()->SendHammerDot(m_Pos + Direction * m_Radius, m_Ids[i]);
	}
}

void CTurret::Die(CInfClassCharacter *pKiller)
{
	pKiller->TakeDamage(vec2(0.f, 0.f), Config()->m_InfTurretSelfDestructDmg, m_Owner, EDamageType::TURRET_DESTRUCTION);
	GameServer()->CreateSound(m_Pos, SOUND_LASER_FIRE);
	int ClientId = pKiller->GetCid();
	GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_SCORE, _("You destroyed {str:PlayerName}'s turret!"),
		"PlayerName", Server()->ClientName(m_Owner),
		nullptr);
	GameServer()->SendChatTarget_Localization(m_Owner, CHATCATEGORY_SCORE, _("{str:PlayerName} has destroyed your turret!"),
		"PlayerName", Server()->ClientName(ClientId),
		nullptr);

	// increase score
	Server()->RoundStatistics()->OnScoreEvent(ClientId, SCOREEVENT_DESTROY_TURRET, pKiller->GetPlayerClass(), Server()->ClientName(ClientId), GameServer()->Console());
	GameServer()->SendScoreSound(pKiller->GetCid());
	Reset();
}
