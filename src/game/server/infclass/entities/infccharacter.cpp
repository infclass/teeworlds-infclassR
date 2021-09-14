#include "infccharacter.h"

#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>

#include <game/server/entities/projectile.h>

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
#include <game/server/infclass/entities/portal.h>
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
	if(m_pClass)
	{
		// Ideally we would reset the Class character on `CPlayer::m_pCharacter = 0`
		// but it would be hard to hook there.
		m_pClass->SetCharacter(nullptr);
	}
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
	m_PositionLockTick = -1;
	m_PositionLocked = false;
	m_PositionLockAvailable = false;

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
		int Killer = GetCID();
		int Weapon = WEAPON_WORLD;
		GetIndirectKiller(&Killer, &Weapon);

		CInfClassPlayer *pKiller = GameController()->GetPlayer(Killer);

		if(pKiller)
		{
			int ModeSpecial = GameController()->OnCharacterDeath(this, pKiller, Weapon);

			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
				Server()->ClientName(Killer),
				Server()->ClientName(m_pPlayer->GetCID()), Weapon);
			GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

			GameServer()->SendKillMessage(Killer, m_pPlayer->GetCID(), Weapon, ModeSpecial);

			// a nice sound
			GameServer()->CreateSound(GetPos(), SOUND_PLAYER_DIE);
		}

		GetPlayer()->Infect(pKiller);

		if (g_Config.m_InfInfzoneFreezeDuration > 0)
		{
			Freeze(g_Config.m_InfInfzoneFreezeDuration, GetCID(), FREEZEREASON_INFECTION);
		}
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

		GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("You have held a bonus area for one minute, +5 points"), NULL);
		GameServer()->SendEmoticon(m_pPlayer->GetCID(), EMOTICON_MUSIC);
		SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		GiveGift(GIFT_HEROFLAG);

		Server()->RoundStatistics()->OnScoreEvent(m_pPlayer->GetCID(), SCOREEVENT_BONUS, GetPlayerClass(), Server()->ClientName(m_pPlayer->GetCID()), Console());
		GameServer()->SendScoreSound(m_pPlayer->GetCID());
	}
}

void CInfClassCharacter::OnWhiteHoleSpawned(const CWhiteHole *pWhiteHole)
{
	GetPlayer()->ResetNumberKills();
	m_ResetKillsTime = pWhiteHole->LifeSpan() + Server()->TickSpeed() * 3;
}

void CInfClassCharacter::Destroy()
{
	if(m_pClass)
	{
		// Ideally we would reset the Class character on `CPlayer::m_pCharacter = 0`
		// but it would be hard to hook there.
		m_pClass->SetCharacter(nullptr);
	}

	m_pClass = nullptr;

	CCharacter::Destroy();
}

void CInfClassCharacter::Tick()
{
	CCharacter::Tick();

	if(m_pClass)
		m_pClass->OnCharacterTick();
}

