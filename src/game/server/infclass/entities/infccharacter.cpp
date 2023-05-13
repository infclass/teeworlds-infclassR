#include "infccharacter.h"
#include "engine/server.h"
#include "game/infclass/classes.h"

#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/collision.h>

#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>

#include <game/server/entities/projectile.h>

#include <game/server/infclass/classes/infected/infected.h>
#include <game/server/infclass/damage_context.h>
#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/death_context.h>
#include <game/server/infclass/entities/plasma.h>
#include <game/server/infclass/entities/superweapon-indicator.h>
#include <game/server/infclass/entities/white-hole.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassCharacter, MAX_CLIENTS)

CInfClassCharacter::CInfClassCharacter(CInfClassGameController *pGameController) :
	CCharacter(pGameController->GameWorld()), m_pGameController(pGameController)
{
}

CInfClassCharacter::~CInfClassCharacter()
{
	FreeChildSnapIDs();
	ResetClassObject();
}

void CInfClassCharacter::ResetClassObject()
{
	if(m_pClass)
	{
		// Ideally we would reset the Class character on `CPlayer::m_pCharacter = 0`
		// but it would be hard to hook there.
		m_pClass->SetCharacter(nullptr);
	}

	m_pClass = nullptr;
}

void CInfClassCharacter::OnCharacterSpawned(const SpawnContext &Context)
{
	SetAntiFire();
	m_IsFrozen = false;
	m_FrozenTime = -1;
	m_LoveTick = -1;
	m_SlowMotionTick = -1;
	m_HallucinationTick = -1;
	m_SlipperyTick = -1;
	m_LastFreezer = -1;
	ResetHelpers();
	m_LastHookers.Clear();
	m_LastHookerTick = -1;
	m_EnforcersInfo.Clear();

	m_DamageZoneTick = -1;
	m_DamageZoneDealtDamage = 0;

	m_Invincible = 0;

	ClassSpawnAttributes();

	if(GetPlayerClass() == PLAYERCLASS_NONE)
	{
		OpenClassChooser();
	}

	m_pClass->OnCharacterSpawned(Context);

	GameController()->OnCharacterSpawned(this);
}

void CInfClassCharacter::OnCharacterInInfectionZone()
{
	if(IsZombie())
	{
		if(Server()->Tick() >= m_HealTick + (Server()->TickSpeed()/g_Config.m_InfInfzoneHealRate))
		{
			m_HealTick = Server()->Tick();
			int BonusArmor = GameController()->InfectedBonusArmor();
			if(m_Armor < BonusArmor)
			{
				Heal(1);
			}
			else
			{
				IncreaseHealth(1);
			}
			if(m_InfZoneTick < 0)
			{
				m_InfZoneTick = Server()->Tick(); // Save Tick when zombie enters infection zone
			}
		}
	}
	else
	{
		if(m_Invincible >= 2)
			return;

		DeathContext Context;

		SDamageContext DamageContext;
		DamageContext.Killer = GetCID();
		DamageContext.DamageType = DAMAGE_TYPE::INFECTION_TILE;
		DamageContext.Mode = TAKEDAMAGEMODE::INFECTION;

		GetDeathContext(DamageContext, &Context);

		GameController()->OnCharacterDeath(this, Context);
		GameServer()->CreateSound(GetPos(), SOUND_PLAYER_DIE);

		GetPlayer()->StartInfection(Context.Killer);
	}
}

void CInfClassCharacter::OnCharacterOutOfInfectionZone()
{
	if(m_InfZoneTick == -1)
		return;

	m_InfZoneTick = -1; // Reset Tick when zombie is not in infection zone

	if(!m_IsInvisible)
	{
		SetEmote(EMOTE_NORMAL, Server()->Tick() + Server()->TickSpeed());
	}

	// Player left spawn before protection ran out
	m_ProtectionTick = 0;
}

void CInfClassCharacter::OnCharacterInBonusZoneTick()
{
	m_BonusTick++;
	if(m_BonusTick > Server()->TickSpeed()*60)
	{
		m_BonusTick = 0;

		GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, _("You have held a bonus area for one minute, +5 points"), NULL);
		GameServer()->SendEmoticon(GetCID(), EMOTICON_MUSIC);
		SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		GiveGift(GIFT_HEROFLAG);

		Server()->RoundStatistics()->OnScoreEvent(GetCID(), SCOREEVENT_BONUS, GetPlayerClass(), Server()->ClientName(GetCID()), GameServer()->Console());
		GameServer()->SendScoreSound(GetCID());
	}
}

void CInfClassCharacter::OnCharacterInDamageZone(float Damage)
{
	constexpr DAMAGE_TYPE DamageType = DAMAGE_TYPE::DAMAGE_TILE;

	const int Tick = Server()->Tick();

	if(m_DamageZoneTick < 0 || (Tick >= (m_DamageZoneTick + Server()->TickSpeed())))
		m_DamageZoneDealtDamage = 0;

	if(Damage > m_DamageZoneDealtDamage)
	{
		Damage -= m_DamageZoneDealtDamage;
		m_DamageZoneDealtDamage += Damage;
		TakeDamage(vec2(), Damage, -1, DamageType);
		m_DamageZoneTick = Server()->Tick();
	}

	if(m_pClass)
	{
		constexpr float DamageDisablesHealingForSeconds = 1;
		constexpr int DamageFrom = -1;
		m_pClass->DisableHealing(DamageDisablesHealingForSeconds, DamageFrom, DamageType);
	}
}

void CInfClassCharacter::Destroy()
{
	ResetClassObject();
	DestroyChildEntities();
	CCharacter::Destroy();
}

void CInfClassCharacter::Tick()
{
	const vec2 PrevPos = m_Core.m_Pos;

	if(IsHuman() && IsAlive() && GameController()->IsInfectionStarted())
	{
	}
	else
		m_BonusTick = 0;

	if(m_pClass)
	{
		// On the very first tick of a new round when the Reset is not complete yet,
		// The character can (still) be in a special zone while still have no class assigned.
		GameController()->HandleCharacterTiles(this);
	}

	CCharacter::Tick();

	if(m_BlindnessTicks > 0)
	{
		--m_BlindnessTicks;
		int EffectSec = 1 + (m_BlindnessTicks / Server()->TickSpeed());
		GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE, BROADCAST_DURATION_REALTIME,
			_("You are blinded: {sec:EffectDuration}"),
			"EffectDuration", &EffectSec,
			nullptr);
	}

	if(m_LastHelper.m_Tick > 0)
	{
		--m_LastHelper.m_Tick;
	}

	if(m_pClass)
		m_pClass->OnCharacterTick();

	if(GetPlayerClass() == PLAYERCLASS_SNIPER && PositionIsLocked())
	{
		m_Core.m_Vel = vec2(0.0f, 0.0f);
		m_Core.m_Pos = PrevPos;
	}
}

void CInfClassCharacter::TickDeferred()
{
	int Events = m_Core.m_TriggeredEvents;

	CCharacter::TickDeferred();

	if(Events & COREEVENT_AIR_JUMP)
	{
		const int64_t MaskOnlyBlind = GameController()->GetBlindCharactersMask(GetCID());
		if(MaskOnlyBlind)
		{
			GameServer()->CreateSound(GetPos(), SOUND_PLAYER_AIRJUMP, MaskOnlyBlind);
		}
	}

	// Ghost events
	int64_t MaskEsceptSelf = CmaskAllExceptOne(m_pPlayer->GetCID());

	if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
		GameServer()->CreateSound(GetPos(), SOUND_HOOK_ATTACH_PLAYER, CmaskAll());

	if(GetPlayerClass() != PLAYERCLASS_GHOST || !m_IsInvisible)
	{
		if(Events & COREEVENT_GROUND_JUMP)
			GameServer()->CreateSound(GetPos(), SOUND_PLAYER_JUMP, MaskEsceptSelf);
		if(Events & COREEVENT_HOOK_ATTACH_GROUND)
			GameServer()->CreateSound(GetPos(), SOUND_HOOK_ATTACH_GROUND, MaskEsceptSelf);
		if(Events & COREEVENT_HOOK_HIT_NOHOOK)
			GameServer()->CreateSound(GetPos(), SOUND_HOOK_NOATTACH, MaskEsceptSelf);
	}
}

void CInfClassCharacter::TickPaused()
{
	if(m_DamageZoneTick != -1)
	{
		m_DamageZoneTick++;
	}
}

