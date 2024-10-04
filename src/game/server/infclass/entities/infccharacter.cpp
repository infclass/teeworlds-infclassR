#include "infccharacter.h"
#include "engine/server.h"
#include "game/infclass/classes.h"
#include "game/server/entities/character.h"

#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/collision.h>

#include <game/generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>

#include <game/server/entities/projectile.h>

#include <game/server/infclass/damage_context.h>
#include <game/infclass/damage_type.h>
#include <game/server/infclass/death_context.h>
#include <game/server/infclass/entities/plasma.h>
#include <game/server/infclass/entities/superweapon-indicator.h>
#include <game/server/infclass/entities/white-hole.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassCharacter, MAX_CLIENTS)

static bool HumansEntitiesFilter(const CEntity *pEntity)
{
	const CInfClassCharacter *pInfEntity = static_cast<const CInfClassCharacter *>(pEntity);
	return pInfEntity->IsHuman();
}

static bool InfectedEntitiesFilter(const CEntity *pEntity)
{
	const CInfClassCharacter *pInfEntity = static_cast<const CInfClassCharacter *>(pEntity);
	return !pInfEntity->IsHuman();
}

CInfClassCharacter::CInfClassCharacter(CInfClassGameController *pGameController) :
	CCharacter(pGameController->GameWorld()), m_pGameController(pGameController)
{
	m_FlagId = Server()->SnapNewId();
	m_HeartId = Server()->SnapNewId();
	m_CursorId = Server()->SnapNewId();
}

CInfClassCharacter::~CInfClassCharacter()
{
	FreeChildSnapIds();
	ResetClassObject();
}

EntityFilter CInfClassCharacter::GetInfectedFilterFunction()
{
	return InfectedEntitiesFilter;
}

EntityFilter CInfClassCharacter::GetHumansFilterFunction()
{
	return HumansEntitiesFilter;
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
	m_NeededFaketuning = FAKETUNE_NOCOLL;

	SetAntiFire();
	m_IsFrozen = false;
	m_FrozenTime = -1;
	m_LoveTick = -1;
	m_FrozenTime = -1;
	m_FreezeReason = FREEZEREASON_FLASH;
	m_Poison = 0;
	m_SlowMotionTick = -1;
	m_HallucinationTick = -1;
	m_SlipperyTick = -1;
	m_LastFreezer = -1;
	m_DropLevel = 0;

	ResetHelpers();
	m_LastHookers.Clear();
	m_LastHookerTick = -1;
	m_EnforcersInfo.Clear();

	m_DamageZoneTick = -1;
	m_DamageZoneDealtDamage = 0;

	m_Invincible = 0;

	ClassSpawnAttributes();

	if(GetPlayerClass() == EPlayerClass::None)
	{
		OpenClassChooser();
	}

	m_pClass->OnCharacterSpawned(Context);

	GameController()->OnCharacterSpawned(this, Context);
}

