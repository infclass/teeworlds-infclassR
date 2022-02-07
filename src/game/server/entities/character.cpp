/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/

#include <base/math.h>
#include <base/vmath.h>
#include <new>
#include <engine/shared/config.h>
#include <engine/server/roundstatistics.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <iostream>

#include "character.h"
#include "projectile.h"

#include <game/server/infclass/entities/hero-flag.h>

MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld, IConsole *pConsole)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER),
m_pConsole(pConsole)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
	
/* INFECTION MODIFICATION START ***************************************/
	m_AirJumpCounter = 0;
	m_FirstShot = true;
	
	m_FlagID = Server()->SnapNewID();
	m_HeartID = Server()->SnapNewID();
	m_CursorID = Server()->SnapNewID();
	m_BarrierHintID = Server()->SnapNewID();
	m_BarrierHintIDs.set_size(2);
	for(int i=0; i<2; i++)
	{
		m_BarrierHintIDs[i] = Server()->SnapNewID();
	}
	m_AntiFireTime = 0;
	m_PainSoundTimer = 0;
	m_IsFrozen = false;
	m_IsInSlowMotion = false;
	m_FrozenTime = -1;
	m_DartLifeSpan = -1;
	m_IsInvisible = false;
	m_InvisibleTick = 0;
	m_HealTick = 0;
	m_InfZoneTick = -1;
	m_InAirTick = 0;
	m_InWater = 0;
	m_ProtectionTick = 0;
	m_BonusTick = 0;
	m_WaterJumpLifeSpan = 0;
	m_NinjaVelocityBuff = 0;
	m_NinjaStrengthBuff = 0;
	m_NinjaAmmoBuff = 0;
	m_HasWhiteHole = false;
	m_HasIndicator = false;
	m_TurretCount = 0;
	m_BroadcastWhiteHoleReady = -100;
	m_pHeroFlag = nullptr;
	m_ResetKillsTime = 0;
/* INFECTION MODIFICATION END *****************************************/
}

CCharacter::~CCharacter()
{
	FreeChildSnapIDs();
}

bool CCharacter::FindWitchSpawnPosition(vec2& Pos)
{
	float Angle = atan2f(m_Input.m_TargetY, m_Input.m_TargetX);//atan2f instead of atan2
	
	for(int i=0; i<32; i++)
	{
		float TestAngle;
		
		TestAngle = Angle + i * (pi / 32.0f);
		Pos = GetPos() + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;
		
		if(GameServer()->m_pController->IsSpawnable(Pos, ZONE_TELE_NOWITCH))
			return true;
		
		TestAngle = Angle - i * (pi / 32.0f);
		Pos = GetPos() + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;
		
		if(GameServer()->m_pController->IsSpawnable(Pos, ZONE_TELE_NOWITCH))
			return true;
	}
	
	return false;
}

bool CCharacter::FindPortalPosition(vec2 Pos, vec2& Res)
{
	vec2 PortalShift = Pos - GetPos();
	vec2 PortalDir = normalize(PortalShift);
	if(length(PortalShift) > 500.0f)
		PortalShift = PortalDir * 500.0f;
	
	float Iterator = length(PortalShift);
	while(Iterator > 0.0f)
	{
		PortalShift = PortalDir * Iterator;
		vec2 PortalPos = GetPos() + PortalShift;
	
		if(GameServer()->m_pController->IsSpawnable(PortalPos, ZONE_TELE_NOSCIENTIST))
		{
			Res = PortalPos;
			return true;
		}
		
		Iterator -= 4.0f;
	}
	
	return false;
}

void CCharacter::Reset()
{	
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	SetActiveWeapon(WEAPON_GUN);
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;

	m_pPlayer = pPlayer;
	m_Pos = Pos;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = GetPos();
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	return true;
}

