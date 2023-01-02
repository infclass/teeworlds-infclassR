#include "human.h"

#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include <game/infclass/classes.h>
#include <game/server/gamecontext.h>

#include <game/server/entities/projectile.h>

#include <game/server/infclass/damage_context.h>
#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/entities/biologist-mine.h>
#include <game/server/infclass/entities/blinding-laser.h>
#include <game/server/infclass/entities/bouncing-bullet.h>
#include <game/server/infclass/entities/engineer-wall.h>
#include <game/server/infclass/entities/growingexplosion.h>
#include <game/server/infclass/entities/hero-flag.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/entities/laser-teleport.h>
#include <game/server/infclass/entities/looper-wall.h>
#include <game/server/infclass/entities/medic-laser.h>
#include <game/server/infclass/entities/merc-bomb.h>
#include <game/server/infclass/entities/merc-laser.h>
#include <game/server/infclass/entities/scatter-grenade.h>
#include <game/server/infclass/entities/scientist-laser.h>
#include <game/server/infclass/entities/scientist-mine.h>
#include <game/server/infclass/entities/soldier-bomb.h>
#include <game/server/infclass/entities/turret.h>
#include <game/server/infclass/entities/white-hole.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>
#include <game/server/teeinfo.h>

static const int SniperPositionLockTimeLimit = 15;

MACRO_ALLOC_POOL_ID_IMPL(CInfClassHuman, MAX_CLIENTS)

CInfClassHuman::CInfClassHuman(CInfClassPlayer *pPlayer)
	: CInfClassPlayerClass(pPlayer)
{
	m_BroadcastWhiteHoleReady = -100;

	for(int i = 0; i < m_BarrierHintIDs.Capacity(); ++i)
	{
		m_BarrierHintIDs.Add(Server()->SnapNewID());
	}
}

CInfClassHuman::~CInfClassHuman()
{
	for(int ID : m_BarrierHintIDs)
	{
		Server()->SnapFreeID(ID);
	}
}

CInfClassHuman *CInfClassHuman::GetInstance(CInfClassPlayer *pPlayer)
{
	CInfClassPlayerClass *pClass = pPlayer ? pPlayer->GetCharacterClass() : nullptr;
	if(pClass && pClass->IsHuman())
	{
		return static_cast<CInfClassHuman*>(pClass);
	}

	return nullptr;
}

CInfClassHuman *CInfClassHuman::GetInstance(CInfClassCharacter *pCharacter)
{
	CInfClassPlayerClass *pClass = pCharacter ? pCharacter->GetClass() : nullptr;
	if(pClass && pClass->IsHuman())
	{
		return static_cast<CInfClassHuman *>(pClass);
	}

	return nullptr;
}

SkinGetter CInfClassHuman::GetSkinGetter() const
{
	return CInfClassHuman::SetupSkin;
}

void CInfClassHuman::SetupSkinContext(CSkinContext *pOutput, bool ForSameTeam) const
{
	pOutput->PlayerClass = GetPlayerClass();
	pOutput->ExtraData1 = 0;
}

bool CInfClassHuman::SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion)
{
	switch(Context.PlayerClass)
	{
	case PLAYERCLASS_ENGINEER:
		pOutput->UseCustomColor = 0;
		pOutput->pSkinName = "limekitty";
		break;
	case PLAYERCLASS_SOLDIER:
		pOutput->pSkinName = "brownbear";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_SNIPER:
		pOutput->pSkinName = "warpaint";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_MERCENARY:
		pOutput->pSkinName = "bluestripe";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_SCIENTIST:
		pOutput->pSkinName = "toptri";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_BIOLOGIST:
		pOutput->pSkinName = "twintri";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_LOOPER:
		pOutput->pSkinName = "bluekitty";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 255;
		pOutput->ColorFeet = 0;
		break;
	case PLAYERCLASS_MEDIC:
		pOutput->pSkinName = "twinbop";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_HERO:
		pOutput->pSkinName = "redstripe";
		pOutput->UseCustomColor = 0;
		break;
	case PLAYERCLASS_NINJA:
		pOutput->pSkinName = "default";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 255;
		pOutput->ColorFeet = 0;
		break;
	case PLAYERCLASS_NONE:
		pOutput->pSkinName = "default";
		pOutput->UseCustomColor = 0;
		break;
	default:
		return false;
	}

	return true;
}


void CInfClassHuman::GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams)
{
	INFWEAPON InfWID = m_pCharacter->GetInfWeaponID(Weapon);
	pParams->MaxAmmo = Server()->GetMaxAmmo(InfWID);
	pParams->RegenInterval = Server()->GetAmmoRegenTime(InfWID);

	switch(InfWID)
	{
	case INFWEAPON::NINJA_GRENADE:
		pParams->MaxAmmo = minimum(pParams->MaxAmmo + m_pCharacter->m_NinjaAmmoBuff, 10);
		break;
	case INFWEAPON::MERCENARY_GUN:
		if(m_pCharacter->GetInAirTick() > Server()->TickSpeed() * 4)
		{
			pParams->RegenInterval = 0;
		}
		break;
	default:
		break;
	}

	if((Config()->m_InfTaxi == 1) && m_pCharacter->IsPassenger())
	{
		pParams->RegenInterval = 0;
	}
}

int CInfClassHuman::GetJumps() const
{
	switch(GetPlayerClass())
	{
	case PLAYERCLASS_SNIPER:
	case PLAYERCLASS_LOOPER:
		return 3;
	default:
		return 2;
	}
}

bool CInfClassHuman::CanBeHit() const
{
	if(GetPlayerClass() == PLAYERCLASS_NINJA)
	{
		// Do not hit slashing ninjas
		if(m_pCharacter->m_DartLifeSpan >= 0)
		{
			return false;
		}
	}

	return true;
}

void CInfClassHuman::CheckSuperWeaponAccess()
{
	if(m_ResetKillsTime)
		return;

	// check kills of player
	int Kills = m_KillsProgression;

	// Only scientists can receive white holes
	if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		if(!GameController()->WhiteHoleEnabled() || m_pCharacter->HasSuperWeaponIndicator())
		{
			// Can't receive a super weapon while having one available
			return;
		}

		// enable white hole probabilities
		if(Kills < Config()->m_InfWhiteHoleMinimalKills)
		{
			return;
		}

		if(random_prob(Config()->m_InfWhiteHoleProbability / 100.0f))
		{
			// Scientist-laser.cpp will make it unavailable after usage and reset player kills
			GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, _("White hole found, adjusting scientific parameters..."), nullptr);
			m_pCharacter->SetSuperWeaponIndicatorEnabled(true);
		}
	}
}

