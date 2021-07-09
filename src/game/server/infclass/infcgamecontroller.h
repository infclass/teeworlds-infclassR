/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INFCLASS_GAMECONTROLLER_H
#define GAME_SERVER_INFCLASS_GAMECONTROLLER_H

#include <engine/console.h>
#include <game/server/gamecontroller.h>
#include <game/server/gameworld.h>

class CInfClassCharacter;
class CInfClassPlayer;
class IConsole;

class CInfClassGameController : public IGameController
{
public:
	CInfClassGameController(class CGameContext *pGameServer);
	~CInfClassGameController() override;

	void IncreaseCurrentRoundCounter() override;

	void Tick();
	void Snap(int SnappingClient);
	// add more virtual functions here if you wish

	CPlayer *CreatePlayer(int ClientID) override;

	bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv) override;
	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	void OnCharacterSpawn(class CCharacter *pChr) override;
	void OnPlayerInfoChange(class CPlayer *pP) override;
	void DoWincheck() override;
	void StartRound() override;
	void EndRound() override;
	bool PreSpawn(CPlayer* pPlayer, vec2 *pPos) override;
	bool PickupAllowed(int Index) ;
	int ChooseHumanClass(const CPlayer *pPlayer) const;
	int ChooseInfectedClass(const CPlayer *pPlayer) const override;
	bool IsEnabledClass(int PlayerClass);
	bool IsChoosableClass(int PlayerClass);
	bool CanVote() override;
	void OnClientDrop(int ClientID, int Type) override;
	void OnPlayerInfected(CPlayer* pPlayer, CPlayer* pInfectiousPlayer) override;
	bool IsInfectionStarted() override;
	bool PortalsAvailableForCharacter(class CCharacter *pCharacter) override;
	bool AreTurretsEnabled() const;
	
	void ResetFinalExplosion();
	void SaveRoundRules();
	
	static bool IsDefenderClass(int PlayerClass);
	static bool IsSupportClass(int PlayerClass);
	static int GetClassByName(const char *pClassName, bool *pOk = nullptr);
	static const char *GetClassName(int PlayerClass);
	static const char *GetClassPluralName(int PlayerClass);
	static const char *GetClassDisplayName(int PlayerClass, const char *pDefaultText = nullptr);
	static const char *GetClassPluralDisplayName(int PlayerClass);
	static int MenuClassToPlayerClass(int MenuClass);

	void RegisterChatCommands(class IConsole *pConsole) override;

	static bool ConSetClass(IConsole::IResult *pResult, void *pUserData);
	bool ConSetClass(IConsole::IResult *pResult);

	static bool ChatWitch(IConsole::IResult *pResult, void *pUserData);
	bool ChatWitch(IConsole::IResult *pResult);

	using IGameController::GameServer;
	CGameWorld *GameWorld();
	IConsole *Console();
	CInfClassPlayer *GetPlayer(int ClientID) const;
	CInfClassCharacter *GetCharacter(int ClientID) const;

private:
	void MaybeSuggestMoreRounds();
	void SnapMapMenu(int SnappingClient, CNetObj_GameInfo *pGameInfoObj);
	void RewardTheKiller(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	bool IsSpawnable(vec2 Pos, int TeleZoneIndex) override;
	void GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected, int& NumFirstInfected);

	int RandomZombieToWitch();
	std::vector<int> m_WitchCallers;
	
private:	
	int m_MapWidth;
	int m_MapHeight;
	int* m_GrowingMap;
	bool m_ExplosionStarted;
	
	bool m_InfectedStarted;
	bool m_RoundStarted = false;
	bool m_TurretsEnabled = false;
	bool m_SuggestMoreRounds = false;
	bool m_MoreRoundsSuggested = false;
};
#endif
