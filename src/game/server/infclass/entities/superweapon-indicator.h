/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_SUPERWEAPON_INDICATOR_H
#define GAME_SERVER_ENTITIES_SUPERWEAPON_INDICATOR_H

#include "infcentity.h"

#include <base/tl/array.h>

class CSuperWeaponIndicator : public CInfCEntity
{
public:
	CSuperWeaponIndicator(CGameContext *pGameContext, vec2 Pos, int Owner);
	~CSuperWeaponIndicator() override;
	
	virtual void Snap(int SnappingClient);
	virtual void Tick();

private:
	int m_StartTick;
	int m_warmUpCounter;
	bool m_IsWarmingUp;
	array<int> m_IDs;
	CCharacter *m_OwnerChar;
	
public:
	float m_Radius;
};

#endif
