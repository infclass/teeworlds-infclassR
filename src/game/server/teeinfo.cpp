#include <base/system.h>

#include "teeinfo.h"

CTeeInfo::CTeeInfo(const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet)
{
	SetSkinName(pSkinName);
	m_UseCustomColor = UseCustomColor;
	m_ColorBody = ColorBody;
	m_ColorFeet = ColorFeet;
}

const char *CTeeInfo::SkinName() const
{
	return m_SkinName;
}

void CTeeInfo::SetSkinName(const char *pSkinName)
{
	str_copy(m_SkinName, pSkinName, sizeof(m_SkinName));
}
