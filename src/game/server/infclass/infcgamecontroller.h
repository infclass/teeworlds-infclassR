/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INFCLASS_GAMECONTROLLER_H
#define GAME_SERVER_INFCLASS_GAMECONTROLLER_H

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

using ClientsArray = icArray<int, 64>; // MAX_CLIENTS

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

class CInfClassGameController : public IGameController
{
public:
	CInfClassGameController(class CGameContext *pGameServer);
	~CInfClassGameController() override;

	void IncreaseCurrentRoundCounter() override;

	void Tick() override;
	void Snap(int SnappingClient) override;

	CPlayer *CreatePlayer(int ClientID, bool IsSpectator) override;

	bool OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv) override;
	void HandleCharacterTiles(CInfClassCharacter *pCharacter);
	void HandleLastHookers();

	int64_t GetBlindCharactersMask(int ExcludeCID) const;

	bool HumanWallAllowedInPos(const vec2 &Pos) const;
	int GetZoneValueAt(int ZoneHandle, const vec2 &Pos, ZoneData *pData = nullptr) const;
	int GetDamageZoneValueAt(const vec2 &Pos, ZoneData *pData = nullptr) const;
	int GetBonusZoneValueAt(const vec2 &Pos, ZoneData *pData = nullptr) const;

	void CreateExplosion(const vec2 &Pos, int Owner, DAMAGE_TYPE DamageType, float DamageFactor = 1.0f);
	void CreateExplosionDisk(vec2 Pos, float InnerRadius, float DamageRadius, int Damage, float Force, int Owner, DAMAGE_TYPE DamageType);
	void CreateExplosionDiskGfx(vec2 Pos, float InnerRadius, float DamageRadius, int Owner);

	void SendHammerDot(const vec2 &Pos, int SnapID);

	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon) override;
	void OnCharacterDeath(CInfClassCharacter *pVictim, const DeathContext &Context);
	void OnCharacterSpawned(CInfClassCharacter *pCharacter);
	void DoWincheck() override;
	void StartRound() override;
	void EndRound() override;
	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true) override;
	bool TryRespawn(CInfClassPlayer *pPlayer, SpawnContext *pContext);
	int ChooseHumanClass(const CPlayer *pPlayer) const;
	int ChooseInfectedClass(const CInfClassPlayer *pPlayer) const;
	bool GetPlayerClassEnabled(int PlayerClass) const;
	int GetMinPlayersForClass(int PlayerClass) const;
	int GetClassPlayerLimit(int PlayerClass) const;
	int GetPlayerClassProbability(int PlayerClass) const;

	ROUND_TYPE GetRoundType() const;

	CLASS_AVAILABILITY GetPlayerClassAvailability(int PlayerClass) const;
	bool CanVote() override;
	void OnPlayerDisconnect(CPlayer *pPlayer, int Type, const char *pReason) override;
	void DoPlayerInfection(CInfClassPlayer *pPlayer, CInfClassPlayer *pInfectiousPlayer, int PreviousClass);

	void OnHeroFlagCollected(int ClientID);

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

	void RegisterChatCommands(class IConsole *pConsole) override;

	static void ConSetClientName(IConsole::IResult *pResult, void *pUserData);
	static void ConRestoreClientName(IConsole::IResult *pResult, void *pUserData);
	static void ConLockClientName(IConsole::IResult *pResult, void *pUserData);
	static void ConUserSetClass(IConsole::IResult *pResult, void *pUserData);
	void ConUserSetClass(IConsole::IResult *pResult);
	static void ConSetClass(IConsole::IResult *pResult, void *pUserData);
	void ConSetClass(IConsole::IResult *pResult);

	static void ConQueueSpecialRound(IConsole::IResult *pResult, void *pUserData);
	static void ConStartFastRound(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueFastRound(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueFunRound(IConsole::IResult *pResult, void *pUserData);
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
	void UpdateNinjaTargets();

	void ReservePlayerOwnSnapItems();
	void FreePlayerOwnSnapItems();

	void MaybeSuggestMoreRounds();
	void SnapMapMenu(int SnappingClient, CNetObj_GameInfo *pGameInfoObj);
	void FallInLoveIfInfectedEarly(CInfClassCharacter *pCharacter);
	void RewardTheKiller(CInfClassCharacter *pVictim, CInfClassPlayer *pKiller, int Weapon);
	void GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected);
	int GetMinimumInfectedForPlayers(int PlayersNumber) const;

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

	array<vec2> m_HeroFlagPositions;
	int m_HeroGiftTick = 0;

	ClientsArray m_NinjaTargets;
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
