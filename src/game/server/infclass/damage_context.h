#ifndef INFCLASS_DAMAGE_CONTEXT_H
#define INFCLASS_DAMAGE_CONTEXT_H

#include "base/vmath.h"

enum class DAMAGE_TYPE;
enum class TAKEDAMAGEMODE;

struct SDamageContext
{
	int Killer = -1;
	int Assistant = -1;
	DAMAGE_TYPE DamageType = static_cast<DAMAGE_TYPE>(0);
	int Weapon = 0;
	TAKEDAMAGEMODE Mode = static_cast<TAKEDAMAGEMODE>(0);
	int Damage = 0;
	vec2 Force = vec2(0, 0);
};

#endif // INFCLASS_DAMAGE_CONTEXT_H
