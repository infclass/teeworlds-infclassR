#include "blinding-laser.h"
#include "infccharacter.h"

#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>

#include <game/server/entities/projectile.h>

#include <game/server/infclass/classes/infected/infected.h>
#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/entities/biologist-mine.h>
#include <game/server/infclass/entities/bouncing-bullet.h>
#include <game/server/infclass/entities/engineer-wall.h>
#include <game/server/infclass/entities/growingexplosion.h>
#include <game/server/infclass/entities/hero-flag.h>
#include <game/server/infclass/entities/infc-laser.h>
#include <game/server/infclass/entities/laser-teleport.h>
#include <game/server/infclass/entities/looper-wall.h>
#include <game/server/infclass/entities/medic-grenade.h>
#include <game/server/infclass/entities/merc-bomb.h>
#include <game/server/infclass/entities/merc-laser.h>
#include <game/server/infclass/entities/plasma.h>
#include <game/server/infclass/entities/scatter-grenade.h>
#include <game/server/infclass/entities/scientist-laser.h>
#include <game/server/infclass/entities/scientist-mine.h>
#include <game/server/infclass/entities/slug-slime.h>
#include <game/server/infclass/entities/soldier-bomb.h>
#include <game/server/infclass/entities/superweapon-indicator.h>
#include <game/server/infclass/entities/turret.h>
#include <game/server/infclass/entities/white-hole.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>

MACRO_ALLOC_POOL_ID_IMPL(CInfClassCharacter, MAX_CLIENTS)

CInfClassCharacter::CInfClassCharacter(CInfClassGameController *pGameController)
	: CCharacter(pGameController->GameWorld(), pGameController->Console())
	, m_pGameController(pGameController)
{
}

CInfClassCharacter::~CInfClassCharacter()
{
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
	m_IsInSlowMotion = false;
	m_FrozenTime = -1;
	m_LoveTick = -1;
	m_SlowMotionTick = -1;
	m_HallucinationTick = -1;
	m_SlipperyTick = -1;
	m_LastFreezer = -1;
	m_LastHooker = -1;
	m_LastHookerTick = -1;
	m_LastEnforcer = -1;
	m_LastEnforcerTick = -1;

	ClassSpawnAttributes();
	DestroyChildEntities();
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
			if((GameController()->GetMinimumInfected() == 1) && (GameServer()->GetZombieCount() == 1))
			{
				// See also: Character::GiveArmorIfLonely()
				IncreaseOverallHp(1);
			}
			else
			{
				IncreaseHealth(1);
			}
		}
		if (m_InfZoneTick < 0) {
			m_InfZoneTick = Server()->Tick(); // Save Tick when zombie enters infection zone
			GrantSpawnProtection();
		}
	}
	else
	{
		int Killer = -1;
		int Assistant = -1;
		DAMAGE_TYPE DamageType = DAMAGE_TYPE::INFECTION_TILE;
		GetActualKillers(GetCID(), DamageType, &Killer, &Assistant);

		CInfClassPlayer *pKiller = GameController()->GetPlayer(Killer);

		GameController()->OnCharacterDeath(this, DamageType, Killer, Assistant);
		GameServer()->CreateSound(GetPos(), SOUND_PLAYER_DIE);

		GetPlayer()->Infect(pKiller);
	}
}

void CInfClassCharacter::OnCharacterOutOfInfectionZone()
{
	m_InfZoneTick = -1;// Reset Tick when zombie is not in infection zone
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

		Server()->RoundStatistics()->OnScoreEvent(GetCID(), SCOREEVENT_BONUS, GetPlayerClass(), Server()->ClientName(GetCID()), Console());
		GameServer()->SendScoreSound(GetCID());
	}
}

void CInfClassCharacter::OnWhiteHoleSpawned(const CWhiteHole *pWhiteHole)
{
	GetPlayer()->ResetNumberKills();
	m_ResetKillsTime = pWhiteHole->LifeSpan() + Server()->TickSpeed() * 3;
}

void CInfClassCharacter::Destroy()
{
	ResetClassObject();
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

	if(m_pClass)
		m_pClass->OnCharacterTick();

	if(GetPlayerClass() == PLAYERCLASS_SNIPER && PositionIsLocked())
	{
		m_Core.m_Vel = vec2(0.0f, 0.0f);
		m_Core.m_Pos = PrevPos;
	}

	//NeedHeal
	if(m_Armor >= 10)
	{
		m_NeedFullHeal = false;
	}
}

void CInfClassCharacter::TickDefered()
{
	int Events = m_Core.m_TriggeredEvents;

	CCharacter::TickDefered();

	const int64_t MaskOnlyBlind = GameController()->GetBlindCharactersMask(GetCID());
	if(MaskOnlyBlind)
	{
		if(Events & COREEVENT_AIR_JUMP)
			GameServer()->CreateSound(GetPos(), SOUND_PLAYER_AIRJUMP, MaskOnlyBlind);
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
}

void CInfClassCharacter::SpecialSnapForClient(int SnappingClient, bool *pDoSnap)
{
	CInfClassPlayer* pDestClient = GameController()->GetPlayer(SnappingClient);

	CInfClassCharacter *pDestCharacter = GameController()->GetCharacter(SnappingClient);
	if((GetCID() != SnappingClient) && pDestCharacter && pDestCharacter->IsBlind())
	{
		*pDoSnap = false;
		return;
	}

	if(GetPlayerClass() == PLAYERCLASS_GHOST)
	{
		if(pDestClient && !pDestClient->IsZombie() && m_IsInvisible)
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

	bool ShowFirstShot = (SnappingClient == -1) || (pDestClient && pDestClient->IsHuman());
	if(ShowFirstShot && GetPlayerClass() == PLAYERCLASS_ENGINEER && !m_FirstShot)
	{
		CEngineerWall* pCurrentWall = NULL;
		for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(!pCurrentWall)
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_BarrierHintID, sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = (int)m_FirstShotCoord.x;
			pObj->m_Y = (int)m_FirstShotCoord.y;
			pObj->m_FromX = (int)m_FirstShotCoord.x;
			pObj->m_FromY = (int)m_FirstShotCoord.y;
			pObj->m_StartTick = Server()->Tick();
			
		}
	}
	if(ShowFirstShot && GetPlayerClass() == PLAYERCLASS_LOOPER && !m_FirstShot)
	{
		CLooperWall* pCurrentWall = NULL;
		for(CLooperWall *pWall = (CLooperWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == GetCID())
			{
				pCurrentWall = pWall;
				break;
			}
		}

		if(!pCurrentWall)
		{
			for(int i=0; i<2; i++)
			{

				CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_BarrierHintIDs[i], sizeof(CNetObj_Laser)));

				if(!pObj)
					return;

				pObj->m_X = (int)m_FirstShotCoord.x-CLooperWall::THICKNESS*i+(CLooperWall::THICKNESS*0.5);
				pObj->m_Y = (int)m_FirstShotCoord.y;
				pObj->m_FromX = (int)m_FirstShotCoord.x-CLooperWall::THICKNESS*i+(CLooperWall::THICKNESS*0.5);
				pObj->m_FromY = (int)m_FirstShotCoord.y;
				pObj->m_StartTick = Server()->Tick();
			}
		}
	}
}