void CInfClassCharacter::OnCharacterInInfectionZone()
{
	if(IsInfected())
	{
		if(Server()->Tick() >= m_HealTick + (Server()->TickSpeed()/g_Config.m_InfInfzoneHealRate))
		{
			m_HealTick = Server()->Tick();
			int BonusArmor = GameController()->InfectedBonusArmor();
			if(m_Health < 10 || m_Armor < BonusArmor)
			{
				Heal(1);
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
		DamageContext.Killer = GetCid();
		DamageContext.DamageType = EDamageType::INFECTION_TILE;
		DamageContext.Mode = TAKEDAMAGEMODE::INFECTION;

		GetDeathContext(DamageContext, &Context);

		Die(Context);
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

void CInfClassCharacter::OnCharacterInDamageZone(float Damage, float DamageInterval)
{
	constexpr EDamageType DamageType = EDamageType::DAMAGE_TILE;

	const int Tick = Server()->Tick();

	if(m_DamageZoneTick < 0 || (Tick >= (m_DamageZoneTick + Server()->TickSpeed() * DamageInterval)))
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

void CInfClassCharacter::TickBeforeWorld()
{
	m_Core.m_Infected = IsInfected();
	m_Core.m_InLove = IsInLove();
	m_Core.m_HookProtected = GetPlayer()->HookProtectionEnabled();
}

void CInfClassCharacter::Tick()
{
	if(!m_pClass)
	{
		// Sometimes m_pClass is still nullptr on the very first tick
		// of a new round when the Reset is not complete yet.
		return;
	}

	const vec2 PrevPos = m_Core.m_Pos;
	const int CurrentTick = Server()->Tick();
	GameController()->HandleCharacterTiles(this);

	CCharacter::Tick();

	if(m_BlindnessTicks > 0)
	{
		--m_BlindnessTicks;
		int EffectSec = 1 + (m_BlindnessTicks / Server()->TickSpeed());
		GameServer()->SendBroadcast_Localization(m_pPlayer->GetCid(), EBroadcastPriority::EFFECTSTATE, BROADCAST_DURATION_REALTIME,
			_("You are blinded: {sec:EffectDuration}"),
			"EffectDuration", &EffectSec,
			nullptr);
	}

	if(m_Poison > 0)
	{
		if(m_PoisonTick <= CurrentTick)
		{
			m_Poison--;
			vec2 Force(0, 0);
			static const int PoisonDamage = 1;
			TakeDamage(Force, PoisonDamage, m_PoisonFrom, m_PoisonDamageType);
			m_PoisonTick = CurrentTick + Server()->TickSpeed() * m_PoisonEffectInterval;

			if(m_PoisonDamageType == EDamageType::SLUG_SLIME)
			{
				GameServer()->CreateDeath(GetPos(), m_PoisonFrom);
			}
		}
	}

	if(m_LastHelper.m_Tick > 0)
	{
		--m_LastHelper.m_Tick;
	}

	m_pClass->OnCharacterTick();
}

void CInfClassCharacter::TickDeferred()
{
	int Events = m_Core.m_TriggeredEvents;

	CCharacter::TickDeferred();

	if(Events & COREEVENT_AIR_JUMP)
	{
		const int64_t MaskOnlyBlind = GameController()->GetBlindCharactersMask(GetCid());
		if(MaskOnlyBlind)
		{
			GameServer()->CreateSound(GetPos(), SOUND_PLAYER_AIRJUMP, MaskOnlyBlind);
		}
	}

	// Ghost events
	int64_t MaskEsceptSelf = CmaskAllExceptOne(m_pPlayer->GetCid());

	if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
		GameServer()->CreateSound(GetPos(), SOUND_HOOK_ATTACH_PLAYER, CmaskAll());

	if(GetPlayerClass() != EPlayerClass::Ghost || !m_IsInvisible)
	{
		if(Events & COREEVENT_GROUND_JUMP)
			GameServer()->CreateSound(GetPos(), SOUND_PLAYER_JUMP, MaskEsceptSelf);
		if(Events & COREEVENT_HOOK_ATTACH_GROUND)
			GameServer()->CreateSound(GetPos(), SOUND_HOOK_ATTACH_GROUND, MaskEsceptSelf);
		if(Events & COREEVENT_HOOK_HIT_NOHOOK)
			GameServer()->CreateSound(GetPos(), SOUND_HOOK_NOATTACH, MaskEsceptSelf);
	}

	if(m_pClass)
	{
		m_pClass->OnCharacterTickDeferred();
	}
}

void CInfClassCharacter::TickPaused()
{
	if(m_pClass)
		m_pClass->OnCharacterTickPaused();

	if(m_DamageZoneTick != -1)
	{
		m_DamageZoneTick++;
	}
	if(m_PoisonTick)
		m_PoisonTick++;
}

void CInfClassCharacter::Snap(int SnappingClient)
{
	int Id = GetCid();

	if(!Server()->Translate(Id, SnappingClient))
		return;

	if(!CanSnapCharacter(SnappingClient))
	{
		return;
	}

	if(!IsSnappingCharacterInView(SnappingClient))
		return;

	bool DoSnap = true;
	SpecialSnapForClient(SnappingClient, &DoSnap);

	if(!DoSnap)
		return;

	if(m_pClass)
	{
		m_pClass->OnCharacterSnap(SnappingClient);
	}

	SnapCharacter(SnappingClient, Id);

	CNetObj_DDNetCharacter *pDDNetCharacter = Server()->SnapNewItem<CNetObj_DDNetCharacter>(Id);
	if(!pDDNetCharacter)
		return;

	pDDNetCharacter->m_Flags = 0;

	if(GetPlayerClass() == EPlayerClass::Mercenary)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_JETPACK;

	if(GetPlayerClass() == EPlayerClass::Scientist)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_TELEGUN_GRENADE;

	if(GetPlayerClass() != EPlayerClass::Boomer)
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

	if(IsInLove())
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_HAMMER_HIT_DISABLED;

	pDDNetCharacter->m_Jumps = m_Core.m_Jumps;
	pDDNetCharacter->m_JumpedTotal = m_Core.m_JumpedTotal;

	if(m_Core.m_Jumps > 100)
	{
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_ENDLESS_JUMP;
	}

	// Send freeze info only to the version that can handle it correctly
	if(IsFrozen())
	{
		IServer::CClientInfo ClientInfo = {0};
		if(SnappingClient != SERVER_DEMO_CLIENT)
		{
			Server()->GetClientInfo(SnappingClient, &ClientInfo);
		}

		if((ClientInfo.m_InfClassVersion > VERSION_INFC_150) || (ClientInfo.m_DDNetVersion >= 17030))
		{
			pDDNetCharacter->m_FreezeStart = m_Core.m_FreezeStart;
			pDDNetCharacter->m_FreezeEnd = Server()->Tick() + m_FrozenTime;
		}
	}

	if(Config()->m_InfTrainingMode)
	{
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_PRACTICE_MODE;
	}
	pDDNetCharacter->m_TargetX = m_Core.m_Input.m_TargetX;
	pDDNetCharacter->m_TargetY = m_Core.m_Input.m_TargetY;
}

void CInfClassCharacter::SpecialSnapForClient(int SnappingClient, bool *pDoSnap)
{
	CInfClassPlayer *pDestClient = GameController()->GetPlayer(SnappingClient);
	CInfClassCharacter *pDestCharacter = GameController()->GetCharacter(SnappingClient);
	if((GetCid() != SnappingClient) && pDestCharacter && pDestCharacter->IsBlind())
	{
		*pDoSnap = false;
		return;
	}

	if(IsInvisible() && !GameController()->CanSeeDetails(SnappingClient, GetCid()))
	{
		*pDoSnap = false;
		return;
	}
}

void CInfClassCharacter::HandleNinja()
{
	if(IsFrozen())
		return;

	if(!m_pClass)
		return;

	m_pClass->HandleNinja();
}

void CInfClassCharacter::HandleNinjaMove(float NinjaVelocity)
{
	SetVelocity(m_DartDir * NinjaVelocity);

	const vec2 GroundElasticity{};
	Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(GetProximityRadius(), GetProximityRadius()), GroundElasticity);

	// reset velocity so the client doesn't predict stuff
	ResetVelocity();
}

void CInfClassCharacter::HandleWeaponSwitch()
{
	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(GetPlayerClass() == EPlayerClass::Spider)
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

	if((GetPlayerClass() == EPlayerClass::None) || !GetClass())
		return;

	DoWeaponSwitch();

	bool FullAuto = false;

	if(m_ActiveWeapon == WEAPON_GUN || m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_LASER)
		FullAuto = true;

	if(GetPlayerClass() == EPlayerClass::Slug && m_ActiveWeapon == WEAPON_HAMMER)
		FullAuto = true;

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;
	else if(FullAuto && (m_LatestInput.m_Fire&1) && (m_aWeapons[m_ActiveWeapon].m_Ammo || (GetInfWeaponId(m_ActiveWeapon) == EInfclassWeapon::POISON_GRENADE)
																					   || (GetInfWeaponId(m_ActiveWeapon) == EInfclassWeapon::HEALING_GRENADE)))
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

	const EInfclassWeapon InfWeaponId = GetInfWeaponId(m_ActiveWeapon);

	WeaponFireContext FireContext;
	FireContext.Weapon = m_ActiveWeapon;
	FireContext.FireAccepted = true;
	FireContext.AmmoConsumed = 1;
	FireContext.AmmoAvailable = m_aWeapons[m_ActiveWeapon].m_Ammo;
	FireContext.NoAmmo = FireContext.AmmoAvailable == 0;
	FireContext.ReloadInterval = GameController()->GetFireDelay(InfWeaponId) / 1000.0f;

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

bool CInfClassCharacter::TakeDamage(const vec2 &Force, float FloatDmg, int From, EDamageType DamageType, float *pDamagePointsLeft)
{
	float TakenPlaceholder{};
	float &DamageLeft = pDamagePointsLeft ? *pDamagePointsLeft : TakenPlaceholder;
	DamageLeft = 0;

	SDamageContext DamageContext;
	{
		DamageContext.Killer = From;
		DamageContext.DamageType = DamageType;
		DamageContext.Force = Force;

		int Dmg = FloatDmg;
		if(FloatDmg != Dmg)
		{
			int ExtraDmg = random_prob(FloatDmg - Dmg) ? 1 : 0;
			Dmg += ExtraDmg;
		}
		DamageContext.Damage = Dmg;
		DamageContext.Weapon = CInfClassGameController::DamageTypeToWeapon(DamageType, &DamageContext.Mode);
	}

	const int &Weapon = DamageContext.Weapon;
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
		if(!pKillerPlayer || !pKillerPlayer->IsInfected() || !IsHuman())
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

	if(Mode == TAKEDAMAGEMODE::INFECTION)
	{
		if(GameController()->GetRoundType() == ERoundType::Survival)
		{
			Mode = TAKEDAMAGEMODE::NOINFECTION;
		}
	}

	if(m_Invincible >= 2)
	{
		Mode = TAKEDAMAGEMODE::NOINFECTION;
	}

	GetClass()->OnCharacterDamage(&DamageContext);

	const bool DmgFromHuman = pKillerPlayer && pKillerPlayer->IsHuman();
	if(DmgFromHuman && (GetPlayerClass() == EPlayerClass::Soldier) && (Weapon == WEAPON_HAMMER))
	{
		// Soldier is immune to any traps force
		DamageContext.Force = vec2(0, 0);
	}

	if((From >= 0) && (From != GetCid()) && (DamageContext.Force.x || DamageContext.Force.y))
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

	if(From != GetCid() && pKillerPlayer)
	{
		if(IsInfected())
		{
			if(pKillerPlayer->IsInfected())
			{
				return false;
			}
		}
		else
		{
			//If the player is a new infected, don't infected other -> nobody knows that he is infected.
			if(!pKillerPlayer->IsInfected() || (Server()->Tick() - pKillerPlayer->GetInfectionTick()) < Server()->TickSpeed() * 0.5)
			{
				return false;
			}
		}
	}

/* INFECTION MODIFICATION END *****************************************/

	// m_pPlayer only inflicts half damage on self
	if(From == GetCid())
	{
		if(Mode == TAKEDAMAGEMODE::ALLOW_SELFHARM)
			Dmg = Dmg ? maximum(1, Dmg / 2) : 0;
		else
			return false;
	}

	if(m_Health <= 0)
	{
		DamageLeft = FloatDmg;
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

		bool IgnoreArmor = false;
		if(DamageType == EDamageType::SLUG_SLIME)
			IgnoreArmor = true;

		int Armor = GetArmor();
		if(!IgnoreArmor)
		{
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
		}

		int Health = GetHealth() - Dmg;
		if(Health < 0)
		{
			DamageLeft = Dmg - GetHealth();
		}
		SetHealthArmor(Health, Armor);

		if(From >= 0 && From != GetCid())
			GameServer()->SendHitSound(From);
	}
/* INFECTION MODIFICATION END *****************************************/

	m_DamageTakenTick = Server()->Tick();

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
			Server()->ClientName(GetCid()), Weapon);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

		DeathContext Context;
		GetDeathContext(DamageContext, &Context);

		GameController()->SendKillMessage(GetCid(), Context);
	}
/* INFECTION MODIFICATION END *****************************************/

	if(Dmg > 2)
		GameServer()->CreateSound(GetPos(), SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(GetPos(), SOUND_PLAYER_PAIN_SHORT);

	SetEmote(EMOTE_PAIN, Server()->Tick() + 500 * Server()->TickSpeed() / 1000);

	return true;
}

bool CInfClassCharacter::Heal(int HitPoints, std::optional<int> FromCid)
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
		GameContext()->CreateSound(GetPos(), Sound, CmaskOne(GetCid()));

		if(FromCid.has_value())
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCid.value(), HealerHelperDuration);
		}
	}

	return Healed;
}

