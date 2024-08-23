#include "infcplayer.h"

#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/events-director.h>

#include "classes/humans/human.h"
#include "classes/infcplayerclass.h"
#include "classes/infected/infected.h"
#include "engine/server.h"
#include "entities/infccharacter.h"

MACRO_ALLOC_POOL_ID_IMPL(CInfClassPlayer, MAX_CLIENTS)

CInfClassPlayer::CInfClassPlayer(CInfClassGameController *pGameController, int ClientId, int Team)
	: CPlayer(pGameController->GameServer(), ClientId, Team)
	, m_pGameController(pGameController)
{
	m_class = EPlayerClass::Invalid;
	m_PreferredClass = EPlayerClass::Invalid;

	m_InfectionTick = -1;
	m_HookProtection = false;
	m_HookProtectionAutomatic = true;

	SetClass(EPlayerClass::None);
}

CInfClassPlayer::~CInfClassPlayer()
{
	SetCharacterClass(nullptr);
}

CInfClassGameController *CInfClassPlayer::GameController() const
{
	return m_pGameController;
}

void CInfClassPlayer::TryRespawn()
{
	SpawnContext Context;
	if(!GameController()->TryRespawn(this, &Context))
		return;

	m_Spawning = false;
	CInfClassCharacter *pCharacter = new(m_ClientId) CInfClassCharacter(GameController());

	m_pCharacter = pCharacter;
	m_pCharacter->Spawn(this, Context.SpawnPos);
	OnCharacterSpawned(Context);

	GameServer()->CreatePlayerSpawn(Context.SpawnPos, GameController()->GetMaskForPlayerWorldEvent(m_ClientId));
}

int CInfClassPlayer::GetScore(int SnappingClient) const
{
	if(GetTeam() == TEAM_SPECTATORS)
	{
	}
	else
	{
		if(GameController()->GetRoundType() == ERoundType::Survival)
		{
			return m_Kills;
		}

		return Server()->RoundStatistics()->PlayerScore(m_ClientId);
	}

	return CPlayer::GetScore(SnappingClient);
}

void CInfClassPlayer::Tick()
{
	if(!Server()->ClientIngame(m_ClientId))
		return;

	HandleInfection();

	CPlayer::Tick();

	if(!GameServer()->m_World.m_Paused)
	{
		if(IsHuman())
			m_HumanTime++;
	}

	if(m_MapMenu > 0)
		m_MapMenuTick++;

	if(GetClass() == EPlayerClass::Ghoul)
	{
		if(m_GhoulLevel > 0)
		{
			m_GhoulLevelTick--;

			if(m_GhoulLevelTick <= 0)
			{
				m_GhoulLevelTick = (Server()->TickSpeed() * GameServer()->Config()->m_InfGhoulDigestion);
				IncreaseGhoulLevel(-1);
				GetCharacterClass()->UpdateSkin();
			}
		}
	}

	HandleTuningParams();

	if(!GameServer()->m_World.m_Paused)
	{
		if(m_FollowTargetTicks > 0)
		{
			--m_FollowTargetTicks;
			if(m_FollowTargetTicks == 0)
			{
				m_FollowTargetId = -1;
			}
			else if(IsForcedToSpectate())
			{
				const CInfClassPlayer *pFollowedPlayer = GameController()->GetPlayer(TargetToFollow());
				m_ViewPos = pFollowedPlayer->m_ViewPos;
			}
			else
			{
				ResetTheTargetToFollow();
			}
		}
	}
}

void CInfClassPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags & PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aCurLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	UpdateSpectatorPos();
}

