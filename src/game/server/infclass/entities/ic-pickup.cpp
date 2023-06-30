/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "ic-pickup.h"

#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>

int CIcPickup::EntityId = CGameWorld::ENTTYPE_PICKUP;

CIcPickup::CIcPickup(CGameContext *pGameContext, EICPickupType Type, vec2 Pos, int Owner) :
	CInfCEntity(pGameContext, EntityId, Pos, Owner, PickupPhysSize)
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

	CInfCEntity::Tick();

	// Check if a player intersected us
	CInfClassCharacter *pChr = (CInfClassCharacter *)GameWorld()->ClosestEntity(m_Pos, PickupPhysSize, CGameWorld::ENTTYPE_CHARACTER, 0);
	if(pChr && pChr->IsAlive())
	{
		// player picked us up, is someone was hooking us, let them go
		bool Picked = false;
		switch(m_Type)
		{
		case EICPickupType::Health:
			if(pChr->GiveHealth(1))
			{
				Picked = true;
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);
			}
			break;

		case EICPickupType::Armor:
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
	
	if(m_Type == EICPickupType::Invalid)
		return;

	if(m_Owner >= 0)
	{
		if(m_Owner != SnappingClient)
			return;
	}

	int NetworkType = -1;
	int Subtype = 0;
	switch(m_Type)
	{
	case EICPickupType::Health:
		NetworkType = POWERUP_HEALTH;
		break;
	case EICPickupType::Armor:
		NetworkType = POWERUP_ARMOR;
		break;
	case EICPickupType::Invalid:
		break;
	}

	if(NetworkType < 0)
		return;

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	GameServer()->SnapPickup(CSnapContext(SnappingClientVersion), GetID(), m_Pos, NetworkType, Subtype);
}

void CIcPickup::Spawn(float Delay)
{
	m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * Delay;
}

void CIcPickup::SetRespawnInterval(float Seconds)
{
	m_SpawnInterval = Seconds;
}
