#include "damage_type.h"

#include <base/tl/ic_enum.h>

static const char *gs_aDamageTypeNames[] = {
	"invalid",
	"unused1",

	"hammer",
	"gun",
	"shotgun",
	"grenade",
	"laser",
	"ninja",

	"sniper_rifle",
	"scientist_laser",
	"medic_shotgun",
	"biologist_shotgun",
	"looper_laser",
	"mercenary_gun",
	"mercenary_grenade",
	"scientist_teleport",
	"stunning_grenade",

	"laser_wall",
	"soldier_bomb",
	"scientist_mine",
	"biologist_mine",
	"mercenary_bomb",
	"white_hole",
	"turret_destruction",
	"turret_laser",
	"turret_plasma",

	"infection_hammer",
	"bite",
	"boomer_explosion",
	"slug_slime",
	"drying_hook",

	"death_tile",
	"infection_tile",

	"game",
	"kill_command",
	"game_final_explosion",
	"game_infection",

	"no_damage",
	"medic_revival",
	"damage_tile",
};

const char *toString(EDamageType DamageType)
{
	return toStringImpl(DamageType, gs_aDamageTypeNames);
}