void CInfClassHuman::OnPlayerSnap(int SnappingClient, int InfClassVersion)
{
	if(InfClassVersion < VERSION_INFC_140)
	{
		// CNetObj_InfClassClassInfo introduced in v0.1.4
		return;
	}

	CNetObj_InfClassClassInfo *pClassInfo = static_cast<CNetObj_InfClassClassInfo *>(Server()->SnapNewItem(NETOBJTYPE_INFCLASSCLASSINFO, GetCID(), sizeof(CNetObj_InfClassClassInfo)));
	if(!pClassInfo)
		return;
	pClassInfo->m_Class = GetPlayerClass();
	pClassInfo->m_Flags = 0;
	pClassInfo->m_Data1 = 0;

	if(SnappingClient == GetCID())
	{
		switch(GetPlayerClass())
		{
		case PLAYERCLASS_HERO:
			if(m_pHeroFlag && m_pHeroFlag->IsAvailable())
			{
				pClassInfo->m_Data1 = m_pHeroFlag->GetSpawnTick();
			}
			else
			{
				pClassInfo->m_Data1 = -1;
			}
			break;
		case PLAYERCLASS_ENGINEER:
			for(TEntityPtr<CEngineerWall> pWall = GameWorld()->FindFirst<CEngineerWall>(); pWall; ++pWall)
			{
				if(pWall->GetOwner() != GetCID())
				{
					continue;
				}

				pClassInfo->m_Data1 = pWall->GetEndTick();
				break;
			}
			break;
		case PLAYERCLASS_LOOPER:
			for(TEntityPtr<CLooperWall> pWall = GameWorld()->FindFirst<CLooperWall>(); pWall; ++pWall)
			{
				if(pWall->GetOwner() != GetCID())
				{
					continue;
				}

				pClassInfo->m_Data1 = pWall->GetEndTick();
				break;
			}
			break;
		default:
			break;
		}
	}
}

void CInfClassHuman::OnCharacterPreCoreTick()
{
	CInfClassPlayerClass::OnCharacterPreCoreTick();

	switch (GetPlayerClass())
	{
		case PLAYERCLASS_SNIPER:
		{
			if(m_pCharacter->PositionIsLocked())
			{
				--m_PositionLockTicksRemaining;
				if((m_PositionLockTicksRemaining <= 0) || m_pCharacter->IsPassenger())
				{
					m_pCharacter->UnlockPosition();
				}
			}

			if(!m_pCharacter->PositionIsLocked())
			{
				if(m_pCharacter->IsGrounded())
				{
					m_PositionLockTicksRemaining = Server()->TickSpeed() * SniperPositionLockTimeLimit;
				}
			}

			if(m_pCharacter->PositionIsLocked())
			{
				if(m_pCharacter->m_Input.m_Jump && !m_pCharacter->m_PrevInput.m_Jump)
				{
					m_pCharacter->UnlockPosition();
				}
				else
				{
					m_pCharacter->ResetMovementsInput();
				}
			}
		}
			break;
		case PLAYERCLASS_NINJA:
		{
			if(m_pCharacter->IsGrounded() && m_pCharacter->m_DartLifeSpan <= 0)
			{
				m_pCharacter->m_DartLeft = Config()->m_InfNinjaJump;
			}
		}
			break;
		default:
			break;
	}
}

void CInfClassHuman::OnCharacterTick()
{
	CInfClassPlayerClass::OnCharacterTick();

	switch(GetPlayerClass())
	{
	case PLAYERCLASS_NINJA:
	{
		if(Server()->Tick() > m_NinjaTargetTick)
		{
			const ClientsArray &ValidNinjaTargets = GameController()->GetValidNinjaTargets();
			if(!ValidNinjaTargets.Contains(m_NinjaTargetCID))
			{
				if(ValidNinjaTargets.IsEmpty())
				{
					m_NinjaTargetCID = -1;
				}
				else
				{
					int Index = random_int(0, ValidNinjaTargets.Size() - 1);
					m_NinjaTargetCID = ValidNinjaTargets[Index];
				}
			}
		}
		else
		{
			m_NinjaTargetCID = -1;
		}
		break;
	}
	}

	if(m_ResetKillsTime)
	{
		m_ResetKillsTime--;
		if(!m_ResetKillsTime)
		{
			m_KillsProgression = 0;
		}
	}
}

void CInfClassHuman::OnCharacterSnap(int SnappingClient)
{
	switch(GetPlayerClass())
	{
	case PLAYERCLASS_HERO:
		SnapHero(SnappingClient);
		break;
	case PLAYERCLASS_ENGINEER:
		SnapEngineer(SnappingClient);
		break;
	case PLAYERCLASS_SCIENTIST:
		SnapScientist(SnappingClient);
		break;
	case PLAYERCLASS_LOOPER:
		SnapLooper(SnappingClient);
		break;
	default:
		break;
	}
}

void CInfClassHuman::OnCharacterDamage(SDamageContext *pContext)
{
	switch(GetPlayerClass())
	{
	case PLAYERCLASS_HERO:
		if(pContext->Mode == TAKEDAMAGEMODE::INFECTION)
		{
			pContext->Mode = TAKEDAMAGEMODE::NOINFECTION;
			pContext->Damage = 12;
		}
		break;
	default:
		break;
	}

	if(pContext->DamageType == DAMAGE_TYPE::NINJA)
	{
		// Humans are immune to Ninja's force
		pContext->Force = vec2(0, 0);
	}
}

void CInfClassHuman::OnKilledCharacter(int Victim, bool Assisted)
{
	if(!m_pCharacter)
		return;

	m_KillsProgression += Assisted ? 0.5f : 1.0f;

	switch(GetPlayerClass())
	{
	case PLAYERCLASS_MERCENARY:
		if(!Assisted)
		{
			m_pCharacter->AddAmmo(WEAPON_LASER, 3);
		}
		break;
	case PLAYERCLASS_NINJA:
		if(Victim == m_NinjaTargetCID)
		{
			OnNinjaTargetKiller(Assisted);
		}
		break;
	case PLAYERCLASS_MEDIC:
		if(!Assisted)
		{
			m_pCharacter->AddAmmo(WEAPON_GRENADE, 1);
		}
		break;
	case PLAYERCLASS_SCIENTIST:
		CheckSuperWeaponAccess();
		break;
	default:
		break;
	}
}

void CInfClassHuman::OnHumanHammerHitHuman(CInfClassCharacter *pTarget)
{
	if(GetPlayerClass() == PLAYERCLASS_MEDIC)
	{
		if(pTarget->GetPlayerClass() != PLAYERCLASS_HERO)
		{
			const int HadArmor = pTarget->GetArmor();
			if(HadArmor < 10)
			{
				pTarget->GiveArmor(4, GetCID());

				if(pTarget->GetArmor() == 10)
				{
					Server()->RoundStatistics()->OnScoreEvent(GetCID(), SCOREEVENT_HUMAN_HEALING,
						GetPlayerClass(), Server()->ClientName(GetCID()), GameServer()->Console());
					GameServer()->SendScoreSound(GetCID());
					m_pCharacter->AddAmmo(WEAPON_GRENADE, 1);
				}
			}
		}
	}
}