void CInfClassCharacter::Snap(int SnappingClient)
{
	int ID = GetCID();

	if(!Server()->Translate(ID, SnappingClient))
		return;

	if(NetworkClipped(SnappingClient))
		return;

	bool DoSnap = true;
	SpecialSnapForClient(SnappingClient, &DoSnap);

	if(!DoSnap)
		return;

	if(m_pClass)
	{
		m_pClass->OnCharacterSnap(SnappingClient);
	}

	CCharacter::Snap(SnappingClient);

	CNetObj_DDNetCharacter *pDDNetCharacter = static_cast<CNetObj_DDNetCharacter *>(Server()->SnapNewItem(NETOBJTYPE_DDNETCHARACTER, ID, sizeof(CNetObj_DDNetCharacter)));
	if(!pDDNetCharacter)
		return;

	pDDNetCharacter->m_Flags = 0;

	if(IsFrozen())
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_MOVEMENTS_DISABLED;

	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_JETPACK;
	}

	if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_TELEGUN_GRENADE;
	}
	if(GetPlayerClass() != PLAYERCLASS_BOOMER)
	{
		if(m_aWeapons[WEAPON_HAMMER].m_Got)
			pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_HAMMER;
	}
	if(m_aWeapons[WEAPON_GUN].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_GUN;
	if(m_aWeapons[WEAPON_SHOTGUN].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_SHOTGUN;
	if(m_aWeapons[WEAPON_GRENADE].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_GRENADE;
	if(m_aWeapons[WEAPON_LASER].m_Got)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_LASER;
	if(m_ActiveWeapon == WEAPON_NINJA)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_WEAPON_NINJA;
	if(IsFrozen())
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_MOVEMENTS_DISABLED;

	pDDNetCharacter->m_Jumps = m_Core.m_Jumps;
	pDDNetCharacter->m_JumpedTotal = m_Core.m_JumpedTotal;

	// Send freeze info only to the version that can handle it correctly
	if(IsZombie() && IsFrozen())
	{
		IServer::CClientInfo ClientInfo = {0};
		if(SnappingClient != SERVER_DEMO_CLIENT)
		{
			Server()->GetClientInfo(SnappingClient, &ClientInfo);
		}

		if(ClientInfo.m_InfClassVersion > 150) // Later on: VERSION_INFC_DDNET_CHARACTER
		{
			pDDNetCharacter->m_FreezeStart = m_Core.m_FreezeStart;
			pDDNetCharacter->m_FreezeEnd = Server()->Tick() + m_FrozenTime;
		}
	}

	pDDNetCharacter->m_TargetX = m_Core.m_Input.m_TargetX;
	pDDNetCharacter->m_TargetY = m_Core.m_Input.m_TargetY;
}

void CInfClassCharacter::SpecialSnapForClient(int SnappingClient, bool *pDoSnap)
{
	CInfClassPlayer *pDestClient = GameController()->GetPlayer(SnappingClient);
	CInfClassCharacter *pDestCharacter = GameController()->GetCharacter(SnappingClient);
	if((GetCID() != SnappingClient) && pDestCharacter && pDestCharacter->IsBlind())
	{
		*pDoSnap = false;
		return;
	}

	if(GetPlayerClass() == PLAYERCLASS_GHOST)
	{
		if(IsInvisible() && !GameController()->CanSeeDetails(SnappingClient, GetCID()))
		{
			*pDoSnap = false;
			return;
		}
	}
	if(GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		CNetObj_Flag *pFlag = (CNetObj_Flag *)Server()->SnapNewItem(NETOBJTYPE_FLAG, m_FlagID, sizeof(CNetObj_Flag));
		if(!pFlag)
			return;
	
		pFlag->m_X = (int)m_Pos.x;
		pFlag->m_Y = (int)m_Pos.y;
		pFlag->m_Team = TEAM_RED;
	}

	if(m_Armor < 10 && SnappingClient != GetCID() && IsHuman() && GetPlayerClass() != PLAYERCLASS_HERO)
	{
		if(pDestClient && pDestClient->GetClass() == PLAYERCLASS_MEDIC)
		{
			CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
			if(!pP)
				return;

			pP->m_X = (int)m_Pos.x;
			pP->m_Y = (int)m_Pos.y - 60.0;
			if(m_Health < 10 && m_Armor == 0)
				pP->m_Type = POWERUP_HEALTH;
			else
				pP->m_Type = POWERUP_ARMOR;
			pP->m_Subtype = 0;
		}
	}
	else if((m_Armor + m_Health) < 10 && SnappingClient != GetCID() && IsZombie() && pDestClient && pDestClient->IsZombie())
	{
		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, m_HeartID, sizeof(CNetObj_Pickup)));
		if(!pP)
			return;

		pP->m_X = (int)m_Pos.x;
		pP->m_Y = (int)m_Pos.y - 60.0;
		pP->m_Type = POWERUP_HEALTH;
		pP->m_Subtype = 0;
	}
}

void CInfClassCharacter::ResetNinjaHits()
{
	m_NumObjectsHit = 0;
}

void CInfClassCharacter::HandleNinja()
{
/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) != INFWEAPON::NINJA_HAMMER)
		return;
/* INFECTION MODIFICATION END *****************************************/

	m_DartLifeSpan--;

	if(m_DartLifeSpan == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_DartDir*m_DartOldVelAmount;
	}

	if(m_DartLifeSpan > 0)
	{
		// Set velocity
		float VelocityBuff = 1.0f + static_cast<float>(m_NinjaVelocityBuff)/2.0f;
		m_Core.m_Vel = m_DartDir * g_pData->m_Weapons.m_Ninja.m_Velocity * VelocityBuff;
		vec2 OldPos = GetPos();
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		if(m_Core.m_Pos != OldPos)
		{
			// Find other players
			for(CInfClassCharacter *pTarget = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); pTarget; pTarget = (CInfClassCharacter *)pTarget->TypeNext())
			{
				if(pTarget->IsHuman())
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if(m_apHitObjects[j] == pTarget)
						bAlreadyHit = true;
				}
				if(bAlreadyHit)
					continue;

				vec2 IntersectPos;
				if(!closest_point_on_line(OldPos, m_Core.m_Pos, pTarget->GetPos(), IntersectPos))
					continue;

				float Len = distance(pTarget->GetPos(), IntersectPos);
				if(Len >= pTarget->GetProximityRadius() / 2 + GetProximityRadius() / 2)
				{
					continue;
				}

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(pTarget->GetPos(), SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = pTarget;

				pTarget->TakeDamage(vec2(0, -10.0f), minimum(g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage + m_NinjaStrengthBuff, 20), GetCID(), DAMAGE_TYPE::NINJA);
			}
		}
	}
}

void CInfClassCharacter::HandleWeaponSwitch()
{
	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(GetPlayerClass() == PLAYERCLASS_SPIDER)
	{
		int WantedHookMode = m_HookMode;

		if(Next < 128) // make sure we only try sane stuff
		{
			while(Next) // Next Weapon selection
			{
				WantedHookMode = (WantedHookMode+1)%2;
				Next--;
			}
		}

		if(Prev < 128) // make sure we only try sane stuff
		{
			while(Prev) // Prev Weapon selection
			{
				WantedHookMode = (WantedHookMode+2-1)%2;
				Prev--;
			}
		}

		// Direct Weapon selection
		if(m_LatestInput.m_WantedWeapon)
			WantedHookMode = m_Input.m_WantedWeapon-1;

		if(WantedHookMode >= 0 && WantedHookMode < 2)
			m_HookMode = WantedHookMode;
	}
	else
	{
		CCharacter::HandleWeaponSwitch();
	}
}

void CInfClassCharacter::FireWeapon()
{
	if(m_AntiFireTime > 0)
		return;

	if(m_ReloadTimer != 0)
		return;

	if((GetPlayerClass() == PLAYERCLASS_NONE) || !GetClass())
		return;

	DoWeaponSwitch();

	bool FullAuto = false;

	if(m_ActiveWeapon == WEAPON_GUN || m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_LASER)
		FullAuto = true;

	if(GetPlayerClass() == PLAYERCLASS_SLUG && m_ActiveWeapon == WEAPON_HAMMER)
		FullAuto = true;

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;
	else if(FullAuto && (m_LatestInput.m_Fire&1) && (m_aWeapons[m_ActiveWeapon].m_Ammo || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON::MERCENARY_GRENADE)
																					   || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON::MEDIC_GRENADE)))
	{
		WillFire = true;
	}

	if(!WillFire || GetPlayer()->MapMenu() > 0)
		return;

	if(IsFrozen())
	{
		// Timer stuff to avoid shrieking orchestra caused by unfreeze-plasma
		if(m_PainSoundTimer <= 0 && !(m_LatestPrevInput.m_Fire & 1))
		{
			m_PainSoundTimer = 1 * Server()->TickSpeed();
			GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
		}
		return;
	}

	const INFWEAPON InfWeaponID = GetInfWeaponID(m_ActiveWeapon);

	WeaponFireContext FireContext;
	FireContext.Weapon = m_ActiveWeapon;
	FireContext.FireAccepted = true;
	FireContext.AmmoConsumed = 1;
	FireContext.AmmoAvailable = m_aWeapons[m_ActiveWeapon].m_Ammo;
	FireContext.NoAmmo = FireContext.AmmoAvailable == 0;
	FireContext.ReloadInterval = Server()->GetFireDelay(InfWeaponID) / 1000.0f;

	GetClass()->OnWeaponFired(&FireContext);

	if(IsInLove() && FireContext.FireAccepted)
	{
		GameServer()->CreateLoveEvent(GetPos());
	}

	if(FireContext.NoAmmo)
	{
		NoAmmo();
		return;
	}

	if(!FireContext.FireAccepted)
	{
		return;
	}

	m_AttackTick = Server()->Tick();

	int &Ammo = m_aWeapons[m_ActiveWeapon].m_Ammo;
	if(Ammo > 0) // -1 == unlimited
	{
		Ammo = maximum(0, Ammo - FireContext.AmmoConsumed);
	}

	if(!m_ReloadTimer)
	{
		SetReloadDuration(FireContext.ReloadInterval);
	}
}

bool CInfClassCharacter::TakeDamage(const vec2 &Force, float FloatDmg, int From, DAMAGE_TYPE DamageType)
{
	SDamageContext DamageContext;
	DamageContext.Killer = From;
	DamageContext.DamageType = DamageType;
	DamageContext.Force = Force;

	{
		int Dmg = FloatDmg;
		if(FloatDmg != Dmg)
		{
			int ExtraDmg = random_prob(FloatDmg - Dmg) ? 1 : 0;
			Dmg += ExtraDmg;
		}
		DamageContext.Damage = Dmg;
	}

	DamageContext.Weapon = CInfClassGameController::DamageTypeToWeapon(DamageType, &DamageContext.Mode);
	return TakeDamage(DamageContext);
}

