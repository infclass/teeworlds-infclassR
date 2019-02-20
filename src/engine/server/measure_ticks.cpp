/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
/* Modifications Copyright 2019 The InfclassR (https://github.com/yavl/teeworlds-infclassR/) Authors */

#include <chrono>
#include "string.h"
#include <base/system.h>
#include "measure_ticks.h"


CMeasureTicks::CMeasureTicks(int LogIntervalInSeconds, const char *pLogTag)
{
	m_LogIntervalInSeconds = LogIntervalInSeconds;
	m_LastReportTime = std::chrono::high_resolution_clock::now();
	m_Ticks = 0;
	m_MillisBuff = 0;
	m_LongestTickMillis = 0;
	str_copy(m_pLogTag, pLogTag, strlen(pLogTag)+1);
}

void CMeasureTicks::Begin()
{
	m_StartTime = std::chrono::high_resolution_clock::now();
}

void CMeasureTicks::End()
{
	m_Ticks++;
	auto EndTime = std::chrono::high_resolution_clock::now();
	double DiffMillis = std::chrono::duration_cast<std::chrono::nanoseconds>(EndTime-m_StartTime).count()*0.000001;
	m_MillisBuff += DiffMillis;
	if (DiffMillis > m_LongestTickMillis)
		m_LongestTickMillis = DiffMillis;
	if (std::chrono::duration_cast<std::chrono::seconds>(EndTime-m_LastReportTime).count() >= m_LogIntervalInSeconds)
	{
		dbg_msg(m_pLogTag, "%ifps, AverageTickTime: %fms, LongestTick: %fms", m_Ticks/m_LogIntervalInSeconds, m_MillisBuff/m_Ticks, m_LongestTickMillis);
		m_Ticks = 0;
		m_MillisBuff = 0;
		m_LongestTickMillis = 0;
		m_LastReportTime = std::chrono::high_resolution_clock::now();
	}
}