void CInfClassHuman::OnHookAttachedPlayer()
{
	if(!m_pCharacter)
		return;

	if(m_pCharacter->IsPassenger())
		return;

	if(!GameController()->GetTaxiMode())
		return;

	CCharacter *pHookedCharacter = GameController()->GetCharacter(m_pCharacter->GetHookedPlayer());
	if(!pHookedCharacter || !pHookedCharacter->IsHuman())
		return;

	m_pCharacter->m_Core.TryBecomePassenger(&pHookedCharacter->m_Core);
}

void CInfClassHuman::OnHammerFired(WeaponFireContext *pFireContext)
{
	switch(GetPlayerClass())
	{
	case PLAYERCLASS_MERCENARY:
		if(GameController()->MercBombsEnabled())
		{
			FireMercenaryBomb(pFireContext);
			return;
		}
		else
		{
			break;
		}
	case PLAYERCLASS_SNIPER:
		if(m_pCharacter->PositionIsLocked())
		{
			m_pCharacter->UnlockPosition();
		}
		else if(PositionLockAvailable())
		{
			m_pCharacter->LockPosition();
		}
		return;
	case PLAYERCLASS_HERO:
		PlaceTurret(pFireContext);
		return;
	case PLAYERCLASS_ENGINEER:
		PlaceEngineerWall(pFireContext);
		return;
	case PLAYERCLASS_SOLDIER:
		FireSoldierBomb(pFireContext);
		return;
	case PLAYERCLASS_NINJA:
		ActivateNinja(pFireContext);
		return;
	case PLAYERCLASS_SCIENTIST:
		PlaceScientistMine(pFireContext);
		return;
	case PLAYERCLASS_LOOPER:
		PlaceLooperWall(pFireContext);
		return;
	default:
		break;
	}

	const vec2 Direction = GetDirection();
	const vec2 ProjStartPos = GetPos() + Direction * GetHammerProjOffset();

	// Lookup for humans
	ClientsArray Targets;
	GameController()->GetSortedTargetsInRange(ProjStartPos, GetHammerRange(), ClientsArray({GetCID()}), &Targets);

	int Hits = 0;
	for(const int TargetCID : Targets)
	{
		CInfClassCharacter *pTarget = GameController()->GetCharacter(TargetCID);

		if(GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos()))
			continue;

		if(pTarget->IsZombie())
		{
			vec2 Dir;
			if(length(pTarget->GetPos() - GetPos()) > 0.0f)
				Dir = normalize(pTarget->GetPos() - GetPos());
			else
				Dir = vec2(0.f, -1.f);

			vec2 Force = vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;

			int Damage = 20;
			pTarget->TakeDamage(Force, Damage, GetCID(), DAMAGE_TYPE::HAMMER);
		}
		else
		{
			OnHumanHammerHitHuman(pTarget);
		}

		Hits++;

		CreateHammerHit(ProjStartPos, pTarget);
	}

	// if we Hit anything, we have to wait for the reload
	if(Hits)
	{
		m_pCharacter->SetReloadDuration(0.33f);
	}

	if(pFireContext->FireAccepted)
	{
		GameServer()->CreateSound(GetPos(), SOUND_HAMMER_FIRE);
	}
}

void CInfClassHuman::OnGunFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
		return;

	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;

	DAMAGE_TYPE DamageType = DAMAGE_TYPE::GUN;
	
	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
		DamageType = DAMAGE_TYPE::MERCENARY_GUN;

	{
		CProjectile *pProj = new CProjectile(GameContext(), WEAPON_GUN,
			GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
			1, 0, 0, -1, DamageType);
		
		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);
		
		CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
		Msg.AddInt(1);
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());
	}

	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		float MaxSpeed = GameServer()->Tuning()->m_GroundControlSpeed * 1.7f;
		vec2 Recoil = Direction * (-MaxSpeed / 5.0f);
		m_pCharacter->SaturateVelocity(Recoil, MaxSpeed);

		GameServer()->CreateSound(GetPos(), SOUND_HOOK_LOOP);
	}
	else
	{
		GameServer()->CreateSound(GetPos(), SOUND_GUN_FIRE);
	}
}

void CInfClassHuman::OnShotgunFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
		return;
	
	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos() + Direction * GetProximityRadius() * 0.75f;

	float Force = 2.0f;
	int ShotSpread = 3;
	DAMAGE_TYPE DamageType = DAMAGE_TYPE::SHOTGUN;

	switch(GetPlayerClass())
	{
	case PLAYERCLASS_BIOLOGIST:
		ShotSpread = 1;
		break;
	case PLAYERCLASS_MEDIC:
		Force = 10.0f;
		DamageType = DAMAGE_TYPE::MEDIC_SHOTGUN;
		break;
	default:
		break;
	}

	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(ShotSpread * 2 + 1);

	for(int i = -ShotSpread; i <= ShotSpread; ++i)
	{
		const float Spreading = i * 0.07f;
		float a = angle(Direction);
		a += Spreading * 2.0f * (0.25f + 0.75f * static_cast<float>(10 - pFireContext->AmmoAvailable) / 10.0f);
		float v = 1 - (absolute(i) / static_cast<float>(ShotSpread));
		float Speed = mix<float>(GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
		vec2 Direction = vec2(cosf(a), sinf(a)) * Speed;

		float LifeTime = GameServer()->Tuning()->m_ShotgunLifetime + 0.1f * static_cast<float>(pFireContext->AmmoAvailable)/10.0f;

		if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
		{
			CBouncingBullet *pProj = new CBouncingBullet(GameServer(),
				GetCID(),
				ProjStartPos,
				Direction);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
		}
		else
		{
			int Damage = 1;
			CProjectile *pProj = new CProjectile(GameContext(), WEAPON_SHOTGUN,
				GetCID(),
				ProjStartPos,
				Direction,
				(int)(Server()->TickSpeed() * LifeTime),
				Damage, false, Force, -1, DamageType);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
		}
	}

	Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());

	GameServer()->CreateSound(GetPos(), SOUND_SHOTGUN_FIRE);
}

