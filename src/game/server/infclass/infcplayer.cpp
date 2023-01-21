#include "infcplayer.h"

#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/infclass/infcgamecontroller.h>
#include <game/server/infclass/events-director.h>

#include "classes/humans/human.h"
#include "classes/infcplayerclass.h"
#include "classes/infected/infected.h"
#include "entities/infccharacter.h"

MACRO_ALLOC_POOL_ID_IMPL(CInfClassPlayer, MAX_CLIENTS)

CInfClassPlayer::CInfClassPlayer(CInfClassGameController *pGameController, int ClientID, int Team)
	: CPlayer(pGameController->GameServer(), ClientID, Team)
	, m_pGameController(pGameController)
{
	m_class = PLAYERCLASS_INVALID;
	m_PreferredClass = PLAYERCLASS_INVALID;

	SetClass(PLAYERCLASS_NONE);
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
	CInfClassCharacter *pCharacter = new(m_ClientID) CInfClassCharacter(GameController());

	m_pCharacter = pCharacter;
	m_pCharacter->Spawn(this, Context.SpawnPos);
	OnCharacterSpawned(Context);
}

int CInfClassPlayer::GetScore(int SnappingClient) const
{
	int SnapScoreMode = PLAYERSCOREMODE_SCORE;
	if(GameServer()->GetPlayer(SnappingClient))
	{
		SnapScoreMode = GameServer()->m_apPlayers[SnappingClient]->GetScoreMode();
	}

	if(GetTeam() == TEAM_SPECTATORS)
	{
	}
	else
	{
		if(SnapScoreMode == PLAYERSCOREMODE_TIME)
		{
			return m_HumanTime / Server()->TickSpeed();
		}
		else
		{
			return Server()->RoundStatistics()->PlayerScore(m_ClientID);
		}
	}

	return CPlayer::GetScore(SnappingClient);
}

void CInfClassPlayer::Tick()
{
	if(!Server()->ClientIngame(m_ClientID))
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

	if(GetClass() == PLAYERCLASS_GHOUL)
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

			if(IsForcedToSpectate() && (Server()->GetClientInfclassVersion(GetCID()) >= VERSION_INFC_FORCED_SPEC))
			{
				const CCharacter *pFollowedCharacter = GameController()->GetCharacter(TargetToFollow());
				if(pFollowedCharacter)
				{
					m_ViewPos = pFollowedCharacter->GetPos();
				}
				else
				{
					m_FollowTargetId = -1;
				}
			}
		}
	}
	else
	{
		if(m_FollowTargetTicks > 0)
		{
			++m_FollowTargetTicks;
		}
	}
}

void CInfClassPlayer::Snap(int SnappingClient)
{
	if(!Server()->ClientIngame(m_ClientID))
		return;

	CPlayer::Snap(SnappingClient);

	int InfClassVersion = Server()->GetClientInfclassVersion(SnappingClient);
	const bool IsForcedToSpec = IsForcedToSpectate();

	if(InfClassVersion)
	{
		CNetObj_InfClassPlayer *pInfClassPlayer = static_cast<CNetObj_InfClassPlayer *>(Server()->SnapNewItem(NETOBJTYPE_INFCLASSPLAYER, m_ClientID, sizeof(CNetObj_InfClassPlayer)));
		if(!pInfClassPlayer)
			return;

		pInfClassPlayer->m_Class = m_class;
		pInfClassPlayer->m_Flags = 0;
		if(IsZombie())
		{
			pInfClassPlayer->m_Flags |= INFCLASS_PLAYER_FLAG_INFECTED;
		}
		if(!HookProtectionEnabled())
		{
			pInfClassPlayer->m_Flags |= INFCLASS_PLAYER_FLAG_HOOK_PROTECTION_OFF;
		}
		if(IsForcedToSpec)
		{
			pInfClassPlayer->m_Flags |= INFCLASS_PLAYER_FLAG_FORCED_TO_SPECTATE;
		}

		pInfClassPlayer->m_Kills = m_Kills;
		pInfClassPlayer->m_Deaths = m_Deaths;
		pInfClassPlayer->m_Assists = m_Assists;
		pInfClassPlayer->m_Score = m_Score;

		GetCharacterClass()->OnPlayerSnap(SnappingClient, InfClassVersion);
	}

	if(IsForcedToSpec && (SnappingClient == m_ClientID) && (InfClassVersion >= VERSION_INFC_FORCED_SPEC))
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, m_ClientID, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpectatorID = TargetToFollow();
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}
}