void CInfClassCharacter::HandleNinja()
{
/* INFECTION MODIFICATION START ***************************************/
	if(GetInfWeaponID(m_ActiveWeapon) != INFWEAPON_NINJA_HAMMER)
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

				vec2 IntersectPos = closest_point_on_line(OldPos, m_Core.m_Pos, pTarget->GetPos());
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
/* INFECTION MODIFICATION START ***************************************/
	if(m_AntiFireTime > 0)
		return;

	if(IsFrozen())
		return;
/* INFECTION MODIFICATION END *****************************************/
	
	if(m_ReloadTimer != 0)
		return;

/* INFECTION MODIFICATION START ***************************************/
	if((GetPlayerClass() == PLAYERCLASS_NONE) || !GetClass())
		return;
/* INFECTION MODIFICATION END *****************************************/

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
	else if(FullAuto && (m_LatestInput.m_Fire&1) && (m_aWeapons[m_ActiveWeapon].m_Ammo || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MERCENARY_GRENADE)
																					   || (GetInfWeaponID(m_ActiveWeapon) == INFWEAPON_MEDIC_GRENADE)))
	{
		WillFire = true;
	}

	if(!WillFire || GetPlayer()->MapMenu() > 0)
		return;

	WeaponFireContext FireContext;
	FireContext.Weapon = m_ActiveWeapon;
	FireContext.FireAccepted = true;
	FireContext.AmmoConsumed = 1;
	FireContext.AmmoAvailable = m_aWeapons[m_ActiveWeapon].m_Ammo;
	FireContext.NoAmmo = FireContext.AmmoAvailable == 0;

	OnWeaponFired(&FireContext);

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
		m_ReloadTimer = Server()->GetFireDelay(GetInfWeaponID(m_ActiveWeapon)) * Server()->TickSpeed() / 1000;
	}
}