void CCharacter::Destroy()
{	
/* INFECTION MODIFICATION START ***************************************/
	DestroyChildEntities();
	FreeChildSnapIDs();
	
	if(m_pPlayer)
		m_pPlayer->ResetNumberKills();
	
/* INFECTION MODIFICATION END *****************************************/

	if(m_pPlayer)
		GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	SetActiveWeapon(W);
	GameServer()->CreateSound(GetPos(), SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		SetActiveWeapon(0);
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(GetPos().x+m_ProximityRadius/2, GetPos().y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(GetPos().x-m_ProximityRadius/2, GetPos().y+m_ProximityRadius/2+5))
		return true;
	return false;
}

void CCharacter::HandleWaterJump()
{
	if(m_InWater)
	{
		m_Core.m_Jumped &= ~2;
		m_Core.m_TriggeredEvents &= ~COREEVENT_GROUND_JUMP;
		m_Core.m_TriggeredEvents &= ~COREEVENT_AIR_JUMP;

		if(m_Input.m_Jump && m_DartLifeSpan <= 0 && m_WaterJumpLifeSpan <=0)
		{
			m_WaterJumpLifeSpan = Server()->TickSpeed()/2;
			m_DartLifeSpan = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
			m_DartDir = Direction;
			m_DartOldVelAmount = length(m_Core.m_Vel);
			
			m_Core.m_TriggeredEvents |= COREEVENT_AIR_JUMP;
		}
	}
	
	m_WaterJumpLifeSpan--;
	
	m_DartLifeSpan--;
	
	if(m_DartLifeSpan == 0)
	{
		//~ m_Core.m_Vel = m_DartDir * 5.0f;
		m_Core.m_Vel = m_DartDir*m_DartOldVelAmount;
	}
	
	if(m_DartLifeSpan > 0)
	{
		m_Core.m_Vel = m_DartDir * 15.0f;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);
		m_Core.m_Vel = vec2(0.f, 0.f);
	}	
}

void CCharacter::DoWeaponSwitch()
{
/* INFECTION MODIFICATION START ***************************************/
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1)
		return;
/* INFECTION MODIFICATION END *****************************************/

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
}

void CCharacter::HandleWeapons()
{
	if(IsFrozen())
		return;
		
	//ninja
	HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();
}

/* INFECTION MODIFICATION START ***************************************/
void CCharacter::SetAntiFire()
{
	m_AntiFireTime = Server()->TickSpeed() * Config()->m_InfAntiFireTime / 1000;
}

/* INFECTION MODIFICATION END *****************************************/

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	int InfWID = GetInfWeaponID(Weapon);
	int MaxAmmo = Server()->GetMaxAmmo(InfWID);
	
	if(InfWID == INFWEAPON_NINJA_GRENADE)
		MaxAmmo = minimum(MaxAmmo + m_NinjaAmmoBuff, 10);
	
	if(Ammo < 0)
		Ammo = MaxAmmo;
	
	if(m_aWeapons[Weapon].m_Ammo < MaxAmmo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = minimum(MaxAmmo, Ammo);
		//dbg_msg("TEST", "TRUE")
		return true;
	}
	return false;
}

