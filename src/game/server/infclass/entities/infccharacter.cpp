#include "infccharacter.h"

#include <engine/shared/config.h>

#include <game/server/gamecontext.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassCharacter, MAX_CLIENTS)

CInfClassCharacter::CInfClassCharacter(CGameContext *pContext)
	: CCharacter(pContext->GameWorld(), pContext->Console())
	, m_pContext(pContext)
{
}
