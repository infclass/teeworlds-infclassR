#ifndef GAME_SERVER_INFCLASS_PLAYER_H
#define GAME_SERVER_INFCLASS_PLAYER_H

#include <game/gamecore.h>
#include <base/tl/ic_array.h>

class CGameContext;
class CInfClassCharacter;
class CInfClassGameController;
class CInfClassPlayerClass;
struct SpawnContext;

// We actually have to include player.h after all this stuff above.
#include <game/server/player.h>
#include <game/server/skininfo.h>

enum class INFECTION_TYPE
{
	NO,
	REGULAR,
	RESTORE_INF_CLASS,
};

enum class INFECTION_CAUSE
{
	GAME,
	PLAYER,
};

enum class EPlayerScoreMode
{
	Class,
	Time,
	Clan,

	Count,
};

class CInfClassPlayer : public CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassPlayer(CInfClassGameController *pGameController, int UniqueClientId, int ClientId, int Team);
	~CInfClassPlayer() override;

	static CInfClassPlayer *GetInstance(CPlayer *pPlayer);

	CInfClassGameController *GameController() const;

	void TryRespawn() override;

	int GetScore(int SnappingClient) const override;

	void Tick() override;
	void PostTick() override;
	void Snap(int SnappingClient) override;
	void SnapClientInfo(int SnappingClient, int SnappingClientMappedId) override;
	int GetDefaultEmote() const override;
	CWeakSkinInfo GetSkinInfo(int SnappingClient) const;

	bool GetAntiPingEnabled() const;
	void SetAntiPingEnabled(bool Enabled);

	void SetInfectionTimestamp(int Timestamp);
	int GetInfectionTimestamp() const;

	EPlayerClass GetPreferredClass() const { return m_PreferredClass; }
	void SetPreferredClass(EPlayerClass Class);
	void SetPreviouslyPickedClass(EPlayerClass Class);

	void HandleInfection();
	void KillCharacter(int Weapon = WEAPON_GAME) override;

	CInfClassCharacter *GetCharacter();
	const CInfClassCharacter *GetCharacter() const;
	CInfClassPlayerClass *GetCharacterClass() { return m_pInfcPlayerClass; }
	const CInfClassPlayerClass *GetCharacterClass() const { return m_pInfcPlayerClass; }
	void SetCharacterClass(CInfClassPlayerClass *pClass);

	void SetClass(EPlayerClass NewClass);
	void UpdateSkin();

	INFECTION_TYPE InfectionType() const { return m_InfectionType; }
	INFECTION_CAUSE InfectionCause() const { return m_InfectionCause; }
	void StartInfection(int InfectiousPlayerCid = -1, INFECTION_TYPE InfectionType = INFECTION_TYPE::REGULAR);
	bool IsInfectionStarted() const;

	int MapMenu() const { return (m_Team != TEAM_SPECTATORS) ? m_MapMenu : 0; }
	void OpenMapMenu(int Menu);
	void CloseMapMenu();
	bool MapMenuClickable();

	void SetHookProtection(bool Value, bool Automatic = true);
	bool HookProtectionEnabled() const { return m_HookProtection; }

	EPlayerScoreMode GetScoreMode() const;
	void SetScoreMode(EPlayerScoreMode Mode);

	int DieTick() const { return m_DieTick; }

	void ResetTheTargetToFollow();
	void SetFollowTarget(int ClientId, float Duration);
	int TargetToFollow() const;

	int GetSpectatingCid() const;

	float GetGhoulPercent() const;
	void IncreaseGhoulLevel(int Diff);
	int GetGhoulLevel() const { return m_GhoulLevel; }

	void SetRandomClassChoosen();
	bool RandomClassChoosen() const;

	EPlayerClass GetPreviousInfectedClass() const;
	EPlayerClass GetPreviousHumanClass() const;
	EPlayerClass GetPreviouslyPickedClass() const;

	void AddSavedPosition(const vec2 Position);
	bool LoadSavedPosition(vec2 *pOutput) const;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const { return CPlayer::Server(); }

	void ResetRoundData();
	int GetInfectionTick() const { return m_InfectionTick; }

	void OnKill();
	void OnDeath();
	void OnAssist();

	int GetMaxHP() const { return m_MaxHP; }
	void SetMaxHP(int MaxHP);
	void ApplyMaxHP();

	int GetKills() const { return m_Kills; }

	virtual void OnCharacterHPChanged(){};

