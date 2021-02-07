#include <base/system.h>

#include "teeinfo.h"

CTeeInfo::CTeeInfo(const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet)
{
	str_copy(m_SkinName, pSkinName, sizeof(m_SkinName));
	m_UseCustomColor = UseCustomColor;
	m_ColorBody = ColorBody;
	m_ColorFeet = ColorFeet;
}