void CInfClassHuman::OnGrenadeFired(WeaponFireContext *pFireContext)
{
	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		// Does not need the ammo in some cases
		OnMercGrenadeFired(pFireContext);
		return;
	}

	if(pFireContext->NoAmmo)
		return;

	switch(GetPlayerClass())
	{
	case PLAYERCLASS_SCIENTIST:
	{
		vec2 PortalPos;
		if(FindPortalPosition(&PortalPos))
		{
			vec2 OldPos = GetPos();
			m_pCharacter->m_Core.m_Pos = PortalPos;
			m_pCharacter->m_Core.m_HookedPlayer = -1;
			m_pCharacter->m_Core.m_HookState = HOOK_RETRACTED;
			m_pCharacter->m_Core.m_HookPos = PortalPos;
			if(g_Config.m_InfScientistTpSelfharm > 0) {
				m_pCharacter->TakeDamage(vec2(0.0f, 0.0f), g_Config.m_InfScientistTpSelfharm * 2, GetCID(), DAMAGE_TYPE::SCIENTIST_TELEPORT);
			}
			GameServer()->CreateDeath(OldPos, GetCID());
			GameServer()->CreateDeath(PortalPos, GetCID());
			GameServer()->CreateSound(PortalPos, SOUND_CTF_RETURN);
			new CLaserTeleport(GameServer(), PortalPos, OldPos);
		}
		else
		{
			pFireContext->FireAccepted = false;
		}
		break;
	}
	case PLAYERCLASS_MEDIC:
		OnMedicGrenadeFired(pFireContext);
		break;
	default:
	{
		vec2 Direction = GetDirection();
		vec2 ProjStartPos = GetPos() + Direction * GetProximityRadius() * 0.75f;
		CProjectile *pProj = new CProjectile(GameContext(), WEAPON_GRENADE,
			GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime),
			1, true, 0, SOUND_GRENADE_EXPLODE, DAMAGE_TYPE::GRENADE);

		if(GetPlayerClass() == PLAYERCLASS_NINJA)
		{
			pProj->FlashGrenade();
			pProj->SetFlashRadius(8);
		}

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
		Msg.AddInt(1);
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile) / sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());

		GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
	}
		break;
	}
}

void CInfClassHuman::OnLaserFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
	{
		return;
	}

	vec2 Direction = GetDirection();
	float StartEnergy = GameServer()->Tuning()->m_LaserReach;
	int Damage = GameServer()->Tuning()->m_LaserDamage;
	DAMAGE_TYPE DamageType = DAMAGE_TYPE::LASER;

	switch(GetPlayerClass())
	{
	case PLAYERCLASS_NINJA:
		OnBlindingLaserFired(pFireContext);
		break;
	case PLAYERCLASS_BIOLOGIST:
		OnBiologistLaserFired(pFireContext);
		break;
	case PLAYERCLASS_SCIENTIST:
		StartEnergy *= 0.6f;
		new CScientistLaser(GameServer(), GetPos(), Direction, StartEnergy, GetCID(), Damage);
		break;
	case PLAYERCLASS_MERCENARY:
		OnMercLaserFired(pFireContext);
		break;
	case PLAYERCLASS_MEDIC:
		new CMedicLaser(GameServer(), GetPos(), Direction, StartEnergy, GetCID());
		break;

	case PLAYERCLASS_LOOPER:
		StartEnergy *= 0.7f;
		Damage = 5;
		DamageType = DAMAGE_TYPE::LOOPER_LASER;
		new CInfClassLaser(GameServer(), GetPos(), Direction, StartEnergy, GetCID(), Damage, DamageType);
		break;
	case PLAYERCLASS_SNIPER:
		Damage = m_pCharacter->PositionIsLocked() ? 30 : random_int(10, 13);
		DamageType = DAMAGE_TYPE::SNIPER_RIFLE;
		new CInfClassLaser(GameServer(), GetPos(), Direction, StartEnergy, GetCID(), Damage, DamageType);
		break;
	default:
		new CInfClassLaser(GameServer(), GetPos(), Direction, StartEnergy, GetCID(), Damage, DamageType);
		break;
	}
	
	if(pFireContext->FireAccepted)
	{
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
}

void CInfClassHuman::GiveClassAttributes()
{
	m_FirstShot = true;

	m_ResetKillsTime = 0;
	m_TurretCount = 0;
	m_NinjaTargetTick = 0;
	m_NinjaTargetCID = -1;

	if(!m_pCharacter)
	{
		return;
	}

	CInfClassPlayerClass::GiveClassAttributes();

	switch(GetPlayerClass())
	{
		case PLAYERCLASS_ENGINEER:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_LASER);
			break;
		case PLAYERCLASS_SOLDIER:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_GRENADE);
			break;
		case PLAYERCLASS_MERCENARY:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			if(GameController()->MercBombsEnabled())
				m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_GUN);
			break;
		case PLAYERCLASS_SNIPER:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_LASER);
			break;
		case PLAYERCLASS_SCIENTIST:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_LASER);
			break;
		case PLAYERCLASS_BIOLOGIST:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->GiveWeapon(WEAPON_SHOTGUN, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_SHOTGUN);
			break;
		case PLAYERCLASS_LOOPER:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_LASER);
			break;
		case PLAYERCLASS_MEDIC:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_SHOTGUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_SHOTGUN);
			break;
		case PLAYERCLASS_HERO:
			if(GameController()->AreTurretsEnabled())
				m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_SHOTGUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_GRENADE);
			break;
		case PLAYERCLASS_NINJA:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
			m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
			m_pCharacter->GiveWeapon(WEAPON_LASER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
			break;
		case PLAYERCLASS_NONE:
			m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
			m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
			break;
	}

	if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		m_PositionLockTicksRemaining = Server()->TickSpeed() * SniperPositionLockTimeLimit;;
	}
	else
	{
		m_PositionLockTicksRemaining = 0;
	}

	if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		if(!m_pHeroFlag)
			m_pHeroFlag = new CHeroFlag(GameServer(), m_pPlayer->GetCID());
	}

	m_KillsProgression = 0;
	m_pCharacter->UnlockPosition();
}

void CInfClassHuman::DestroyChildEntities()
{
	if(!m_pCharacter)
	{
		return;
	}

	m_PositionLockTicksRemaining = 0;
	m_NinjaTargetTick = 0;
	m_NinjaTargetCID = -1;

	if(m_pHeroFlag)
	{
		// The flag removed in CInfClassCharacter::DestroyChildEntities()
		// delete m_pHeroFlag;
		m_pHeroFlag = nullptr;
	}
	m_pCharacter->UnlockPosition();
}