void CInfClassPlayer::Snap(int SnappingClient)
{
	if(!Server()->ClientIngame(m_ClientId))
		return;

	CPlayer::Snap(SnappingClient);

	CNetObj_DDNetPlayer *pDDNetPlayer = Server()->SnapNewItem<CNetObj_DDNetPlayer>(m_ClientId);
	if(!pDDNetPlayer)
		return;

	pDDNetPlayer->m_AuthLevel = 0; // Server()->GetAuthedState(id);
	pDDNetPlayer->m_Flags = 0;
	if(m_Afk)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_AFK;
	if(m_FollowTargetTicks > 0)
		pDDNetPlayer->m_Flags |= EXPLAYERFLAG_SPEC;

	int InfClassVersion = Server()->GetClientInfclassVersion(SnappingClient);

	if(InfClassVersion)
	{
		CNetObj_InfClassPlayer *pInfClassPlayer = Server()->SnapNewItem<CNetObj_InfClassPlayer>(m_ClientId);
		if(!pInfClassPlayer)
			return;

		pInfClassPlayer->m_Class = toNetValue(m_class);
		pInfClassPlayer->m_Flags = 0;
		if(IsInfected())
		{
			pInfClassPlayer->m_Flags |= INFCLASS_PLAYER_FLAG_INFECTED;
		}
		if(!HookProtectionEnabled())
		{
			pInfClassPlayer->m_Flags |= INFCLASS_PLAYER_FLAG_HOOK_PROTECTION_OFF;
		}
		// Note:
		// INFCLASS_PLAYER_FLAG_FORCED_TO_SPECTATE flag is deprecated because
		// EXPLAYERFLAG_SPEC does the same thing in a more compatible way

		pInfClassPlayer->m_Kills = m_Kills;
		pInfClassPlayer->m_Deaths = m_Deaths;
		pInfClassPlayer->m_Assists = m_Assists;
		pInfClassPlayer->m_Score = m_Score;

		GetCharacterClass()->OnPlayerSnap(SnappingClient, InfClassVersion);
	}

	if(!IsSpectator() && (m_FollowTargetTicks > 0) && (SnappingClient == m_ClientId))
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = Server()->SnapNewItem<CNetObj_SpectatorInfo>(m_ClientId);
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorId = TargetToFollow();
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CInfClassPlayer::SnapClientInfo(int SnappingClient, int SnappingClientMappedId)
{
	CNetObj_ClientInfo *pClientInfo = Server()->SnapNewItem<CNetObj_ClientInfo>(SnappingClientMappedId);
	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientId));
	StrToInts(&pClientInfo->m_Clan0, 3, GetClan(SnappingClient));
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientId);

	const CWeakSkinInfo SkinInfo = GetSkinInfo(SnappingClient);

	StrToInts(&pClientInfo->m_Skin0, 6, SkinInfo.pSkinName);
	pClientInfo->m_UseCustomColor = SkinInfo.UseCustomColor;
	pClientInfo->m_ColorBody = SkinInfo.ColorBody;
	pClientInfo->m_ColorFeet = SkinInfo.ColorFeet;
}

void CInfClassPlayer::HandleInfection()
{
	if(m_InfectionType == INFECTION_TYPE::NO)
	{
		return;
	}
	if(IsInfected() && (m_InfectionType == INFECTION_TYPE::REGULAR))
	{
		// Do not infect if inf class already set
		m_InfectionType = INFECTION_TYPE::NO;
		return;
	}

	const EPlayerClass PreviousClass = GetClass();
	CInfClassPlayer *pInfectiousPlayer = GameController()->GetPlayer(m_InfectiousPlayerCid);

	m_InfectionType = INFECTION_TYPE::NO;
	m_InfectiousPlayerCid = -1;

	GameController()->DoPlayerInfection(this, pInfectiousPlayer, PreviousClass);
}

void CInfClassPlayer::KillCharacter(int Weapon)
{
	if(!m_pCharacter)
		return;

	// Character actually died / removed from the world
	constexpr icArray<EPlayerClass, 2> EphemeralClasses = {
		EPlayerClass::Undead,
		EPlayerClass::Witch,
	};

	if((Weapon == WEAPON_SELF) && (IsHuman() || EphemeralClasses.Contains(GetClass())))
	{
		static const float SelfKillConfirmationTime = 3;
		if(Server()->Tick() > m_SelfKillAttemptTick + Server()->TickSpeed() * SelfKillConfirmationTime)
		{
			GameServer()->SendChatTarget_Localization(GetCid(), CHATCATEGORY_PLAYER,
				_("Self kill attempt prevented. Trigger self kill again to confirm."));
			m_SelfKillAttemptTick = Server()->Tick();
			// Reset last kill tick:
			m_LastKill = -1; // This could be done in the GameContext but let's keep it here to avoid conflicts
			return;
		}
	}

	CPlayer::KillCharacter(Weapon);

	if(!m_pCharacter && (Weapon != WEAPON_GAME))
	{
		for(int i = m_PreviousClasses.Size() - 1; i >= 0; i--)
		{
			if(EphemeralClasses.Contains(m_PreviousClasses.At(i)))
			{
				m_PreviousClasses.RemoveAt(i);
			}
		}
	}
}