bool CInfClassCharacter::TakeDamage(vec2 Force, float FloatDmg, int From, DAMAGE_TYPE DamageType)
{
	int Dmg = FloatDmg;
	if(FloatDmg != Dmg)
	{
		int ExtraDmg = random_prob(FloatDmg - Dmg) ? 1 : 0;
		Dmg += ExtraDmg;
	}

	TAKEDAMAGEMODE Mode = TAKEDAMAGEMODE::NOINFECTION;
	int Weapon = CInfClassGameController::DamageTypeToWeapon(DamageType, &Mode);

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

	if(GetPlayerClass() == PLAYERCLASS_HERO && Mode == TAKEDAMAGEMODE::INFECTION)
	{
		Dmg = 12;
		// A zombie can't infect a hero
		Mode = TAKEDAMAGEMODE::NOINFECTION;
	}

	if(pKillerChar && pKillerChar->IsInLove())
	{
		Dmg = 0;
		Mode = TAKEDAMAGEMODE::NOINFECTION;
		if(!IsZombie())
		{
			Force *= 0.1;
		}
	}

	if((GetPlayerClass() == PLAYERCLASS_HUNTER) && (DamageType == DAMAGE_TYPE::MEDIC_SHOTGUN))
	{
		// Hunters are immune to shotgun force
		Force = vec2(0, 0);
	}

	if(IsHuman() && (Weapon == WEAPON_NINJA))
	{
		// Humans are immune to Ninja's force
		Force = vec2(0, 0);
	}

	const bool DmgFromHuman = pKillerPlayer && pKillerPlayer->IsHuman();
	if(DmgFromHuman && (GetPlayerClass() == PLAYERCLASS_SOLDIER) && (Weapon == WEAPON_HAMMER))
	{
		// Soldier is immune to any traps force
		Force = vec2(0, 0);
	}

	if((From >= 0) && (From != GetCID()) && (Force.x || Force.y))
	{
		const float CurrentSpeed = length(m_Core.m_Vel);
		const float AddedForce = length(Force);
		if(AddedForce > CurrentSpeed * 0.5)
		{
			UpdateLastEnforcer(From, AddedForce, DamageType, Server()->Tick());
		}
	}

	m_Core.m_Vel += Force;

	if(GetPlayerClass() == PLAYERCLASS_GHOUL)
	{
		int DamageAccepted = 0;
		for(int i=0; i<Dmg; i++)
		{
			if(random_prob(m_pClass->GetGhoulPercent() * 0.33))
				continue;

			DamageAccepted++;
		}
		Dmg = DamageAccepted;
	}

	if(m_ProtectionTick > 0)
	{
		Dmg = 0;
	}

	if(From != GetCID() && pKillerPlayer)
	{
		if(IsZombie())
		{
			if(pKillerPlayer->IsZombie())
			{
				//Heal and unfreeze
				if(DamageType == DAMAGE_TYPE::BOOMER_EXPLOSION)
				{
					TryUnfreeze();
					if(!IsFrozen())
					{
						IncreaseOverallHp(8+random_int(0, 10));
						SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
					}
				}
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
			Dmg = maximum(1, Dmg/2);
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
		if(m_Armor)
		{
			if(Dmg <= m_Armor)
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
			else
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
		}

		m_Health -= Dmg;

		if(From != GetCID() && pKillerPlayer)
			m_NeedFullHeal = true;

		if(From >= 0 && From != GetCID())
			GameServer()->SendHitSound(From);
	}
/* INFECTION MODIFICATION END *****************************************/

	m_DamageTakenTick = Server()->Tick();
	m_InvisibleTick = Server()->Tick();

	// check for death
	if(m_Health <= 0)
	{
		Die(From, DamageType);
		return false;
	}

/* INFECTION MODIFICATION START ***************************************/
	if(Mode == TAKEDAMAGEMODE::INFECTION)
	{
		GetPlayer()->Infect(pKillerPlayer);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
			Server()->ClientName(From),
			Server()->ClientName(GetCID()), Weapon);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

		GameController()->SendKillMessage(GetCID(), DamageType, From);
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

void CInfClassCharacter::OnWeaponFired(WeaponFireContext *pFireContext)
{
	GetClass()->OnWeaponFired(pFireContext);

	switch(pFireContext->Weapon)
	{
		case WEAPON_HAMMER:
			OnHammerFired(pFireContext);
			break;
		case WEAPON_GUN:
			OnGunFired(pFireContext);
			break;
		case WEAPON_SHOTGUN:
			OnShotgunFired(pFireContext);
			break;
		case WEAPON_GRENADE:
			OnGrenadeFired(pFireContext);
			break;
		case WEAPON_LASER:
			OnLaserFired(pFireContext);
			break;
		case WEAPON_NINJA:
			OnNinjaFired(pFireContext);
			break;
		default:
			break;
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

void CInfClassCharacter::ResetMovementsInput()
{
	m_Input.m_Jump = 0;
	m_Input.m_Direction = 0;
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

int CInfClassCharacter::GetFlagCoolDown()
{
	return m_pHeroFlag ? m_pHeroFlag->GetCoolDown() : 0;
}

void CInfClassCharacter::GetActualKillers(int GivenKiller, DAMAGE_TYPE GivenWeapon, int *pKillerId, int *pAssistant) const
{
	switch(GivenWeapon)
	{
	case DAMAGE_TYPE::GAME:
	case DAMAGE_TYPE::GAME_FINAL_EXPLOSION:
	case DAMAGE_TYPE::GAME_INFECTION:
		return;
	default:
		break;
	}

	if(GivenWeapon == DAMAGE_TYPE::BOOMER_EXPLOSION)
	{
		if(GetPlayerClass() == PLAYERCLASS_BOOMER)
		{
			return;
		}
	}

	int Killer = -1;
	int Assistant = -1;

	const auto AddKiller = [&Killer, &Assistant](int ExtraKiller)
	{
		if(Killer < 0)
		{
			Killer = ExtraKiller;
			return;
		}

		if(Killer == ExtraKiller)
		{
			return;
		}

		if(Assistant < 0)
		{
			Assistant = ExtraKiller;
			return;
		}
	};
	const auto AddAssistant = [&Killer, &Assistant](int ExtraKiller)
	{
		if(Killer < 0)
		{
			return;
		}

		if((Killer == ExtraKiller) || (Assistant == ExtraKiller))
		{
			return;
		}

		if(Assistant < 0)
		{
			Assistant = ExtraKiller;
		}
	};

	if(GivenKiller != GetCID())
	{
		AddKiller(GivenKiller);
	}

	if(IsFrozen())
	{
		if(m_FreezeReason == FREEZEREASON_FLASH)
		{
			// DAMAGE_TYPE::STUNNING_GRENADE
		}
		AddKiller(m_LastFreezer);
	}

	bool DamageIsPassive = false;
	switch(GivenWeapon) {
	case DAMAGE_TYPE::DEATH_TILE:
	case DAMAGE_TYPE::INFECTION_TILE:
	case DAMAGE_TYPE::LASER_WALL:
		DamageIsPassive = true;
	default:
		break;
	}

	ClientsArray Killers;
	ClientsArray Assistants;

	ClientsArray &Enforcers = DamageIsPassive ? Killers : Assistants;
	// if hooked
	{
		for(CInfClassCharacter *pHooker = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);
			pHooker;
			pHooker = (CInfClassCharacter *)pHooker->TypeNext())
		{
			if(pHooker->GetPlayer() && pHooker->GetHookedPlayer() == GetCID())
			{
				Enforcers.Add(pHooker->GetCID());
			}
		}

		if(Enforcers.Size() > 1)
		{
			GameController()->SortCharactersByDistance(Enforcers, &Enforcers, GetPos());
		}
	}

	if(m_LastEnforcer >= 0 && (m_LastEnforcerTick > m_LastHookerTick))
	{
		if(!Enforcers.Contains(m_LastEnforcer))
		{
			Enforcers.Add(m_LastEnforcer);
		}
	}

	if(m_LastHooker >= 0)
	{
		if(!Enforcers.Contains(m_LastHooker))
		{
			Enforcers.Add(m_LastHooker);
		}
	}

	if(DamageIsPassive)
	{
		for(int i = 0; i < Enforcers.Size(); ++i)
		{
			AddKiller(Enforcers.At(i));
		}
	}

	if(IsInSlowMotion() && (m_SlowEffectApplicant >= 0))
	{
		AddAssistant(m_SlowEffectApplicant);
	}

	if(!DamageIsPassive)
	{
		for(int i = 0; i < Enforcers.Size(); ++i)
		{
			AddAssistant(Enforcers.At(i));
		}
	}

	if(Killer < 0)
	{
		Killer = GivenKiller;
	}

	if(IsBlind())
	{
		AddAssistant(m_LastBlinder);
	}

	*pKillerId = Killer;
	*pAssistant = Assistant;
}

void CInfClassCharacter::UpdateLastHooker(int ClientID, int HookerTick)
{
	m_LastHooker = ClientID;
	m_LastHookerTick = HookerTick;
}

void CInfClassCharacter::UpdateLastEnforcer(int ClientID, float Force, DAMAGE_TYPE DamageType, int Tick)
{
	if(Force < 3)
		return;

	m_LastEnforcer = ClientID;
	m_LastEnforcerDamageType = DamageType;
	m_LastEnforcerTick = Tick;
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

bool CInfClassCharacter::HasPassenger() const
{
	return m_Core.m_Passenger;
}

CCharacter *CInfClassCharacter::GetPassenger()
{
	if(!m_Core.m_Passenger)
	{
		return nullptr;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacterCore *pCharCore = GameServer()->m_World.m_Core.m_apCharacters[i];
		if(pCharCore == m_Core.m_Passenger)
			return GameServer()->GetPlayerChar(i);
	}

	return nullptr;
}

int CInfClassCharacter::GetInfZoneTick() // returns how many ticks long a player is already in InfZone
{
	if(m_InfZoneTick < 0)
		return 0;

	return Server()->Tick()-m_InfZoneTick;
}

CGameWorld *CInfClassCharacter::GameWorld() const
{
	return m_pGameController->GameWorld();
}

const IServer *CInfClassCharacter::Server() const
{
	return m_pGameController->GameWorld()->Server();
}

void CInfClassCharacter::OnHammerFired(WeaponFireContext *pFireContext)
{
	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;

	bool AutoFire = false;
	bool FullAuto = false;

	if(GetPlayerClass() == PLAYERCLASS_SLUG)
		FullAuto = true;

	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
	{
	}
	else if(FullAuto && (m_LatestInput.m_Fire&1) && (pFireContext->AmmoAvailable))
	{
		AutoFire = true;
	}

	if(GetPlayerClass() == PLAYERCLASS_ENGINEER)
	{
		for(CEngineerWall *pWall = (CEngineerWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_ENGINEER_WALL); pWall; pWall = (CEngineerWall*) pWall->TypeNext())
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
			//Check if the barrier is in toxic gases
			bool isAccepted = true;
			for(int i=0; i<15; i++)
			{
				vec2 TestPos = m_FirstShotCoord + (GetPos() - m_FirstShotCoord)*(static_cast<float>(i)/14.0f);
				if(GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_icDamage, TestPos) == ZONE_DAMAGE_INFECTION)
				{
					isAccepted = false;
				}
			}

			if(isAccepted)
			{
				m_FirstShot = true;
				new CEngineerWall(GameServer(), m_FirstShotCoord, GetPos(), GetCID());
				GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_LOOPER)
	{
		//Potential variable name conflicts with engineers wall (for example *pWall is used twice for both Looper and Engineer)
		for(CLooperWall *pWall = (CLooperWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*) pWall->TypeNext())
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
			//Check if the barrier is in toxic gases
			bool isAccepted = true;
			for(int i=0; i<15; i++)
			{
				vec2 TestPos = m_FirstShotCoord + (GetPos() - m_FirstShotCoord)*(static_cast<float>(i)/14.0f);
				if(GameServer()->Collision()->GetZoneValueAt(GameServer()->m_ZoneHandle_icDamage, TestPos) == ZONE_DAMAGE_INFECTION)
				{
					isAccepted = false;
				}
			}

			if(isAccepted)
			{
				m_FirstShot = true;
				
				new CLooperWall(GameServer(), m_FirstShotCoord, GetPos(), GetCID());
				
				GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_HERO)
	{
		if(g_Config.m_InfTurretEnable && m_TurretCount)
		{
			if (g_Config.m_InfTurretEnableLaser)
			{
				new CTurret(GameServer(), GetPos(), GetCID(), Direction, CTurret::LASER);
			}
			else if (g_Config.m_InfTurretEnablePlasma)
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
			GameServer()->CreateSound(GetPos(), SOUND_WEAPON_NOAMMO);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_SOLDIER)
	{
		FireSoldierBomb();
	}
	else if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		// Moved to CInfClassHuman::OnHammerFired()
	}
	else if(GetPlayerClass() == PLAYERCLASS_MERCENARY && GameController()->MercBombsEnabled())
	{
		CMercenaryBomb* pCurrentBomb = NULL;
		for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
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

		m_ReloadTimer = Server()->TickSpeed()/4;
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		bool FreeSpace = true;
		int NbMine = 0;
		
		int OlderMineTick = Server()->Tick()+1;
		CScientistMine* pOlderMine = 0;
		CScientistMine* pIntersectMine = 0;
		
		CScientistMine* p = (CScientistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCIENTIST_MINE);
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

				if(d < 2.0f*g_Config.m_InfMineRadius)
				{
					if(pIntersectMine)
						FreeSpace = false;
					else
						pIntersectMine = p;
				}
			}
			else if(d < 2.0f*g_Config.m_InfMineRadius)
				FreeSpace = false;
			
			p = (CScientistMine *)p->TypeNext();
		}

		if(FreeSpace)
		{
			if(pIntersectMine) //Move the mine
				GameWorld()->DestroyEntity(pIntersectMine);
			else if(NbMine >= g_Config.m_InfMineLimit && pOlderMine)
				GameWorld()->DestroyEntity(pOlderMine);
			
			new CScientistMine(GameServer(), ProjStartPos, GetCID());
			
			m_ReloadTimer = Server()->TickSpeed()/2;
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_NINJA)
	{
		if(m_DartLeft || m_InWater)
		{
			if(!m_InWater)
				m_DartLeft--;

			// reset Hit objects
			m_NumObjectsHit = 0;

			m_DartDir = Direction;
			m_DartLifeSpan = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			m_DartOldVelAmount = length(m_Core.m_Vel);

			GameServer()->CreateSound(GetPos(), SOUND_NINJA_HIT);
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_BOOMER)
	{
		if(!IsFrozen() && !IsInLove())
		{
			pFireContext->FireAccepted = false;
			Die(GetCID(), DAMAGE_TYPE::BOOMER_EXPLOSION);
		}
	}
	else
	{
/* INFECTION MODIFICATION END *****************************************/
		// reset objects Hit
		int Hits = 0;
		bool ShowAttackAnimation = false;

		// make sure that the slug will not auto-fire to attack
		if(!AutoFire)
		{
			ShowAttackAnimation = true;

			m_NumObjectsHit = 0;

			if(GetPlayerClass() == PLAYERCLASS_GHOST)
			{
				m_IsInvisible = false;
				m_InvisibleTick = Server()->Tick();
			}

			CInfClassCharacter *apEnts[MAX_CLIENTS];
			int Num = GameWorld()->FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				CInfClassCharacter *pTarget = apEnts[i];

				if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), NULL, NULL))
					continue;

				vec2 Dir;
				if (length(pTarget->GetPos() - GetPos()) > 0.0f)
					Dir = normalize(pTarget->GetPos() - GetPos());
				else
					Dir = vec2(0.f, -1.f);

/* INFECTION MODIFICATION START ***************************************/
				if(IsZombie())
				{
					if(pTarget->IsZombie())
					{
						if(pTarget->IsFrozen())
						{
							pTarget->TryUnfreeze();
						}
						else
						{
							if(pTarget->IncreaseOverallHp(4))
							{
								IncreaseOverallHp(1);
								pTarget->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
							}

							if(!pTarget->GetPlayer()->HookProtectionEnabled())
								pTarget->m_Core.m_Vel += vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;
						}
					}
					else
					{
						if(pTarget->GetPlayerClass() == PLAYERCLASS_NINJA)
						{
							// Do not hit slashing ninjas
							if(pTarget->m_DartLifeSpan >= 0)
							{
								continue;
							}
						}
						int Damage = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
						DAMAGE_TYPE DamageType = DAMAGE_TYPE::INFECTION_HAMMER;

						if(GetPlayerClass() == PLAYERCLASS_BAT)
						{
							Damage = g_Config.m_InfBatDamage;
							DamageType = DAMAGE_TYPE::BITE;
						}

						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, Damage,
							GetCID(), DamageType);
					}
				}
				else if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST || GetPlayerClass() == PLAYERCLASS_MERCENARY)
				{
					/* affects mercenary only if love bombs are disabled. */
					if (pTarget->IsZombie())
					{
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20, 
								GetCID(), DAMAGE_TYPE::HAMMER);
					}
				}
				else if(GetPlayerClass() == PLAYERCLASS_MEDIC)
				{
					if (pTarget->IsZombie())
					{
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20, 
								GetCID(), DAMAGE_TYPE::HAMMER);
					}
					else
					{
						if(pTarget->GetPlayerClass() != PLAYERCLASS_HERO)
						{
							pTarget->IncreaseArmor(4);
							if(pTarget->m_Armor == 10 && pTarget->m_NeedFullHeal)
							{
								Server()->RoundStatistics()->OnScoreEvent(GetCID(), SCOREEVENT_HUMAN_HEALING, GetPlayerClass(), Server()->ClientName(GetCID()), Console());
								GameServer()->SendScoreSound(GetCID());
								pTarget->m_NeedFullHeal = false;
								m_aWeapons[WEAPON_GRENADE].m_Ammo++;
							}
							pTarget->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
						}
					}
				}
				else
				{
					pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
						GetCID(), DAMAGE_TYPE::HAMMER);
				}
/* INFECTION MODIFICATION END *****************************************/
				Hits++;

				// set his velocity to fast upward (for now)
				if(length(pTarget->GetPos()-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->GetPos()-normalize(pTarget->GetPos()-ProjStartPos)*m_ProximityRadius*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);
			}
		}

		if(!ShowAttackAnimation)
		{
			pFireContext->FireAccepted = false;
		}

		// if we Hit anything, we have to wait for the reload
		if(Hits)
		{
			m_ReloadTimer = Server()->TickSpeed()/3;
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
}

void CInfClassCharacter::OnGunFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
		return;

	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;
	
	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		CProjectile *pProj = new CProjectile(GameContext(), WEAPON_GUN,
			GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
			1, 0, 0, -1, DAMAGE_TYPE::MERCENARY_GUN);

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
		Msg.AddInt(1);
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);

		Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());
		
		float MaxSpeed = GameServer()->Tuning()->m_GroundControlSpeed*1.7f;
		vec2 Recoil = Direction*(-MaxSpeed/5.0f);
		SaturateVelocity(Recoil, MaxSpeed);

		GameServer()->CreateSound(GetPos(), SOUND_HOOK_LOOP);
	}
	else
	{
		CProjectile *pProj = new CProjectile(GameContext(), WEAPON_GUN,
			GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
			1, 0, 0, -1, DAMAGE_TYPE::GUN);

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
		Msg.AddInt(1);
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);

		Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());

		GameServer()->CreateSound(GetPos(), SOUND_GUN_FIRE);
	}
}

