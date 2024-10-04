#include "infected.h"
#include "game/generated/protocol.h"
#include "game/server/entity.h"
#include "game/server/gameworld.h"
#include "game/server/infclass/classes/infcplayerclass.h"
#include "game/server/infclass/entities/slug-slime.h"
#include "game/server/infclass/entities/turret.h"

#include <game/generated/server_data.h>

#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/damage_context.h>
#include <game/infclass/damage_type.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>
#include <game/server/teeinfo.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassInfected, MAX_CLIENTS)

CInfClassInfected::CInfClassInfected(CInfClassPlayer *pPlayer)
	: CInfClassPlayerClass(pPlayer)
{
	SetNormalEmote(EMOTE_ANGRY);
}

CInfClassInfected *CInfClassInfected::GetInstance(CInfClassCharacter *pCharacter)
{
	CInfClassPlayerClass *pClass = pCharacter ? pCharacter->GetClass() : nullptr;
	if(pClass && pClass->IsZombie())
	{
		return static_cast<CInfClassInfected*>(pClass);
	}
	
	return nullptr;
}

SkinGetter CInfClassInfected::GetSkinGetter() const
{
	return CInfClassInfected::SetupSkin;
}

void CInfClassInfected::SetupSkinContext(CSkinContext *pOutput, bool ForSameTeam) const
{
	pOutput->PlayerClass = GetPlayerClass();
	switch(GetPlayerClass())
	{
	case EPlayerClass::Spider:
		pOutput->ExtraData1 = ForSameTeam ? m_HookOnTheLimit : 0;
		break;
	case EPlayerClass::Ghoul:
		pOutput->ExtraData1 = GetGhoulPercent() * 100;
		break;
	case EPlayerClass::Voodoo:
		pOutput->ExtraData1 = m_VoodooAboutToDie;
		break;
	default:
		pOutput->ExtraData1 = 0;
		break;
	}

	if(m_pPlayer && (m_pPlayer->GetMaxHP() > 20))
	{
		pOutput->Highlight = true;
	}
}

bool CInfClassInfected::SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion)
{
	switch(Context.PlayerClass)
	{
	case EPlayerClass::Smoker:
		pOutput->UseCustomColor = 1;
		pOutput->pSkinName = "cammostripes";
		pOutput->ColorBody = 3866368;
		pOutput->ColorFeet = 65414;
		break;
	case EPlayerClass::Boomer:
		pOutput->pSkinName = "saddo";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 3866368;
		pOutput->ColorFeet = 65414;
		break;
	case EPlayerClass::Hunter:
		pOutput->pSkinName = "warpaint";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 3866368;
		pOutput->ColorFeet = 65414;
		break;
	case EPlayerClass::Bat:
		pOutput->pSkinName = "limekitty";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 3866368;
		pOutput->ColorFeet = 2866368;

		if(Context.Highlight)
		{
			pOutput->ColorFeet = 16776744;
		}
		break;
	case EPlayerClass::Ghost:
		pOutput->pSkinName = "twintri";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 3866368;
		pOutput->ColorFeet = 65414;
		break;
	case EPlayerClass::Spider:
		pOutput->pSkinName = "pinky";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 3866368;
		if(Context.ExtraData1)
		{
			pOutput->ColorFeet = 16776960; // Dark red
		}
		else
		{
			pOutput->ColorFeet = 65414;
		}
		break;
	case EPlayerClass::Ghoul:
		pOutput->pSkinName = "cammo";
		pOutput->UseCustomColor = 1;
		{
			float Percent = Context.ExtraData1 / 100.0f;
			int Hue = 58 * (1.0f - Percent * 0.8f);
			pOutput->ColorBody = (Hue << 16) + (255 << 8);
		}
		pOutput->ColorFeet = 65414;
		break;
	case EPlayerClass::Slug:
		pOutput->pSkinName = "coala";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 3866368;
		pOutput->ColorFeet = 65414;
		break;
	case EPlayerClass::Voodoo:
		pOutput->pSkinName = "bluestripe";
		pOutput->UseCustomColor = 1;
		if(!Context.ExtraData1)
		{
			pOutput->ColorBody = 3866368;
		}
		else
		{
			pOutput->ColorBody = 6183936; // grey-green
		}
		pOutput->ColorFeet = 65414;
		break;
	case EPlayerClass::Undead:
		pOutput->pSkinName = "redstripe";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 3014400;
		pOutput->ColorFeet = 13168;
		break;
	case EPlayerClass::Witch:
		pOutput->pSkinName = "redbopp";
		pOutput->UseCustomColor = 1;
		pOutput->ColorBody = 16776744;
		pOutput->ColorFeet = 13168;
		break;
	default:
		return false;
	}

	return true;
}