public:
	int m_MapMenuItem = -1;

protected:
	virtual void OnCharacterSpawned(const SpawnContext &Context);
	const char *GetClan(int SnappingClient = -1) const override;
	void HandleAutoRespawn() override;
	void UpdateSpectatorPos();

	bool IsForcedToSpectate() const;

	void SendClassIntro();

	CSkinContext m_SameTeamSkinContext;
	CSkinContext m_DiffTeamSkinContext;
	SkinGetter m_SkinGetter;

	CInfClassGameController *m_pGameController = nullptr;
	CInfClassPlayerClass *m_pInfcPlayerClass = nullptr;
	int m_HumanTime{};
	int m_InfectionTick;
	bool m_HookProtection{};
	bool m_HookProtectionAutomatic{};

	bool m_aKnownClasses[NB_PLAYERCLASS]{};

	EPlayerClass m_PreferredClass;
	bool m_AntiPing = false;

	int m_MaxHP = 0;

	int m_Kills = 0;
	int m_Deaths = 0;
	int m_Assists = 0;
	int m_Score = 0;

	int m_RandomClassRoundId = 0;
	int m_GameInfectionTimestamp = 0;

	INFECTION_TYPE m_InfectionType = INFECTION_TYPE::NO;
	INFECTION_CAUSE m_InfectionCause = INFECTION_CAUSE::GAME;
	int m_InfectiousPlayerCid = -1;

	int m_SelfKillAttemptTick = -1;

	int m_FollowTargetId = -1;
	int m_FollowTargetTicks = 0;

	int m_MapMenu = 0;
	int m_MapMenuTick = -1;

	EPlayerScoreMode m_ScoreMode = EPlayerScoreMode::Class;

	int m_GhoulLevel = 0;
	int m_GhoulLevelTick = 0;

	EPlayerClass m_PickedClass = EPlayerClass::Invalid;
	icArray<EPlayerClass, 5> m_PreviousClasses;
	icArray<vec2, 1> m_SavedPositions;
};

inline CInfClassPlayer *CInfClassPlayer::GetInstance(CPlayer *pPlayer)
{
	return static_cast<CInfClassPlayer *>(pPlayer);
}

enum
{
	PLAYERITER_ALL = 0x0,

	PLAYERITER_COND_READY = 0x1,
	PLAYERITER_COND_SPEC = 0x2,
	PLAYERITER_COND_NOSPEC = 0x4,

	PLAYERITER_INGAME = PLAYERITER_COND_READY | PLAYERITER_COND_NOSPEC,
	PLAYERITER_SPECTATORS = PLAYERITER_COND_READY | PLAYERITER_COND_SPEC,
};

template<int FLAGS>
class CInfClassPlayerIterator
{
public:
	CInfClassPlayerIterator(CPlayer **ppPlayers) :
		m_ppPlayers(ppPlayers)
	{
		Reset();
	}

	bool Next()
	{
		for(m_ClientId = m_ClientId + 1; m_ClientId < MAX_CLIENTS; m_ClientId++)
		{
			CPlayer *pPlayer = Player();

			if(!pPlayer)
				continue;
			if((FLAGS & PLAYERITER_COND_READY) && (!pPlayer->m_IsInGame || !pPlayer->m_IsReady))
				continue;
			if((FLAGS & PLAYERITER_COND_NOSPEC) && (pPlayer->GetTeam() == TEAM_SPECTATORS))
				continue;
			if((FLAGS & PLAYERITER_COND_SPEC) && (pPlayer->GetTeam() != TEAM_SPECTATORS))
				continue;

			return true;
		}

		return false;
	}

	void Reset() { m_ClientId = -1; }
	CInfClassPlayer *Player() { return static_cast<CInfClassPlayer *>(m_ppPlayers[m_ClientId]); }
	int ClientId() { return m_ClientId; }

private:
	CPlayer **m_ppPlayers;
	int m_ClientId;
};

#endif // GAME_SERVER_INFCLASS_PLAYER_H