bool CInfClassCharacter::TakeDamage(SDamageContext DamageContext)
{
	const int From = DamageContext.Killer;
	const int &Weapon = DamageContext.Weapon;
	const DAMAGE_TYPE DamageType = DamageContext.DamageType;
	TAKEDAMAGEMODE &Mode = DamageContext.Mode;
	int &Dmg = DamageContext.Damage;

	/* INFECTION MODIFICATION START ***************************************/

	//KillerPlayer
	CInfClassPlayer *pKillerPlayer = GameController()->GetPlayer(From);
	CInfClassCharacter *pKillerChar = nullptr;
	if(pKillerPlayer)
		pKillerChar = pKillerPlayer->GetCharacter();

	if(Mode == TAKEDAMAGEMODE::INFECTION)
	{
		if(!pKillerPlayer || !pKillerPlayer->IsZombie() || !IsHuman())
		{
			// The infection is only possible if the killer is a zombie and the target is a human
			Mode = TAKEDAMAGEMODE::NOINFECTION;
		}
	}

	if(pKillerChar && pKillerChar->IsInLove())
	{
		Dmg = 0;
		Mode = TAKEDAMAGEMODE::NOINFECTION;
		DamageContext.Force *= 0.1f;
	}

	if(m_Invincible >= 2)
	{
		Mode = TAKEDAMAGEMODE::NOINFECTION;
	}

	GetClass()->OnCharacterDamage(&DamageContext);

	const bool DmgFromHuman = pKillerPlayer && pKillerPlayer->IsHuman();
	if(DmgFromHuman && (GetPlayerClass() == PLAYERCLASS_SOLDIER) && (Weapon == WEAPON_HAMMER))
	{
		// Soldier is immune to any traps force
		DamageContext.Force = vec2(0, 0);
	}

	if((From >= 0) && (From != GetCID()) && (DamageContext.Force.x || DamageContext.Force.y))
	{
		const float CurrentSpeed = length(m_Core.m_Vel);
		const float AddedForce = length(DamageContext.Force);
		if(AddedForce > CurrentSpeed * 0.5)
		{
			UpdateLastEnforcer(From, AddedForce, DamageType, Server()->Tick());
		}
	}

	m_Core.m_Vel += DamageContext.Force;

	if(IsInvincible())
	{
		Dmg = 0;
	}

	if(From != GetCID() && pKillerPlayer)
	{
		if(IsZombie())
		{
			if(pKillerPlayer->IsZombie())
			{
				return false;
			}
		}
		else
		{
			//If the player is a new infected, don't infected other -> nobody knows that he is infected.
			if(!pKillerPlayer->IsZombie() || (Server()->Tick() - pKillerPlayer->m_InfectionTick)*Server()->TickSpeed() < 0.5)
			{
				return false;
			}
		}
	}

/* INFECTION MODIFICATION END *****************************************/

	// m_pPlayer only inflicts half damage on self
	if(From == GetCID())
	{
		if(Mode == TAKEDAMAGEMODE::SELFHARM)
			Dmg = Dmg ? maximum(1, Dmg / 2) : 0;
		else
			return false;
	}

	if(m_Health <= 0)
	{
		return false;
	}

	m_DamageTaken++;

	// create healthmod indicator
	if(Server()->Tick() < m_DamageTakenTick+25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(GetPos(), m_DamageTaken*0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(GetPos(), 0, Dmg);
	}

/* INFECTION MODIFICATION START ***************************************/
	if(Dmg)
	{
		HandleDamage(From, Dmg, DamageType);

		int Armor = GetArmor();
		if(Armor)
		{
			if(Dmg <= Armor)
			{
				Armor -= Dmg;
				Dmg = 0;
			}
			else
			{
				Dmg -= Armor;
				Armor = 0;
			}
		}

		int Health = GetHealth() - Dmg;
		SetHealthArmor(Health, Armor);

		if(From >= 0 && From != GetCID())
			GameServer()->SendHitSound(From);
	}
/* INFECTION MODIFICATION END *****************************************/

	m_DamageTakenTick = Server()->Tick();
	m_InvisibleTick = Server()->Tick();

	// check for death
	if(m_Health <= 0)
	{
		DeathContext Context;
		GetDeathContext(DamageContext, &Context);

		Die(Context);
		return false;
	}

/* INFECTION MODIFICATION START ***************************************/
	if(Mode == TAKEDAMAGEMODE::INFECTION)
	{
		GetPlayer()->StartInfection(DamageContext.Killer);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
			Server()->ClientName(From),
			Server()->ClientName(GetCID()), Weapon);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

		DeathContext Context;
		GetDeathContext(DamageContext, &Context);

		GameController()->SendKillMessage(GetCID(), Context);
	}
/* INFECTION MODIFICATION END *****************************************/

	if(Dmg > 2)
		GameServer()->CreateSound(GetPos(), SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(GetPos(), SOUND_PLAYER_PAIN_SHORT);

	SetEmote(EMOTE_PAIN, Server()->Tick() + 500 * Server()->TickSpeed() / 1000);

	return true;
}

bool CInfClassCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon, TAKEDAMAGEMODE Mode)
{
	DAMAGE_TYPE DamageType = DAMAGE_TYPE::INVALID;
	return TakeDamage(Force, Dmg, From, DamageType);
}

bool CInfClassCharacter::Heal(int HitPoints, int FromCID)
{
	if(GetClass() && GetClass()->IsHealingDisabled())
	{
		return false;
	}

	bool HadFullHealth = m_Health >= 10;
	bool Healed = IncreaseOverallHp(HitPoints);

	if(Healed)
	{
		SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		int Sound = HadFullHealth ? SOUND_PICKUP_ARMOR : SOUND_PICKUP_HEALTH;
		GameContext()->CreateSound(GetPos(), Sound, CmaskOne(GetCID()));

		if(FromCID >= 0)
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCID, HealerHelperDuration);
		}
	}

	return Healed;
}

bool CInfClassCharacter::GiveHealth(int HitPoints, int FromCID)
{
	if(GetClass() && GetClass()->IsHealingDisabled())
	{
		return false;
	}
	
	bool Healed = IncreaseHealth(HitPoints);

	if(Healed)
	{
		SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		int Sound = SOUND_PICKUP_HEALTH;
		GameContext()->CreateSound(GetPos(), Sound, CmaskOne(GetCID()));

		if(FromCID >= 0)
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCID, HealerHelperDuration);
		}
	}

	return Healed;
}

bool CInfClassCharacter::GiveArmor(int HitPoints, int FromCID)
{
	if(GetClass() && GetClass()->IsHealingDisabled())
	{
		return false;
	}

	bool Armored = IncreaseArmor(HitPoints);

	if(Armored)
	{
		SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		int Sound = SOUND_PICKUP_ARMOR;
		GameContext()->CreateSound(GetPos(), Sound, CmaskOne(GetCID()));

		if(FromCID >= 0)
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCID, HealerHelperDuration);
		}
	}

	return Armored;
}

PLAYERCLASS CInfClassCharacter::GetPlayerClass() const
{
	if(!m_pPlayer)
		return PLAYERCLASS_NONE;
	else
		return m_pPlayer->GetClass();
}

void CInfClassCharacter::HandleDamage(int From, int Damage, DAMAGE_TYPE DamageType)
{
	if(!m_TakenDamageDetails.IsEmpty())
	{
		CDamagePoint *pLastHit = &m_TakenDamageDetails.Last();

		if((pLastHit->From == From) && (pLastHit->DamageType == DamageType))
		{
			pLastHit->Amount += Damage;
			pLastHit->Tick = Server()->Tick();
			return;
		}
	}

	CDamagePoint Hit;
	Hit.Amount = Damage;
	Hit.From = From;
	Hit.DamageType = DamageType;
	Hit.Tick = Server()->Tick();

	if(m_TakenDamageDetails.Size() == m_TakenDamageDetails.Capacity())
	{
		m_TakenDamageDetails.RemoveAt(0);
	}
	m_TakenDamageDetails.Add(Hit);
}

void CInfClassCharacter::OnTotalHealthChanged(int Difference)
{
	if(Difference > 0)
	{
		m_TakenDamageDetails.Clear();
	}

	if(m_pPlayer)
	{
		GetPlayer()->OnCharacterHPChanged();
	}
}

void CInfClassCharacter::PrepareToDie(const DeathContext &Context, bool *pRefusedToDie)
{
	switch(Context.DamageType)
	{
	case DAMAGE_TYPE::DEATH_TILE:
		if(m_Invincible >= 3)
		{
			*pRefusedToDie = true;
			return;
		}
		else
		{
			return;
		}
	case DAMAGE_TYPE::GAME:
	case DAMAGE_TYPE::KILL_COMMAND:
	case DAMAGE_TYPE::GAME_FINAL_EXPLOSION:
		// Accept the death to go with the default self kill routine
		return;
	default:
		break;
	}

	if(Context.Killer == GetCID())
	{
		return;
	}

	if(IsInvincible())
	{
		*pRefusedToDie = true;
		return;
	}

	if(GetClass())
	{
		GetClass()->PrepareToDie(Context, pRefusedToDie);
	}
}

// TODO: Move those to CInfClassHuman
bool CInfClassCharacter::PositionIsLocked() const
{
	return m_PositionLocked;
}

void CInfClassCharacter::LockPosition()
{
	m_PositionLocked = true;
}

void CInfClassCharacter::UnlockPosition()
{
	m_PositionLocked = false;
}

bool CInfClassCharacter::IsInSlowMotion() const
{
	return m_SlowMotionTick > 0;
}

float CInfClassCharacter::SlowMotionEffect(float Duration, int FromCID)
{
	if(Duration == 0)
		return 0.0f;
	int NewSlowTick = Server()->TickSpeed() * Duration;
	if(m_SlowMotionTick >= NewSlowTick)
		return 0.0f;

	float AddedDuration = 0;
	if(m_SlowMotionTick > 0)
	{
		AddedDuration = Duration - static_cast<float>(m_SlowMotionTick) / Server()->TickSpeed();
	}
	else
	{
		m_Core.m_Vel *= 0.4f;
		AddedDuration = Duration;
	}

	m_SlowMotionTick = NewSlowTick;
	m_SlowEffectApplicant = FromCID;

	return AddedDuration;
}

void CInfClassCharacter::CancelSlowMotion()
{
	m_SlowMotionTick = -1;
}

