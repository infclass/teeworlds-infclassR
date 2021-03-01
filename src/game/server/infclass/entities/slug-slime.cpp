/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <engine/shared/config.h>

#include "slug-slime.h"

CSlugSlime::CSlugSlime(CGameContext *pGameContext, vec2 Pos, int Owner)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_SLUG_SLIME, Pos, Owner)
{
	m_LifeSpan = Server()->TickSpeed()*Config()->m_InfSlimeDuration;
	GameWorld()->InsertEntity(this);
	m_HealTick = 0;
}

void CSlugSlime::Tick()
{
	if(m_MarkedForDestroy) return;
	
	if(m_LifeSpan <= 0)
	{
		GameServer()->m_World.DestroyEntity(this);
		return;
	}
	
	// Find other players
	for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
	{
		if(!GameServer()->Collision()->AreConnected(p->m_Pos, m_Pos, 84.0f))
			continue; // not in reach
		
		if(p->IsZombie()) 
		{
			if(p->GetClass() != PLAYERCLASS_SLUG)
			{
				p->SetEmote(EMOTE_HAPPY, Server()->Tick());
				if(Server()->Tick() >= m_HealTick + (Server()->TickSpeed()/Config()->m_InfSlimeHealRate))
				{
					m_HealTick = Server()->Tick();
					p->IncreaseHealth(1);
				}
			}
		} 
		else // p->IsHuman()
		{ 
			p->Poison(Config()->m_InfSlimePoisonDuration, m_Owner); 
		}
	}
	
	if(random_prob(0.2f))
	{
		GameServer()->CreateDeath(m_Pos, m_Owner);
	}
	
	m_LifeSpan--;
}

int CSlugSlime::GetLifeSpan() const
{
	return m_LifeSpan;
}

int CSlugSlime::GetMaxLifeSpan()
{
	return Server()->TickSpeed()*Config()->m_InfSlimeDuration;
}

void CSlugSlime::Replenish(int PlayerID)
{
	m_Owner = PlayerID;
	m_LifeSpan = GetMaxLifeSpan();
}