void CInfClassInfected::ResetNormalEmote()
{
	SetNormalEmote(EMOTE_ANGRY);
}

int CInfClassInfected::GetJumps() const
{
	switch(GetPlayerClass())
	{
	case EPlayerClass::Hunter:
		return 3;
	case EPlayerClass::Bat:
		return Config()->m_InfBatAirjumpLimit;
	default:
		return 2;
	}
}

void CInfClassInfected::OnPlayerSnap(int SnappingClient, int InfClassVersion)
{
	if(InfClassVersion < VERSION_INFC_140)
	{
		// CNetObj_InfClassClassInfo introduced in v0.1.4
		return;
	}

	CNetObj_InfClassClassInfo *pClassInfo = Server()->SnapNewItem<CNetObj_InfClassClassInfo>(GetCid());
	if(!pClassInfo)
		return;
	pClassInfo->m_Class = toNetValue(GetPlayerClass());
	pClassInfo->m_Flags = 0;
	pClassInfo->m_Data1 = -1;

	if(GameController()->CanSeeDetails(SnappingClient, GetCid()))
	{
		if(m_pCharacter)
		{
			if(m_pCharacter->IsInvisible())
				pClassInfo->m_Flags |= INFCLASS_CLASSINFO_FLAG_IS_INVISIBLE;
		}
	}
}

bool CInfClassInfected::CanDie() const
{
	if ((GetPlayerClass() == EPlayerClass::Undead) && m_pCharacter->IsFrozen()) {
		return false;
	}
	if ((GetPlayerClass() == EPlayerClass::Voodoo) && m_VoodooAboutToDie) {
		return false;
	}

	return true;
}

bool CInfClassInfected::CanBeUnfreezed() const
{
	return Server()->Tick() > m_LaserWallTick + 1;
}

void CInfClassInfected::OnCharacterPreCoreTick()
{
	CInfClassPlayerClass::OnCharacterPreCoreTick();

	switch(GetPlayerClass())
	{
	case EPlayerClass::Ghost:
		GhostPreCoreTick();
		break;
	case EPlayerClass::Spider:
		SpiderPreCoreTick();
		break;
	default:
		break;
	}
}

void CInfClassInfected::OnCharacterTick()
{
	CInfClassPlayerClass::OnCharacterTick();

	if(GetPlayerClass() == EPlayerClass::Voodoo && m_VoodooAboutToDie)
	{
		// Delayed Death
		if (m_VoodooTimeAlive > 0)
			m_VoodooTimeAlive-=1000;
		else
			m_pCharacter->Die(m_VoodooDeathContext);

		// Display time left to live
		int Time = m_VoodooTimeAlive/Server()->TickSpeed();
		GameServer()->SendBroadcast_Localization(GetCid(), EBroadcastPriority::WEAPONSTATE, BROADCAST_DURATION_REALTIME,
			_C("Voodoo", "Staying alive for: {int:RemainingTime}"),
			"RemainingTime", &Time,
			NULL
		);
	}
	if(GetPlayerClass() == EPlayerClass::Spider)
	{
		const bool HookIsOnTheLimit = m_pCharacter->WebHookLength() > Config()->m_InfSpiderWebHookLength - 48.0f;
		SetHookOnLimit(HookIsOnTheLimit);
	}
}

void CInfClassInfected::OnCharacterTickPaused()
{
	CInfClassPlayerClass::OnCharacterTickPaused();

	++m_HookDmgTick;
}

