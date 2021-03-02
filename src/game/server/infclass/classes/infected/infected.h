#ifndef GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
#define GAME_SERVER_INFCLASS_CLASSES_INFECTED_H

#include "../infcplayerclass.h"

#include <game/server/alloc.h>

class CInfClassInfected : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassInfected();

	bool IsHuman() const final { return false; }
};

#endif // GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
