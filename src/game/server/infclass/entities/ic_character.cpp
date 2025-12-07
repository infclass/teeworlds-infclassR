#include "ic_character.h"

#include <engine/server.h>
#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/collision.h>
#include <game/generated/server_data.h>
#include <game/infclass/damage_type.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/ic_playerclass.h>
#include <game/server/infclass/damage_context.h>
#include <game/server/infclass/death_context.h>
#include <game/server/infclass/entities/ic_projectile.h>
#include <game/server/infclass/entities/plasma.h>
#include <game/server/infclass/entities/superweapon-indicator.h>
#include <game/server/infclass/entities/white-hole.h>
#include <game/server/infclass/ic_gamecontroller.h>
#include <game/server/infclass/ic_player.h>

MACRO_ALLOC_POOL_ID_IMPL(CIcCharacter, MAX_CLIENTS)

CIcCharacter::CIcCharacter(CIcGameController *pGameController, CNetObj_PlayerInput LastInput) :
	CCharacter(pGameController->GameWorld(), LastInput), m_pGameController(pGameController)
{
	m_FlagId = Server()->SnapNewId();
	m_HeartId = Server()->SnapNewId();
	m_CursorId = Server()->SnapNewId();

	m_EffectsFactor = 1;
}

CIcCharacter::~CIcCharacter()
{
	FreeChildSnapIds();
	ResetClassObject();
}

CharacterFilter CIcCharacter::GetInfectedFilter()
{
	const auto InfectedEntitiesFilter = [](const CCharacter *pEntity) {
		const CIcCharacter *pInfEntity = CIcCharacter::GetInstance(pEntity);
		return !pInfEntity->IsHuman();
	};

	return InfectedEntitiesFilter;
}

CharacterFilter CIcCharacter::GetHumansFilter()
{
	const auto HumansEntitiesFilter = [](const CCharacter *pEntity) {
		const CIcCharacter *pInfEntity = CIcCharacter::GetInstance(pEntity);
		return pInfEntity->IsHuman();
	};

	return HumansEntitiesFilter;
}

CharacterFilter CIcCharacter::GetExceptCharacterFilter(int ClientId)
{
	static int s_ExceptCharacterId{};
	s_ExceptCharacterId = ClientId;

	const auto ExceptCharacterFilter = [](const CCharacter *pCh) {
		const CIcCharacter *pCharacter = CIcCharacter::GetInstance(pCh);
		return pCharacter->GetCid() != s_ExceptCharacterId;
	};

	return ExceptCharacterFilter;
}

CharacterFilter CIcCharacter::GetExceptCharactersFilter(const icArray<const CIcCharacter *, 10> &aCharacters)
{
	static icArray<const CIcCharacter *, 10> s_aFilterCharacters;
	s_aFilterCharacters = aCharacters;

	const auto ExceptCharactersFilter = [](const CCharacter *pCh) {
		const CIcCharacter *pCharacter = CIcCharacter::GetInstance(pCh);
		return !s_aFilterCharacters.Contains(pCharacter);
	};

	return ExceptCharactersFilter;
}

CharacterFilter CIcCharacter::GetFilterAllOff(CharacterFilter Filter1, CharacterFilter Filter2)
{
	static icArray<CharacterFilter, 4> s_aCombinedCharacterFilters;
	s_aCombinedCharacterFilters.Clear();
	s_aCombinedCharacterFilters.Add(Filter1);
	s_aCombinedCharacterFilters.Add(Filter2);

	auto CombinedCharactersFilter = [](const CCharacter *pEntity) {
		for(const CharacterFilter &Filter : s_aCombinedCharacterFilters)
		{
			if(!Filter(pEntity))
				return false;
		}

		return true;
	};

	return CombinedCharactersFilter;
}

void CIcCharacter::ResetClassObject()
{
	if(m_pClass)
	{
		// Ideally we would reset the Class character on `CPlayer::m_pCharacter = 0`
		// but it would be hard to hook there.
		m_pClass->SetCharacter(nullptr);
	}

	m_pClass = nullptr;
}

void CIcCharacter::OnCharacterSpawned(const SpawnContext &Context)
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

	GameController()->OnIcCharacterSpawned(this, Context);
}

void CIcCharacter::OnCharacterInInfectionZone()
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

		Die(&Context);
	}
}

void CIcCharacter::OnCharacterOutOfInfectionZone()
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

void CIcCharacter::OnCharacterInDamageZone(float Damage, float DamageInterval)
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

void CIcCharacter::Destroy()
{
	ResetClassObject();
	DestroyChildEntities();
	CCharacter::Destroy();
}