void CInfClassPlayer::SnapClientInfo(int SnappingClient, int SnappingClientMappedId)
{
	CNetObj_ClientInfo *pClientInfo = static_cast<CNetObj_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_CLIENTINFO, SnappingClientMappedId, sizeof(CNetObj_ClientInfo)));

	if(!pClientInfo)
		return;

	StrToInts(&pClientInfo->m_Name0, 4, Server()->ClientName(m_ClientID));
	StrToInts(&pClientInfo->m_Clan0, 3, GetClan(SnappingClient));
	pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);

	IServer::CClientInfo ClientInfo = {0};
	if(SnappingClient != SERVER_DEMO_CLIENT)
	{
		Server()->GetClientInfo(SnappingClient, &ClientInfo);
	}

	CWeakSkinInfo SkinInfo;
	if(m_SkinGetter)
	{
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
	if(IsZombie() && (m_InfectionType == INFECTION_TYPE::REGULAR))
	{
		// Do not infect if inf class already set
		m_InfectionType = INFECTION_TYPE::NO;
		return;
	}

	if(IsHuman())
	{
		m_InfectionTick = Server()->Tick();
	}

	const int PreviousClass = GetClass();
	CInfClassPlayer *pInfectiousPlayer = GameController()->GetPlayer(m_InfectiousPlayerCID);

	m_InfectionType = INFECTION_TYPE::NO;
	m_InfectiousPlayerCID = -1;

	GameController()->DoPlayerInfection(this, pInfectiousPlayer, PreviousClass);
}

