/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef GAME_SERVER_ENTITIES_GROWINGEXPLOSION_H
#define GAME_SERVER_ENTITIES_GROWINGEXPLOSION_H

#include "infcentity.h"

#include <game/server/entity.h>
#include <game/server/entities/character.h>

#include <vector>

enum class DAMAGE_TYPE;

enum class GROWING_EXPLOSION_EFFECT
{
	INVALID,
	FREEZE_INFECTED,
	POISON_INFECTED,
	ELECTRIC_INFECTED,
	LOVE_INFECTED,
	BOOM_INFECTED,
	HEAL_HUMANS,
};

class CGrowingExplosion : public CInfCEntity
{
public:
	CGrowingExplosion(CGameContext *pGameContext, vec2 Pos, vec2 Dir, int Owner, int Radius, GROWING_EXPLOSION_EFFECT ExplosionEffect);
	CGrowingExplosion(CGameContext *pGameContext, vec2 Pos, vec2 Dir, int Owner, int Radius, DAMAGE_TYPE DamageType);

	void Tick() override;
	void TickPaused() override;

	void SetDamage(int Damage);
	int GetActualDamage();

	void SetTriggeredBy(int CID);

private:
	void ProcessMercenaryBombHit(CInfClassCharacter *pCharacter);

	int m_MaxGrowing;
	int m_GrowingMap_Length;
	int m_GrowingMap_Size;
	DAMAGE_TYPE m_DamageType;
	int m_TriggeredByCID;
	TAKEDAMAGEMODE m_TakeDamageMode = TAKEDAMAGEMODE::NOINFECTION;
	
	vec2 m_SeedPos;
	int m_SeedX;
	int m_SeedY;
	int m_StartTick;
	std::vector<int> m_pGrowingMap;
	std::vector<vec2> m_pGrowingMapVec;
	GROWING_EXPLOSION_EFFECT m_ExplosionEffect = GROWING_EXPLOSION_EFFECT::INVALID;
	bool m_Hit[MAX_CLIENTS];
	int m_Damage = -1;
};

#endif // GAME_SERVER_ENTITIES_GROWINGEXPLOSION_H