void CIcCharacter::TickBeforeWorld()
{
	const int CurrentTick = Server()->Tick();

	m_Core.m_Infected = IsInfected();
	m_Core.m_InLove = IsInLove();
	m_Core.m_HookProtected = GetPlayer()->HookProtectionEnabled();
	m_SleepThisTick = std::max(m_SleepTicks, m_DeepSleepTicks) > 0;

	for(std::optional<int> *pTick : {
			&m_SoloUntilTick,
		})
	{
		if(pTick->has_value() && pTick->value() >= 0 && pTick->value() < CurrentTick)
		{
			pTick->reset();
		}
	}

	UpdateCoreSolo();
}

void CIcCharacter::Tick()
{
	if(!m_pClass)
	{
		// Sometimes m_pClass is still nullptr on the very first tick
		// of a new round when the Reset is not complete yet.
		return;
	}

	const int CurrentTick = Server()->Tick();
	GameController()->HandleCharacterTiles(this);

	CCharacter::Tick();

	int SleepTicks = std::max(m_SleepTicks, m_DeepSleepTicks);
	if(SleepTicks > 0)
	{
		int EffectSec = 1 + (SleepTicks / Server()->TickSpeed());
		GameServer()->SendBroadcast_Localization(m_pPlayer->GetCid(), EBroadcastPriority::EFFECTSTATE, BROADCAST_DURATION_REALTIME,
			_("You are sleeping: {sec:EffectDuration}"),
			"EffectDuration", &EffectSec,
			nullptr);

		for(int *pEffectTicks : {&m_SleepTicks, &m_DeepSleepTicks})
		{
			int &EffectTicks = *pEffectTicks;
			if(EffectTicks > 0)
			{
				--EffectTicks;
			}
		}
	}
	else if(m_BlindnessTicks > 0)
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
		else if(m_PoisonEffectInterval > 1.0)
		{
			int DeathEventsPerInterval = std::ceil(m_PoisonEffectInterval);
			int DeathEventsTicks = Server()->TickSpeed() * m_PoisonEffectInterval / DeathEventsPerInterval;
			if ((CurrentTick - m_PoisonTick) % DeathEventsTicks == 0)
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

void CIcCharacter::TickDeferred()
{
	int Events = m_Core.m_TriggeredEvents;

	CCharacter::TickDeferred();

	if(Events & COREEVENT_AIR_JUMP)
	{
		const CClientMask MaskOnlyBlind = GameController()->GetBlindCharactersMask(GetCid());
		if(MaskOnlyBlind.any())
		{
			GameServer()->CreateSound(GetPos(), SOUND_PLAYER_AIRJUMP, MaskOnlyBlind);
		}
	}

	// Ghost events
	CClientMask MaskEsceptSelf = CClientMask().set().reset(m_pPlayer->GetCid());

	if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
		GameServer()->CreateSound(GetPos(), SOUND_HOOK_ATTACH_PLAYER);

	if(!IsInvisible())
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

	if(IsSleeping() && !m_Core.m_AttachedPlayers.empty())
	{
		CancelSleep(*m_Core.m_AttachedPlayers.begin());
	}
}

void CIcCharacter::TickPaused()
{
	if(m_pClass)
		m_pClass->OnCharacterTickPaused();

	if(m_DamageZoneTick != -1)
	{
		m_DamageZoneTick++;
	}
	if(m_PoisonTick)
		m_PoisonTick++;

	for(std::optional<int> *pTick : {
			&m_SoloUntilTick,
		})
	{
		if(pTick->has_value() && pTick->value() > 0)
		{
			pTick->value()++;
		}
	}
}

void CIcCharacter::Snap(int SnappingClient)
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
	if(m_Core.m_Solo)
		pDDNetCharacter->m_Flags |= CHARACTERFLAG_SOLO;

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
		IServer::CClientInfo ClientInfo = {nullptr};
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

void CIcCharacter::SpecialSnapForClient(int SnappingClient, bool *pDoSnap)
{
	CIcCharacter *pDestCharacter = GameController()->GetCharacter(SnappingClient);
	if((GetCid() != SnappingClient) && pDestCharacter && pDestCharacter->IsBlind())
	{
		*pDoSnap = false;
		return;
	}

	if((IsSolo() || !IsVisibleForPlayer(SnappingClient)) && !GameController()->CanSeeDetails(SnappingClient, GetCid()))
	{
		*pDoSnap = false;
		return;
	}
}

void CIcCharacter::HandleNinja()
{
	if(IsFrozen())
		return;

	if(!m_pClass)
		return;

	m_pClass->HandleNinja();
}

void CIcCharacter::HandleNinjaMove(float NinjaVelocity)
{
	SetVelocity(m_DartDir * NinjaVelocity);

	const vec2 GroundElasticity{};
	Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(GetProximityRadius(), GetProximityRadius()), GroundElasticity);

	// reset velocity so the client doesn't predict stuff
	ResetVelocity();
}

void CIcCharacter::HandleWeaponSwitch()
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

void CIcCharacter::FireWeapon()
{
	if(m_AntiFireTime > 0)
		return;

	if(m_ReloadTimer != 0)
		return;

	if((GetPlayerClass() == EPlayerClass::None) || !GetClass())
		return;

	if(IsSleeping())
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

	WeaponFireContext FireContext;
	FireContext.Weapon = m_ActiveWeapon;
	FireContext.InfClassWeapon = GetInfWeaponId(m_ActiveWeapon);
	FireContext.FireAccepted = true;
	FireContext.AmmoConsumed = 1;
	FireContext.AmmoAvailable = m_aWeapons[m_ActiveWeapon].m_Ammo;
	FireContext.NoAmmo = FireContext.AmmoAvailable == 0;
	FireContext.ReloadInterval = GameController()->GetFireDelay(FireContext.InfClassWeapon) / 1000.0f;

	GetClass()->OnWeaponFired(&FireContext);

	if(IsInLove() && FireContext.FireAccepted && !IsSolo())
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

bool CIcCharacter::TakeDamage(const vec2 &Force, float FloatDmg, int From, EDamageType DamageType, float *pDamagePointsLeft)
{
	float TakenPlaceholder{};
	float &DamageLeft = pDamagePointsLeft ? *pDamagePointsLeft : TakenPlaceholder;
	DamageLeft = 0;

	bool DamageIsPhysical{};
	switch(DamageType)
	{
	case EDamageType::SLUG_SLIME:
	case EDamageType::MERCENARY_GRENADE:
		DamageIsPhysical = false;
		break;
	default:
		DamageIsPhysical = true;
		break;
	}

	if(IsSleeping())
	{
		if(DamageIsPhysical)
		{
			FloatDmg *= Config()->m_InfSleeperTakeDamageRatio;
			CancelSleep(From);
		}
		else
		{
			const int MaxTicks = Server()->TickSpeed() * 3.0f;
			if(m_SleepTicks > MaxTicks)
			{
				m_SleepTicks = MaxTicks;
				m_AwakenedBy = From;
			}
		}
	}

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
		DamageContext.Weapon = DamageTypeToWeapon(DamageType, &DamageContext.Mode);
	}

	const int &Weapon = DamageContext.Weapon;
	TAKEDAMAGEMODE &Mode = DamageContext.Mode;
	int &Dmg = DamageContext.Damage;

	/* INFECTION MODIFICATION START ***************************************/

	//KillerPlayer
	CIcPlayer *pKillerPlayer = GameController()->GetPlayer(From);
	CIcCharacter *pKillerChar = nullptr;
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
	GetPlayer()->OnCharacterDamage(DamageContext);

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

		Die(&Context);
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

bool CIcCharacter::Heal(int HitPoints, std::optional<int> FromCid)
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
		GameContext()->CreateSound(GetPos(), Sound, CClientMask().set(GetCid()));

		if(FromCid.has_value())
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCid.value(), HealerHelperDuration);
		}
	}

	return Healed;
}