void CInfClassCharacter::OnShotgunFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
		return;

	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;

	int ShotSpread = 3;
	if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
		ShotSpread = 1;

	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(ShotSpread*2+1);

	float Force = 2.0f;
	DAMAGE_TYPE DamageType = DAMAGE_TYPE::SHOTGUN;
	if(GetPlayerClass() == PLAYERCLASS_MEDIC)
	{
		Force = 10.0f;
		DamageType = DAMAGE_TYPE::MEDIC_SHOTGUN;
	}

	for(int i = -ShotSpread; i <= ShotSpread; ++i)
	{
		float Spreading[] = {-0.21f, -0.14f, -0.070f, 0, 0.070f, 0.14f, 0.21f};
		float a = GetAngle(Direction);
		a += Spreading[i+3] * 2.0f*(0.25f + 0.75f*static_cast<float>(10 - pFireContext->AmmoAvailable)/10.0f);
		float v = 1-(absolute(i)/(float)ShotSpread);
		float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);

		float LifeTime = GameServer()->Tuning()->m_ShotgunLifetime + 0.1f*static_cast<float>(pFireContext->AmmoAvailable)/10.0f;

		if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
		{
			CBouncingBullet *pProj = new CBouncingBullet(GameServer(), GetCID(), ProjStartPos, vec2(cosf(a), sinf(a))*Speed);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
		}
		else
		{
			CProjectile *pProj = new CProjectile(GameContext(), WEAPON_SHOTGUN,
				GetCID(),
				ProjStartPos,
				vec2(cosf(a), sinf(a))*Speed,
				(int)(Server()->TickSpeed()*LifeTime),
				1, 0, Force, -1, DamageType);

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

void CInfClassCharacter::OnGrenadeFired(WeaponFireContext *pFireContext)
{
	if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		OnMercGrenadeFired(pFireContext);
		return;
	}

	if(GetPlayerClass() == PLAYERCLASS_MEDIC)
	{
		OnMedicGrenadeFired(pFireContext);
		return;
	}

	if(pFireContext->NoAmmo)
	{
		return;
	}

	vec2 Direction = GetDirection();
	vec2 ProjStartPos = GetPos()+Direction*GetProximityRadius()*0.75f;

	if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		vec2 PortalShift = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
		vec2 PortalDir = normalize(PortalShift);
		if(length(PortalShift) > 500.0f)
			PortalShift = PortalDir * 500.0f;
		vec2 PortalPos;

		if(FindPortalPosition(GetPos() + PortalShift, PortalPos))
		{
			vec2 OldPos = m_Core.m_Pos;
			m_Core.m_Pos = PortalPos;
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_HookPos = m_Core.m_Pos;
			if(g_Config.m_InfScientistTpSelfharm > 0) {
				TakeDamage(vec2(0.0f, 0.0f), g_Config.m_InfScientistTpSelfharm * 2, GetCID(), DAMAGE_TYPE::SCIENTIST_TELEPORT);
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
	}
	else
	{
		CProjectile *pProj = new CProjectile(GameContext(), WEAPON_GRENADE,
			GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
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
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());

		GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
	}
}

void CInfClassCharacter::OnLaserFired(WeaponFireContext *pFireContext)
{
	if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST)
	{
		OnBiologistLaserFired(pFireContext);
		return;
	}

	if(pFireContext->NoAmmo)
	{
		return;
	}

	vec2 Direction = GetDirection();
	int Damage = GameServer()->Tuning()->m_LaserDamage;

	if(GetPlayerClass() == PLAYERCLASS_SNIPER)
	{
		if(PositionIsLocked())
			Damage = 30;
		else
			Damage = random_int(10, 13);
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, GetCID(), Damage, DAMAGE_TYPE::SNIPER_RIFLE);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		//white hole activation in scientist-laser
		
		new CScientistLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach*0.6f, GetCID(), Damage);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if (GetPlayerClass() == PLAYERCLASS_LOOPER) 
	{
		Damage = 5;
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach*0.7f, GetCID(), Damage, DAMAGE_TYPE::LOOPER_LASER);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		CMercenaryBomb* pCurrentBomb = nullptr;
		for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
		{
			if(pBomb->GetOwner() == GetCID())
			{
				pCurrentBomb = pBomb;
				break;
			}
		}

		if(!pCurrentBomb)
		{
			GameServer()->SendBroadcast_Localization(GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, 60, "Bomb needed");
			pFireContext->FireAccepted = false;
		}
		else
		{
			new CMercenaryLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, GetCID());
			GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
		}
	}
	else if (GetPlayerClass() == PLAYERCLASS_NINJA)
	{
		// Do nothing, the processing is done in CInfClassHuman::OnLaserFired()
		return;
	}
	else
	{
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, GetCID(), Damage, DAMAGE_TYPE::LASER);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
}