void CInfClassCharacter::ResetMovementsInput()
{
	m_Input.m_Jump = 0;
	m_Input.m_Direction = 0;
}

void CInfClassCharacter::ResetHookInput()
{
	m_Input.m_Hook = 0;
}

void CInfClassCharacter::GiveNinjaBuf()
{
	if(GetPlayerClass() != PLAYERCLASS_NINJA)
		return;

	switch(random_int(0, 2))
	{
		case 0: //Velocity Buff
			m_NinjaVelocityBuff++;
			GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, _("Sword velocity increased"), NULL);
			break;
		case 1: //Strength Buff
			m_NinjaStrengthBuff++;
			GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, _("Sword strength increased"), NULL);
			break;
		case 2: //Ammo Buff
			m_NinjaAmmoBuff++;
			GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, _("Grenade limit increased"), NULL);
			break;
	}
}

void CInfClassCharacter::AddHelper(int HelperCID, float Time)
{
	if(HelperCID == GetCID())
		return;

	if(HelperCID < 0)
		return;

	int HelpTicks = Server()->TickSpeed() * Time;
	const int NewHelpPriority = 12;
	if(m_LastHelper.m_Tick > (HelpTicks + Server()->TickSpeed() * NewHelpPriority))
	{
		// Keep the previous helper
		return;
	}

	m_LastHelper.m_CID = HelperCID;
	m_LastHelper.m_Tick = HelpTicks;
	dbg_msg("tracking", "%d added as a helper of %d for %d", HelperCID, GetCID(), m_LastHelper.m_Tick);
}

void CInfClassCharacter::ResetHelpers()
{
	m_LastHelper.m_CID = -1;
	m_LastHelper.m_Tick = 0;
}

void CInfClassCharacter::GetDeathContext(const SDamageContext &DamageContext, DeathContext *pContext) const
{
	pContext->Killer = DamageContext.Killer;
	pContext->DamageType = DamageContext.DamageType;

	const int GivenKiller = DamageContext.Killer;
	const DAMAGE_TYPE DamageType = DamageContext.DamageType;

	switch(DamageContext.DamageType)
	{
	case DAMAGE_TYPE::GAME:
	case DAMAGE_TYPE::GAME_FINAL_EXPLOSION:
	case DAMAGE_TYPE::GAME_INFECTION:
		return;
	default:
		break;
	}

	// Test Cases:
	// - Soldier exploded themself while being hooked by an infected:
	//   Message: Killed by the infected with SOLDIER_BOMB
	// - Medic pushed an infected to spikes by a shotgun:
	//   Medic killed the infected with DEATH_TILE
	// - Medic pushed an infected to LaserWall by a shotgun:
	//   Medic killed the infected with LASER_WALL
	// - An infected was freezed by ninja and then fell on a DEATH_TILE:
	//   Ninja killed the infected with DEATH_TILE
	// - Inf freezed by ninja and an engi hooked the inf to make it falling down (to kill tiles):
	//   killer=engi with assistant=ninja killed the inf with DEATH_TILE
	// - Sniper killed a merc-poisoned inf from a locked position:
	//   killer=sniper (insta-kill)
	// - Sniper killed a merc-poisoned inf from unlocked position:
	//   killer=sniper assistant=merc
	// - Scientist spawns a WhiteHole which drags an infected and then the inf does selfkill:
	//   killer=scientist
	//
	// - Hunter hammered a med poisoned by a slug:
	//   killer=hunter (insta kill)
	// - Hunter hammered a hero poisoned by a slug:
	//   killer=hunter assistant=slug
	// - Smoker hooked a med poisoned by a slug:
	//   killer=smoker assistant=slug

	const auto AddUnique = [](int CID, ClientsArray *pArray)
	{
		if(pArray->Contains(CID))
			return;

		pArray->Add(CID);
	};

	ClientsArray MustBeKillerOrAssistant;
	// If killed with a LASER_WALL then the Engineer must be either the Killer orthe  Assistant
	if(DamageType == DAMAGE_TYPE::LASER_WALL)
	{
		// GivenKiller is the wall owner
		AddUnique(GivenKiller, &MustBeKillerOrAssistant);
	}

	// If the victim affected by a WhiteHole then
	// the Scientist must be either the Killer or the Assistant
	for(const EnforcerInfo &Enforcer : m_EnforcersInfo)
	{
		const float MaxSecondsAgo = 1.0;
		if(Enforcer.m_Tick + Server()->TickSpeed() * MaxSecondsAgo < Server()->Tick())
		{
			continue;
		}

		if(Enforcer.m_DamageType == DAMAGE_TYPE::WHITE_HOLE)
		{
			AddUnique(Enforcer.m_CID, &MustBeKillerOrAssistant);
			break;
		}
	}

	if(IsFrozen() && (m_LastFreezer >= 0))
	{
		// The Freezer must be either the Killer or the Assistant
		AddUnique(m_LastFreezer, &MustBeKillerOrAssistant);
	}

	ClientsArray HookersRightNow;
	if(m_LastHookerTick + 1 >= Server()->Tick())
	{
		// + 1 to still count hookers from the previous tick for the case if the
		// kill happened before GameController::Tick() came to HandleLastHookers() at this Tick.
		HookersRightNow = m_LastHookers;
	}

	bool DirectKill = true;
	switch(DamageType) {
	case DAMAGE_TYPE::DEATH_TILE:
	case DAMAGE_TYPE::INFECTION_TILE:
	case DAMAGE_TYPE::KILL_COMMAND:
	case DAMAGE_TYPE::LASER_WALL:
		DirectKill = false;
	default:
		break;
	}

	ClientsArray Killers;
	ClientsArray Assistants;
	if(!DirectKill)
	{
		Killers = HookersRightNow;

		if(m_LastFreezer >= 0)
		{
			AddUnique(m_LastFreezer, &Killers);
		}
	}

	if(IsInSlowMotion() && (m_SlowEffectApplicant >= 0))
	{
		// The Looper should be the Assistant (if not the killer) - before any other player
		AddUnique(m_SlowEffectApplicant, &Assistants);
	}

	if(IsBlind())
	{
		// The Blinder should be the Assistant (if not the killer) - before any other player
		AddUnique(m_LastBlinder, &Assistants);
	}

	if(GivenKiller != GetCID())
	{
		AddUnique(GivenKiller, &Killers);
	}

	if(DirectKill && !m_TakenDamageDetails.IsEmpty())
	{
		// DirectDieCall means that this is a direct die() call.
		// It means that the dealt damage does not matter.
		bool DirectDieCall = m_TakenDamageDetails.Last().From != GivenKiller || m_TakenDamageDetails.Last().DamageType != DamageType;

		bool SniperOneshot = (DamageType == DAMAGE_TYPE::SNIPER_RIFLE) && (m_TakenDamageDetails.Last().From == GivenKiller) && (m_TakenDamageDetails.Last().Amount >= 20);
		bool InevitableDeath = DirectDieCall || (DamageContext.Mode == TAKEDAMAGEMODE::INFECTION) || SniperOneshot;

		if(InevitableDeath)
		{
		}
		else
		{
			// Consider only the last N seconds
			float MaxTime = 7;

			int MinAcceptableTick = Server()->Tick() - MaxTime * Server()->TickSpeed();

			const CDamagePoint *pPoint = nullptr;
			for(int i = m_TakenDamageDetails.Size() - 1; i >= 0; --i) {
				if(m_TakenDamageDetails.At(i).From == GivenKiller)
					continue;

				if(m_TakenDamageDetails.At(i).Tick < MinAcceptableTick)
				{
					// Too old
					break;
				}

				if(pPoint && (pPoint->Amount > m_TakenDamageDetails.At(i).Amount))
				{
					continue;
				}

				pPoint = &m_TakenDamageDetails.At(i);
			}
			if(pPoint)
			{
				AddUnique(pPoint->From, &Assistants);
			}
		}
	}

	if(DirectKill)
	{
		for(int CID : HookersRightNow)
		{
			AddUnique(CID, &Assistants);
		}
	}

	{
		ClientsArray &Enforcers = DirectKill ? Assistants : Killers;

		for(const EnforcerInfo &info : m_EnforcersInfo)
		{
			if(info.m_Tick > m_LastHookerTick)
			{
				AddUnique(info.m_CID, &Enforcers);
			}
		}

		if((m_LastHookerTick > 0) && (!m_LastHookers.IsEmpty()))
		{
			AddUnique(m_LastHookers.First(), &Enforcers);
		}
	}

	int Killer = Killers.IsEmpty() ? GivenKiller : Killers.First();
	int Assistant = -1;

	if((Killer >= 0) && (GetCID() != Killer))
	{
		const CInfClassCharacter *pKiller = GameController()->GetCharacter(Killer);
		if(pKiller && pKiller->m_LastHelper.m_Tick > 0)
		{
			// Check if the helper is in game
			const CInfClassCharacter *pKillerHelper = GameController()->GetCharacter(pKiller->m_LastHelper.m_CID);
			if(pKillerHelper)
			{
				AddUnique(pKiller->m_LastHelper.m_CID, &Assistants);
			}
		}
	}

	if(Killers.Size() > 1)
	{
		Assistant = Killers.At(1);
	}

	if(!MustBeKillerOrAssistant.IsEmpty())
	{
		int First = MustBeKillerOrAssistant.First();
		if(Killer != First && Assistant != First)
		{
			Assistant = First;
		}
		else if(MustBeKillerOrAssistant.Size() > 1)
		{
			int Second = MustBeKillerOrAssistant.At(1);
			if(Killer != Second && Assistant != Second)
			{
				Assistant = Second;
			}
		}
	}

	if(Assistant < 0)
	{
		for(const int CID : Assistants)
		{
			if(CID == Killer)
				continue;

			Assistant = CID;
			break;
		}
	}

	pContext->Killer = Killer;
	pContext->Assistant = Assistant;
}

