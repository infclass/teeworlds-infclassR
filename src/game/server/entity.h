/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITY_H
#define GAME_SERVER_ENTITY_H

#include <new>
#include <base/vmath.h>
#include <game/server/gameworld.h>

#include "alloc.h"

class CCollision;

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

	/* Identity */
	CGameWorld *m_pGameWorld;
	CCollision *m_pCCollision;
protected:
	bool m_MarkedForDestroy;
	int m_ID;
	int m_ObjType;
public:

	int GetID() const { return m_ID; }

	/* Constructor */
	CEntity(CGameWorld *pGameWorld, int Objtype, const vec2 &Pos = vec2(0,0), int ProximityRadius = 0);

	/* Destructor */
	virtual ~CEntity();
	
	/* Objects */
	class CGameWorld *GameWorld() { return m_pGameWorld; }
	class CConfig *Config() { return m_pGameWorld->Config(); }
	class CGameContext *GameServer() { return m_pGameWorld->GameServer(); }
	class IServer *Server() { return m_pGameWorld->Server(); }
	CCollision *Collision() { return m_pCCollision; }
	const CCollision *Collision() const { return m_pCCollision; }

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
			Destroys the entity.
	*/
	virtual void Destroy() { delete this; }

	/*
		Function: Reset
			Called when the game resets the map. Puts the entity
			back to its starting state or perhaps destroys it.
	*/
	virtual void Reset() {}

	/*
		Function: Tick
			Called to progress the entity to the next tick. Updates
			and moves the entity to its new state and position.
	*/
	virtual void Tick() {}

	/*
		Function: TickDeferred
			Called after all entities Tick() function has been called.
	*/
	virtual void TickDeferred() {}

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
		Function: NetworkClipped
			Performs a series of test to see if a client can see the
			entity.

		Arguments:
			SnappingClient - ID of the client which snapshot is
				being generated. Could be -1 to create a complete
				snapshot of everything in the game for demo
				recording.

		Returns:
			True if the entity doesn't have to be in the snapshot.
	*/
	bool NetworkClipped(int SnappingClient) const;
	bool NetworkClipped(int SnappingClient, vec2 CheckPos) const;
	bool NetworkClippedLine(int SnappingClient, vec2 StartPos, vec2 EndPos) const;

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

bool NetworkClipped(const CGameContext *pGameServer, int SnappingClient, vec2 CheckPos);
bool NetworkClippedLine(const CGameContext *pGameServer, int SnappingClient, vec2 StartPos, vec2 EndPos);

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

template <typename T>
class TEntityPtr
{
public:
	using value_type = T;

	TEntityPtr(T *pData)
		: m_pData(pData)
	{
	}

	T &operator*()
	{
		return *(static_cast<T*>(m_pData));
	}
	T *operator->()
	{
		return static_cast<T*>(m_pData);
	}
	
	T *data()
	{
		return static_cast<T*>(m_pData);
	}

	operator bool()
	{
		return m_pData;
	}

	operator T *()
	{
		return static_cast<T*>(m_pData);
	}

	TEntityPtr &operator++()
	{
		if(m_pData)
		{
			m_pData = m_pData->TypeNext();
		}
		
		return *this;
	}

	TEntityPtr operator++(int)
	{
		TEntityPtr tmp(m_pData);
		if(m_pData)
		{
			m_pData = m_pData->TypeNext();
		}
		
		return tmp;
	}
	
	friend bool operator==(const TEntityPtr &a, const TEntityPtr &b)
	{
		return a.m_pData == b.m_pData;
	}

	friend bool operator==(const TEntityPtr &a, const T *b)
	{
		return a.m_pData == b;
	}

	friend bool operator==(const TEntityPtr &a, T *b)
	{
		return a.m_pData == b;
	}

private:
	CEntity *m_pData = nullptr;
};

#endif
