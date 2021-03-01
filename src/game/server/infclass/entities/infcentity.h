/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_INFC_ENTITY_H
#define GAME_SERVER_ENTITIES_INFC_ENTITY_H

#include <game/server/entity.h>

class CGameContext;

class CInfCEntity : public CEntity
{
public:
	CInfCEntity(CGameContext *pGameContext, int ObjectType, vec2 Pos, int Owner,
	            int ProximityRadius=0);

	int GetOwner() const { return m_Owner; }

	void Reset() override;

	void SetPosition(const vec2 &Position);

protected:
	int m_Owner = 0;
};

#endif // GAME_SERVER_ENTITIES_INFC_ENTITY_H