void CCharacter::SetActiveWeapon(int Weapon)
{
	m_ActiveWeapon = Weapon;
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::NoAmmo()
{
	// 125ms is a magical limit of how fast a human can click
	m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
	if(m_LastNoAmmoSound + Server()->TickSpeed() * 0.5 <= Server()->Tick())
	{
		GameServer()->CreateSound(GetPos(), SOUND_WEAPON_NOAMMO);
		m_LastNoAmmoSound = Server()->Tick();
	}
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	// set emote
	if(m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = m_pPlayer->GetDefaultEmote();
		m_EmoteStop = -1;
	}

/* INFECTION MODIFICATION START ***************************************/
	//~ if(GameServer()->Collision()->CheckPhysicsFlag(m_Core.m_Pos, CCollision::COLFLAG_WATER))
	//~ {
		//~ if(m_InWater == 0)
		//~ {
			//~ m_InWater = 1;
			//~ m_Core.m_Vel /= 2.0f;
			//~ m_WaterJumpLifeSpan = 0;
		//~ }
	//~ }
	//~ else
		//~ m_InWater = 0;

	--m_FrozenTime;
	if(m_IsFrozen)
	{
		if(m_FrozenTime <= 0)
		{
			Unfreeze();
		}
		else
		{
			int FreezeSec = 1+(m_FrozenTime/Server()->TickSpeed());
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE, BROADCAST_DURATION_REALTIME, _("You are frozen: {sec:EffectDuration}"), "EffectDuration", &FreezeSec, NULL);
		}
	}
	
	
	if(m_SlowMotionTick > 0)
	{
		--m_SlowMotionTick;
		
		if(m_SlowMotionTick <= 0)
		{
			m_IsInSlowMotion = false;
		}
		else
		{
			int SloMoSec = 1+(m_SlowMotionTick/Server()->TickSpeed());
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE, BROADCAST_DURATION_REALTIME, _("You are slowed: {sec:EffectDuration}"), "EffectDuration", &SloMoSec, NULL);
		}
	}
	
	if(m_AntiFireTime > 0)
		--m_AntiFireTime;
	
	if(m_HallucinationTick > 0)
		--m_HallucinationTick;
	
	if(m_LoveTick > 0)
		--m_LoveTick;
	
	if(m_SlipperyTick > 0)
		--m_SlipperyTick;
	
	if(m_ProtectionTick > 0)
	{
		--m_ProtectionTick;

		// Player left spawn before protection ran out
		if(m_InfZoneTick == -1)
		{
			if(!m_IsInvisible)
			{
				SetEmote(EMOTE_NORMAL, Server()->Tick() + Server()->TickSpeed());
			}
			m_ProtectionTick = 0;
		}
	}

	//Ghost
	if(GetPlayerClass() == PLAYERCLASS_GHOST)
	{
		if(Server()->Tick() < m_InvisibleTick + 3*Server()->TickSpeed() || IsFrozen() || IsInSlowMotion())
		{
			m_IsInvisible = false;
		}
		else
		{
			//Search nearest human
			int cellGhostX = static_cast<int>(round(GetPos().x))/32;
			int cellGhostY = static_cast<int>(round(GetPos().y))/32;
			
			vec2 SeedPos = vec2(16.0f, 16.0f) + vec2(
				static_cast<float>(static_cast<int>(round(GetPos().x))/32)*32.0,
				static_cast<float>(static_cast<int>(round(GetPos().y))/32)*32.0);
			
			for(int y=0; y<GHOST_SEARCHMAP_SIZE; y++)
			{
				for(int x=0; x<GHOST_SEARCHMAP_SIZE; x++)
				{
					vec2 Tile = SeedPos + vec2(32.0f*(x-GHOST_RADIUS), 32.0f*(y-GHOST_RADIUS));
					if(GameServer()->Collision()->CheckPoint(Tile))
					{
						m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] = 0x8;
					}
					else
					{
						m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] = 0x0;
					}
				}
			}
			for(CCharacter *p = (CCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
			{
				if(p->IsZombie()) continue;
				
				int cellHumanX = static_cast<int>(round(p->GetPos().x))/32;
				int cellHumanY = static_cast<int>(round(p->GetPos().y))/32;
				
				int cellX = cellHumanX - cellGhostX + GHOST_RADIUS;
				int cellY = cellHumanY - cellGhostY + GHOST_RADIUS;
				
				if(cellX >= 0 && cellX < GHOST_SEARCHMAP_SIZE && cellY >= 0 && cellY < GHOST_SEARCHMAP_SIZE)
				{
					m_GhostSearchMap[cellY * GHOST_SEARCHMAP_SIZE + cellX] |= 0x2;
				}
			}
			m_GhostSearchMap[GHOST_RADIUS * GHOST_SEARCHMAP_SIZE + GHOST_RADIUS] |= 0x1;
			bool HumanFound = false;
			for(int i=0; i<GHOST_RADIUS; i++)
			{
				for(int y=0; y<GHOST_SEARCHMAP_SIZE; y++)
				{
					for(int x=0; x<GHOST_SEARCHMAP_SIZE; x++)
					{
						if(!((m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] & 0x1) || (m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] & 0x8)))
						{
							if(
								(
									(x > 0 && (m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x-1] & 0x1)) ||
									(x < GHOST_SEARCHMAP_SIZE-1 && (m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x+1] & 0x1)) ||
									(y > 0 && (m_GhostSearchMap[(y-1)*GHOST_SEARCHMAP_SIZE+x] & 0x1)) ||
									(y < GHOST_SEARCHMAP_SIZE-1 && (m_GhostSearchMap[(y+1)*GHOST_SEARCHMAP_SIZE+x] & 0x1))
								) ||
								(
									(random_prob(0.25f)) && (
										(x > 0 && y > 0 && (m_GhostSearchMap[(y-1)*GHOST_SEARCHMAP_SIZE+x-1] & 0x1)) ||
										(x > 0 && y < GHOST_SEARCHMAP_SIZE-1 && (m_GhostSearchMap[(y+1)*GHOST_SEARCHMAP_SIZE+x-1] & 0x1)) ||
										(x < GHOST_SEARCHMAP_SIZE-1 && y > 0 && (m_GhostSearchMap[(y-1)*GHOST_SEARCHMAP_SIZE+x+1] & 0x1)) ||
										(x < GHOST_SEARCHMAP_SIZE-1 && y < GHOST_SEARCHMAP_SIZE-1 && (m_GhostSearchMap[(y+1)*GHOST_SEARCHMAP_SIZE+x+1] & 0x1))
									)
								)
							)
							{
								m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] |= 0x4;
								//~ if((Server()->Tick()%5 == 0) && i == (Server()->Tick()/5)%GHOST_RADIUS)
								//~ {
									//~ vec2 HintPos = vec2(
										//~ 32.0f*(cellGhostX + (x - GHOST_RADIUS))+16.0f,
										//~ 32.0f*(cellGhostY + (y - GHOST_RADIUS))+16.0f);
									//~ GameServer()->CreateHammerHit(HintPos);
								//~ }
								if(m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] & 0x2)
								{
									HumanFound = true;
								}
							}
						}
					}
				}
				for(int y=0; y<GHOST_SEARCHMAP_SIZE; y++)
				{
					for(int x=0; x<GHOST_SEARCHMAP_SIZE; x++)
					{
						if(m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] & 0x4)
						{
							m_GhostSearchMap[y*GHOST_SEARCHMAP_SIZE+x] |= 0x1;
						}
					}
				}
			}
			
			if(HumanFound)
			{
				// TODO: Move the code and use CInfClassCharacter::MakeVisible()
				if(m_IsInvisible)
				{
					GameServer()->CreatePlayerSpawn(GetPos());
					m_IsInvisible = false;
				}
				
				m_InvisibleTick = Server()->Tick();
			}
			else
			{
				m_IsInvisible = true;
			}
		}
	}

	PreCoreTick();

	m_Core.m_Input = m_Input;
	
	CCharacterCore::CParams CoreTickParams(&m_pPlayer->m_NextTuningParams);
	//~ CCharacterCore::CParams CoreTickParams(&GameWorld()->m_Core.m_Tuning);
	
	if(GetPlayerClass() == PLAYERCLASS_SPIDER)
	{
		CoreTickParams.m_HookGrabTime = g_Config.m_InfSpiderHookTime*SERVER_TICK_SPEED;
	}
	if(GetPlayerClass() == PLAYERCLASS_BAT)
	{
		CoreTickParams.m_HookGrabTime = g_Config.m_InfBatHookTime*SERVER_TICK_SPEED;
	}
	CoreTickParams.m_HookMode = m_HookMode;
	
	m_Core.Tick(true, &CoreTickParams);
	
	//Hook protection
	if(m_Core.m_HookedPlayer >= 0)
	{
		CPlayer *pHookedPlayer = GameServer()->m_apPlayers[m_Core.m_HookedPlayer];
		if(pHookedPlayer)
		{
			if(IsZombie() == pHookedPlayer->IsZombie() && pHookedPlayer->HookProtectionEnabled())
			{
				m_Core.m_HookedPlayer = -1;
				m_Core.m_HookState = HOOK_RETRACTED;
				m_Core.m_HookPos = GetPos();
			}
		}
	}
	
	HandleWaterJump();
	HandleWeapons();

	PostCoreTick();

	if(GetPlayerClass() == PLAYERCLASS_HUNTER || GetPlayerClass() == PLAYERCLASS_SNIPER ||GetPlayerClass() == PLAYERCLASS_LOOPER)
	{
		if(IsGrounded()) m_AirJumpCounter = 0;
		if(m_Core.m_TriggeredEvents&COREEVENT_AIR_JUMP && m_AirJumpCounter < 1)
		{
			m_Core.m_Jumped &= ~2;
			m_AirJumpCounter++;
		}
	}

	if(GetPlayerClass() == PLAYERCLASS_BAT)
	{
		if(IsGrounded() || g_Config.m_InfBatAirjumpLimit == 0)
		{
			m_AirJumpCounter = 0;
		}
		else if(m_Core.m_TriggeredEvents&COREEVENT_AIR_JUMP && m_AirJumpCounter < g_Config.m_InfBatAirjumpLimit)
		{
			m_Core.m_Jumped &= ~2;
			m_AirJumpCounter++;
		}
	}

	if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		if(!m_pHeroFlag)
			m_pHeroFlag = new CHeroFlag(GameServer(), m_pPlayer->GetCID());
	}