bool CInfClassCharacter::GiveHealth(int HitPoints, std::optional<int> FromCid)
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
		GameContext()->CreateSound(GetPos(), Sound, CmaskOne(GetCid()));

		if(FromCid.has_value())
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCid.value(), HealerHelperDuration);
		}
	}

	return Healed;
}

bool CInfClassCharacter::GiveArmor(int HitPoints, std::optional<int> FromCid)
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
		GameContext()->CreateSound(GetPos(), Sound, CmaskOne(GetCid()));

		if(FromCid.has_value())
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCid.value(), HealerHelperDuration);
		}
	}

	return Armored;
}

void CInfClassCharacter::SetJumpsLimit(int Limit)
{
	m_Core.m_Jumps = Limit;
}

EPlayerClass CInfClassCharacter::GetPlayerClass() const
{
	if(!m_pPlayer)
		return EPlayerClass::None;
	else
		return m_pPlayer->GetClass();
}

void CInfClassCharacter::SetDropLevel(int Level)
{
	m_DropLevel = Level;
}

void CInfClassCharacter::HandleDamage(int From, int Damage, EDamageType DamageType)
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
	case EDamageType::DEATH_TILE:
		if(m_Invincible >= 3)
		{
			*pRefusedToDie = true;
			return;
		}
		else
		{
			return;
		}
	case EDamageType::GAME:
	case EDamageType::KILL_COMMAND:
	case EDamageType::GAME_FINAL_EXPLOSION:
		// Accept the death to go with the default self kill routine
		return;
	default:
		break;
	}

	if(Context.Killer == GetCid())
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