void CInfClassCharacter::UpdateLastHookers(const ClientsArray &Hookers, int HookerTick)
{
	m_LastHookers = Hookers;
	m_LastHookerTick = HookerTick;
}

void CInfClassCharacter::UpdateLastEnforcer(int ClientID, float Force, DAMAGE_TYPE DamageType, int Tick)
{
	if(Force < 3)
		return;

	if(m_EnforcersInfo.Size() == m_EnforcersInfo.Capacity())
	{
		m_EnforcersInfo.RemoveAt(0);
	}

	if(!m_EnforcersInfo.IsEmpty())
	{
		if(m_EnforcersInfo.Last().m_CID == ClientID)
		{
			m_EnforcersInfo.Last().m_Tick = Tick;
			return;
		}
	}

	EnforcerInfo Info;
	Info.m_CID = ClientID;
	Info.m_DamageType = DamageType;
	Info.m_Tick = Tick;

	m_EnforcersInfo.Add(Info);
}

void CInfClassCharacter::RemoveReferencesToCID(int ClientID)
{
	for(int i = 0; i < m_EnforcersInfo.Size(); ++i)
	{
		if(m_EnforcersInfo.At(i).m_CID == ClientID)
		{
			m_EnforcersInfo.RemoveAt(i);
		}
	}

	if(m_LastFreezer == ClientID)
	{
		m_LastFreezer = -1;
	}

	if(m_LastHelper.m_CID == ClientID)
	{
		m_LastHelper.m_CID = -1;
	}

	m_LastHookers.RemoveOne(ClientID);

	for(int i = m_TakenDamageDetails.Size() - 1; i >= 0; --i)
	{
		if(m_TakenDamageDetails.At(i).From == ClientID)
		{
			m_TakenDamageDetails.RemoveAt(i);
		}
	}
}

void CInfClassCharacter::SaturateVelocity(vec2 Force, float MaxSpeed)
{
	if(length(Force) < 0.00001)
		return;

	float Speed = length(m_Core.m_Vel);
	vec2 VelDir = normalize(m_Core.m_Vel);
	if(Speed < 0.00001)
	{
		VelDir = normalize(Force);
	}
	vec2 OrthoVelDir = vec2(-VelDir.y, VelDir.x);
	float VelDirFactor = dot(Force, VelDir);
	float OrthoVelDirFactor = dot(Force, OrthoVelDir);

	vec2 NewVel = m_Core.m_Vel;
	if(Speed < MaxSpeed || VelDirFactor < 0.0f)
	{
		NewVel += VelDir*VelDirFactor;
		float NewSpeed = length(NewVel);
		if(NewSpeed > MaxSpeed)
		{
			if(VelDirFactor > 0.f)
				NewVel = VelDir*MaxSpeed;
			else
				NewVel = -VelDir*MaxSpeed;
		}
	}

	NewVel += OrthoVelDir * OrthoVelDirFactor;

	m_Core.m_Vel = NewVel;
}

bool CInfClassCharacter::IsPassenger() const
{
	return m_Core.m_IsPassenger;
}

bool CInfClassCharacter::HasPassenger() const
{
	return m_Core.m_Passenger;
}

CInfClassCharacter *CInfClassCharacter::GetPassenger()
{
	if(!m_Core.m_Passenger)
	{
		return nullptr;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacterCore *pCharCore = GameServer()->m_World.m_Core.m_apCharacters[i];
		if(pCharCore == m_Core.m_Passenger)
			return GameController()->GetCharacter(i);
	}

	return nullptr;
}

CInfClassCharacter *CInfClassCharacter::GetTaxiDriver()
{
	if(!IsPassenger())
	{
		return nullptr;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacterCore *pCharCore = GameServer()->m_World.m_Core.m_apCharacters[i];
		if(pCharCore && (pCharCore->m_Passenger == &m_Core))
			return GameController()->GetCharacter(i);
	}

	return nullptr;
}

void CInfClassCharacter::SetPassenger(CCharacter *pPassenger)
{
	m_Core.SetPassenger(pPassenger ? &pPassenger->m_Core : nullptr);
}

int CInfClassCharacter::GetInfZoneTick() // returns how many ticks long a player is already in InfZone
{
	if(m_InfZoneTick < 0)
		return 0;

	return Server()->Tick() - m_InfZoneTick;
}

bool CInfClassCharacter::HasSuperWeaponIndicator() const
{
	return m_HasIndicator;
}

void CInfClassCharacter::SetSuperWeaponIndicatorEnabled(bool Enabled)
{
	if(m_HasIndicator == Enabled)
		return;

	// create an indicator object
	if(Enabled)
	{
		new CSuperWeaponIndicator(GameServer(), GetPos(), GetCID());
	}
	m_HasIndicator = Enabled;
}

CGameWorld *CInfClassCharacter::GameWorld() const
{
	return m_pGameController->GameWorld();
}

const IServer *CInfClassCharacter::Server() const
{
	return m_pGameController->GameWorld()->Server();
}

void CInfClassCharacter::OpenClassChooser()
{
	GameController()->OnClassChooserRequested(this);
}

void CInfClassCharacter::HandleMapMenu()
{
	CInfClassPlayer *pPlayer = GetPlayer();
	if(GetPlayerClass() != PLAYERCLASS_NONE)
	{
		SetAntiFire();
		pPlayer->CloseMapMenu();
		return;
	}

	vec2 CursorPos = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
	if(length(CursorPos) < 100.0f)
	{
		pPlayer->m_MapMenuItem = -1;
		GameServer()->SendBroadcast_Localization(GetCID(),
			BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
			_("Choose your class"), NULL);

		return;
	}

	float Angle = 2.0f * pi + atan2(CursorPos.x, -CursorPos.y);
	float AngleStep = 2.0f * pi / static_cast<float>(CMapConverter::NUM_MENUCLASS);
	int HoveredMenuItem = ((int)((Angle + AngleStep / 2.0f) / AngleStep)) % CMapConverter::NUM_MENUCLASS;
	if(HoveredMenuItem == CMapConverter::MENUCLASS_RANDOM)
	{
		GameServer()->SendBroadcast_Localization(GetCID(),
			BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Random choice"), nullptr);
		pPlayer->m_MapMenuItem = HoveredMenuItem;
	}
	else
	{
		PLAYERCLASS NewClass = CInfClassGameController::MenuClassToPlayerClass(HoveredMenuItem);
		CLASS_AVAILABILITY Availability = GameController()->GetPlayerClassAvailability(NewClass, pPlayer);

		switch(Availability)
		{
		case CLASS_AVAILABILITY::AVAILABLE:
		{
			const char *pClassName = CInfClassGameController::GetClassDisplayName(NewClass);
			GameServer()->SendBroadcast_Localization(GetCID(),
				BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
				pClassName, nullptr);
		}
		break;
		case CLASS_AVAILABILITY::DISABLED:
			GameServer()->SendBroadcast_Localization(GetCID(),
				BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
				_("The class is disabled"), nullptr);
			break;
		case CLASS_AVAILABILITY::NEED_MORE_PLAYERS:
		{
			int MinPlayers = GameController()->GetMinPlayersForClass(NewClass);
			GameServer()->SendBroadcast_Localization_P(GetCID(),
				BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
				MinPlayers,
				_P("Need at least {int:MinPlayers} player",
					"Need at least {int:MinPlayers} players"),
				"MinPlayers", &MinPlayers,
				nullptr);
		}
		break;
		case CLASS_AVAILABILITY::LIMIT_EXCEEDED:
			GameServer()->SendBroadcast_Localization(GetCID(),
				BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
				_("The class limit exceeded"), nullptr);
			break;
		}

		if(Availability == CLASS_AVAILABILITY::AVAILABLE)
		{
			pPlayer->m_MapMenuItem = HoveredMenuItem;
		}
		else
		{
			pPlayer->m_MapMenuItem = -1;
		}
	}

	if(pPlayer->MapMenuClickable() && m_Input.m_Fire & 1)
	{
		HandleMapMenuClicked();
	}
}

void CInfClassCharacter::HandleMapMenuClicked()
{
	bool Random = false;

	CInfClassPlayer *pPlayer = GetPlayer();
	int MenuClass = pPlayer->m_MapMenuItem;
	PLAYERCLASS NewClass = CInfClassGameController::MenuClassToPlayerClass(MenuClass);
	if(NewClass == PLAYERCLASS_RANDOM)
	{
		NewClass = GameController()->ChooseHumanClass(pPlayer);
		Random = true;
		pPlayer->SetRandomClassChoosen();
	}
	if(NewClass == PLAYERCLASS_INVALID)
	{
		return;
	}

	if(GameController()->GetPlayerClassAvailability(NewClass, pPlayer) == CLASS_AVAILABILITY::AVAILABLE)
	{
		SetAntiFire();
		pPlayer->m_MapMenuItem = 0;
		pPlayer->SetClass(NewClass);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "choose_class player='%s' class='%d' random='%d'",
			Server()->ClientName(GetCID()), NewClass, Random);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);

		if(Random)
		{
			GiveRandomClassSelectionBonus();
		}

		SetAntiFire();
		pPlayer->CloseMapMenu();
	}
}

void CInfClassCharacter::HandleWeaponsRegen()
{
	if(!m_pClass)
	{
		return;
	}

	for(int i=WEAPON_GUN; i<=WEAPON_LASER; i++)
	{
		if(m_ReloadTimer)
		{
			if(i == m_ActiveWeapon)
			{
				continue;
			}
		}

		WeaponRegenParams Params;
		m_pClass->GetAmmoRegenParams(i, &Params);

		if(Params.RegenInterval)
		{
			if (m_aWeapons[i].m_AmmoRegenStart < 0)
				m_aWeapons[i].m_AmmoRegenStart = Server()->Tick();

			if ((Server()->Tick() - m_aWeapons[i].m_AmmoRegenStart) >= Params.RegenInterval * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[i].m_Ammo = minimum(m_aWeapons[i].m_Ammo + 1, Params.MaxAmmo);
				m_aWeapons[i].m_AmmoRegenStart = -1;
			}
		}
	}
}

