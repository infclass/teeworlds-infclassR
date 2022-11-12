#include "infected.h"
#include "game/server/entity.h"
#include "game/server/gameworld.h"
#include "game/server/infclass/classes/infcplayerclass.h"
#include "game/server/infclass/entities/slug-slime.h"

#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/damage_context.h>
#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>
#include <game/server/teeinfo.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassInfected, MAX_CLIENTS)

CInfClassInfected::CInfClassInfected(CInfClassPlayer *pPlayer)
	: CInfClassPlayerClass(pPlayer)
{
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

void CInfClassInfected::SetupSkinContext(CSkinContext *output, bool ForSameTeam) const
{
	output->PlayerClass = GetPlayerClass();
	switch(GetPlayerClass())
	{
	case PLAYERCLASS_SPIDER:
		output->ExtraData1 = ForSameTeam ? m_HookOnTheLimit : 0;
		break;
	case PLAYERCLASS_GHOUL:
		output->ExtraData1 = GetGhoulPercent() * 100;
		break;
	case PLAYERCLASS_VOODOO:
		output->ExtraData1 = m_VoodooAboutToDie;
		break;
	default:
		output->ExtraData1 = 0;
		break;
	}
}

bool CInfClassInfected::SetupSkin(const CSkinContext &Context, CWeakSkinInfo *pOutput, int DDNetVersion, int InfClassVersion)
{
	switch(Context.PlayerClass)
	{
		case PLAYERCLASS_SMOKER:
			pOutput->UseCustomColor = 1;
			pOutput->pSkinName = "cammostripes";
			pOutput->ColorBody = 3866368;
			pOutput->ColorFeet = 65414;
			break;
		case PLAYERCLASS_BOOMER:
			pOutput->pSkinName = "saddo";
			pOutput->UseCustomColor = 1;
			pOutput->ColorBody = 3866368;
			pOutput->ColorFeet = 65414;
			break;
		case PLAYERCLASS_HUNTER:
			pOutput->pSkinName = "warpaint";
			pOutput->UseCustomColor = 1;
			pOutput->ColorBody = 3866368;
			pOutput->ColorFeet = 65414;
			break;
		case PLAYERCLASS_BAT:
			pOutput->pSkinName = "limekitty";
			pOutput->UseCustomColor = 1;
			pOutput->ColorBody = 3866368;
			pOutput->ColorFeet = 2866368;
			break;
		case PLAYERCLASS_GHOST:
			pOutput->pSkinName = "twintri";
			pOutput->UseCustomColor = 1;
			pOutput->ColorBody = 3866368;
			pOutput->ColorFeet = 65414;
			break;
		case PLAYERCLASS_SPIDER:
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
		case PLAYERCLASS_GHOUL:
			pOutput->pSkinName = "cammo";
			pOutput->UseCustomColor = 1;
			{
				float Percent = Context.ExtraData1 / 100.0f;
				int Hue = 58 * (1.0f - Percent * 0.8f);
				pOutput->ColorBody = (Hue<<16) + (255<<8);
			}
			pOutput->ColorFeet = 65414;
			break;
		case PLAYERCLASS_SLUG:
			pOutput->pSkinName = "coala";
			pOutput->UseCustomColor = 1;
			pOutput->ColorBody = 3866368;
			pOutput->ColorFeet = 65414;
			break;
		case PLAYERCLASS_VOODOO:
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
		case PLAYERCLASS_UNDEAD:
			pOutput->pSkinName = "redstripe";
			pOutput->UseCustomColor = 1;
			pOutput->ColorBody = 3014400;
			pOutput->ColorFeet = 13168;
			break;
		case PLAYERCLASS_WITCH:
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

int CInfClassInfected::GetDefaultEmote() const
{
	int EmoteNormal = EMOTE_ANGRY;

	if(!m_pCharacter)
		return EmoteNormal;

	if(m_pCharacter->IsBlind())
		EmoteNormal = EMOTE_BLINK;

	if(m_pCharacter->IsInvisible())
		EmoteNormal = EMOTE_BLINK;

	if(m_pCharacter->IsInLove() || m_pCharacter->IsInSlowMotion() || m_pCharacter->HasHallucination())
		EmoteNormal = EMOTE_SURPRISE;

	if(m_pCharacter->IsFrozen())
	{
		if(m_pCharacter->GetFreezeReason() == FREEZEREASON_UNDEAD)
		{
			EmoteNormal = EMOTE_PAIN;
		}
		else
		{
			EmoteNormal = EMOTE_BLINK;
		}
	}

	return EmoteNormal;
}

bool CInfClassInfected::CanDie() const
{
	if ((GetPlayerClass() == PLAYERCLASS_UNDEAD) && m_pCharacter->IsFrozen()) {
		return false;
	}
	if ((GetPlayerClass() == PLAYERCLASS_VOODOO) && m_VoodooAboutToDie) {
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
		case PLAYERCLASS_SPIDER:
		{
			if(m_pCharacter->WebHookLength() > 48.0f && m_pCharacter->GetHookedPlayer() < 0)
			{
				// Find other players
				for(CInfClassCharacter *p = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER); p; p = (CInfClassCharacter *)p->TypeNext())
				{
					if(p->IsZombie())
						continue;

					vec2 IntersectPos;
					if(!closest_point_on_line(GetPos(), m_pCharacter->GetHookPos(), p->GetPos(), IntersectPos))
						continue;

					float Len = distance(p->GetPos(), IntersectPos);
					if(Len < p->GetProximityRadius())
					{
						m_pCharacter->SetHookedPlayer(p->GetCID());
						// Note: typical Teeworlds clients restore m_HookMode = 1
						// via "Direct weapon selection" / m_LatestInput.m_WantedWeapon
						m_pCharacter->m_HookMode = 0;
						m_pCharacter->m_Core.m_HookTick = 0;

						break;
					}
				}
			}
		}
			break;
		default:
			break;
	}
}

void CInfClassInfected::OnCharacterTick()
{
	CInfClassPlayerClass::OnCharacterTick();

	if(GetPlayerClass() == PLAYERCLASS_VOODOO && m_VoodooAboutToDie)
	{
		// Delayed Death
		if (m_VoodooTimeAlive > 0)
			m_VoodooTimeAlive-=1000;
		else
			m_pCharacter->Die(m_VoodooDeathContext);

		// Display time left to live
		int Time = m_VoodooTimeAlive/Server()->TickSpeed();
		GameServer()->SendBroadcast_Localization(GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, BROADCAST_DURATION_REALTIME,
			_C("Voodoo", "Staying alive for: {int:RemainingTime}"),
			"RemainingTime", &Time,
			NULL
		);
	}
	if(GetPlayerClass() == PLAYERCLASS_SPIDER)
	{
		const bool HookIsOnTheLimit = m_pCharacter->WebHookLength() > Config()->m_InfSpiderWebHookLength - 48.0f;
		SetHookOnLimit(HookIsOnTheLimit);
	}
}

void CInfClassInfected::OnCharacterSnap(int SnappingClient)
{
	if(SnappingClient == m_pPlayer->GetCID())
	{
		switch(GetPlayerClass())
		{
		case PLAYERCLASS_WITCH:
		{
			if(m_pCharacter->GetActiveWeapon() == WEAPON_HAMMER)
			{
				vec2 SpawnPos;
				if(FindWitchSpawnPosition(SpawnPos))
				{
					const int CursorID = GameController()->GetPlayerOwnCursorID(GetCID());
					GameController()->SendHammerDot(SpawnPos, CursorID);
				}
			}
			break;
		}
		default:
			break;
		}
	}
}

void CInfClassInfected::OnCharacterSpawned(const SpawnContext &Context)
{
	CInfClassPlayerClass::OnCharacterSpawned(Context);

	m_SlimeHealTick = 0;
	m_LaserWallTick = 0;

	if(Context.SpawnType == SpawnContext::MapSpawn)
	{
		m_pCharacter->GrantSpawnProtection();
	}
}

void CInfClassInfected::OnCharacterDeath(DAMAGE_TYPE DamageType)
{
	CInfClassPlayerClass::OnCharacterDeath(DamageType);

	if(GetPlayerClass() == PLAYERCLASS_GHOUL)
	{
		IncreaseGhoulLevel(-20);
		UpdateSkin();
	}

	if(GetPlayerClass() == PLAYERCLASS_BOOMER)
	{
		bool CanExplode = true;

		if(DamageType == DAMAGE_TYPE::GAME)
			CanExplode = false;

		if(m_pCharacter->IsFrozen())
			CanExplode = false;

		if(m_pCharacter->IsInLove() && (DamageType == DAMAGE_TYPE::KILL_COMMAND))
			CanExplode = false;

		if(CanExplode)
		{
			DoBoomerExplosion();
		}
	}
}

void CInfClassInfected::OnCharacterDamage(SDamageContext *pContext)
{
	switch(GetPlayerClass())
	{
	case PLAYERCLASS_HUNTER:
		if(pContext->DamageType == DAMAGE_TYPE::MEDIC_SHOTGUN)
		{
			pContext->Force = vec2(0, 0);
		}
		break;
	case PLAYERCLASS_GHOUL:
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
	if(GetPlayerClass() == PLAYERCLASS_BOOMER)
	{
		if(!m_pCharacter->IsFrozen() && !m_pCharacter->IsInLove())
		{
			pFireContext->FireAccepted = false;
			m_pCharacter->Die(GetCID(), DAMAGE_TYPE::BOOMER_EXPLOSION);
		}

		return;
	}

	bool AutoFire = false;
	bool FullAuto = false;

	if(GetPlayerClass() == PLAYERCLASS_SLUG)
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

		if(GetPlayerClass() == PLAYERCLASS_GHOST)
		{
			m_pCharacter->MakeVisible();
		}

		// Lookup for humans
		ClientsArray Targets;
		GameController()->GetSortedTargetsInRange(ProjStartPos, GetHammerRange(), ClientsArray({GetCID()}), &Targets);

		for(const int TargetCID : Targets)
		{
			CInfClassCharacter *pTarget = GameController()->GetCharacter(TargetCID);

			if(GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos()))
				continue;

			vec2 Dir;
			if(length(pTarget->GetPos() - GetPos()) > 0.0f)
				Dir = normalize(pTarget->GetPos() - GetPos());
			else
				Dir = vec2(0.f, -1.f);

			vec2 Force = vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;

			if(pTarget->IsZombie())
			{
				if(pTarget->IsFrozen())
				{
					pTarget->TryUnfreeze(GetCID());
				}
				else
				{
					if(pTarget->Heal(4, GetCID()))
					{
						m_pCharacter->Heal(1);
					}

					if(!pTarget->GetPlayer()->HookProtectionEnabled())
					{
						pTarget->m_Core.m_Vel += Force;

						if(-Force.y > 6.f)
						{
							const float HammerFlyHelperDuration = 20;
							pTarget->AddHelper(GetCID(), HammerFlyHelperDuration);
						}
					}
				}
			}
			else
			{
				if(!pTarget->GetClass()->CanBeHit())
					continue;

				int Damage = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
				DAMAGE_TYPE DamageType = DAMAGE_TYPE::INFECTION_HAMMER;

				if(GetPlayerClass() == PLAYERCLASS_BAT)
				{
					Damage = g_Config.m_InfBatDamage;
					DamageType = DAMAGE_TYPE::BITE;
				}

				pTarget->TakeDamage(Force, Damage, GetCID(), DamageType);
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
		m_pCharacter->SetReloadDuration(0.33f);
	}
	else if(GetPlayerClass() == PLAYERCLASS_SLUG)
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
	if(!m_pCharacter)
	{
		return;
	}

	CInfClassPlayerClass::GiveClassAttributes();

	m_pCharacter->GiveWeapon(WEAPON_HAMMER, -1);
	m_pCharacter->SetActiveWeapon(WEAPON_HAMMER);

	m_VoodooAboutToDie = false;
	m_VoodooTimeAlive = Server()->TickSpeed()*Config()->m_InfVoodooAliveTime;
}

void CInfClassInfected::BroadcastWeaponState() const
{
	if(GetPlayerClass() == PLAYERCLASS_SPIDER)
	{
		if(m_pCharacter->m_HookMode > 0)
		{
			GameServer()->SendBroadcast_Localization(GetCID(), BROADCAST_PRIORITY_WEAPONSTATE,
				BROADCAST_DURATION_REALTIME, _C("Spider", "Web mode enabled"), NULL);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_GHOUL)
	{
		if(m_pPlayer->GetGhoulLevel())
		{
			float FodderInStomach = GetGhoulPercent();
			GameServer()->SendBroadcast_Localization(GetCID(), BROADCAST_PRIORITY_WEAPONSTATE,
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

		for(int i = 0; i < Num; i++)
		{
			CInfClassCharacter *pTarget = apEnts[i];
			if(pTarget == m_pCharacter)
				continue;

			vec2 Diff = pTarget->GetPos() - GetPos();
			if (Diff.x == 0.0f && Diff.y == 0.0f)
				Diff.y = -0.5f;
			vec2 ForceDir(0,1);
			float Length = length(Diff);
			if(pTarget->IsZombie() && (Length < DamageRadius))
			{
				if(Length < ClosestCharacterDistance)
				{
					pBestBFTarget = pTarget;
					ClosestCharacterDistance = Length;
				}
			}

			Length = 1-clamp((Length-InnerRadius)/(DamageRadius-InnerRadius), 0.0f, 1.0f);

			if(Length)
				ForceDir = normalize(Diff);

			float DamageToDeal = 1 + ((Damage - 1) * Length);
			pTarget->TakeDamage(ForceDir*Force*Length, DamageToDeal, GetCID(), DAMAGE_TYPE::BOOMER_EXPLOSION);

			const CInfClassPlayer *pTargetPlayer = pTarget->GetPlayer();
			if(pTarget->IsZombie() || (pTargetPlayer && pTargetPlayer->IsInfectionStarted()))
			{
				const float BoomerHelperDuration = 30;
				pTarget->AddHelper(GetCID(), BoomerHelperDuration);
			}
		}
	}

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_EXPLODE);
	GameController()->CreateExplosionDiskGfx(GetPos(), InnerRadius, DamageRadius, m_pPlayer->GetCID());

	if(pBestBFTarget)
	{
		m_pPlayer->SetFollowTarget(pBestBFTarget->GetCID(), 5.0);
		m_pPlayer->m_DieTick = Server()->Tick() + Server()->TickSpeed() * 10;
	}
}

void CInfClassInfected::PlaceSlugSlime(WeaponFireContext *pFireContext)
{
	if(m_pCharacter->IsInLove())
		return;

	vec2 CheckPos = GetPos() + GetDirection() * 64.0f;
	if(GameServer()->Collision()->IntersectLine(GetPos(), CheckPos, 0x0, &CheckPos))
	{
		static const float MinDistance = 84.0f;
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
				// Replenish the slime
				if(pSlime->GetMaxLifeSpan() - pSlime->GetLifeSpan() > Server()->TickSpeed())
				{
					pSlime->Replenish(GetCID());
					pFireContext->FireAccepted = true;
					break;
				}
			}
		}

		if(DistanceToTheNearestSlime > MinDistance)
		{
			new CSlugSlime(GameServer(), CheckPos, GetCID());
			pFireContext->FireAccepted = true;
		}
	}
}

bool CInfClassInfected::FindWitchSpawnPosition(vec2 &Position)
{
	float Angle = atan2f(m_pCharacter->m_Input.m_TargetY, m_pCharacter->m_Input.m_TargetX);//atan2f instead of atan2

	for(int i=0; i<32; i++)
	{
		float TestAngle;

		TestAngle = Angle + i * (pi / 32.0f);
		Position = GetPos() + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;

		if(GameController()->IsSpawnable(Position, ZONE_TELE_NOWITCH))
			return true;

		TestAngle = Angle - i * (pi / 32.0f);
		Position = GetPos() + vec2(cos(TestAngle), sin(TestAngle)) * 84.0f;

		if(GameController()->IsSpawnable(Position, ZONE_TELE_NOWITCH))
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

void CInfClassInfected::OnSlimeEffect(int Owner)
{
	if(!m_pCharacter->IsAlive())
		return;

	m_pCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick());
	if(Server()->Tick() >= m_SlimeHealTick + (Server()->TickSpeed() / Config()->m_InfSlimeHealRate))
	{
		if(m_pCharacter->GetHealthArmorSum() < Config()->m_InfSlimeMaxHeal)
		{
			m_pCharacter->Heal(1, GetCID());
		}
		m_SlimeHealTick = Server()->Tick();
	}
}

void CInfClassInfected::OnFloatingPointCollected(int Points)
{
	if(GetPlayerClass() != PLAYERCLASS_GHOUL)
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
	if(GetPlayerClass() == PLAYERCLASS_UNDEAD)
	{
		m_pCharacter->Freeze(10.0, Context.Killer, FREEZEREASON_UNDEAD);
		m_pCharacter->SetHealthArmor(0, 0);
		*pRefusedToDie = true;
		return;
	}

	// Start counting down, delay killer message for later
	if(GetPlayerClass() == PLAYERCLASS_VOODOO)
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
	if(GetPlayerClass() == PLAYERCLASS_VOODOO)
	{
		m_VoodooAboutToDie = true;
		m_VoodooDeathContext = Context;
		UpdateSkin();

		*pRefusedToDie = true;
	}
}
