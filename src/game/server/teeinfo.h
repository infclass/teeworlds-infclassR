#ifndef GAME_SERVER_TEEINFO_H
#define GAME_SERVER_TEEINFO_H

class CTeeInfo
{
public:
	char m_SkinName[64] = {'\0'};
/* INFECTION MODIFICATION START ***************************************/
	char m_CustomSkinName[64];
/* INFECTION MODIFICATION END *****************************************/
	int m_UseCustomColor = 0;
	int m_ColorBody = 0;
	int m_ColorFeet = 0;

	CTeeInfo() = default;

	CTeeInfo(const char *pSkinName, int UseCustomColor, int ColorBody, int ColorFeet);

	const char *SkinName() const;
	void SetSkinName(const char *pSkinName);
};
#endif //GAME_SERVER_TEEINFO_H