void CInfClassCharacter::OnNinjaFired(WeaponFireContext *pFireContext)
{
	// The design of ninja supposes different implementation (not via FireWeapon)
}

void CInfClassCharacter::OnMercGrenadeFired(WeaponFireContext *pFireContext)
{
	float BaseAngle = GetAngle(GetDirection());

	//Find bomb
	bool BombFound = false;
	for(CScatterGrenade *pGrenade = (CScatterGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SCATTER_GRENADE); pGrenade; pGrenade = (CScatterGrenade*) pGrenade->TypeNext())
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
	Msg.AddInt(ShotSpread*2+1);

	for(int i = -ShotSpread; i <= ShotSpread; ++i)
	{
		float a = BaseAngle + random_float()/3.0f;

		CScatterGrenade *pProj = new CScatterGrenade(GameServer(), GetCID(), GetPos(), vec2(cosf(a), sinf(a)));

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, GetCID());
	}

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);

	m_ReloadTimer = Server()->TickSpeed()/4;
}

void CInfClassCharacter::OnMedicGrenadeFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->NoAmmo)
		return;

	int HealingExplosionRadius = 4;
	new CGrowingExplosion(GameServer(), GetPos(), GetDirection(), GetCID(), HealingExplosionRadius, GROWING_EXPLOSION_EFFECT::HEAL_HUMANS);

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
}

