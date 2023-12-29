/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PROJECTILE_H
#define GAME_SERVER_ENTITIES_PROJECTILE_H

#include <game/server/infclass/entities/infcentity.h>

enum class TAKEDAMAGEMODE;
enum class EDamageType;

class CProjectile : public CInfCEntity
{
public:
	CProjectile(CGameContext *pGameContext, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
		int Damage, bool Explosive, float Force, int SoundImpact, EDamageType DamageType);

	vec2 GetPos(float Time);
	void FillInfo(CNetObj_Projectile *pProj);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

private:
	vec2 m_Direction;
	int m_LifeSpan;
	int m_Type;
	int m_Damage;
	int m_SoundImpact;
	int m_Weapon;
	EDamageType m_DamageType;
	float m_Force;
	int m_StartTick;
	bool m_Explosive;
	
/* INFECTION MODIFICATION START ***************************************/
	bool m_IsFlashGrenade;
	int m_FlashRadius = 0;
	vec2 m_StartPos;
	TAKEDAMAGEMODE m_TakeDamageMode;
	
public:
	void FlashGrenade();
	void SetFlashRadius(int Radius);
/* INFECTION MODIFICATION END *****************************************/
};

#endif
