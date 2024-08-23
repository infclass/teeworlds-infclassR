/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				 */
#include "laser-teleport.h"

#include <game/infclass/damage_type.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

int CLaserTeleport::EntityId = CGameWorld::ENTTYPE_LASER_TELEPORT;

void CLaserTeleport::OnFired(CInfClassCharacter *pCharacter, WeaponFireContext *pFireContext, int SelfDamage)
{
	if(pFireContext->NoAmmo)
		return;

	const std::optional<vec2> PortalPos = FindPortalPosition(pCharacter);
	if(!PortalPos.has_value())
	{
		pFireContext->FireAccepted = false;
		return;
	}

	const int OwnerCid = pCharacter->GetCid();
	vec2 OldPos = pCharacter->GetPos();
	pCharacter->SetPosition(PortalPos.value());
	pCharacter->ResetHook();

	if(SelfDamage)
	{
		pCharacter->TakeDamage(vec2(0.0f, 0.0f), SelfDamage * 2, OwnerCid, EDamageType::SCIENTIST_TELEPORT);
	}
	pCharacter->GameServer()->CreateDeath(OldPos, OwnerCid);
	pCharacter->GameServer()->CreateSound(OldPos, SOUND_GRENADE_FIRE);

	pCharacter->GameServer()->CreateDeath(PortalPos.value(), OwnerCid);
	pCharacter->GameServer()->CreateSound(PortalPos.value(), SOUND_CTF_RETURN);
	new CLaserTeleport(pCharacter->GameServer(), PortalPos.value(), OldPos);
}

std::optional<vec2> CLaserTeleport::FindPortalPosition(CInfClassCharacter *pCharacter)
{
	vec2 PortalShift = vec2(pCharacter->m_Input.m_TargetX, pCharacter->m_Input.m_TargetY);
	vec2 PortalDir = normalize(PortalShift);
	if(length(PortalShift) > 500.0f)
		PortalShift = PortalDir * 500.0f;

	float Iterator = length(PortalShift);
	while(Iterator > 0.0f)
	{
		PortalShift = PortalDir * Iterator;
		vec2 PortalPos = pCharacter->GetPos() + PortalShift;

		if(pCharacter->GameController()->IsSpawnable(PortalPos, EZoneTele::NoTeleport))
		{
			return PortalPos;
		}

		Iterator -= 4.0f;
	}

	return {};
}

CLaserTeleport::CLaserTeleport(CGameContext *pGameContext, vec2 StartPos, vec2 EndPos)
	: CInfCEntity(pGameContext, EntityId)
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
	GameServer()->SnapLaserObject(Context, GetId(), m_EndPos, m_StartPos, Server()->Tick(), GetOwner());
}
