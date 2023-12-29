/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INFCLASS_GAMECONTROLLER_H
#define GAME_SERVER_INFCLASS_GAMECONTROLLER_H

#include <game/infclass/classes.h>
#include <game/server/gamecontroller.h>
#include <game/server/teams.h>

#include <base/tl/ic_array.h>
#include <engine/console.h>

class CGameWorld;
class CHintMessage;
class CInfClassCharacter;
class CInfClassPlayer;
struct CNetObj_GameInfo;
struct SpawnContext;
struct DeathContext;
struct ZoneData;

enum class TAKEDAMAGEMODE;
enum class EDamageType;
enum class ROUND_CANCELATION_REASON;
enum class ROUND_END_REASON;
enum class EPlayerScoreMode;

static const int MaxWaves = 20;

using ClientsArray = icArray<int, MAX_CLIENTS>;

enum class ERoundType
{
	Normal,
	Fun,
	Fast,
	Survival,
	Count,
	Invalid = Count,
};

enum class CLASS_AVAILABILITY
{
	AVAILABLE,
	DISABLED,
	NEED_MORE_PLAYERS,
	LIMIT_EXCEEDED,
};

static const char *toString(ERoundType RoundType);

struct FunRoundConfiguration
{
	FunRoundConfiguration() = default;
	FunRoundConfiguration(PLAYERCLASS Infected, PLAYERCLASS Human) :
		InfectedClass(Infected),
		HumanClass(Human)
	{
	}

	PLAYERCLASS InfectedClass = PLAYERCLASS_INVALID;
	PLAYERCLASS HumanClass = PLAYERCLASS_INVALID;
};

class CInfClassGameController : public IGameController
{
public:
	CInfClassGameController(class CGameContext *pGameServer);
	~CInfClassGameController() override;

	void IncreaseCurrentRoundCounter() override;

	void DoTeamBalance() override;

	void TickBeforeWorld() override;
	void Tick() override;
	void OnGameRestart() override;
	void Snap(int SnappingClient) override;

	CPlayer *CreatePlayer(int ClientID, bool IsSpectator, void *pData) override;
	int PersistentClientDataSize() const override;
	bool GetClientPersistentData(int ClientID, void *pData) const override;

	bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv) override;
	void HandleCharacterTiles(CInfClassCharacter *pCharacter);
	void HandleLastHookers();

	bool CanSeeDetails(int Who, int Whom) const;
	int64_t GetBlindCharactersMask(int ExcludeCID) const;
	int64_t GetMaskForPlayerWorldEvent(int Asker, int ExceptID = -1);

	bool HumanWallAllowedInPos(const vec2 &Pos) const;
	int GetZoneValueAt(int ZoneHandle, const vec2 &Pos, ZoneData *pData = nullptr) const;
	int GetDamageZoneValueAt(const vec2 &Pos, ZoneData *pData = nullptr) const;
	EZoneTele GetTeleportZoneValueAt(const vec2 &Pos, ZoneData *pData = nullptr) const;
	int GetBonusZoneValueAt(const vec2 &Pos, ZoneData *pData = nullptr) const;

	void ExecuteFileEx(const char *pBaseName);

	void CreateExplosion(const vec2 &Pos, int Owner, EDamageType DamageType, float DamageFactor = 1.0f);
	void CreateExplosionDisk(vec2 Pos, float InnerRadius, float DamageRadius, int Damage, float Force, int Owner, EDamageType DamageType);
	void CreateExplosionDiskGfx(vec2 Pos, float InnerRadius, float DamageRadius, int Owner);

	void SendHammerDot(const vec2 &Pos, int SnapID);
	void SendServerParams(int ClientID) const;

	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	void OnCharacterDeath(CInfClassCharacter *pVictim, const DeathContext &Context);
	void OnCharacterSpawned(CInfClassCharacter *pCharacter, const SpawnContext &Context);
	void OnClassChooserRequested(CInfClassCharacter *pCharacter);
	void CheckRoundFailed();
	void DoWincheck() override;
	void StartRound() override;
	void ResetRoundData();
	void EndRound() override;
	void EndRound(ROUND_END_REASON Reason);
	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true) override;
	bool TryRespawn(CInfClassPlayer *pPlayer, SpawnContext *pContext);
	PLAYERCLASS ChooseHumanClass(const CInfClassPlayer *pPlayer) const;
	PLAYERCLASS ChooseInfectedClass(const CInfClassPlayer *pPlayer) const;

	bool GetPlayerClassEnabled(PLAYERCLASS PlayerClass) const;
	bool SetPlayerClassEnabled(PLAYERCLASS PlayerClass, bool Enabled);
	bool SetPlayerClassProbability(int PlayerClass, int Probability);

	int GetMinPlayersForClass(PLAYERCLASS PlayerClass) const;
	int GetClassPlayerLimit(PLAYERCLASS PlayerClass) const;
	int GetPlayerClassProbability(PLAYERCLASS PlayerClass) const;

	int GetInfectedCount(PLAYERCLASS InfectedPlayerClass = PLAYERCLASS_INVALID) const;
	int GetMinPlayers() const;

	ERoundType GetDefaultRoundType() const;
	ERoundType GetRoundType() const;
	void QueueRoundType(ERoundType RoundType);

	CLASS_AVAILABILITY GetPlayerClassAvailability(PLAYERCLASS PlayerClass, const CInfClassPlayer *pForPlayer = nullptr) const;
	bool CanVote() override;

	void OnPlayerVoteCommand(int ClientID, int Vote) override;
	void OnPlayerClassChanged(CInfClassPlayer *pPlayer);

	void OnPlayerConnect(CPlayer *pPlayer) override;
	void OnPlayerDisconnect(CPlayer *pBasePlayer, EClientDropType Type, const char *pReason) override;

	void OnReset() override;

	void DoPlayerInfection(CInfClassPlayer *pPlayer, CInfClassPlayer *pInfectiousPlayer, PLAYERCLASS PreviousClass);

	void OnHeroFlagCollected(int ClientID);
	float GetHeroFlagCooldown() const;

	bool IsInfectionStarted() const;
	bool MapRotationEnabled() const override;
	void OnTeamChangeRequested(int ClientID, int Team) override;
	bool CanJoinTeam(int Team, int ClientID) override;
	bool AreTurretsEnabled() const;
	bool MercBombsEnabled() const;
	bool WhiteHoleEnabled() const;
	float GetWhiteHoleLifeSpan() const;
	int MinimumInfectedForRevival() const;
	bool IsClassChooserEnabled() const;
	int GetTaxiMode() const;
	int GetGhoulStomackSize() const;
	EPlayerScoreMode GetPlayerScoreMode(int SnappingClient) const;

	float GetTimeLimit() const;
	float GetInfectionDelay() const;

	bool IsSpawnable(vec2 Pos, EZoneTele TeleZoneIndex);

	const ClientsArray &GetValidNinjaTargets() const { return m_NinjaTargets; }

	bool HeroGiftAvailable() const;
	bool GetHeroFlagPosition(vec2 *pFlagPosition) const;
	bool IsPositionAvailableForHumans(const vec2 &FlagPosition) const;

	void StartFunRound();
	void EndFunRound();

	void StartSurvivalRound();
	void EndSurvivalRound();

	void ResetFinalExplosion();
	void SaveRoundRules();
	void StartSurvivalGame();
	void EndSurvivalGame();

	int GetRoundTick() const;
	int GetInfectionTick() const;
	int GetInfectionStartTick() const;

	static bool IsDefenderClass(PLAYERCLASS PlayerClass);
	static bool IsSupportClass(PLAYERCLASS PlayerClass);
	static PLAYERCLASS GetClassByName(const char *pClassName, bool *pOk = nullptr);
	static const char *GetClassName(PLAYERCLASS PlayerClass);
	static const char *GetClassPluralName(PLAYERCLASS PlayerClass);
	static const char *GetClassDisplayName(PLAYERCLASS PlayerClass, const char *pDefaultText = nullptr);
	static const char *GetClanForClass(PLAYERCLASS PlayerClass, const char *pDefaultText = nullptr);
	static const char *GetClassPluralDisplayName(PLAYERCLASS PlayerClass);
	static PLAYERCLASS MenuClassToPlayerClass(int MenuClass);
	static int DamageTypeToWeapon(EDamageType DamageType, TAKEDAMAGEMODE *pMode = nullptr);

	int GetPlayerTeam(int ClientID) const override;
	void SetPlayerInfected(int ClientID, bool Infected);

	void RegisterChatCommands(class IConsole *pConsole) override;

	static void ConSetClientName(IConsole::IResult *pResult, void *pUserData);
	static void ConRestoreClientName(IConsole::IResult *pResult, void *pUserData);
	static void ConLockClientName(IConsole::IResult *pResult, void *pUserData);
	static void ConPreferClass(IConsole::IResult *pResult, void *pUserData);
	static void ConAlwaysRandom(IConsole::IResult *pResult, void *pUserData);
	void SetPreferredClass(int ClientID, const char *pClassName);
	void SetPreferredClass(int ClientID, PLAYERCLASS Class);
	static void ConAntiPing(IConsole::IResult *pResult, void *pUserData);

	static void ConUserSetClass(IConsole::IResult *pResult, void *pUserData);
	void ConUserSetClass(IConsole::IResult *pResult);
	static void ConSetClass(IConsole::IResult *pResult, void *pUserData);
	void ConSetClass(IConsole::IResult *pResult);

	static FunRoundConfiguration ParseFunRoundConfigArguments(IConsole::IResult *pResult);

	static void ConQueueSpecialRound(IConsole::IResult *pResult, void *pUserData);
	static void ConStartRound(IConsole::IResult *pResult, void *pUserData);
	static void ConStartFunRound(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueFunRound(IConsole::IResult *pResult, void *pUserData);
	static void ConStartSpecialFunRound(IConsole::IResult *pResult, void *pUserData);
	static void ConClearFunRounds(IConsole::IResult *pResult, void *pUserData);
	static void ConAddFunRound(IConsole::IResult *pResult, void *pUserData);

	static void ConStartFastRound(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueFastRound(IConsole::IResult *pResult, void *pUserData);
	static void ConPrintPlayerPickingTimestamp(IConsole::IResult *pResult, void *pUserData);
	void ConPrintPlayerPickingTimestamp(IConsole::IResult *pResult);
	static void ConMapRotationStatus(IConsole::IResult *pResult, void *pUserData);
	static void ConSaveMapsData(IConsole::IResult *pResult, void *pUserData);
	static void ConPrintMapsData(IConsole::IResult *pResult, void *pUserData);
	static void ConResetMapData(IConsole::IResult *pResult, void *pUserData);
	static void ConAddMapData(IConsole::IResult *pResult, void *pUserData);
	static void ConSetMapMinMaxPlayers(IConsole::IResult *pResult, void *pUserData);
	void ConSetMapMinMaxPlayers(IConsole::IResult *pResult);
	static void ConSavePosition(IConsole::IResult *pResult, void *pUserData);
	void ConSavePosition(IConsole::IResult *pResult);
	static void ConLoadPosition(IConsole::IResult *pResult, void *pUserData);
	void ConLoadPosition(IConsole::IResult *pResult);

	static void ConSetHealthArmor(IConsole::IResult *pResult, void *pUserData);
	void ConSetHealthArmor(IConsole::IResult *pResult);	
	static void ConSetInvincible(IConsole::IResult *pResult, void *pUserData);
	void ConSetInvincible(IConsole::IResult *pResult);
	static void ConSetHookProtection(IConsole::IResult *pResult, void *pUserData);
	void ConSetHookProtection(IConsole::IResult *pResult);

	static void ChatWitch(IConsole::IResult *pResult, void *pUserData);
	void ChatWitch(IConsole::IResult *pResult);

	using IGameController::GameServer;
	CGameWorld *GameWorld();
	IConsole *Console() const;
	CInfClassPlayer *GetPlayer(int ClientID) const;
	CInfClassCharacter *GetCharacter(int ClientID) const;
	int GetPlayerOwnCursorID(int ClientID) const;

	void SortCharactersByDistance(ClientsArray *pCharacterIds, const vec2 &Center, const float MaxDistance = 0);
	void SortCharactersByDistance(const ClientsArray &Input, ClientsArray *pOutput, const vec2 &Center, const float MaxDistance = 0);
	void GetSortedTargetsInRange(const vec2 &Center, const float Radius, const ClientsArray &SkipList, ClientsArray *pOutput);
	int GetMinimumInfected() const;
	int InfectedBonusArmor() const;

	void SendKillMessage(int Victim, const DeathContext &Context);

protected:
	void RoundTickBeforeInitialInfection();
	void RoundTickAfterInitialInfection();

	void PreparePlayerToJoin(CInfClassPlayer *pPlayer);
	void SetPlayerPickedTimestamp(CInfClassPlayer *pPlayer, int Timestamp) const;

	int InfectHumans(int NumHumansToInfect);
	void ForcePlayersBalance(int PlayersToBalance);
	void UpdateBalanceFactors();

	void MaybeSendStatistics();
	void CancelTheRound(ROUND_CANCELATION_REASON Reason);
	void AnnounceTheWinner(int NumHumans);
	void BroadcastInfectionComing(int InfectionTick);

private:
	void UpdateNinjaTargets();

	void ReservePlayerOwnSnapItems();
	void FreePlayerOwnSnapItems();

	void SendHintMessage();
	void FormatHintMessage(const CHintMessage &Message, dynamic_string *pBuffer, const char *pLanguage) const;

	void OnInfectionTriggered();

	void StartInfectionGameplay(int PlayersToInfect);

	void MaybeSuggestMoreRounds();
	void SnapMapMenu(int SnappingClient, CNetObj_GameInfo *pGameInfoObj);
	void FallInLoveIfInfectedEarly(CInfClassCharacter *pCharacter);
	void RewardTheKillers(CInfClassCharacter *pVictim, const DeathContext &Context);
	void GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected);
	int GetMinimumInfectedForPlayers(int PlayersNumber) const;

	int GetClientIdForNewWitch() const;
	bool IsSafeWitchCandidate(int ClientID) const;
	ClientsArray m_WitchCallers;

	struct PlayerScore
	{
		char aPlayerName[MAX_NAME_LENGTH];
		int Kills;
		int ClientID;
	};

	struct
	{
		int Wave = 0;
		int Kills = 0;
		icArray<PlayerScore, MAX_CLIENTS> Scores;
		icArray<int, MAX_CLIENTS> SurvivedPlayers;
		icArray<int, MAX_CLIENTS> KilledPlayers;
	} m_SurvivalState;
	int m_BestSurvivalScore = 0;

	bool m_AllowSurvivalAutostart = false;

	PlayerScore *GetSurvivalPlayerScore(int ClientID);
	PlayerScore *EnsureSurvivalPlayerScore(int ClientID);

private:
	int m_ZoneHandle_icDamage;
	int m_ZoneHandle_icTeleport;
	int m_ZoneHandle_icBonus;

	int m_MapWidth;
	int m_MapHeight;
	int* m_GrowingMap;
	bool m_ExplosionStarted;

	CGameTeams m_Teams;

	int m_InfUnbalancedTick;
	float m_InfBalanceBoostFactor = 0;
	array<vec2> m_HeroFlagPositions;
	int m_HeroGiftTick = 0;

	ClientsArray m_NinjaTargets;
	int m_PlayerOwnCursorID = -1;

	ERoundType m_RoundType = ERoundType::Normal;
	ERoundType m_QueuedRoundType = ERoundType::Normal;

	FunRoundConfiguration m_FunRoundConfiguration;
	std::vector<FunRoundConfiguration> m_FunRoundConfigurations;
	int m_FunRoundsPassed = 0;

	bool m_InfectedStarted;
	bool m_RoundStarted = false;
	bool m_SuggestMoreRounds = false;
	bool m_MoreRoundsSuggested = false;

	static int64_t m_LastTipTime;
};

#endif
