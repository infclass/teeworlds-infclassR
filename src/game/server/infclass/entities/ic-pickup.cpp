/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "ic-pickup.h"

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>

CIcPickup::CIcPickup(CGameContext *pGameContext, IC_PICKUP_TYPE Type, vec2 Pos, int Owner)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_PICKUP, Pos, Owner, PickupPhysSize)
{
	m_Type = Type;

	Reset();

	GameWorld()->InsertEntity(this);
}

void CIcPickup::Reset()
{
	m_SpawnTick = -1;
}

void CIcPickup::Tick()
{
	// wait for respawn
	if(m_SpawnTick > 0)
	{
		if(Server()->Tick() > m_SpawnTick)
		{
			// respawn
			m_SpawnTick = -1;
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
		}
		else
			return;
	}
	// Check if a player intersected us
	CInfClassCharacter *pChr = (CInfClassCharacter *)GameWorld()->ClosestEntity(m_Pos, PickupPhysSize, CGameWorld::ENTTYPE_CHARACTER, 0);
	if(pChr && pChr->IsAlive())
	{
		// player picked us up, is someone was hooking us, let them go
		bool Picked = false;
		switch (m_Type)
		{
			case IC_PICKUP_TYPE::HEALTH:
				if(pChr->GiveHealth(1))
				{
					Picked = true;
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);
				}
				break;

			case IC_PICKUP_TYPE::ARMOR:
				if(pChr->GiveArmor(1))
				{
					Picked = true;
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
				}
				break;

			default:
				break;
		};

		if(Picked)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "pickup player='%d:%s' item=%d",
				pChr->GetCID(), Server()->ClientName(pChr->GetCID()), static_cast<int>(m_Type));
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
			if(m_SpawnInterval >= 0)
			{
				Spawn(m_SpawnInterval);
			}
			else
			{
				MarkForDestroy();
			}
		}
	}
}

void CIcPickup::TickPaused()
{
	if(m_SpawnTick != -1)
		++m_SpawnTick;
}

void CIcPickup::Snap(int SnappingClient)
{
	if(m_SpawnTick != -1 || NetworkClipped(SnappingClient))
		return;
	
	if(m_Type == IC_PICKUP_TYPE::INVALID)
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = round_to_int(m_Pos.x);
	pP->m_Y = round_to_int(m_Pos.y);
	
	int NetworkType = 0;
	switch(m_Type)
	{
	case IC_PICKUP_TYPE::HEALTH:
		NetworkType = POWERUP_HEALTH;
		break;
	case IC_PICKUP_TYPE::ARMOR:
		NetworkType = POWERUP_ARMOR;
		break;
	case IC_PICKUP_TYPE::INVALID:
		break;
	}
	
	pP->m_Type = NetworkType;
}

void CIcPickup::Spawn(float Delay)
{
	m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * Delay;
}

void CIcPickup::SetRespawnInterval(float Seconds)
{
	m_SpawnInterval = Seconds;
}
