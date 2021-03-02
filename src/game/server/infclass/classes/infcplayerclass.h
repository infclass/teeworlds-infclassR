#ifndef GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H
#define GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H

#include <base/vmath.h>
#include <game/server/entity.h>

class CConfig;
class CGameContext;
class CGameWorld;
class CInfClassCharacter;
class CInfClassGameContext;
class CInfClassPlayer;
class IServer;

class CInfClassPlayerClass
{
	MACRO_ALLOC_POOL_ID()

public:
	CInfClassPlayerClass();
	virtual ~CInfClassPlayerClass() = default;

	void SetCharacter(CInfClassCharacter *character);

	CGameContext *GameContext() const;
	CGameContext *GameServer() const;
	CGameWorld *GameWorld() const;
	CConfig *Config();
	IServer *Server();
	CInfClassPlayer *GetPlayer();
	int GetCID();
	vec2 GetPos() const;
	vec2 GetDirection() const;
	float GetProximityRadius() const;

protected:
	CInfClassCharacter *m_pCharacter = nullptr;
};

#endif // GAME_SERVER_INFCLASS_CLASSES_PLAYER_CLASS_H
