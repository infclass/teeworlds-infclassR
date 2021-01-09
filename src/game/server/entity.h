/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITY_H
#define GAME_SERVER_ENTITY_H

#include <new>
#include <base/vmath.h>
#include <game/server/gameworld.h>

#include "alloc.h"

/*
	Class: Entity
		Basic entity class.
*/
class CEntity
{
	MACRO_ALLOC_HEAP()

	friend class CGameWorld;	// entity list handling
	CEntity *m_pPrevTypeEntity;
	CEntity *m_pNextTypeEntity;

	class CGameWorld *m_pGameWorld;
protected:
	bool m_MarkedForDestroy;
	int m_ID;
	int m_ObjType;
public:
	/* Constructor */
	CEntity(CGameWorld *pGameWorld, int Objtype, const vec2 &Pos = vec2(0,0), int ProximityRadius = 0);

	/* Destructor */
	virtual ~CEntity();
	
	/* Objects */
	class CGameWorld *GameWorld() { return m_pGameWorld; }
	class CConfig *Config() { return m_pGameWorld->Config(); }
	class CGameContext *GameServer() { return GameWorld()->GameServer(); }
	class IServer *Server() { return GameWorld()->Server(); }
	
	/* Getters */
	CEntity *TypeNext() { return m_pNextTypeEntity; }
	CEntity *TypePrev() { return m_pPrevTypeEntity; }
	const vec2 &GetPos() const { return m_Pos; }
	float GetProximityRadius() const { return m_ProximityRadius; }
	bool IsMarkedForDestroy() const { return m_MarkedForDestroy; }

	/* Setters */
	void MarkForDestroy() { m_MarkedForDestroy = true; }

	/* Other functions */

	/*
		Function: Destroy
			Destorys the entity.
	*/
	virtual void Destroy() { delete this; }

	/*
		Function: Reset
			Called when the game resets the map. Puts the entity
			back to it's starting state or perhaps destroys it.
	*/
	virtual void Reset() {}

	/*
		Function: Tick
			Called progress the entity to the next tick. Updates
			and moves the entity to it's new state and position.
	*/
	virtual void Tick() {}

	/*
		Function: TickDefered
			Called after all entities Tick() function has been called.
	*/
	virtual void TickDefered() {}

	/*
		Function: TickPaused
			Called when the game is paused, to freeze the state and position of the entity.
	*/
	virtual void TickPaused() {}

	/*
		Function: Snap
			Called when a new snapshot is being generated for a specific
			client.

		Arguments:
			SnappingClient - ID of the client which snapshot is
				being generated. Could be -1 to create a complete
				snapshot of everything in the game for demo
				recording.
	*/
	virtual void Snap(int SnappingClient) {}

	/*
		Function: NetworkClipped(int SnappingClient)
			Performs a series of test to see if a client can see the
			entity.

		Arguments:
			SnappingClient - ID of the client which snapshot is
				being generated. Could be -1 to create a complete
				snapshot of everything in the game for demo
				recording.

		Returns:
			Non-zero if the entity doesn't have to be in the snapshot.
	*/
	int NetworkClipped(int SnappingClient);
	int NetworkClipped(int SnappingClient, vec2 CheckPos);

	bool GameLayerClipped(vec2 CheckPos);

	/*
		Variable: proximity_radius
			Contains the physical size of the entity.
	*/
	float m_ProximityRadius;

	/*
		Variable: pos
			Contains the current posititon of the entity.
	*/
	vec2 m_Pos;
};

class CAnimatedEntity : public CEntity
{
protected:
	vec2 m_Pivot;
	vec2 m_RelPosition;
	int m_PosEnv;

protected:
	virtual void Tick();
	
public:
	CAnimatedEntity(CGameWorld *pGameWorld, int Objtype, vec2 Pivot);
	CAnimatedEntity(CGameWorld *pGameWorld, int Objtype, vec2 Pivot, vec2 RelPosition, int PosEnv);
};

#endif