/* INFECTION MODIFICATION END *****************************************/

	if(m_ResetKillsTime)
	{
		m_ResetKillsTime--;
		if(!m_ResetKillsTime)
			GetPlayer()->ResetNumberKills();
	}

	// Previnput
	m_PrevInput = m_Input;
	
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CCharacterCore::CParams CoreTickParams(&GameWorld()->m_Core.m_Tuning);
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false, &CoreTickParams);
		m_ReckoningCore.Move(&CoreTickParams);
		m_ReckoningCore.Quantize();
	}

	CCharacterCore::CParams CoreTickParams(&m_pPlayer->m_NextTuningParams);
	
	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move(&CoreTickParams);
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());


	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(GetPos(), SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if(GetPlayerClass() != PLAYERCLASS_GHOST || !m_IsInvisible)
	{
		if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(GetPos(), SOUND_PLAYER_JUMP, Mask);
		if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(GetPos(), SOUND_HOOK_ATTACH_GROUND, Mask);
		if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(GetPos(), SOUND_HOOK_NOATTACH, Mask);
	}


	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_DamageTakenTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
		
/* INFECTION MODIFICATION START ***************************************/
	++m_HookDmgTick;
/* INFECTION MODIFICATION END *****************************************/
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;

	SetHealthArmor(m_Health + Amount, m_Armor);

	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;

	SetHealthArmor(m_Health, m_Armor + Amount);

	return true;
}

