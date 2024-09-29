/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include "character.h"

#include <engine/shared/config.h>

#include <game/generated/protocol.h>
#include <game/generated/server_data.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/teams.h>

int CCharacter::EntityId = CGameWorld::ENTTYPE_CHARACTER;

MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld) :
	CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER, vec2(0, 0), CCharacterCore::PhysicalSize())
{
	m_Health = 0;
	m_Armor = 0;
	
/* INFECTION MODIFICATION START ***************************************/
	m_MaxArmor = 10;

	m_AntiFireTime = 0;
	m_PainSoundTimer = 0;
	m_DartLifeSpan = -1;
	m_InAirTick = 0;
	m_InWater = 0;
	m_WaterJumpLifeSpan = 0;
	m_HasIndicator = false;
/* INFECTION MODIFICATION END *****************************************/
}

CCharacter::~CCharacter()
{
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
	m_Core.m_Id = m_pPlayer->GetCid();
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCid()] = &m_Core;

	m_ReckoningTick = 0;
	m_SendCore = CCharacterCore();
	m_ReckoningCore = CCharacterCore();

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	DDRaceInit();

	m_NeededFaketuning = 0; // reset fake tunings on respawn and send the client

	return true;
}

void CCharacter::Destroy()
{	
	if(m_pPlayer)
		GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCid()] = 0;
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

bool CCharacter::IsGrounded() const
{
	if(Collision()->CheckPoint(GetPos().x + m_ProximityRadius / 2, GetPos().y + m_ProximityRadius / 2 + 5))
		return true;
	if(Collision()->CheckPoint(GetPos().x - m_ProximityRadius / 2, GetPos().y + m_ProximityRadius / 2 + 5))
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
		const vec2 GroundElasticity{};
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), GroundElasticity);
		m_Core.m_Vel = vec2(0.f, 0.f);
	}	
}

