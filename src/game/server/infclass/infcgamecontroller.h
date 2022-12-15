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
class CInfClassCharacter;
class CInfClassPlayer;
struct CNetObj_GameInfo;
struct SpawnContext;
struct DeathContext;
struct ZoneData;

enum class TAKEDAMAGEMODE;
enum class DAMAGE_TYPE;
enum class ROUND_CANCELATION_REASON;
enum class ROUND_END_REASON;

using ClientsArray = icArray<int, MAX_CLIENTS>;

enum class ROUND_TYPE
{
	NORMAL,
	FUN,
	FAST,
	COUNT,
	INVALID = COUNT,
};

enum class CLASS_AVAILABILITY
{
	AVAILABLE,
	DISABLED,
	NEED_MORE_PLAYERS,
	LIMIT_EXCEEDED,
};

static const char *toString(ROUND_TYPE RoundType);

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

struct SSnapContext
{
	int SnappingClient = 0;
	int Version = 0;
	int InfclassVersion = 0;
};

class CInfClassGameController : public IGameController
{
public:
	CInfClassGameController(class CGameContext *pGameServer);
	~CInfClassGameController() override;

	void IncreaseCurrentRoundCounter() override;

	void Tick() override;
	void Snap(int SnappingClient) override;

	CPlayer *CreatePlayer(int ClientID, bool IsSpectator, void *pData) override;
	int PersistentClientDataSize() const override;
	bool GetClientPersistentData(int ClientID, void *pData) const override;

	bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv) override;
	void HandleCharacterTiles(CInfClassCharacter *pCharacter);
	void HandleLastHookers();

	bool CanSeeDetails(int Who, int Whom) const;
	int64_t GetBlindCharactersMask(int ExcludeCID) const;

	bool HumanWallAllowedInPos(const vec2 &Pos) const;
	int GetZoneValueAt(int ZoneHandle, const vec2 &Pos, ZoneData *pData = nullptr) const;
	int GetDamageZoneValueAt(const vec2 &Pos, ZoneData *pData = nullptr) const;
	int GetBonusZoneValueAt(const vec2 &Pos, ZoneData *pData = nullptr) const;

	void CreateExplosion(const vec2 &Pos, int Owner, DAMAGE_TYPE DamageType, float DamageFactor = 1.0f);
	void CreateExplosionDisk(vec2 Pos, float InnerRadius, float DamageRadius, int Damage, float Force, int Owner, DAMAGE_TYPE DamageType);
	void CreateExplosionDiskGfx(vec2 Pos, float InnerRadius, float DamageRadius, int Owner);

	void SendHammerDot(const vec2 &Pos, int SnapID);

	bool SnapLaserObject(const SSnapContext &Context, int SnapID, const vec2 &To, const vec2 &From, int StartTick, int Owner, int Type = -1);

	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	void OnCharacterDeath(CInfClassCharacter *pVictim, const DeathContext &Context);
	void OnCharacterSpawned(CInfClassCharacter *pCharacter);
	void OnClassChooserRequested(CInfClassCharacter *pCharacter);
	void CheckRoundFailed();
	void DoWincheck() override;
	void StartRound() override;
	void EndRound() override;
	void EndRound(ROUND_END_REASON Reason);
	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true) override;
	bool TryRespawn(CInfClassPlayer *pPlayer, SpawnContext *pContext);
	PLAYERCLASS ChooseHumanClass(const CInfClassPlayer *pPlayer) const;
	PLAYERCLASS ChooseInfectedClass(const CInfClassPlayer *pPlayer) const;
	bool GetPlayerClassEnabled(PLAYERCLASS PlayerClass) const;
	int GetMinPlayersForClass(PLAYERCLASS PlayerClass) const;
	int GetClassPlayerLimit(PLAYERCLASS PlayerClass) const;
	int GetPlayerClassProbability(PLAYERCLASS PlayerClass) const;

	int GetInfectedCount(PLAYERCLASS InfectedPlayerClass = PLAYERCLASS_INVALID) const;
	int GetMinPlayers() const;

	ROUND_TYPE GetRoundType() const;
	void QueueRoundType(ROUND_TYPE RoundType);

	CLASS_AVAILABILITY GetPlayerClassAvailability(PLAYERCLASS PlayerClass, const CInfClassPlayer *pForPlayer = nullptr) const;
	bool CanVote() override;

	void OnPlayerConnect(CPlayer *pPlayer) override;
	void OnPlayerDisconnect(CPlayer *pBasePlayer, int Type, const char *pReason) override;

	void OnReset() override;

	void DoPlayerInfection(CInfClassPlayer *pPlayer, CInfClassPlayer *pInfectiousPlayer, PLAYERCLASS PreviousClass);

	void OnHeroFlagCollected(int ClientID);
	float GetHeroFlagCooldown() const;

	bool IsInfectionStarted() const;
	bool CanJoinTeam(int Team, int ClientID) override;
	bool AreTurretsEnabled() const;
	bool MercBombsEnabled() const;
	bool WhiteHoleEnabled() const;
	bool IsClassChooserEnabled() const;

	float GetTimeLimit() const;
	float GetInfectionDelay() const;

	bool IsSpawnable(vec2 Pos, int TeleZoneIndex) override;

	const ClientsArray &GetValidNinjaTargets() const { return m_NinjaTargets; }

	bool HeroGiftAvailable() const;
	bool GetHeroFlagPosition(vec2 *pFlagPosition) const;
	bool IsPositionAvailableForHumans(const vec2 &FlagPosition) const;

	void StartFunRound(const FunRoundConfiguration &Configuration);
	void EndFunRound();
	void ResetFinalExplosion();
	void SaveRoundRules();

	int GetRoundTick() const;

	static bool IsDefenderClass(PLAYERCLASS PlayerClass);
	static bool IsSupportClass(PLAYERCLASS PlayerClass);
	static PLAYERCLASS GetClassByName(const char *pClassName, bool *pOk = nullptr);
	static const char *GetClassName(PLAYERCLASS PlayerClass);
	static const char *GetClassPluralName(PLAYERCLASS PlayerClass);
	static const char *GetClassDisplayName(PLAYERCLASS PlayerClass, const char *pDefaultText = nullptr);
	static const char *GetClanForClass(PLAYERCLASS PlayerClass, const char *pDefaultText = nullptr);
	static const char *GetClassPluralDisplayName(PLAYERCLASS PlayerClass);
	static PLAYERCLASS MenuClassToPlayerClass(int MenuClass);
	static int DamageTypeToWeapon(DAMAGE_TYPE DamageType, TAKEDAMAGEMODE *pMode = nullptr);

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

	static void ConQueueSpecialRound(IConsole::IResult *pResult, void *pUserData);
	static void ConStartFunRound(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueFunRound(IConsole::IResult *pResult, void *pUserData);
	static void ConStartSpecialFunRound(IConsole::IResult *pResult, void *pUserData);
	static void ConClearFunRounds(IConsole::IResult *pResult, void *pUserData);
	static void ConAddFunRound(IConsole::IResult *pResult, void *pUserData);

	static void ConStartFastRound(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueFastRound(IConsole::IResult *pResult, void *pUserData);
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
	int InfectHumans(int NumHumansToInfect);
	void UpdateBalanceFactors(int NumHumans, int NumInfected);

	void MaybeSendStatistics();
	void CancelTheRound(ROUND_CANCELATION_REASON Reason);
	void AnnounceTheWinner(int NumHumans);
	void BroadcastInfectionComing(int InfectionTick);

private:
	void UpdateNinjaTargets();

	void ReservePlayerOwnSnapItems();
	void FreePlayerOwnSnapItems();

	void OnInfectionTriggered();
	void MaybeSuggestMoreRounds();
	void SnapMapMenu(int SnappingClient, CNetObj_GameInfo *pGameInfoObj);
	void FallInLoveIfInfectedEarly(CInfClassCharacter *pCharacter);
	void RewardTheKillers(CInfClassCharacter *pVictim, const DeathContext &Context);
	void GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected);
	int GetMinimumInfectedForPlayers(int PlayersNumber) const;

	void SetAvailabilities(std::vector<int> value);
	void SetProbabilities(std::vector<int> value);

	int RandomZombieToWitch();
	bool IsSafeWitchCandidate(int ClientID) const;
	ClientsArray m_WitchCallers;

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
	int m_TargetToKill;
	int m_TargetToKillCoolDown;

	int m_PlayerOwnCursorID = -1;

	ROUND_TYPE m_RoundType = ROUND_TYPE::NORMAL;
	ROUND_TYPE m_QueuedRoundType = ROUND_TYPE::NORMAL;

	std::vector<FunRoundConfiguration> m_FunRoundConfigurations;
	int m_FunRoundsPassed = 0;

	std::vector<int> m_DefaultAvailabilities;
	std::vector<int> m_DefaultProbabilities;

	bool m_InfectedStarted;
	bool m_RoundStarted = false;
	bool m_SuggestMoreRounds = false;
	bool m_MoreRoundsSuggested = false;
};

#endif