void CInfClassInfected::OnCharacterPostCoreTick()
{
	CInfClassPlayerClass::OnCharacterPostCoreTick();

	int HookerPlayer = m_pCharacter->Core()->HookedPlayer();
	if(HookerPlayer >= 0)
	{
		CInfClassCharacter *pVictimChar = GameController()->GetCharacter(HookerPlayer);
		if(pVictimChar && pVictimChar->IsHuman())
		{
			float Rate = 1.0f;
			int Damage = 1;

			if(GetPlayerClass() == EPlayerClass::Smoker)
			{
				Rate = 0.5f;
				Damage = g_Config.m_InfSmokerHookDamage;
			}

			if(m_HookDmgTick + Server()->TickSpeed() * Rate < Server()->Tick())
			{
				m_HookDmgTick = Server()->Tick();
				pVictimChar->TakeDamage(vec2(0.0f, 0.0f), Damage, GetCid(), EDamageType::DRYING_HOOK);
				if(HasDrainingHook())
				{
					m_pCharacter->Heal(Damage);
				}
			}
		}
	}
}

void CInfClassInfected::OnCharacterTickDeferred()
{
	const int Tick = Server()->Tick();
	if(m_SlimeEffectTicks)
	{
		if(Tick >= m_SlimeLastHealTick + (Server()->TickSpeed() / Config()->m_InfSlimeHealRate))
		{
			if(m_pCharacter->GetHealthArmorSum() < Config()->m_InfSlimeMaxHeal)
			{
				m_pCharacter->Heal(1, GetCid());
			}
			m_SlimeLastHealTick = Tick;
		}
		m_SlimeEffectTicks--;
	}
}

void CInfClassInfected::OnCharacterSnap(int SnappingClient)
{
	const vec2 Pos = m_pCharacter->GetPos();

	if(GetPlayerClass() == EPlayerClass::Witch)
	{
		CNetObj_Flag *pFlag = Server()->SnapNewItem<CNetObj_Flag>(m_pCharacter->GetFlagId());
		if(!pFlag)
			return;

		pFlag->m_X = Pos.x;
		pFlag->m_Y = Pos.y;
		pFlag->m_Team = TEAM_RED;
	}

	if(SnappingClient == m_pPlayer->GetCid())
	{
		switch(GetPlayerClass())
		{
		case EPlayerClass::Witch:
		{
			if(m_pCharacter->GetActiveWeapon() == WEAPON_HAMMER)
			{
				vec2 SpawnPos;
				if(FindWitchSpawnPosition(SpawnPos))
				{
					const int CursorId = GameController()->GetPlayerOwnCursorId(GetCid());
					GameController()->SendHammerDot(SpawnPos, CursorId);
				}
			}
			break;
		}
		default:
			break;
		}
	}
	else
	{
		const CInfClassPlayer *pDestClient = GameController()->GetPlayer(SnappingClient);
		if(pDestClient && pDestClient->IsInGame() && pDestClient->IsInfected())
		{
			if(m_pCharacter->GetHealthArmorSum() < 10)
			{
				CNetObj_Pickup *pP = Server()->SnapNewItem<CNetObj_Pickup>(m_pCharacter->GetHeartId());
				if(!pP)
					return;

				pP->m_X = Pos.x;
				pP->m_Y = Pos.y - 60.0;
				pP->m_Type = POWERUP_HEALTH;
				pP->m_Subtype = 0;
			}
		}
	}
}

void CInfClassInfected::OnCharacterSpawned(const SpawnContext &Context)
{
	CInfClassPlayerClass::OnCharacterSpawned(Context);

	m_HookDmgTick = 0;
	m_SlimeEffectTicks = 0;
	m_SlimeLastHealTick = 0;
	m_LaserWallTick = 0;

	if(Context.SpawnType == SpawnContext::MapSpawn)
	{
		if(GetPlayerClass() == EPlayerClass::Ghost)
		{
			m_pCharacter->MakeInvisible();
		}
	}
}

void CInfClassInfected::OnCharacterDeath(EDamageType DamageType)
{
	CInfClassPlayerClass::OnCharacterDeath(DamageType);

	if(GetPlayerClass() == EPlayerClass::Ghoul)
	{
		IncreaseGhoulLevel(-20);
		UpdateSkin();
	}

	if(GetPlayerClass() == EPlayerClass::Boomer)
	{
		bool CanExplode = true;

		if(DamageType == EDamageType::GAME)
			CanExplode = false;

		if(m_pCharacter->IsFrozen())
			CanExplode = false;

		if(m_pCharacter->IsInLove() && (DamageType == EDamageType::KILL_COMMAND))
			CanExplode = false;

		if(CanExplode)
		{
			DoBoomerExplosion();
		}
	}
}

void CInfClassInfected::OnHookAttachedPlayer()
{
	m_LastSeenTick = Server()->Tick();
}

