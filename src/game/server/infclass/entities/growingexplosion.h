/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef GAME_SERVER_ENTITIES_GROWINGEXPLOSION_H
#define GAME_SERVER_ENTITIES_GROWINGEXPLOSION_H

#include "infcentity.h"

#include <engine/shared/config.h>
#include <game/server/entity.h>
#include <game/server/entities/character.h>

enum
{
	GROWINGEXPLOSIONEFFECT_FREEZE_INFECTED=0,
	GROWINGEXPLOSIONEFFECT_POISON_INFECTED,
	GROWINGEXPLOSIONEFFECT_ELECTRIC_INFECTED,
	GROWINGEXPLOSIONEFFECT_LOVE_INFECTED,
	GROWINGEXPLOSIONEFFECT_BOOM_INFECTED,
	GROWINGEXPLOSIONEFFECT_HEAL_HUMANS,
};

class CGrowingExplosion : public CInfCEntity
{
public:
	CGrowingExplosion(CGameContext *pGameContext, vec2 Pos, vec2 Dir, int Owner, int Radius, int ExplosionEffect, TAKEDAMAGEMODE TakeDamageMode = TAKEDAMAGEMODE_NOINFECTION);
	virtual ~CGrowingExplosion();

	virtual void Tick();
	virtual void TickPaused();

private:
	void DamagePortals();

	int m_MaxGrowing;
	int m_GrowingMap_Length;
	int m_GrowingMap_Size;
	TAKEDAMAGEMODE m_TakeDamageMode;
	
	vec2 m_SeedPos;
	int m_SeedX;
	int m_SeedY;
	int m_StartTick;
	int* m_pGrowingMap;
	vec2* m_pGrowingMapVec;
	int m_ExplosionEffect;
	bool m_Hit[MAX_CLIENTS];
};

#endif // GAME_SERVER_ENTITIES_GROWINGEXPLOSION_H