void CInfClassCharacter::HandleHookDraining()
{
	if(IsZombie())
	{
		if(m_Core.m_HookedPlayer >= 0)
		{
			CInfClassCharacter *VictimChar = GameController()->GetCharacter(m_Core.m_HookedPlayer);
			if(VictimChar)
			{
				float Rate = 1.0f;
				int Damage = 1;

				if(GetPlayerClass() == PLAYERCLASS_SMOKER)
				{
					Rate = 0.5f;
					Damage = g_Config.m_InfSmokerHookDamage;
				}

				if(m_HookDmgTick + Server()->TickSpeed()*Rate < Server()->Tick())
				{
					m_HookDmgTick = Server()->Tick();
					VictimChar->TakeDamage(vec2(0.0f,0.0f), Damage, GetCID(), DAMAGE_TYPE::DRYING_HOOK);
					if((GetPlayerClass() == PLAYERCLASS_SMOKER || GetPlayerClass() == PLAYERCLASS_BAT) && VictimChar->IsHuman())
					{
						Heal(2);
					}
				}
			}
		}
	}
}

void CInfClassCharacter::HandleTeleports()
{
	int Index = GameServer()->Collision()->GetPureMapIndex(m_Pos);

	CTeleTile *pTeleLayer = GameServer()->Collision()->TeleLayer();
	if(!pTeleLayer)
		return;

	int TeleNumber = pTeleLayer[Index].m_Number;
	int TeleType = pTeleLayer[Index].m_Type;
	if((TeleNumber > 0) && (TeleType != TILE_TELEOUT))
	{
		dbg_msg("InfClass", "Character TeleNumber: %d, TeleType: %d", TeleNumber, TeleType);
		TeleToId(TeleNumber, TeleType);
	}
}

void CInfClassCharacter::HandleIndirectKillerCleanup()
{
	bool CharacterControlsItsPosition = IsGrounded() || m_Core.m_HookState == HOOK_GRABBED || m_Core.m_IsPassenger;

	if(!CharacterControlsItsPosition)
	{
		return;
	}

	const float LastEnforcerTimeoutInSeconds = Config()->m_InfLastEnforcerTimeMs / 1000.0f;
	while(!m_EnforcersInfo.IsEmpty())
	{
		const EnforcerInfo &info = m_EnforcersInfo.First();
		if(Server()->Tick() > info.m_Tick + Server()->TickSpeed() * LastEnforcerTimeoutInSeconds)
		{
			m_EnforcersInfo.RemoveAt(0);
		}
		else
		{
			break;
		}
	}

	if(!m_EnforcersInfo.IsEmpty())
	{
		for(EnforcerInfo &info : m_EnforcersInfo)
		{
			if(Server()->Tick() > info.m_Tick + Server()->TickSpeed() * LastEnforcerTimeoutInSeconds)
			{
				info.m_CID = -1;
				info.m_Tick = -1;
			}
		}
	}

	if(!m_LastHookers.IsEmpty())
	{
		if(Server()->Tick() > m_LastHookerTick + Server()->TickSpeed() * LastEnforcerTimeoutInSeconds)
		{
			m_LastHookers.Clear();
			m_LastHookerTick = -1;
		}
	}

	if(m_LastFreezer >= 0)
	{
		if(!IsFrozen())
		{
			m_LastFreezer = -1;
		}
	}
}

void CInfClassCharacter::Die(int Killer, int Weapon)
{
	DAMAGE_TYPE DamageType = DAMAGE_TYPE::INVALID;
	switch(Weapon)
	{
	case WEAPON_SELF:
		DamageType = DAMAGE_TYPE::KILL_COMMAND;
		break;
	case WEAPON_GAME:
		DamageType = DAMAGE_TYPE::GAME;
		break;
	default:
		dbg_msg("infclass", "Invalid Die() event: victim=%d, killer=%d, weapon=%d", GetCID(), Killer, Weapon);
	}

	Die(Killer, DamageType);
}

void CInfClassCharacter::Die(int Killer, DAMAGE_TYPE DamageType)
{
	dbg_msg("server", "CInfClassCharacter::Die: victim: %d, killer: %d, DT: %d", GetCID(), Killer, static_cast<int>(DamageType));

	SDamageContext DamageContext;
	DamageContext.Killer = Killer;
	DamageContext.DamageType = DamageType;
	CInfClassGameController::DamageTypeToWeapon(DamageType, &DamageContext.Mode);

	DeathContext Context;
	GetDeathContext(DamageContext, &Context);

	Die(Context);
}

void CInfClassCharacter::Die(const DeathContext &Context)
{
	if(!IsAlive())
	{
		return;
	}
	
	bool RefusedToDie = false;
	PrepareToDie(Context, &RefusedToDie);

	if(RefusedToDie)
	{
		return;
	}

	DestroyChildEntities();

	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	GameController()->OnCharacterDeath(this, Context);

	// a nice sound
	GameServer()->CreateSound(GetPos(), SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameWorld()->RemoveEntity(this);
	GameWorld()->m_Core.m_apCharacters[GetCID()] = 0;
	GameServer()->CreateDeath(GetPos(), GetCID());
}

void CInfClassCharacter::SetActiveWeapon(int Weapon)
{
	m_ActiveWeapon = Weapon;
}

void CInfClassCharacter::SetLastWeapon(int Weapon)
{
	m_LastWeapon = Weapon;
}

void CInfClassCharacter::TakeAllWeapons()
{
	for (WeaponStat &weapon : m_aWeapons)
	{
		weapon.m_Got = false;
		weapon.m_Ammo = 0;
	}
}

void CInfClassCharacter::AddAmmo(int Weapon, int Ammo)
{
	INFWEAPON InfWID = GetInfWeaponID(Weapon);
	int MaxAmmo = Server()->GetMaxAmmo(InfWID);

	if(InfWID == INFWEAPON::NINJA_GRENADE)
		MaxAmmo = minimum(MaxAmmo + m_NinjaAmmoBuff, 10);

	if(Ammo <= 0)
		return;

	if(!m_aWeapons[Weapon].m_Got)
		return;

	int TargetAmmo = maximum(0, m_aWeapons[Weapon].m_Ammo) + Ammo;
	m_aWeapons[Weapon].m_Ammo = minimum(MaxAmmo, TargetAmmo);
}

int CInfClassCharacter::GetAmmo(int Weapon) const
{
	return m_aWeapons[Weapon].m_Ammo;
}

int CInfClassCharacter::GetCID() const
{
	if(m_pPlayer)
	{
		return m_pPlayer->GetCID();
	}

	return -1;
}

CInfClassPlayer *CInfClassCharacter::GetPlayer()
{
	return static_cast<CInfClassPlayer*>(m_pPlayer);
}

void CInfClassCharacter::SetClass(CInfClassPlayerClass *pClass)
{
	m_pClass = pClass;

	DestroyChildEntities();

	if(!pClass)
	{
		// Destruction. Do not care about initialization
		return;
	}

	// ex SetClass(int):
	ClassSpawnAttributes();

	m_QueuedWeapon = -1;
	m_TakenDamageDetails.Clear();

	GameServer()->CreatePlayerSpawn(GetPos());

	if(GetPlayerClass() == PLAYERCLASS_NONE)
	{
		OpenClassChooser();
	}
}

CInputCount CInfClassCharacter::CountFireInput() const
{
	return CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire);
}

bool CInfClassCharacter::FireJustPressed() const
{
	return m_LatestInput.m_Fire & 1;
}

void CInfClassCharacter::SetReloadTimer(int Ticks)
{
	m_ReloadTimer = Ticks;
}

void CInfClassCharacter::SetReloadDuration(float Seconds)
{
	m_ReloadTimer = Server()->TickSpeed() * Seconds;
}

vec2 CInfClassCharacter::GetHookPos() const
{
	return m_Core.m_HookPos;
}

int CInfClassCharacter::GetHookedPlayer() const
{
	return m_Core.m_HookedPlayer;
}

void CInfClassCharacter::SetHookedPlayer(int ClientID)
{
	m_Core.m_HookedPlayer = ClientID;

	if(ClientID >= 0)
	{
		m_Core.m_HookTick = 0;
		m_Core.m_TriggeredEvents |= COREEVENT_HOOK_ATTACH_PLAYER;
		m_Core.m_HookState = HOOK_GRABBED;

		const CInfClassCharacter *pCharacter = GameController()->GetCharacter(ClientID);
		const CCharacterCore *pCharCore = pCharacter ? &pCharacter->m_Core : nullptr;
		if(pCharCore)
		{
			m_Core.m_HookPos = pCharCore->m_Pos;
		}
	}
}

vec2 CInfClassCharacter::Velocity() const
{
	return m_Core.m_Vel;
}

float CInfClassCharacter::Speed() const
{
	return length(m_Core.m_Vel);
}

CGameContext *CInfClassCharacter::GameContext() const
{
	return m_pGameController->GameServer();
}

bool CInfClassCharacter::CanDie() const
{
	return IsAlive() && m_pClass && m_pClass->CanDie();
}

bool CInfClassCharacter::CanJump() const
{
	// 1 bit = to keep track if a jump has been made on this input
	if(m_Core.m_Jumped & 1)
		return false;
	// 2 bit = to keep track if an air-jump has been made
	if(m_Core.m_Jumped & 2)
		return false;

	return true;
}

bool CInfClassCharacter::IsInvisible() const
{
	return m_IsInvisible;
}

bool CInfClassCharacter::IsInvincible() const
{
	return m_Invincible || (m_ProtectionTick > 0);
}

void CInfClassCharacter::SetInvincible(int Invincible)
{
	m_Invincible = Invincible;
}

