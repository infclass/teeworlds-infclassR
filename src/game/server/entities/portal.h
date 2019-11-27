/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PORTAL_H
#define GAME_SERVER_ENTITIES_PORTAL_H

#include <game/server/entity.h>

class CPortal : public CEntity
{
public:
	enum
	{
		NUM_HINT = 12,
		NUM_SIDE = 12,
		NUM_IDS = NUM_HINT + NUM_SIDE,
	};

	enum PortalType
	{
		Disconnected,
		In,
		Out,
	};
	CPortal(CGameWorld *pGameWorld, vec2 CenterPos, int OwnerClientID, PortalType Type);
	~CPortal() override;

	void Snap(int SnappingClient) override;
	void Reset() override;
	void TickPaused() override;
	void Tick() override;

	int GetOwner() const;
	int GetNewEntitySound() const;

	PortalType GetPortalType() const;
	void ConnectPortal(CPortal *anotherPortal);
	void Disconnect();
	CPortal *GetAnotherPortal() const;
	float GetRadius() const { return m_Radius; }

protected:
	void StartParallelsVisualEffect();
	void StartMeridiansVisualEffect();
	void MoveParallelsParticles();
	void MoveMeridiansParticles();
	void TeleportCharacters();
	float GetSpeedMultiplier();

protected:
	// visual
	const float m_ParticleStartSpeed = 1.1f;
	const float m_ParticleAcceleration = 1.01f;
	int m_ParticleStopTickTime; // when X time is left stop creating particles - close animation

	int m_IDs[NUM_IDS];
	vec2 m_ParticlePos[NUM_IDS];
	vec2 m_ParticleVec[NUM_HINT];
	float m_Radius = 0;
	float m_Angle = 0;

	PortalType m_PortalType = PortalType::Disconnected;
	CPortal *m_AnotherPortal = nullptr;

	int m_StartTick;
	int m_Owner;
};

#endif // GAME_SERVER_ENTITIES_PORTAL_H