void CInfClassInfected::OnCharacterDamage(SDamageContext *pContext)
{
	m_LastSeenTick = Server()->Tick();

	switch(GetPlayerClass())
	{
	case EPlayerClass::Hunter:
		if(pContext->DamageType == EDamageType::MEDIC_SHOTGUN)
		{
			pContext->Force = vec2(0, 0);
		}
		break;
	case EPlayerClass::Ghost:
		m_pCharacter->MakeVisible();
		break;
	case EPlayerClass::Ghoul:
	{
		int DamageAccepted = 0;
		for(int i = 0; i < pContext->Damage; i++)
		{
			if(random_prob(GetGhoulPercent() * 0.33))
				continue;

			DamageAccepted++;
		}
		pContext->Damage = DamageAccepted;
		break;
	}
	default:
		break;
	}
}

void CInfClassInfected::OnHammerFired(WeaponFireContext *pFireContext)
{
	if(GetPlayerClass() == EPlayerClass::Boomer)
	{
		if(!m_pCharacter->IsFrozen() && !m_pCharacter->IsInLove())
		{
			pFireContext->FireAccepted = false;
			m_pCharacter->Die(GetCid(), EDamageType::BOOMER_EXPLOSION);
		}

		return;
	}

	bool AutoFire = false;
	bool FullAuto = false;

	if(GetPlayerClass() == EPlayerClass::Slug)
		FullAuto = true;

	if(m_pCharacter->CountFireInput().m_Presses)
	{
	}
	else if(FullAuto && m_pCharacter->FireJustPressed() && pFireContext->AmmoAvailable)
	{
		AutoFire = true;
	}

	// reset objects Hit
	int Hits = 0;
	bool ShowAttackAnimation = false;

	if(!AutoFire)
	{
		const vec2 Direction = GetDirection();
		const vec2 ProjStartPos = GetPos() + Direction * GetHammerProjOffset();

		ShowAttackAnimation = true;

		if(GetPlayerClass() == EPlayerClass::Ghost)
		{
			m_pCharacter->MakeVisible();
			m_LastSeenTick = Server()->Tick();
		}

		// Lookup for humans
		ClientsArray Targets;
		GameController()->GetSortedTargetsInRange(ProjStartPos, GetHammerRange(), ClientsArray({GetCid()}), &Targets);

		for(const int TargetCid : Targets)
		{
			if (m_pCharacter->IsInLove())
			{
				break;
			}

			CInfClassCharacter *pTarget = GameController()->GetCharacter(TargetCid);

			if(GameServer()->Collision()->IntersectLineWeapon(ProjStartPos, pTarget->GetPos()))
				continue;

			vec2 Dir;
			if(length(pTarget->GetPos() - GetPos()) > 0.0f)
				Dir = normalize(pTarget->GetPos() - GetPos());
			else
				Dir = vec2(0.f, -1.f);

			vec2 Force = vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;

			if(pTarget->IsInfected())
			{
				if(pTarget->IsFrozen())
				{
					pTarget->TryUnfreeze(GetCid());
				}
				else
				{
					if(GameController()->GetRoundType() != ERoundType::Survival)
					{
						if(pTarget->Heal(4, GetCid()))
						{
							m_pCharacter->Heal(1);
						}
					}

					if(!pTarget->GetPlayer()->HookProtectionEnabled())
					{
						pTarget->AddVelocity(Force);

						if(-Force.y > 6.f)
						{
							const float HammerFlyHelperDuration = 20;
							pTarget->AddHelper(GetCid(), HammerFlyHelperDuration);
						}
					}
				}

				CInfClassInfected *pInfectedTarget = CInfClassInfected::GetInstance(pTarget);
				pInfectedTarget->m_LastSeenTick = Server()->Tick();
			}
			else
			{
				if(!pTarget->GetClass()->CanBeHit())
					continue;

				int Damage = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
				EDamageType DamageType = EDamageType::INFECTION_HAMMER;

				if(GetPlayerClass() == EPlayerClass::Bat)
				{
					Damage = Config()->m_InfBatDamage;
					DamageType = EDamageType::BITE;

					if(GameController()->GetRoundType() != ERoundType::Survival)
					{
						m_pCharacter->Heal(Config()->m_InfBatLifeSteal);
					}
				}

				pTarget->TakeDamage(Force, Damage, GetCid(), DamageType);
			}
			Hits++;

			CreateHammerHit(ProjStartPos, pTarget);
		}
	}

	if(!ShowAttackAnimation)
	{
		pFireContext->FireAccepted = false;
	}

	// if we Hit anything, we have to wait for the reload
	if(Hits)
	{
		pFireContext->ReloadInterval = 0.33f;
	}
	else if(GetPlayerClass() == EPlayerClass::Slug)
	{
		PlaceSlugSlime(pFireContext);
	}

	if(pFireContext->FireAccepted)
	{
		GameServer()->CreateSound(GetPos(), SOUND_HAMMER_FIRE);
	}
}

