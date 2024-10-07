/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <base/tl/ic_array.h>

#include <engine/server.h>
#include <engine/storage.h>
#include <engine/console.h>
#include <engine/shared/memheap.h>

#include <game/collision.h>
#include <game/infclass/classes.h>
#include <game/layers.h>
#include <game/voting.h>

#include <teeuniverses/components/localization.h>

#include "eventhandler.h"
#include "gamecontroller.h"
#include "gameworld.h"

#include <fstream>
#include <string>

/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/

#define BROADCAST_DURATION_REALTIME (0)
#define BROADCAST_DURATION_GAMEANNOUNCE (Server()->TickSpeed()*2)

enum class EBroadcastPriority
{
	LOWEST,
	WEAPONSTATE,
	EFFECTSTATE,
	GAMEANNOUNCE,
	SERVERANNOUNCE,
	INTERFACE,
};

class CConfig;
class IEngine;

class CGameContext : public IGameServer
{
	IServer *m_pServer;
	CConfig *m_pConfig;
	IConsole *m_pConsole;
	IEngine *m_pEngine;
	IStorage *m_pStorage;
	CLayers m_Layers;
	CCollision m_Collision;
	CNetObjHandler m_NetObjHandler;
	CTuningParams m_Tuning;

	CUuid m_GameUuid;

