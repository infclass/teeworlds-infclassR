#ifndef GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
#define GAME_SERVER_INFCLASS_CLASSES_HUMAN_H

#include "../infcplayerclass.h"

#include <game/server/alloc.h>

class CInfClassHuman : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	explicit CInfClassHuman();

	bool IsHuman() const final { return true; }

	void GiveClassAttributes() override;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_HUMAN_H
