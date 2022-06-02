/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hero-flag.h"

#include <game/server/gamecontext.h>
#include <engine/server/server.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

CHeroFlag::CHeroFlag(CGameContext *pGameContext, int Owner)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_HERO_FLAG, vec2(), Owner, ms_PhysSize)
{
	for(int &ID : m_IDs)
	{
		ID = Server()->SnapNewID();
	}

	FindPosition();
	GameWorld()->InsertEntity(this);
}

CHeroFlag::~CHeroFlag()
{
	for(int ID : m_IDs)
	{
		Server()->SnapFreeID(ID);
	}
}

void CHeroFlag::FindPosition()
{
	m_HasSpawnPosition = GameController()->GetHeroFlagPosition(&m_Pos);
}

void CHeroFlag::SetCoolDown()
{
	// Set cooldown for next flag depending on how many players are online
	int PlayerCount = Server()->GetActivePlayerCount();
	if (PlayerCount <= 1)
	{
		// only 1 player on, let him find as many flags as he wants
		m_SpawnTick = Server()->Tick() + 2;
		return;
	}
	float t = (8-PlayerCount) / 8.0f;
	if (t < 0.0f) 
		t = 0.0f;
	m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * (15+(120*t));
}

void CHeroFlag::GiveGift(CInfClassCharacter *pHero)
{
	pHero->SetHealthArmor(10, 10);
	pHero->GiveWeapon(WEAPON_SHOTGUN, -1);
	pHero->GiveWeapon(WEAPON_GRENADE, -1);
	pHero->GiveWeapon(WEAPON_LASER, -1);

	pHero->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
	GameServer()->SendEmoticon(pHero->GetCID(), EMOTICON_MUSIC);

	if(GameController()->AreTurretsEnabled())
	{
		int NewNumberOfTurrets = clamp<int>(pHero->m_TurretCount + Config()->m_InfTurretGive, 0, Config()->m_InfTurretMaxPerPlayer);
		if(pHero->m_TurretCount != NewNumberOfTurrets)
		{
			if(pHero->m_TurretCount == 0)
				pHero->GiveWeapon(WEAPON_HAMMER, -1);

			pHero->m_TurretCount = NewNumberOfTurrets;

			GameServer()->SendChatTarget_Localization_P(pHero->GetCID(), CHATCATEGORY_SCORE, pHero->m_TurretCount,
				_P("You have {int:NumTurrets} turret available, use the Hammer to place it",
				   "You have {int:NumTurrets} turrets available, use the Hammer to place it"),
				"NumTurrets", &pHero->m_TurretCount,
				nullptr);
		}
	}

	// Only increase your *own* character health when on cooldown
	if(!GameController()->HeroGiftAvailable())
		return;

	GameController()->OnHeroFlagCollected(GetOwner());

	// Find other players	
	for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
	{
		if(p->IsZombie() || p == pHero)
			continue;

		p->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		GameServer()->SendEmoticon(p->GetCID(), EMOTICON_MUSIC);
		
		p->GiveGift(GIFT_HEROFLAG);
	}
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

	if(pOwner->GetPlayerClass() != PLAYERCLASS_HERO)
	{
		// Makes no sense
		return;
	}

	float Len = distance(pOwner->GetPos(), GetPos());
	if(Len < GetProximityRadius() + pOwner->GetProximityRadius())
	{
		GiveGift(pOwner);
		SetCoolDown();
		FindPosition();

		if(Server()->GetActivePlayerCount() > 3)
		{
			int ClientID = pOwner->GetCID();
			Server()->RoundStatistics()->OnScoreEvent(ClientID, SCOREEVENT_HERO_FLAG, pOwner->GetPlayerClass(), Server()->ClientName(ClientID), GameServer()->Console());
			GameServer()->SendScoreSound(pOwner->GetCID());
		}
	}
}

void CHeroFlag::TickPaused()
{
	m_SpawnTick++;
}

void CHeroFlag::Snap(int SnappingClient)
{
	if(Server()->Tick() < m_SpawnTick)
		return;
	
	if(NetworkClipped(SnappingClient))
		return;
	
	if(SnappingClient != SERVER_DEMO_CLIENT && SnappingClient != m_Owner)
		return;
	
	CInfClassPlayer* pOwnerPlayer = GameController()->GetPlayer(m_Owner);
	if(pOwnerPlayer->GetClass() != PLAYERCLASS_HERO)
		return;

	if(GameController()->HeroGiftAvailable())
	{
		const float Speed = 0.1f;
		float AngleStart = (2.0f * pi * Server()->Tick()/static_cast<float>(Server()->TickSpeed())) * Speed;
		float AngleStep = 2.0f * pi / CHeroFlag::SHIELD_COUNT;

		const vec2 DecorationsPivot(m_Pos.x, m_Pos.y - 20);
		const float Radius = 38;
		
		for(int i=0; i<CHeroFlag::SHIELD_COUNT; i++)
		{
			CNetObj_Pickup *pObj = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_IDs[i], sizeof(CNetObj_Pickup)));
			if(!pObj)
				return;

			vec2 PosStart = DecorationsPivot + vec2(cos(AngleStart + AngleStep*i), sin(AngleStart + AngleStep*i)) * Radius;

			pObj->m_X = (int)PosStart.x;
			pObj->m_Y = (int)PosStart.y;
			pObj->m_Type = i % 2 == 0 ? POWERUP_ARMOR : POWERUP_HEALTH;
			pObj->m_Subtype = 0;
		}
	}

	CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, m_ID, sizeof(CNetObj_Flag));
	if(!pFlag)
		return;

	pFlag->m_X = (int)m_Pos.x;
	pFlag->m_Y = (int)m_Pos.y+16.0f;
	pFlag->m_Team = TEAM_BLUE;
}
