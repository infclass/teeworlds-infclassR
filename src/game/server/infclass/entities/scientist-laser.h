/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_SCILASER_H
#define GAME_SERVER_ENTITIES_SCILASER_H

#include "infc-laser.h"

class CScientistLaser : public CInfClassLaser
{
public:
	CScientistLaser(CGameContext *pGameContext, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Dmg);

protected:
	bool OnCharacterHit(CInfClassCharacter *pHit) final;
	void DoBounce() final;
	void CreateWhiteHole(const vec2 &CenterPos, const vec2 &To);
};

#endif