void CInfClassCharacter::CancelLoveEffect()
{
	m_LoveTick = -1;
}

bool CInfClassCharacter::IsInSlowMotion() const
{
	return m_SlowMotionTick > 0;
}

float CInfClassCharacter::SlowMotionEffect(float Duration, std::optional<int> FromCid)
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
	m_SlowEffectApplicant = FromCid;

	return AddedDuration;
}

void CInfClassCharacter::CancelSlowMotion()
{
	m_SlowMotionTick = -1;
}

bool CInfClassCharacter::IsPoisoned() const
{
	return m_Poison > 0;
}

void CInfClassCharacter::Poison(int Count, int From, EDamageType DamageType, float Interval)
{
	bool JustPoisoned = m_PoisonTick > Server()->Tick();
	if(Count > m_Poison + JustPoisoned ? 1 : 0)
	{
		m_Poison = Count;
		m_PoisonEffectInterval = Interval;
		m_PoisonFrom = From;
		m_PoisonDamageType = DamageType;
	}
}

void CInfClassCharacter::ResetPoisonEffect()
{
	m_Poison = 0;
	// Do not reset m_PoisonTick here to prevent extra poisoning
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

void CInfClassCharacter::AddHelper(int HelperCid, float Time)
{
	if(HelperCid == GetCid())
		return;

	if(HelperCid < 0)
		return;

	int HelpTicks = Server()->TickSpeed() * Time;
	const int NewHelpPriority = 12;
	if(m_LastHelper.m_Tick > (HelpTicks + Server()->TickSpeed() * NewHelpPriority))
	{
		// Keep the previous helper
		return;
	}

	m_LastHelper.m_Cid = HelperCid;
	m_LastHelper.m_Tick = HelpTicks;
	dbg_msg("tracking", "%d added as a helper of %d for %d", HelperCid, GetCid(), m_LastHelper.m_Tick);
}

void CInfClassCharacter::ResetHelpers()
{
	m_LastHelper.m_Cid = -1;
	m_LastHelper.m_Tick = 0;
}

void CInfClassCharacter::GetDeathContext(const SDamageContext &DamageContext, DeathContext *pContext) const
{
	pContext->Killer = DamageContext.Killer;
	pContext->DamageType = DamageContext.DamageType;

	const int GivenKiller = DamageContext.Killer;
	const EDamageType DamageType = DamageContext.DamageType;

	switch(DamageContext.DamageType)
	{
	case EDamageType::GAME:
	case EDamageType::GAME_FINAL_EXPLOSION:
	case EDamageType::GAME_INFECTION:
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
	// If killed with a LASER_WALL then the Engineer must be either the Killer or the Assistant
	if(DamageType == EDamageType::LASER_WALL)
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

		if(Enforcer.m_DamageType == EDamageType::WHITE_HOLE)
		{
			AddUnique(Enforcer.m_Cid, &MustBeKillerOrAssistant);
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
	case EDamageType::DEATH_TILE:
	case EDamageType::INFECTION_TILE:
	case EDamageType::KILL_COMMAND:
	case EDamageType::LASER_WALL:
	case EDamageType::SCIENTIST_TELEPORT:
		DirectKill = false;
	default:
		break;
	}

	ClientsArray Killers;
	ClientsArray Assistants;
	if(!DirectKill)
	{
		if(IsPassenger())
		{
			const CInfClassCharacter *pDriver = GetTaxiDriver();
			if(pDriver->m_LastHookerTick + 1 >= Server()->Tick())
			{
				Killers = pDriver->m_LastHookers;
			}
		}
		else
		{
			Killers = HookersRightNow;
		}

		if(m_LastFreezer >= 0)
		{
			AddUnique(m_LastFreezer, &Killers);
		}
	}

	if(IsInSlowMotion() && m_SlowEffectApplicant.has_value())
	{
		// The Looper should be the Assistant (if not the killer) - before any other player
		AddUnique(m_SlowEffectApplicant.value(), &Assistants);
	}

	if(IsBlind() && m_LastBlinder.has_value())
	{
		// The Blinder should be the Assistant (if not the killer) - before any other player
		AddUnique(m_LastBlinder.value(), &Assistants);
	}

	if(GivenKiller != GetCid())
	{
		AddUnique(GivenKiller, &Killers);
	}

	if(DirectKill && !m_TakenDamageDetails.IsEmpty())
	{
		// DirectDieCall means that this is a direct die() call.
		// It means that the dealt damage does not matter.
		bool DirectDieCall = m_TakenDamageDetails.Last().From != GivenKiller || m_TakenDamageDetails.Last().DamageType != DamageType;

		bool SniperOneshot = (DamageType == EDamageType::SNIPER_RIFLE) && (m_TakenDamageDetails.Last().From == GivenKiller) && (m_TakenDamageDetails.Last().Amount >= 20);
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
				AddUnique(info.m_Cid, &Enforcers);
			}
		}

		if((m_LastHookerTick > 0) && (!m_LastHookers.IsEmpty()))
		{
			AddUnique(m_LastHookers.First(), &Enforcers);
		}
	}

	int Killer = Killers.IsEmpty() ? GivenKiller : Killers.First();
	int Assistant = -1;

	if((Killer >= 0) && (GetCid() != Killer))
	{
		const CInfClassCharacter *pKiller = GameController()->GetCharacter(Killer);
		if(pKiller && pKiller->m_LastHelper.m_Tick > 0)
		{
			// Check if the helper is in game
			const CInfClassCharacter *pKillerHelper = GameController()->GetCharacter(pKiller->m_LastHelper.m_Cid);
			if(pKillerHelper)
			{
				AddUnique(pKiller->m_LastHelper.m_Cid, &Assistants);
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

void CInfClassCharacter::UpdateLastEnforcer(int ClientId, float Force, EDamageType DamageType, int Tick)
{
	if(Force < 3)
		return;

	if(m_EnforcersInfo.Size() == m_EnforcersInfo.Capacity())
	{
		m_EnforcersInfo.RemoveAt(0);
	}

	if(!m_EnforcersInfo.IsEmpty())
	{
		if(m_EnforcersInfo.Last().m_Cid == ClientId)
		{
			m_EnforcersInfo.Last().m_Tick = Tick;
			return;
		}
	}

	EnforcerInfo Info;
	Info.m_Cid = ClientId;
	Info.m_DamageType = DamageType;
	Info.m_Tick = Tick;

	m_EnforcersInfo.Add(Info);
}

void CInfClassCharacter::RemoveReferencesToCid(int ClientId)
{
	for(std::size_t i = 0; i < m_EnforcersInfo.Size(); ++i)
	{
		if(m_EnforcersInfo.At(i).m_Cid == ClientId)
		{
			m_EnforcersInfo.RemoveAt(i);
		}
	}

	if(m_LastFreezer == ClientId)
	{
		m_LastFreezer = -1;
	}

	if(m_LastHelper.m_Cid == ClientId)
	{
		m_LastHelper.m_Cid = -1;
	}

	m_LastHookers.RemoveOne(ClientId);

	std::erase_if(m_TakenDamageDetails, [ClientId](const CDamagePoint &DP) { return DP.From == ClientId; });
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

CInfClassCharacter *CInfClassCharacter::GetPassenger() const
{
	if(!m_Core.m_Passenger)
	{
		return nullptr;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacterCore *pCharCore = GameWorld()->m_Core.m_apCharacters[i];
		if(pCharCore == m_Core.m_Passenger)
			return GameController()->GetCharacter(i);
	}

	return nullptr;
}

CInfClassCharacter *CInfClassCharacter::GetTaxi() const
{
	if(!IsPassenger())
	{
		return nullptr;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacterCore *pCharCore = GameWorld()->m_Core.m_apCharacters[i];
		if(pCharCore && (pCharCore->m_Passenger == &m_Core))
			return GameController()->GetCharacter(i);
	}

	return nullptr;
}

CInfClassCharacter *CInfClassCharacter::GetTaxiDriver() const
{
	CInfClassCharacter *pDriver = nullptr;
	CInfClassCharacter *pTaxi = GetTaxi();
	while(pTaxi)
	{
		pDriver = pTaxi;
		pTaxi = pTaxi->GetTaxi();
	}
	return pDriver;
}

void CInfClassCharacter::SetPassenger(CInfClassCharacter *pPassenger)
{
	m_Core.SetPassenger(pPassenger ? &pPassenger->m_Core : nullptr);
}

void CInfClassCharacter::TryBecomePassenger(CInfClassCharacter *pTargetDriver)
{
	m_Core.TryBecomePassenger(&pTargetDriver->m_Core);
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
		new CSuperWeaponIndicator(GameServer(), GetPos(), GetCid());
	}
	m_HasIndicator = Enabled;
}

EInfclassWeapon CInfClassCharacter::GetInfWeaponId(int WID) const
{
	if(WID < 0)
		WID = m_ActiveWeapon;

	if(WID == WEAPON_HAMMER)
	{
		switch(GetPlayerClass())
		{
		case EPlayerClass::Ninja:
			return EInfclassWeapon::NINJA_KATANA;
		case EPlayerClass::Hero:
			return EInfclassWeapon::TURRET_INSTALL_KIT;
		default:
			return EInfclassWeapon::HAMMER;
		}
	}
	else if(WID == WEAPON_GUN)
	{
		switch(GetPlayerClass())
		{
		case EPlayerClass::Mercenary:
			return EInfclassWeapon::MERCENARY_GUN;
		default:
			return EInfclassWeapon::GUN;
		}
		return EInfclassWeapon::GUN;
	}
	else if(WID == WEAPON_SHOTGUN)
	{
		switch(GetPlayerClass())
		{
		case EPlayerClass::Medic:
			return EInfclassWeapon::MEDIC_SHOTGUN;
		case EPlayerClass::Hero:
			return EInfclassWeapon::HERO_SHOTGUN;
		case EPlayerClass::Biologist:
			return EInfclassWeapon::RICOCHET_SHOTGUN;
		default:
			return EInfclassWeapon::SHOTGUN;
		}
	}
	else if(WID == WEAPON_GRENADE)
	{
		switch(GetPlayerClass())
		{
		case EPlayerClass::Mercenary:
			return EInfclassWeapon::POISON_GRENADE;
		case EPlayerClass::Medic:
			return EInfclassWeapon::HEALING_GRENADE;
		case EPlayerClass::Soldier:
			return EInfclassWeapon::SOLDIER_GRENADE;
		case EPlayerClass::Ninja:
			return EInfclassWeapon::NINJA_GRENADE;
		case EPlayerClass::Scientist:
			return EInfclassWeapon::TELEPORT_GUN;
		case EPlayerClass::Hero:
			return EInfclassWeapon::HERO_GRENADE;
		case EPlayerClass::Looper:
			return EInfclassWeapon::LOOPER_GRENADE;
		default:
			return EInfclassWeapon::GRENADE;
		}
	}
	else if(WID == WEAPON_LASER)
	{
		switch(GetPlayerClass())
		{
		case EPlayerClass::Engineer:
			return EInfclassWeapon::ENGINEER_LASER;
		case EPlayerClass::Ninja:
			return EInfclassWeapon::BLINDING_LASER;
		case EPlayerClass::Looper:
			return EInfclassWeapon::LOOPER_LASER;
		case EPlayerClass::Scientist:
			return EInfclassWeapon::EXPLOSIVE_LASER;
		case EPlayerClass::Sniper:
			return EInfclassWeapon::SNIPER_RIFLE;
		case EPlayerClass::Hero:
			return EInfclassWeapon::HERO_LASER;
		case EPlayerClass::Biologist:
			return EInfclassWeapon::BIOLOGIST_MINE_LASER;
		case EPlayerClass::Medic:
			return EInfclassWeapon::MEDIC_LASER;
		case EPlayerClass::Mercenary:
			return EInfclassWeapon::MERCENARY_UPGRADE_LASER;
		default:
			return EInfclassWeapon::LASER;
		}
	}
	else if(WID == WEAPON_NINJA)
	{
		return EInfclassWeapon::NINJA;
	}
	else
	{
		return EInfclassWeapon::NONE;
	}
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
	if(GetPlayerClass() != EPlayerClass::None)
	{
		SetAntiFire();
		pPlayer->CloseMapMenu();
		return;
	}

	vec2 CursorPos = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
	if(length(CursorPos) < 100.0f)
	{
		pPlayer->m_MapMenuItem = -1;
		GameServer()->SendBroadcast_Localization(GetCid(),
			EBroadcastPriority::INTERFACE, BROADCAST_DURATION_REALTIME,
			_("Choose your class"), NULL);

		return;
	}

	float Angle = 2.0f * pi + atan2(CursorPos.x, -CursorPos.y);
	float AngleStep = 2.0f * pi / static_cast<float>(CMapConverter::NUM_MENUCLASS);
	int HoveredMenuItem = ((int)((Angle + AngleStep / 2.0f) / AngleStep)) % CMapConverter::NUM_MENUCLASS;
	if(HoveredMenuItem == CMapConverter::MENUCLASS_RANDOM)
	{
		GameServer()->SendBroadcast_Localization(GetCid(),
			EBroadcastPriority::INTERFACE, BROADCAST_DURATION_REALTIME, _("Random choice"), nullptr);
		pPlayer->m_MapMenuItem = HoveredMenuItem;
	}
	else
	{
		EPlayerClass NewClass = CInfClassGameController::MenuClassToPlayerClass(HoveredMenuItem);
		CLASS_AVAILABILITY Availability = GameController()->GetPlayerClassAvailability(NewClass, pPlayer);

		switch(Availability)
		{
		case CLASS_AVAILABILITY::AVAILABLE:
		{
			const char *pClassName = CInfClassGameController::GetClassDisplayName(NewClass);
			GameServer()->SendBroadcast_Localization(GetCid(),
				EBroadcastPriority::INTERFACE, BROADCAST_DURATION_REALTIME,
				pClassName, nullptr);
		}
		break;
		case CLASS_AVAILABILITY::DISABLED:
			GameServer()->SendBroadcast_Localization(GetCid(),
				EBroadcastPriority::INTERFACE, BROADCAST_DURATION_REALTIME,
				_("The class is disabled"), nullptr);
			break;
		case CLASS_AVAILABILITY::NEED_MORE_PLAYERS:
		{
			int MinPlayers = GameController()->GetMinPlayersForClass(NewClass);
			GameServer()->SendBroadcast_Localization_P(GetCid(),
				EBroadcastPriority::INTERFACE, BROADCAST_DURATION_REALTIME,
				MinPlayers,
				_P("Need at least {int:MinPlayers} player",
					"Need at least {int:MinPlayers} players"),
				"MinPlayers", &MinPlayers,
				nullptr);
		}
		break;
		case CLASS_AVAILABILITY::LIMIT_EXCEEDED:
			GameServer()->SendBroadcast_Localization(GetCid(),
				EBroadcastPriority::INTERFACE, BROADCAST_DURATION_REALTIME,
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
	EPlayerClass NewClass = CInfClassGameController::MenuClassToPlayerClass(MenuClass);
	if(NewClass == EPlayerClass::Random)
	{
		NewClass = GameController()->ChooseHumanClass(pPlayer);
		Random = true;
		pPlayer->SetRandomClassChoosen();
	}
	if(NewClass == EPlayerClass::Invalid)
	{
		return;
	}

	if(GameController()->GetPlayerClassAvailability(NewClass, pPlayer) == CLASS_AVAILABILITY::AVAILABLE)
	{
		SetAntiFire();
		pPlayer->m_MapMenuItem = 0;
		pPlayer->SetClass(NewClass);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "choose_class player='%s' class='%s' random='%d'",
			Server()->ClientName(GetCid()), toString(NewClass), Random);
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
				info.m_Cid = -1;
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
	EDamageType DamageType = EDamageType::INVALID;
	switch(Weapon)
	{
	case WEAPON_SELF:
		DamageType = EDamageType::KILL_COMMAND;
		break;
	case WEAPON_GAME:
		DamageType = EDamageType::GAME;
		break;
	default:
		dbg_msg("infclass", "Invalid Die() event: victim=%d, killer=%d, weapon=%d", GetCid(), Killer, Weapon);
	}

	Die(Killer, DamageType);
}

void CInfClassCharacter::Die(int Killer, EDamageType DamageType)
{
	dbg_msg("server", "CInfClassCharacter::Die: victim: %d, killer: %d, DT: %d", GetCid(), Killer, static_cast<int>(DamageType));

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

	if(Context.DamageType == EDamageType::INFECTION_TILE)
	{
		return;
	}

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameWorld()->RemoveEntity(this);
	GameWorld()->m_Core.m_apCharacters[GetCid()] = 0;
	GameServer()->CreateDeath(GetPos(), GetCid());
}

void CInfClassCharacter::GiveWeapon(int Weapon, int Ammo)
{
	m_aWeapons[Weapon].m_Got = true;
	AddAmmo(Weapon, Ammo);
}

void CInfClassCharacter::SetActiveWeapon(int Weapon)
{
	m_ActiveWeapon = Weapon;
}

void CInfClassCharacter::SetLastWeapon(int Weapon)
{
	m_LastWeapon = Weapon;
}

bool CInfClassCharacter::HasWeapon(int Weapon) const
{
	return m_aWeapons[Weapon].m_Got;
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
	if(!m_aWeapons[Weapon].m_Got)
		return;

	if(!m_pClass)
		return;

	WeaponRegenParams Params;
	m_pClass->GetAmmoRegenParams(Weapon, &Params);

	if(Ammo < 0)
		Ammo = Params.MaxAmmo;

	int TargetAmmo = maximum(0, m_aWeapons[Weapon].m_Ammo) + Ammo;
	m_aWeapons[Weapon].m_Ammo = minimum(Params.MaxAmmo, TargetAmmo);
}

int CInfClassCharacter::GetAmmo(int Weapon) const
{
	return m_aWeapons[Weapon].m_Ammo;
}

int CInfClassCharacter::GetCid() const
{
	if(m_pPlayer)
	{
		return m_pPlayer->GetCid();
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

	if(GetPlayerClass() == EPlayerClass::None)
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
	return m_Core.HookedPlayer();
}

void CInfClassCharacter::SetHookedPlayer(int ClientId)
{
	m_Core.SetHookedPlayer(ClientId);

	if(ClientId >= 0)
	{
		m_Core.m_HookTick = 0;
		m_Core.m_TriggeredEvents |= COREEVENT_HOOK_ATTACH_PLAYER;
		m_Core.m_HookState = HOOK_GRABBED;

		const CInfClassCharacter *pCharacter = GameController()->GetCharacter(ClientId);
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

bool CInfClassCharacter::HasGrantedInvisibility() const
{
	return Server()->Tick() < m_GrantedInvisibilityUntilTick;
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

void CInfClassCharacter::Freeze(float Time, int Player, FREEZEREASON Reason)
{
	if(m_IsFrozen && m_FreezeReason == FREEZEREASON_UNDEAD)
		return;

	m_IsFrozen = true;
	m_FrozenTime = Server()->TickSpeed()*Time;
	m_FreezeReason = Reason;

	m_LastFreezer = Player;

	m_Core.m_FreezeStart = Server()->Tick();
}

bool CInfClassCharacter::IsFrozen() const
{
	return m_IsFrozen;
}

void CInfClassCharacter::Unfreeze()
{
	m_IsFrozen = false;
	m_FrozenTime = 0;
	m_Core.m_FreezeStart = 0;

	if(m_pPlayer)
	{
		GameServer()->ClearBroadcast(m_pPlayer->GetCid(), EBroadcastPriority::EFFECTSTATE);
	}
	GameServer()->CreatePlayerSpawn(GetPos());

	if(m_FreezeReason == FREEZEREASON_UNDEAD)
	{
		m_Health = 10.0;
		GetPlayer()->ResetTheTargetToFollow();
	}
}

void CInfClassCharacter::TryUnfreeze(int UnfreezerCid)
{
	if(!IsFrozen())
		return;

	if(!m_pClass->CanBeUnfreezed())
	{
		return;
	}

	Unfreeze();

	if(UnfreezerCid >= 0)
	{
		const float UnfreezerHelperDuration = 10;
		AddHelper(UnfreezerCid, UnfreezerHelperDuration);
	}
}

int CInfClassCharacter::GetFreezer() const
{
	return IsFrozen() ? m_LastFreezer : -1;
}

void CInfClassCharacter::ResetBlindness()
{
	m_BlindnessTicks = 0;
}

void CInfClassCharacter::MakeBlind(float Duration, std::optional<int> FromCid)
{
	m_BlindnessTicks = Server()->TickSpeed() * Duration;
	m_LastBlinder = FromCid;

	GameServer()->SendEmoticon(GetCid(), EMOTICON_QUESTION);
}

float CInfClassCharacter::WebHookLength() const
{
	if((GetEffectiveHookMode() != 1) && !g_Config.m_InfSpiderCatchHumans)
		return 0;

	if(m_Core.m_HookState != HOOK_GRABBED)
		return 0;

	return distance(m_Core.m_Pos, m_Core.m_HookPos);
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
}

void CInfClassCharacter::MakeInvisible()
{
	m_IsInvisible = true;
}

void CInfClassCharacter::GrantInvisibility(float Duration)
{
	m_GrantedInvisibilityUntilTick = Server()->Tick() + Server()->TickSpeed() * Duration;
	if(Duration > 0)
	{
		MakeInvisible();
	}
}

void CInfClassCharacter::GrantSpawnProtection(float Duration)
{
	// Indicate time left being protected via eyes
	m_ProtectionTick = Server()->TickSpeed() * Duration;
	if(!IsFrozen() && !IsInvisible())
	{
		SetEmote(EMOTE_SURPRISE, Server()->Tick() + m_ProtectionTick);
	}
}

void CInfClassCharacter::PreCoreTick()
{
	m_InputBackup = m_Input;

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
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCid(), EBroadcastPriority::EFFECTSTATE, BROADCAST_DURATION_REALTIME, _("You are frozen: {sec:EffectDuration}"), "EffectDuration", &FreezeSec, NULL);
		}
	}

	if(m_SlowMotionTick > 0)
	{
		--m_SlowMotionTick;

		if(m_SlowMotionTick > 0)
		{
			int SloMoSec = 1 + (m_SlowMotionTick / Server()->TickSpeed());
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCid(), EBroadcastPriority::EFFECTSTATE, BROADCAST_DURATION_REALTIME, _("You are slowed: {sec:EffectDuration}"), "EffectDuration", &SloMoSec, NULL);
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

	if(!m_InWater && !IsGrounded() && (m_Core.m_HookState != HOOK_GRABBED || m_Core.HookedPlayer() != -1))
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

	if(m_pClass)
	{
		if(m_Core.m_TriggeredEvents & COREEVENT_HOOK_ATTACH_PLAYER)
		{
			m_pClass->OnHookAttachedPlayer();
		}

		m_pClass->OnCharacterPostCoreTick();
	}

	HandleWeaponsRegen();
	HandleIndirectKillerCleanup();
	HandleTeleports();

	// Handle the pain
	if(m_PainSoundTimer > 0)
	{
		m_PainSoundTimer--;
	}

	// Some classes (namely Sniper) resets the input to lock the position
	// Restore the Input value after physics handling is done to fix the m_PrevInput
	m_Input = m_InputBackup;
}

void CInfClassCharacter::SnapCharacter(int SnappingClient, int Id)
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

	CNetObj_Character *pCharacter = Server()->SnapNewItem<CNetObj_Character>(Id);
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
	if(GetInfWeaponId(m_ActiveWeapon) == EInfclassWeapon::NINJA_KATANA)
	{
		Weapon = WEAPON_NINJA;
	}

	if(PrivateGetPlayerClass() == EPlayerClass::Spider)
	{
		pCharacter->m_HookTick -= (g_Config.m_InfSpiderHookTime - 1) * SERVER_TICK_SPEED - SERVER_TICK_SPEED / 5;
		if(pCharacter->m_HookTick < 0)
			pCharacter->m_HookTick = 0;
	}
	if(PrivateGetPlayerClass() == EPlayerClass::Bat)
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
	int ClientVersion = Server()->GetClientInfclassVersion(SnappingClient);
	int SnappingSpectatorId = -1;
	if(pSnappingClient)
	{
		SnappingSpectatorId = pSnappingClient->m_SpectatorId;
		int FollowingCid = pSnappingClient->TargetToFollow();
		if(FollowingCid >= 0)
		{
			SnappingSpectatorId = FollowingCid;
		}
	}

	int NormalizedArmor = clamp<int>(std::ceil(m_Armor * 10.0f / m_MaxArmor), 0, 10);
	if(GameController()->CanSeeDetails(SnappingClient, GetCid()) ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCid() == SnappingSpectatorId))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = NormalizedArmor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}
	else if(pSnappingClient && pSnappingClient->IsHuman() == m_pPlayer->IsHuman())
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = NormalizedArmor;
	}

	if(IsInvisible())
	{
		if(ClientVersion < VERSION_INFC_160)
		{
			pCharacter->m_Emote = EMOTE_BLINK;
		}
	}

	/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponId(m_ActiveWeapon) == EInfclassWeapon::MERCENARY_GUN)
	{
		pCharacter->m_AmmoCount /= (GameController()->GetMaxAmmo(EInfclassWeapon::MERCENARY_GUN) / 10);
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

	const EPlayerClass PlayerClass = GetPlayerClass();
	if(!IsHumanClass(PlayerClass))
	{
		Armor = 0;
	}

	SetHealthArmor(10, Armor);
}

void CInfClassCharacter::DestroyChildEntities()
{
	static const auto InfCEntities = {
		CGameWorld::ENTTYPE_PICKUP,
		CGameWorld::ENTTYPE_PROJECTILE,
		CGameWorld::ENTTYPE_LASER,
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
			if(p->GetOwner() != m_pPlayer->GetCid())
				continue;

			GameServer()->m_World.DestroyEntity(p);
		}
	}

	m_HookMode = 0;
}

void CInfClassCharacter::FreeChildSnapIds()
{
	if(m_FlagId >= 0)
	{
		Server()->SnapFreeId(m_FlagId);
		m_FlagId = -1;
	}
	if(m_HeartId >= 0)
	{
		Server()->SnapFreeId(m_HeartId);
		m_HeartId = -1;
	}
	if(m_CursorId >= 0)
	{
		Server()->SnapFreeId(m_CursorId);
		m_CursorId = -1;
	}
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
	
	if(GetPlayerClass() == EPlayerClass::Ghoul)
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
		GameWorld()->ReleaseHooked(GetPlayer()->GetCid());
	}

	ResetHook();
}