int CInfClassPlayer::GetDefaultEmote() const
{
	if(m_pInfcPlayerClass)
		return m_pInfcPlayerClass->GetDefaultEmote();

	return CPlayer::GetDefaultEmote();
}

CWeakSkinInfo CInfClassPlayer::GetSkinInfo(int SnappingClient) const
{
	CWeakSkinInfo SkinInfo;
	if(m_SkinGetter)
	{
		IServer::CClientInfo ClientInfo = {0};
		if(SnappingClient != SERVER_DEMO_CLIENT)
		{
			Server()->GetClientInfo(SnappingClient, &ClientInfo);
		}

		CInfClassPlayer *pSnappingClient = GameController()->GetPlayer(SnappingClient);

		bool SameTeam = pSnappingClient && (m_Team == pSnappingClient->m_Team) && (IsHuman() == pSnappingClient->IsHuman());

		const CSkinContext &SkinContext = SameTeam ? m_SameTeamSkinContext : m_DiffTeamSkinContext;
		m_SkinGetter(SkinContext, &SkinInfo, ClientInfo.m_DDNetVersion, ClientInfo.m_InfClassVersion);
		EventsDirector::SetupSkin(SkinContext, &SkinInfo, ClientInfo.m_DDNetVersion, ClientInfo.m_InfClassVersion);
	}
	else
	{
		SkinInfo.pSkinName = "default";
	}
	return SkinInfo;
}

bool CInfClassPlayer::GetAntiPingEnabled() const
{
	return m_AntiPing;
}

void CInfClassPlayer::SetAntiPingEnabled(bool Enabled)
{
	m_AntiPing = Enabled;
}

void CInfClassPlayer::SetInfectionTimestamp(int Timestamp)
{
	m_GameInfectionTimestamp = Timestamp;
}

int CInfClassPlayer::GetInfectionTimestamp() const
{
	return m_GameInfectionTimestamp;
}

void CInfClassPlayer::SetPreferredClass(EPlayerClass Class)
{
	m_PreferredClass = Class;
}

void CInfClassPlayer::SetPreviouslyPickedClass(EPlayerClass Class)
{
	m_PickedClass = Class;
}

CInfClassCharacter *CInfClassPlayer::GetCharacter()
{
	return CInfClassCharacter::GetInstance(m_pCharacter);
}

const CInfClassCharacter *CInfClassPlayer::GetCharacter() const
{
	return static_cast<const CInfClassCharacter*>(m_pCharacter);
}

void CInfClassPlayer::SetCharacterClass(CInfClassPlayerClass *pClass)
{
	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->SetCharacter(nullptr);
		delete m_pInfcPlayerClass;
	}

	m_pInfcPlayerClass = pClass;
}

