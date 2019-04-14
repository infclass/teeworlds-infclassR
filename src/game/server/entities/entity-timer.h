/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_ENTITY_TIMER
#define GAME_SERVER_ENTITIES_ENTITY_TIMER
#include <game/server/gameworld.h>

class CEntityTimer
{
	public:
		CEntityTimer(CGameWorld *pGameWorld, int Seconds, bool IsWarmUp = false);
		
		virtual void Reset();
		virtual void Tick();
		
		bool HasReachedTarget();

	private:
		bool m_IsWarmUp;
		int m_CoolDownValue;
		int m_WarmUpTarget;
		
		int m_WarmUpCounter;
		int m_CoolDownCounter;
};

#endif