bool CInfClassCharacter::HasHallucination() const
{
	return m_HallucinationTick > 0;
}

void CInfClassCharacter::TryUnfreeze(int UnfreezerCID)
{
	if(!IsFrozen())
		return;

	CInfClassInfected *pInfected = CInfClassInfected::GetInstance(this);
	if(pInfected && !pInfected->CanBeUnfreezed())
	{
		return;
	}

	Unfreeze();

	if(UnfreezerCID >= 0)
	{
		const float UnfreezerHelperDuration = 10;
		AddHelper(UnfreezerCID, UnfreezerHelperDuration);
	}
}

int CInfClassCharacter::GetFreezer() const
{
	return IsFrozen() ? m_LastFreezer : -1;
}

void CInfClassCharacter::MakeBlind(int ClientID, float Duration)
{
	m_BlindnessTicks = Server()->TickSpeed() * Duration;
	m_LastBlinder = ClientID;

	GameServer()->SendEmoticon(GetCID(), EMOTICON_QUESTION);
}

float CInfClassCharacter::WebHookLength() const
{
	if((GetEffectiveHookMode() != 1) && !g_Config.m_InfSpiderCatchHumans)
		return 0;

	if(m_Core.m_HookState != HOOK_GRABBED)
		return 0;

	return distance(m_Core.m_Pos, m_Core.m_HookPos);
}

void CInfClassCharacter::GiveGift(int GiftType)
{
	IncreaseHealth(1);
	GiveArmor(4);

	const auto AllWeaponsWithAmmo =
	{
		WEAPON_GUN,
		WEAPON_SHOTGUN,
		WEAPON_GRENADE,
		WEAPON_LASER,
	};

	for(int WeaponSlot : AllWeaponsWithAmmo)
	{
		if(m_aWeapons[WeaponSlot].m_Got)
		{
			GiveWeapon(WeaponSlot, -1);
		}
	}
}

void CInfClassCharacter::GiveRandomClassSelectionBonus()
{
	IncreaseArmor(10);
}

void CInfClassCharacter::MakeVisible()
{
	if(m_IsInvisible)
	{
		GameServer()->CreatePlayerSpawn(m_Pos);
		m_IsInvisible = false;
	}

	m_InvisibleTick = Server()->Tick();
}

void CInfClassCharacter::GrantSpawnProtection()
{
	// Indicate time left being protected via eyes
	if(m_ProtectionTick <= 0)
	{
		m_ProtectionTick = Server()->TickSpeed() * g_Config.m_InfSpawnProtectionTime / 1000;
		if(!IsFrozen() && !IsInvisible())
		{
			SetEmote(EMOTE_SURPRISE, Server()->Tick() + m_ProtectionTick);
		}
	}
}

void CInfClassCharacter::PreCoreTick()
{
	--m_FrozenTime;
	if(m_IsFrozen)
	{
		if(m_FrozenTime <= 0)
		{
			Unfreeze();
		}
		else
		{
			int FreezeSec = 1 + (m_FrozenTime / Server()->TickSpeed());
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE, BROADCAST_DURATION_REALTIME, _("You are frozen: {sec:EffectDuration}"), "EffectDuration", &FreezeSec, NULL);
		}
	}

	if(m_SlowMotionTick > 0)
	{
		--m_SlowMotionTick;

		if(m_SlowMotionTick > 0)
		{
			int SloMoSec = 1 + (m_SlowMotionTick / Server()->TickSpeed());
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
	}

	// Ghost
	if(GetPlayerClass() == PLAYERCLASS_GHOST)
	{
		if(Server()->Tick() < m_InvisibleTick + 3 * Server()->TickSpeed() || IsFrozen() || IsInSlowMotion())
		{
			m_IsInvisible = false;
		}
		else
		{
			// Search nearest human
			int cellGhostX = static_cast<int>(round(GetPos().x)) / 32;
			int cellGhostY = static_cast<int>(round(GetPos().y)) / 32;

			vec2 SeedPos = vec2(16.0f, 16.0f) + vec2(
													static_cast<float>(static_cast<int>(round(GetPos().x)) / 32) * 32.0,
													static_cast<float>(static_cast<int>(round(GetPos().y)) / 32) * 32.0);

			for(int y = 0; y < GHOST_SEARCHMAP_SIZE; y++)
			{
				for(int x = 0; x < GHOST_SEARCHMAP_SIZE; x++)
				{
					vec2 Tile = SeedPos + vec2(32.0f * (x - GHOST_RADIUS), 32.0f * (y - GHOST_RADIUS));
					if(GameServer()->Collision()->CheckPoint(Tile))
					{
						m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] = 0x8;
					}
					else
					{
						m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] = 0x0;
					}
				}
			}
			for(CCharacter *p = (CCharacter *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CCharacter *)p->TypeNext())
			{
				if(p->IsZombie())
					continue;

				int cellHumanX = static_cast<int>(round(p->GetPos().x)) / 32;
				int cellHumanY = static_cast<int>(round(p->GetPos().y)) / 32;

				int cellX = cellHumanX - cellGhostX + GHOST_RADIUS;
				int cellY = cellHumanY - cellGhostY + GHOST_RADIUS;

				if(cellX >= 0 && cellX < GHOST_SEARCHMAP_SIZE && cellY >= 0 && cellY < GHOST_SEARCHMAP_SIZE)
				{
					m_GhostSearchMap[cellY * GHOST_SEARCHMAP_SIZE + cellX] |= 0x2;
				}
			}
			m_GhostSearchMap[GHOST_RADIUS * GHOST_SEARCHMAP_SIZE + GHOST_RADIUS] |= 0x1;
			bool HumanFound = false;
			for(int i = 0; i < GHOST_RADIUS; i++)
			{
				for(int y = 0; y < GHOST_SEARCHMAP_SIZE; y++)
				{
					for(int x = 0; x < GHOST_SEARCHMAP_SIZE; x++)
					{
						if(!((m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] & 0x1) || (m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] & 0x8)))
						{
							if(
								(
									(x > 0 && (m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x - 1] & 0x1)) ||
									(x < GHOST_SEARCHMAP_SIZE - 1 && (m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x + 1] & 0x1)) ||
									(y > 0 && (m_GhostSearchMap[(y - 1) * GHOST_SEARCHMAP_SIZE + x] & 0x1)) ||
									(y < GHOST_SEARCHMAP_SIZE - 1 && (m_GhostSearchMap[(y + 1) * GHOST_SEARCHMAP_SIZE + x] & 0x1))) ||
								((random_prob(0.25f)) && ((x > 0 && y > 0 && (m_GhostSearchMap[(y - 1) * GHOST_SEARCHMAP_SIZE + x - 1] & 0x1)) ||
															 (x > 0 && y < GHOST_SEARCHMAP_SIZE - 1 && (m_GhostSearchMap[(y + 1) * GHOST_SEARCHMAP_SIZE + x - 1] & 0x1)) ||
															 (x < GHOST_SEARCHMAP_SIZE - 1 && y > 0 && (m_GhostSearchMap[(y - 1) * GHOST_SEARCHMAP_SIZE + x + 1] & 0x1)) ||
															 (x < GHOST_SEARCHMAP_SIZE - 1 && y < GHOST_SEARCHMAP_SIZE - 1 && (m_GhostSearchMap[(y + 1) * GHOST_SEARCHMAP_SIZE + x + 1] & 0x1)))))
							{
								m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] |= 0x4;
								//~ if((Server()->Tick()%5 == 0) && i == (Server()->Tick()/5)%GHOST_RADIUS)
								//~ {
								//~ vec2 HintPos = vec2(
								//~ 32.0f*(cellGhostX + (x - GHOST_RADIUS))+16.0f,
								//~ 32.0f*(cellGhostY + (y - GHOST_RADIUS))+16.0f);
								//~ GameServer()->CreateHammerHit(HintPos);
								//~ }
								if(m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] & 0x2)
								{
									HumanFound = true;
								}
							}
						}
					}
				}
				for(int y = 0; y < GHOST_SEARCHMAP_SIZE; y++)
				{
					for(int x = 0; x < GHOST_SEARCHMAP_SIZE; x++)
					{
						if(m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] & 0x4)
						{
							m_GhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] |= 0x1;
						}
					}
				}
			}

			if(HumanFound)
			{
				MakeVisible();
			}
			else
			{
				m_IsInvisible = true;
			}
		}
	}

	if(!m_InWater && !IsGrounded() && (m_Core.m_HookState != HOOK_GRABBED || m_Core.m_HookedPlayer != -1))
	{
		m_InAirTick++;
	}
	else
	{
		m_InAirTick = 0;
	}

	if(m_pClass)
		m_pClass->OnCharacterPreCoreTick();

	if(IsFrozen())
	{
		if(m_FrozenTime % Server()->TickSpeed() == Server()->TickSpeed() - 1)
		{
			int FreezeSec = 1+(m_FrozenTime/Server()->TickSpeed());
			GameServer()->CreateDamageInd(m_Pos, 0, FreezeSec);
		}

		ResetMovementsInput();
		ResetHookInput();
	}

	UpdateTuningParam();

	if(HasPassenger())
	{
		const bool SameTeam = m_Core.m_Passenger->m_Infected == m_Core.m_Infected;
		if(SameTeam && (m_Core.m_HookProtected || m_Core.m_Passenger->m_HookProtected))
		{
			m_Core.SetPassenger(nullptr);
		}
	}
}

void CInfClassCharacter::PostCoreTick()
{
	CCharacter::PostCoreTick();

	if(GetPlayer()->MapMenu() == 1)
	{
		HandleMapMenu();
	}

	if(m_Core.m_TriggeredEvents & COREEVENT_HOOK_ATTACH_PLAYER)
	{
		if(m_pClass)
		{
			m_pClass->OnHookAttachedPlayer();
		}
	}

	HandleWeaponsRegen();
	HandleHookDraining();
	HandleIndirectKillerCleanup();
	HandleTeleports();

	// Handle the pain
	if(m_PainSoundTimer > 0)
	{
		m_PainSoundTimer--;
	}
}