void CInfClassCharacter::OnBiologistLaserFired(WeaponFireContext *pFireContext)
{
	if(pFireContext->AmmoAvailable < 10)
	{
		pFireContext->NoAmmo = true;
		pFireContext->AmmoConsumed = 0;
		return;
	}

	for(CBiologistMine* pMine = (CBiologistMine*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_BIOLOGIST_MINE); pMine; pMine = (CBiologistMine*) pMine->TypeNext())
	{
		if(pMine->GetOwner() != GetCID()) continue;
			GameWorld()->DestroyEntity(pMine);
	}

	const float BigLaserMaxLength = 400.0f;
	vec2 To = GetPos() + GetDirection() * BigLaserMaxLength;
	if(GameServer()->Collision()->IntersectLine(GetPos(), To, 0x0, &To))
	{
		new CBiologistMine(GameServer(), GetPos(), To, GetCID());
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
		pFireContext->AmmoConsumed = pFireContext->AmmoAvailable;
	}
	else
	{
		pFireContext->FireAccepted = false;
	}
}

void CInfClassCharacter::OpenClassChooser()
{
	if(GameController()->GetRoundType() == ROUND_TYPE::FUN)
	{
		IncreaseArmor(10);
		GetPlayer()->CloseMapMenu();
		return;
	}

	if(!Server()->IsClassChooserEnabled() || Server()->GetClientAlwaysRandom(GetCID()))
	{
		m_pPlayer->SetClass(GameController()->ChooseHumanClass(m_pPlayer));
		if(Server()->IsClassChooserEnabled())
			GiveRandomClassSelectionBonus();
	}
	else
	{
		GetPlayer()->OpenMapMenu(1);
	}
}

