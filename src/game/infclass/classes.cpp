#include "classes.h"

namespace
{

enum PLAYERCLASS
{
	PLAYERCLASS_INVALID = -1,
	PLAYERCLASS_NONE = 0,

	START_HUMANCLASS,
	PLAYERCLASS_MERCENARY,
	PLAYERCLASS_MEDIC,
	PLAYERCLASS_HERO,
	PLAYERCLASS_ENGINEER,
	PLAYERCLASS_SOLDIER,
	PLAYERCLASS_NINJA,
	PLAYERCLASS_SNIPER,
	PLAYERCLASS_SCIENTIST,
	PLAYERCLASS_BIOLOGIST,
	PLAYERCLASS_LOOPER,
	END_HUMANCLASS,

	START_INFECTEDCLASS,
	PLAYERCLASS_SMOKER,
	PLAYERCLASS_BOOMER,
	PLAYERCLASS_HUNTER,
	PLAYERCLASS_BAT,
	PLAYERCLASS_GHOST,
	PLAYERCLASS_SPIDER,
	PLAYERCLASS_GHOUL,
	PLAYERCLASS_SLUG,
	PLAYERCLASS_VOODOO,
	PLAYERCLASS_WITCH,
	PLAYERCLASS_UNDEAD,
	PLAYERCLASS_TANK,
	END_INFECTEDCLASS,

	NB_PLAYERCLASS,
	NB_HUMANCLASS = END_HUMANCLASS - START_HUMANCLASS - 1,
	NB_INFECTEDCLASS = END_INFECTEDCLASS - START_INFECTEDCLASS - 1,
};

}

int toNetValue(EPlayerClass C)
{
	if (C == EPlayerClass::None)
		return PLAYERCLASS_NONE;

	int Value = static_cast<int>(C);
	if (IsInfectedClass(C))
	{
		constexpr int InfectedClassOffset = PLAYERCLASS_SMOKER - static_cast<int>(EPlayerClass::Smoker);
		return Value + InfectedClassOffset;
	}
	else
	{
		constexpr int HumanClassOffset = PLAYERCLASS_MERCENARY - static_cast<int>(EPlayerClass::Mercenary);
		return Value + HumanClassOffset;
	}
}

const char *toString(EPlayerClass PlayerClass)
{
	switch (PlayerClass)
	{
	case EPlayerClass::None:
		return "none";

	case EPlayerClass::Mercenary:
		return "mercenary";
	case EPlayerClass::Medic:
		return "medic";
	case EPlayerClass::Hero:
		return "hero";
	case EPlayerClass::Engineer:
		return "engineer";
	case EPlayerClass::Soldier:
		return "soldier";
	case EPlayerClass::Ninja:
		return "ninja";
	case EPlayerClass::Sniper:
		return "sniper";
	case EPlayerClass::Scientist:
		return "scientist";
	case EPlayerClass::Biologist:
		return "biologist";
	case EPlayerClass::Looper:
		return "looper";

	case EPlayerClass::Smoker:
		return "smoker";
	case EPlayerClass::Boomer:
		return "boomer";
	case EPlayerClass::Hunter:
		return "hunter";
	case EPlayerClass::Bat:
		return "bat";
	case EPlayerClass::Ghost:
		return "ghost";
	case EPlayerClass::Spider:
		return "spider";
	case EPlayerClass::Ghoul:
		return "ghoul";
	case EPlayerClass::Slug:
		return "slug";
	case EPlayerClass::Voodoo:
		return "voodoo";
	case EPlayerClass::Witch:
		return "witch";
	case EPlayerClass::Undead:
		return "undead";

	case EPlayerClass::Invalid:
	case EPlayerClass::Count:
		break;
	}

	return "unknown";
}