bool CCharacter::IncreaseOverallHp(int Amount)
{
	int MissingHealth = 10 - m_Health;
	int MissingArmor = 10 - m_Armor;
	int ExtraHealthAmount = clamp<int>(Amount, 0, MissingHealth);
	int ExtraArmorAmount = clamp<int>(Amount - ExtraHealthAmount, 0, MissingArmor);

	if((ExtraHealthAmount > 0) || (ExtraArmorAmount > 0))
	{
		SetHealthArmor(m_Health + ExtraHealthAmount, m_Armor + ExtraArmorAmount);
		return true;
	}

	return false;
}

void CCharacter::SetHealthArmor(int HealthAmount, int ArmorAmount)
{
	m_Health = clamp<int>(HealthAmount, 0, 10);
	m_Armor = clamp<int>(ArmorAmount, 0, 10);

	int TotalBefore = m_Health + m_Armor;
	int TotalAfter = m_Health + m_Armor;

	OnTotalHealthChanged(TotalAfter - TotalBefore);
}

int CCharacter::GetHealthArmorSum()
{
	return m_Health + m_Armor;
}

void CCharacter::Die(int Killer, int Weapon)
{
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon, TAKEDAMAGEMODE Mode)
{
	return false;
}

void CCharacter::Snap(int SnappingClient)
{
	int ID = m_pPlayer->GetCID();

	if(!Server()->Translate(ID, SnappingClient))
		return;

	if(NetworkClipped(SnappingClient))
		return;

	if(SnappingClient == m_pPlayer->GetCID())
	{
		if(GetPlayerClass() == PLAYERCLASS_HERO && g_Config.m_InfHeroFlagIndicator && m_pHeroFlag)
		{
			CHeroFlag *pFlag = m_pHeroFlag;

			long tickLimit = m_pPlayer->m_LastActionMoveTick+g_Config.m_InfHeroFlagIndicatorTime*Server()->TickSpeed();
			
			// Guide hero to flag
			if(pFlag->GetCoolDown() <= 0 && Server()->Tick() > tickLimit)
			{
				CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_CursorID, sizeof(CNetObj_Laser)));
				if(!pObj)
					return;

				float Angle = atan2f(pFlag->GetPos().y-GetPos().y, pFlag->GetPos().x-GetPos().x);
				vec2 vecDir = vec2(cos(Angle), sin(Angle));
				vec2 Indicator = GetPos() + vecDir * 84.0f;
				vec2 IndicatorM = GetPos() - vecDir * 84.0f;

				// display laser beam for 0.5 seconds
				int tickShowBeamTime = Server()->TickSpeed()*0.5;
				long ticksInactive = tickShowBeamTime - (Server()->Tick()-tickLimit);
				if(g_Config.m_InfHeroFlagIndicatorTime > 0 && ticksInactive > 0)
				{
					CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
					if(!pObj)
						return;

					Indicator = IndicatorM + vecDir * 168.0f * (1.0f-(ticksInactive/(float)tickShowBeamTime)); 

					pObj->m_X = (int)Indicator.x;
					pObj->m_Y = (int)Indicator.y;
					pObj->m_FromX = (int)IndicatorM.x;
					pObj->m_FromY = (int)IndicatorM.y;
					if(ticksInactive < 4)
						pObj->m_StartTick = Server()->Tick()-(6-ticksInactive);
					else 
						pObj->m_StartTick = Server()->Tick()-3;
				}

				pObj->m_X = (int)Indicator.x;
				pObj->m_Y = (int)Indicator.y;
				pObj->m_FromX = pObj->m_X;
				pObj->m_FromY = pObj->m_Y;
				pObj->m_StartTick = Server()->Tick();
			}
		}
	}