void CInfClassPlayer::SetClass(EPlayerClass NewClass)
{
	if(m_class == NewClass)
		return;

	if(m_class != EPlayerClass::Invalid)
	{
		if(IsHumanClass(NewClass) && (NewClass != EPlayerClass::None))
		{
			SetPreviouslyPickedClass(NewClass);
		}
		if(m_PreviousClasses.Size() == m_PreviousClasses.Capacity())
		{
			m_PreviousClasses.RemoveAt(0);
		}

		m_PreviousClasses.Add(m_class);
	}

	m_GhoulLevel = 0;
	m_GhoulLevelTick = 0;

	// Also reset the last move tick to fix Hero flag indicator
	m_LastActionMoveTick = Server()->Tick();

	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->SetCharacter(nullptr);
	}

	m_class = NewClass;

	const bool HadHumanClass = GetCharacterClass() && GetCharacterClass()->IsHuman();
	const bool HadInfectedClass = GetCharacterClass() && GetCharacterClass()->IsZombie();
	const bool SameTeam = IsHuman() ? HadHumanClass : HadInfectedClass;

	if(!SameTeam)
	{
		if(IsInfected())
		{
			SetCharacterClass(new(m_ClientId) CInfClassInfected(this));
			m_InfectionTick = Server()->Tick();
		}
		else
		{
			SetCharacterClass(new(m_ClientId) CInfClassHuman(this));
			m_InfectionTick = -1;
		}
	}

	// Skip the SetCharacter() routine if the World ResetRequested because it
	// means that the Character is going to be destroyed during this
	// IGameServer::Tick() which also invalidates possible auto class selection.
	if(!GameServer()->m_World.m_ResetRequested)
	{
		CInfClassCharacter *pCharacter = GetCharacter();
		m_pInfcPlayerClass->SetCharacter(pCharacter);
		if(pCharacter && !SameTeam)
		{
			// Changed team (was not an infected but is infected now or vice versa)
			pCharacter->ResetHelpers();
			pCharacter->SetPassenger(nullptr);
			CInfClassCharacter *pDriver = pCharacter->GetTaxi();
			if(pDriver)
			{
				pDriver->SetPassenger(nullptr);
			}
		}
	}
	m_pInfcPlayerClass->OnPlayerClassChanged();
	GameController()->OnPlayerClassChanged(this);

	SendClassIntro();
}

void CInfClassPlayer::UpdateSkin()
{
	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->SetupSkinContext(&m_SameTeamSkinContext, true);
		m_pInfcPlayerClass->SetupSkinContext(&m_DiffTeamSkinContext, false);
		m_SkinGetter = m_pInfcPlayerClass->GetSkinGetter();
	}
	else
	{
		m_SkinGetter = nullptr;
	}

	const CWeakSkinInfo SkinInfo = GetSkinInfo(SERVER_DEMO_CLIENT);
	m_TeeInfos = CTeeInfo(SkinInfo.pSkinName, SkinInfo.UseCustomColor, SkinInfo.ColorBody, SkinInfo.ColorFeet);
	m_TeeInfos.ToSixup();
}

void CInfClassPlayer::StartInfection(int InfectiousPlayerCid, INFECTION_TYPE InfectionType)
{
	dbg_assert(InfectionType != INFECTION_TYPE::NO, "Invalid infection");

	if((InfectionType == INFECTION_TYPE::REGULAR) && IsInfected())
		return;

	m_InfectionType = InfectionType;
	m_InfectiousPlayerCid = InfectiousPlayerCid;
	m_InfectionCause = InfectiousPlayerCid >= 0 ? INFECTION_CAUSE::PLAYER : INFECTION_CAUSE::GAME;
}

bool CInfClassPlayer::IsInfectionStarted() const
{
	return m_InfectionType != INFECTION_TYPE::NO;
}

void CInfClassPlayer::OpenMapMenu(int Menu)
{
	m_MapMenu = Menu;
	m_MapMenuTick = 0;
}

void CInfClassPlayer::CloseMapMenu()
{
	m_MapMenu = 0;
	m_MapMenuTick = -1;
}

bool CInfClassPlayer::MapMenuClickable()
{
	return (m_MapMenu > 0 && (m_MapMenuTick > Server()->TickSpeed()/2));
}

void CInfClassPlayer::SetHookProtection(bool Value, bool Automatic)
{
	if(m_HookProtection != Value)
	{
		m_HookProtection = Value;

		if(!m_HookProtectionAutomatic || !Automatic)
		{
			if(m_HookProtection)
				GameServer()->SendChatTarget_Localization(GetCid(), CHATCATEGORY_DEFAULT, _("Hook protection enabled"), NULL);
			else
				GameServer()->SendChatTarget_Localization(GetCid(), CHATCATEGORY_DEFAULT, _("Hook protection disabled"), NULL);
		}
	}

	m_HookProtectionAutomatic = Automatic;
}

EPlayerScoreMode CInfClassPlayer::GetScoreMode() const
{
	return m_ScoreMode;
}

void CInfClassPlayer::SetScoreMode(EPlayerScoreMode Mode)
{
	m_ScoreMode = Mode;
}

void CInfClassPlayer::ResetTheTargetToFollow()
{
	SetFollowTarget(-1, 0);
}

