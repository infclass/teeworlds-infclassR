#ifndef GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
#define GAME_SERVER_INFCLASS_CLASSES_INFECTED_H

#include "../infcplayerclass.h"

#include <game/server/alloc.h>

class CInfClassInfected : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassInfected(CInfClassPlayer *pPlayer);

	bool IsHuman() const final { return false; }

	void OnCharacterSpawned() override;
	void GiveClassAttributes() override;

	void OnSlimeEffect(int Owner) override;

protected:
	int m_HealTick = 0;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
