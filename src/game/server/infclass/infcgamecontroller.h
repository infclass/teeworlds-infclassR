/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INFCLASS_GAMECONTROLLER_H
#define GAME_SERVER_INFCLASS_GAMECONTROLLER_H

#include <game/server/gamecontroller.h>

#include <base/tl/array_on_stack.h>
#include <engine/console.h>

class CGameWorld;
class CInfClassCharacter;
class CInfClassPlayer;
struct CNetObj_GameInfo;

using ClientsArray = array_on_stack<int, 64>; // MAX_CLIENTS

enum class CLASS_AVAILABILITY
{
	AVAILABLE,
	DISABLED,
	NEED_MORE_PLAYERS,
	LIMIT_EXCEEDED,
};

class CInfClassGameController : public IGameController
{
public:
	CInfClassGameController(class CGameContext *pGameServer);
	~CInfClassGameController() override;

	void IncreaseCurrentRoundCounter() override;

	void Tick() override;
	void Snap(int SnappingClient) override;

	CPlayer *CreatePlayer(int ClientID) override;

	bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv) override;
	void HandleCharacterTiles(class CCharacter *pChr) override;

	int GetZoneValueAt(int ZoneHandle, const vec2 &Pos) const;
	int GetDamageZoneValueAt(const vec2 &Pos) const;
	int GetBonusZoneValueAt(const vec2 &Pos) const;

	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	void OnCharacterSpawn(class CCharacter *pChr) override;
	void OnPlayerInfoChange(class CPlayer *pP) override;
	void DoWincheck() override;
	void StartRound() override;
	void EndRound() override;
	bool PreSpawn(CPlayer* pPlayer, vec2 *pPos) override;
	int ChooseHumanClass(const CPlayer *pPlayer) const;
	int ChooseInfectedClass(const CPlayer *pPlayer) const override;
	bool IsEnabledClass(int PlayerClass);
	CLASS_AVAILABILITY GetPlayerClassAvailability(int PlayerClass);
	bool CanVote() override;
	void OnClientDrop(int ClientID, int Type) override;
	void OnPlayerInfected(CPlayer* pPlayer, CPlayer* pInfectiousPlayer) override;
	bool IsInfectionStarted() override;
	bool PortalsAvailableForCharacter(class CCharacter *pCharacter) override;
	bool AreTurretsEnabled() const;

	void ResetFinalExplosion();
	void SaveRoundRules();

	static bool IsZombieClass(int PlayerClass);
	static bool IsDefenderClass(int PlayerClass);
	static bool IsSupportClass(int PlayerClass);
	static int GetClassByName(const char *pClassName, bool *pOk = nullptr);
	static const char *GetClassName(int PlayerClass);
	static const char *GetClassPluralName(int PlayerClass);
	static const char *GetClassDisplayName(int PlayerClass, const char *pDefaultText = nullptr);
	static const char *GetClanForClass(int PlayerClass, const char *pDefaultText = nullptr);
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
	int GetPlayerOwnCursorID(int ClientID) const;

	void GetSortedTargetsInRange(const vec2 &Center, const float Radius, const ClientsArray &SkipList, ClientsArray *pOutput);

private:
	void ReservePlayerOwnSnapItems();
	void FreePlayerOwnSnapItems();

	void MaybeSuggestMoreRounds();
	void SnapMapMenu(int SnappingClient, CNetObj_GameInfo *pGameInfoObj);
	void RewardTheKiller(CInfClassCharacter *pVictim, CInfClassPlayer *pKiller, int Weapon);
	bool IsSpawnable(vec2 Pos, int TeleZoneIndex) override;
	void GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected, int& NumFirstInfected);

	int RandomZombieToWitch();
	std::vector<int> m_WitchCallers;

private:
	int m_MapWidth;
	int m_MapHeight;
	int* m_GrowingMap;
	bool m_ExplosionStarted;

	int m_PlayerOwnCursorID = -1;
	
	bool m_InfectedStarted;
	bool m_RoundStarted = false;
	bool m_TurretsEnabled = false;
	bool m_SuggestMoreRounds = false;
	bool m_MoreRoundsSuggested = false;
};
#endif