void CInfClassPlayer::SetFollowTarget(int ClientId, float Duration)
{
	m_FollowTargetId = ClientId;
	if(m_FollowTargetId < 0)
	{
		m_FollowTargetTicks = 0;
	}
	else
	{
		m_FollowTargetTicks = Duration * Server()->TickSpeed();
	}
}

int CInfClassPlayer::TargetToFollow() const
{
	return m_FollowTargetTicks > 0 ? m_FollowTargetId : -1;
}

int CInfClassPlayer::GetSpectatingCid() const
{
	int TargetCid = TargetToFollow();
	if (TargetCid < 0)
	{
		TargetCid = m_SpectatorId;
	}

	return TargetCid;
}

float CInfClassPlayer::GetGhoulPercent() const
{
	return clamp(m_GhoulLevel/static_cast<float>(GameController()->GetGhoulStomackSize()), 0.0f, 1.0f);
}

void CInfClassPlayer::IncreaseGhoulLevel(int Diff)
{
	int NewGhoulLevel = m_GhoulLevel + Diff;
	m_GhoulLevel = clamp(NewGhoulLevel, 0, GameController()->GetGhoulStomackSize());
}

void CInfClassPlayer::SetRandomClassChoosen()
{
	m_RandomClassRoundId = GameController()->GetRoundId();
}

bool CInfClassPlayer::RandomClassChoosen() const
{
	return m_RandomClassRoundId == GameController()->GetRoundId();
}

EPlayerClass CInfClassPlayer::GetPreviousInfectedClass() const
{
	for (int i = m_PreviousClasses.Size() - 1; i > 0; --i)
	{
		EPlayerClass Class = m_PreviousClasses.At(i);
		if(IsInfectedClass(Class))
		{
			return Class;
		}
	}

	return EPlayerClass::Invalid;
}

EPlayerClass CInfClassPlayer::GetPreviousHumanClass() const
{
	for(int i = m_PreviousClasses.Size() - 1; i > 0; --i)
	{
		EPlayerClass Class = m_PreviousClasses.At(i);
		if(IsHumanClass(Class))
		{
			return Class;
		}
	}

	return EPlayerClass::Invalid;
}

EPlayerClass CInfClassPlayer::GetPreviouslyPickedClass() const
{
	return m_PickedClass;
}

void CInfClassPlayer::AddSavedPosition(const vec2 Position)
{
	m_SavedPositions.Resize(1);
	m_SavedPositions[0] = Position;
}

bool CInfClassPlayer::LoadSavedPosition(vec2 *pOutput) const
{
	if(m_SavedPositions.IsEmpty())
		return false;

	*pOutput = m_SavedPositions.At(0);
	return true;
}

void CInfClassPlayer::ResetRoundData()
{
	SetClass(EPlayerClass::None);
	m_PreviousClasses.Clear();

	m_HumanTime = 0;

	m_Kills = 0;
	m_Deaths = 0;
	m_Assists = 0;
	m_Score = 0;
}

void CInfClassPlayer::OnKill()
{
	++m_Kills;
}

void CInfClassPlayer::OnDeath()
{
	++m_Deaths;
}

void CInfClassPlayer::OnAssist()
{
	++m_Assists;
}

void CInfClassPlayer::SetMaxHP(int MaxHP)
{
	m_MaxHP = MaxHP;
	if(m_pInfcPlayerClass)
	{
		m_pInfcPlayerClass->UpdateSkin();
	}
}

void CInfClassPlayer::ApplyMaxHP()
{
	if(!GetCharacter())
		return;

	if(!m_MaxHP)
		return;

	int HP = clamp<int>(m_MaxHP, 0, 10);
	int Armor = m_MaxHP - HP;

	GetCharacter()->SetMaxArmor(Armor);
	GetCharacter()->SetHealthArmor(HP, Armor);
}

void CInfClassPlayer::OnCharacterSpawned(const SpawnContext &Context)
{
	CInfClassCharacter *pCharacter = GetCharacter();

	m_pInfcPlayerClass->SetCharacter(pCharacter);
	pCharacter->OnCharacterSpawned(Context);

	ResetTheTargetToFollow();
	ApplyMaxHP();
}

