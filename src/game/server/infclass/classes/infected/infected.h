#ifndef GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
#define GAME_SERVER_INFCLASS_CLASSES_INFECTED_H

#include "../infcplayerclass.h"

#include <game/server/alloc.h>

class CInfClassInfected : public CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassInfected(CInfClassPlayer *pPlayer);

	bool IsHuman() const final { return false; }
	int GetDefaultEmote() const override;
	bool CanDie() const override;

	void OnCharacterTick() override;
	void OnCharacterSpawned() override;

	void OnSlimeEffect(int Owner) override;
	void OnFloatingPointCollected(int Points) override;

	float GetGhoulPercent() const override;
	void IncreaseGhoulLevel(int Diff);
	int GetGhoulLevel() const;

	void PrepareToDie(int Killer, int Weapon, bool *pRefusedToDie) override;

protected:
	void GiveClassAttributes() override;
	void SetupSkin(CTeeInfo *output) override;

	int m_SlimeHealTick = 0;

	int m_VoodooTimeAlive = 0;
	int m_VoodooKiller; // Save killer + weapon for delayed kill message
	int m_VoodooWeapon = 0;
	bool m_VoodooAboutToDie = false;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_INFECTED_H