void CInfClassCharacter::HandleMapMenu()
{
	CInfClassPlayer *pPlayer = GetPlayer();
	if(GetPlayerClass() != PLAYERCLASS_NONE)
	{
		SetAntiFire();
		pPlayer->CloseMapMenu();
	}
	else
	{
		vec2 CursorPos = vec2(m_Input.m_TargetX, m_Input.m_TargetY);

		if(length(CursorPos) > 100.0f)
		{
			float Angle = 2.0f*pi+atan2(CursorPos.x, -CursorPos.y);
			float AngleStep = 2.0f*pi/static_cast<float>(CMapConverter::NUM_MENUCLASS);
			int HoveredMenuItem = ((int)((Angle+AngleStep/2.0f)/AngleStep))%CMapConverter::NUM_MENUCLASS;
			if(HoveredMenuItem == CMapConverter::MENUCLASS_RANDOM)
			{
				GameServer()->SendBroadcast_Localization(GetCID(),
					BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Random choice"), NULL);
				pPlayer->m_MapMenuItem = HoveredMenuItem;
			}
			else
			{
				int NewClass = CInfClassGameController::MenuClassToPlayerClass(HoveredMenuItem);
				CLASS_AVAILABILITY Availability = GameController()->GetPlayerClassAvailability(NewClass);
				switch(Availability)
				{
					case CLASS_AVAILABILITY::AVAILABLE:
					{
						const char *pClassName = CInfClassGameController::GetClassDisplayName(NewClass);
						GameServer()->SendBroadcast_Localization(GetCID(),
							BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
							pClassName, NULL);
					}
						break;
					case CLASS_AVAILABILITY::DISABLED:
						GameServer()->SendBroadcast_Localization(GetCID(),
							BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
							_("The class is disabled"), NULL);
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
							NULL);
					}
						break;
					case CLASS_AVAILABILITY::LIMIT_EXCEEDED:
						GameServer()->SendBroadcast_Localization(GetCID(),
							BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
							_("The class limit exceeded"), NULL);
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
		}
		else
		{
			pPlayer->m_MapMenuItem = -1;
			GameServer()->SendBroadcast_Localization(GetCID(),
				BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
				_("Choose your class"), NULL);

			return;
		}

		if(pPlayer->MapMenuClickable() && m_Input.m_Fire&1)
		{
			bool Bonus = false;

			int MenuClass = pPlayer->m_MapMenuItem;
			int NewClass = CInfClassGameController::MenuClassToPlayerClass(MenuClass);
			if(NewClass == PLAYERCLASS_NONE)
			{
				NewClass = GameController()->ChooseHumanClass(pPlayer);
				Bonus = true;
			}
			if(NewClass == PLAYERCLASS_INVALID)
			{
				return;
			}

			if(GameController()->GetPlayerClassAvailability(NewClass) == CLASS_AVAILABILITY::AVAILABLE)
			{
				SetAntiFire();
				pPlayer->m_MapMenuItem = 0;
				pPlayer->SetClass(NewClass);
				
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "choose_class player='%s' class='%d' random='%d'",
					Server()->ClientName(GetCID()), NewClass, Bonus);
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);

				if(Bonus)
					GiveRandomClassSelectionBonus();
			}
		}
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
						IncreaseOverallHp(2);
					}
				}
			}
		}
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
	if(m_LastEnforcer >= 0)
	{
		if(Server()->Tick() > m_LastEnforcerTick + Server()->TickSpeed() * LastEnforcerTimeoutInSeconds)
		{
			m_LastEnforcer = -1;
			m_LastEnforcerTick = -1;
		}
	}

	if(m_LastHooker >= 0)
	{
		if(Server()->Tick() > m_LastHookerTick + Server()->TickSpeed() * LastEnforcerTimeoutInSeconds)
		{
			m_LastHooker = -1;
			m_LastHookerTick = -1;
		}
	}
}

void CInfClassCharacter::Die(int Killer, DAMAGE_TYPE DamageType)
{
	if(!IsAlive())
	{
		return;
	}

	dbg_msg("server", "CInfClassCharacter::Die: victim: %d, killer: %d, DT: %d", GetCID(), Killer, static_cast<int>(DamageType));

	if(GetPlayerClass() == PLAYERCLASS_UNDEAD && Killer != GetCID())
	{
		Freeze(10.0, Killer, FREEZEREASON_UNDEAD);
		return;
	}

	bool RefusedToDie = false;
	if(GetClass())
	{
		GetClass()->PrepareToDie(Killer, DamageType, &RefusedToDie);
	}
	if(RefusedToDie)
	{
		return;
	}

	DestroyChildEntities();
/* INFECTION MODIFICATION END *****************************************/

	int Assistant = -1;
	GetActualKillers(Killer, DamageType, &Killer, &Assistant);

	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	GameController()->OnCharacterDeath(this, DamageType, Killer, Assistant);

	// a nice sound
	GameServer()->CreateSound(GetPos(), SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameWorld()->RemoveEntity(this);
	GameWorld()->m_Core.m_apCharacters[GetCID()] = 0;
	GameServer()->CreateDeath(GetPos(), GetCID());
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
	int InfWID = GetInfWeaponID(Weapon);
	int MaxAmmo = Server()->GetMaxAmmo(InfWID);

	if(InfWID == INFWEAPON_NINJA_GRENADE)
		MaxAmmo = minimum(MaxAmmo + m_NinjaAmmoBuff, 10);

	if(Ammo <= 0)
		return;

	if(!m_aWeapons[Weapon].m_Got)
		return;

	int TargetAmmo = maximum(0, m_aWeapons[Weapon].m_Ammo) + Ammo;
	m_aWeapons[Weapon].m_Ammo = minimum(MaxAmmo, TargetAmmo);
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
	m_NeedFullHeal = false;

	GameServer()->CreatePlayerSpawn(GetPos());

	if(GetPlayerClass() == PLAYERCLASS_BAT) {
		if(m_AirJumpCounter < g_Config.m_InfBatAirjumpLimit) {
			EnableJump();
			m_AirJumpCounter++;
		}
	}
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

	if(ClientID > 0)
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

void CInfClassCharacter::EnableJump()
{
	m_Core.EnableJump();
}

bool CInfClassCharacter::IsInvisible() const
{
	return m_IsInvisible;
}

bool CInfClassCharacter::IsInvincible() const
{
	return m_ProtectionTick > 0;
}

bool CInfClassCharacter::HasHallucination() const
{
	return m_HallucinationTick > 0;
}

void CInfClassCharacter::TryUnfreeze()
{
	if(!IsFrozen())
		return;

	CInfClassInfected *pInfected = CInfClassInfected::GetInstance(this);
	if(pInfected && !pInfected->CanBeUnfreezed())
	{
		return;
	}

	Unfreeze();
}

void CInfClassCharacter::MakeBlind(int ClientID, float Duration)
{
	m_BlindnessTicks = Server()->TickSpeed() * Duration;
	m_LastBlinder = ClientID;

	GameServer()->SendEmoticon(GetCID(), EMOTICON_QUESTION);
}

float CInfClassCharacter::WebHookLength() const
{
	if((m_HookMode != 1) && !g_Config.m_InfSpiderCatchHumans)
		return 0;

	if(m_Core.m_HookState != HOOK_GRABBED)
		return 0;

	return distance(m_Core.m_Pos, m_Core.m_HookPos);
}

void CInfClassCharacter::CheckSuperWeaponAccess()
{
	if(m_ResetKillsTime)
		return;

	// check kills of player
	int kills = m_pPlayer->GetNumberKills();

	//Only scientists can receive white holes
	if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		if(GameController()->WhiteHoleEnabled() && !m_HasWhiteHole) // Can't receive a white hole while having one available
		{
			// enable white hole probabilities
			if (kills > g_Config.m_InfWhiteHoleMinimalKills) 
			{
				if (random_int(0,100) < g_Config.m_InfWhiteHoleProbability) 
				{
					//Scientist-laser.cpp will make it unavailable after usage and reset player kills
					
					//create an indicator object
					if (m_HasIndicator == false) {
						m_HasIndicator = true;
						GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_SCORE, _("White hole found, adjusting scientific parameters..."), NULL);
						new CSuperWeaponIndicator(GameServer(), GetPos(), GetCID());
					}
				} 
			} 
		}
	}
}