void CInfClassInfected::GiveClassAttributes()
{
	CInfClassPlayerClass::GiveClassAttributes();

	if(!m_pCharacter)
	{
		return;
	}

	switch(GetPlayerClass())
	{
	default:
		m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
		m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);
		break;
	}

	m_VoodooAboutToDie = false;
	m_VoodooTimeAlive = Server()->TickSpeed()*Config()->m_InfVoodooAliveTime;
}

void CInfClassInfected::BroadcastWeaponState() const
{
	if(GetPlayerClass() == EPlayerClass::Spider)
	{
		if(m_pCharacter->m_HookMode > 0)
		{
			GameServer()->SendBroadcast_Localization(GetCid(), EBroadcastPriority::WEAPONSTATE,
				BROADCAST_DURATION_REALTIME, _C("Spider", "Web mode enabled"), NULL);
		}
	}
	else if(GetPlayerClass() == EPlayerClass::Ghoul)
	{
		if(m_pPlayer->GetGhoulLevel())
		{
			float FodderInStomach = GetGhoulPercent();
			GameServer()->SendBroadcast_Localization(GetCid(), EBroadcastPriority::WEAPONSTATE,
				BROADCAST_DURATION_REALTIME,
				_C("Ghoul", "Stomach filled by {percent:FodderInStomach}"),
				"FodderInStomach", &FodderInStomach,
				NULL
			);
		}
	}
}

void CInfClassInfected::DoBoomerExplosion()
{
	float InnerRadius = 60.0f;
	float DamageRadius = 80.5f;
	int Damage = 14;
	float Force = 52;

	CInfClassCharacter *pBestBFTarget = nullptr;

	{
		CInfClassCharacter *apEnts[MAX_CLIENTS];
		int Num = GameWorld()->FindEntities(GetPos(), DamageRadius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		float ClosestCharacterDistance = DamageRadius * 2;

		const vec2 Pos = GetPos();
		for(int i = 0; i < Num; i++)
		{
			CInfClassCharacter *pTarget = apEnts[i];
			if(pTarget == m_pCharacter)
				continue;

			vec2 Diff = pTarget->GetPos() - Pos;
			if (Diff.x == 0.0f && Diff.y == 0.0f)
				Diff.y = -0.5f;
			vec2 ForceDir(0,1);
			const float Length = length(Diff);
			const float NormalizedLength = 1 - clamp((Length - InnerRadius) / (DamageRadius - InnerRadius), 0.0f, 1.0f);

			if(NormalizedLength)
				ForceDir = normalize(Diff);

			float DamageToDeal = 1 + ((Damage - 1) * NormalizedLength);
			pTarget->TakeDamage(ForceDir * Force * NormalizedLength, DamageToDeal, GetCid(), EDamageType::BOOMER_EXPLOSION);
			if(pTarget->IsInfected())
			{
				pTarget->TryUnfreeze(GetCid());
				if(!pTarget->IsFrozen())
				{
					pTarget->Heal(4 + DamageToDeal, GetCid());
				}
			}

			const CInfClassPlayer *pTargetPlayer = pTarget->GetPlayer();
			if(pTarget->IsInfected() || (pTargetPlayer && pTargetPlayer->IsInfectionStarted()))
			{
				const float BoomerHelperDuration = 30;
				pTarget->AddHelper(GetCid(), BoomerHelperDuration);

				if(Length < ClosestCharacterDistance)
				{
					pBestBFTarget = pTarget;
					ClosestCharacterDistance = Length;
				}
			}
		}

		const float InnerRadius2 = InnerRadius * InnerRadius;
		for(TEntityPtr<CTurret> pTarget = GameWorld()->FindFirst<CTurret>(); pTarget; ++pTarget)
		{
			if(!pTarget->IsMarkedForDestroy() && distance_squared(pTarget->GetPos(), Pos) <= InnerRadius2)
			{
				pTarget->Die(m_pCharacter);
			}
		}
	}

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_EXPLODE);
	GameController()->CreateExplosionDiskGfx(GetPos(), InnerRadius, DamageRadius, m_pPlayer->GetCid());

	if(pBestBFTarget)
	{
		m_pPlayer->SetFollowTarget(pBestBFTarget->GetCid(), 5.0);
		m_pPlayer->m_DieTick = Server()->Tick() + Server()->TickSpeed() * 10;
	}
}

