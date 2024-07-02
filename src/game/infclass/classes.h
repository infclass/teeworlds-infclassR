#pragma once

#include "base/tl/ic_array.h"

enum class EPlayerClass
{
	Invalid = -1,
	Random = 0,
	None = 0,

	Mercenary,
	Medic,
	Hero,
	Engineer,
	Soldier,
	Ninja,
	Sniper,
	Scientist,
	Biologist,
	Looper,

	Smoker,
	Boomer,
	Hunter,
	Bat,
	Ghost,
	Spider,
	Ghoul,
	Slug,
	Voodoo,
	Witch,
	Undead,

	Count
};

constexpr int NB_PLAYERCLASS = static_cast<int>(EPlayerClass::Count);

static constexpr icArray<EPlayerClass, NB_PLAYERCLASS> AllHumanClasses{
	EPlayerClass::None,

	EPlayerClass::Mercenary,
	EPlayerClass::Medic,
	EPlayerClass::Hero,
	EPlayerClass::Engineer,
	EPlayerClass::Soldier,
	EPlayerClass::Ninja,
	EPlayerClass::Sniper,
	EPlayerClass::Scientist,
	EPlayerClass::Biologist,
	EPlayerClass::Looper,
};

static constexpr icArray<EPlayerClass, NB_PLAYERCLASS> AllInfectedClasses{
	EPlayerClass::Smoker,
	EPlayerClass::Boomer,
	EPlayerClass::Hunter,
	EPlayerClass::Bat,
	EPlayerClass::Ghost,
	EPlayerClass::Spider,
	EPlayerClass::Ghoul,
	EPlayerClass::Slug,
	EPlayerClass::Voodoo,
	EPlayerClass::Witch,
	EPlayerClass::Undead,
};

static constexpr icArray<EPlayerClass, NB_PLAYERCLASS> AllPlayerClasses{
	EPlayerClass::None,

	EPlayerClass::Mercenary,
	EPlayerClass::Medic,
	EPlayerClass::Hero,
	EPlayerClass::Engineer,
	EPlayerClass::Soldier,
	EPlayerClass::Ninja,
	EPlayerClass::Sniper,
	EPlayerClass::Scientist,
	EPlayerClass::Biologist,
	EPlayerClass::Looper,

	EPlayerClass::Smoker,
	EPlayerClass::Boomer,
	EPlayerClass::Hunter,
	EPlayerClass::Bat,
	EPlayerClass::Ghost,
	EPlayerClass::Spider,
	EPlayerClass::Ghoul,
	EPlayerClass::Slug,
	EPlayerClass::Voodoo,
	EPlayerClass::Witch,
	EPlayerClass::Undead,
};

constexpr int NB_HUMANCLASS = AllHumanClasses.Size();
constexpr int NB_INFECTEDCLASS = AllInfectedClasses.Size();
static_assert(NB_HUMANCLASS + NB_INFECTEDCLASS == NB_PLAYERCLASS);
static_assert(AllPlayerClasses.Size() == NB_PLAYERCLASS);

inline bool IsHumanClass(EPlayerClass C)
{
	return AllHumanClasses.Contains(C);
}
inline bool IsInfectedClass(EPlayerClass C)
{
	return AllInfectedClasses.Contains(C);
}

int toNetValue(EPlayerClass C);

const char *toString(EPlayerClass PlayerClass);
