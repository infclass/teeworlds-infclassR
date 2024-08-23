#include "infcplayerclass.h"

#include <base/system.h>
#include <engine/shared/config.h>
#include <game/gamecore.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/infcplayer.h>
#include <game/server/teeinfo.h>

CInfClassPlayerClass::CInfClassPlayerClass(CInfClassPlayer *pPlayer)
	: m_pPlayer(pPlayer)
{
	m_NormalEmote = EMOTE_NORMAL;
}

CGameContext *CInfClassPlayerClass::GameContext() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer();

	return nullptr;
}

// A lot of code use GameServer() as CGameContext* getter, so let it be.
CGameContext *CInfClassPlayerClass::GameServer() const
{
	return GameContext();
}

CGameWorld *CInfClassPlayerClass::GameWorld() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer()->GameWorld();

	return nullptr;
}

CInfClassGameController *CInfClassPlayerClass::GameController() const
{
	if(m_pPlayer)
		return m_pPlayer->GameController();

	return nullptr;
}

CConfig *CInfClassPlayerClass::Config()
{
	if(m_pPlayer)
		return m_pPlayer->GameServer()->Config();

	return nullptr;
}

const CConfig *CInfClassPlayerClass::Config() const
{
	if(m_pPlayer)
		return m_pPlayer->GameServer()->Config();

	return nullptr;
}

IServer *CInfClassPlayerClass::Server() const
{
	if(m_pPlayer)
		return m_pPlayer->Server();

	return nullptr;
}

CInfClassPlayer *CInfClassPlayerClass::GetPlayer()
{
	return m_pPlayer;
}

const CInfClassPlayer *CInfClassPlayerClass::GetPlayer() const
{
	return m_pPlayer;
}

int CInfClassPlayerClass::GetCid() const
{
	const CInfClassPlayer *pPlayer = GetPlayer();
	if(pPlayer)
		return pPlayer->GetCid();

	return -1;
}

vec2 CInfClassPlayerClass::GetPos() const
{
	if(m_pCharacter)
		return m_pCharacter->GetPos();

	return vec2(0, 0);
}

vec2 CInfClassPlayerClass::GetDirection() const
{
	if(m_pCharacter)
		return m_pCharacter->GetDirection();

	return vec2(0, 0);
}

vec2 CInfClassPlayerClass::GetProjectileStartPos(float Offset) const
{
	vec2 From = GetPos();
	vec2 To = From + GetDirection() * Offset;
	GameServer()->Collision()->IntersectLine(From, To, nullptr, &To);

	return To;
}

float CInfClassPlayerClass::GetProximityRadius() const
{
	if(m_pCharacter)
		return m_pCharacter->GetProximityRadius();

	return 0;
}

int CInfClassPlayerClass::GetUpgradeLevel() const
{
	return m_UpgradeLevel;
}

void CInfClassPlayerClass::ResetUpgradeLevel()
{
	m_UpgradeLevel = 0;
}

void CInfClassPlayerClass::SetCharacter(CInfClassCharacter *character)
{
	if(m_pCharacter == character)
	{
		return;
	}

	if(m_pCharacter)
	{
		DestroyChildEntities();
		m_pCharacter->SetClass(nullptr);
	}

	m_pCharacter = character;

	if(m_pCharacter)
	{
		m_pCharacter->SetClass(this);
		GiveClassAttributes();
	}
}

bool CInfClassPlayerClass::IsZombie() const
{
	return !IsHuman();
}

void CInfClassPlayerClass::ResetNormalEmote()
{
	SetNormalEmote(EMOTE_NORMAL);
}

void CInfClassPlayerClass::SetNormalEmote(int Emote)
{
	m_NormalEmote = Emote;
}

int CInfClassPlayerClass::GetDefaultEmote() const
{
	return m_NormalEmote;
}

void CInfClassPlayerClass::GetAmmoRegenParams(int Weapon, WeaponRegenParams *pParams)
{
	EInfclassWeapon InfWID = m_pCharacter->GetInfWeaponId(Weapon);
	pParams->RegenInterval = Server()->GetAmmoRegenTime(InfWID);
	pParams->MaxAmmo = Server()->GetMaxAmmo(InfWID);
}

int CInfClassPlayerClass::GetJumps() const
{
	// From DDNet:

	// Special jump cases:
	// Jumps == -1: A tee may only make one ground jump. Second jumped bit is always set
	// Jumps == 0: A tee may not make a jump. Second jumped bit is always set
	// Jumps == 1: A tee may do either a ground jump or an air jump. Second jumped bit is set after the first jump
	// The second jumped bit can be overridden by special tiles so that the tee can nevertheless jump.

	return 2; // Ground jump + Air jump
}

bool CInfClassPlayerClass::CanDie() const
{
	return true;
}

bool CInfClassPlayerClass::CanBeHit() const
{
	return true;
}

bool CInfClassPlayerClass::CanBeUnfreezed() const
{
	return true;
}