void CInfClassInfected::PlaceSlugSlime(WeaponFireContext *pFireContext)
{
	static constexpr float MinDistance = 84.0f;
	vec2 CheckPos = GetPos() + GetDirection() * 64.0f;
	CSlugSlime *pSlime = PlaceSlime(CheckPos, MinDistance);

	if(pSlime)
	{
		int MaxLifeSpan = Server()->TickSpeed() * Config()->m_InfSlimeDuration;
		int NewEndTick = Server()->Tick() + MaxLifeSpan;

		if(pSlime->Replenish(GetCid(), NewEndTick))
		{
			pFireContext->FireAccepted = true;
		}
	}
}

CSlugSlime *CInfClassInfected::PlaceSlime(vec2 PlaceToPos, float MinDistance)
{
	if(m_pCharacter->IsInLove())
		return nullptr;

	if(!GameServer()->Collision()->IntersectLineWeapon(GetPos(), PlaceToPos, 0x0, &PlaceToPos))
	{
		return nullptr;
	}

	float DistanceToTheNearestSlime = MinDistance * 2;
	for(TEntityPtr<CSlugSlime> pSlime = GameWorld()->FindFirst<CSlugSlime>(); pSlime; ++pSlime)
	{
		const float d = distance(pSlime->GetPos(), GetPos());
		if(d < DistanceToTheNearestSlime)
		{
			DistanceToTheNearestSlime = d;
		}
		if(d <= MinDistance / 2)
		{
			return pSlime;
		}
	}

	if(DistanceToTheNearestSlime < MinDistance)
	{
		return nullptr;
	}

	CSlugSlime *pNewSlime = new CSlugSlime(GameServer(), PlaceToPos, GetCid());
	return pNewSlime;
}

bool CInfClassInfected::FindWitchSpawnPosition(vec2 &Position)
{
	float Angle = atan2f(m_pCharacter->m_Input.m_TargetY, m_pCharacter->m_Input.m_TargetX);//atan2f instead of atan2

	for(int i=0; i<32; i++)
	{
		float TestAngle;

		TestAngle = Angle + i * (pi / 32.0f);
		Position = GetPos() + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;

		if(GameController()->IsSpawnable(Position, EZoneTele::NoWitch))
			return true;

		TestAngle = Angle - i * (pi / 32.0f);
		Position = GetPos() + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;

		if(GameController()->IsSpawnable(Position, EZoneTele::NoWitch))
			return true;
	}

	return false;
}

void CInfClassInfected::SetHookOnLimit(bool OnLimit)
{
	if(m_HookOnTheLimit == OnLimit)
		return;

	m_HookOnTheLimit = OnLimit;
	UpdateSkin();
}

void CInfClassInfected::GhostPreCoreTick()
{
	constexpr float InvisibilityCooldown = 3.f;
	if(Server()->Tick() < m_LastSeenTick + InvisibilityCooldown * Server()->TickSpeed() || m_pCharacter->IsFrozen() || m_pCharacter->IsInSlowMotion())
	{
		m_pCharacter->MakeVisible();
	}
	else
	{
		bool HumanFound = !m_pCharacter->HasGrantedInvisibility() && HasHumansNearby();
		if(HumanFound)
		{
			m_pCharacter->MakeVisible();
			m_LastSeenTick = Server()->Tick();
		}
		else
		{
			m_pCharacter->MakeInvisible();
		}
	}
}

