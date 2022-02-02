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
struct SpawnContext;

enum class TAKEDAMAGEMODE;
enum class DAMAGE_TYPE;

using ClientsArray = array_on_stack<int, 64>; // MAX_CLIENTS

enum class ROUND_TYPE
{
	NORMAL,
	FUN,
	FAST,
};

enum class CLASS_AVAILABILITY
{
	AVAILABLE,
	DISABLED,
	NEED_MORE_PLAYERS,
	LIMIT_EXCEEDED,
};

static const char *toString(ROUND_TYPE RoundType);

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
	void HandleCharacterTiles(class CCharacter *pChr);
	void HandleLastHookers();

	int64_t GetBlindCharactersMask(int ExcludeCID) const;

	int GetZoneValueAt(int ZoneHandle, const vec2 &Pos) const;
	int GetDamageZoneValueAt(const vec2 &Pos) const;
	int GetBonusZoneValueAt(const vec2 &Pos) const;

	void CreateExplosion(const vec2 &Pos, int Owner, DAMAGE_TYPE DamageType, float DamageFactor = 1.0f);
	void CreateExplosionDisk(vec2 Pos, float InnerRadius, float DamageRadius, int Damage, float Force, int Owner, DAMAGE_TYPE DamageType);
	void CreateExplosionDiskGfx(vec2 Pos, float InnerRadius, float DamageRadius, int Owner);

	void SendHammerDot(const vec2 &Pos, int SnapID);

	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	void OnCharacterDeath(CInfClassCharacter *pVictim, DAMAGE_TYPE DamageType, int Killer, int Assistant);
	void OnCharacterSpawned(CInfClassCharacter *pCharacter);
	void DoWincheck() override;
	void StartRound() override;
	void EndRound() override;
	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true) override;
	bool TryRespawn(CInfClassPlayer *pPlayer, SpawnContext *pContext);
	int ChooseHumanClass(const CPlayer *pPlayer) const;
	int ChooseInfectedClass(const CPlayer *pPlayer) const override;
	bool GetPlayerClassEnabled(int PlayerClass) const;
	int GetMinPlayersForClass(int PlayerClass) const;
	int GetClassPlayerLimit(int PlayerClass) const;
	int GetPlayerClassProbability(int PlayerClass) const;

	ROUND_TYPE GetRoundType() const;

	CLASS_AVAILABILITY GetPlayerClassAvailability(int PlayerClass) const;
	bool CanVote() override;
	void OnPlayerDisconnect(CPlayer *pPlayer, int Type, const char *pReason) override;
	void OnPlayerInfected(CInfClassPlayer *pPlayer, CInfClassPlayer *pInfectiousPlayer, int PreviousClass);
	bool IsInfectionStarted() const;
	bool CanJoinTeam(int Team, int ClientID) override;
	bool AreTurretsEnabled() const;
	bool MercBombsEnabled() const;
	bool WhiteHoleEnabled() const;

	float GetTimeLimit() const;
	float GetInfectionDelay() const;

	int GetTargetToKill() const;
	void TargetKilled();
	void EnableTargetToKill() { m_TargetToKill = (m_TargetToKill < 0 ? -1 : m_TargetToKill); }
	void DisableTargetToKill() { m_TargetToKill = -2; }
	int GetTargetToKillCoolDown() { return m_TargetToKillCoolDown; }

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
	static int DamageTypeToWeapon(DAMAGE_TYPE DamageType, TAKEDAMAGEMODE *pMode = nullptr);
	static TAKEDAMAGEMODE DamageTypeToDamageMode(DAMAGE_TYPE DamageType);

	void RegisterChatCommands(class IConsole *pConsole) override;

	static bool ConSetClientName(IConsole::IResult *pResult, void *pUserData);
	static bool ConSetClass(IConsole::IResult *pResult, void *pUserData);
	bool ConSetClass(IConsole::IResult *pResult);

	static bool ConStartFastRound(IConsole::IResult *pResult, void *pUserData);
	static bool ConQueueFastRound(IConsole::IResult *pResult, void *pUserData);
	static bool ConQueueFunRound(IConsole::IResult *pResult, void *pUserData);

	static bool ChatWitch(IConsole::IResult *pResult, void *pUserData);
	bool ChatWitch(IConsole::IResult *pResult);

	using IGameController::GameServer;
	CGameWorld *GameWorld();
	IConsole *Console() const;
	CInfClassPlayer *GetPlayer(int ClientID) const;
	CInfClassCharacter *GetCharacter(int ClientID) const;
	int GetPlayerOwnCursorID(int ClientID) const;

	void SortCharactersByDistance(const ClientsArray &Input, ClientsArray *pOutput, const vec2 &Center, const float MaxDistance = 0);
	void GetSortedTargetsInRange(const vec2 &Center, const float Radius, const ClientsArray &SkipList, ClientsArray *pOutput);
	int GetMinimumInfected() const;

	void SendKillMessage(int Victim, DAMAGE_TYPE DamageType, int Killer = -1, int Assistant = -1);

protected:
	void TickInfectionStarted();
	void TickInfectionNotStarted();

	void MaybeSendStatistics();
	void AnnounceTheWinner(int NumHumans, int Seconds);

private:
	void HandleTargetsToKill();

	void ReservePlayerOwnSnapItems();
	void FreePlayerOwnSnapItems();

	void MaybeSuggestMoreRounds();
	void SnapMapMenu(int SnappingClient, CNetObj_GameInfo *pGameInfoObj);
	void FallInLoveIfInfectedEarly(CInfClassCharacter *pCharacter);
	void RewardTheKiller(CInfClassCharacter *pVictim, CInfClassPlayer *pKiller, int Weapon);
	bool IsSpawnable(vec2 Pos, int TeleZoneIndex) override;
	void GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected);
	int GetMinimumInfectedForPlayers(int PlayersNumber) const;

	int RandomZombieToWitch();
	ClientsArray m_WitchCallers;

private:
	int m_MapWidth;
	int m_MapHeight;
	int* m_GrowingMap;
	bool m_ExplosionStarted;

	int m_TargetToKill;
	int m_TargetToKillCoolDown;

	int m_PlayerOwnCursorID = -1;

	ROUND_TYPE m_RoundType = ROUND_TYPE::NORMAL;
	ROUND_TYPE m_QueuedRoundType = ROUND_TYPE::NORMAL;
	
	bool m_InfectedStarted;
	bool m_RoundStarted = false;
	bool m_TurretsEnabled = false;
	bool m_SuggestMoreRounds = false;
	bool m_MoreRoundsSuggested = false;
};
#endif
