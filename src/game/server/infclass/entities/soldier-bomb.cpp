/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "soldier-bomb.h"

#include <engine/shared/config.h>

#include <game/infclass/damage_type.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>

#include <cmath>

constexpr float SoldierBombRadius = 60.0f;

int CSoldierBomb::EntityId = CGameWorld::ENTTYPE_SOLDIER_BOMB;

void CSoldierBomb::OnFired(CInfClassCharacter *pCharacter, WeaponFireContext *pFireContext)
{
	vec2 Pos = pCharacter->GetPos();
	vec2 ProjStartPos = Pos + pCharacter->GetDirection() * pCharacter->GetProximityRadius() * 0.75f;

	for(TEntityPtr<CSoldierBomb> pBomb = pCharacter->GameWorld()->FindFirst<CSoldierBomb>(); pBomb; ++pBomb)
	{
		if(pBomb->GetOwner() == pCharacter->GetCid())
		{
			pBomb->Explode();
			return;
		}
	}

	new CSoldierBomb(pCharacter->GameServer(), ProjStartPos, pCharacter->GetCid());
	pCharacter->GameServer()->CreateSound(ProjStartPos, SOUND_GRENADE_FIRE);
}

CSoldierBomb::CSoldierBomb(CGameContext *pGameContext, vec2 Pos, int Owner) :
	CPlacedObject(pGameContext, EntityId, Pos, Owner, SoldierBombRadius)
{
	m_InfClassObjectType = INFCLASS_OBJECT_TYPE_SOLDIER_BOMB;
	GameWorld()->InsertEntity(this);
	m_StartTick = Server()->Tick();

	m_nbBomb = Config()->m_InfSoldierBombs;
	m_ChargedBomb = Config()->m_InfSoldierBombs;

	m_IdBomb.set_size(Config()->m_InfSoldierBombs);
	for(int i = 0; i < m_IdBomb.size(); i++)
	{
		m_IdBomb[i] = Server()->SnapNewId();
	}
}

CSoldierBomb::~CSoldierBomb()
{
	for(int i = 0; i < m_IdBomb.size(); i++)
		Server()->SnapFreeId(m_IdBomb[i]);
}

void CSoldierBomb::Explode()
{
	if(Server()->Tick() < m_StartTick + Config()->m_InfDoubleClickFilterMs * Server()->TickSpeed() / 1000.0)
		return;

	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwnerChar)
		return;

	vec2 dir = normalize(pOwnerChar->m_Pos - m_Pos);

	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameController()->CreateExplosion(m_Pos, m_Owner, EDamageType::SOLDIER_BOMB);
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
			GameController()->CreateExplosion(expPos, m_Owner, EDamageType::SOLDIER_BOMB);
		}
		for(int i = 0; i < 12; i++)
		{
			float angle = static_cast<float>(i) * 2.0 * pi / 12.0;
			vec2 expPos = vec2(180.0 * cos(angle), 180.0 * sin(angle));
			if(dot(expPos, dir) <= 0)
			{
				GameController()->CreateExplosion(m_Pos + expPos, m_Owner, EDamageType::SOLDIER_BOMB);
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

	int InfclassVersion = Server()->GetClientInfclassVersion(SnappingClient);
	if(InfclassVersion)
	{
		CNetObj_InfClassObject *pInfClassObject = SnapInfClassObject();
		if(!pInfClassObject)
			return;

		pInfClassObject->m_StartTick = m_StartTick;
		pInfClassObject->m_Data1 = m_nbBomb;
		if(InfclassVersion >= VERSION_INFC_180)
		{
			pInfClassObject->m_Flags |= INFCLASS_OBJECT_FLAG_RELY_ON_CLIENTSIDE_RENDERING;
			return;
		}
	}

	for(int i = 0; i < m_nbBomb; i++)
	{
		float shiftedAngle = m_Angle + 2.0 * pi * static_cast<float>(i) / static_cast<float>(m_IdBomb.size());

		CNetObj_Projectile *pProj = Server()->SnapNewItem<CNetObj_Projectile>(m_IdBomb[i]);
		pProj->m_X = m_Pos.x + SoldierBombRadius * std::cos(shiftedAngle);
		pProj->m_Y = m_Pos.y + SoldierBombRadius * std::sin(shiftedAngle);
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
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