void CInfClassInfected::SpiderPreCoreTick()
{
	if(m_pCharacter->WebHookLength() > 48.0f && m_pCharacter->GetHookedPlayer() < 0)
	{
		// Find other players
		for(CInfClassCharacter *p = (CInfClassCharacter *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
		{
			if(p->IsInfected())
				continue;

			vec2 IntersectPos;
			if(!closest_point_on_line(GetPos(), m_pCharacter->GetHookPos(), p->GetPos(), IntersectPos))
				continue;

			float Len = distance(p->GetPos(), IntersectPos);
			if(Len < p->GetProximityRadius())
			{
				m_pCharacter->SetHookedPlayer(p->GetCid());
				// Note: typical Teeworlds clients restore m_HookMode = 1
				// via "Direct weapon selection" / m_LatestInput.m_WantedWeapon
				m_pCharacter->m_HookMode = 0;
				break;
			}
		}
	}
}

bool CInfClassInfected::HasDrainingHook() const
{
	switch(GetPlayerClass())
	{
	case EPlayerClass::Smoker:
		return true;
	default:
		return false;
	}
}

bool CInfClassInfected::HasHumansNearby()
{
	constexpr int GHOST_RADIUS = 11;
	constexpr int GHOST_SEARCHMAP_SIZE = (2 * GHOST_RADIUS + 1);

	// Search nearest human
	int cellGhostX = static_cast<int>(round(GetPos().x)) / 32;
	int cellGhostY = static_cast<int>(round(GetPos().y)) / 32;

	vec2 SeedPos = vec2(16.0f, 16.0f) + vec2(cellGhostX * 32.0, cellGhostY * 32.0);
	uint8_t aGhostSearchMap[GHOST_SEARCHMAP_SIZE * GHOST_SEARCHMAP_SIZE];

	constexpr uint8_t FlagVisitedBefore = 0x1;
	constexpr uint8_t FlagHasHuman = 0x2;
	constexpr uint8_t FlagVisited = 0x4;
	constexpr uint8_t FlagHasSolid = 0x8;

	for(int y = 0; y < GHOST_SEARCHMAP_SIZE; y++)
	{
		for(int x = 0; x < GHOST_SEARCHMAP_SIZE; x++)
		{
			vec2 Tile = SeedPos + vec2(32.0f * (x - GHOST_RADIUS), 32.0f * (y - GHOST_RADIUS));
			if(GameServer()->Collision()->CheckPoint(Tile))
			{
				aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] = FlagHasSolid;
			}
			else
			{
				aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] = 0x0;
			}
		}
	}
	for(TEntityPtr<CInfClassCharacter> p = GameWorld()->FindFirst<CInfClassCharacter>(); p; ++p)
	{
		if(p->IsInfected())
			continue;

		int cellHumanX = static_cast<int>(round(p->GetPos().x)) / 32;
		int cellHumanY = static_cast<int>(round(p->GetPos().y)) / 32;

		int cellX = cellHumanX - cellGhostX + GHOST_RADIUS;
		int cellY = cellHumanY - cellGhostY + GHOST_RADIUS;

		if(cellX >= 0 && cellX < GHOST_SEARCHMAP_SIZE && cellY >= 0 && cellY < GHOST_SEARCHMAP_SIZE)
		{
			const int TileIndex = cellY * GHOST_SEARCHMAP_SIZE + cellX;
			aGhostSearchMap[TileIndex] |= FlagHasHuman;
		}
	}
	aGhostSearchMap[GHOST_RADIUS * GHOST_SEARCHMAP_SIZE + GHOST_RADIUS] |= FlagVisitedBefore;
	for(int i = 0; i < GHOST_RADIUS; i++)
	{
		for(int y = 0; y < GHOST_SEARCHMAP_SIZE; y++)
		{
			for(int x = 0; x < GHOST_SEARCHMAP_SIZE; x++)
			{
				if((aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] & FlagVisitedBefore) || (aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] & FlagHasSolid))
				{
					// Skip solid tiles and the tiles we've checked before
					continue;
				}

				if(
					// Check if we visited top, left, right, top, or bottom neighboring tiles
					((x > 0 && (aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x - 1] & FlagVisitedBefore)) ||
						(x < GHOST_SEARCHMAP_SIZE - 1 && (aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x + 1] & FlagVisitedBefore)) ||
						(y > 0 && (aGhostSearchMap[(y - 1) * GHOST_SEARCHMAP_SIZE + x] & FlagVisitedBefore)) ||
						(y < GHOST_SEARCHMAP_SIZE - 1 && (aGhostSearchMap[(y + 1) * GHOST_SEARCHMAP_SIZE + x] & FlagVisitedBefore))) ||
					((random_prob(0.25f))
						// Check if we visited top/left, bottom/left, top/right, or bottom/right neighboring tiles
						&& ((x > 0 && y > 0 && (aGhostSearchMap[(y - 1) * GHOST_SEARCHMAP_SIZE + x - 1] & FlagVisitedBefore)) ||
							   (x > 0 && y < GHOST_SEARCHMAP_SIZE - 1 && (aGhostSearchMap[(y + 1) * GHOST_SEARCHMAP_SIZE + x - 1] & FlagVisitedBefore)) ||
							   (x < GHOST_SEARCHMAP_SIZE - 1 && y > 0 && (aGhostSearchMap[(y - 1) * GHOST_SEARCHMAP_SIZE + x + 1] & FlagVisitedBefore)) ||
							   (x < GHOST_SEARCHMAP_SIZE - 1 && y < GHOST_SEARCHMAP_SIZE - 1 && (aGhostSearchMap[(y + 1) * GHOST_SEARCHMAP_SIZE + x + 1] & FlagVisitedBefore)))))
				{
					aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] |= FlagVisited;
					//~ if((Server()->Tick()%5 == 0) && i == (Server()->Tick()/5)%GHOST_RADIUS)
					//~ {
					//~ vec2 HintPos = vec2(
					//~ 32.0f*(cellGhostX + (x - GHOST_RADIUS))+16.0f,
					//~ 32.0f*(cellGhostY + (y - GHOST_RADIUS))+16.0f);
					//~ GameServer()->CreateHammerHit(HintPos);
					//~ }
					if(aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] & FlagHasHuman)
					{
						return true;
					}
				}
			}
		}
		for(int y = 0; y < GHOST_SEARCHMAP_SIZE; y++)
		{
			for(int x = 0; x < GHOST_SEARCHMAP_SIZE; x++)
			{
				if(aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] & FlagVisited)
				{
					aGhostSearchMap[y * GHOST_SEARCHMAP_SIZE + x] |= FlagVisitedBefore;
				}
			}
		}
	}

	return false;
}