void CInfClassCharacter::SnapCharacter(int SnappingClient, int ID)
{
	CCharacterCore *pCore;
	int Tick, Weapon = m_ActiveWeapon;

	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		Tick = 0;
		pCore = &m_Core;
	}
	else
	{
		Tick = m_ReckoningTick;
		pCore = &m_SendCore;
	}

	int EmoteNormal = m_pPlayer->GetDefaultEmote();

	CNetObj_Character *pCharacter = Server()->SnapNewItem<CNetObj_Character>(ID);
	if(!pCharacter)
		return;
	pCharacter->m_Tick = Tick;
	pCore->Write(pCharacter);
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
	if(GetInfWeaponID(m_ActiveWeapon) == INFWEAPON::NINJA_HAMMER)
	{
		Weapon = WEAPON_NINJA;
	}

	if(PrivateGetPlayerClass() == PLAYERCLASS_SPIDER)
	{
		pCharacter->m_HookTick -= (g_Config.m_InfSpiderHookTime - 1) * SERVER_TICK_SPEED - SERVER_TICK_SPEED / 5;
		if(pCharacter->m_HookTick < 0)
			pCharacter->m_HookTick = 0;
	}
	if(PrivateGetPlayerClass() == PLAYERCLASS_BAT)
	{
		pCharacter->m_HookTick -= (g_Config.m_InfBatHookTime - 1) * SERVER_TICK_SPEED - SERVER_TICK_SPEED / 5;
		if(pCharacter->m_HookTick < 0)
			pCharacter->m_HookTick = 0;
	}
	/* INFECTION MODIFICATION END *****************************************/
	pCharacter->m_AttackTick = m_AttackTick;
	pCharacter->m_Direction = m_Input.m_Direction;
	pCharacter->m_Weapon = Weapon;

	const CInfClassPlayer *pSnappingClient = GameController()->GetPlayer(SnappingClient);
	int SnappingSpectatorID = -1;
	if(pSnappingClient)
	{
		SnappingSpectatorID = pSnappingClient->m_SpectatorID;
		int FollowingCID = pSnappingClient->TargetToFollow();
		if(FollowingCID >= 0)
		{
			SnappingSpectatorID = FollowingCID;
		}
	}

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == SERVER_DEMO_CLIENT ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == SnappingSpectatorID))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = clamp<int>(m_Armor, 0, 10);
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) == INFWEAPON::MERCENARY_GUN)
	{
		pCharacter->m_AmmoCount /= (Server()->GetMaxAmmo(INFWEAPON::MERCENARY_GUN) / 10);
	}
	/* INFECTION MODIFICATION END *****************************************/

	if(pCharacter->m_Emote == EmoteNormal)
	{
		if(250 - ((Server()->Tick() - m_LastAction) % (250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}

void CInfClassCharacter::ClassSpawnAttributes()
{
	int Armor = m_Armor;
	m_IsInvisible = false;

	const PLAYERCLASS PlayerClass = GetPlayerClass();
	const bool isHuman = PlayerClass < END_HUMANCLASS; // PLAYERCLASS_NONE is also a human (not infected) class
	if(isHuman)
	{
		m_pPlayer->m_InfectionTick = -1;
	}
	else
	{
		Armor = 0;
	}

	SetHealthArmor(10, Armor);
}

void CInfClassCharacter::DestroyChildEntities()
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

	m_HookMode = 0;
}

void CInfClassCharacter::UpdateTuningParam()
{
	CTuningParams* pTuningParams = &m_pPlayer->m_NextTuningParams;
	
	bool NoHook = false;
	bool NoHookAcceleration = false;
	bool NoControls = false;
	bool NoGravity = false;
	
	if(PositionIsLocked())
	{
		NoControls = true;
		NoGravity = true;
		NoHookAcceleration = true;
	}
	if(m_IsFrozen)
	{
		NoHook = true;
		NoControls = true;
	}

	if(m_Core.m_IsPassenger)
	{
		NoHook = true;
		NoControls = true;
		NoGravity = true;
	}
	
	if(m_SlowMotionTick > 0)
	{
		float Factor = 1.0f - ((float)g_Config.m_InfSlowMotionPercent / 100);
		float FactorSpeed = 1.0f - ((float)g_Config.m_InfSlowMotionHookSpeed / 100);
		float FactorAccel = 1.0f - ((float)g_Config.m_InfSlowMotionHookAccel / 100);
		pTuningParams->m_GroundControlSpeed = pTuningParams->m_GroundControlSpeed * Factor;
		pTuningParams->m_HookFireSpeed = pTuningParams->m_HookFireSpeed * FactorSpeed;
		//pTuningParams->m_GroundJumpImpulse = pTuningParams->m_GroundJumpImpulse * Factor;
		//pTuningParams->m_AirJumpImpulse = pTuningParams->m_AirJumpImpulse * Factor;
		pTuningParams->m_AirControlSpeed = pTuningParams->m_AirControlSpeed * Factor;
		pTuningParams->m_HookDragAccel = pTuningParams->m_HookDragAccel * FactorAccel;
		pTuningParams->m_HookDragSpeed = pTuningParams->m_HookDragSpeed * FactorSpeed;
		pTuningParams->m_Gravity = g_Config.m_InfSlowMotionGravity * 0.01f;

		if(g_Config.m_InfSlowMotionMaxSpeed > 0)
		{
			float MaxSpeed = g_Config.m_InfSlowMotionMaxSpeed * 0.1f;
			float diff = MaxSpeed / length(m_Core.m_Vel);
			if (diff < 1.0f) m_Core.m_Vel *= diff;
		}
	}
	
	if(GetEffectiveHookMode() == 1)
	{
		pTuningParams->m_HookDragSpeed = 0.0f;
		pTuningParams->m_HookDragAccel = 1.0f;
	}
	if(m_InWater == 1)
	{
		pTuningParams->m_Gravity = -0.05f;
		pTuningParams->m_GroundFriction = 0.95f;
		pTuningParams->m_GroundControlSpeed = 250.0f / Server()->TickSpeed();
		pTuningParams->m_GroundControlAccel = 1.5f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirFriction = 0.95f;
		pTuningParams->m_AirControlSpeed = 250.0f / Server()->TickSpeed();
		pTuningParams->m_AirControlAccel = 1.5f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
	}
	if(m_SlipperyTick > 0)
	{
		pTuningParams->m_GroundFriction = 1.0f;
	}

	if(NoHook)
	{
		pTuningParams->m_HookLength = 0.0f;
	}

	if(NoHookAcceleration)
	{
		pTuningParams->m_HookDragSpeed = 0.0f;
		pTuningParams->m_HookDragAccel = 0.0f;
	}

	if(NoControls)
	{
		pTuningParams->m_GroundControlAccel = 0.0f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
		pTuningParams->m_AirControlAccel = 0.0f;
	}
	if(NoGravity)
	{
		pTuningParams->m_Gravity = 0.0f;
	}
	if(GetPlayer()->HookProtectionEnabled())
	{
		pTuningParams->m_PlayerHooking = 0;
	}
	
	if(GetPlayerClass() == PLAYERCLASS_GHOUL)
	{
		float Factor = GetClass()->GetGhoulPercent() * 0.7;
		pTuningParams->m_GroundControlSpeed = pTuningParams->m_GroundControlSpeed * (1.0f + 0.35f * Factor);
		pTuningParams->m_GroundControlAccel = pTuningParams->m_GroundControlAccel * (1.0f + 0.35f * Factor);
		pTuningParams->m_GroundJumpImpulse = pTuningParams->m_GroundJumpImpulse * (1.0f + 0.35f * Factor);
		pTuningParams->m_AirJumpImpulse = pTuningParams->m_AirJumpImpulse * (1.0f + 0.35f * Factor);
		pTuningParams->m_AirControlSpeed = pTuningParams->m_AirControlSpeed * (1.0f + 0.35f * Factor);
		pTuningParams->m_AirControlAccel = pTuningParams->m_AirControlAccel * (1.0f + 0.35f * Factor);
		pTuningParams->m_HookDragAccel = pTuningParams->m_HookDragAccel * (1.0f + 0.35f * Factor);
		pTuningParams->m_HookDragSpeed = pTuningParams->m_HookDragSpeed * (1.0f + 0.35f * Factor);
	}
}

void CInfClassCharacter::TeleToId(int TeleNumber, int TeleType)
{
	const std::map<int, std::vector<vec2>> &AllTeleOuts = GameServer()->Collision()->GetTeleOuts();
	if(AllTeleOuts.find(TeleNumber) == AllTeleOuts.cend())
	{
		dbg_msg("InfClass", "No tele out for tele number: %d", TeleNumber);
		return;
	}
	const std::vector<vec2> Outs = AllTeleOuts.at(TeleNumber);
	if(Outs.empty())
	{
		dbg_msg("InfClass", "No tele out for tele number: %d", TeleNumber);
		return;
	}

	switch(TeleType)
	{
		case TILE_TELEINEVIL:
		case TILE_TELEIN:
			break;
		default:
			dbg_msg("InfClass", "Unsupported tele type: %d", TeleType);
			return;
	}

	int DestTeleNumber = random_int(0, Outs.size() - 1);
	vec2 DestPosition = Outs.at(DestTeleNumber);
	m_Core.m_Pos = DestPosition;
	if(TeleType == TILE_TELEINEVIL)
	{
		m_Core.m_Vel = vec2(0, 0);
		GameWorld()->ReleaseHooked(GetPlayer()->GetCID());
	}

	m_Core.m_HookedPlayer = -1;
	m_Core.m_HookState = HOOK_RETRACTED;
	m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
	m_Core.m_HookPos = m_Core.m_Pos;
}