const char *CInfClassPlayer::GetClan(int SnappingClient) const
{
	if(GetTeam() == TEAM_SPECTATORS)
	{
		return Server()->ClientClan(m_ClientId);
	}

	EPlayerScoreMode SnapScoreMode = GameController()->GetPlayerScoreMode(SnappingClient);
	static char aBuf[32];

	if(SnapScoreMode == EPlayerScoreMode::Class)
	{
		const char *ClassName = CInfClassGameController::GetClanForClass(GetClass(), "?????");
		str_format(aBuf, sizeof(aBuf), "%s%s", Server()->IsClientLogged(GetCid()) ? "@" : " ", ClassName);
	}
	else if(SnapScoreMode == EPlayerScoreMode::Time)
	{
		float RoundDuration = static_cast<float>(m_HumanTime / ((float)Server()->TickSpeed())) / 60.0f;
		int Minutes = static_cast<int>(RoundDuration);
		int Seconds = static_cast<int>((RoundDuration - Minutes) * 60.0f);

		str_format(aBuf, sizeof(aBuf), "%i:%s%i min", Minutes, ((Seconds < 10) ? "0" : ""), Seconds);
	}
	else if(SnapScoreMode == EPlayerScoreMode::Clan)
	{
		return Server()->ClientClan(m_ClientId);
	}

	// This is not thread-safe but we don't have threads.
	return aBuf;
}

void CInfClassPlayer::HandleAutoRespawn()
{
	float AutoSpawnInterval = 3;

	if(GameController()->GetRoundType() == ERoundType::Survival && IsInfected())
	{
		AutoSpawnInterval = 0;
	}

	if(!m_pCharacter && m_DieTick+Server()->TickSpeed() * AutoSpawnInterval <= Server()->Tick())
	{
		Respawn();
	}
}

void CInfClassPlayer::UpdateSpectatorPos()
{
	if(m_Team != TEAM_SPECTATORS || m_SpectatorId == SPEC_FREEVIEW)
		return;

	const CInfClassPlayer *pTarget = GameController()->GetPlayer(m_SpectatorId);
	if(!pTarget)
		return;

	if(g_Config.m_SvStrictSpectateMode)
	{
		const CInfClassCharacter *pCharacter = pTarget->GetCharacter();
		if(pCharacter && pCharacter->IsInvisible())
			return;
	}

	m_ViewPos = GameServer()->m_apPlayers[m_SpectatorId]->m_ViewPos;
}

bool CInfClassPlayer::IsForcedToSpectate() const
{
	if(!g_Config.m_InfEnableFollowingCamera)
	{
		return false;
	}

	if (IsSpectator() || (m_pCharacter && m_pCharacter->IsAlive()))
		return false;

	int Target = TargetToFollow();
	if (Target >= 0)
	{
		const CInfClassPlayer *pFollowedPlayer = GameController()->GetPlayer(Target);
		if(pFollowedPlayer && pFollowedPlayer->GetCharacter() && (!pFollowedPlayer->IsHuman() || (pFollowedPlayer->IsHuman() == IsHuman())))
		{
			return true;
		}
	}

	return false;
}

void CInfClassPlayer::SendClassIntro()
{
	const EPlayerClass Class = GetClass();
	if(!IsBot() && (Class != EPlayerClass::None) && (Class != EPlayerClass::Invalid))
	{
		const char *pClassName = CInfClassGameController::GetClassDisplayName(Class);
		const char *pTranslated = Server()->Localization()->Localize(GetLanguage(), pClassName);

		if(IsHuman())
			GameServer()->SendBroadcast_Localization(GetCid(), BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE,
				_("You are a human: {str:ClassName}"), "ClassName", pTranslated, NULL);
		else
			GameServer()->SendBroadcast_Localization(GetCid(), BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE,
				_("You are an infected: {str:ClassName}"), "ClassName", pTranslated, NULL);

		int Index = static_cast<int>(Class);
		if(!m_aKnownClasses[Index])
		{
			const char *className = CInfClassGameController::GetClassName(Class);
			GameServer()->SendChatTarget_Localization(GetCid(), CHATCATEGORY_DEFAULT, _("Type “/help {str:ClassName}” for more information about your class"), "ClassName", className, NULL);
			m_aKnownClasses[Index] = true;
		}
	}
}
