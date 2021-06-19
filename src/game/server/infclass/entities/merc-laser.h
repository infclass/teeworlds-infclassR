/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INFCLASS_ENTITIES_MERC_LASER_H
#define GAME_SERVER_INFCLASS_ENTITIES_MERC_LASER_H

#include "infc-laser.h"

class CMercenaryLaser : public CInfClassLaser
{
public:
	CMercenaryLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner);

	bool HitCharacter(vec2 From, vec2 To) override;
	void DoBounce() override;
};

#endif // GAME_SERVER_INFCLASS_ENTITIES_MERC_LASER_H
