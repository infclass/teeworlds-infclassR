#include "roundstatistics.h"

#include <base/system.h>

#include <game/infclass/classes.h>

int CRoundStatistics::CPlayerStats::OnScoreEvent(int EventType, EPlayerClass Class)
{
	int Points = 0;
	switch(EventType)
	{
		case SCOREEVENT_HUMAN_SURVIVE:
			Points = 50;
			break;
		case SCOREEVENT_HUMAN_SUICIDE:
			Points = -10;
			break;
		case SCOREEVENT_INFECTION:
			Points = 30;
			break;
		case SCOREEVENT_KILL_INFECTED:
			Points = 10;
			break;
		case SCOREEVENT_KILL_TARGET:
			Points = 20;
			break;
		case SCOREEVENT_KILL_WITCH:
			Points = 50;
			break;
		case SCOREEVENT_KILL_UNDEAD:
			Points = 50;
			break;
		case SCOREEVENT_DESTROY_TURRET:
			Points = 10;
			break;
		case SCOREEVENT_HELP_FREEZE:
			Points = 10;
			break;
		case SCOREEVENT_HELP_HOOK_BARRIER:
			Points = 10;
			break;
		case SCOREEVENT_HELP_HOOK_INFECTION:
			Points = 10;
			break;
		case SCOREEVENT_HUMAN_HEALING:
			Points = 10;
			break;
		case SCOREEVENT_HERO_FLAG:
			Points = 10;
			break;
		case SCOREEVENT_BONUS:
			Points = 50;
			break;
		case SCOREEVENT_MEDIC_REVIVE:
			Points = 50;
			break;
	}

	m_Score += Points;
	
	switch(Class)
	{
		case EPlayerClass::Engineer:
			m_EngineerScore += Points;
			break;
		case EPlayerClass::Soldier:
			m_SoldierScore += Points;
			break;
		case EPlayerClass::Scientist:
			m_ScientistScore += Points;
			break;
		case EPlayerClass::Biologist:
			m_BiologistScore += Points;
			break;
		case EPlayerClass::Looper:
			m_LooperScore += Points;
			break;
		case EPlayerClass::Medic:
			m_MedicScore += Points;
			break;
		case EPlayerClass::Hero:
			m_HeroScore += Points;
			break;
		case EPlayerClass::Ninja:
			m_NinjaScore += Points;
			break;
		case EPlayerClass::Mercenary:
			m_MercenaryScore += Points;
			break;
		case EPlayerClass::Sniper:
			m_SniperScore += Points;
			break;
		case EPlayerClass::Smoker:
			m_SmokerScore += Points;
			break;
		case EPlayerClass::Hunter:
			m_HunterScore += Points;
			break;
		case EPlayerClass::Bat:
			m_BatScore += Points;
			break;
		case EPlayerClass::Boomer:
			m_BoomerScore += Points;
			break;
		case EPlayerClass::Ghost:
			m_GhostScore += Points;
			break;
		case EPlayerClass::Spider:
			m_SpiderScore += Points;
			break;
		case EPlayerClass::Ghoul:
			m_GhoulScore += Points;
			break;
		case EPlayerClass::Slug:
			m_SlugScore += Points;
			break;
		case EPlayerClass::Voodoo:
			m_VoodooScore += Points;
			break;
		case EPlayerClass::Undead:
			m_UndeadScore += Points;
			break;
		case EPlayerClass::Witch:
			m_WitchScore += Points;
			break;

		case EPlayerClass::Invalid:
		case EPlayerClass::None:
		case EPlayerClass::Count:
			break;
	}

	return Points;
}

void CRoundStatistics::ResetPlayer(int ClientID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_aPlayers[ClientID].Reset();
}

void CRoundStatistics::OnScoreEvent(int ClientID, int EventType, EPlayerClass Class, const char* Name, IConsole* console)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS) {
		int Score = m_aPlayers[ClientID].OnScoreEvent(EventType, Class);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "score player='%s' amount='%d'",
			Name,
			Score);
		console->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}
	
}

void CRoundStatistics::SetPlayerAsWinner(int ClientID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_aPlayers[ClientID].m_Won = true;
}

CRoundStatistics::CPlayerStats* CRoundStatistics::PlayerStatistics(int ClientID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		return &m_aPlayers[ClientID];
	else return 0;
}


int CRoundStatistics::PlayerScore(int ClientID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		return m_aPlayers[ClientID].m_Score/10;
	else return 0;
}
	
int CRoundStatistics::NumWinners() const
{
	int NumWinner = 0;
	for(int i=0; i<MAX_CLIENTS; i++)
	{
		if(m_aPlayers[i].m_Won)
			NumWinner++;
	}
	return NumWinner;
}

void CRoundStatistics::UpdatePlayer(int ClientID, bool IsSpectator)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_aPlayers[ClientID].m_WasSpectator = IsSpectator || m_aPlayers[ClientID].m_WasSpectator;
}
	
void CRoundStatistics::UpdateNumberOfPlayers(int Num)
{
	if(m_NumPlayersMin > Num)
		m_NumPlayersMin = Num;
	
	if(m_NumPlayersMax < Num)
		m_NumPlayersMax = Num;
	
	if(Num > 1)
		m_PlayedTicks++;
}
	
bool CRoundStatistics::IsValidePlayer(int ClientID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		return true;
	else
		return false;
}
