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
	m_CoolDownTick = 0;
	for(int i=0; i<CHeroFlag::SHIELD_COUNT; i++)
	{
		m_IDs[i] = Server()->SnapNewID();
	}
	FindPosition();
	GameWorld()->InsertEntity(this);
}

CHeroFlag::~CHeroFlag()
{
	for(int i=0; i<CHeroFlag::SHIELD_COUNT; i++)
	{
		Server()->SnapFreeID(m_IDs[i]);
	}
}

void CHeroFlag::FindPosition()
{
	int NbPos = GameController()->HeroFlagPositions().size();
	int Index = random_int(0, NbPos-1);
	
	m_Pos = GameController()->HeroFlagPositions()[Index];
}

void CHeroFlag::SetCoolDown()
{
	// Set cooldown for next flag depending on how many players are online
	int PlayerCount = Server()->GetActivePlayerCount();
	if (PlayerCount <= 1)
	{
		// only 1 player on, let him find as many flags as he wants
		m_CoolDownTick = 2;
		return;
	}
	float t = (8-PlayerCount) / 8.0f;
	if (t < 0.0f) 
		t = 0.0f;
	m_CoolDownTick = Server()->TickSpeed() * (15+(120*t));
}

void CHeroFlag::GiveGift(CInfClassCharacter* pHero)
{
	pHero->IncreaseHealth(10);
	pHero->IncreaseArmor(10);
	pHero->GiveWeapon(WEAPON_SHOTGUN, -1);
	pHero->GiveWeapon(WEAPON_GRENADE, -1);
	pHero->GiveWeapon(WEAPON_LASER, -1);
	SetCoolDown();

	pHero->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
	GameServer()->SendEmoticon(pHero->GetCID(), EMOTICON_MUSIC);

	if(GameController()->AreTurretsEnabled())
	{
		if(pHero->m_TurretCount < Config()->m_InfTurretMaxPerPlayer)
		{
			if (pHero->m_TurretCount == 0)
			pHero->GiveWeapon(WEAPON_HAMMER, -1);

			pHero->m_TurretCount += Config()->m_InfTurretGive;

			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "you gained a turret (%i), place it with the hammer", pHero->m_TurretCount);
			GameServer()->SendChatTarget_Localization(pHero->GetCID(), CHATCATEGORY_SCORE, aBuf, NULL);
		}
	}
		
	// Only increase your *own* character health when on cooldown
	if (GameServer()->GetHeroGiftCoolDown() > 0)
		return;

	// Find other players	
	GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The Hero found the flag!"), NULL);
	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
	GameServer()->FlagCollected();

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
	if(m_CoolDownTick <= 0)
	{
		// Find other players
		int NbPlayer = 0;
		for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
		{
			NbPlayer++;
		}
		
		for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
		{
			if(p->GetPlayerClass() != PLAYERCLASS_HERO)
				continue;

			if(p->GetCID() != m_Owner)
				continue;

			float Len = distance(p->m_Pos, m_Pos);
			if(Len < m_ProximityRadius + p->m_ProximityRadius)
			{
				FindPosition();
				GiveGift(p);
				
				if(NbPlayer > 3)
				{
					int ClientID = p->GetCID();
					Server()->RoundStatistics()->OnScoreEvent(ClientID, SCOREEVENT_HERO_FLAG, p->GetPlayerClass(), Server()->ClientName(ClientID), GameServer()->Console());
					GameServer()->SendScoreSound(p->GetCID());
				}
				break;
			}
		}
	}
	else
		m_CoolDownTick--;

}

void CHeroFlag::Snap(int SnappingClient)
{
	if(m_CoolDownTick > 0)
		return;
	
	if(NetworkClipped(SnappingClient))
		return;
	
	if(SnappingClient != DemoClientID && SnappingClient != m_Owner)
		return;
	
	CInfClassPlayer* pOwnerPlayer = GameController()->GetPlayer(m_Owner);
	if(pOwnerPlayer->GetClass() != PLAYERCLASS_HERO)
		return;

	if (GameServer()->GetHeroGiftCoolDown() <= 0)
	{
		float AngleStart = (2.0f * pi * Server()->Tick()/static_cast<float>(Server()->TickSpeed()))/CHeroFlag::SPEED;
		float AngleStep = 2.0f * pi / CHeroFlag::SHIELD_COUNT;
		
		for(int i=0; i<CHeroFlag::SHIELD_COUNT; i++)
		{
			CNetObj_Pickup *pObj = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_IDs[i], sizeof(CNetObj_Pickup)));
			if(!pObj)
				return;

			vec2 PosStart = m_Pos + vec2(CHeroFlag::RADIUS * cos(AngleStart + AngleStep*i), CHeroFlag::RADIUS * sin(AngleStart + AngleStep*i));

			pObj->m_X = (int)PosStart.x;
			pObj->m_Y = (int)PosStart.y;
			pObj->m_Type = i % 2 == 0 ? POWERUP_ARMOR : POWERUP_HEALTH;
			pObj->m_Subtype = 0;
		}
	}

	CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, TEAM_BLUE, sizeof(CNetObj_Flag));
	if(!pFlag)
		return;

	pFlag->m_X = (int)m_Pos.x;
	pFlag->m_Y = (int)m_Pos.y+16.0f;
	pFlag->m_Team = TEAM_BLUE;
}
