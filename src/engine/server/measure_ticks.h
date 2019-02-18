/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
/* Modifications Copyright 2019 The InfclassR (https://github.com/yavl/teeworlds-infclassR/) Authors */
#ifndef ENGINE_SERVER_MEASURE_TICKS_H
#define ENGINE_SERVER_MEASURE_TICKS_H

#include <chrono>

class CMeasureTicks
{
	
public:
	CMeasureTicks(int LogIntervalInSeconds, const char *pLogTag);
	void Begin();
	void End();

private:
	std::chrono::time_point<std::chrono::high_resolution_clock> m_LastReportTime;
	std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTime;
	double m_MillisBuff;
	double m_LongestTickMillis;
	int m_Ticks;
	char m_pLogTag[64];
	int m_LogIntervalInSeconds;
};

#endif
