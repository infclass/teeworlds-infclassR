/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "soldier-bomb.h"

#include <engine/shared/config.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/infcgamecontroller.h>

#include <cmath>

constexpr float SoldierBombRadius = 60.0f;

CSoldierBomb::CSoldierBomb(CGameContext *pGameContext, vec2 Pos, int Owner) :
	CPlacedObject(pGameContext, CGameWorld::ENTTYPE_SOLDIER_BOMB, Pos, Owner, SoldierBombRadius)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_SOLDIER_BOMB;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();

	m_nbBomb = Config()->m_InfSoldierBombs;
	m_ChargedBomb = Config()->m_InfSoldierBombs;

	m_IDBomb.set_size(Config()->m_InfSoldierBombs);
	for(int i = 0; i < m_IDBomb.size(); i++)
	{
		m_IDBomb[i] = Server()->SnapNewID();
	}
}

CSoldierBomb::~CSoldierBomb()
{
	for(int i = 0; i < m_IDBomb.size(); i++)
		Server()->SnapFreeID(m_IDBomb[i]);
}

void CSoldierBomb::Explode()
{
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwnerChar)
		return;

	vec2 dir = normalize(pOwnerChar->m_Pos - m_Pos);

	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameController()->CreateExplosion(m_Pos, m_Owner, DAMAGE_TYPE::SOLDIER_BOMB);
	if(m_ChargedBomb <= m_nbBomb)
	{
		/*
		 * Charged bomb makes bigger explosions.
		 *
		 * there are m_nbBomb bombs. Firstly, bomb #m_nbBomb explodes. Then
		 * #m_nbBomb-1. Then #m_nbBomb-2. If there are 3 bombs, bomb #3 explodes
		 * first, bomb #2 second, bomb #1 last.
		 * charged_bomb points to the last charged bomb. So, if charged_bomb == 1,
		 * it means that bombs #m_mbBomb, ..., #2, #1 are charged.
		 */
		for(int i = 0; i < 6; i++)
		{
			float angle = static_cast<float>(i) * 2.0 * pi / 6.0;
			vec2 expPos = m_Pos + vec2(90.0 * cos(angle), 90.0 * sin(angle));
			GameController()->CreateExplosion(expPos, m_Owner, DAMAGE_TYPE::SOLDIER_BOMB);
		}
		for(int i = 0; i < 12; i++)
		{
			float angle = static_cast<float>(i) * 2.0 * pi / 12.0;
			vec2 expPos = vec2(180.0 * cos(angle), 180.0 * sin(angle));
			if(dot(expPos, dir) <= 0)
			{
				GameController()->CreateExplosion(m_Pos + expPos, m_Owner, DAMAGE_TYPE::SOLDIER_BOMB);
			}
		}
	}

	m_nbBomb--;

	if(m_nbBomb == 0)
	{
		GameWorld()->DestroyEntity(this);
	}
}

void CSoldierBomb::ChargeBomb(float time)
{
	if(m_ChargedBomb > 1)
	{
		// time is multiplied by N, bombs will get charged every 1/N sec
		if(std::floor(time * 1.4) >
			Config()->m_InfSoldierBombs - m_ChargedBomb)
		{
			m_ChargedBomb--;
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
		}
	}
}

void CSoldierBomb::Snap(int SnappingClient)
{
	if(!DoSnapForClient(SnappingClient))
		return;

	if(Server()->GetClientInfclassVersion(SnappingClient))
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;

		pInfClassObject->m_StartTick = m_StartTick;
	}

	for(int i = 0; i < m_nbBomb; i++)
	{
		float shiftedAngle = m_Angle + 2.0 * pi * static_cast<float>(i) / static_cast<float>(m_IDBomb.size());

		CNetObj_Projectile *pProj = Server()->SnapNewItem<CNetObj_Projectile>(m_IDBomb[i]);
		pProj->m_X = (int)(m_Pos.x + SoldierBombRadius * cos(shiftedAngle));
		pProj->m_Y = (int)(m_Pos.y + SoldierBombRadius * sin(shiftedAngle));
		pProj->m_VelX = (int)(0.0f);
		pProj->m_VelY = (int)(0.0f);
		pProj->m_StartTick = Server()->Tick();
		pProj->m_Type = WEAPON_GRENADE;
	}
}

void CSoldierBomb::Tick()
{
	float time = (Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed();
	ChargeBomb(time);
	m_Angle = fmodf(time * pi / 2, 2.0f * pi);
}

void CSoldierBomb::TickPaused()
{
	++m_StartTick;
}