void CInfClassCharacter::Snap(int SnappingClient)
{
	int ID = m_pPlayer->GetCID();

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

	if(m_Armor < 10 && SnappingClient != m_pPlayer->GetCID() && IsHuman() && GetPlayerClass() != PLAYERCLASS_HERO)
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
	else if((m_Armor + m_Health) < 10 && SnappingClient != m_pPlayer->GetCID() && IsZombie() && pDestClient && pDestClient->IsZombie())
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
			if(pWall->GetOwner() == m_pPlayer->GetCID())
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
			if(pWall->GetOwner() == m_pPlayer->GetCID())
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

	if(!WillFire || m_pPlayer->MapMenu() > 0)
		return;

	WeaponFireContext FireContext;
	FireContext.Weapon = m_ActiveWeapon;
	FireContext.FireAccepted = true;
	FireContext.AmmoConsumed = 1;
	FireContext.AmmoAvailable = m_aWeapons[m_ActiveWeapon].m_Ammo;
	FireContext.NoAmmo = FireContext.AmmoAvailable == 0;

	OnWeaponFired(&FireContext);

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

bool CInfClassCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon, TAKEDAMAGEMODE Mode)
{
/* INFECTION MODIFICATION START ***************************************/

	//KillerPlayer
	CInfClassPlayer *pKillerPlayer = GameController()->GetPlayer(From);
	CInfClassCharacter *pKillerChar = nullptr;
	if(pKillerPlayer)
		pKillerChar = pKillerPlayer->GetCharacter();

	if(Mode == TAKEDAMAGEMODE_INFECTION)
	{
		if(!pKillerPlayer || !pKillerPlayer->IsZombie() || !IsHuman())
		{
			// The infection is only possible if the killer is a zombie and the target is a human
			Mode = TAKEDAMAGEMODE_NOINFECTION;
		}
	}

	if(GetPlayerClass() == PLAYERCLASS_HERO && Mode == TAKEDAMAGEMODE_INFECTION)
	{
		Dmg = 12;
		// A zombie can't infect a hero
		Mode = TAKEDAMAGEMODE_NOINFECTION;
	}

	if(pKillerChar && pKillerChar->IsInLove())
	{
		Dmg = 0;
		Mode = TAKEDAMAGEMODE_NOINFECTION;
	}

	if((GetPlayerClass() == PLAYERCLASS_HUNTER) && (Weapon == WEAPON_SHOTGUN))
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
		if(AddedForce > 3 && (AddedForce > CurrentSpeed * 0.5))
		{
			UpdateLastEnforcer(From, AddedForce, Weapon, Server()->Tick());
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
				if(pKillerPlayer->GetClass() == PLAYERCLASS_BOOMER && Weapon == WEAPON_HAMMER)
				{
					IncreaseOverallHp(8+random_int(0, 10));
					if(IsFrozen())
						Unfreeze();

					SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
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
		if(Mode == TAKEDAMAGEMODE_SELFHARM)
			Dmg = maximum(1, Dmg/2);
		else
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
		Die(From, Weapon);
		return false;
	}

/* INFECTION MODIFICATION START ***************************************/
	if(Mode == TAKEDAMAGEMODE_INFECTION)
	{
		m_pPlayer->Infect(pKillerPlayer);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
			Server()->ClientName(From),
			Server()->ClientName(GetCID()), Weapon);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

		GameServer()->SendKillMessage(From, GetCID(), WEAPON_HAMMER, 0);
	}
/* INFECTION MODIFICATION END *****************************************/

	if(Dmg > 2)
		GameServer()->CreateSound(GetPos(), SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(GetPos(), SOUND_PLAYER_PAIN_SHORT);

	SetEmote(EMOTE_PAIN, Server()->Tick() + 500 * Server()->TickSpeed() / 1000);

	return true;
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

void CInfClassCharacter::LockPosition()
{
	m_PositionLockTick = Server()->TickSpeed()*15;
	m_PositionLocked = true;
	m_PositionLockAvailable = false;
}

void CInfClassCharacter::UnlockPosition()
{
	m_PositionLockTick = 0;
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
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("Sword velocity increased"), NULL);
			break;
		case 1: //Strength Buff
			m_NinjaStrengthBuff++;
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("Sword strength increased"), NULL);
			break;
		case 2: //Ammo Buff
			m_NinjaAmmoBuff++;
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("Grenade limit increased"), NULL);
			break;
	}
}

int CInfClassCharacter::GetFlagCoolDown()
{
	return m_pHeroFlag ? m_pHeroFlag->GetCoolDown() : 0;
}

bool CInfClassCharacter::GetIndirectKiller(int *pKillerId, int *pWeaponId)
{
	//Search for the real killer
	if(IsFrozen())
	{
		*pKillerId = m_LastFreezer;
		if(m_FreezeReason == FREEZEREASON_FLASH)
		{
			*pWeaponId = WEAPON_GRENADE;
		}
		else
		{
			*pWeaponId = WEAPON_NINJA;
		}
		return true;
	}

	// if hooked
	{
		int CurrentHookerCID = -1;
		for(CInfClassCharacter *pHooker = (CInfClassCharacter*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);
			pHooker;
			pHooker = (CInfClassCharacter *)pHooker->TypeNext())
		{
			if(pHooker->GetPlayer() && pHooker->GetHookedPlayer() == GetCID())
			{
				if(CurrentHookerCID < 0)
				{
					CurrentHookerCID = pHooker->GetCID();
				}
				else
				{
					// More than one player hooked this victim
					// We don't support cooperative killing.
					// Break this loop and check for the freezer instead.
					CurrentHookerCID = -1;
					break;
				}
			}
		}

		if(CurrentHookerCID >= 0)
		{
			*pKillerId = CurrentHookerCID;
			*pWeaponId = WEAPON_NINJA;
			return true;
		}
	}

	const float LastEnforcerTimeoutInSeconds = Config()->m_InfLastEnforcerTimeMs / 1000.0f;
	if(m_LastEnforcer >= 0 && (m_LastEnforcerTick > m_LastHookerTick))
	{
		*pKillerId = m_LastEnforcer;
		*pWeaponId = m_LastEnforcerWeapon;
		return true;
	}

	if(m_LastHooker >= 0)
	{
		*pKillerId = m_LastHooker;
		*pWeaponId = WEAPON_NINJA;
		return true;
	}

	return false;
}

