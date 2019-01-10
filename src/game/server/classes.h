#ifndef GAME_SERVER_CLASSES_H
#define GAME_SERVER_CLASSES_H

enum
{
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
	PLAYERCLASS_KING, // must always be last, see comment below
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
	PLAYERCLASS_WITCH,
	PLAYERCLASS_UNDEAD,
	END_INFECTEDCLASS,
	
	NB_PLAYERCLASS,
	/* count the number of human classes excluding N last classes
	 * which are considered special and should not be part
	 * of calculations, relying on NB_HUMANCLASS. Example of such
	 * calculations is class of the day.
	 * The value to subtract is N = (number of special classes + 1) */
	NB_HUMANCLASS = END_HUMANCLASS - START_HUMANCLASS - 2,
	NB_INFECTEDCLASS = END_INFECTEDCLASS - START_INFECTEDCLASS - 1,
};

#endif
