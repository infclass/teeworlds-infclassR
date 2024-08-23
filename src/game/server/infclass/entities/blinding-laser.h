#ifndef GAME_SERVER_ENTITIES_BLINDING_LASER_H
#define GAME_SERVER_ENTITIES_BLINDING_LASER_H

#include "infc-laser.h"

class CBlindingLaser : public CInfClassLaser
{
public:
	static void OnFired(CInfClassCharacter *pCharacter, WeaponFireContext *pFireContext);

	CBlindingLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, int Owner);

protected:
	bool OnCharacterHit(CInfClassCharacter *pHit) final;
	void DoBounce() final;

};

#endif
