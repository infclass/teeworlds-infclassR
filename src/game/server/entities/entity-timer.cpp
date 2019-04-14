/* (c) InfClass Contributors */
#include <engine/config.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include "entity-timer.h"

CEntityTimer::CEntityTimer(CGameWorld *pGameWorld, int Seconds, bool IsWarmUp)
{
	m_WarmUpTarget = pGameWorld->Server()->TickSpeed()*Seconds;
	m_CoolDownValue = pGameWorld->Server()->TickSpeed()*Seconds;
	m_IsWarmUp = IsWarmUp;
	Reset();

}

void CEntityTimer::Reset()
{
    m_WarmUpCounter = 0;
	m_CoolDownCounter = m_CoolDownValue;
}

void CEntityTimer::Tick()
{
	if (m_IsWarmUp)
	{
		m_WarmUpCounter++;
	} 
	else
	{
		m_CoolDownCounter--;
	}
	
	if ( HasReachedTarget() )
		Reset();
	
}

bool CEntityTimer::HasReachedTarget()
{
	return (m_WarmUpCounter == m_WarmUpTarget) || (m_CoolDownCounter == 0);
}
