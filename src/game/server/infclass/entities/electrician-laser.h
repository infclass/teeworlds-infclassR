#ifndef GAME_SERVER_ENTITIES_ELECTRICIAN_LASER_H
#define GAME_SERVER_ENTITIES_ELECTRICIAN_LASER_H

#include "infc-laser.h"

class CElectricianLaser : public CInfClassLaser
{
public:
	CElectricianLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner);

protected:
	bool HitCharacter(vec2 From, vec2 To) override;

};

#endif // GAME_SERVER_ENTITIES_ELECTRICIAN_LASER_H
