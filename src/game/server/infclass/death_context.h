#ifndef INFCLASS_DEATH_CONTEXT_H
#define INFCLASS_DEATH_CONTEXT_H

enum class DAMAGE_TYPE;

struct DeathContext
{
	int Killer = -1;
	int Assistant = -1;
	DAMAGE_TYPE DamageType = static_cast<DAMAGE_TYPE>(0);
};

#endif // INFCLASS_DEATH_CONTEXT_H
