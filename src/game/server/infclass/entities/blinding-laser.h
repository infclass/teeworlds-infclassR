#ifndef GAME_SERVER_ENTITIES_BLINDING_LASER_H
#define GAME_SERVER_ENTITIES_BLINDING_LASER_H

#include "infc-laser.h"

class CBlindingLaser : public CInfClassLaser
{
public:
	CBlindingLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, int Owner);

protected:
	bool HitCharacter(vec2 From, vec2 To) final;
	void DoBounce() final;

};

#endif
