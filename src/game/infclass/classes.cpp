#include "classes.h"

#include "base/tl/ic_array.h"

const icArray<PLAYERCLASS, NB_PLAYERCLASS> &AllPlayerClasses()
{
	static icArray<PLAYERCLASS, NB_PLAYERCLASS> Classes;
	if(Classes.IsEmpty())
	{
		for(int i = START_HUMANCLASS + 1; i < END_HUMANCLASS; ++i)
		{
			Classes.Add(static_cast<PLAYERCLASS>(i));
		}
		for(int i = START_INFECTEDCLASS + 1; i < END_INFECTEDCLASS; ++i)
		{
			Classes.Add(static_cast<PLAYERCLASS>(i));
		}
	}
	return Classes;
}

const icArray<PLAYERCLASS, NB_HUMANCLASS> &AllHumanClasses()
{
	static icArray<PLAYERCLASS, NB_HUMANCLASS> Classes;
	if(Classes.IsEmpty())
	{
		for(int i = START_HUMANCLASS + 1; i < END_HUMANCLASS; ++i)
		{
			Classes.Add(static_cast<PLAYERCLASS>(i));
		}
	}
	return Classes;
}

const icArray<PLAYERCLASS, NB_INFECTEDCLASS> &AllInfectedClasses()
{
	static icArray<PLAYERCLASS, NB_INFECTEDCLASS> Classes;
	if(Classes.IsEmpty())
	{
		for(int i = START_INFECTEDCLASS + 1; i < END_INFECTEDCLASS; ++i)
		{
			Classes.Add(static_cast<PLAYERCLASS>(i));
		}
	}
	return Classes;
}