bool CIcCharacter::GiveHealth(int HitPoints, std::optional<int> FromCid)
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
		GameContext()->CreateSound(GetPos(), Sound, CClientMask().set(GetCid()));

		if(FromCid.has_value())
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCid.value(), HealerHelperDuration);
		}
	}

	return Healed;
}

bool CIcCharacter::GiveArmor(int HitPoints, std::optional<int> FromCid)
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
		GameContext()->CreateSound(GetPos(), Sound, CClientMask().set(GetCid()));

		if(FromCid.has_value())
		{
			const float HealerHelperDuration = 20;
			AddHelper(FromCid.value(), HealerHelperDuration);
		}
	}

	return Armored;
}

void CIcCharacter::SetJumpsLimit(int Limit)
{
	m_Core.m_Jumps = Limit;
}

EPlayerClass CIcCharacter::GetPlayerClass() const
{
	if(!m_pPlayer)
		return EPlayerClass::None;
	else
		return m_pPlayer->GetClass();
}

void CIcCharacter::SetDropLevel(int Level)
{
	m_DropLevel = Level;
}

void CIcCharacter::HandleDamage(int From, int Damage, EDamageType DamageType)
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

void CIcCharacter::OnTotalHealthChanged(int Difference)
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

void CIcCharacter::OnMaxHealthArmorChanged()
{
	UpdateEffectsFactor();
}

