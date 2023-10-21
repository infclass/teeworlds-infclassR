/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				 */
#include "laser-teleport.h"

#include <game/server/gamecontext.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

CLaserTeleport::CLaserTeleport(CGameContext *pGameContext, vec2 StartPos, vec2 EndPos)
	: CInfCEntity(pGameContext, CGameWorld::ENTTYPE_LASER_TELEPORT)
{
	m_StartPos = StartPos;
	m_EndPos = EndPos;
	m_LaserFired = false;
	GameWorld()->InsertEntity(this);
}

void CLaserTeleport::Tick()
{
	if (m_LaserFired)
		GameWorld()->DestroyEntity(this);
}

void CLaserTeleport::Snap(int SnappingClient)
{
	m_LaserFired = true;

	const CInfClassPlayer *pPlayer = GameController()->GetPlayer(SnappingClient);
	const bool AntiPing = pPlayer && pPlayer->GetAntiPingEnabled();

	if(AntiPing)
		return;

	int SnappingClientVersion = GameServer()->GetClientVersion(SnappingClient);
	CSnapContext Context(SnappingClientVersion);
	GameServer()->SnapLaserObject(Context, GetID(), m_EndPos, m_StartPos, Server()->Tick(), GetOwner());
}
