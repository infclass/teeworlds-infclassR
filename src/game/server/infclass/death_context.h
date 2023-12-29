#ifndef INFCLASS_DEATH_CONTEXT_H
#define INFCLASS_DEATH_CONTEXT_H

enum class EDamageType;

struct DeathContext
{
	int Killer = -1;
	int Assistant = -1;
	EDamageType DamageType = static_cast<EDamageType>(0);
};

#endif // INFCLASS_DEATH_CONTEXT_H
