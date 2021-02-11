#include "gamecontext.h"

bool CGameContext::ConChangeLog(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	int ClientID = pResult->GetClientID();

	// if we don't get the parameter, the list = 1
	int list = pResult->GetInteger(0); 
	if (!list)
		list = 1; 
	int lists = 1;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "list %d/%d", list, lists);

	switch(list)
	{
		case 1:
			pSelf->SendChatTarget(ClientID, "--------- Changelog ---------");
			pSelf->SendChatTarget(ClientID, "Fix Self killing/spectator gives you points. [σℓí♡]");
			pSelf->SendChatTarget(ClientID, "Add new weapon laser for mercenary's bomb [σℓí♡]");
			pSelf->SendChatTarget(ClientID, "Add converse (/c \"text\") for whisper-chat [σℓí♡]");
			pSelf->SendChatTarget(ClientID, "Add funround ghosts vs ninjas [breton]");
			pSelf->SendChatTarget(ClientID, "Add funround ghouls vs biologists [breton]");
			pSelf->SendChatTarget(ClientID, "Add changelog [σℓí♡]");
			pSelf->SendChatTarget(ClientID, "---------------------------------");
			break;	
		default:
			pSelf->SendChatTarget(ClientID, "This list does not exist.");
			break;
	}
	return true;
}
