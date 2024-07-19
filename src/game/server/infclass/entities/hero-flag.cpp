/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hero-flag.h"

#include <game/server/gamecontext.h>
#include <engine/server/server.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/server/infclass/classes/humans/human.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>

CHeroFlag::CHeroFlag(CGameContext *pGameContext, int Owner)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_HERO_FLAG, vec2(), Owner, ms_PhysSize)
{
	for(int &Id : m_Ids)
	{
		Id = Server()->SnapNewId();
	}

	FindPosition();
	GameWorld()->InsertEntity(this);
}

CHeroFlag::~CHeroFlag()
{
	for(int Id : m_Ids)
	{
		Server()->SnapFreeId(Id);
	}
}

void CHeroFlag::FindPosition()
{
	m_HasSpawnPosition = GameController()->GetHeroFlagPosition(&m_Pos);
}

void CHeroFlag::ResetCooldown()
{
	m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * GameController()->GetHeroFlagCooldown();
}

void CHeroFlag::GiveGift(CInfClassCharacter *pHero)
{
	CInfClassHuman *pHeroClass = CInfClassHuman::GetInstance(pHero);
	if(!pHeroClass)
	{
		return;
	}

	pHeroClass->OnHeroFlagTaken(pHero);
}

void CHeroFlag::Tick()
{
	if(IsAvailable())
	{
		m_HasSpawnPosition = GameController()->IsPositionAvailableForHumans(GetPos());
	}

	if(!IsAvailable())
	{
		FindPosition();
	}

	if(!IsAvailable())
		return;

	if(Server()->Tick() < GetSpawnTick())
		return;

	CInfClassCharacter *pOwner = GetOwnerCharacter();
	if(!pOwner)
		return;

	if(pOwner->GetPlayerClass() != EPlayerClass::Hero)
	{
		// Makes no sense
		return;
	}

	float Len = distance(pOwner->GetPos(), GetPos());
	if(Len < GetProximityRadius() + pOwner->GetProximityRadius())
	{
		GiveGift(pOwner);
		ResetCooldown();
		FindPosition();

		if(Server()->GetActivePlayerCount() > 3)
		{
			int ClientId = pOwner->GetCid();
			Server()->RoundStatistics()->OnScoreEvent(ClientId, SCOREEVENT_HERO_FLAG, pOwner->GetPlayerClass(), Server()->ClientName(ClientId), GameServer()->Console());
			GameServer()->SendScoreSound(pOwner->GetCid());
		}
	}
}

void CHeroFlag::TickPaused()
{
	m_SpawnTick++;
}

void CHeroFlag::Snap(int SnappingClient)
{
	if(!IsAvailable())
		return;

	if(Server()->Tick() < GetSpawnTick())
		return;

	if(NetworkClipped(SnappingClient))
		return;
	
	if(SnappingClient != SERVER_DEMO_CLIENT && SnappingClient != m_Owner)
		return;

	CInfClassCharacter *pOwner = GetOwnerCharacter();
	if(!pOwner)
		return;

	if(pOwner->GetPlayerClass() != EPlayerClass::Hero)
		return;

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);

	if(GameController()->HeroGiftAvailable())
	{
		const float Speed = 0.1f;
		float AngleStart = (2.0f * pi * Server()->Tick()/static_cast<float>(Server()->TickSpeed())) * Speed;
		float AngleStep = 2.0f * pi / ms_SHIELD_COUNT;

		const vec2 DecorationsPivot(m_Pos.x, m_Pos.y - 20);
		const float Radius = 38;

		CSnapContext Context(SnappingClientVersion);
		for(int i = 0; i < ms_SHIELD_COUNT; i++)
		{
			vec2 PosStart = DecorationsPivot + vec2(cos(AngleStart + AngleStep*i), sin(AngleStart + AngleStep*i)) * Radius;
			int Type = i % 2 == 0 ? POWERUP_ARMOR : POWERUP_HEALTH;
			int Subtype = 0;
			GameServer()->SnapPickup(Context, m_Ids[i], PosStart, Type, Subtype);
		}
	}

	CNetObj_Flag *pFlag = Server()->SnapNewItem<CNetObj_Flag>(m_Id);
	if(!pFlag)
		return;

	pFlag->m_X = m_Pos.x;
	pFlag->m_Y = m_Pos.y + TileSizeF * 0.5;
	pFlag->m_Team = TEAM_BLUE;
}