void CIcCharacter::PrepareToDie(DeathContext *pContext)
{
	switch(pContext->DamageType)
	{
	case EDamageType::DEATH_TILE:
		if(m_Invincible >= 3)
		{
			pContext->RefuseToDie = true;
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

	if(pContext->Killer == GetCid())
	{
		return;
	}

	if(IsInvincible())
	{
		pContext->RefuseToDie = true;
		return;
	}

	if(GetClass())
	{
		GetClass()->PrepareToDie(pContext);
	}
}

// TODO: Move those to CInfClassHuman
bool CIcCharacter::PositionIsLocked() const
{
	return m_PositionLocked;
}

void CIcCharacter::LockPosition()
{
	m_PositionLocked = true;
}

void CIcCharacter::UnlockPosition()
{
	m_PositionLocked = false;
}

void CIcCharacter::CancelLoveEffect()
{
	m_LoveTick = -1;
}

void CIcCharacter::PutToSleep(float Duration, std::optional<int> FromCid)
{
	int NewTicks = Server()->TickSpeed() * Duration;
	if(NewTicks > m_SleepTicks)
	{
		m_SleepTicks = NewTicks;
		m_PutToSleepBy = FromCid;
	}
}

void CIcCharacter::PutToDeepSleep(float Duration, std::optional<int> FromCid)
{
	int NewTicks = Server()->TickSpeed() * Duration;
	if(NewTicks > m_DeepSleepTicks)
	{
		m_DeepSleepTicks = NewTicks;
		m_PutToDeepSleepBy = FromCid;
	}
}

void CIcCharacter::CancelSleep(std::optional<int> ByCid)
{
	if(m_SleepTicks)
	{
		m_SleepTicks = 0;
		m_PutToSleepBy.reset();
		m_AwakenedBy = ByCid;
	}
}

void CIcCharacter::CancelDeepSleep(std::optional<int> ByCid)
{
	m_DeepSleepTicks = 0;
	m_PutToDeepSleepBy.reset();
}

int CIcCharacter::AwakenedBy() const
{
	return m_AwakenedBy.value_or(-1);
}

bool CIcCharacter::IsInSlowMotion() const
{
	return m_SlowMotionTick > 0;
}

float CIcCharacter::SlowMotionEffect(float Duration, std::optional<int> FromCid)
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

void CIcCharacter::CancelSlowMotion()
{
	m_SlowMotionTick = -1;
}

bool CIcCharacter::IsPoisoned() const
{
	return m_Poison > 0;
}

void CIcCharacter::Poison(int Count, int From, EDamageType DamageType, float Interval)
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

void CIcCharacter::ResetPoisonEffect()
{
	m_Poison = 0;
	// Do not reset m_PoisonTick here to prevent extra poisoning
}

void CIcCharacter::ResetMovementsInput()
{
	m_Input.m_Jump = 0;
	m_Input.m_Direction = 0;
}

void CIcCharacter::ResetHookInput()
{
	m_Input.m_Hook = 0;
}

bool CIcCharacter::IsInfected() const
{
	return m_pPlayer->IsInfected();
}

bool CIcCharacter::IsHuman() const
{
	return m_pPlayer->IsHuman();
}

void CIcCharacter::AddHelper(int HelperCid, float Time)
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

void CIcCharacter::ResetHelpers()
{
	m_LastHelper.m_Cid = -1;
	m_LastHelper.m_Tick = 0;
}

void CIcCharacter::UpdateEffectsFactor()
{
	float WeightRate = (10 + GetMaxArmor()) / 20.0f;
	// Rate 1 for standard 10 hp 10 armor
	// Rate 0.5 for 80 total hp
	// Rate 0.25 for 160 total hp
	m_EffectsFactor = 1.0f / std::sqrt(WeightRate);
}

void CIcCharacter::GetDeathContext(const SDamageContext &DamageContext, DeathContext *pContext) const
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

	if(IsSleeping() && (m_PutToSleepBy.has_value()))
	{
		// The Freezer must be either the Killer or the Assistant
		AddUnique(m_PutToSleepBy.value(), &MustBeKillerOrAssistant);
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
			const CIcCharacter *pDriver = GetTaxiDriver();
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
		const CIcCharacter *pKiller = GameController()->GetCharacter(Killer);
		if(pKiller && pKiller->m_LastHelper.m_Tick > 0)
		{
			// Check if the helper is in game
			const CIcCharacter *pKillerHelper = GameController()->GetCharacter(pKiller->m_LastHelper.m_Cid);
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

void CIcCharacter::UpdateLastHookers(const ClientsArray &Hookers, int HookerTick)
{
	m_LastHookers = Hookers;
	m_LastHookerTick = HookerTick;
}

void CIcCharacter::UpdateLastEnforcer(int ClientId, float Force, EDamageType DamageType, int Tick)
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

void CIcCharacter::RemoveReferencesToCid(int ClientId)
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

void CIcCharacter::SaturateVelocity(vec2 Force, float MaxSpeed)
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

bool CIcCharacter::IsPassenger() const
{
	return m_Core.m_IsPassenger;
}

bool CIcCharacter::HasPassenger() const
{
	return m_Core.m_Passenger;
}

CIcCharacter *CIcCharacter::GetPassenger() const
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

CIcCharacter *CIcCharacter::GetTaxi() const
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

CIcCharacter *CIcCharacter::GetTaxiDriver() const
{
	CIcCharacter *pDriver = nullptr;
	CIcCharacter *pTaxi = GetTaxi();
	while(pTaxi)
	{
		pDriver = pTaxi;
		pTaxi = pTaxi->GetTaxi();
	}
	return pDriver;
}

void CIcCharacter::SetPassenger(CIcCharacter *pPassenger)
{
	m_Core.SetPassenger(pPassenger ? &pPassenger->m_Core : nullptr);
}

void CIcCharacter::TryBecomePassenger(CIcCharacter *pTargetDriver)
{
	m_Core.TryBecomePassenger(&pTargetDriver->m_Core);
}

int CIcCharacter::GetInfZoneTick() // returns how many ticks long a player is already in InfZone
{
	if(m_InfZoneTick < 0)
		return 0;

	return Server()->Tick() - m_InfZoneTick;
}

bool CIcCharacter::HasSuperWeaponIndicator() const
{
	return m_HasIndicator;
}

void CIcCharacter::SetSuperWeaponIndicatorEnabled(bool Enabled)
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

EInfclassWeapon CIcCharacter::GetInfWeaponId(int WID) const
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
			return g_Config.m_InfTurretEnableLaser ? EInfclassWeapon::LASER_TURRET : EInfclassWeapon::PLASMA_TURRET;
		case EPlayerClass::Boomer:
			return EInfclassWeapon::BOOMER_EXPLOSION;
		case EPlayerClass::Bat:
			return EInfclassWeapon::JAWS;
		case EPlayerClass::Slug:
			return EInfclassWeapon::SLIME;
		default:
			return IsInfectedClass(GetPlayerClass()) ? EInfclassWeapon::INFECTED_HAMMER : EInfclassWeapon::HAMMER;
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
			return g_Config.m_InfEnableTranquilizerRifle ? EInfclassWeapon::TRANQUILIZER_RIFLE : EInfclassWeapon::MEDIC_LASER;
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

CGameWorld *CIcCharacter::GameWorld() const
{
	return m_pGameController->GameWorld();
}

const IServer *CIcCharacter::Server() const
{
	return m_pGameController->GameWorld()->Server();
}

void CIcCharacter::OpenClassChooser()
{
	GameController()->OnClassChooserRequested(this);
}

void CIcCharacter::HandleMapMenu()
{
	CIcPlayer *pPlayer = GetPlayer();
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
		EPlayerClass NewClass = CIcGameController::MenuClassToPlayerClass(HoveredMenuItem);
		CLASS_AVAILABILITY Availability = GameController()->GetPlayerClassAvailability(NewClass, pPlayer);

		switch(Availability)
		{
		case CLASS_AVAILABILITY::AVAILABLE:
		{
			const char *pClassName = CIcGameController::GetClassDisplayName(NewClass);
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
					"Need at least {int:MinPlayers} players", MinPlayers),
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

void CIcCharacter::HandleMapMenuClicked()
{
	bool Random = false;

	CIcPlayer *pPlayer = GetPlayer();
	int MenuClass = pPlayer->m_MapMenuItem;
	EPlayerClass NewClass = CIcGameController::MenuClassToPlayerClass(MenuClass);
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

void CIcCharacter::HandleWeaponsRegen()
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

		CAmmoParams Params = GetAmmoParams(static_cast<EWeapon>(i));

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

void CIcCharacter::HandleIndirectKillerCleanup()
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

void CIcCharacter::Die(int Killer, int Weapon)
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
		break;
	}

	Die(Killer, DamageType);
}

void CIcCharacter::Die(int Killer, EDamageType DamageType)
{
	dbg_msg("server", "CIcCharacter::Die: victim: %d, killer: %d, DT: %d", GetCid(), Killer, static_cast<int>(DamageType));

	SDamageContext DamageContext;
	DamageContext.Killer = Killer;
	DamageContext.DamageType = DamageType;
	DamageTypeToWeapon(DamageType, &DamageContext.Mode);

	DeathContext Context;
	GetDeathContext(DamageContext, &Context);

	Die(&Context);
}

void CIcCharacter::Die(DeathContext *pContext)
{
	if(!IsAlive())
	{
		return;
	}

	PrepareToDie(pContext);

	if(pContext->RefuseToDie)
	{
		return;
	}

	DestroyChildEntities();

	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	GameController()->OnIcCharacterDeath(this, pContext);

	// a nice sound
	GameServer()->CreateSound(GetPos(), SOUND_PLAYER_DIE);

	if(pContext->DamageType == EDamageType::INFECTION_TILE)
	{
		return;
	}

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();
	GameServer()->CreateDeath(GetPos(), GetCid());

	if(!pContext->KeepCharacter)
	{
		m_Alive = false;
		GameWorld()->RemoveEntity(this);
		GameWorld()->m_Core.m_apCharacters[GetCid()] = nullptr;
	}
}

CAmmoParams CIcCharacter::GetAmmoParams(EWeapon Weapon) const
{
	if (!m_pClass)
		return {};

	return m_pClass->GetAmmoParams(Weapon);
}

void CIcCharacter::SetLastWeapon(int Weapon)
{
	m_LastWeapon = Weapon;
}

bool CIcCharacter::HasWeapon(int Weapon) const
{
	return m_aWeapons[Weapon].m_Got;
}

bool CIcCharacter::HasWeapon(EWeaponClass WeaponClass) const
{
	for (int WeaponSlot = 0; WeaponSlot < NUM_WEAPONS; ++WeaponSlot)
	{
		const CWeaponStat &Weapon = m_aWeapons[WeaponSlot];
		if (!Weapon.m_Got)
			continue;

		const EInfclassWeapon InfId = GetInfWeaponId(WeaponSlot);
		if(GetWeaponClassById(InfId) == WeaponClass)
		{
			return true;
		}
	}

	return false;
}

CIcPlayer *CIcCharacter::GetPlayer()
{
	return static_cast<CIcPlayer*>(m_pPlayer);
}

void CIcCharacter::SetClass(CIcPlayerClass *pClass)
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

CInputCount CIcCharacter::CountFireInput() const
{
	return CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire);
}

bool CIcCharacter::FireJustPressed() const
{
	return m_LatestInput.m_Fire & 1;
}

void CIcCharacter::SetReloadTimer(int Ticks)
{
	m_ReloadTimer = Ticks;
}

void CIcCharacter::SetReloadDuration(float Seconds)
{
	m_ReloadTimer = Server()->TickSpeed() * Seconds;
}

void CIcCharacter::SetAntiFire()
{
	SetAntiFireDuration(Config()->m_InfAntiFireTime / 1000.0f);
}

void CIcCharacter::SetAntiFireDuration(float Seconds)
{
	m_AntiFireTime = Server()->TickSpeed() * Seconds;
}

vec2 CIcCharacter::GetHookPos() const
{
	return m_Core.m_HookPos;
}

int CIcCharacter::GetHookedPlayer() const
{
	return m_Core.HookedPlayer();
}

void CIcCharacter::SetHookedPlayer(int ClientId)
{
	m_Core.SetHookedPlayer(ClientId);

	if(ClientId >= 0)
	{
		m_Core.m_HookTick = 0;
		m_Core.m_TriggeredEvents |= COREEVENT_HOOK_ATTACH_PLAYER;
		m_Core.m_HookState = HOOK_GRABBED;

		const CIcCharacter *pCharacter = GameController()->GetCharacter(ClientId);
		const CCharacterCore *pCharCore = pCharacter ? &pCharacter->m_Core : nullptr;
		if(pCharCore)
		{
			m_Core.m_HookPos = pCharCore->m_Pos;
		}
	}
}

vec2 CIcCharacter::Velocity() const
{
	return m_Core.m_Vel;
}

float CIcCharacter::Speed() const
{
	return length(m_Core.m_Vel);
}

CGameContext *CIcCharacter::GameContext() const
{
	return m_pGameController->GameServer();
}

bool CIcCharacter::CanDie() const
{
	return IsAlive() && m_pClass && m_pClass->CanDie();
}

bool CIcCharacter::CanJump() const
{
	// 1 bit = to keep track if a jump has been made on this input
	if(m_Core.m_Jumped & 1)
		return false;
	// 2 bit = to keep track if an air-jump has been made
	if(m_Core.m_Jumped & 2)
		return false;

	return true;
}

bool CIcCharacter::IsVisibleForPlayer(int ClientId) const
{
	return !IsInvisible();
}

bool CIcCharacter::IsInvisible() const
{
	return m_IsInvisible;
}

bool CIcCharacter::HasGrantedInvisibility() const
{
	return Server()->Tick() < m_GrantedInvisibilityUntilTick;
}

bool CIcCharacter::IsSolo() const
{
	return m_Core.m_Solo;
}

bool CIcCharacter::IsInvincible() const
{
	return m_Invincible || (m_ProtectionTick > 0);
}

void CIcCharacter::SetInvincible(int Invincible)
{
	m_Invincible = Invincible;
}

bool CIcCharacter::HasHallucination() const
{
	return m_HallucinationTick > 0;
}

void CIcCharacter::Freeze(float Time, int Player, FREEZEREASON Reason)
{
	if(m_IsFrozen && m_FreezeReason == FREEZEREASON_UNDEAD)
		return;

	if(Reason == FREEZEREASON_FLASH)
	{
		Time *= m_EffectsFactor;
	}
	m_IsFrozen = true;
	m_FrozenTime = Server()->TickSpeed()*Time;
	m_FreezeReason = Reason;

	m_LastFreezer = Player;

	m_Core.m_FreezeStart = Server()->Tick();
}

bool CIcCharacter::IsFrozen() const
{
	return m_IsFrozen;
}

void CIcCharacter::Unfreeze()
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
		GetPlayer()->ResetSpecialCamera();
	}
}

void CIcCharacter::TryUnfreeze(int UnfreezerCid)
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

int CIcCharacter::GetFreezer() const
{
	return IsFrozen() ? m_LastFreezer : -1;
}

int CIcCharacter::FreezeStartTick() const
{
	return m_Core.m_FreezeStart;
}

void CIcCharacter::ResetBlindness()
{
	m_BlindnessTicks = 0;
}

void CIcCharacter::MakeBlind(float Duration, std::optional<int> FromCid)
{
	if(FromCid.has_value())
	{
		Duration *= m_EffectsFactor;
	}

	m_BlindnessTicks = Server()->TickSpeed() * Duration;
	m_LastBlinder = FromCid;

	GameServer()->SendEmoticon(GetCid(), EMOTICON_QUESTION);
}

float CIcCharacter::WebHookLength() const
{
	if((GetEffectiveHookMode() != 1) && !g_Config.m_InfSpiderCatchHumans)
		return 0;

	if(m_Core.m_HookState != HOOK_GRABBED)
		return 0;

	return distance(m_Core.m_Pos, m_Core.m_HookPos);
}

void CIcCharacter::GiveRandomClassSelectionBonus()
{
	IncreaseArmor(10);
}

void CIcCharacter::MakeVisible()
{
	if(m_IsInvisible)
	{
		GameServer()->CreatePlayerSpawn(m_Pos);
		m_IsInvisible = false;
	}
}

void CIcCharacter::MakeInvisible()
{
	m_IsInvisible = true;
}

void CIcCharacter::GrantInvisibility(float Duration)
{
	m_GrantedInvisibilityUntilTick = Server()->Tick() + Server()->TickSpeed() * Duration;
	if(Duration > 0)
	{
		MakeInvisible();
	}
}

void CIcCharacter::SetSoloForDuration(float Duration)
{
	if(Duration > 0)
		m_SoloUntilTick = static_cast<int>(Server()->Tick() + Server()->TickSpeed() * Duration);
	else
		m_SoloUntilTick.reset();

	UpdateCoreSolo();
}

void CIcCharacter::GrantSpawnProtection(float Duration)
{
	// Indicate time left being protected via eyes
	m_ProtectionTick = Server()->TickSpeed() * Duration;
	if(!IsFrozen() && !IsInvisible())
	{
		SetEmote(EMOTE_SURPRISE, Server()->Tick() + m_ProtectionTick);
	}
}

void CIcCharacter::PreCoreTick()
{
	m_InputBackup = m_Input;
	const int CurrentTick = Server()->Tick();

	--m_FrozenTime;
	if(m_IsFrozen)
	{
		if(m_FrozenTime <= 0)
		{
			TryUnfreeze();
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

	if(m_SoloUntilTick.has_value())
	{
		int EffectUntilTick = m_SoloUntilTick.value();
		if(EffectUntilTick < 0)
		{
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCid(), EBroadcastPriority::EFFECTSTATE,
				BROADCAST_DURATION_REALTIME, _("You are in Solo mode"), nullptr);
		}
		else
		{
			const int RemainingTicks = EffectUntilTick - CurrentTick;
			int EffectDuration = 1 + (RemainingTicks / Server()->TickSpeed());
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCid(), EBroadcastPriority::EFFECTSTATE,
				BROADCAST_DURATION_REALTIME, _("You are in Solo mode for {sec:EffectDuration}"), "EffectDuration", &EffectDuration, nullptr);
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

	if(IsFrozen() || IsSleeping())
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

void CIcCharacter::PostCoreTick()
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

	// Handle the pain
	if(m_PainSoundTimer > 0)
	{
		m_PainSoundTimer--;
	}

	// Some classes (namely Sniper) resets the input to lock the position
	// Restore the Input value after physics handling is done to fix the m_PrevInput
	m_Input = m_InputBackup;
}

void CIcCharacter::PrepareSnapContext(CCharacterSnapContext &SnapContext) const
{
	CCharacter::PrepareSnapContext(SnapContext);

	const int SnappingClient = SnapContext.SnappingClient;
	int AmmoCount = 0;
	int Health = 0;
	int Armor = 0;

	const CIcPlayer *pSnappingClient = GameController()->GetPlayer(SnappingClient);
	int ClientVersion = Server()->GetClientInfclassVersion(SnappingClient);
	int SnappingSpectatorId = -1;
	if(pSnappingClient)
	{
		SnappingSpectatorId = pSnappingClient->GetSpectatingCid();
	}

	/* INFECTION MODIFICATION START ***************************************/
	if(IsInvisible())
	{
		if(ClientVersion < VERSION_INFC_160)
		{
			SnapContext.Emote = EMOTE_BLINK;
		}
	}

	int NormalizedArmor = clamp<int>(std::ceil(m_Armor * 10.0f / m_MaxArmor), 0, 10);
	if(GameController()->CanSeeDetails(SnappingClient, GetCid()) ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCid() == SnappingSpectatorId))
	{
		Health = m_Health;
		Armor = NormalizedArmor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}
	else if(pSnappingClient && pSnappingClient->IsHuman() == m_pPlayer->IsHuman())
	{
		Health = m_Health;
		Armor = NormalizedArmor;
	}

	if(GetInfWeaponId(m_ActiveWeapon) == EInfclassWeapon::MERCENARY_GUN)
	{
		AmmoCount /= (GameController()->GetMaxAmmo(EInfclassWeapon::MERCENARY_GUN) / 10);
	}
	if(GetInfWeaponId(m_ActiveWeapon) == EInfclassWeapon::NINJA_KATANA)
	{
		SnapContext.Weapon = WEAPON_NINJA;
	}

	int DDNetVersion = Server()->GetClientVersion(SnappingClient);
	if(GetPlayerClass() == EPlayerClass::Boomer)
	{
		if(DDNetVersion >= 18080)
		{
			// https://github.com/ddnet/ddnet/pull/9230
			SnapContext.Weapon = -1;
		}
	}
	int HookTick = SnapContext.pCore->m_HookTick;
	if(GetPlayerClass() == EPlayerClass::Spider)
	{
		HookTick -= (g_Config.m_InfSpiderHookTime - 1) * SERVER_TICK_SPEED - SERVER_TICK_SPEED / 5;
		if(HookTick < 0)
			HookTick = 0;
	}
	if(GetPlayerClass() == EPlayerClass::Bat)
	{
		HookTick -= (g_Config.m_InfBatHookTime - 1) * SERVER_TICK_SPEED - SERVER_TICK_SPEED / 5;
		if(HookTick < 0)
			HookTick = 0;
	}
	/* INFECTION MODIFICATION END *****************************************/

	SnapContext.HookTick = HookTick;
	SnapContext.AmmoCount = AmmoCount;
	SnapContext.Health = Health;
	SnapContext.Armor = Armor;
}

void CIcCharacter::ClassSpawnAttributes()
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

void CIcCharacter::DestroyChildEntities()
{
	GameController()->DestroyChildEntities(GetCid());

	m_HookMode = 0;
}

void CIcCharacter::FreeChildSnapIds()
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

void CIcCharacter::UpdateCoreSolo()
{
	SetSolo(m_SoloUntilTick.has_value());
}

void CIcCharacter::UpdateTuningParam()
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
	if(m_IsFrozen || IsSleeping())
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

	if(GetPlayerClass() == EPlayerClass::None)
	{
		pTuningParams->m_HammerFireDelay = 100000; // None shouldn't be able to hammer
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