void CInfClassPlayer::KillCharacter(int Weapon)
{
	if(!m_pCharacter)
		return;

	// Character actually died / removed from the world
	const icArray<int, 2> EphemeralClasses = {
		PLAYERCLASS_UNDEAD,
		PLAYERCLASS_WITCH,
	};

	if((Weapon == WEAPON_SELF) && (IsHuman() || EphemeralClasses.Contains(GetClass())))
	{
		static const float SelfKillConfirmationTime = 3;
		if(Server()->Tick() > m_SelfKillAttemptTick + Server()->TickSpeed() * SelfKillConfirmationTime)
		{
			GameServer()->SendChatTarget_Localization(GetCID(), CHATCATEGORY_PLAYER,
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

void CInfClassPlayer::SetPreferredClass(PLAYERCLASS Class)
{
	m_PreferredClass = Class;
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

void CInfClassPlayer::SetClass(PLAYERCLASS NewClass)
{
	if(m_class == NewClass)
		return;

	if(m_class != PLAYERCLASS_INVALID)
	{
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
		if(IsZombie())
		{
			SetCharacterClass(new(m_ClientID) CInfClassInfected(this));
		}
		else
		{
			SetCharacterClass(new(m_ClientID) CInfClassHuman(this));
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
			CInfClassCharacter *pDriver = pCharacter->GetTaxiDriver();
			if(pDriver)
			{
				pDriver->SetPassenger(nullptr);
			}
		}
	}
	m_pInfcPlayerClass->OnPlayerClassChanged();
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
}

void CInfClassPlayer::StartInfection(CPlayer *pInfectiousPlayer, INFECTION_TYPE InfectionType)
{
	dbg_assert(InfectionType != INFECTION_TYPE::NO, "Invalid infection");

	if((InfectionType == INFECTION_TYPE::REGULAR) && IsZombie())
		return;

	m_InfectionType = InfectionType;
	m_InfectiousPlayerCID = pInfectiousPlayer ? pInfectiousPlayer->GetCID() : -1;
	m_InfectionCause = pInfectiousPlayer ? INFECTION_CAUSE::PLAYER : INFECTION_CAUSE::GAME;
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

void CInfClassPlayer::ResetTheTargetToFollow()
{
	SetFollowTarget(-1, 0);
}

void CInfClassPlayer::SetFollowTarget(int ClientID, float Duration)
{
	m_FollowTargetId = ClientID;
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

float CInfClassPlayer::GetGhoulPercent() const
{
	return clamp(m_GhoulLevel/static_cast<float>(g_Config.m_InfGhoulStomachSize), 0.0f, 1.0f);
}

void CInfClassPlayer::IncreaseGhoulLevel(int Diff)
{
	int NewGhoulLevel = m_GhoulLevel + Diff;
	m_GhoulLevel = clamp(NewGhoulLevel, 0, g_Config.m_InfGhoulStomachSize);
}

void CInfClassPlayer::SetRandomClassChoosen()
{
	m_RandomClassRoundId = GameController()->GetRoundId();
}

bool CInfClassPlayer::RandomClassChoosen() const
{
	return m_RandomClassRoundId == GameController()->GetRoundId();
}

PLAYERCLASS CInfClassPlayer::GetPreviousInfectedClass() const
{
	for (int i = m_PreviousClasses.Size() - 1; i > 0; --i)
	{
		PLAYERCLASS Class = m_PreviousClasses.At(i);
		if(IsInfectedClass(Class))
		{
			return Class;
		}
	}

	return PLAYERCLASS_INVALID;
}

PLAYERCLASS CInfClassPlayer::GetPreviousHumanClass() const
{
	for (int i = m_PreviousClasses.Size() - 1; i > 0; --i)
	{
		PLAYERCLASS Class = m_PreviousClasses.At(i);
		if(IsHumanClass(Class))
		{
			return Class;
		}
	}

	return PLAYERCLASS_INVALID;
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
	SetClass(PLAYERCLASS_NONE);
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

void CInfClassPlayer::OnCharacterSpawned(const SpawnContext &Context)
{
	CInfClassCharacter *pCharacter = GetCharacter();

	m_pInfcPlayerClass->SetCharacter(pCharacter);
	pCharacter->OnCharacterSpawned(Context);
}

const char *CInfClassPlayer::GetClan(int SnappingClient) const
{
	if(GetTeam() == TEAM_SPECTATORS)
	{
		return Server()->ClientClan(m_ClientID);
	}

	int SnapScoreMode = PLAYERSCOREMODE_SCORE;
	if(GameServer()->GetPlayer(SnappingClient))
	{
		SnapScoreMode = GameServer()->m_apPlayers[SnappingClient]->GetScoreMode();
	}
	
	static char aBuf[32];

	if(SnapScoreMode == PLAYERSCOREMODE_TIME)
	{
		float RoundDuration = static_cast<float>(m_HumanTime/((float)Server()->TickSpeed()))/60.0f;
		int Minutes = static_cast<int>(RoundDuration);
		int Seconds = static_cast<int>((RoundDuration - Minutes)*60.0f);
		
		str_format(aBuf, sizeof(aBuf), "%i:%s%i min", Minutes,((Seconds < 10) ? "0" : ""), Seconds);
	}
	else
	{
		const char *ClassName = CInfClassGameController::GetClanForClass(GetClass(), "?????");
		str_format(aBuf, sizeof(aBuf), "%s%s", Server()->IsClientLogged(GetCID()) ? "@" : " ", ClassName);
	}

	// This is not thread-safe but we don't have threads.
	return aBuf;
}

void CInfClassPlayer::HandleAutoRespawn()
{
	int InfClassVersion = Server()->GetClientInfclassVersion(m_ClientID);

	float AutoSpawnInterval = 3;
	if((InfClassVersion >= VERSION_INFC_FORCED_SPEC) && IsForcedToSpectate())
		AutoSpawnInterval = 5;

	if(!m_pCharacter && m_DieTick+Server()->TickSpeed() * AutoSpawnInterval <= Server()->Tick())
	{
		Respawn();
	}
}

bool CInfClassPlayer::IsForcedToSpectate() const
{
	return !IsSpectator() && (!m_pCharacter || !m_pCharacter->IsAlive()) && TargetToFollow() >= 0;
}