/* INFECTION MODIFICATION END ***************************************/

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, ID, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;
	int EmoteNormal = m_pPlayer->GetDefaultEmote();;
	
	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	if(pCharacter->m_HookedPlayer != -1)
	{
		if(!Server()->Translate(pCharacter->m_HookedPlayer, SnappingClient))
			pCharacter->m_HookedPlayer = -1;
	}
	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_NINJA_HAMMER)
	{
		pCharacter->m_Weapon = WEAPON_NINJA;
	}
	else
	{
		pCharacter->m_Weapon = m_ActiveWeapon;
	}
	
	if(GetPlayerClass() == PLAYERCLASS_SPIDER)
	{
		pCharacter->m_HookTick -= (g_Config.m_InfSpiderHookTime - 1) * SERVER_TICK_SPEED-SERVER_TICK_SPEED/5;
		if(pCharacter->m_HookTick < 0)
			pCharacter->m_HookTick = 0;
	}
	if(GetPlayerClass() == PLAYERCLASS_BAT)
	{
		pCharacter->m_HookTick -= (g_Config.m_InfBatHookTime - 1) * SERVER_TICK_SPEED - SERVER_TICK_SPEED/5;
		if(pCharacter->m_HookTick < 0)
			pCharacter->m_HookTick = 0;
	}
/* INFECTION MODIFICATION END *****************************************/
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}
	
/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MERCENARY_GUN)
	{
		pCharacter->m_AmmoCount /= (Server()->GetMaxAmmo(INFWEAPON_MERCENARY_GUN)/10);
	}
/* INFECTION MODIFICATION END *****************************************/

	if(pCharacter->m_Emote == EmoteNormal)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}

/* INFECTION MODIFICATION START ***************************************/
vec2 CCharacter::GetDirection() const
{
	return normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
}

int CCharacter::GetPlayerClass() const
{
	if(!m_pPlayer)
		return PLAYERCLASS_NONE;
	else
		return m_pPlayer->GetClass();
}

