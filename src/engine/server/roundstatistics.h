#ifndef ENGINE_SERVER_ROUND_STATISTICS_H
#define ENGINE_SERVER_ROUND_STATISTICS_H

#include <engine/console.h>
#include <engine/shared/protocol.h>

enum class EPlayerClass;

enum
{
	SCOREEVENT_HUMAN_SURVIVE=0,
	SCOREEVENT_HUMAN_SUICIDE,
	SCOREEVENT_HUMAN_HEALING,
	SCOREEVENT_INFECTION,
	SCOREEVENT_KILL_INFECTED,
	SCOREEVENT_KILL_WITCH,
	SCOREEVENT_KILL_UNDEAD,
	SCOREEVENT_KILL_TARGET,
	SCOREEVENT_DESTROY_TURRET,
	SCOREEVENT_HELP_FREEZE,
	SCOREEVENT_HELP_HOOK_BARRIER,
	SCOREEVENT_HELP_HOOK_INFECTION,
	SCOREEVENT_HERO_FLAG,
	SCOREEVENT_BONUS,
	SCOREEVENT_MEDIC_REVIVE,
};

class CRoundStatistics
{
public:
	class CPlayerStats
	{
	public:
		int m_Score{};
		int m_EngineerScore{};
		int m_SoldierScore{};
		int m_ScientistScore{};
		int m_BiologistScore{};
		int m_LooperScore{};
		int m_MedicScore{};
		int m_HeroScore{};
		int m_NinjaScore{};
		int m_MercenaryScore{};
		int m_SniperScore{};

		int m_SmokerScore{};
		int m_HunterScore{};
		int m_BatScore{};
		int m_BoomerScore{};
		int m_GhostScore{};
		int m_SpiderScore{};
		int m_GhoulScore{};
		int m_SlugScore{};
		int m_VoodooScore{};
		int m_UndeadScore{};
		int m_WitchScore{};

		bool m_WasSpectator{};
		bool m_Won{};

	public:
		CPlayerStats() = default;

		void Reset() { *this = CPlayerStats{}; }
		int OnScoreEvent(int EventType, EPlayerClass Class);
	};

public:
	CPlayerStats m_aPlayers[MAX_CLIENTS];
	int m_NumPlayersMin{};
	int m_NumPlayersMax{};
	int m_PlayedTicks{};
	
public:
	CRoundStatistics() = default;
	void Reset() { *this = CRoundStatistics{}; }
	void ResetPlayer(int ClientID);
	void OnScoreEvent(int ClientID, int EventType, EPlayerClass Class, const char* Name, IConsole* console);
	void SetPlayerAsWinner(int ClientID);
	
	CRoundStatistics::CPlayerStats* PlayerStatistics(int ClientID);
	int PlayerScore(int ClientID);
	
	int NumWinners() const;
	
	void UpdatePlayer(int ClientID, bool IsSpectator);
	void UpdateNumberOfPlayers(int Num);
	
	bool IsValidePlayer(int ClientID);
};

#endif