void CInfClassCharacter::UpdateLastHooker(int ClientID, int HookerTick)
{
	m_LastHooker = ClientID;
	m_LastHookerTick = HookerTick;
}

void CInfClassCharacter::UpdateLastEnforcer(int ClientID, float Force, int Weapon, int Tick)
{
	m_LastEnforcer = ClientID;
	m_LastEnforcerWeapon = Weapon;
	m_LastEnforcerTick = Tick;
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
			if(pWall->GetOwner() == m_pPlayer->GetCID())
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
				new CEngineerWall(GameServer(), m_FirstShotCoord, GetPos(), m_pPlayer->GetCID());
				GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_LOOPER)
	{
		//Potential variable name conflicts with engineers wall (for example *pWall is used twice for both Looper and Engineer)
		for(CLooperWall *pWall = (CLooperWall*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_LOOPER_WALL); pWall; pWall = (CLooperWall*) pWall->TypeNext())
		{
			if(pWall->GetOwner() == m_pPlayer->GetCID())
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
				
				new CLooperWall(GameServer(), m_FirstShotCoord, GetPos(), m_pPlayer->GetCID());
				
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
				new CTurret(GameServer(), GetPos(), m_pPlayer->GetCID(), Direction, CTurret::LASER);
			}
			else if (g_Config.m_InfTurretEnablePlasma)
			{
				new CTurret(GameServer(), GetPos(), m_pPlayer->GetCID(), Direction, CTurret::PLASMA);
			}

			GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
			m_TurretCount--;
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "placed turret, %i left", m_TurretCount);
			GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, aBuf, NULL);
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
		if(GetPos().y > -600.0)
		{
			if(m_PositionLockTick <= 0 && m_PositionLockAvailable)
			{
				LockPosition();
			}
			else
			{
				UnlockPosition();
			}
		}
	}
	else if(GetPlayerClass() == PLAYERCLASS_MERCENARY && g_Config.m_InfMercLove && !GameServer()->m_FunRound)
	{
		CMercenaryBomb* pCurrentBomb = NULL;
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
			if(pCurrentBomb->ReadyToExplode() || distance(pCurrentBomb->GetPos(), GetPos()) > 80.0f)
				pCurrentBomb->Explode();
			else
			{
				pCurrentBomb->IncreaseDamage(WEAPON_HAMMER);
				GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);
			}
		}
		else
		{
			new CMercenaryBomb(GameServer(), GetPos(), m_pPlayer->GetCID());
			GameServer()->CreateSound(GetPos(), SOUND_PICKUP_ARMOR);
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
			
			if(p->GetOwner() == m_pPlayer->GetCID())
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
			
			new CScientistMine(GameServer(), ProjStartPos, m_pPlayer->GetCID());
			
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
			Die(m_pPlayer->GetCID(), WEAPON_SELF);
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
							pTarget->Unfreeze();
							GameServer()->ClearBroadcast(pTarget->GetCID(), BROADCAST_PRIORITY_EFFECTSTATE);
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
						TAKEDAMAGEMODE DamageMode = TAKEDAMAGEMODE_INFECTION;

						if(GetPlayerClass() == PLAYERCLASS_BAT)
						{
							Damage = g_Config.m_InfBatDamage;
							DamageMode = TAKEDAMAGEMODE_NOINFECTION;
						}

						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, Damage,
							GetCID(), m_ActiveWeapon, DamageMode);
					}
				}
				else if(GetPlayerClass() == PLAYERCLASS_BIOLOGIST || GetPlayerClass() == PLAYERCLASS_MERCENARY)
				{
					/* affects mercenary only if love bombs are disabled. */
					if (pTarget->IsZombie())
					{
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20, 
								m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
					}
				}
				else if(GetPlayerClass() == PLAYERCLASS_MEDIC)
				{
					if (pTarget->IsZombie())
					{
						pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, 20, 
								m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
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
						m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
				}
/* INFECTION MODIFICATION END *****************************************/
				Hits++;

				// set his velocity to fast upward (for now)
				if(length(pTarget->GetPos()-ProjStartPos) > 0.0f)
					GameServer()->CreateHammerHit(pTarget->GetPos()-normalize(pTarget->GetPos()-ProjStartPos)*m_ProximityRadius*0.5f);
				else
					GameServer()->CreateHammerHit(ProjStartPos);
			}

			for(CPortal* pPortal = (CPortal*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_PORTAL); pPortal; pPortal = (CPortal*) pPortal->TypeNext())
			{
				if(m_pPlayer->IsZombie())
					continue;

				if(pPortal->GetOwner() == m_pPlayer->GetCID())
					continue;

				if(distance(GetPos(), pPortal->GetPos()) > (pPortal->m_ProximityRadius + m_ProximityRadius*0.5f))
					continue;

				pPortal->TakeDamage(g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage, m_pPlayer->GetCID(), m_ActiveWeapon, TAKEDAMAGEMODE_NOINFECTION);
				Hits++;
			}
		}

		// if we Hit anything, we have to wait for the reload
		if(Hits)
		{
			m_ReloadTimer = Server()->TickSpeed()/3;
		}
		else if(GetPlayerClass() == PLAYERCLASS_SLUG)
		{
			vec2 CheckPos = GetPos() + Direction * 64.0f;
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
							pSlime->Replenish(m_pPlayer->GetCID());
							ShowAttackAnimation = true;
							break;
						}
					}
				}

				if(DistanceToTheNearestSlime > MinDistance)
				{
					ShowAttackAnimation = true;
					new CSlugSlime(GameServer(), CheckPos, m_pPlayer->GetCID());
				}
			}
		}

		if(!ShowAttackAnimation)
		{
			pFireContext->FireAccepted = false;
			return;
		}

		GameServer()->CreateSound(GetPos(), SOUND_HAMMER_FIRE);
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
		CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
			m_pPlayer->GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
			1, 0, 0, -1, WEAPON_GUN);

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
		Msg.AddInt(1);
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);

		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
		
		float MaxSpeed = GameServer()->Tuning()->m_GroundControlSpeed*1.7f;
		vec2 Recoil = Direction*(-MaxSpeed/5.0f);
		SaturateVelocity(Recoil, MaxSpeed);

		GameServer()->CreateSound(GetPos(), SOUND_HOOK_LOOP);
	}
	else
	{
		CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
			m_pPlayer->GetCID(),
			ProjStartPos,
			Direction,
			(int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),
			1, 0, 0, -1, WEAPON_GUN);

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
		Msg.AddInt(1);
		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);

		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

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
	if(GetPlayerClass() == PLAYERCLASS_MEDIC)
		Force = 10.0f;

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
			CBouncingBullet *pProj = new CBouncingBullet(GameServer(), m_pPlayer->GetCID(), ProjStartPos, vec2(cosf(a), sinf(a))*Speed);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
		}
		else
		{
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				vec2(cosf(a), sinf(a))*Speed,
				(int)(Server()->TickSpeed()*LifeTime),
				1, 0, Force, -1, WEAPON_SHOTGUN);

			// pack the Projectile and send it to the client Directly
			CNetObj_Projectile p;
			pProj->FillInfo(&p);
			for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
				Msg.AddInt(((int *)&p)[i]);
		}
	}

	Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

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
				auto pScientist = GetPlayer()->GetCharacter();
				pScientist->TakeDamage(vec2(0.0f, 0.0f), g_Config.m_InfScientistTpSelfharm * 2, GetCID(), WEAPON_HAMMER, TAKEDAMAGEMODE_SELFHARM);
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
		CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
											 m_pPlayer->GetCID(),
											 ProjStartPos,
											 Direction,
											 (int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),
											 1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

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
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());

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

	if(CanOpenPortals())
	{
		PlacePortal(pFireContext);
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
		if(m_PositionLocked)
			Damage = 30;
		else
			Damage = random_int(10, 13);
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if(GetPlayerClass() == PLAYERCLASS_SCIENTIST)
	{
		//white hole activation in scientist-laser
		
		new CScientistLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach*0.6f, m_pPlayer->GetCID(), Damage);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if (GetPlayerClass() == PLAYERCLASS_LOOPER) 
	{
		Damage = 5;
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach*0.7f, m_pPlayer->GetCID(), Damage);
		GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
	}
	else if(GetPlayerClass() == PLAYERCLASS_MERCENARY)
	{
		CMercenaryBomb* pCurrentBomb = nullptr;
		for(CMercenaryBomb *pBomb = (CMercenaryBomb*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MERCENARY_BOMB); pBomb; pBomb = (CMercenaryBomb*) pBomb->TypeNext())
		{
			if(pBomb->GetOwner() == m_pPlayer->GetCID())
			{
				pCurrentBomb = pBomb;
				break;
			}
		}

		if(!pCurrentBomb)
		{
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(), BROADCAST_PRIORITY_WEAPONSTATE, 60, "Bomb needed");
			pFireContext->FireAccepted = false;
		}
		else
		{
			new CMercenaryLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, GetCID());
			GameServer()->CreateSound(GetPos(), SOUND_LASER_FIRE);
		}
	}
	else
	{
		new CInfClassLaser(GameServer(), GetPos(), Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), Damage);
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
		if(pGrenade->GetOwner() != m_pPlayer->GetCID())
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

		CScatterGrenade *pProj = new CScatterGrenade(GameServer(), m_pPlayer->GetCID(), GetPos(), vec2(cosf(a), sinf(a)));

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
	}

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);

	m_ReloadTimer = Server()->TickSpeed()/4;
}