void CCharacter::DestroyChildEntities()
{
	m_NinjaVelocityBuff = 0;
	m_NinjaStrengthBuff = 0;
	m_NinjaAmmoBuff = 0;

	static const auto InfCEntities = {
		CGameWorld::ENTTYPE_PROJECTILE,
		CGameWorld::ENTTYPE_ENGINEER_WALL,
		CGameWorld::ENTTYPE_LOOPER_WALL,
		CGameWorld::ENTTYPE_SOLDIER_BOMB,
		CGameWorld::ENTTYPE_SCATTER_GRENADE,
		CGameWorld::ENTTYPE_MEDIC_GRENADE,
		CGameWorld::ENTTYPE_MERCENARY_BOMB,
		CGameWorld::ENTTYPE_SCIENTIST_MINE,
		CGameWorld::ENTTYPE_BIOLOGIST_MINE,
		CGameWorld::ENTTYPE_SLUG_SLIME,
		CGameWorld::ENTTYPE_GROWINGEXPLOSION,
		CGameWorld::ENTTYPE_WHITE_HOLE,
		CGameWorld::ENTTYPE_SUPERWEAPON_INDICATOR,
		CGameWorld::ENTTYPE_TURRET,
		CGameWorld::ENTTYPE_PLASMA,
		CGameWorld::ENTTYPE_HERO_FLAG,
	};

	for(const auto EntityType : InfCEntities) {
		for(CInfCEntity *p = (CInfCEntity*) GameWorld()->FindFirst(EntityType); p; p = (CInfCEntity*) p->TypeNext())
		{
			if(p->GetOwner() != m_pPlayer->GetCID())
				continue;

			GameServer()->m_World.DestroyEntity(p);
		}
	}

	m_FirstShot = true;
	m_HookMode = 0;
}

void CCharacter::FreeChildSnapIDs()
{
	if(m_FlagID >= 0)
	{
		Server()->SnapFreeID(m_FlagID);
		m_FlagID = -1;
	}
	if(m_HeartID >= 0)
	{
		Server()->SnapFreeID(m_HeartID);
		m_HeartID = -1;
	}
	if(m_CursorID >= 0)
	{
		Server()->SnapFreeID(m_CursorID);
		m_CursorID = -1;
	}

	if(m_BarrierHintID >= 0)
	{
		Server()->SnapFreeID(m_BarrierHintID);
		m_BarrierHintID = -1;
	}

	if(m_BarrierHintIDs[0] >= 0)
	{
		for(int i=0; i<2; i++)
		{
			Server()->SnapFreeID(m_BarrierHintIDs[i]);
			m_BarrierHintIDs[i] = -1;
		}

	}
}

bool CCharacter::IsZombie() const
{
	return m_pPlayer->IsZombie();
}

bool CCharacter::IsHuman() const
{
	return m_pPlayer->IsHuman();
}

bool CCharacter::IsInLove() const
{
	return m_LoveTick > 0;
}

void CCharacter::LoveEffect(float Time)
{
	if(m_LoveTick <= 0)
		m_LoveTick = Server()->TickSpeed() * Time;
}

void CCharacter::HallucinationEffect()
{
	if(m_HallucinationTick <= 0)
		m_HallucinationTick = Server()->TickSpeed()*5;
}

void CCharacter::SlipperyEffect()
{
	if(m_SlipperyTick <= 0)
		m_SlipperyTick = Server()->TickSpeed()/2;
}

void CCharacter::Freeze(float Time, int Player, FREEZEREASON Reason)
{
	if(m_IsFrozen && m_FreezeReason == FREEZEREASON_UNDEAD)
		return;
	
	m_IsFrozen = true;
	m_FrozenTime = Server()->TickSpeed()*Time;
	m_FreezeReason = Reason;
	
	m_LastFreezer = Player;
}

void CCharacter::Unfreeze()
{
	m_IsFrozen = false;
	m_FrozenTime = -1;
	
	if(m_FreezeReason == FREEZEREASON_UNDEAD)
	{
		m_Health = 10.0;
	}
	
	if(m_pPlayer)
	{
		GameServer()->ClearBroadcast(m_pPlayer->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE);
	}
	GameServer()->CreatePlayerSpawn(GetPos());
}