void CCharacter::DoWeaponSwitch()
{
/* INFECTION MODIFICATION START ***************************************/
	if(m_QueuedWeapon == -1 || !m_aWeapons[m_QueuedWeapon].m_Got)
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

void CCharacter::OnPredictedInput(const CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(const CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 1 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ReleaseHook()
{
	m_Core.SetHookedPlayer(-1);
	m_Core.m_HookState = HOOK_RETRACTED;
	m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
}

void CCharacter::ResetHook()
{
	ReleaseHook();
	m_Core.m_HookPos = m_Core.m_Pos;
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire & 1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::PreTick()
{
	// set emote
	if(m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = m_pPlayer->GetDefaultEmote();
		m_EmoteStop = -1;
	}
}

void CCharacter::Tick()
{
	PreTick();

	m_Core.m_Id = GetPlayer()->GetCid();
/* INFECTION MODIFICATION START ***************************************/
	PreCoreTick();

	m_Core.m_Input = m_Input;
	
	CCharacterCore::CParams CoreTickParams(&m_pPlayer->m_NextTuningParams);
	//~ CCharacterCore::CParams CoreTickParams(&GameWorld()->m_Core.m_Tuning);

	if(PrivateGetPlayerClass() == EPlayerClass::Spider)
	{
		CoreTickParams.m_HookGrabTime = g_Config.m_InfSpiderHookTime*SERVER_TICK_SPEED;
	}
	if(PrivateGetPlayerClass() == EPlayerClass::Bat)
	{
		CoreTickParams.m_HookGrabTime = g_Config.m_InfBatHookTime*SERVER_TICK_SPEED;
	}

	CoreTickParams.m_HookMode = GetEffectiveHookMode();
	
	m_Core.Tick(true, &CoreTickParams);

	HandleWaterJump();
	HandleWeapons();

	PostCoreTick();

/* INFECTION MODIFICATION END *****************************************/

	// Previnput
	m_PrevInput = m_Input;

	m_PrevPos = m_Core.m_Pos;
}

void CCharacter::TickDeferred()
{
	// advance the dummy
	{
		CCharacterCore::CParams CoreTickParams(&GameWorld()->m_Core.m_Tuning);
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision(), &Teams()->m_Core);
		m_ReckoningCore.m_Id = m_pPlayer->GetCid();
		m_ReckoningCore.Tick(false, &CoreTickParams);
		m_ReckoningCore.Move(&CoreTickParams);
		m_ReckoningCore.Quantize();
	}

	CCharacterCore::CParams CoreTickParams(&m_pPlayer->m_NextTuningParams);
	
	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.m_Id = m_pPlayer->GetCid();
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
		} StartPosX, StartPosY, StartVelX, StartVelY;

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
	if(m_Armor >= m_MaxArmor)
		return false;

	SetHealthArmor(m_Health, m_Armor + Amount);

	return true;
}

bool CCharacter::IncreaseOverallHp(int Amount)
{
	int MissingHealth = 10 - m_Health;
	int MissingArmor = m_MaxArmor - m_Armor;
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
	int TotalBefore = m_Health + m_Armor;

	m_Health = clamp<int>(HealthAmount, 0, 10);
	m_Armor = clamp<int>(ArmorAmount, 0, m_MaxArmor);

	int TotalAfter = m_Health + m_Armor;

	OnTotalHealthChanged(TotalAfter - TotalBefore);
}

int CCharacter::GetHealthArmorSum()
{
	return m_Health + m_Armor;
}

void CCharacter::SetMaxArmor(int Amount)
{
	m_MaxArmor = Amount;
}

void CCharacter::Die(int Killer, int Weapon)
{
}

//TODO: Move the emote stuff to a function
void CCharacter::SnapCharacter(int SnappingClient, int Id)
{
}

bool CCharacter::CanSnapCharacter(int SnappingClient)
{
	if(SnappingClient == SERVER_DEMO_CLIENT)
		return true;

	return true;
}

bool CCharacter::IsSnappingCharacterInView(int SnappingClientId)
{
	int Id = m_pPlayer->GetCid();

	// A player may not be clipped away if his hook or a hook attached to him is in the field of view
	bool PlayerAndHookNotInView = NetworkClippedLine(SnappingClientId, m_Pos, m_Core.m_HookPos);
	bool AttachedHookInView = false;
	if(PlayerAndHookNotInView)
	{
		for(const auto &AttachedPlayerId : m_Core.m_AttachedPlayers)
		{
			const CCharacter *pOtherPlayer = GameServer()->GetPlayerChar(AttachedPlayerId);
			if(pOtherPlayer && pOtherPlayer->m_Core.HookedPlayer() == Id)
			{
				if(!NetworkClippedLine(SnappingClientId, m_Pos, pOtherPlayer->m_Pos))
				{
					AttachedHookInView = true;
					break;
				}
			}
		}
	}
	if(PlayerAndHookNotInView && !AttachedHookInView)
	{
		return false;
	}
	return true;
}

void CCharacter::Snap(int SnappingClient)
{
}

bool CCharacter::CanCollide(int ClientId)
{
	return Teams()->m_Core.CanCollide(GetPlayer()->GetCid(), ClientId);
}
bool CCharacter::SameTeam(int ClientId)
{
	return Teams()->m_Core.SameTeam(GetPlayer()->GetCid(), ClientId);
}

int CCharacter::Team()
{
	return Teams()->m_Core.Team(m_pPlayer->GetCid());
}

void CCharacter::HandleSkippableTiles(int Index)
{
#if 0
	if(GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCid(), WEAPON_WORLD);
		return;
	}
#endif

	if(Index < 0)
		return;

	// handle speedup tiles
	if(GameServer()->Collision()->IsSpeedup(Index))
	{
		vec2 Direction, TempVel = m_Core.m_Vel;
		int Force, MaxSpeed = 0;
		float TeeAngle, SpeederAngle, DiffAngle, SpeedLeft, TeeSpeed;
		GameServer()->Collision()->GetSpeedup(Index, &Direction, &Force, &MaxSpeed);
		if(Force == 255 && MaxSpeed)
		{
			m_Core.m_Vel = Direction * (MaxSpeed / 5);
		}
		else
		{
			if(MaxSpeed > 0 && MaxSpeed < 5)
				MaxSpeed = 5;
			if(MaxSpeed > 0)
			{
				if(Direction.x > 0.0000001f)
					SpeederAngle = -std::atan(Direction.y / Direction.x);
				else if(Direction.x < 0.0000001f)
					SpeederAngle = std::atan(Direction.y / Direction.x) + 2.0f * std::asin(1.0f);
				else if(Direction.y > 0.0000001f)
					SpeederAngle = std::asin(1.0f);
				else
					SpeederAngle = std::asin(-1.0f);

				if(SpeederAngle < 0)
					SpeederAngle = 4.0f * std::asin(1.0f) + SpeederAngle;

				if(TempVel.x > 0.0000001f)
					TeeAngle = -std::atan(TempVel.y / TempVel.x);
				else if(TempVel.x < 0.0000001f)
					TeeAngle = std::atan(TempVel.y / TempVel.x) + 2.0f * std::asin(1.0f);
				else if(TempVel.y > 0.0000001f)
					TeeAngle = std::asin(1.0f);
				else
					TeeAngle = std::asin(-1.0f);

				if(TeeAngle < 0)
					TeeAngle = 4.0f * std::asin(1.0f) + TeeAngle;

				TeeSpeed = std::sqrt(std::pow(TempVel.x, 2) + std::pow(TempVel.y, 2));

				DiffAngle = SpeederAngle - TeeAngle;
				SpeedLeft = MaxSpeed / 5.0f - std::cos(DiffAngle) * TeeSpeed;
				if(absolute((int)SpeedLeft) > Force && SpeedLeft > 0.0000001f)
					TempVel += Direction * Force;
				else if(absolute((int)SpeedLeft) > Force)
					TempVel += Direction * -Force;
				else
					TempVel += Direction * SpeedLeft;
			}
			else
				TempVel += Direction * Force;

			m_Core.m_Vel = ClampVel(m_MoveRestrictions, TempVel);
		}
	}
}

/* INFECTION MODIFICATION START ***************************************/
vec2 CCharacter::GetDirection() const
{
	return normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
}

EPlayerClass CCharacter::PrivateGetPlayerClass() const
{
	if(!m_pPlayer)
		return EPlayerClass::None;
	else
		return m_pPlayer->GetClass();
}

bool CCharacter::IsInfected() const
{
	return m_pPlayer->IsInfected();
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

int CCharacter::GetEffectiveHookMode() const
{
	if(m_Core.HookedPlayer() >= 0)
		return 0;

	return m_HookMode;
}

/* INFECTION MODIFICATION END *****************************************/

void CCharacter::PostCoreTick()
{
	// following jump rules can be overridden by tiles, like Refill Jumps, Stopper and Wall Jump
	if(m_Core.m_Jumps == -1)
	{
		// The player has only one ground jump, so his feet are always dark
		m_Core.m_Jumped |= 2;
	}
	else if(m_Core.m_Jumps == 0)
	{
		// The player has no jumps at all, so his feet are always dark
		m_Core.m_Jumped |= 2;
	}
	else if(m_Core.m_Jumps == 1 && m_Core.m_Jumped > 0)
	{
		// If the player has only one jump, each jump is the last one
		m_Core.m_Jumped |= 2;
	}
	else if(m_Core.m_JumpedTotal < m_Core.m_Jumps - 1 && m_Core.m_Jumped > 1)
	{
		// The player has not yet used up all his jumps, so his feet remain light
		m_Core.m_Jumped = 1;
	}

	int CurrentIndex = GameServer()->Collision()->GetMapIndex(m_Pos);
	HandleSkippableTiles(CurrentIndex);
	m_MoveRestrictions = GameServer()->Collision()->GetMoveRestrictions(nullptr, this, m_Pos, 18.0f, CurrentIndex);
}

void CCharacter::SetTeams(CGameTeams *pTeams)
{
	m_pTeams = pTeams;
	m_Core.SetTeamsCore(&m_pTeams->m_Core);
}

void CCharacter::DDRaceInit()
{
	m_Core.m_Id = GetPlayer()->GetCid();
	m_PrevPos = m_Pos;
}

void CCharacter::SetPosition(const vec2 &Position)
{
	m_Core.m_Pos = Position;
}

void CCharacter::Move(vec2 RelPos)
{
	m_Core.m_Pos += RelPos;
}

void CCharacter::ResetVelocity()
{
	m_Core.m_Vel = vec2(0, 0);
}

void CCharacter::SetVelocity(vec2 NewVelocity)
{
	m_Core.m_Vel = ClampVel(m_MoveRestrictions, NewVelocity);
}

void CCharacter::AddVelocity(vec2 Addition)
{
	SetVelocity(m_Core.m_Vel + Addition);
}