void CInfClassInfected::OnSlimeEffect(int Owner, int Damage, float DamageInterval)
{
	if(!m_pCharacter->IsAlive())
		return;

	const int Tick = Server()->Tick();
	m_pCharacter->SetEmote(EMOTE_HAPPY, Tick);

	const float SlimeEffectDuration = 2.0f;
	m_SlimeEffectTicks = SlimeEffectDuration * Server()->TickSpeed();
}

void CInfClassInfected::OnFloatingPointCollected(int Points)
{
	if(GetPlayerClass() != EPlayerClass::Ghoul)
		return;

	m_pCharacter->Heal(4);
	IncreaseGhoulLevel(Points);
}

void CInfClassInfected::OnLaserWall()
{
	m_LaserWallTick = Server()->Tick();
}

float CInfClassInfected::GetGhoulPercent() const
{
	return GetPlayer()->GetGhoulPercent();
}

void CInfClassInfected::IncreaseGhoulLevel(int Diff)
{
	GetPlayer()->IncreaseGhoulLevel(Diff);
}

int CInfClassInfected::GetGhoulLevel() const
{
	return GetPlayer()->GetGhoulLevel();
}

void CInfClassInfected::PrepareToDie(const DeathContext &Context, bool *pRefusedToDie)
{
	if(GetPlayerClass() == EPlayerClass::Undead)
	{
		m_pCharacter->Freeze(10.0, Context.Killer, FREEZEREASON_UNDEAD);
		m_pCharacter->SetHealthArmor(0, 0);
		*pRefusedToDie = true;
		return;
	}

	// Start counting down, delay killer message for later
	if(GetPlayerClass() == EPlayerClass::Voodoo)
	{
		if(m_VoodooAboutToDie)
		{
			if(m_VoodooTimeAlive > 0)
			{
				// If about to die, yet killed again, dont kill him either
				*pRefusedToDie = true;
			}

			// Return here to allow the death on voodoo time expired
			return;
		}
	}

	// Start counting down, delay killer message for later
	if(GetPlayerClass() == EPlayerClass::Voodoo)
	{
		m_VoodooAboutToDie = true;
		m_VoodooDeathContext = Context;
		UpdateSkin();

		*pRefusedToDie = true;
	}
}