void CInfClassCharacter::OnMedicGrenadeFired(WeaponFireContext *pFireContext)
{
	float BaseAngle = GetAngle(GetDirection());

	//Find bomb
	bool BombFound = false;
	for(CMedicGrenade *pGrenade = (CMedicGrenade*) GameWorld()->FindFirst(CGameWorld::ENTTYPE_MEDIC_GRENADE); pGrenade; pGrenade = (CMedicGrenade*) pGrenade->TypeNext())
	{
		if(pGrenade->GetOwner() != m_pPlayer->GetCID()) continue;
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

	int ShotSpread = 0;

	CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
	Msg.AddInt(ShotSpread*2+1);

	for(int i = -ShotSpread; i <= ShotSpread; ++i)
	{
		float a = BaseAngle + random_float()/5.0f;

		CMedicGrenade *pProj = new CMedicGrenade(GameServer(), m_pPlayer->GetCID(), GetPos(), vec2(cosf(a), sinf(a)));

		// pack the Projectile and send it to the client Directly
		CNetObj_Projectile p;
		pProj->FillInfo(&p);

		for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
			Msg.AddInt(((int *)&p)[i]);
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
	}

	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);

	m_ReloadTimer = Server()->TickSpeed()/4;
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
		if(pMine->GetOwner() != m_pPlayer->GetCID()) continue;
			GameWorld()->DestroyEntity(pMine);
	}

	const float BigLaserMaxLength = 400.0f;
	vec2 To = GetPos() + GetDirection() * BigLaserMaxLength;
	if(GameServer()->Collision()->IntersectLine(GetPos(), To, 0x0, &To))
	{
		new CBiologistMine(GameServer(), GetPos(), To, m_pPlayer->GetCID());
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
	if(GameServer()->m_FunRound)
	{
		IncreaseArmor(10);
		m_pPlayer->CloseMapMenu();
		return;
	}

	if(!Server()->IsClassChooserEnabled() || Server()->GetClientAlwaysRandom(m_pPlayer->GetCID()))
	{
		m_pPlayer->SetClass(GameController()->ChooseHumanClass(m_pPlayer));
		if(Server()->IsClassChooserEnabled())
			GiveRandomClassSelectionBonus();
	}
	else
	{
		m_pPlayer->OpenMapMenu(1);
	}
}