bool CCharacter::IsFrozen() const
{
	return m_IsFrozen > 0;
}

bool CCharacter::IsInSlowMotion() const
{
	return m_SlowMotionTick > 0;
}

// duration in centiSec (10 == 1 second)
void CCharacter::SlowMotionEffect(float Duration, int FromCID)
{
	if(Duration == 0)
		return;
	Duration *= 0.1f;
	int NewSlowTick = Server()->TickSpeed()*Duration;
	if(m_SlowMotionTick <= 0)
	{
		m_SlowMotionTick = NewSlowTick;
		m_SlowEffectApplicant = FromCID;
		m_IsInSlowMotion = true;
		m_Core.m_Vel *= 0.4;
	}
}

int CCharacter::GetInfWeaponID(int WID) const
{
	if(WID == WEAPON_HAMMER)
	{
		switch(GetPlayerClass())
		{
			case PLAYERCLASS_NINJA:
				return INFWEAPON_NINJA_HAMMER;
			default:
				return INFWEAPON_HAMMER;
		}
	}
	else if(WID == WEAPON_GUN)
	{
		switch(GetPlayerClass())
		{
			case PLAYERCLASS_MERCENARY:
				return INFWEAPON_MERCENARY_GUN;
			default:
				return INFWEAPON_GUN;
		}
		return INFWEAPON_GUN;
	}
	else if(WID == WEAPON_SHOTGUN)
	{
		switch(GetPlayerClass())
		{
			case PLAYERCLASS_MEDIC:
				return INFWEAPON_MEDIC_SHOTGUN;
			case PLAYERCLASS_HERO:
				return INFWEAPON_HERO_SHOTGUN;
			case PLAYERCLASS_BIOLOGIST:
				return INFWEAPON_BIOLOGIST_SHOTGUN;
			default:
				return INFWEAPON_SHOTGUN;
		}
	}
	else if(WID == WEAPON_GRENADE)
	{
		switch(GetPlayerClass())
		{
			case PLAYERCLASS_MERCENARY:
				return INFWEAPON_MERCENARY_GRENADE;
			case PLAYERCLASS_MEDIC:
				return INFWEAPON_MEDIC_GRENADE;
			case PLAYERCLASS_SOLDIER:
				return INFWEAPON_SOLDIER_GRENADE;
			case PLAYERCLASS_NINJA:
				return INFWEAPON_NINJA_GRENADE;
			case PLAYERCLASS_SCIENTIST:
				return INFWEAPON_SCIENTIST_GRENADE;
			case PLAYERCLASS_HERO:
				return INFWEAPON_HERO_GRENADE;
			case PLAYERCLASS_LOOPER:
				return INFWEAPON_LOOPER_GRENADE;
			default:
				return INFWEAPON_GRENADE;
		}
	}
	else if(WID == WEAPON_LASER)
	{
		switch(GetPlayerClass())
		{
			case PLAYERCLASS_ENGINEER:
				return INFWEAPON_ENGINEER_LASER;
			case PLAYERCLASS_NINJA:
				return INFWEAPON_BLINDING_LASER;
			case PLAYERCLASS_LOOPER:
				return INFWEAPON_LOOPER_LASER;
			case PLAYERCLASS_SCIENTIST:
				return INFWEAPON_SCIENTIST_LASER;
			case PLAYERCLASS_SNIPER:
				return INFWEAPON_SNIPER_LASER;
			case PLAYERCLASS_HERO:
				return INFWEAPON_HERO_LASER;
			case PLAYERCLASS_BIOLOGIST:
				return INFWEAPON_BIOLOGIST_LASER;
			case PLAYERCLASS_MEDIC:
				return INFWEAPON_MEDIC_LASER;
			case PLAYERCLASS_MERCENARY:
				return INFWEAPON_MERCENARY_LASER;
			default:
				return INFWEAPON_LASER;
		}
	}
	else if(WID == WEAPON_NINJA)
	{
		return INFWEAPON_NINJA;
	}
	else
	{
		return INFWEAPON_NONE;
	}
}

/* INFECTION MODIFICATION END *****************************************/