	bool m_Resetting;

	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDump(IConsole::IResult *pResult, void *pUserData);
	static void ConPause(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeMap(IConsole::IResult *pResult, void *pUserData);
	static void ConSkipMap(IConsole::IResult *pResult, void *pUserData);
	static void ConQueueMap(IConsole::IResult *pResult, void *pUserData);
	static void ConAddMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeamAll(IConsole::IResult *pResult, void *pUserData);
	static void ConInsertVote(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConForceVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConAddMapVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSyncMapRotation(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	CGameContext(int Resetting);
	void Construct(int Resetting);
	void Destruct(int Resetting);
	static int MapScan(const char *pName, int IsDir, int DirType, void *pUserData);

public:
	struct CPersistentData
	{
		CUuid m_PrevGameUuid;
	};

	struct CPersistentClientData
	{
		bool m_IsSpectator;
		bool m_ClientNameLocked;
	};

public:
	IServer *Server() const { return m_pServer; }
	CConfig *Config() { return m_pConfig; }
	IStorage *Storage() const { return m_pStorage; }
	class IConsole *Console() { return m_pConsole; }
	CGameWorld *GameWorld() { return &m_World; }
	CCollision *Collision() { return &m_Collision; }
	CTuningParams *Tuning() { return &m_Tuning; }
	virtual class CLayers *Layers() { return &m_Layers; }

	CGameContext();
	~CGameContext();

	void Clear();

	CEventHandler m_Events;
	CPlayer *m_apPlayers[MAX_CLIENTS];
	// keep last input to always apply when none is sent
	CNetObj_PlayerInput m_aLastPlayerInput[MAX_CLIENTS];
	bool m_aPlayerHasInput[MAX_CLIENTS];

	// returns last input if available otherwise nulled PlayerInput object
	// ClientId has to be valid
	CNetObj_PlayerInput GetLastPlayerInput(int ClientId) const;

	IGameController *m_pController;
	CGameWorld m_World;

	// helper functions
	CPlayer *GetPlayer(int ClientId) const;
	class CCharacter *GetPlayerChar(int ClientId);

	// voting
	void AddVote(const char *pDescription, const char *pCommand);
	bool InsertVote(int Position, const char *pDescription, const char *pCommand);
	void RemoveVote(const char *pVote);
	void ClearVotes();
	void StartVote(const char *pDesc, const char *pCommand, const char *pReason);
	void EndVote();
	void SendVoteSet(int ClientId);
	void SendVoteStatus(int ClientId, int Total, int Yes, int No);
	void AbortVoteKickOnDisconnect(int ClientId);
	void RequestVotesUpdate();

	bool HasActiveVote() const;

	int m_VoteCreator;
	int64_t m_VoteCloseTime;
	bool m_VoteUpdate;
	int m_VotePos;
	char m_aVoteDescription[VOTE_DESC_LENGTH];
	char m_aVoteCommand[VOTE_CMD_LENGTH];
	char m_aVoteReason[VOTE_REASON_LENGTH];
	int m_NumVoteOptions;
	int m_VoteEnforce;

	void CreateAllEntities(bool Initial);

	enum
	{
		VOTE_ENFORCE_UNKNOWN=0,
		VOTE_ENFORCE_NO,
		VOTE_ENFORCE_YES,
	};
	CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	// helper functions
	void CreateDamageInd(vec2 Pos, float AngleMod, int Amount, int64_t Mask = -1);
	void CreateExplosion(vec2 Pos, int Owner, int Weapon, int64_t Mask = -1);
	void CreateHammerHit(vec2 Pos, int64_t Mask = -1);
	void CreatePlayerSpawn(vec2 Pos, int64_t Mask = -1);
	void CreateDeath(vec2 Pos, int Who, int64_t Mask = -1);
	void CreateSound(vec2 Pos, int Sound, int64_t Mask = -1);
	void CreateSoundGlobal(int Sound, int Target = -1);

	bool SnapLaserObject(const CSnapContext &Context, int SnapId, const vec2 &To, const vec2 &From, int StartTick, int Owner = -1, int LaserType = -1, int Subtype = -1, int SwitchNumber = -1);
	bool SnapPickup(const CSnapContext &Context, int SnapId, const vec2 &Pos, int Type, int SubType, int SwitchNumber = 0);

	enum
	{
		CHAT_ALL = -2,
		CHAT_SPEC = -1,
		CHAT_RED = 0,
		CHAT_BLUE = 1,
		CHAT_WHISPER_SEND = 2,
		CHAT_WHISPER_RECV = 3,
	};

	// network
	void CallVote(int ClientId, const char *aDesc, const char *aCmd, const char *pReason, const char *aChatmsg);
	void SendChatTarget(int To, const char *pText) override;
	void SendChat(int ClientId, int Team, const char *pText, int SpamProtectionClientId = -1);
	void SendEmoticon(int ClientId, int Emoticon);
	void SendWeaponPickup(int ClientId, int Weapon);
	void SendMotd(int ClientId);
	void SendKillMessage(int Killer, int Victim, int Weapon, int ModeSpecial);

	//
	void CheckPureTuning();
	void SendTuningParams(int ClientId);
	void SendTuningParams(int ClientId, const CTuningParams &params);

	struct CVoteOptionServer *GetVoteOption(int Index);
	void ProgressVoteOptions(int ClientId);

	// engine events
	void OnInit(const void *pPersistentData) override;
	void OnConsoleInit() override;
	void OnMapChange(char *pNewMapName, int MapNameSize) override;
	void OnShutdown(const void *pPersistentData) override;

	void OnTick() override;
	void OnPreSnap() override;
	void OnSnap(int ClientId) override;
	void OnPostSnap() override;

	void UpdatePlayerMaps();

	void *PreProcessMsg(int *pMsgId, CUnpacker *pUnpacker, int ClientId);
	void CensorMessage(char *pCensoredMessage, const char *pMessage, int Size);
	void OnMessage(int MsgId, CUnpacker *pUnpacker, int ClientId) override;
	void OnSayNetMessage(const CNetMsg_Cl_Say *pMsg, int ClientId, const CUnpacker *pUnpacker);
	void OnCallVoteNetMessage(const CNetMsg_Cl_CallVote *pMsg, int ClientId);
	void OnVoteNetMessage(const CNetMsg_Cl_Vote *pMsg, int ClientId);
	void OnSetTeamNetMessage(const CNetMsg_Cl_SetTeam *pMsg, int ClientId);
	void OnIsDDNetLegacyNetMessage(const CNetMsg_Cl_IsDDNetLegacy *pMsg, int ClientId, CUnpacker *pUnpacker);
	void OnSetSpectatorModeNetMessage(const CNetMsg_Cl_SetSpectatorMode *pMsg, int ClientId);
	void OnChangeInfoNetMessage(const CNetMsg_Cl_ChangeInfo *pMsg, int ClientId);
	void OnEmoticonNetMessage(const CNetMsg_Cl_Emoticon *pMsg, int ClientId);
	void OnKillNetMessage(const CNetMsg_Cl_Kill *pMsg, int ClientId);
	void OnStartInfoNetMessage(const CNetMsg_Cl_StartInfo *pMsg, int ClientId);

	bool OnClientDataPersist(int ClientId, void *pData) override;
	void OnClientConnected(int ClientId, void *pData) override;
	void OnClientEnter(int ClientId) override;
	void OnClientDrop(int ClientId, EClientDropType Type, const char *pReason) override;
	void OnClientPrepareInput(int ClientId, void *pInput) override;
	void OnClientDirectInput(int ClientId, void *pInput) override;
	void OnClientPredictedInput(int ClientId, void *pInput) override;
	void OnClientPredictedEarlyInput(int ClientId, void *pInput) override;

	void OnClientEngineJoin(int ClientId, bool Sixup) override;
	void OnClientEngineDrop(int ClientId, const char *pReason) override;

	bool IsClientReady(int ClientId) const override;
	bool IsClientPlayer(int ClientId) const override;
	int PersistentDataSize() const override { return sizeof(CPersistentData); }
	int PersistentClientDataSize() const override;

	CUuid GameUuid() const override;
	const char *GameType() const override;
	const char *Version() const override;
	const char *NetVersion() const override;

	// DDRace
	void OnPreTickTeehistorian() override;
	bool OnClientDDNetVersionKnown(int ClientId);
	void FillAntibot(CAntibotRoundData *pData) override;
	bool ProcessSpamProtection(int ClientId, bool RespectChatInitialDelay = true);
	// Describes the time when the first player joined the server.
	int64_t m_NonEmptySince;
	int64_t m_LastMapVote;
	int GetClientVersion(int ClientId) const;
	CClientMask ClientsMaskExcludeClientVersionAndHigher(int Version);
	bool PlayerExists(int ClientId) const override { return m_apPlayers[ClientId]; }
	// Returns true if someone is actively moderating.
	bool PlayerModerating() const;
	void ForceVote(int EnforcerId, bool Success);

	// Checks if player can vote and notify them about the reason
	bool RateLimitPlayerVote(int ClientId);
	bool RateLimitPlayerMapVote(int ClientId);

	void OnUpdatePlayerServerInfo(char *aBuf, int BufSize, int Id) override;

/* INFECTION MODIFICATION START ***************************************/
private:
	static void ConKillPlayer(IConsole::IResult *pResult, void *pUserData);

	static void ConCredits(IConsole::IResult *pResult, void *pUserData);
	static void ConInfo(IConsole::IResult *pResult, void *pUserData);
	static void ConHelp(IConsole::IResult *pResult, void *pUserData);
	static void ConRules(IConsole::IResult *pResult, void *pUserData);

	static void ConTimeout(IConsole::IResult *pResult, void *pUserData);
	static void ConMe(IConsole::IResult *pResult, void *pUserData);
	static void ConWhisper(IConsole::IResult *pResult, void *pUserData);
	static void ConConverse(IConsole::IResult *pResult, void *pUserData);
	static void ConShowOthers(IConsole::IResult *pResult, void *pUserData);
	static void ConShowAll(IConsole::IResult *pResult, void *pUserData);

	static void ConAbout(IConsole::IResult *pResult, void *pUserData);
	void ConAbout(IConsole::IResult *pResult);
	static void ConVersion(IConsole::IResult *pResult, void *pUserData);
	static void ConRegister(IConsole::IResult *pResult, void *pUserData);
	static void ConLogin(IConsole::IResult *pResult, void *pUserData);
	static void ConLogout(IConsole::IResult *pResult, void *pUserData);
#ifdef CONF_SQL
	static void ConSetEmail(IConsole::IResult *pResult, void *pUserData);
	static void ConTop10(IConsole::IResult *pResult, void *pUserData);
	static void ConChallenge(IConsole::IResult *pResult, void *pUserData);
	static void ConRank(IConsole::IResult *pResult, void *pUserData);
	static void ConGoal(IConsole::IResult *pResult, void *pUserData);
	static void ConStats(IConsole::IResult *pResult, void *pUserData);
#endif
	void ChatHelp(int ClientId, const char *pHelpPage);
	bool WriteClassHelpPage(dynamic_string *pOutput, const char *pLanguage, EPlayerClass PlayerClass);
	static void ConLanguage(IConsole::IResult *pResult, void *pUserData);
	static void ConCmdList(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeLog(IConsole::IResult *pResult, void *pUserData);
	void ConChangeLog(IConsole::IResult *pResult);
	static void ConReloadChangeLog(IConsole::IResult *pResult, void *pUserData);

	void PrivateMessage(const char* pStr, int ClientId, bool TeamChat);
	void Whisper(int ClientId, char *pStr);
	void WhisperId(int ClientId, int VictimId, const char *pMessage);
	void Converse(int ClientId, const char *pStr);
	void MutePlayer(const char* pStr, int ClientId);

	void InitGeolocation();

	enum OPTION_VOTE_TYPE
	{
		OTHER_OPTION_VOTE_TYPE = 0,
		SV_MAP = 1,
		CHANGE_MAP = 2,
		SKIP_MAP = 3,
		PLAY_MORE_VOTE_TYPE = 4,
		QUEUED_VOTE = 8,

		MAP_VOTE_BITS = SV_MAP | CHANGE_MAP | SKIP_MAP, // Yeah, this is just '3'
	};
	
	static OPTION_VOTE_TYPE GetOptionVoteType(const char *pVoteCommand);
	void GetMapNameFromCommand(char* pMapName, const char *pCommand);

public:
	virtual void SendBroadcast(int To, const char *pText, EBroadcastPriority Priority, int LifeSpan);
	virtual void SendBroadcast_Localization(int To, EBroadcastPriority Priority, int LifeSpan, const char* pText, ...);
	virtual void SendBroadcast_Localization_P(int To, EBroadcastPriority Priority, int LifeSpan, int Number, const char* pText, ...);
	virtual void ClearBroadcast(int To, EBroadcastPriority Priority);
	
	static const char *GetChatCategoryPrefix(int Category);
	virtual void SendChatTarget_Localization(int To, int Category, const char* pText, ...);
	virtual void SendChatTarget_Localization_P(int To, int Category, int Number, const char* pText, ...);
	
	virtual void SendMOTD(int To, const char* pParam);
	virtual void SendMOTD_Localization(int To, const char* pText, ...);
	
	void CreateLaserDotEvent(vec2 Pos0, vec2 Pos1, int LifeSpan);
	void CreateHammerDotEvent(vec2 Pos, int LifeSpan);
	void CreateLoveEvent(vec2 Pos);
	void SendHitSound(int ClientId);
	void SendScoreSound(int ClientId);
	void AddBroadcast(int ClientId, const char* pText, EBroadcastPriority Priority, int LifeSpan);
	void SetClientLanguage(int ClientId, const char *pLanguage);
	void InitChangelog();
	void ReloadChangelog();

	bool IsPaused() const;
	void SetPaused(bool Paused);

	bool MapExists(const char *pMapName) const;
	
private:
	int m_VoteLanguageTick[MAX_CLIENTS];
	char m_VoteLanguage[MAX_CLIENTS][16];
	int m_VoteBanClientId;
	static bool m_ClientMuted[MAX_CLIENTS][MAX_CLIENTS]; // m_ClientMuted[i][j]: i muted j
	static icArray<std::string, 256> m_aChangeLogEntries;
	static icArray<uint32_t, 16> m_aChangeLogPageIndices;
	
	class CBroadcastState
	{
	public:
		int m_NoChangeTick;
		char m_PrevMessage[1024];
		
		EBroadcastPriority m_Priority;
		char m_NextMessage[1024];
		
		int m_LifeSpanTick;
		EBroadcastPriority m_TimedPriority;
		char m_TimedMessage[1024];
	};

	static void ConList(IConsole::IResult *pResult, void *pUserData);

	
	CBroadcastState m_BroadcastStates[MAX_CLIENTS];
	
	struct LaserDotState
	{
		vec2 m_Pos0;
		vec2 m_Pos1;
		int m_LifeSpan;
		int m_SnapId;
	};
	array<LaserDotState> m_LaserDots;
	
	struct HammerDotState
	{
		vec2 m_Pos;
		int m_LifeSpan;
		int m_SnapId;
	};
	array<HammerDotState> m_HammerDots;
	
	struct LoveDotState
	{
		vec2 m_Pos;
		int m_LifeSpan;
		int m_SnapId;
	};
	array<LoveDotState> m_LoveDots;

	int m_aHitSoundState[MAX_CLIENTS]; // 1 for hit, 2 for kill (no sounds must be sent)

public:
	void OnSetAuthed(int ClientId, int Level) override;

public:
	bool IsVersionBanned(int Version);
};

inline int64_t CmaskAll() { return -1LL; }
inline int64_t CmaskOne(int ClientId) { return 1LL<<ClientId; }
inline int64_t CmaskAllExceptOne(int ClientId) { return CmaskAll()^CmaskOne(ClientId); }
inline bool CmaskIsSet(int64_t Mask, int ClientId) { return (Mask&CmaskOne(ClientId)) != 0; }
#endif