void CInfClassCharacter::FireSoldierBomb()
{
	vec2 ProjStartPos = GetPos()+GetDirection()*GetProximityRadius()*0.75f;

	for(CSoldierBomb *pBomb = (CSoldierBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SOLDIER_BOMB); pBomb; pBomb = (CSoldierBomb*) pBomb->TypeNext())
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

void CInfClassCharacter::PlaceSlugSlime(WeaponFireContext *pFireContext)
{
	if(IsInLove())
		return;

	vec2 CheckPos = GetPos() + GetDirection() * 64.0f;
	if(GameServer()->Collision()->IntersectLine(GetPos(), CheckPos, 0x0, &CheckPos))
	{
		static const float MinDistance = 84.0f;
		float DistanceToTheNearestSlime = MinDistance * 2;
		for(CSlugSlime* pSlime = (CSlugSlime*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_SLUG_SLIME); pSlime; pSlime = (CSlugSlime*) pSlime->TypeNext())
		{
			const float d = distance(pSlime->GetPos(), GetPos());
			if(d < DistanceToTheNearestSlime)
			{
				DistanceToTheNearestSlime = d;
			}
			if (d <= MinDistance / 2)
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

void CInfClassCharacter::GiveGift(int GiftType)
{
	IncreaseHealth(1);
	IncreaseArmor(4);

	switch(GetPlayerClass())
	{
		case PLAYERCLASS_ENGINEER:
			GiveWeapon(WEAPON_LASER, -1);
			GiveWeapon(WEAPON_GUN, -1);
			break;
		case PLAYERCLASS_SOLDIER:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			break;
		case PLAYERCLASS_SCIENTIST:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_LASER, -1);
			break;
		case PLAYERCLASS_BIOLOGIST:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_LASER, -1);
			GiveWeapon(WEAPON_SHOTGUN, -1);
			break;
		case PLAYERCLASS_LOOPER:
			GiveWeapon(WEAPON_LASER, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			break;
		case PLAYERCLASS_MEDIC:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_SHOTGUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_LASER, -1);
			break;
		case PLAYERCLASS_HERO:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_SHOTGUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_LASER, -1);
			break;
		case PLAYERCLASS_NINJA:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_LASER, -1);
			break;
		case PLAYERCLASS_SNIPER:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_LASER, -1);
			break;
		case PLAYERCLASS_MERCENARY:
			GiveWeapon(WEAPON_GUN, -1);
			GiveWeapon(WEAPON_GRENADE, -1);
			GiveWeapon(WEAPON_LASER, -1);
			break;
	}
}

void CInfClassCharacter::GiveRandomClassSelectionBonus()
{
	IncreaseArmor(10);
}

void CInfClassCharacter::GiveLonelyZombieBonus()
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
		m_ProtectionTick = Server()->TickSpeed() * g_Config.m_InfSpawnProtectionTime;
		if(!IsFrozen() && !IsInvisible())
		{
			SetEmote(EMOTE_SURPRISE, Server()->Tick() + m_ProtectionTick);
		}
	}
}

void CInfClassCharacter::PreCoreTick()
{
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
	}

	UpdateTuningParam();

	if(HasPassenger())
	{
		if(m_Core.m_Passenger->m_Infected || m_Core.m_Infected || m_Core.m_HookProtected)
		{
			m_Core.SetPassenger(nullptr);
		}
	}
}

void CInfClassCharacter::PostCoreTick()
{
	if(GetPlayer()->MapMenu() == 1)
	{
		HandleMapMenu();
	}

	HandleWeaponsRegen();
	HandleHookDraining();
	HandleIndirectKillerCleanup();
}

void CInfClassCharacter::ClassSpawnAttributes()
{
	m_Health = 10;
	m_IsInvisible = false;

	const int PlayerClass = GetPlayerClass();
	const bool isHuman = PlayerClass < END_HUMANCLASS; // PLAYERCLASS_NONE is also a human (not infected) class
	if(isHuman)
	{
		m_pPlayer->m_InfectionTick = -1;
	}
	else
	{
		m_Armor = 0;
	}

	if(PlayerClass == PLAYERCLASS_HERO)
	{
		m_pHeroFlag = nullptr;
	}

	if(PlayerClass != PLAYERCLASS_NONE)
	{
		GameServer()->SendBroadcast_ClassIntro(m_pPlayer->GetCID(), PlayerClass);
		if(!m_pPlayer->IsKnownClass(PlayerClass))
		{
			const char *className = CInfClassGameController::GetClassName(PlayerClass);
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", className, NULL);
			m_pPlayer->m_knownClass[PlayerClass] = true;
		}
	}
}

void CInfClassCharacter::UpdateTuningParam()
{
	CTuningParams* pTuningParams = &m_pPlayer->m_NextTuningParams;
	
	bool NoActions = false;
	bool FixedPosition = false;
	
	if(PositionIsLocked())
	{
		NoActions = true;
		FixedPosition = true;
	}
	if(m_IsFrozen)
	{
		NoActions = true;
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
	
	if(m_HookMode == 1)
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
	
	if(NoActions)
	{
		pTuningParams->m_GroundControlAccel = 0.0f;
		pTuningParams->m_GroundJumpImpulse = 0.0f;
		pTuningParams->m_AirJumpImpulse = 0.0f;
		pTuningParams->m_AirControlAccel = 0.0f;
		pTuningParams->m_HookLength = 0.0f;
	}
	if(FixedPosition || m_Core.m_IsPassenger)
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