void CInfClassHuman::BroadcastWeaponState() const
{
	const int CurrentTick = Server()->Tick();
	int ClientVersion = Server()->GetClientInfclassVersion(GetCID());

	if(GetPlayerClass() == PLAYERCLASS_ENGINEER)
	{
		if(ClientVersion >= VERSION_INFC_160)
			return;

		CEngineerWall *pCurrentWall = NULL;
		for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(pCurrentWall)
		{
			int RemainingTicks = pCurrentWall->GetEndTick() - CurrentTick;
			int Seconds = 1 + RemainingTicks / Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Laser wall: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_MEDIC)
	{
		if(m_pCharacter->GetActiveWeapon() == WEAPON_LASER)
		{
			const int MIN_ZOMBIES = 4;
			const int DAMAGE_ON_REVIVE = 17;

			if(m_pCharacter->GetHealthArmorSum() <= DAMAGE_ON_REVIVE)
			{
				int MinHp = DAMAGE_ON_REVIVE + 1;
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("You need at least {int:MinHp} HP to revive a zombie"),
					"MinHp", &MinHp,
					NULL
				);
			}
			else if(GameController()->GetInfectedCount() <= MIN_ZOMBIES)
			{
				int MinZombies = MIN_ZOMBIES+1;
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("Too few zombies to revive anyone (less than {int:MinZombies})"),
					"MinZombies", &MinZombies,
					NULL
				);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_LOOPER)
	{
		if(ClientVersion >= VERSION_INFC_160)
			return;

		//Potential variable name conflict with engineerwall with pCurrentWall
		CLooperWall* pCurrentWall = NULL;
		for(CLooperWall *pWall = (CLooperWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(pCurrentWall)
		{
			int RemainingTicks = pCurrentWall->GetEndTick() - CurrentTick;
			int Seconds = 1 + RemainingTicks / Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Looper laser wall: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SOLDIER)
	{
		int NumBombs = 0;
		for(CSoldierBomb *pBomb = (CSoldierBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb*) pBomb->TypeNext())
		{
			if(pBomb->GetOwner() == m_pPlayer->GetCID())
				NumBombs += pBomb->GetNbBombs();
		}

		if(NumBombs)
		{
			GameServer()->SendBroadcast_Localization_P(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				NumBombs,
				_CP("Soldier", "One bomb left", "{int:NumBombs} bombs left"),
				"NumBombs", &NumBombs,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		int NumMines = 0;
		for(CScientistMine *pMine = (CScientistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE); pMine; pMine = (CScientistMine*) pMine->TypeNext())
		{
			if(pMine->GetOwner() == m_pPlayer->GetCID())
				NumMines++;
		}

		CWhiteHole* pCurrentWhiteHole = NULL;
		for(CWhiteHole *pWhiteHole = (CWhiteHole*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_WHITE_HOLE); pWhiteHole; pWhiteHole = (CWhiteHole*) pWhiteHole->TypeNext())
		{
			if(pWhiteHole->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentWhiteHole = pWhiteHole;
				break;
			}
		}

		if(m_BroadcastWhiteHoleReady + (2 * Server()->TickSpeed()) > Server()->Tick())
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("The white hole is available!"),
				NULL
			);
		}
		else if(NumMines > 0 && !pCurrentWhiteHole)
		{
			GameServer()->SendBroadcast_Localization_P(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME, NumMines,
				_P("One mine is active", "{int:NumMines} mines are active"),
				"NumMines", &NumMines,
				NULL
			);
		}
		else if(NumMines <= 0 && pCurrentWhiteHole)
		{
			int Seconds = 1+pCurrentWhiteHole->LifeSpan()/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("White hole: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
		else if(NumMines > 0 && pCurrentWhiteHole)
		{
			dynamic_string Buffer;
			Server()->Localization()->Format_LP(Buffer, GetPlayer()->GetLanguage(), NumMines,
				_P("One mine is active", "{int:NumMines} mines are active"),
				"NumMines", &NumMines,
				nullptr);
			Buffer.append("\n");
			int Seconds = 1+pCurrentWhiteHole->LifeSpan()/Server()->TickSpeed();
			Server()->Localization()->Format_L(Buffer, GetPlayer()->GetLanguage(),
				_("White hole: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				nullptr);
			GameServer()->SendBroadcast(GetCID(), Buffer.buffer(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
	{
		int NumMines = 0;
		for(CBiologistMine *pMine = (CBiologistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*) pMine->TypeNext())
		{
			if(pMine->GetOwner() == m_pPlayer->GetCID())
				NumMines++;
		}

		if(NumMines > 0)
		{
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_C("Biologist", "Mine activated"),
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_NINJA)
	{
		int TargetID = m_NinjaTargetCID;
		int CoolDown = m_NinjaTargetTick - Server()->Tick();

		const ClientsArray &ValidNinjaTargets = GameController()->GetValidNinjaTargets();

		if((CoolDown > 0))
		{
			int Seconds = 1 + CoolDown / Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_C("Ninja", "Next target in {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
		else if(TargetID >= 0)
		{
			GameServer()->SendBroadcast_Localization(GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_C("Ninja", "Target to eliminate: {str:PlayerName}"),
				"PlayerName", Server()->ClientName(TargetID),
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		if(m_pCharacter->PositionIsLocked())
		{
			int Seconds = 1+m_PositionLockTicksRemaining/Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_C("Sniper", "Position lock: {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				NULL
			);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		CMercenaryBomb *pCurrentBomb = nullptr;
		for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
		{
			if(pBomb->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentBomb = pBomb;
				break;
			}
		}

		if(pCurrentBomb)
		{
			float BombLevel = pCurrentBomb->m_Damage/static_cast<float>(Config()->m_InfMercBombs);

			if(m_pCharacter->GetActiveWeapon() == WEAPON_LASER)
			{
				if(BombLevel < 1.0)
				{
					dynamic_string Line1;
					Server()->Localization()->Format_L(Line1, GetPlayer()->GetLanguage(),
						_C("Mercenary", "Use the laser to upgrade the bomb"), NULL);

					dynamic_string Line2;
					Server()->Localization()->Format_L(Line2, GetPlayer()->GetLanguage(),
						_C("Mercenary", "Explosive yield: {percent:BombLevel}"), "BombLevel", &BombLevel, NULL);

					Line1.append("\n");
					Line1.append(Line2);

					GameServer()->AddBroadcast(GetPlayer()->GetCID(), Line1.buffer(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME);
				}
				else
				{
					GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
						_C("Mercenary", "The bomb is fully upgraded.\n"
						  "There is nothing to do with the laser."), NULL
					);
				}
			}
			else
			{
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_C("Mercenary", "Explosive yield: {percent:BombLevel}"),
					"BombLevel", &BombLevel,
					NULL
				);
			}
		}
		else
		{
			if(m_pCharacter->GetActiveWeapon() == WEAPON_LASER)
			{
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_C("Mercenary", "Use the hammer to place a bomb and\n"
					  "then use the laser to upgrade it"),
					NULL
				);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		//Search for flag
		int CoolDown = m_pHeroFlag ? m_pHeroFlag->GetSpawnTick() - CurrentTick : 0;

		if(m_pCharacter->GetActiveWeapon() == WEAPON_HAMMER)
		{
			int Turrets = m_TurretCount;
			if(!GameController()->AreTurretsEnabled())
			{
				GameServer()->SendBroadcast_Localization(GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("The turrets are not allowed by the game rules (at least right now)."),
					nullptr);
			}
			else if(Turrets > 0)
			{
				int MaxTurrets = Config()->m_InfTurretMaxPerPlayer;
				if(MaxTurrets == 1)
				{
					GameServer()->SendBroadcast_Localization(GetCID(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
						_("You have a turret. Use the hammer to place it."),
						nullptr
					);
				}
				else
				{
					GameServer()->SendBroadcast_Localization_P(GetCID(),
						BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME, Turrets,
						_("You have {int:NumTurrets} of {int:MaxTurrets} turrets. Use the hammer to place one."),
						"NumTurrets", &Turrets,
						"MaxTurrets", &MaxTurrets,
						nullptr
					);
				}
			}
			else
			{
				GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
					BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
					_("You don't have a turret to place"),
					nullptr
				);
			}
		}
		else if(CoolDown > 0 && (ClientVersion < VERSION_INFC_140)) // 140 introduces native timers for Hero
		{
			int Seconds = 1 + CoolDown / Server()->TickSpeed();
			GameServer()->SendBroadcast_Localization(GetPlayer()->GetCID(),
				BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
				_("Next flag in {sec:RemainingTime}"),
				"RemainingTime", &Seconds,
				nullptr
			);
		}
	}
}

void CInfClassHuman::OnNinjaTargetKiller(bool Assisted)
{
	GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, _("You have eliminated your target, +2 points"), NULL);
	Server()->RoundStatistics()->OnScoreEvent(GetCID(), SCOREEVENT_KILL_TARGET, GetPlayerClass(), Server()->ClientName(GetCID()), GameServer()->Console());

	if(m_pCharacter)
	{
		m_pCharacter->GiveNinjaBuf();
		m_pCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		GameServer()->SendEmoticon(GetCID(), EMOTICON_MUSIC);

		if(!Assisted)
		{
			m_pCharacter->Heal(4);
		}
	}

	int PlayerCounter = Server()->GetActivePlayerCount();
	int CooldownTicks = Server()->TickSpeed()*(10 + 3 * maximum(0, 16 - PlayerCounter));
	m_NinjaTargetCID = -1;
	m_NinjaTargetTick = Server()->Tick() + CooldownTicks;
}

void CInfClassHuman::SnapHero(int SnappingClient)
{
	if(SnappingClient != m_pPlayer->GetCID())
		return;

	const int CurrentTick = Server()->Tick();

	if(m_pHeroFlag && Config()->m_InfHeroFlagIndicator)
	{
		int TickLimit = m_pPlayer->m_LastActionMoveTick + Config()->m_InfHeroFlagIndicatorTime * Server()->TickSpeed();
		TickLimit = maximum(TickLimit, m_pHeroFlag->GetSpawnTick());

		if(CurrentTick > TickLimit)
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_pCharacter->GetCursorID(), sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			float Angle = atan2f(m_pHeroFlag->GetPos().y - GetPos().y, m_pHeroFlag->GetPos().x - GetPos().x);
			vec2 vecDir = vec2(cos(Angle), sin(Angle));
			vec2 Indicator = GetPos() + vecDir * 84.0f;
			vec2 IndicatorM = GetPos() - vecDir * 84.0f;

			// display laser beam for 0.5 seconds
			int TickShowBeamTime = Server()->TickSpeed() * 0.5;
			long TicksInactive = TickShowBeamTime - (Server()->Tick() - TickLimit);
			if(g_Config.m_InfHeroFlagIndicatorTime > 0 && TicksInactive > 0)
			{
				Indicator = IndicatorM + vecDir * 168.0f * (1.0f - (TicksInactive / (float)TickShowBeamTime));

				pObj->m_X = (int)Indicator.x;
				pObj->m_Y = (int)Indicator.y;
				pObj->m_FromX = (int)IndicatorM.x;
				pObj->m_FromY = (int)IndicatorM.y;
				if(TicksInactive < 4)
				{
					pObj->m_StartTick = Server()->Tick() - (6 - TicksInactive);
				}
				else
				{
					pObj->m_StartTick = Server()->Tick() - 3;
				}
			}
			else
			{
				pObj->m_X = (int)Indicator.x;
				pObj->m_Y = (int)Indicator.y;
				pObj->m_FromX = pObj->m_X;
				pObj->m_FromY = pObj->m_Y;
				pObj->m_StartTick = Server()->Tick();
			}
		}
	}
}

void CInfClassHuman::SnapEngineer(int SnappingClient)
{
	const CInfClassPlayer *pDestClient = GameController()->GetPlayer(SnappingClient);
	bool ShowFirstShot = (SnappingClient == -1) || (pDestClient && pDestClient->IsHuman());

	if(ShowFirstShot && !m_FirstShot)
	{
		CEngineerWall *pCurrentWall = NULL;
		for(CEngineerWall *pWall = (CEngineerWall *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall *)pWall->TypeNext())
		{
			if(pWall->GetOwner() == GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(!pCurrentWall)
		{
			SSnapContext Context;
			Context.Version = GameServer()->GetClientVersion(SnappingClient);

			GameController()->SnapLaserObject(Context, m_BarrierHintIDs.First(), m_FirstShotCoord, m_FirstShotCoord, Server()->Tick(), GetCID());
		}
	}
}

void CInfClassHuman::SnapLooper(int SnappingClient)
{
	const CInfClassPlayer *pDestClient = GameController()->GetPlayer(SnappingClient);
	bool ShowFirstShot = (SnappingClient == -1) || (pDestClient && pDestClient->IsHuman());

	if(ShowFirstShot && !m_FirstShot)
	{
		CLooperWall *pCurrentWall = NULL;
		for(CLooperWall *pWall = (CLooperWall *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall *)pWall->TypeNext())
		{
			if(pWall->GetOwner() == GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(!pCurrentWall)
		{
			for(int i = 0; i < 2; i++)
			{
				CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_BarrierHintIDs[i], sizeof(CNetObj_Laser)));

				if(!pObj)
					return;

				pObj->m_X = (int)m_FirstShotCoord.x - CLooperWall::THICKNESS * i + (CLooperWall::THICKNESS * 0.5);
				pObj->m_Y = (int)m_FirstShotCoord.y;
				pObj->m_FromX = (int)m_FirstShotCoord.x - CLooperWall::THICKNESS * i + (CLooperWall::THICKNESS * 0.5);
				pObj->m_FromY = (int)m_FirstShotCoord.y;
				pObj->m_StartTick = Server()->Tick();
			}
		}
	}
}

void CInfClassHuman::SnapScientist(int SnappingClient)
{
	if(SnappingClient != m_pPlayer->GetCID())
		return;

	if(m_pCharacter->GetActiveWeapon() == WEAPON_GRENADE)
	{
		vec2 PortalPos;

		if(FindPortalPosition(&PortalPos))
		{
			const int CursorID = GameController()->GetPlayerOwnCursorID(GetCID());
			GameController()->SendHammerDot(PortalPos, CursorID);
		}
	}
}

void CInfClassHuman::ActivateNinja(WeaponFireContext *pFireContext)
{
	if(m_pCharacter->m_DartLeft || m_pCharacter->m_InWater)
	{
		if(!m_pCharacter->m_InWater)
			m_pCharacter->m_DartLeft--;

		m_pCharacter->ResetNinjaHits();

		m_pCharacter->m_DartDir = GetDirection();
		m_pCharacter->m_DartLifeSpan = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
		m_pCharacter->m_DartOldVelAmount = length(m_pCharacter->m_Core.m_Vel);

		GameServer()->CreateSound(GetPos(), SOUND_NINJA_HIT);
	}
}

void CInfClassHuman::PlaceEngineerWall(WeaponFireContext *pFireContext)
{
	for(TEntityPtr<CEngineerWall> pWall = GameWorld()->FindFirst<CEngineerWall>(); pWall; ++pWall)
	{
		if(pWall->GetOwner() == GetCID())
			GameWorld()->DestroyEntity(pWall);
	}

	if(m_FirstShot)
	{
		m_FirstShot = false;
		m_FirstShotCoord = GetPos();
	}
	else if(distance(m_FirstShotCoord, GetPos()) > 10.0)
	{
		m_FirstShot = true;

		for(int i = 0; i < 15; i++)
		{
			vec2 TestPos = m_FirstShotCoord + (GetPos() - m_FirstShotCoord) * (static_cast<float>(i) / 14.0f);
			if(!GameController()->HumanWallAllowedInPos(TestPos))
			{
				pFireContext->FireAccepted = false;
				pFireContext->NoAmmo = true;
				break;
			}
		}

		if(pFireContext->FireAccepted)
		{
			new CEngineerWall(GameServer(), m_FirstShotCoord, GetPos(), GetCID());
			GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
		}
	}
}

void CInfClassHuman::PlaceLooperWall(WeaponFireContext *pFireContext)
{
	for(CLooperWall *pWall = (CLooperWall *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall *)pWall->TypeNext())
	{
		if(pWall->GetOwner() == GetCID())
			GameWorld()->DestroyEntity(pWall);
	}

	if(m_FirstShot)
	{
		m_FirstShot = false;
		m_FirstShotCoord = GetPos();
	}
	else if(distance(m_FirstShotCoord, GetPos()) > 10.0)
	{
		m_FirstShot = true;

		for(int i = 0; i < 15; i++)
		{
			vec2 TestPos = m_FirstShotCoord + (GetPos() - m_FirstShotCoord) * (static_cast<float>(i) / 14.0f);
			if(!GameController()->HumanWallAllowedInPos(TestPos))
			{
				pFireContext->FireAccepted = false;
				pFireContext->NoAmmo = true;
				break;
			}
		}

		if(pFireContext->FireAccepted)
		{
			new CLooperWall(GameServer(), m_FirstShotCoord, GetPos(), GetCID());
			GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
		}
	}
}

void CInfClassHuman::FireSoldierBomb(WeaponFireContext *pFireContext)
{
	vec2 ProjStartPos = GetPos() + GetDirection() * GetProximityRadius() * 0.75f;

	for(CSoldierBomb *pBomb = (CSoldierBomb *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb *)pBomb->TypeNext())
	{
		if(pBomb->GetOwner() == GetCID())
		{
			pBomb->Explode();
			return;
		}
	}

	new CSoldierBomb(GameServer(), ProjStartPos, GetCID());
	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
}

void CInfClassHuman::FireMercenaryBomb(WeaponFireContext *pFireContext)
{
	CMercenaryBomb *pCurrentBomb = nullptr;
	for(CMercenaryBomb *pBomb = (CMercenaryBomb *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb *)pBomb->TypeNext())
	{
		if(pBomb->GetOwner() == GetCID())
		{
			pCurrentBomb = pBomb;
			break;
		}
	}

	if(pCurrentBomb)
	{
		float Distance = distance(pCurrentBomb->GetPos(), GetPos());
		const float SafeDistance = 16;
		if(pCurrentBomb->ReadyToExplode() || Distance > CMercenaryBomb::GetMaxRadius() + SafeDistance)
		{
			pCurrentBomb->Explode();
		}
		else
		{
			const float UpgradePoints = Distance <= CMercenaryBomb::GetMaxRadius() ? 2 : 0.5;
			pCurrentBomb->Upgrade(UpgradePoints);
		}
	}
	else
	{
		new CMercenaryBomb(GameServer(), GetPos(), GetCID());
	}

	m_pCharacter->SetReloadDuration(0.25f);
}

void CInfClassHuman::PlaceScientistMine(WeaponFireContext *pFireContext)
{
	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos() + Direction * GetProximityRadius() * 0.75f;

	bool FreeSpace = true;
	int NbMine = 0;

	int OlderMineTick = Server()->Tick() + 1;
	CScientistMine *pOlderMine = 0;
	CScientistMine *pIntersectMine = 0;

	CScientistMine *p = (CScientistMine *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE);
	while(p)
	{
		float d = distance(p->GetPos(), ProjStartPos);

		if(p->GetOwner() == GetCID())
		{
			if(OlderMineTick > p->m_StartTick)
			{
				OlderMineTick = p->m_StartTick;
				pOlderMine = p;
			}
			NbMine++;

			if(d < 2.0f * g_Config.m_InfMineRadius)
			{
				if(pIntersectMine)
					FreeSpace = false;
				else
					pIntersectMine = p;
			}
		}
		else if(d < 2.0f * g_Config.m_InfMineRadius)
			FreeSpace = false;

		p = (CScientistMine *)p->TypeNext();
	}

	if(!FreeSpace)
		return;

	if(pIntersectMine) // Move the mine
		GameWorld()->DestroyEntity(pIntersectMine);
	else if(NbMine >= g_Config.m_InfMineLimit && pOlderMine)
		GameWorld()->DestroyEntity(pOlderMine);

	new CScientistMine(GameServer(), ProjStartPos, GetCID());

	m_pCharacter->SetReloadDuration(0.5f);
}

void CInfClassHuman::PlaceTurret(WeaponFireContext *pFireContext)
{
	const vec2 Direction = GetDirection();

	if(GameController()->AreTurretsEnabled() && m_TurretCount)
	{
		if(Config()->m_InfTurretEnableLaser)
		{
			new CTurret(GameServer(), GetPos(), GetCID(), Direction, CTurret::LASER);
		}
		else if(Config()->m_InfTurretEnablePlasma)
		{
			new CTurret(GameServer(), GetPos(), GetCID(), Direction, CTurret::PLASMA);
		}

		GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
		m_TurretCount--;
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "placed turret, %i left", m_TurretCount);
		GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, aBuf, NULL);
	}
	else
	{
		pFireContext->NoAmmo = true;
	}
}

void CInfClassHuman::OnMercGrenadeFired(WeaponFireContext *pFireContext)
{
	float BaseAngle = angle(GetDirection());

	// Find bomb
	bool BombFound = false;

	for(TEntityPtr<CScatterGrenade> pGrenade = GameWorld()->FindFirst<CScatterGrenade>(); pGrenade; ++pGrenade)
	{
		if(pGrenade->GetOwner() != GetCID())
			continue;
		pGrenade->Explode();
		BombFound = true;
	}

	if(BombFound)
	{
		pFireContext->AmmoConsumed = 0;
		pFireContext->NoAmmo = false;
		return;
	}

	if(pFireContext->NoAmmo)
		return;

	int ShotSpread = 2;

	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(ShotSpread * 2 + 1);

	for(int i = -ShotSpread; i <= ShotSpread; ++i)
	{
		float a = BaseAngle + random_float() / 3.0f;

		CScatterGrenade *pProj = new CScatterGrenade(GameServer(), GetCID(), GetPos(), vec2(cosf(a), sinf(a)));

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		for(unsigned i = 0; i < sizeof(CNetObj_Projectile) / sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());
	}

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);

	m_pCharacter->SetReloadDuration(0.25f);
}

void CInfClassHuman::OnMedicGrenadeFired(WeaponFireContext *pFireContext)
{
	int HealingExplosionRadius = 4;
	new CGrowingExplosion(GameServer(), GetPos(), GetDirection(), GetCID(), HealingExplosionRadius, GROWING_EXPLOSION_EFFECT::HEAL_HUMANS);

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
}

void CInfClassHuman::OnBlindingLaserFired(WeaponFireContext *pFireContext)
{
	new CBlindingLaser(GameContext(), GetPos(), GetDirection(), GetCID());
}

void CInfClassHuman::OnBiologistLaserFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->AmmoAvailable < 10)
	{
		pFireContext->FireAccepted = false;
		pFireContext->NoAmmo = true;
		return;
	}

	const float BioLaserMaxLength = 400.0f;
	vec2 To = GetPos() + GetDirection() * BioLaserMaxLength;
	bool CanFire = GameServer()->Collision()->IntersectLine(GetPos(), To, 0x0, &To);

	if(!CanFire)
	{
		pFireContext->FireAccepted = false;
		pFireContext->NoAmmo = true;
		return;
	}

	for(TEntityPtr<CBiologistMine> pMine = GameWorld()->FindFirst<CBiologistMine>(); pMine; ++pMine)
	{
		if(pMine->GetOwner() != GetCID()) continue;
			GameWorld()->DestroyEntity(pMine);
	}

	int Lasers = 15;
	int PerLaserDamage = 10;
	new CBiologistMine(GameServer(), GetPos(), To, GetCID(), Lasers, PerLaserDamage);
	pFireContext->AmmoConsumed = pFireContext->AmmoAvailable;
}

void CInfClassHuman::OnMercLaserFired(WeaponFireContext *pFireContext)
{
	CMercenaryBomb *pCurrentBomb = nullptr;
	for(TEntityPtr<CMercenaryBomb> pBomb = GameWorld()->FindFirst<CMercenaryBomb>(); pBomb; ++pBomb)
	{
		if(pBomb->GetOwner() == GetCID())
		{
			pCurrentBomb = pBomb;
			break;
		}
	}

	if(!pCurrentBomb)
	{
		pFireContext->FireAccepted = false;
	}
	else
	{
		new CMercenaryLaser(GameServer(), GetPos(), GetDirection(), GameServer()->Tuning()->m_LaserReach, GetCID());
	}
}

bool CInfClassHuman::PositionLockAvailable() const
{
	const int TickSpeed = GameContext()->Server()->TickSpeed();
	if(m_PositionLockTicksRemaining < TickSpeed * (SniperPositionLockTimeLimit - 1))
	{
		return false;
	}

	if(GetPos().y <= -600)
	{
		return false;
	}

	if(m_pCharacter->IsPassenger())
	{
		return false;
	}

	return true;
}

bool CInfClassHuman::FindPortalPosition(vec2 *pPosition)
{
	vec2 PortalShift = vec2(m_pCharacter->m_Input.m_TargetX, m_pCharacter->m_Input.m_TargetY);
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
			*pPosition = PortalPos;
			return true;
		}

		Iterator -= 4.0f;
	}

	return false;
}

void CInfClassHuman::OnSlimeEffect(int Owner)
{
	int Count = Config()->m_InfSlimePoisonDamage;
	Poison(Count, Owner, DAMAGE_TYPE::SLUG_SLIME);
}

bool CInfClassHuman::HasWhiteHole() const
{
	return m_HasWhiteHole;
}

void CInfClassHuman::GiveWhiteHole()
{
	m_HasWhiteHole = true;
	m_BroadcastWhiteHoleReady = Server()->Tick();
	GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, _("The white hole is ready, use the laser rifle to disrupt space-time"), nullptr);
}

void CInfClassHuman::RemoveWhiteHole()
{
	m_HasWhiteHole = false;
	m_pCharacter->SetSuperWeaponIndicatorEnabled(false);
}

void CInfClassHuman::OnHeroFlagTaken(CInfClassCharacter *pHero)
{
	if(!m_pCharacter)
		return;

	m_pCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
	GameServer()->SendEmoticon(GetCID(), EMOTICON_MUSIC);

	if(pHero != m_pCharacter)
	{
		m_pCharacter->GiveGift(GIFT_HEROFLAG);
		return;
	}

	{
		// Gift to self
		m_pCharacter->SetHealthArmor(10, 10);
		m_pCharacter->GiveWeapon(WEAPON_GUN, -1);
		m_pCharacter->GiveWeapon(WEAPON_SHOTGUN, -1);
		m_pCharacter->GiveWeapon(WEAPON_GRENADE, -1);
		m_pCharacter->GiveWeapon(WEAPON_LASER, -1);

		if(GameController()->AreTurretsEnabled())
		{
			int NewNumberOfTurrets = clamp<int>(m_TurretCount + Config()->m_InfTurretGive, 0, Config()->m_InfTurretMaxPerPlayer);
			if(m_TurretCount != NewNumberOfTurrets)
			{
				if(m_TurretCount == 0)
					m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);

				m_TurretCount = NewNumberOfTurrets;

				GameServer()->SendChatTarget_Localization_P(GetCID(), CHATCATEGORY_SCORE, m_TurretCount,
					_P("You have {int:NumTurrets} turret available, use the Hammer to place it",
						"You have {int:NumTurrets} turrets available, use the Hammer to place it"),
					"NumTurrets", &m_TurretCount,
					nullptr);
			}
		}
	}

	// Only increase your *own* character health when on cooldown
	if(!GameController()->HeroGiftAvailable())
		return;

	GameController()->OnHeroFlagCollected(GetCID());

	// Find other players
	for(TEntityPtr<CInfClassCharacter> p = GameWorld()->FindFirst<CInfClassCharacter>(); p; ++p)
	{
		if(p->IsZombie() || p == m_pCharacter)
			continue;

		CInfClassHuman *pHumanClass = CInfClassHuman::GetInstance(p);
		pHumanClass->OnHeroFlagTaken(m_pCharacter);
	}
}

void CInfClassHuman::OnWhiteHoleSpawned(const CWhiteHole *pWhiteHole)
{
	m_KillsProgression = 0;
	m_ResetKillsTime = pWhiteHole->LifeSpan() + Server()->TickSpeed() * 3;
}
