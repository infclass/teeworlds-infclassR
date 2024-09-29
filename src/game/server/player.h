/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

#include <game/gamecore.h>
#include <game/infclass/classes.h>
#include <game/server/alloc.h>
#include <game/server/teeinfo.h>

class CCharacter;
class CGameContext;
class IServer;
struct CNetObj_PlayerInput;

enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, int ClientId, int Team);
	virtual ~CPlayer();

	void Reset();

	virtual void TryRespawn();
	void Respawn();
	void SetTeam(int Team, bool DoChatMsg=true);
	int GetTeam() const { return m_Team; };
	int GetCid() const { return m_ClientId; };
	int GetClientVersion() const;
	virtual bool IsBot() const { return false; }

	virtual int GetScore(int SnappingClient) const;

	virtual void Tick();
	virtual void PostTick();
	virtual void Snap(int SnappingClient);
	virtual void SnapClientInfo(int SnappingClient, int SnappingClientMappedId);

	void OnDirectInput(const CNetObj_PlayerInput *pNewInput);
	void OnPredictedInput(const CNetObj_PlayerInput *pNewInput);
	void OnPredictedEarlyInput(const CNetObj_PlayerInput *pNewInput);
	void OnDisconnect();

	virtual void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aCurLatency[MAX_CLIENTS];

	// used for spectator mode
	int m_SpectatorId;

	bool m_IsReady;
	bool m_IsInGame;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;
	int m_aLastCommands[4];
	int m_LastCommandPos;
	int m_LastWhisperTo;

	int m_SendVoteIndex;

	CTeeInfo m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_ScoreStartTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	int m_LastActionMoveTick;
	int m_TeamChangeTick;
	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

protected:
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	virtual const char *GetName(int SnappingClient = -1) const;
	virtual const char *GetClan(int SnappingClient = -1) const;

	//
	bool m_Spawning;
	int m_ClientId;
	int m_Team;

	int m_Paused;
	int64_t m_ForcePauseTime;
	int64_t m_LastPause;
	bool m_Afk;

	int m_DefEmote;
	int m_OverrideEmote;
	int m_OverrideEmoteReset;

public:
	enum
	{
		PAUSE_NONE = 0,
		PAUSE_PAUSED,
		PAUSE_SPEC
	};

	bool m_DND;
	int64_t m_FirstVoteTick;
	char m_aTimeoutCode[64];

	void ProcessPause();
	int Pause(int State, bool Force);
	int ForcePause(int Time);
	int IsPaused();

	bool IsPlaying();
	bool IsInGame() const;

	int m_ShowOthers;
	bool m_ShowAll;
	vec2 m_ShowDistance;
	bool m_SpecTeam;

	void UpdatePlaytime();
	void SetAfk(bool Afk);
	bool IsAfk() const { return m_Afk; }

	int64_t m_LastPlaytime;
	int64_t m_LastEyeEmote;

	virtual int GetDefaultEmote() const;
	void OverrideDefaultEmote(int Emote, int Tick);
	bool CanOverrideDefaultEmote() const;

/* INFECTION MODIFICATION START ***************************************/
protected:
	EPlayerClass m_class;
	int m_DefaultScoreMode;
	char m_aLanguage[16];

public:
	EPlayerClass GetClass() const;
	bool IsInfected() const;
	bool IsHuman() const;
	bool IsSpectator() const;

	const char *GetLanguage() const;
	void SetLanguage(const char* pLanguage);

	void SetOriginalName(const char *pName);
	const char *GetOriginalName() const { return m_aOriginalName; }

	bool m_ClientNameLocked;

	CTuningParams m_PrevTuningParams;
	CTuningParams m_NextTuningParams;
	
	void HandleTuningParams();

/* INFECTION MODIFICATION END *****************************************/

protected:
	virtual void HandleAutoRespawn();

	char m_aOriginalName[MAX_NAME_LENGTH];
};

#endif
