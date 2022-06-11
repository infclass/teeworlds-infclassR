#ifndef INFCLASS_DAMAGE_TYPE_H
#define INFCLASS_DAMAGE_TYPE_H

// Everything that can kill should be listed here
enum class DAMAGE_TYPE
{
	INVALID,

	UNUSED1,

	// Standard weapons
	HAMMER, // The damage from hammer resulted in death
	GUN, // The default gun from vanilla
	SHOTGUN, // The default shotgun from vanilla
	GRENADE, // The default grenade from vanilla
	LASER, // The default laser from vanilla
	NINJA,

	// Mod custom weapons
	SNIPER_RIFLE,
	SCIENTIST_LASER, // Explosive laser
	MEDIC_SHOTGUN, // Keep this different from the default shotgun just for a case
	BIOLOGIST_SHOTGUN,
	LOOPER_LASER,
	MERCENARY_GUN,
	MERCENARY_GRENADE,
	SCIENTIST_TELEPORT, // Self damage
	STUNNING_GRENADE, // Only indirect kill via DEATH_TILE

	// Structures
	LASER_WALL, // Engineer wall
	SOLDIER_BOMB,
	SCIENTIST_MINE,
	BIOLOGIST_MINE,
	MERCENARY_BOMB,
	WHITE_HOLE, // Explosion
	TURRET_DESTRUCTION,
	TURRET_LASER,
	TURRET_PLASMA,

	// Infection weapons
	INFECTION_HAMMER, // TAKEDAMAGEMODE::INFECTION
	BITE, // Hammers are only for humans
	BOOMER_EXPLOSION,
	SLUG_SLIME,
	DRYING_HOOK,

	// Tiles
	DEATH_TILE,
	INFECTION_TILE, // Not a real damage type but as we show kill message for that...

	// Game special
	GAME, // Disconnect or joining spec
	KILL_COMMAND, // Self kill command
	GAME_FINAL_EXPLOSION,
	GAME_INFECTION,

	NO_DAMAGE, // Sometimes we need DAMAGE_TYPE for the API and INVALID value does not fit
	MEDIC_REVIVAL,
	DAMAGE_TILE,
};

#endif // INFCLASS_DAMAGE_TYPE_H