void CInfClassCharacter::HandleMapMenu()
{
	if(GetPlayerClass() != PLAYERCLASS_NONE)
	{
		SetAntiFire();
		m_pPlayer->CloseMapMenu();
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
				GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
					BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME, _("Random choice"), NULL);
				m_pPlayer->m_MapMenuItem = HoveredMenuItem;
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
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
							BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
							pClassName, NULL);
					}
						break;
					case CLASS_AVAILABILITY::DISABLED:
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
							BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
							_("The class is disabled"), NULL);
						break;
					case CLASS_AVAILABILITY::NEED_MORE_PLAYERS:
					{
						int MinPlayers = Server()->GetMinPlayersForClass(NewClass);
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
							BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
							_P("Need at least {int:MinPlayers} player",
							   "Need at least {int:MinPlayers} players"),
							"MinPlayers", &MinPlayers,
							NULL);
					}
						break;
					case CLASS_AVAILABILITY::LIMIT_EXCEEDED:
						GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
							BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
							_("The class limit exceeded"), NULL);
						break;
				}

				if(Availability == CLASS_AVAILABILITY::AVAILABLE)
				{
					m_pPlayer->m_MapMenuItem = HoveredMenuItem;
				}
				else
				{
					m_pPlayer->m_MapMenuItem = -1;
				}
			}
		}
		else
		{
			m_pPlayer->m_MapMenuItem = -1;
			GameServer()->SendBroadcast_Localization(m_pPlayer->GetCID(),
				BROADCAST_PRIORITY_INTERFACE, BROADCAST_DURATION_REALTIME,
				_("Choose your class"), NULL);

			return;
		}

		if(m_pPlayer->MapMenuClickable() && m_Input.m_Fire&1)
		{
			bool Bonus = false;

			int MenuClass = m_pPlayer->m_MapMenuItem;
			int NewClass = CInfClassGameController::MenuClassToPlayerClass(MenuClass);
			if(NewClass == PLAYERCLASS_NONE)
			{
				NewClass = GameController()->ChooseHumanClass(m_pPlayer);
				Bonus = true;
			}
			if(NewClass == PLAYERCLASS_INVALID)
			{
				return;
			}

			if(GameController()->GetPlayerClassAvailability(NewClass) == CLASS_AVAILABILITY::AVAILABLE)
			{
				SetAntiFire();
				m_pPlayer->m_MapMenuItem = 0;
				m_pPlayer->SetClass(NewClass);
				
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "choose_class player='%s' class='%d' random='%d'",
					Server()->ClientName(m_pPlayer->GetCID()), NewClass, Bonus);
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
			CCharacter *VictimChar = GameServer()->GetPlayerChar(m_Core.m_HookedPlayer);
			if(VictimChar)
			{
				float Rate = 1.0f;
				int Damage = 1;

				if(GetPlayerClass() == PLAYERCLASS_SMOKER)
				{
					Rate = 0.5f;
					Damage = g_Config.m_InfSmokerHookDamage;
				}
				else if(GetPlayerClass() == PLAYERCLASS_GHOUL)
				{
					Rate = 0.56f + 0.44f * (1.0f-m_pClass->GetGhoulPercent());
				}

				if(m_HookDmgTick + Server()->TickSpeed()*Rate < Server()->Tick())
				{
					m_HookDmgTick = Server()->Tick();
					VictimChar->TakeDamage(vec2(0.0f,0.0f), Damage, m_pPlayer->GetCID(), WEAPON_NINJA, TAKEDAMAGEMODE_NOINFECTION);
					if((GetPlayerClass() == PLAYERCLASS_SMOKER || GetPlayerClass() == PLAYERCLASS_BAT) && VictimChar->IsHuman())
						IncreaseOverallHp(2);
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

void CInfClassCharacter::Die(int Killer, int Weapon)
{
/* INFECTION MODIFICATION START ***************************************/
	if(GetPlayerClass() == PLAYERCLASS_UNDEAD && Killer != m_pPlayer->GetCID())
	{
		Freeze(10.0, Killer, FREEZEREASON_UNDEAD);
		return;
	}

	bool RefusedToDie = false;
	GetClass()->PrepareToDie(Killer, Weapon, &RefusedToDie);
	if (RefusedToDie)
	{
		return;
	}

	DestroyChildEntities();
/* INFECTION MODIFICATION END *****************************************/

	if(((Weapon == WEAPON_WORLD) || (Weapon == WEAPON_SELF)) && Killer == m_pPlayer->GetCID())
	{
		GetIndirectKiller(&Killer, &Weapon);
	}

	CInfClassCharacter *pKillerCharacter = GameController()->GetCharacter(Killer);

	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	CInfClassPlayer* pKillerPlayer = GameController()->GetPlayer(Killer);
	int ModeSpecial = GameController()->OnCharacterDeath(this, pKillerPlayer, Weapon);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
		Server()->ClientName(Killer),
		Server()->ClientName(m_pPlayer->GetCID()), Weapon);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	GameServer()->SendKillMessage(Killer, m_pPlayer->GetCID(), Weapon, ModeSpecial);

	// a nice sound
	GameServer()->CreateSound(GetPos(), SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameWorld()->RemoveEntity(this);
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(GetPos(), m_pPlayer->GetCID());

	GetClass()->OnCharacterDeath(Weapon);
/* INFECTION MODIFICATION START ***************************************/

	if(GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		m_pPlayer->StartInfection(true, pKillerPlayer);
		GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The witch is dead"), NULL);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else if(GetPlayerClass() == PLAYERCLASS_UNDEAD)
	{
		m_pPlayer->StartInfection(true, pKillerPlayer);
		GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The undead is finally dead"), NULL);
		GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
	}
	else
	{
		m_pPlayer->Infect(pKillerPlayer);
	}
	if (m_Core.m_Passenger) {
		m_Core.SetPassenger(nullptr);
	}
/* INFECTION MODIFICATION END *****************************************/

	if(pKillerCharacter && (pKillerCharacter != this))
	{
		// set attacker's face to happy (taunt!)
		pKillerCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		pKillerCharacter->CheckSuperWeaponAccess();

		if(pKillerCharacter->GetPlayerClass() == PLAYERCLASS_MERCENARY)
		{
			pKillerCharacter->GiveWeapon(WEAPON_LASER, m_aWeapons[WEAPON_LASER].m_Ammo + 3);
		}
	}
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

int CInfClassCharacter::GetCID() const
{
	if(m_pPlayer)
	{
		return m_pPlayer->GetCID();
	}

	return -1;
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
	return m_pClass && m_pClass->CanDie();
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
		if (!m_HasWhiteHole) // Can't receive a white hole while having one available
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
						GameServer()->SendChatTarget_Localization(m_pPlayer->GetCID(), CHATCATEGORY_SCORE, _("white hole found, adjusting scientific parameters..."), NULL);
						new CSuperWeaponIndicator(GameServer(), GetPos(), m_pPlayer->GetCID());
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
		if(pBomb->GetOwner() == m_pPlayer->GetCID())
		{
			pBomb->Explode();
			return;
		}
	}

	new CSoldierBomb(GameServer(), ProjStartPos, m_pPlayer->GetCID());
	GameServer()->CreateSound(GetPos(), SOUND_GRENADE_FIRE);
}

void CInfClassCharacter::PlacePortal(WeaponFireContext *pFireContext)
{
	if(IsFrozen() && IsInLove())
		return;

	vec2 TargetPos = GetPos();

	m_ReloadTimer = Server()->TickSpeed() / 4;

	if(GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		if(!FindWitchSpawnPosition(TargetPos))
		{
			// Witch can't place the portal here
			pFireContext->FireAccepted = false;
			return;
		}
	}

	CPortal *PortalToTake = FindPortalInTarget();

	if(PortalToTake)
	{
		PortalToTake->Disconnect();
		GameWorld()->DestroyEntity(PortalToTake);

		if (PortalToTake == m_pPortalIn)
			m_pPortalIn = nullptr;
		if (PortalToTake == m_pPortalOut)
			m_pPortalOut = nullptr;

		GiveWeapon(WEAPON_LASER, m_aWeapons[WEAPON_LASER].m_Ammo + 1);
		return;
	}

	if(pFireContext->NoAmmo)
	{
		return;
	}

	// Place new portal
	int OwnerCID = GetPlayer() ? GetCID() : -1;
	CPortal *existingPortal = m_pPortalIn ? m_pPortalIn : m_pPortalOut;
	if(existingPortal && distance(existingPortal->GetPos(), TargetPos) < g_Config.m_InfMinPortalDistance)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Unable to place portals that close to each other");
		GameServer()->SendChatTarget(OwnerCID, aBuf);
		return;
	}

	if(TargetPos.y < -20)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "Unable to open a portal at this height");
		GameServer()->SendChatTarget(OwnerCID, aBuf);
		return;
	}

	if(m_pPortalIn && m_pPortalOut)
	{
		m_pPortalOut->Disconnect();
		GameWorld()->DestroyEntity(m_pPortalOut);
		m_pPortalOut = nullptr;
	}

	if(m_pPortalIn)
	{
		m_pPortalOut = new CPortal(GameServer(), TargetPos, OwnerCID, CPortal::PortalType::Out);
		m_pPortalOut->ConnectPortal(m_pPortalIn);
		GameServer()->CreateSound(m_pPortalOut->GetPos(), m_pPortalOut->GetNewEntitySound());
	}
	else
	{
		m_pPortalIn = new CPortal(GameServer(), TargetPos, OwnerCID, CPortal::PortalType::In);
		m_pPortalIn->ConnectPortal(m_pPortalOut);
		GameServer()->CreateSound(m_pPortalIn->GetPos(), m_pPortalIn->GetNewEntitySound());
	}
}

CPortal *CInfClassCharacter::FindPortalInTarget()
{
	vec2 TargetPos = GetPos();

	if (GetPlayerClass() == PLAYERCLASS_WITCH)
	{
		if(!FindWitchSpawnPosition(TargetPos))
		{
			// Witch can't place the portal here
			return nullptr;
		}
	}

	// Check if unmount wanted
	const int displacementExtraDistance = 20;
	if(m_pPortalIn && (distance(m_pPortalIn->GetPos(), TargetPos) < m_ProximityRadius + m_pPortalIn->GetRadius() + displacementExtraDistance))
	{
		return m_pPortalIn;
	}

	if(m_pPortalOut && (distance(m_pPortalOut->GetPos(), TargetPos) < m_ProximityRadius + m_pPortalOut->GetRadius() + displacementExtraDistance))
	{
		return m_pPortalOut;
	}

	return nullptr;
}

void CInfClassCharacter::OnPortalDestroy(CPortal *pPortal)
{
	if (!pPortal)
		return;

	if (m_pPortalIn == pPortal)
	{
		m_pPortalIn->Disconnect();
		m_pPortalIn = nullptr;
	}
	else if (m_pPortalOut == pPortal)
	{
		m_pPortalOut->Disconnect();
		m_pPortalOut = nullptr;
	}
}

bool CInfClassCharacter::ProcessCharacterOnPortal(CPortal *pPortal, CCharacter *pCharacter)
{
	switch (GetPlayerClass())
	{
		case PLAYERCLASS_WITCH:
			if (pPortal->GetPortalType() != CPortal::PortalType::In)
				return false;

			if(!pCharacter->IsZombie())
				return false;

			if (pCharacter == this)
				return false;

			break;

		default:
			return false;
	}

	// The idea here is to have a point to catch all allowed teleportations
	SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
	GameServer()->SendEmoticon(GetCID(), EMOTICON_MUSIC);

	Server()->RoundStatistics()->OnScoreEvent(m_pPlayer->GetCID(), SCOREEVENT_PORTAL_USED, GetPlayerClass(), Server()->ClientName(m_pPlayer->GetCID()), Console());
	GameServer()->SendScoreSound(m_pPlayer->GetCID());

	return true;
}

bool CInfClassCharacter::CanOpenPortals() const
{
	return m_canOpenPortals;
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
	if(m_pPlayer->MapMenu() == 1)
	{
		HandleMapMenu();
	}

	HandleWeaponsRegen();
	HandleHookDraining();
	HandleIndirectKillerCleanup();
}

void CInfClassCharacter::UpdateTuningParam()
{
	CTuningParams* pTuningParams = &m_pPlayer->m_NextTuningParams;
	
	bool NoActions = false;
	bool FixedPosition = false;
	
	if(m_PositionLocked)
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