SClassUpgrade CInfClassPlayerClass::GetNextUpgrade() const
{
	return SClassUpgrade::Invalid();
}

float CInfClassPlayerClass::GetHammerProjOffset() const
{
	return GetProximityRadius() * 0.75f;
}

float CInfClassPlayerClass::GetHammerRange() const
{
	return m_pCharacter ? m_pCharacter->GetProximityRadius() * 0.5f : 0;
}

float CInfClassPlayerClass::GetGhoulPercent() const
{
	return 0;
}

EPlayerClass CInfClassPlayerClass::GetPlayerClass() const
{
	if(m_pPlayer)
		return m_pPlayer->GetClass();

	return EPlayerClass::None;
}

void CInfClassPlayerClass::OnPlayerClassChanged()
{
	UpdateSkin();
	SetNormalEmote(EMOTE_NORMAL);

	// Enable hook protection by default for both infected and humans on class changed
	m_pPlayer->SetHookProtection(true);

	if(m_pCharacter)
	{
		GameServer()->CreatePlayerSpawn(GetPos(), GameController()->GetMaskForPlayerWorldEvent(GetCid()));
	}
}

void CInfClassPlayerClass::PrepareToDie(const DeathContext &Context, bool *pRefusedToDie)
{
}

void CInfClassPlayerClass::DisableHealing(float Duration, int From, EDamageType DamageType)
{
	m_HealingDisabledTicks = maximum<int>(m_HealingDisabledTicks, Duration * Server()->TickSpeed());
}

void CInfClassPlayerClass::OnPlayerSnap(int SnappingClient, int InfClassVersion)
{
}

bool CInfClassPlayerClass::IsHealingDisabled() const
{
	return m_HealingDisabledTicks > 0;
}

void CInfClassPlayerClass::OnCharacterPreCoreTick()
{
	if(m_pCharacter->IsPassenger())
	{
		if(m_pCharacter->m_Input.m_Jump && !m_pCharacter->m_PrevInput.m_Jump)
		{
			// Jump off is still in CCharacterCore::UpdateTaxiPassengers()
		}
		else
		{
			m_pCharacter->ResetMovementsInput();
			m_pCharacter->ResetHookInput();
		}
	}
}

void CInfClassPlayerClass::OnCharacterTick()
{
	if(m_HealingDisabledTicks > 0)
	{
		m_HealingDisabledTicks--;
	}

	BroadcastWeaponState();
}

void CInfClassPlayerClass::OnCharacterTickPaused()
{
}

void CInfClassPlayerClass::OnCharacterPostCoreTick()
{
}

void CInfClassPlayerClass::OnCharacterTickDeferred()
{
}

void CInfClassPlayerClass::OnCharacterSnap(int SnappingClient)
{
}

void CInfClassPlayerClass::OnCharacterSpawned(const SpawnContext &Context)
{
	m_HealingDisabledTicks = 0;

	UpdateSkin();
	GiveClassAttributes();
}

void CInfClassPlayerClass::OnCharacterDeath(EDamageType DamageType)
{
	if(m_pCharacter->HasPassenger())
	{
		m_pCharacter->SetPassenger(nullptr);
	}

	DestroyChildEntities();
}

void CInfClassPlayerClass::OnCharacterDamage(SDamageContext *pContext)
{
}

void CInfClassPlayerClass::OnKilledCharacter(CInfClassCharacter *pVictim, const DeathContext &Context)
{
}

void CInfClassPlayerClass::OnHookAttachedPlayer()
{
}

void CInfClassPlayerClass::OnWeaponFired(WeaponFireContext *pFireContext)
{
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

void CInfClassPlayerClass::OnHammerFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnGunFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnShotgunFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnGrenadeFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnLaserFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnNinjaFired(WeaponFireContext *pFireContext)
{
}

void CInfClassPlayerClass::OnFloatingPointCollected(int Points)
{
}

void CInfClassPlayerClass::GiveClassAttributes()
{
	if(!m_pCharacter)
	{
		return;
	}

	m_pCharacter->TakeAllWeapons();
	m_pCharacter->SetJumpsLimit(GetJumps());
}

void CInfClassPlayerClass::DestroyChildEntities()
{
}

void CInfClassPlayerClass::BroadcastWeaponState() const
{
}

void CInfClassPlayerClass::CreateHammerHit(const vec2 &ProjStartPos, const CInfClassCharacter *pTarget)
{
	const vec2 VecToTarget(pTarget->GetPos() - ProjStartPos);
	if(length(VecToTarget) > 0.0f)
		GameServer()->CreateHammerHit(pTarget->GetPos() - normalize(VecToTarget) * pTarget->GetProximityRadius() * 0.5f);
	else
		GameServer()->CreateHammerHit(ProjStartPos);
}

void CInfClassPlayerClass::UpdateSkin()
{
	if(!m_pPlayer)
		return;

	m_pPlayer->UpdateSkin();
}
