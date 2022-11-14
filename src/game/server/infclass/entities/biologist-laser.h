/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_BIOLOGIST_LASER_H
#define GAME_SERVER_ENTITIES_BIOLOGIST_LASER_H

#include "infc-laser.h"

class CBiologistLaser : public CInfClassLaser
{
public:
	CBiologistLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, int Owner, int Dmg);

protected:
	bool HitCharacter(vec2 From, vec2 To) final;
	void DoBounce() final;
};

#endif
