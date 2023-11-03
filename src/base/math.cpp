#include "math.h"

#include <random>

static std::random_device RandomDevice;
static std::mt19937 RandomEngine(RandomDevice());

bool random_prob(float f)
{
	return (random_float() < f);
}

int random_int(int Min, int Max)
{
	std::uniform_int_distribution<int> Distribution(Min, Max);
	return Distribution(RandomEngine);
}

int random_distribution(double* pProb, double* pProb2)
{
	std::discrete_distribution<int> Distribution(pProb, pProb2);
	return Distribution(RandomEngine);
}
