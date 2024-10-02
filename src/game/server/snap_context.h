#pragma once

struct CSnapContext
{
	CSnapContext(int Version, bool Sixup = false) :
		m_ClientVersion(Version), m_Sixup(Sixup)
	{
	}

	int GetClientVersion() const { return m_ClientVersion; }
	bool IsSixup() const { return m_Sixup; }

private:
	int m_ClientVersion;
	bool m_Sixup;
};
