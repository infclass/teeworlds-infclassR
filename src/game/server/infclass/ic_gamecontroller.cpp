/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include "ic_gamecontroller.h"

#include <game/infclass/ic_classes.h>
#include <game/infclass/damage_type.h>
#include <game/server/infclass/classes/humans/human.h>
#include <game/server/infclass/classes/ic_playerclass.h>
#include <game/server/infclass/classes/infected/infected.h>
#include <game/server/infclass/death_context.h>
#include <game/server/infclass/entities/flyingpoint.h>
#include <game/server/infclass/entities/ic_character.h>
#include <game/server/infclass/entities/ic_door.h>
#include <game/server/infclass/entities/ic_pickup.h>
#include <game/server/infclass/events-director.h>
#include <game/server/infclass/ic_player.h>

#include <game/generated/protocol.h>
#include <game/mapitems.h>
#include <game/server/gamecontext.h>
#include <game/version.h>

#include <engine/message.h>
#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/config.h>
#include <engine/shared/config.h>
#include <engine/shared/network.h>
#include <engine/shared/protocolglue.h>

#include <base/tl/ic_array.h>
#include <base/tl/ic_enum.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <time.h>

const int InfClassModeSpecialSkip = 0x100;

static const char *gs_aRoundEndReasonNames[] = {
	"finished",
	"canceled",
	"invalid",
};

const char *toString(ERoundEndReason Reason)
{
	return toStringImpl(Reason, gs_aRoundEndReasonNames);
}

static const char *gs_aRoundNames[] = {
	"classic",
	"fun",
	"fast",
	"survival",
	"invalid",
};

const char *toString(ERoundType RoundType)
{
	return toStringImpl(RoundType, gs_aRoundNames);
}

template ERoundType fromString<ERoundType>(const char *pString);

static const char *gs_aCharacterSpawnTypes[] = {
	"map",
	"witch",
	"invalid",
};

const char *toString(SpawnContext::SPAWN_TYPE SpawnType)
{
	return toStringImpl(SpawnType, gs_aCharacterSpawnTypes);
}

class CHintMessage
{
public:
	CHintMessage(const char *pText) :
		m_pText(pText)
	{
	}

	CHintMessage(const char *pText, const char *pArg1Name, void *pArg1Value) :
		m_pText(pText),
		m_pArg1Name(pArg1Name),
		m_pArg1Value(pArg1Value)
	{
	}

	const char *m_pText{};
	const char *m_pArg1Name{};
	void *m_pArg1Value{};
};

static const char gs_aInvalidWeaponIdMsg[] = "Invalid weapon id."
											 " Use 'inf_list_weapons' to get the list of the available weapons.";
static const CHintMessage gs_aHintMessages[] = {
	_("Taxi prevents ammo regeneration for all passengers."),
	_("Choosing a random class grants full armor."),
	_("You can toggle hook protection by pressing f3 (\"Vote yes\" keybind)."),
	_("Mercenary can reduce ammo usage during flight by tapping instead of holding down the \"fire\" button."),
	_("Mercenary's grenades prevent zombies from healing."),
	_("Medic can heal Heroes using the grenade launcher."),
	_("Medic and Biologist can use hammer to instantly kill an infected."),
	_("Hero can stand still for a short time to be pointed towards the flag."),
	_("Hero's turrets are great for detecting Ghosts."),
	_("Hero's flags fully restore ammo to all humans"),
	_("Soldier's bombs can hit through walls."),
	_("Ninja's slash deals 9 damage by default. One more pistol shot will kill an infected with no armor."),
	_("Ninja with a single strength upgrade can kill an armorless infected with a single slash."),
	_("Ninja doesn't need to directly kill their target. An assist with a laser blind or grenade stun will still grant the rewards."),
	_("Ninja heals slightly on a target kill."),
	_("Sniper deals double as much damage in locked position."),
	_("Scientist can use Taxi to teleport his teammates into safety."),
	{
		_P("Scientist can get a white hole after {int:Kills} kill.",
			"Scientist can get a white hole after {int:Kills} kills.", g_Config.m_InfWhiteHoleMinimalKills),
		"Kills",
		&g_Config.m_InfWhiteHoleMinimalKills,
	},
	_("Scientist can rocket jump with the laser rifle."),
	_("Biologist's hammer can stop poison effect on teammates. Look for heart icons."),
	_("Biologist's bouncy shotgun can be used to hit the infected around corners."),
	_("Smoker heals by hooking humans."),
	_("Boomer can infect through narrow walls."),
	_("Hunter receives no knockback from Medic's shotgun."),
	_("Bat can heal by hitting humans."),
	_("Spider doesn't need to be in Web mode to automatically grab any humans touching its hook."),
	_("Spider can be hooked and transported by teammates to extend its hook trap."),
	{
		_("Slug can heal itself and allies over time up to {int:MaxHP} HP with its slime."),
		"MaxHP",
		&g_Config.m_InfSlimeMaxHeal,
	},
	_("Slug can hold down the \"fire\" button to automatically spread slime. The hammer swings won't hurt humans this way though."),
	_("Voodoo can unfreeze an Undead while in Spirit mode."),
	_("Witch can spawn the infected through narrow walls."),
	_("Undead can be removed from a game by throwing it into kill tiles or reviving it as a Medic.")};

enum class ROUND_CANCELATION_REASON
{
	INVALID,
	ALL_INFECTED_LEFT_THE_GAME,
	EVERYONE_INFECTED_BY_THE_GAME,
};

struct InfclassPlayerPersistantData : public CGameContext::CPersistentClientData
{
	EPlayerScoreMode m_ScoreMode = EPlayerScoreMode::Class;
	bool m_AntiPing = false;
	EPlayerClass m_PreferredClass = EPlayerClass::Invalid;
	EPlayerClass m_PreviouslyPickedClass = EPlayerClass::Invalid;
	int m_LastInfectionTime = 0;
};

struct SurvivalWaveConfiguration
{
	SurvivalWaveConfiguration() = default;

	char aName[64] = {0};
};

icArray<SurvivalWaveConfiguration, MaxWaves> m_SurvivalWaves;

int64_t CIcGameController::m_LastTipTime = 0;

IGameController *CreateInfclassModController(CGameContext *pGameServer) { return new CIcGameController(pGameServer); }

CIcGameController::CIcGameController(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
	m_Teams.m_Core.m_IsInfclass = true;

	for(std::vector<vec2> &vTeamSpawnPoints : m_avSpawnPoints)
	{
		vTeamSpawnPoints.reserve(32);
	}

	m_GrowingMap = nullptr;

	//Get zones
	m_ZoneHandle_icDamage = GameServer()->Collision()->GetZoneHandle("icDamage");
	m_ZoneHandle_icTeleport = GameServer()->Collision()->GetZoneHandle("icTele");
	m_ZoneHandle_icBonus = GameServer()->Collision()->GetZoneHandle("icBonus");

	m_MapWidth = GameServer()->Collision()->GetWidth();
	m_MapHeight = GameServer()->Collision()->GetHeight();
	m_GrowingMap = new int[m_MapWidth*m_MapHeight];

	m_RoundType = GetDefaultRoundType();
	m_QueuedRoundType = ERoundType::Invalid;
	m_InfectedStarted = false;
	m_VanillaMapLoaded = !str_startswith(Config()->m_SvMap, "infc_");

	for(int j=0; j<m_MapHeight; j++)
	{
		for(int i=0; i<m_MapWidth; i++)
		{
			vec2 TilePos = vec2(16.0f, 16.0f) + vec2(i*32.0f, j*32.0f);
			if(GameServer()->Collision()->CheckPoint(TilePos))
			{
				m_GrowingMap[j*m_MapWidth+i] = 4;
			}
			else
			{
				m_GrowingMap[j*m_MapWidth+i] = 1;
			}
		}
	}

	SetHealthArmorHudEnabled(true);
	SetAmmoHudEnabled(true);

	RegisterEntityTypes();
	InitWeapons();
	ReservePlayerOwnSnapItems();

	ResetPlayerClassesEnablement();
}

CIcGameController::~CIcGameController()
{
	FreePlayerOwnSnapItems();

	if(m_GrowingMap) delete[] m_GrowingMap;
}

const char *CIcGameController::GameType() const
{
	return "InfClassR";
}

void CIcGameController::IncreaseCurrentRoundCounter()
{
	IGameController::IncreaseCurrentRoundCounter();

	m_MoreRoundsSuggested = false;

	MaybeSuggestMoreRounds();
}

void CIcGameController::DoTeamBalance()
{
	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	const int NumPlayers = NumHumans + NumInfected;
	const int NumFirstPickedPlayers = GetMinimumInfectedForPlayers(NumPlayers);
	const int PlayersToBalance = maximum<int>(0, NumFirstPickedPlayers - NumInfected);

	if(PlayersToBalance == 0)
	{
		m_InfUnbalancedTick = -1;
	}
	else if (m_InfUnbalancedTick < 0)
	{
		m_InfUnbalancedTick = Server()->Tick();
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION,
			_("The game is not balanced. Infection is coming."), nullptr);
	}
	else
	{
		int BalancingTick = m_InfUnbalancedTick + Server()->TickSpeed() * Config()->m_InfTeamBalanceSeconds;
		if(Server()->Tick() > BalancingTick)
		{
			ForcePlayersBalance(PlayersToBalance);
		}
		else
		{
			BroadcastInfectionComing(BalancingTick);
		}
	}
}

void CIcGameController::OnPlayerConnect(CPlayer *pPlayer)
{
	IGameController::OnPlayerConnect(pPlayer);
	int ClientId = pPlayer->GetCid();

	pPlayer->SetOriginalName(Server()->ClientName(ClientId));

	Server()->RoundStatistics()->ResetPlayer(ClientId);

	SendServerParams(pPlayer->GetCid());

	if(!Server()->ClientPrevIngame(ClientId))
	{
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} entered and joined the game"), "PlayerName", Server()->ClientName(ClientId), nullptr);

		GameServer()->SendChatTarget(ClientId, "InfectionClass Mod. Version: " GAME_VERSION);
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT,
			_("See also: /help, /changelog, /about"), nullptr);

		if(Config()->m_AboutContactsDiscord[0])
		{
			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT,
				_("Join our Discord server: {str:Url}"), "Url",
				Config()->m_AboutContactsDiscord, nullptr);
		}
		if(Config()->m_AboutContactsTelegram[0])
		{
			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT,
				_("Join our Telegram: {str:Url}"), "Url",
				Config()->m_AboutContactsTelegram, nullptr);
		}
		if(Config()->m_AboutContactsMatrix[0])
		{
			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT,
				_("Join our Matrix room: {str:Url}"), "Url",
				Config()->m_AboutContactsMatrix, nullptr);
		}
	}
}

void CIcGameController::OnPlayerDisconnect(CPlayer *pBasePlayer, EClientDropType Type, const char *pReason)
{
	Server()->RoundStatistics()->ResetPlayer(pBasePlayer->GetCid());

	for(CPlayer *pPlayer : GameServer()->m_apPlayers)
	{
		if(pPlayer && (pPlayer != pBasePlayer))
		{
			CIcCharacter *pCharacter = CIcCharacter::GetInstance(pPlayer->GetCharacter());
			if(pCharacter)
			{
				pCharacter->RemoveReferencesToCid(pBasePlayer->GetCid());
			}
		}
	}

	CIcPlayer *pPlayer = CIcPlayer::GetInstance(pBasePlayer);
	PlayerScore *pScore = GetSurvivalPlayerScore(pPlayer->GetCid());
	if(pScore)
	{
		str_copy(pScore->aPlayerName, Server()->ClientName(pPlayer->GetCid()));
		pScore->ClientId = -1;
		pScore->Kills = pPlayer->GetKills();
	}
	m_SurvivalState.SurvivedPlayers.RemoveOne(pPlayer->GetCid());
	m_SurvivalState.KilledPlayers.RemoveOne(pPlayer->GetCid());

	static const auto aIgnoreReasons = []()
	{
		EClientDropType aIgnoreReasons[]{
			EClientDropType::Ban,
			EClientDropType::Kick,
			EClientDropType::Redirected,
			EClientDropType::Shutdown,
			EClientDropType::TimeoutProtectionUsed,
		};

		return icArray(aIgnoreReasons);
	}();

	if(!aIgnoreReasons.Contains(Type))
	{
		if(pPlayer && pPlayer->IsInGame() && pPlayer->IsInfected() && m_InfectedStarted)
		{
			int NumHumans;
			int NumInfected;
			GetPlayerCounter(pPlayer->GetCid(), NumHumans, NumInfected);
			const int NumPlayers = NumHumans + NumInfected;
			const int NumFirstInfected = GetMinimumInfectedForPlayers(NumPlayers);

			if(NumInfected < NumFirstInfected)
			{
				Server()->Ban(pPlayer->GetCid(), 60 * Config()->m_InfLeaverBanTime, "Leaver");
			}
		}
	}

	IGameController::OnPlayerDisconnect(pBasePlayer, Type, pReason);
}

void CIcGameController::OnReset()
{
	// IGameController::OnReset();

	for(auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(pPlayer)
		{
			pPlayer->Respawn();
			pPlayer->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;
		}
	}
}

void CIcGameController::DoPlayerInfection(CIcPlayer *pPlayer, CIcPlayer *pInfectiousPlayer, EPlayerClass PreviousClass)
{
	if(GetRoundType() == ERoundType::Survival)
	{
		DoTeamChange(pPlayer, TEAM_SPECTATORS, false);
		return;
	}

	EPlayerClass c = ChooseInfectedClass(pPlayer);
	pPlayer->SetClass(c);

	FallInLoveIfInfectedEarly(pPlayer->GetCharacter());

	if(!pInfectiousPlayer)
	{
		if(pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsAlive())
		{
			// Still send a kill message to notify other players about the infection
			GameServer()->SendKillMessage(pPlayer->GetCid(), pPlayer->GetCid(), WEAPON_WORLD, 0);
			GameServer()->CreateSound(pPlayer->GetCharacter()->m_Pos, SOUND_PLAYER_DIE);
		}

		return;
	}

	const int InfectedByCid = pInfectiousPlayer->GetCid();
	if(!IsInfectedClass(PreviousClass) && (pPlayer != pInfectiousPlayer))
	{
		if(pInfectiousPlayer->IsHuman())
		{
			GameServer()->SendChatTarget_Localization(InfectedByCid, CHATCATEGORY_SCORE,
				_("You have infected {str:VictimName}, shame on you!"),
				"VictimName", Server()->ClientName(pPlayer->GetCid()),
				nullptr);
			GameServer()->SendEmoticon(pInfectiousPlayer->GetCid(), EMOTICON_SORRY);
			CIcCharacter *pGuiltyCharacter = pInfectiousPlayer->GetCharacter();
			if(pGuiltyCharacter)
			{
				const float GuiltyPlayerFreeze = 3;
				pGuiltyCharacter->Freeze(GuiltyPlayerFreeze, -1, FREEZEREASON_INFECTION);
				pGuiltyCharacter->SetEmote(EMOTE_PAIN, Server()->Tick() + Server()->TickSpeed() * GuiltyPlayerFreeze);
			}
		}
		else
		{
			GameServer()->SendChatTarget_Localization(pPlayer->GetCid(), CHATCATEGORY_INFECTED,
				_("You have been infected by {str:KillerName}"),
				"KillerName", Server()->ClientName(pInfectiousPlayer->GetCid()),
				nullptr);
			GameServer()->SendChatTarget_Localization(InfectedByCid, CHATCATEGORY_SCORE,
				_("You have infected {str:VictimName}, +3 points"),
				"VictimName", Server()->ClientName(pPlayer->GetCid()),
				nullptr);
			Server()->RoundStatistics()->OnScoreEvent(InfectedByCid, EScoreEvent::INFECTION,
				pInfectiousPlayer->GetClass(), Server()->ClientName(InfectedByCid), Console());
			GameServer()->SendScoreSound(InfectedByCid);
		}
	}

	//Search for hook
	for(TEntityPtr<CIcCharacter> pHook = GameWorld()->FindFirst<CIcCharacter>(); pHook; ++pHook)
	{
		if(
			pHook->GetPlayer() &&
			pHook->GetHookedPlayer() == pPlayer->GetCid() &&
			pHook->GetCid() != InfectedByCid
		)
		{
			Server()->RoundStatistics()->OnScoreEvent(pHook->GetCid(), EScoreEvent::HELP_HOOK_INFECTION, pHook->GetPlayerClass(), Server()->ClientName(pHook->GetCid()), Console());
			GameServer()->SendScoreSound(pHook->GetCid());
		}
	}
}

void CIcGameController::OnHeroFlagCollected(int ClientId)
{
	GameServer()->SendBroadcast_Localization(-1, EBroadcastPriority::GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The Hero found the flag!"), NULL);
	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);

	int Tick = Server()->Tick();
	if(Tick < m_HeroGiftTick)
		return;

	m_HeroGiftTick = Tick + GetHeroFlagCooldown() * Server()->TickSpeed();
}

float CIcGameController::GetHeroFlagCooldown() const
{
	if(GetRoundType() == ERoundType::Survival)
	{
		return 30;
	}

	// Set cooldown for next flag depending on how many players are online
	int PlayerCount = Server()->GetActivePlayerCount();
	if(PlayerCount <= 1)
	{
		// only 1 player on, let him find as many flags as he wants
		return 2.0 / Server()->TickSpeed();
	}

	float t = (8 - PlayerCount) / 8.0f;
	if(t < 0.0f)
		t = 0.0f;

	return 15 + (120 * t);
}

bool CIcGameController::OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv)
{
	bool Result = IGameController::OnEntity(pName, Pivot, P0, P1, P2, P3, PosEnv);
	vec2 Pos = (P0 + P1 + P2 + P3)/4.0f;

	if(str_comp(pName, "icInfected") == 0)
		m_avSpawnPoints[0].push_back(Pos);
	else if(str_comp(pName, "icHuman") == 0)
		m_avSpawnPoints[1].push_back(Pos);

	CIcEntity *pNewEntity = nullptr;
	if(str_comp(pName, "icInfected") == 0)
	{
		int SpawnX = static_cast<int>(Pos.x)/32.0f;
		int SpawnY = static_cast<int>(Pos.y)/32.0f;
		
		if(SpawnX >= 0 && SpawnX < m_MapWidth && SpawnY >= 0 && SpawnY < m_MapHeight)
		{
			m_GrowingMap[SpawnY*m_MapWidth+SpawnX] = 6;
		}
	}
	else if(str_comp(pName, "icHeroFlag") == 0)
	{
		//Add hero flag spawns
		m_HeroFlagPositions.add(Pos);
	}
	else if(str_comp(pName, "health") == 0)
	{
		CIcPickup *p = new CIcPickup(GameServer(), EICPickupType::Health, Pos);
		p->SetRespawnInterval(15);
		p->Spawn();
		pNewEntity = p;
	}
	else if(str_comp(pName, "armor") == 0)
	{
		CIcPickup *p = new CIcPickup(GameServer(), EICPickupType::Armor, Pos);
		p->SetRespawnInterval(15);
		p->Spawn();
		pNewEntity = p;
	}

	if(pNewEntity && (PosEnv >= 0))
	{
		pNewEntity->SetAnimatedPos(Pivot, Pos - Pivot, PosEnv);
	}

	return Result;
}

void CIcGameController::HandleCharacterTiles(CIcCharacter *pCharacter)
{
	ZoneData Data0;
	ZoneData Data1;
	ZoneData Data2;
	ZoneData Data3;

	GetDamageZoneValueAt(vec2(pCharacter->GetPos().x + pCharacter->GetProximityRadius() / 3.f, pCharacter->GetPos().y - pCharacter->GetProximityRadius() / 3.f), &Data0);
	GetDamageZoneValueAt(vec2(pCharacter->GetPos().x + pCharacter->GetProximityRadius() / 3.f, pCharacter->GetPos().y + pCharacter->GetProximityRadius() / 3.f), &Data1);
	GetDamageZoneValueAt(vec2(pCharacter->GetPos().x - pCharacter->GetProximityRadius() / 3.f, pCharacter->GetPos().y - pCharacter->GetProximityRadius() / 3.f), &Data2);
	GetDamageZoneValueAt(vec2(pCharacter->GetPos().x - pCharacter->GetProximityRadius() / 3.f, pCharacter->GetPos().y + pCharacter->GetProximityRadius() / 3.f), &Data3);

	icArray<int, 4> Indices;
	Indices.Add(Data0.Index);
	Indices.Add(Data1.Index);
	Indices.Add(Data2.Index);
	Indices.Add(Data3.Index);

	if(Indices.Contains(ZONE_DAMAGE_DEATH))
	{
		pCharacter->Die(pCharacter->GetCid(), EDamageType::DEATH_TILE);
	}
	else if(pCharacter->GetPlayerClass() != EPlayerClass::Undead && Indices.Contains(ZONE_DAMAGE_DEATH_NOUNDEAD))
	{
		pCharacter->Die(pCharacter->GetCid(), EDamageType::DEATH_TILE);
	}
	else if(pCharacter->IsInfected() && Indices.Contains(ZONE_DAMAGE_DEATH_INFECTED))
	{
		pCharacter->Die(pCharacter->GetCid(), EDamageType::DEATH_TILE);
	}
	else if(pCharacter->IsAlive() && Indices.Contains(ZONE_DAMAGE_INFECTION))
	{
		if((GetRoundType() == ERoundType::Survival) && pCharacter->IsHuman())
		{
			int Damage = 3;
			pCharacter->OnCharacterInDamageZone(Damage, 0.25f);
		}
		else
		{
			pCharacter->OnCharacterInInfectionZone();
		}
	}
	if(pCharacter->IsAlive() && !Indices.Contains(ZONE_DAMAGE_INFECTION))
	{
		pCharacter->OnCharacterOutOfInfectionZone();
	}

	int TeamDamageIndex = pCharacter->IsHuman() ? ZONE_DAMAGE_DAMAGE_HUMANS : ZONE_DAMAGE_DAMAGE_INFECTED;
	bool TakeDamage = Indices.Contains(ZONE_DAMAGE_DAMAGE) || Indices.Contains(TeamDamageIndex);
	if(TakeDamage)
	{
		int Damage = 0;
		for(const ZoneData &Data : { Data0, Data1, Data2, Data3 })
		{
			if((Data.Index == ZONE_DAMAGE_DAMAGE) || Data.Index == TeamDamageIndex)
			{
				Damage = maximum(Damage, Data.ExtraData);
			}
		}

		if(Damage > 0)
		{
			pCharacter->OnCharacterInDamageZone(Damage);
		}
	}
}

void CIcGameController::HandleLastHookers()
{
	const int CurrentTick = Server()->Tick();
	ClientsArray CharacterHookedBy[MAX_CLIENTS]{};

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CIcCharacter *pCharacter = GetCharacter(i);
		if(!pCharacter)
		{
			continue;
		}

		int HookedPlayer = pCharacter->GetHookedPlayer();
		if(HookedPlayer < 0)
		{
			continue;
		}

		CharacterHookedBy[HookedPlayer].Add(i);
	}

	for(int TargetCid = 0; TargetCid < MAX_CLIENTS; ++TargetCid)
	{
		ClientsArray &HookedBy = CharacterHookedBy[TargetCid];
		if(HookedBy.IsEmpty())
		{
			continue;
		}
		CIcCharacter *pHookedCharacter = GetCharacter(TargetCid);
		if(!pHookedCharacter)
		{
			continue;
		}

		if(HookedBy.Size() > 1)
		{
			SortCharactersByDistance(HookedBy, &HookedBy, pHookedCharacter->GetPos());
		}
		pHookedCharacter->UpdateLastHookers(HookedBy, CurrentTick);
	}
}

bool CIcGameController::CanSeeDetails(int Who, int Whom) const
{
	if(Who == SERVER_DEMO_CLIENT)
		return true;

	CIcPlayer *pWhom = GetPlayer(Whom);
	if(!pWhom || pWhom->GetTeam() == TEAM_SPECTATORS)
		return false;

	CIcPlayer *pWho = GetPlayer(Who);
	if(!pWho)
		return false;

	if(pWho->GetTeam() == TEAM_SPECTATORS)
		return Config()->m_SvStrictSpectateMode == 0;

	return pWho->IsHuman() == pWhom->IsHuman();
}

CClientMask CIcGameController::GetBlindCharactersMask(int ExcludeCid) const
{
	CClientMask Mask;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ExcludeCid)
			continue;

		const CIcCharacter *pTarget = GetCharacter(i);
		if(!pTarget)
			continue;
		if(!pTarget->IsBlind())
			continue;

		Mask.set(i);
	}

	return Mask;
}

CClientMask CIcGameController::GetMaskForPlayerWorldEvent(int Asker, int ExceptId)
{
	dbg_assert(Asker >= 0, "Incorrect client id");

	const CIcCharacter *pCharacter = GetCharacter(Asker);
	if(!pCharacter || !pCharacter->IsInvisible())
	{
		if(ExceptId == -1)
			return CClientMask().set();
		return CClientMask().set().reset(ExceptId);
	}

	return m_Teams.TeamMask(GetPlayerTeam(Asker), ExceptId, Asker);
}

bool CIcGameController::HumanWallAllowedInPos(const vec2 &Pos) const
{
	const float Radius = 32.0f;

	if (GetDamageZoneValueAt(Pos) == ZONE_DAMAGE_INFECTION)
		return false;

	{ // Check for spawns
		int Type = 0; // InfectedSpawn

		// get spawn point
		for(std::size_t i = 0; i < m_avSpawnPoints[Type].size(); i++)
		{
			if(distance(Pos, m_avSpawnPoints[Type][i]) <= Radius)
			{
				return false;
			}
		}
	}

	return true;
}

int CIcGameController::GetZoneValueAt(int ZoneHandle, const vec2 &Pos, ZoneData *pData) const
{
	return GameServer()->Collision()->GetZoneValueAt(ZoneHandle, Pos, pData);
}

int CIcGameController::GetDamageZoneValueAt(const vec2 &Pos, ZoneData *pData) const
{
	int DamageIndex = GetZoneValueAt(m_ZoneHandle_icDamage, Pos, pData);
	if(!DamageIndex && m_VanillaMapLoaded)
	{
		int GameTile = GameServer()->Collision()->GetCollisionAt(Pos.x, Pos.y);
		if(GameTile == TILE_DEATH)
		{
			DamageIndex = ZONE_DAMAGE_DEATH;

			if(pData)
			{
				pData->Index = DamageIndex;
				pData->ExtraData = 0;
			}
		}
	}

	return DamageIndex;
}

EZoneTele CIcGameController::GetTeleportZoneValueAt(const vec2 &Pos, ZoneData *pData) const
{
	return static_cast<EZoneTele>(GetZoneValueAt(m_ZoneHandle_icTeleport, Pos, pData));
}

int CIcGameController::GetBonusZoneValueAt(const vec2 &Pos, ZoneData *pData) const
{
	return GetZoneValueAt(m_ZoneHandle_icBonus, Pos, pData);
}

void CIcGameController::ExecuteFileEx(const char *pBaseName)
{
	char aBuf[256];
	const char *pFileName = pBaseName;
	{
		const char aPlayersNumberVar[] = "${players_number}";
		const char *pPlayersNumber = str_find(pFileName, aPlayersNumberVar);
		if(pPlayersNumber)
		{
			int ClientException = -1;
			int NumHumans;
			int NumInfected;
			GetPlayerCounter(ClientException, NumHumans, NumInfected);
			int Count = NumHumans + NumInfected;

			str_copy(aBuf, pFileName);
			const std::ptrdiff_t Offset = pPlayersNumber - pFileName;
			str_format(aBuf + Offset, std::size(aBuf) - Offset, "%d%s", Count, pPlayersNumber + std::size(aPlayersNumberVar) - 1);
			pFileName = &aBuf[0];
		}
	}

	{
		const char aMapNameVar[] = "${map_name}";
		const char *pVarIndex = str_find(pFileName, aMapNameVar);
		if(pVarIndex)
		{
			if(pFileName != &aBuf[0])
			{
				str_copy(aBuf, pFileName);
			}

			const char *pMapName = Server()->GetMapName();
			const std::ptrdiff_t Offset = pVarIndex - pFileName;
			str_format(aBuf + Offset, std::size(aBuf) - Offset, "%s%s", pMapName, pVarIndex + std::size(aMapNameVar) - 1);
			pFileName = &aBuf[0];
		}
	}

	Console()->ExecuteFile(pFileName, -1, true);
}

void CIcGameController::CreateExplosion(const vec2 &Pos, int Owner, EDamageType DamageType, float DamageFactor)
{
	int Weapon = WEAPON_WORLD;
	GameServer()->CreateExplosion(Pos, Owner, Weapon);

	if(DamageFactor != 0)
	{
		// deal damage
		bool AffectOwner = true;
		if(DamageType == EDamageType::WHITE_HOLE)
			AffectOwner = false;

		CIcCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = GameWorld()->FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			if(apEnts[i]->GetCid() == Owner)
			{
				if(!AffectOwner)
					continue;
			}
			if(!Config()->m_InfShockwaveAffectHumans)
			{
				if(apEnts[i]->GetCid() == Owner)
				{
					//owner selfharm
				}
				else if(apEnts[i]->IsHuman())
				{
					continue;// humans are not affected by force
				}
			}
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			if(l)
				ForceDir = normalize(Diff);
			l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Dmg = 6 * l * DamageFactor;
			if((int)Dmg)
			{
				apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, Owner, DamageType);
			}
		}
	}
}

// Thanks to Stitch for the idea
void CIcGameController::CreateExplosionDisk(vec2 Pos, float InnerRadius, float DamageRadius, int Damage, float Force, int Owner, EDamageType DamageType)
{
	CreateExplosionDiskGfx(Pos, InnerRadius, DamageRadius, Owner);

	if(Damage > 0)
	{
		// deal damage
		CIcCharacter *apEnts[MAX_CLIENTS];
		int Num = GameWorld()->FindEntities(Pos, DamageRadius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			if (Diff.x == 0.0f && Diff.y == 0.0f)
				Diff.y = -0.5f;
			vec2 ForceDir(0,1);
			float len = length(Diff);
			len = 1-clamp((len-InnerRadius)/(DamageRadius-InnerRadius), 0.0f, 1.0f);
			
			if(len)
				ForceDir = normalize(Diff);
			
			float DamageToDeal = 1 + ((Damage - 1) * len);
			apEnts[i]->TakeDamage(ForceDir*Force*len, DamageToDeal, Owner, DamageType);
		}
	}
}

void CIcGameController::CreateExplosionDiskGfx(vec2 Pos, float InnerRadius, float DamageRadius, int Owner)
{
	int Weapon = WEAPON_WORLD;
	GameServer()->CreateExplosion(Pos, Owner, Weapon);

	float CircleLength = 2.0*pi*maximum(DamageRadius-135.0f, 0.0f);
	int NumSuroundingExplosions = CircleLength/32.0f;
	float AngleStart = random_float()*pi*2.0f;
	float AngleStep = pi*2.0f/static_cast<float>(NumSuroundingExplosions);
	const float Radius = (DamageRadius-135.0f);
	for(int i=0; i<NumSuroundingExplosions; i++)
	{
		vec2 Offset = vec2(Radius * cos(AngleStart + i*AngleStep), Radius * sin(AngleStart + i*AngleStep));
		GameServer()->CreateExplosion(Pos + Offset, Owner, Weapon);
	}
}

void CIcGameController::SendHammerDot(const vec2 &Pos, int SnapId)
{
	CNetObj_Projectile *pObj = Server()->SnapNewItem<CNetObj_Projectile>(SnapId);
	if(!pObj)
		return;;

	pObj->m_X = Pos.x;
	pObj->m_Y = Pos.y;
	pObj->m_VelX = 0;
	pObj->m_VelY = 0;
	pObj->m_Type = WEAPON_HAMMER;
	pObj->m_StartTick = Server()->Tick();
}

void CIcGameController::SendServerParams(int ClientId) const
{
	CNetMsg_InfClass_ServerParams Msg{};
	Msg.m_Version = 1;
	if(WhiteHoleEnabled())
	{
		Msg.m_WhiteHoleMinKills = Config()->m_InfWhiteHoleMinimalKills;
	}
	Msg.m_SoldierBombs = Config()->m_InfSoldierBombs;

	if(ClientId == -1)
	{
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			CIcPlayer *pPlayer = GetPlayer(i);
			if(pPlayer)
			{
				int InfclassVersion = Server()->GetClientInfclassVersion(i);
				if(InfclassVersion >= VERSION_INFC_180)
				{
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
				}
			}
		}
	}
	else
	{
		int InfclassVersion = Server()->GetClientInfclassVersion(ClientId);
		if(InfclassVersion >= VERSION_INFC_180)
		{
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientId);
		}
	}
}

void CIcGameController::ResetFinalExplosion()
{
	m_FinalExplosionState = EFinalExplosionState::NotStarted;

	for(int j = 0; j < m_MapHeight; j++)
	{
		for(int i = 0; i < m_MapWidth; i++)
		{
			if(!(m_GrowingMap[j * m_MapWidth + i] & 4))
			{
				m_GrowingMap[j * m_MapWidth + i] = 1;
			}
		}
	}
}

void CIcGameController::SaveRoundRules()
{
	SendServerParams(-1);
}

void CIcGameController::StartSurvivalGame()
{
	m_SurvivalState.Scores.Clear();
	m_SurvivalState.Kills = 0;
	m_SurvivalState.KilledPlayers.Clear();
	m_SurvivalState.SurvivedPlayers.Clear();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CIcPlayer *pPlayer = GetPlayer(i);
		if(pPlayer)
		{
			pPlayer->ResetRoundData();
			CIcCharacter *pCharacter = pPlayer->GetCharacter();
			if(pCharacter)
			{
				pPlayer->KillCharacter();
				pPlayer->SetClass(EPlayerClass::None);
			}
		}
	}
}

void CIcGameController::EndSurvivalGame()
{
	// Sync the scores
	for(PlayerScore &Score : m_SurvivalState.Scores)
	{
		if(Score.ClientId < 0)
			continue;

		CIcPlayer *pPlayer = GetPlayer(Score.ClientId);
		Score.Kills = pPlayer->GetKills();
		str_copy(Score.aPlayerName, Server()->ClientName(pPlayer->GetCid()));
	}

	const auto Sorter = [](const PlayerScore &s1, const PlayerScore &s2) -> bool {
		return s1.Kills > s2.Kills;
	};

	std::stable_sort(m_SurvivalState.Scores.begin(), m_SurvivalState.Scores.end(), Sorter);

	GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE, "Score", nullptr);
	for(const PlayerScore &Score : m_SurvivalState.Scores)
	{
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE, "- {str:PlayerName}: {int:Score}",
			"PlayerName", Score.aPlayerName,
			"Score", &Score.Kills,
			nullptr);
	}
	int Score = m_SurvivalState.Kills;
	GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE, "Total team score: {int:Score}",
		"Score", &Score,
		nullptr);

	if(m_BestSurvivalScore)
	{
		if(Score > m_BestSurvivalScore)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE, "This is the new best score on the server!",
				nullptr);
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE, "The previous best score is {int:Score}",
				"Score", &m_BestSurvivalScore,
				nullptr);
		}
		else if(Score == m_BestSurvivalScore)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE, "This is the same score as the best one!",
				nullptr);
		}
		else
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE, "The best score is: {int:Score}",
				"Score", &m_BestSurvivalScore,
				nullptr);
		}
	}

	if(Score > m_BestSurvivalScore)
	{
		m_BestSurvivalScore = Score;
	}

	m_SurvivalState.Wave = 0;

	m_SurvivalState.Kills = 0;
	m_SurvivalState.Scores.Clear();
	m_SurvivalState.SurvivedPlayers.Clear();
	m_SurvivalState.KilledPlayers.Clear();
}

int CIcGameController::GetRoundTick() const
{
	return Server()->Tick() - m_RoundStartTick;
}

int CIcGameController::GetInfectionTick() const
{
	return Server()->Tick() - GetInfectionStartTick();
}

int CIcGameController::GetInfectionStartTick() const
{
	const int InfectionTick = m_RoundStartTick + Server()->TickSpeed() * GetInfectionDelay();
	return InfectionTick;
}

bool CIcGameController::IsDefenderClass(EPlayerClass PlayerClass)
{
	switch (PlayerClass)
	{
		case EPlayerClass::Engineer:
		case EPlayerClass::Soldier:
		case EPlayerClass::Scientist:
		case EPlayerClass::Biologist:
		case EPlayerClass::Looper:
			return true;
		default:
			return false;
	}
}

bool CIcGameController::IsSupportClass(EPlayerClass PlayerClass)
{
	switch (PlayerClass)
	{
		case EPlayerClass::Ninja:
		case EPlayerClass::Mercenary:
		case EPlayerClass::Sniper:
			return true;
		default:
			return false;
	}
}

EPlayerClass CIcGameController::GetClassByName(const char *pClassName, bool *pOk)
{
	struct ExtraName
	{
		ExtraName(const char *pN, EPlayerClass Class) :
			pName(pN), PlayerClass(Class)
		{
		}

		const char *pName = nullptr;
		EPlayerClass PlayerClass = EPlayerClass::Invalid;
	};

	static const ExtraName extraNames[] = {
		ExtraName("bio", EPlayerClass::Biologist),
		ExtraName("bios", EPlayerClass::Biologist),
		ExtraName("engi", EPlayerClass::Engineer),
		ExtraName("merc", EPlayerClass::Mercenary),
		ExtraName("mercs", EPlayerClass::Mercenary),
		ExtraName("sci", EPlayerClass::Scientist),
		ExtraName("random", EPlayerClass::Random),
	};
	if(pOk)
	{
		*pOk = true;
	}
	for(const ExtraName &Extra : extraNames)
	{
		if(str_comp(pClassName, Extra.pName) == 0)
		{
			return Extra.PlayerClass;
		}
	}

	for(EPlayerClass PlayerClass : AllPlayerClasses)
	{
		const char *pSingularName = CIcGameController::GetClassName(PlayerClass);
		const char *pPluralName = CIcGameController::GetClassPluralName(PlayerClass);
		if((str_comp(pClassName, pSingularName) == 0) || (str_comp(pClassName, pPluralName) == 0)) {
			return static_cast<EPlayerClass>(PlayerClass);
		}
	}

	if(pOk)
	{
		*pOk = false;
	}
	return EPlayerClass::Invalid;
}

const char *CIcGameController::GetClassName(EPlayerClass PlayerClass)
{
	return toString(PlayerClass);
}

const char *CIcGameController::GetClassPluralName(EPlayerClass PlayerClass)
{
	switch(PlayerClass)
	{
	case EPlayerClass::Mercenary:
		return "mercenaries";
	case EPlayerClass::Medic:
		return "medics";
	case EPlayerClass::Hero:
		return "heroes";
	case EPlayerClass::Engineer:
		return "engineers";
	case EPlayerClass::Soldier:
		return "soldiers";
	case EPlayerClass::Ninja:
		return "ninjas";
	case EPlayerClass::Sniper:
		return "snipers";
	case EPlayerClass::Scientist:
		return "scientists";
	case EPlayerClass::Biologist:
		return "biologists";
	case EPlayerClass::Looper:
		return "loopers";

	case EPlayerClass::Smoker:
		return "smokers";
	case EPlayerClass::Boomer:
		return "boomers";
	case EPlayerClass::Hunter:
		return "hunters";
	case EPlayerClass::Bat:
		return "bats";
	case EPlayerClass::Ghost:
		return "ghosts";
	case EPlayerClass::Spider:
		return "spiders";
	case EPlayerClass::Ghoul:
		return "ghouls";
	case EPlayerClass::Slug:
		return "slugs";
	case EPlayerClass::Voodoo:
		return "voodoos";
	case EPlayerClass::Witch:
		return "witches";
	case EPlayerClass::Undead:
		return "undeads";

	case EPlayerClass::Invalid:
	case EPlayerClass::None:
	case EPlayerClass::Count:
		break;
	}

	return "unknown";
}

const char *CIcGameController::GetClassDisplayName(EPlayerClass PlayerClass, const char *pDefaultText)
{
	switch(PlayerClass)
	{
	case EPlayerClass::Mercenary:
		return _("Mercenary");
	case EPlayerClass::Medic:
		return _("Medic");
	case EPlayerClass::Hero:
		return _("Hero");
	case EPlayerClass::Engineer:
		return _("Engineer");
	case EPlayerClass::Soldier:
		return _("Soldier");
	case EPlayerClass::Ninja:
		return _("Ninja");
	case EPlayerClass::Sniper:
		return _("Sniper");
	case EPlayerClass::Scientist:
		return _("Scientist");
	case EPlayerClass::Biologist:
		return _("Biologist");
	case EPlayerClass::Looper:
		return _("Looper");

	case EPlayerClass::Smoker:
		return _("Smoker");
	case EPlayerClass::Boomer:
		return _("Boomer");
	case EPlayerClass::Hunter:
		return _("Hunter");
	case EPlayerClass::Bat:
		return _("Bat");
	case EPlayerClass::Ghost:
		return _("Ghost");
	case EPlayerClass::Spider:
		return _("Spider");
	case EPlayerClass::Ghoul:
		return _("Ghoul");
	case EPlayerClass::Slug:
		return _("Slug");
	case EPlayerClass::Voodoo:
		return _("Voodoo");
	case EPlayerClass::Witch:
		return _("Witch");
	case EPlayerClass::Undead:
		return _("Undead");

	case EPlayerClass::Invalid:
	case EPlayerClass::None:
	case EPlayerClass::Count:
		break;
	}

	return pDefaultText ? pDefaultText : "Unknown";
}

const char *CIcGameController::GetClassDisplayNameForKilledBy(EPlayerClass PlayerClass, const char *pDefaultText)
{
	switch(PlayerClass)
	{
		// Only the infected classes are used for now; do not add others to keep the translations smaller
	case EPlayerClass::Smoker:
		return _C_NOOP("For 'Killed by <>'", "a Smoker");
	case EPlayerClass::Boomer:
		return _C_NOOP("For 'Killed by <>'", "a Boomer");
	case EPlayerClass::Hunter:
		return _C_NOOP("For 'Killed by <>'", "a Hunter");
	case EPlayerClass::Bat:
		return _C_NOOP("For 'Killed by <>'", "a Bat");
	case EPlayerClass::Ghost:
		return _C_NOOP("For 'Killed by <>'", "a Ghost");
	case EPlayerClass::Spider:
		return _C_NOOP("For 'Killed by <>'", "a Spider");
	case EPlayerClass::Ghoul:
		return _C_NOOP("For 'Killed by <>'", "a Ghoul");
	case EPlayerClass::Slug:
		return _C_NOOP("For 'Killed by <>'", "a Slug");
	case EPlayerClass::Voodoo:
		return _C_NOOP("For 'Killed by <>'", "a Voodoo");
	case EPlayerClass::Witch:
		return _C_NOOP("For 'Killed by <>'", "a Witch");
	case EPlayerClass::Undead:
		return _C_NOOP("For 'Killed by <>'", "an Undead");

	default:
		break;
	}

	return pDefaultText ? pDefaultText : "Unknown";
}

const char *CIcGameController::GetClanForClass(EPlayerClass PlayerClass, const char *pDefaultText)
{
	switch (PlayerClass)
	{
		default:
			return GetClassDisplayName(PlayerClass, pDefaultText);
	}
}

const char *CIcGameController::GetClassPluralDisplayName(EPlayerClass PlayerClass)
{
	switch(PlayerClass)
	{
	case EPlayerClass::Mercenary:
		return _("Mercenaries");
	case EPlayerClass::Medic:
		return _("Medics");
	case EPlayerClass::Hero:
		return _("Heroes");
	case EPlayerClass::Engineer:
		return _("Engineers");
	case EPlayerClass::Soldier:
		return _("Soldiers");
	case EPlayerClass::Ninja:
		return _("Ninjas");
	case EPlayerClass::Sniper:
		return _("Snipers");
	case EPlayerClass::Scientist:
		return _("Scientists");
	case EPlayerClass::Biologist:
		return _("Biologists");
	case EPlayerClass::Looper:
		return _("Loopers");

	case EPlayerClass::Smoker:
		return _("Smokers");
	case EPlayerClass::Boomer:
		return _("Boomers");
	case EPlayerClass::Hunter:
		return _("Hunters");
	case EPlayerClass::Bat:
		return _("Bats");
	case EPlayerClass::Ghost:
		return _("Ghosts");
	case EPlayerClass::Spider:
		return _("Spiders");
	case EPlayerClass::Ghoul:
		return _("Ghouls");
	case EPlayerClass::Slug:
		return _("Slugs");
	case EPlayerClass::Voodoo:
		return _("Voodoos");
	case EPlayerClass::Witch:
		return _("Witches");
	case EPlayerClass::Undead:
		return _("Undeads");

	case EPlayerClass::Invalid:
	case EPlayerClass::None:
	case EPlayerClass::Count:
		break;
	}
	return _("Unknown");
}

EPlayerClass CIcGameController::MenuClassToPlayerClass(int MenuClass)
{
	EPlayerClass PlayerClass = EPlayerClass::Invalid;
	switch(MenuClass)
	{
		case CMapConverter::MENUCLASS_MEDIC:
			PlayerClass = EPlayerClass::Medic;
			break;
		case CMapConverter::MENUCLASS_HERO:
			PlayerClass = EPlayerClass::Hero;
			break;
		case CMapConverter::MENUCLASS_NINJA:
			PlayerClass = EPlayerClass::Ninja;
			break;
		case CMapConverter::MENUCLASS_MERCENARY:
			PlayerClass = EPlayerClass::Mercenary;
			break;
		case CMapConverter::MENUCLASS_SNIPER:
			PlayerClass = EPlayerClass::Sniper;
			break;
		case CMapConverter::MENUCLASS_RANDOM:
			PlayerClass = EPlayerClass::None;
			break;
		case CMapConverter::MENUCLASS_ENGINEER:
			PlayerClass = EPlayerClass::Engineer;
			break;
		case CMapConverter::MENUCLASS_SOLDIER:
			PlayerClass = EPlayerClass::Soldier;
			break;
		case CMapConverter::MENUCLASS_SCIENTIST:
			PlayerClass = EPlayerClass::Scientist;
			break;
		case CMapConverter::MENUCLASS_BIOLOGIST:
			PlayerClass = EPlayerClass::Biologist;
			break;
		case CMapConverter::MENUCLASS_LOOPER:
			PlayerClass = EPlayerClass::Looper;
			break;
	}

	return PlayerClass;
}

int CIcGameController::GetPlayerTeam(int ClientId) const
{
	return m_Teams.m_Core.Team(ClientId);
}

void CIcGameController::SetPlayerInfected(int ClientId, bool Infected)
{
	return m_Teams.m_Core.SetInfected(ClientId, Infected);
}

void CIcGameController::RegisterChatCommands(IConsole *pConsole)
{
	Console()->Register("inf_set_weapon_fire_delay", "s[weapon] i[msec]", CFGFLAG_SERVER, ConSetWeaponFireDelay, this,
		"Set InfClass weapon fire delay");
	Console()->Register("inf_set_weapon_ammo_regen", "s[weapon] i[msec]", CFGFLAG_SERVER, ConSetWeaponAmmoRegen, this,
		"Set InfClass weapon ammo regen interval");
	Console()->Register("inf_set_weapon_max_ammo", "s[weapon] i[ammo]", CFGFLAG_SERVER, ConSetWeaponMaxAmmo, this,
		"Set InfClass weapon max ammo");
	Console()->Register("inf_weapon_force", "s[weapon] ?f[force]", CFGFLAG_SERVER, ConWeaponForce, this, "Set InfClass weapon (base) force");
	Console()->Register("inf_list_weapons", "", CFGFLAG_SERVER, ConListWeapons, this, "List InfClass weapon names");

	pConsole->Register("restore_client_name", "i[ClientId]", CFGFLAG_SERVER, ConRestoreClientName, this, "Set the name of a player");
	pConsole->Register("set_client_name", "i[ClientId] r[name]", CFGFLAG_SERVER, ConSetClientName, this, "Set the name of a player (and also lock it)");
	pConsole->Register("lock_client_name", "i[ClientId] i[lock]", CFGFLAG_SERVER, ConLockClientName, this, "Set the name of a player");

	pConsole->Register("set_health_armor", "i[ClientId] i[health] i[armor]", CFGFLAG_SERVER, ConSetHealthArmor, this, "Set the player health/armor");
	pConsole->Register("set_invincible", "i[ClientId] i[level]", CFGFLAG_SERVER, ConSetInvincible, this, "Set the player invincibility level (1 inv to damage, 2 inv to inf, 3 inv to death tiles");
	pConsole->Register("set_hook_protection", "i[ClientId] i[protection]", CFGFLAG_SERVER, ConSetHookProtection, this, "Enable the player hook protection (0 disabled, 1 enabled)");
	pConsole->Register("give_upgrade", "i[ClientId]", CFGFLAG_SERVER, ConGiveUpgrade, this, "Give an upgrade to the player");
	pConsole->Register("inf_set_drop", "i[ClientId] ?i[level]", CFGFLAG_SERVER, ConSetDrop, this, "Make the character drop an upgrade on killed or died");

	pConsole->Register("inf_set_class", "i[ClientId] s[classname]", CFGFLAG_SERVER, ConSetClass, this, "Set the class of a player");
	pConsole->Register("queue_round", "s[type]", CFGFLAG_SERVER, ConQueueSpecialRound, this, "Queue a special round");
	pConsole->Register("start_round", "?s[type]", CFGFLAG_SERVER, ConStartRound, this, "Start a special round");

	pConsole->Register("start_fun_round", "", CFGFLAG_SERVER, ConStartFunRound, this, "Start a random fun round");
	pConsole->Register("start_special_fun_round", "s[classname] s[classname] ?s[more classes]", CFGFLAG_SERVER, ConStartSpecialFunRound, this, "Start a fun round");
	pConsole->Register("clear_fun_rounds", "", CFGFLAG_SERVER, ConClearFunRounds, this, "Clears added fun rounds");
	pConsole->Register("add_fun_round", "s[classname] s[classname] ?s[more classes]", CFGFLAG_SERVER, ConAddFunRound, this, "Add a fun round to be played when starting a fun round.");

	pConsole->Register("start_fast_round", "", CFGFLAG_SERVER, ConStartFastRound, this, "Start a faster gameplay round");
	pConsole->Register("queue_fast_round", "", CFGFLAG_SERVER, ConQueueFastRound, this, "Queue a faster gameplay round");
	pConsole->Register("queue_fun_round", "", CFGFLAG_SERVER, ConQueueFunRound, this, "Queue a fun gameplay round");
	pConsole->Register("print_players_picking", "", CFGFLAG_SERVER, ConPrintPlayerPickingTimestamp, this, "");
	pConsole->Register("map_rotation_status", "", CFGFLAG_SERVER, ConMapRotationStatus, this, "Print the status of map rotation");

	pConsole->Register("save_maps_data", "s[filename]", CFGFLAG_SERVER, ConSaveMapsData, this, "Save the map rotation data to a file");
	pConsole->Register("print_maps_data", "", CFGFLAG_SERVER, ConPrintMapsData, this, "Print the data of map rotation");
	pConsole->Register("reset_map_data", "s[mapname]", CFGFLAG_SERVER, ConResetMapData, this, "Reset map rotation data");
	pConsole->Register("add_map_data", "s[mapname] i[timestamp]", CFGFLAG_SERVER, ConAddMapData, this, "Add map rotation data");
	pConsole->Register("set_map_min_max_players", "s[mapname] i[min] ?i[max]", CFGFLAG_SERVER, ConSetMapMinMaxPlayers, this, "Set min/max players for a map");

	Console()->Register("prefer_class", "s[classname]", CFGFLAG_CHAT, ConPreferClass, this, "Set the preferred human class to <classname>");
	Console()->Register("alwaysrandom", "i['0'|'1']", CFGFLAG_CHAT, ConAlwaysRandom, this, "Set the preferred class to Random");
	Console()->Register("antiping", "i['0'|'1']", CFGFLAG_CHAT, ConAntiPing, this, "Try to improve your ping (reduce the traffic)");

	pConsole->Register("set_class", "s[classname]", CFGFLAG_CHAT, ConUserSetClass, this, "Set the class of a player");
	pConsole->Register("save_position", "", CFGFLAG_CHAT, ConSavePosition, this, "Save the current character position");
	pConsole->Register("load_position", "", CFGFLAG_CHAT, ConLoadPosition, this, "Load (restore) the current character position");
	pConsole->Register("sp", "", CFGFLAG_CHAT, ConSavePosition, this, "Save the current character position");
	pConsole->Register("lp", "", CFGFLAG_CHAT, ConLoadPosition, this, "Load (restore) the current character position");

	pConsole->Register("witch", "", CFGFLAG_CHAT, ChatWitch, this, "Call Witch");
	pConsole->Register("santa", "", CFGFLAG_CHAT, ChatWitch, this, "Call the Santa");
}

EInfclassWeapon CIcGameController::GetWeaponIdFromConArgument(IConsole::IResult *pResult, unsigned int Index)
{
	EInfclassWeapon WeaponId = fromString<EInfclassWeapon>(pResult->GetString(Index));
	if (WeaponId == EInfclassWeapon::Invalid)
	{
		// Fallback to old index-based setup
		const int WeaponIdInt = pResult->GetInteger(Index);
		if((WeaponIdInt >= 0) && (WeaponIdInt < NB_INFWEAPON))
		{
			WeaponId = static_cast<EInfclassWeapon>(WeaponIdInt);
		}
	}

	return WeaponId;
}

void CIcGameController::ConSetWeaponFireDelay(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	if(pResult->NumArguments() != 2)
		return;

	EInfclassWeapon WeaponId = GetWeaponIdFromConArgument(pResult, 0);
	if (WeaponId == EInfclassWeapon::Invalid)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", gs_aInvalidWeaponIdMsg);
		return;
	}

	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return;
	}

	pSelf->SetFireDelay(WeaponId, Interval);
}

void CIcGameController::ConSetWeaponAmmoRegen(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	if(pResult->NumArguments() != 2)
		return;

	EInfclassWeapon WeaponId = GetWeaponIdFromConArgument(pResult, 0);
	if (WeaponId == EInfclassWeapon::Invalid)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", gs_aInvalidWeaponIdMsg);
		return;
	}

	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return;
	}

	pSelf->SetAmmoRegenTime(WeaponId, Interval);

	return;
}

void CIcGameController::ConSetWeaponMaxAmmo(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	if(pResult->NumArguments() != 2)
		return;

	EInfclassWeapon WeaponId = GetWeaponIdFromConArgument(pResult, 0);
	if (WeaponId == EInfclassWeapon::Invalid)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", gs_aInvalidWeaponIdMsg);
		return;
	}
	int Interval = pResult->GetInteger(1);
	if(Interval < 0)
	{
		return;
	}

	pSelf->SetMaxAmmo(WeaponId, Interval);
}

void CIcGameController::ConWeaponForce(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	if(pResult->NumArguments() < 1)
		return;

	EInfclassWeapon WeaponId = GetWeaponIdFromConArgument(pResult, 0);
	if (WeaponId == EInfclassWeapon::Invalid)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", gs_aInvalidWeaponIdMsg);
		return;
	}

	if (pResult->NumArguments() < 2)
	{
		CFixedPointNumber Force = pSelf->GetWeaponForce(WeaponId);
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "Value: %s", Force.AsStr());
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "console", aBuf);
	}

	float Force = pResult->GetFloat(1);
	if(Force < 0)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Negative values are not allowed.");
		return;
	}

	if (Force > 1000)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "The given value is beyond all reasonable limits. Please use values in range from 0.1 to 100.");
		return;
	}

	pSelf->SetWeaponForce(WeaponId, Force);
}

void CIcGameController::ConListWeapons(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "Available weapons:");
	std::string line;
	std::size_t WeaponIndex = 0;
	bool LastWeaponProcessed{};
	do
	{
		line = "    ";
		for(;; ++WeaponIndex)
		{
			LastWeaponProcessed = WeaponIndex + 1 == NB_INFWEAPON;
			EInfclassWeapon WeaponId = static_cast<EInfclassWeapon>(WeaponIndex);
			line += toString(WeaponId);
			if(LastWeaponProcessed)
				break;

			line += ", ";
			if(line.size() > 80)
			{
				break;
			}
		}

		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", line.c_str());
	} while(!LastWeaponProcessed);
}

void CIcGameController::ConRestoreClientName(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;

	int PlayerId = pResult->GetInteger(0);

	CIcPlayer *pPlayer = pSelf->GetPlayer(PlayerId);
	if(!pPlayer)
	{
		return;
	}

	pPlayer->m_ClientNameLocked = true;
	pSelf->Server()->SetClientName(PlayerId, pPlayer->GetOriginalName());
}

void CIcGameController::ConSetClientName(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;

	int PlayerId = pResult->GetInteger(0);
	const char *pNewName = pResult->GetString(1);

	if(pResult->NumArguments() != 2)
	{
		return;
	}

	CIcPlayer *pPlayer = pSelf->GetPlayer(PlayerId);
	if(!pPlayer)
	{
		return;
	}

	pPlayer->m_ClientNameLocked = true;
	pSelf->Server()->SetClientName(PlayerId, pNewName);
}

void CIcGameController::ConLockClientName(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;

	int PlayerId = pResult->GetInteger(0);
	int Lock = pResult->GetInteger(1);

	if(pResult->NumArguments() != 2)
	{
		return;
	}

	CIcPlayer *pPlayer = pSelf->GetPlayer(PlayerId);
	if(!pPlayer)
	{
		return;
	}

	pPlayer->m_ClientNameLocked = Lock != 0;
}

void CIcGameController::ConPreferClass(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	int ClientId = pResult->GetClientId();

	const char *pClassName = pResult->GetString(0);
	pSelf->SetPreferredClass(ClientId, pClassName);
}

void CIcGameController::ConAlwaysRandom(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	int ClientId = pResult->GetClientId();

	bool Random = pResult->GetInteger(0) > 0;
	pSelf->SetPreferredClass(ClientId, Random ? EPlayerClass::Random : EPlayerClass::Invalid);
}

void CIcGameController::SetPreferredClass(int ClientId, const char *pClassName)
{
	bool Ok = false;
	EPlayerClass PlayerClass = GetClassByName(pClassName, &Ok);

	if(!Ok)
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT,
			_("Unable to set preferred class: Invalid class name"), nullptr);
		return;
	}
	SetPreferredClass(ClientId, PlayerClass);
}

void CIcGameController::SetPreferredClass(int ClientId, EPlayerClass Class)
{
	if(!IsHumanClass(Class))
	{
		return;
	}

	CIcPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
	{
		return;
	}

	pPlayer->SetPreferredClass(Class);

	switch(Class)
	{
	case EPlayerClass::Random:
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_PLAYER,
			_("A random class will be automatically attributed to you when round starts"),
			nullptr);
		break;
	case EPlayerClass::Invalid:
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_PLAYER,
			_("The class selector will be displayed when round starts"),
			nullptr);
		break;
	default:
	{
		const char *pClassDisplayName = GetClassDisplayName(Class);
		const char *pTranslated = Server()->Localization()->Localize(pPlayer->GetLanguage(), pClassDisplayName);
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_PLAYER,
			_("Class {str:ClassName} will be automatically attributed to you when round starts"),
			"ClassName", pTranslated,
			nullptr);
		break;
	}
	}
}

void CIcGameController::ConAntiPing(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	int ClientId = pResult->GetClientId();

	int Arg = pResult->GetInteger(0);
	dbg_msg("server", "set_antiping ClientId=%d antiping=%d", ClientId, Arg);

	CIcPlayer *pPlayer = pSelf->GetPlayer(ClientId);
	pPlayer->SetAntiPingEnabled(Arg > 0);
}

void CIcGameController::ConUserSetClass(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->ConUserSetClass(pResult);
}

void CIcGameController::ConUserSetClass(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetClientId();
	if(!Config()->m_InfTrainingMode)
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("The command is not available (enabled only in training mode)"), nullptr);
		return;
	}

	const char *pClassName = pResult->GetString(0);

	CIcPlayer *pPlayer = GetPlayer(ClientId);

	if(!pPlayer)
		return;

	bool Ok = false;
	EPlayerClass PlayerClass = GetClassByName(pClassName, &Ok);
	if(Ok)
	{
		pPlayer->SetClass(PlayerClass);
		const char *pClassDisplayName = GetClassDisplayName(PlayerClass);
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} changed the class to {str:ClassName}"),
									"PlayerName", Server()->ClientName(ClientId),
									"ClassName", pClassDisplayName,
									nullptr);

		return;
	}

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "inf_set_class", "Unknown class");
}

void CIcGameController::ConSetClass(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->ConSetClass(pResult);
}

void CIcGameController::ConSetClass(IConsole::IResult *pResult)
{
	int PlayerId = pResult->GetInteger(0);
	const char *pClassName = pResult->GetString(1);

	CIcPlayer *pPlayer = GetPlayer(PlayerId);

	if(!pPlayer)
		return;

	bool Ok = false;
	EPlayerClass PlayerClass = GetClassByName(pClassName, &Ok);
	if(Ok)
	{
		pPlayer->SetClass(PlayerClass);
		char aBuf[256];
		const char *pClassDisplayName = GetClassDisplayName(PlayerClass);
		str_format(aBuf, sizeof(aBuf), "The admin change the class of %s to %s", GameServer()->Server()->ClientName(PlayerId), pClassDisplayName);
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

		return;
	}

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "inf_set_class", "Unknown class");
}

FunRoundConfiguration CIcGameController::ParseFunRoundConfigArguments(IConsole::IResult *pResult)
{
	FunRoundConfiguration FunRoundConfig;

	for(int argN = 0; argN < pResult->NumArguments(); ++argN)
	{
		const char *pArgument = pResult->GetString(argN);
		bool Ok = true;
		const EPlayerClass PlayerClass = CIcGameController::GetClassByName(pArgument, &Ok);
		if(!Ok)
		{
			// Ignore other words (there can be "undeads vs heroes", ignore "vs" in such case)
			continue;
		}
		if(IsHumanClass(PlayerClass))
		{
			FunRoundConfig.HumanClass = PlayerClass;
		}
		if(IsInfectedClass(PlayerClass))
		{
			FunRoundConfig.InfectedClass = PlayerClass;
		}
	}

	return FunRoundConfig;
}

void CIcGameController::ConQueueSpecialRound(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	const char *pRoundTypeName = pResult->GetString(0);
	pSelf->ConQueueRound(pRoundTypeName);
}

void CIcGameController::ConQueueRound(const char *pRoundTypeName)
{
	ERoundType Type = fromString<ERoundType>(pRoundTypeName);
	if(Type == ERoundType::Invalid)
	{
		return;
	}
	QueueRoundType(Type);
}

void CIcGameController::ConStartRound(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	const char *pRoundTypeName = pResult->NumArguments() > 0 ? pResult->GetString(0) : nullptr;
	if(pRoundTypeName)
	{
		ERoundType Type = fromString<ERoundType>(pRoundTypeName);

		if(Type == ERoundType::Invalid)
		{
			return;
		}
		pSelf->QueueRoundType(Type);
	}

	pSelf->StartRound();
}

void CIcGameController::ConStartFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	if(pSelf->m_FunRoundConfigurations.empty())
	{
		int ClientId = pResult->GetClientId();
		const char *pErrorMessage = "Unable to start fun round: rounds configuration is empty";
		if(ClientId >= 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", pErrorMessage);
		}
		else
		{
			pSelf->GameServer()->SendChatTarget(-1, pErrorMessage);
		}
		return;
	}
	if (pSelf->m_FunRoundsPassed >= g_Config.m_FunRoundLimit)
	{
		pSelf->GameServer()->SendChat(-1, CGameContext::CHAT_ALL, "Unable to start fun round: limit per map reached");
		return;
	}

	pSelf->QueueRoundType(ERoundType::Fun);
	pSelf->StartRound();
}

void CIcGameController::ConQueueFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;

	if(pSelf->m_FunRoundConfigurations.empty())
	{
		int ClientId = pResult->GetClientId();
		const char *pErrorMessage = "Unable to start a fun round: rounds configuration is empty";
		if(ClientId >= 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", pErrorMessage);
		}
		else
		{
			pSelf->GameServer()->SendChatTarget(-1, pErrorMessage);
		}
		return;
	}

	pSelf->QueueRoundType(ERoundType::Fun);
}

void CIcGameController::ConStartSpecialFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	FunRoundConfiguration FunRoundConfig = ParseFunRoundConfigArguments(pResult);

	std::vector<FunRoundConfiguration> aOldConfigurations;
	std::swap(pSelf->m_FunRoundConfigurations, aOldConfigurations);
	pSelf->m_FunRoundConfigurations = {FunRoundConfig};

	pSelf->QueueRoundType(ERoundType::Fun);

	if(!pSelf->m_Warmup)
	{
		pSelf->StartRound();
	}

	std::swap(pSelf->m_FunRoundConfigurations, aOldConfigurations);
}

void CIcGameController::ConClearFunRounds(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->m_FunRoundConfigurations.clear();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "fun rounds cleared");
}

void CIcGameController::ConAddFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	FunRoundConfiguration FunRoundConfig = ParseFunRoundConfigArguments(pResult);

	if((FunRoundConfig.HumanClass == EPlayerClass::Invalid) || (FunRoundConfig.InfectedClass == EPlayerClass::Invalid))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid special fun round configuration");
		return;
	}
	else
	{
		char aBuf[256];
		const char *HumanClassText = CIcGameController::GetClassPluralDisplayName(FunRoundConfig.HumanClass);
		const char *InfectedClassText = CIcGameController::GetClassPluralDisplayName(FunRoundConfig.InfectedClass);
		str_format(aBuf, sizeof(aBuf), "Added fun round: %s vs %s", InfectedClassText, HumanClassText);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

	pSelf->m_FunRoundConfigurations.push_back(FunRoundConfig);
}

void CIcGameController::ConStartFastRound(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->QueueRoundType(ERoundType::Fast);
	pSelf->StartRound();
}

void CIcGameController::ConQueueFastRound(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->QueueRoundType(ERoundType::Fast);
}

void CIcGameController::ConPrintPlayerPickingTimestamp(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->ConPrintPlayerPickingTimestamp(pResult);
}

void CIcGameController::ConPrintPlayerPickingTimestamp(IConsole::IResult *pResult)
{
	char aBuf[256];
	int CurrentTimestamp = time_timestamp();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CIcPlayer *pPlayer = GetPlayer(i);
		if(pPlayer == nullptr)
			continue;
		if(pPlayer->IsBot())
			continue;

		int Timestamp = pPlayer->GetInfectionTimestamp();

		const char *pPickedSecondsAgo = "";
		char aSecondsBuf[32];
		if(Timestamp && CurrentTimestamp > Timestamp)
		{
			int SecondsAgo = CurrentTimestamp - Timestamp;
			str_format(aSecondsBuf, sizeof(aSecondsBuf), " (%d seconds ago)", SecondsAgo);
			pPickedSecondsAgo = aSecondsBuf;
		}

		str_format(aBuf, sizeof(aBuf), "id=%d name='%s' team='%d' ts=%d%s", i, Server()->ClientName(i), pPlayer->GetTeam(), Timestamp, pPickedSecondsAgo);

		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CIcGameController::ConMapRotationStatus(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->ConSmartMapRotationStatus();
}

void CIcGameController::ConSaveMapsData(IConsole::IResult *pResult, void *pUserData)
{
	const char *pFileName = pResult->GetString(0);

	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->SaveMapRotationData(pFileName);
}

void CIcGameController::ConPrintMapsData(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->PrintMapRotationData();
}

void CIcGameController::ConResetMapData(IConsole::IResult *pResult, void *pUserData)
{
	const char *pMapName = pResult->GetString(0);

	ResetMapInfo(pMapName);
}

void CIcGameController::ConAddMapData(IConsole::IResult *pResult, void *pUserData)
{
	const char *pMapName = pResult->GetString(0);
	int Timestamp = pResult->GetInteger(1);

	AddMapTimestamp(pMapName, Timestamp);
}

void CIcGameController::ConSetMapMinMaxPlayers(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	return pSelf->ConSetMapMinMaxPlayers(pResult);
}

void CIcGameController::ConSetMapMinMaxPlayers(IConsole::IResult *pResult)
{
	if((pResult->NumArguments() < 2) || (pResult->NumArguments() > 3))
	{
		return;
	}

	const char *pMapName = pResult->GetString(0);
	int MinPlayers = pResult->GetInteger(1);
	int MaxPlayers = pResult->NumArguments() == 3 ? pResult->GetInteger(2) : 0;

	SetMapMinMaxPlayers(pMapName, MinPlayers, MaxPlayers);
}

void CIcGameController::ConSavePosition(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	return pSelf->ConSavePosition(pResult);
}

void CIcGameController::ConSavePosition(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetClientId();
	if(!Config()->m_InfTrainingMode)
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("The command is not available (enabled only in training mode)"), nullptr);
		return;
	}

	CIcCharacter *pCharacter = GetCharacter(ClientId);
	if(!pCharacter)
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Unable to save the position: you have no character to save its position"), nullptr);
		return;
	}

	if(!pCharacter->IsAlive())
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Unable to save the position: the character state is not valid"), nullptr);
		return;
	}

	if(!pCharacter->IsGrounded())
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Unable to save the position: the character does not stand on the ground"), nullptr);
		return;
	}

	vec2 Position = pCharacter->GetPos();
	CIcPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
	{
		// What...
		return;
	}

	pPlayer->AddSavedPosition(Position);
}

void CIcGameController::ConLoadPosition(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	return pSelf->ConLoadPosition(pResult);
}

void CIcGameController::ConLoadPosition(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetClientId();
	if(!Config()->m_InfTrainingMode)
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("The command is not available (enabled only in training mode)"), nullptr);
		return;
	}

	CIcCharacter *pCharacter = GetCharacter(ClientId);
	if(!pCharacter)
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Unable to load the position: you have no character to load its position"), nullptr);
		return;
	}

	if(!pCharacter->IsAlive())
	{
		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Unable to load the position: the character state is not valid"), nullptr);
		return;
	}

	vec2 Position;
	CIcPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
	{
		// What...
		return;
	}

	pPlayer->LoadSavedPosition(&Position);

	pCharacter->SetPosition(Position);
	pCharacter->ResetVelocity();
	GameWorld()->ReleaseHooked(ClientId);
	pCharacter->ResetHook();
}

void CIcGameController::ConSetHealthArmor(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	return pSelf->ConSetHealthArmor(pResult);
}

void CIcGameController::ConSetHealthArmor(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetInteger(0);
	int Health = pResult->GetInteger(1);
	int Armor = pResult->GetInteger(2);

	CIcCharacter *pCharacter = GetCharacter(ClientId);
	if(!pCharacter)
	{
		return;
	}

	pCharacter->SetHealthArmor(Health, Armor);
}

void CIcGameController::ConSetInvincible(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	return pSelf->ConSetInvincible(pResult);
}

void CIcGameController::ConSetInvincible(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetInteger(0);
	int Invincible = pResult->GetInteger(1);

	CIcCharacter *pCharacter = GetCharacter(ClientId);
	if(!pCharacter)
	{
		return;
	}

	pCharacter->SetInvincible(Invincible);
}

void CIcGameController::ConSetHookProtection(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	return pSelf->ConSetHookProtection(pResult);
}

void CIcGameController::ConSetHookProtection(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetInteger(0);
	int Protection = pResult->GetInteger(1);

	CIcPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
	{
		return;
	}

	bool Automatic = false;
	pPlayer->SetHookProtection(Protection, Automatic);
}

void CIcGameController::ConGiveUpgrade(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	return pSelf->ConGiveUpgrade(pResult);
}

void CIcGameController::ConGiveUpgrade(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetInteger(0);

	CIcPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer || !pPlayer->GetCharacterClass())
	{
		return;
	}

	pPlayer->GetCharacterClass()->GiveUpgrade();
}

void CIcGameController::ConSetDrop(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	return pSelf->ConSetDrop(pResult);
}

void CIcGameController::ConSetDrop(IConsole::IResult *pResult)
{
	const int ClientId = pResult->GetInteger(0);
	CIcCharacter *pCharacter = GetCharacter(ClientId);
	if(!pCharacter || !pCharacter->IsAlive())
	{
		return;
	}

	const int UnreasonableMaxLevel = 99;
	const int DropLevel = pResult->NumArguments() > 1 ? pResult->GetInteger(1) : UnreasonableMaxLevel;
	if(DropLevel < 0)
	{
		return;
	}

	pCharacter->SetDropLevel(DropLevel);
}

void CIcGameController::ChatWitch(IConsole::IResult *pResult, void *pUserData)
{
	CIcGameController *pSelf = (CIcGameController *)pUserData;
	pSelf->ChatWitch(pResult);
}

void CIcGameController::ChatWitch(IConsole::IResult *pResult)
{
	int ClientId = pResult->GetClientId();
	const int REQUIRED_CALLERS_COUNT = 5;
	const int MIN_ZOMBIES = 2;

	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "conwitch", "ChatWitch() called");

	const bool Winter = EventsDirector::IsWinter();

	{
		bool CanCallWitch = true;
		if(GetRoundType() == ERoundType::Survival)
		{
			CanCallWitch = false;
		}

		if(!CanCallWitch)
		{
			const char *pMessage =  _("The witch is not available in this round");
			if(Winter)
			{
				pMessage = _("The Santa is not available in this round");
			}

			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, pMessage, nullptr);
			return;
		}
	}

	int MaxWitches = GetClassPlayerLimit(EPlayerClass::Witch);
	if(Winter)
	{
		// Santa is a new Witch; allow only one Santa at time.
		MaxWitches = 1;
	}
	if(GetInfectedCount(EPlayerClass::Witch) >= MaxWitches)
	{
		if(Winter)
		{
			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("The Santa is already here"), nullptr);
			return;
		}

		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("All witches are already here"), nullptr);
		return;
	}

	int Humans = 0;
	int Infected = 0;
	GetPlayerCounter(-1, Humans, Infected);

	if(Humans + Infected < REQUIRED_CALLERS_COUNT)
	{
		if(Winter)
		{
			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Too few players to call the Santa"), nullptr);
			return;
		}

		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Too few players to call a witch"), nullptr);
		return;
	}
	if(Infected < MIN_ZOMBIES)
	{
		if(Winter)
		{
			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Too few infected to call the Santa"), nullptr);
			return;
		}

		GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("Too few infected to call a witch"), nullptr);
		return;
	}

	// It is possible that we had the needed callers but all witches already were there.
	// In that case even if the caller is already in the list, we still want to spawn
	// a new one without a message to the caller.
	if(m_WitchCallers.Size() < REQUIRED_CALLERS_COUNT)
	{
		if(m_WitchCallers.Contains(ClientId))
		{
			if(Winter)
			{
				GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("You have called the Santa once again"), nullptr);
				return;
			}

			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT, _("You can't call witch twice"), nullptr);
			return;
		}

		m_WitchCallers.Add(ClientId);

		int PrintableRequiredCallers = REQUIRED_CALLERS_COUNT;
		int PrintableCallers = m_WitchCallers.Size();
		if(m_WitchCallers.Size() == 1)
		{
			if(Winter)
			{
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT,
					_("{str:PlayerName} is calling for Santa! (1/{int:RequiredCallers}) To call the Santa write: /santa"),
					"PlayerName", Server()->ClientName(ClientId),
					"RequiredCallers", &PrintableRequiredCallers,
					nullptr);
				return;
			}

			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT,
				_("{str:PlayerName} is calling for Witch! (1/{int:RequiredCallers}) To call witch write: /witch"),
				"PlayerName", Server()->ClientName(ClientId),
				"RequiredCallers", &PrintableRequiredCallers,
				nullptr);
		}
		else if(m_WitchCallers.Size() < REQUIRED_CALLERS_COUNT)
		{
			if(Winter)
			{
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT,
					_("Santa ({int:Callers}/{int:RequiredCallers})"),
					"Callers", &PrintableCallers,
					"RequiredCallers", &PrintableRequiredCallers,
					nullptr);
			}
			else
			{
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT,
					_("Witch ({int:Callers}/{int:RequiredCallers})"),
					"Callers", &PrintableCallers,
					"RequiredCallers", &PrintableRequiredCallers,
					nullptr);
			}
		}
	}

	if(m_WitchCallers.Size() >= REQUIRED_CALLERS_COUNT)
	{
		int WitchId = GetClientIdForNewWitch();
		if(WitchId < 0)
		{
			if(Winter)
			{
				GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT,
					_("The Santa is already here"),
					nullptr);
				return;
			}
			GameServer()->SendChatTarget_Localization(ClientId, CHATCATEGORY_DEFAULT,
				_("All witches are already here"),
				nullptr);
		}
		else
		{
			if(Winter)
			{
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT,
					_("Santa {str:PlayerName} has arrived!"),
					"PlayerName", Server()->ClientName(WitchId),
					nullptr);
				return;
			}

			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT,
				_("Witch {str:PlayerName} has arrived!"),
				"PlayerName", Server()->ClientName(WitchId),
				nullptr);
		}

		m_WitchCallers.Clear();
	}
}

CDoor *CIcGameController::AddDoor(const vec2 &From, const vec2 &To)
{
	return new CDoor(GameServer(), From, To);
}

IConsole *CIcGameController::Console() const
{
	return GameServer()->Console();
}

CIcPlayer *CIcGameController::GetPlayer(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;

	return CIcPlayer::GetInstance(GameServer()->m_apPlayers[ClientId]);
}

CIcCharacter *CIcGameController::GetCharacter(int ClientId) const
{
	CIcPlayer *pPlayer = GetPlayer(ClientId);
	return pPlayer ? pPlayer->GetCharacter() : nullptr;
}

int CIcGameController::GetPlayerOwnCursorId(int ClientId) const
{
	return m_PlayerOwnCursorId;
}

void CIcGameController::SortCharactersByDistance(ClientsArray *pCharacterIds, const vec2 &Center, const float MaxDistance)
{
	SortCharactersByDistance(*pCharacterIds, pCharacterIds, Center, MaxDistance);
}

void CIcGameController::SortCharactersByDistance(const ClientsArray &Input, ClientsArray *pOutput, const vec2 &Center, const float MaxDistance)
{
	struct DistanceItem
	{
		DistanceItem() = default;
		DistanceItem(int C, float D)
			: ClientId(C)
			, Distance(D)
		{
		}

		int ClientId;
		float Distance;

		bool operator<(const DistanceItem &AnotherDistanceItem) const
		{
			return Distance < AnotherDistanceItem.Distance;
		}
	};

	icArray<DistanceItem, MAX_CLIENTS> Distances;

	for(int ClientId : Input)
	{
		const CCharacter *pChar = GetCharacter(ClientId);
		if(!pChar)
			continue;

		const vec2 &CharPos = pChar->GetPos();
		const float Distance = std::max<float>(0.f, distance(CharPos, Center) - pChar->GetProximityRadius());
		if(MaxDistance && (Distance > MaxDistance))
			continue;

		Distances.Add(DistanceItem(ClientId, Distance));
	}

	std::sort(Distances.begin(), Distances.end());

	pOutput->Clear();
	for(const DistanceItem &DistanceItem : Distances)
	{
		pOutput->Add(DistanceItem.ClientId);
	}
}

void CIcGameController::GetSortedTargetsInRange(const vec2 &Center, const float Radius, const ClientsArray &SkipList, ClientsArray *pOutput)
{
	ClientsArray PossibleCids;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CCharacter *pChar = GetCharacter(ClientId);
		if(!pChar)
			continue;

		if(SkipList.Contains(ClientId))
			continue;

		PossibleCids.Add(ClientId);
	}

	SortCharactersByDistance(PossibleCids, pOutput, Center, Radius);
}

void CIcGameController::UpdateNinjaTargets()
{
	m_NinjaTargets.Clear();

	if(!m_InfectedStarted && !Config()->m_InfTrainingMode)
		return;

	if(GetRoundType() == ERoundType::Survival)
		return;

	int InfectedCount = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GetCharacter(i) && GetCharacter(i)->IsInfected())
		{
			InfectedCount++;
			if(GetPlayer(i)->GetClass() == EPlayerClass::Undead)
				continue;

			if(GetCharacter(i)->GetInfZoneTick() * Server()->TickSpeed() < 1000 * Config()->m_InfNinjaTargetAfkTime) // Make sure zombie is not camping in InfZone
			{
				m_NinjaTargets.Add(i);
			}
		}
	}

	if(InfectedCount < Config()->m_InfNinjaMinInfected)
	{
		m_NinjaTargets.Clear();
	}
}

void CIcGameController::ReservePlayerOwnSnapItems()
{
	m_PlayerOwnCursorId = Server()->SnapNewId();
}

void CIcGameController::FreePlayerOwnSnapItems()
{
	Server()->SnapFreeId(m_PlayerOwnCursorId);
}

void CIcGameController::SendHintMessage()
{
	if((g_Config.m_TipsInterval == 0) || (time_get() - m_LastTipTime < time_freq() * g_Config.m_TipsInterval * 60))
		return;

	m_LastTipTime = time_get();

	const int MessageIndex = random_int(0, std::size(gs_aHintMessages) - 1);
	const CHintMessage &Message = gs_aHintMessages[MessageIndex];
	dynamic_string Buffer;
	const char *pPrevLang = nullptr;
	bool Sent = false;

	const auto PrepareBufferForLanguage = [&](const char *pLang) {
		if(!pPrevLang || str_comp(pLang, pPrevLang) != 0)
		{
			pPrevLang = pLang;

			FormatHintMessage(Message, &Buffer, pLang);
		}
	};

	for(int CID = 0; CID < MAX_CLIENTS; ++CID)
	{
		const CIcPlayer *pPlayer = GetPlayer(CID);
		if(!pPlayer || pPlayer->IsBot() || !pPlayer->m_IsReady)
			continue;
		PrepareBufferForLanguage(GetPlayer(CID)->GetLanguage());
		GameServer()->SendChatTarget(CID, Buffer.buffer());
	}

	if(Sent && g_Config.m_SvDemoChat)
	{
		// for demo record
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientId = -1;

		PrepareBufferForLanguage("en");
		Msg.m_pMessage = Buffer.buffer();
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, SERVER_DEMO_CLIENT);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "*** %s", Msg.m_pMessage);
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "chat", aBuf);
	}
}

void CIcGameController::FormatHintMessage(const CHintMessage &Message, dynamic_string *pBuffer, const char *pLanguage) const
{
	pBuffer->clear();
	pBuffer->append("TIP: ");
	if (Message.m_pArg1Value)
	{
		Server()->Localization()->Format_L(*pBuffer, pLanguage, Message.m_pText, Message.m_pArg1Name, Message.m_pArg1Value);
	}
	else
	{
		Server()->Localization()->Format_L(*pBuffer, pLanguage, Message.m_pText);
	}
}

void CIcGameController::OnInfectionTriggered()
{
	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	const int NumPlayers = NumHumans + NumInfected;
	const int NumFirstPickedPlayers = GetMinimumInfectedForPlayers(NumPlayers);

	const int PlayersToInfect = maximum<int>(0, NumFirstPickedPlayers - NumInfected);
	StartInfectionGameplay(PlayersToInfect);

	m_InfUnbalancedTick = -1;
	MaybeSuggestMoreRounds();
}

void CIcGameController::StartInfectionGameplay(int PlayersToInfect)
{
	InfectHumans(PlayersToInfect);

	CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		CIcPlayer *pPlayer = Iter.Player();
		if(pPlayer->GetClass() == EPlayerClass::None)
		{
			pPlayer->SetClass(ChooseHumanClass(pPlayer));
			pPlayer->SetRandomClassChoosen();
			CIcCharacter *pCharacter = Iter.Player()->GetCharacter();
			if(pCharacter)
			{
				pCharacter->GiveRandomClassSelectionBonus();
			}
		}
		if(pPlayer->IsInfected() || pPlayer->IsInfectionStarted())
		{
			pPlayer->KillCharacter(); // Infect the player
			pPlayer->m_DieTick = m_RoundStartTick;
			continue;
		}
	}
}

void CIcGameController::MaybeSuggestMoreRounds()
{
	if(m_MoreRoundsSuggested)
		return;

	if(Config()->m_SvSuggestMoreRounds == 0)
		return;

	if(m_RoundCount != Config()->m_SvRoundsPerMap-1)
		return;

	m_SuggestMoreRounds = true;
}

CGameWorld *CIcGameController::GameWorld()
{
	return &GameServer()->m_World;
}

void CIcGameController::StartRound()
{
	const bool StartAfterGameOver = IsGameOver();

	ERoundType NewRoundType = m_QueuedRoundType;
	if (NewRoundType == ERoundType::Invalid)
	{
		NewRoundType = GetDefaultRoundType();
	}

	m_RoundType = NewRoundType;
	QueueRoundType(ERoundType::Invalid);

	m_WinCheckEnabled.reset();
	m_VotesEnabled.reset();
	m_RoundMinimumPlayers.reset();
	m_RoundMinimumInfected.reset();
	m_RoundTimeLimitSeconds.reset();
	m_RoundInfectionDelaySeconds.reset();

	switch(GetRoundType())
	{
	case ERoundType::Normal:
		break;
	case ERoundType::Fun:
	{
		if(m_FunRoundConfigurations.empty())
		{
			m_RoundType = ERoundType::Normal;
			break;
		}
		StartFunRound();
	}
		break;
	case ERoundType::Fast:
		GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
		GameServer()->SendChatTarget(-1, "Starting the 'fast' round. Good luck everyone!");
		break;
	case ERoundType::Survival:
	case ERoundType::Invalid:
		break;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CIcPlayer *pPlayer = GetPlayer(i);
		if(pPlayer)
		{
			Server()->SetClientMemory(i, CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE, true);
		}
	}

	m_RoundStarted = true;
	IGameController::StartRound();

	if(StartAfterGameOver)
	{
		IncreaseCurrentRoundCounter();
	}

	ResetRoundData();
	SaveRoundRules();
	OnStartRound();
}

void CIcGameController::ResetRoundData()
{
	Server()->ResetStatistics();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CIcPlayer *pPlayer = GetPlayer(i);
		if(pPlayer)
		{
			pPlayer->ResetRoundData();
		}
	}

	m_HeroGiftTick = 0;
	m_WitchCallers.Clear();
}

void CIcGameController::EndRound()
{
	// The EndRound() override is called only from the IGameController on map skipped or changed
	EndRound(ERoundEndReason::CANCELED);
}

void CIcGameController::EndRound(ERoundEndReason Reason)
{
	int NumHumans = 0;
	int NumInfected = 0;
	{
		GetPlayerCounter(-1, NumHumans, NumInfected);

		const char *pWinnerTeam = Reason == ERoundEndReason::FINISHED ? (NumHumans > 0 ? "humans" : "zombies") : "none";
		const char *pRoundType = toString(GetRoundType());

		// Win check
		const int Seconds = (Server()->Tick() - m_RoundStartTick) / ((float)Server()->TickSpeed());

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "round_end winner='%s' survivors='%d' duration='%d' round='%d of %d' type='%s'",
			pWinnerTeam,
			NumHumans, Seconds, m_RoundCount + 1, Config()->m_SvRoundsPerMap, pRoundType);
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}
	ResetFinalExplosion();
	IGameController::EndRound();

	if(Reason == ERoundEndReason::FINISHED)
	{
		MaybeSendStatistics();
		Server()->OnRoundIsOver();
	}
	else if(Reason == ERoundEndReason::CANCELED)
	{
		if(!m_InfectedStarted || (NumHumans + NumInfected) < 2)
		{
			m_GameOverTick = 0;
		}
	}

	m_InfectedStarted = false;

	switch(GetRoundType())
	{
	case ERoundType::Normal:
	case ERoundType::Fast:
		break;
	case ERoundType::Fun:
		EndFunRound();
		break;
	case ERoundType::Survival:
		EndSurvivalRound();
		break;
	case ERoundType::Invalid:
		break;
	}

	m_RoundStarted = false;
}

void CIcGameController::DoTeamChange(CPlayer *pBasePlayer, int Team, bool DoChatMsg)
{
	CIcPlayer *pPlayer = CIcPlayer::GetInstance(pBasePlayer);
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	IGameController::DoTeamChange(pPlayer, Team, false);

	int ClientId = pPlayer->GetCid();

	if(DoChatMsg)
	{
		if(Team == TEAM_SPECTATORS)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} joined the spectators"), "PlayerName", Server()->ClientName(ClientId), NULL);
		}
		else
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} joined the game"), "PlayerName", Server()->ClientName(ClientId), NULL);
		}
	}

	if(Team != TEAM_SPECTATORS)
	{
		PreparePlayerToJoin(pPlayer);
	}
}

void CIcGameController::GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected)
{
	NumHumans = 0;
	NumInfected = 0;
	
	//Count type of players
	CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if(Iter.ClientId() == ClientException) continue;
		
		if(Iter.Player()->IsInfected()) NumInfected++;
		else NumHumans++;
	}
}

int CIcGameController::GetMinimumInfectedForPlayers(int PlayersNumber) const
{
	if(m_RoundMinimumInfected.has_value())
		return m_RoundMinimumInfected.value();

	if(GetRoundType() == ERoundType::Fast)
	{
		//  7 | 3 vs 4 | 3.01
		//  8 | 3 vs 5 | 3.44
		//  9 | 3 vs 6 | 3.87
		// 10 | 4 vs 6 | 4.30
		// 11 | 4 vs 7 | 4.73
		// 12 | 5 vs 7 | 5.16
		return PlayersNumber * 0.43;
	}

	int InitialPlayersLimit = Config()->m_InfFirstInfectedLimit;
	int NumFirstInfected = 0;

	if(PlayersNumber > 20)
		NumFirstInfected = 4;
	else if(PlayersNumber > 8)
		NumFirstInfected = 3;
	else if(PlayersNumber > 3)
		NumFirstInfected = 2;
	else if(PlayersNumber > 1)
		NumFirstInfected = 1;
	else
		NumFirstInfected = 0;

	if(InitialPlayersLimit && NumFirstInfected > InitialPlayersLimit)
	{
		NumFirstInfected = InitialPlayersLimit;
	}

	return NumFirstInfected;
}

int CIcGameController::InfectedBonusArmor() const
{
	const float Factor = clamp<float>(m_InfBalanceBoostFactor, 0, 1);
	return Factor * 10;
}

void CIcGameController::SendKillMessage(int Victim, const DeathContext &Context)
{
	EDamageType DamageType = Context.DamageType;
	int VanillaWeapon = DamageTypeToWeapon(DamageType);
	int Killer = Context.Killer;
	int Assistant = Context.Assistant;

	if(Killer < 0)
	{
		Killer = Victim;
	}

	if ((Killer != Victim) && (VanillaWeapon < 0)) {
		VanillaWeapon = WEAPON_NINJA;
	}

	// Old clients have no idea about DAMAGE_TILEs,
	// and we don't need a different UI indication
	if(Context.DamageType == EDamageType::DAMAGE_TILE)
	{
		DamageType = EDamageType::DEATH_TILE;
	}

	// Substitute the weapon for clients for better UI icon
	if(DamageType == EDamageType::DEATH_TILE)
		VanillaWeapon = WEAPON_NINJA;

	int DamageTypeInt = static_cast<int>(DamageType);
	dbg_msg("inf-proto", "Sent kill message victim=%d damage_type=%s killer=%d assistant=%d", Victim, toString(DamageType), Killer, Assistant);

	CNetMsg_Inf_KillMsg InfClassMsg;
	InfClassMsg.m_Killer = Killer;
	InfClassMsg.m_Victim = Victim;
	InfClassMsg.m_Assistant = Assistant;
	InfClassMsg.m_InfDamageType = DamageTypeInt;
	InfClassMsg.m_Weapon = VanillaWeapon;

	CMsgPacker InfCPacker(InfClassMsg.ms_MsgId, false);
	InfClassMsg.Pack(&InfCPacker);

	CNetMsg_Sv_KillMsg VanillaMsg;
	VanillaMsg.m_Killer = Killer;
	VanillaMsg.m_Victim = Victim;
	VanillaMsg.m_Weapon = VanillaWeapon;
	VanillaMsg.m_ModeSpecial = InfClassModeSpecialSkip;

	CMsgPacker VanillaPacker(VanillaMsg.ms_MsgId, false);
	VanillaMsg.Pack(&VanillaPacker);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(Server()->ClientIngame(i))
		{
			IServer::CClientInfo Info;
			Server()->GetClientInfo(i, &Info);

			if(Info.m_InfClassVersion)
			{
				Server()->SendMsg(&InfCPacker, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
			}
			Server()->SendMsg(&VanillaPacker, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}

	Server()->SendMsg(&InfCPacker, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);
	Server()->SendMsg(&VanillaPacker, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

	if(VanillaWeapon != WEAPON_GAME)
	{
		CIcPlayer *pKiller = GetPlayer(Killer);
		CIcPlayer *pVictim = GetPlayer(Victim);
		CIcPlayer *pAssistant = GetPlayer(Assistant);
		if(pKiller && (pKiller != pVictim))
			pKiller->OnKill();
		if(pVictim)
			pVictim->OnDeath();
		if(pAssistant && (pAssistant != pVictim))
			pAssistant->OnAssist();
	}

	OnKillOrInfection(Victim, Context);
}

void CIcGameController::OnKillOrInfection(int Victim, const DeathContext &Context)
{
	if(GetRoundType() != ERoundType::Survival)
		return;

	int VanillaWeapon = DamageTypeToWeapon(Context.DamageType);

	if(VanillaWeapon == WEAPON_GAME)
		return;

	const CIcPlayer *pVictim = GetPlayer(Victim);
	const CIcPlayer *pKiller = Context.Killer < 0 ? pVictim : GetPlayer(Context.Killer);
	const int Tick = Server()->Tick();
	const int TickSpeed = Server()->TickSpeed();

	if(pKiller && pKiller->IsHuman() && (pVictim != pKiller))
	{
		m_SurvivalState.Kills++;
	}

	if(!pVictim || pVictim->IsBot() || !pVictim->IsHuman())
		return;

	if(!m_SurvivalState.KilledPlayers.Contains(Victim))
	{
		m_SurvivalState.KilledPlayers.Add(Victim);
	}

	const CIcCharacter *pVictimCharacter = pVictim->GetCharacter();
	if(pKiller && pKiller != pVictim)
	{
		const EPlayerClass KillerClass = pKiller->GetClass();
		icArray<const char *, 20> PossibleMessages;
		switch(Context.DamageType)
		{
		case EDamageType::MERCENARY_BOMB:
			PossibleMessages.Add(("{str:PlayerName} got to know their bomb too closely."));
			PossibleMessages.Add(("{str:PlayerName} rode the blast wave for the last time."));
			break;
		case EDamageType::BOOMER_EXPLOSION:
			PossibleMessages.Add(("{str:PlayerName} was exploded by {str:Killer}."));
			PossibleMessages.Add(("{str:PlayerName} was eliminated by {str:Killer}."));
			PossibleMessages.Add(("{str:PlayerName} met a boomer."));
			break;
		case EDamageType::SLUG_SLIME:
			PossibleMessages.Add(("{str:PlayerName} had no aid against {str:Killer}."));
			break;
		case EDamageType::SCIENTIST_MINE:
			PossibleMessages.Add(("{str:PlayerName} was electrified by {str:Killer}."));
			break;
		case EDamageType::DRYING_HOOK:
			if(pVictimCharacter->Core()->m_AttachedPlayers.size() >= 2)
			{
				const float StretchingDistance = 12 * TileSizeF;
				const float StretchingDistance2 = StretchingDistance * StretchingDistance;
				int Stretchers = 0;
				for(const auto &AttachedPlayerId : pVictimCharacter->Core()->m_AttachedPlayers)
				{
					const CCharacter *pOtherPlayer = GameServer()->GetPlayerChar(AttachedPlayerId);
					if(pOtherPlayer && distance_squared(pOtherPlayer->GetPos(), pVictimCharacter->GetPos()) > StretchingDistance2)
					{
						Stretchers++;
						if(Stretchers >= 2)
							break;
					}
				}

				if(Stretchers >= 2)
				{
					PossibleMessages.Clear();
					PossibleMessages.Add(("{str:PlayerName} did a stretching exercise."));
					PossibleMessages.Add(("{str:PlayerName} was torn apart."));
				}
				break;
			}
			break;
		default:
			break;
		}

		switch(KillerClass)
		{
		case EPlayerClass::Smoker:
			if(Context.DamageType == EDamageType::DRYING_HOOK)
			{
				PossibleMessages.Add(("{str:PlayerName} was drained by {str:Killer}."));
			}
			break;
		case EPlayerClass::Ghost:
			PossibleMessages.Add(("{str:PlayerName} was surprised by {str:Killer}."));
			break;
		case EPlayerClass::Bat:
			PossibleMessages.Add(("{str:PlayerName} was bitten by {str:Killer}."));
			break;
		default:
			break;
		}

		const CIcCharacter *pVictimCharacter = pVictim->GetCharacter();
		if(pVictimCharacter->GetAttackTick() + TickSpeed * 1.25f < Tick)
		{
			PossibleMessages.Add("{str:PlayerName} kinda gave up.");
		}
		else if(pVictimCharacter->GetLastNoAmmoSoundTick() + Server()->TickSpeed() * 0.6 < Tick)
		{
			PossibleMessages.Add("{str:PlayerName} had no ammo to kill them all.");
			PossibleMessages.Add("{str:PlayerName} forgot to reload timely.");
		}

		const char *apPlayerKilledByMessages[] = {
			"{str:PlayerName} was destroyed by {str:Killer}.",
			"{str:PlayerName} was slain by {str:Killer}.",
			"{str:PlayerName} was decapitated by {str:Killer}.",
			"{str:PlayerName} was chopped up by {str:Killer}.",
			"{str:PlayerName} was removed from this world by {str:Killer}.",
		};

		if(PossibleMessages.IsEmpty())
		{
			for(const char *pMessage : apPlayerKilledByMessages)
			{
				PossibleMessages.Add(pMessage);
			}
		}
		else
		{
			PossibleMessages.Add(apPlayerKilledByMessages[random_int(0, std::size(apPlayerKilledByMessages) - 1)]);
		}

		if(PossibleMessages.Size() > 1)
		{
			for(std::size_t i = 0; i < PossibleMessages.Size(); ++i)
			{
				if(PossibleMessages.At(i) == m_LastUsedKillMessage)
				{
					PossibleMessages.RemoveAt(i);
				}
			}
		}

		const char *pMessage = PossibleMessages.At(random_int(0, PossibleMessages.Size() - 1));
		m_LastUsedKillMessage = pMessage;

		const char *pKillerText = GetClassDisplayNameForKilledBy(KillerClass);
		dbg_assert(pKillerText, "Killer class has no display name");
		if(!pKillerText)
		{
			return;
		}
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER,
			pMessage,
			"PlayerName", GameServer()->Server()->ClientName(Victim),
			"Killer", pKillerText,
			nullptr);
	}
	else
	{
		icArray<const char *, 20> PossibleMessages;

		const icArray<EDamageType, 2> BadTiles = {
			EDamageType::DEATH_TILE,
			EDamageType::INFECTION_TILE,
		};
		if(BadTiles.Contains(Context.DamageType))
		{
			PossibleMessages.Add(("{str:PlayerName} made a wrong step."));
			PossibleMessages.Add(("{str:PlayerName} went where they shouldn't."));
		}

		const char *apPlayerDeathMessages[] = {
			"{str:PlayerName} didn't survive in this round.",
		};

		if(PossibleMessages.IsEmpty())
		{
			for(const char *pMessage : apPlayerDeathMessages)
			{
				PossibleMessages.Add(pMessage);
			}
		}
		else
		{
			PossibleMessages.Add(apPlayerDeathMessages[random_int(0, std::size(apPlayerDeathMessages) - 1)]);
		}

		const char *pMessage = PossibleMessages.At(random_int(0, PossibleMessages.Size() - 1));
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER,
			pMessage,
			"PlayerName", GameServer()->Server()->ClientName(Victim),
			nullptr);
	}
}

int CIcGameController::GetClientIdForNewWitch() const
{
	ClientsArray SuitableInfected;
	ClientsArray SafeInfected;

	for(int ClientId : m_WitchCallers)
	{
		CIcPlayer *pPlayer = GetPlayer(ClientId);
		if(!pPlayer || !pPlayer->IsInGame())
			continue;
		if(pPlayer->GetClass() == EPlayerClass::Witch)
			continue;
		if(!pPlayer->IsInfected())
			continue;

		SuitableInfected.Add(ClientId);

		if(!IsSafeWitchCandidate(ClientId))
			continue;

		SafeInfected.Add(ClientId);
	}

	if(SuitableInfected.IsEmpty())
	{
		// fallback
		for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
		{
			CIcPlayer *pPlayer = GetPlayer(ClientId);
			if(!pPlayer || !pPlayer->IsInGame())
				continue;
			if(pPlayer->GetClass() == EPlayerClass::Witch)
				continue;
			if(!pPlayer->IsInfected())
				continue;

			SuitableInfected.Add(ClientId);

			if(!IsSafeWitchCandidate(ClientId))
				continue;

			SafeInfected.Add(ClientId);
		}
	}

	if(SuitableInfected.IsEmpty())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "witch", "Unable to find any suitable player");
		return -1;
	}

	const ClientsArray &Candidates = SafeInfected.IsEmpty() ? SuitableInfected : SafeInfected;
	int id = random_int(0, Candidates.Size() - 1);
	char aBuf[512];
	/* debug */
	str_format(aBuf, sizeof(aBuf), "going through MAX_CLIENTS=%d, zombie_count=%d, random_int=%d, id=%d", MAX_CLIENTS, static_cast<int>(SuitableInfected.Size()), id, SuitableInfected[id]);
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "witch", aBuf);
	/* /debug */
	CIcPlayer *pPlayer = GetPlayer(Candidates[id]);
	pPlayer->SetClass(EPlayerClass::Witch);
	return Candidates[id];
}

bool CIcGameController::IsSafeWitchCandidate(int ClientId) const
{
	constexpr double MaxInactiveSeconds = 5;
	constexpr double SafeRadius = 1000;

	const CIcPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
		return false;

	if(Server()->Tick() > pPlayer->m_LastActionTick + MaxInactiveSeconds * Server()->TickSpeed())
		return false;

	const CIcCharacter *pCharacter = GetCharacter(ClientId);
	if(pCharacter && pCharacter->IsAlive())
	{
		icArray<CIcCharacter *, MAX_CLIENTS> aCharsNearby;
		int Num = GameServer()->m_World.FindEntities(pCharacter->GetPos(), SafeRadius,
			reinterpret_cast<CEntity**>(aCharsNearby.begin()),
			aCharsNearby.Capacity(),
			CGameWorld::ENTTYPE_CHARACTER);
		aCharsNearby.Resize(Num);

		for(const CIcCharacter *pCharNearby : aCharsNearby)
		{
			if(pCharNearby == pCharacter)
				continue;
			
			if(pCharNearby->IsAlive() && pCharNearby->IsHuman())
			{
				return false;
			}
		}
	}

	return true;
}

CIcGameController::PlayerScore *CIcGameController::GetSurvivalPlayerScore(int ClientId)
{
	for(PlayerScore &Score : m_SurvivalState.Scores)
	{
		if(Score.ClientId == ClientId)
			return &Score;
	}

	return nullptr;
}

CIcGameController::PlayerScore *CIcGameController::EnsureSurvivalPlayerScore(int ClientId)
{
	PlayerScore *pScore = GetSurvivalPlayerScore(ClientId);
	if(pScore)
		return pScore;

	m_SurvivalState.Scores.Add({});
	PlayerScore &Score = m_SurvivalState.Scores.Last();
	Score.ClientId = ClientId;
	Score.aPlayerName[0] = '\0';
	Score.Kills = 0;

	return &Score;
}

void CIcGameController::TickBeforeWorld()
{
	// update core properties important for hook
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CIcCharacter *pCharacter = GetCharacter(i);
		if(pCharacter)
		{
			pCharacter->TickBeforeWorld();
			m_Teams.m_Core.SetProtected(i, pCharacter->GetPlayer()->HookProtectionEnabled());
		}
	}
}
void CIcGameController::Tick()
{
	IGameController::Tick();

	//Check session
	{
		CIcPlayerIterator<PLAYERITER_ALL> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			//Update session
			IServer::CClientSession* pSession = Server()->GetClientSession(Iter.ClientId());
			if(pSession)
			{
				if(!Server()->GetClientMemory(Iter.ClientId(), CLIENTMEMORY_SESSION_PROCESSED))
				{
					//The client already participated to this round,
					//and he exit the game as infected.
					//To avoid cheating, we assign to him the same class again.
					if(pSession->m_RoundId == m_RoundId)
					{
						const EPlayerClass ClassFromSession = static_cast<EPlayerClass>(pSession->m_Class);
						if(IsInfectedClass(ClassFromSession) == IsInfectionStarted())
						{
							Iter.Player()->SetClass(ClassFromSession);
							Iter.Player()->SetInfectionTimestamp(pSession->m_LastInfectionTime);
						}
					}

					Server()->SetClientMemory(Iter.ClientId(), CLIENTMEMORY_SESSION_PROCESSED, true);
				}
				
				pSession->m_Class = static_cast<int>(Iter.Player()->GetClass());
				pSession->m_RoundId = GameServer()->m_pController->GetRoundId();
				pSession->m_LastInfectionTime = Iter.Player()->GetInfectionTimestamp();
			}
		}
	}

	CheckRoundFailed();

	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	const int NumPlayers = NumHumans + NumInfected;

	bool Allowed = !m_Warmup;
	m_InfectedStarted = false;

	//If the game can start ...
	if(Allowed && m_GameOverTick == -1 && NumPlayers >= GetMinPlayers())
	{
		//If the infection started
		if(IsInfectionStarted())
		{
			m_InfectedStarted = true;
			RoundTickAfterInitialInfection();
		}
		else
		{
			RoundTickBeforeInitialInfection();
		}

		DoWincheck();
		if(m_FinalExplosionState == EFinalExplosionState::Started)
		{
			ProgressFinalExplosion();
		}
	}
	else
	{
		m_RoundStartTick = Server()->Tick();
	}

	if(GameWorld()->m_Paused)
	{
		m_HeroGiftTick++;
	}
	else
	{
		UpdateNinjaTargets();
		HandleLastHookers();
	}

	if(m_SuggestMoreRounds && !GameServer()->HasActiveVote())
	{
		const char pDescription[] = _("Play more on this map");
		char aCommandBuffer[256];
		str_format(aCommandBuffer, sizeof(aCommandBuffer), "adjust sv_rounds_per_map +%d", Config()->m_SvSuggestMoreRounds);
		const char pReason[] = _("The last round");

		GameServer()->StartVote(pDescription, aCommandBuffer, pReason);

		m_SuggestMoreRounds = false;
		m_MoreRoundsSuggested = true;
	}

	if(NumPlayers)
		SendHintMessage();
}

void CIcGameController::RoundTickBeforeInitialInfection()
{
	BroadcastInfectionComing(GetInfectionStartTick());
}

void CIcGameController::RoundTickAfterInitialInfection()
{
	bool StartInfectionTrigger = GetInfectionStartTick() == Server()->Tick();

	if(StartInfectionTrigger)
		OnInfectionTriggered();

	// Ensure that the newly joined players have correct state/class
	CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		CIcPlayer *pPlayer = Iter.Player();
		if(pPlayer->GetClass() == EPlayerClass::None)
		{
			pPlayer->KillCharacter(); // Infect the player
			pPlayer->StartInfection();
			pPlayer->m_DieTick = m_RoundStartTick;
		}
	}

	if(!StartInfectionTrigger)
		DoTeamBalance();

	UpdateBalanceFactors();
}

void CIcGameController::PreparePlayerToJoin(CIcPlayer *pPlayer)
{
	if(IsInfectionStarted())
	{
		if (!pPlayer->IsInfected())
		{
			EPlayerClass c = ChooseInfectedClass(pPlayer);
			pPlayer->SetClass(c);
		}
	}
}

void CIcGameController::SetPlayerPickedTimestamp(CIcPlayer *pPlayer, int Timestamp) const
{
	const int PrevInfectionTimestamp = pPlayer->GetInfectionTimestamp();
	pPlayer->SetInfectionTimestamp(Timestamp);

	if(PrevInfectionTimestamp && Timestamp > PrevInfectionTimestamp)
	{
		int PrevInfectionSeconds = Timestamp - PrevInfectionTimestamp;
		dbg_msg("server", "SetPlayerPickedTimestamp: Pick cid=%d (previously picked %d seconds ago)", pPlayer->GetCid(), PrevInfectionSeconds);
	}
	else
	{
		dbg_msg("server", "SetPlayerPickedTimestamp: Pick cid=%d (was not picked before)", pPlayer->GetCid());
	}
}

uint32_t CIcGameController::InfectHumans(uint32_t NumHumansToInfect)
{
	if(NumHumansToInfect == 0)
		return 0;

	CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	ClientsArray FairCandidates;
	ClientsArray UnfairCandidates;

	icArray<CIcPlayer *, MAX_CLIENTS> Humans;

	while(Iter.Next())
	{
		CIcPlayer *pPlayer = Iter.Player();
		if(pPlayer->IsHuman())
		{
			Humans.Add(pPlayer);
		}
	}

	if(NumHumansToInfect > Humans.Size())
	{
		// Makes no sense, must be a testing game
		return 0;
	}

	const auto Sorter = [](const CIcPlayer *p1, const CIcPlayer *p2) -> bool {
		return p1->GetInfectionTimestamp() < p2->GetInfectionTimestamp();
	};

	std::stable_sort(Humans.begin(), Humans.end(), Sorter);

	int Timestamp = time_timestamp();

	uint32_t NewInfected = 0;
	for(CIcPlayer *pPlayer : Humans)
	{
		pPlayer->KillCharacter(); // Infect the player
		pPlayer->StartInfection();
		pPlayer->m_DieTick = m_RoundStartTick;
		NewInfected++;

		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION,
			_("{str:VictimName} has been infected"),
				"VictimName", Server()->ClientName(pPlayer->GetCid()),
				nullptr);

		SetPlayerPickedTimestamp(pPlayer, Timestamp);
		if(NewInfected >= NumHumansToInfect)
		{
			break;
		}
	}

	return NewInfected;
}

void CIcGameController::ForcePlayersBalance(uint32_t PlayersToBalance)
{
	// Force balance
	InfectHumans(PlayersToBalance);
	GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION,
		_("Someone was infected to balance the game."), nullptr);
}

void CIcGameController::UpdateBalanceFactors()
{
	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	if(NumInfected == 1)
	{
		m_InfBalanceBoostFactor = 1;
		return;
	}

	const int NumPlayers = NumHumans + NumInfected;
	const int NumMinimumInfected = GetMinimumInfectedForPlayers(NumPlayers);
	const int PlayersToInfect = maximum<int>(0, NumMinimumInfected - NumInfected);
	if(PlayersToInfect >= 2)
	{
		// ToInfect / MinInfected
		// 2 / 3 = 0.66 // Could be 66% bonus but 'num infected = 1' already handled and gives 100%
		// 2 / 4 = 0.5  // If min infected = 4 and there are two infected, they'll have 50% bonus (5 extra armor)
		// 2 / 5 = 0.4  // ...
		m_InfBalanceBoostFactor = static_cast<float>(PlayersToInfect) / NumMinimumInfected;
		return;
	}

	m_InfBalanceBoostFactor = 0;
}

void CIcGameController::MaybeSendStatistics()
{
	// skip some maps that are not very fair
	if (
			str_comp(g_Config.m_SvMap, "infc_toilet") == 0 ||
			str_comp(g_Config.m_SvMap, "infc_toilet_old") == 0) {
		return;
	}

	if (Server()->GetActivePlayerCount() < 6) {
		return;
	}

	if(GetRoundType() != ERoundType::Normal)
		return;

	Server()->SendStatistics();
}

void CIcGameController::CancelTheRound(ROUND_CANCELATION_REASON Reason)
{
	switch(Reason)
	{
	case ROUND_CANCELATION_REASON::INVALID:
		return;
	case ROUND_CANCELATION_REASON::ALL_INFECTED_LEFT_THE_GAME:
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTED, _("The round canceled: All infected have left the game"), "RoundDuration", nullptr);
		break;
	case ROUND_CANCELATION_REASON::EVERYONE_INFECTED_BY_THE_GAME:
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTED, _("The round canceled: All humans have left the game"), nullptr);
		break;
	}

	EndRound(ERoundEndReason::CANCELED);
}

void CIcGameController::AnnounceTheWinner(int NumHumans)
{
	if(NumHumans)
	{
		GameServer()->SendChatTarget_Localization_P(-1, CHATCATEGORY_HUMANS, NumHumans,
			_P("One human won the round",
			   "{int:NumHumans} humans won the round", NumHumans),
			"NumHumans", &NumHumans,
			nullptr);

		char aBuf[256];
		CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			if(Iter.Player()->IsHuman())
			{
				//TAG_SCORE
				Server()->RoundStatistics()->OnScoreEvent(Iter.ClientId(), EScoreEvent::HUMAN_SURVIVE, Iter.Player()->GetClass(), Server()->ClientName(Iter.ClientId()), Console());
				Server()->RoundStatistics()->SetPlayerAsWinner(Iter.ClientId());
				GameServer()->SendScoreSound(Iter.ClientId());

				GameServer()->SendChatTarget_Localization(Iter.ClientId(), CHATCATEGORY_SCORE, _("You have survived, +5 points"), NULL);
				str_format(aBuf, sizeof(aBuf), "survived player='%s'", Server()->ClientName(Iter.ClientId()));
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
			}
		}
	}
	else
	{
		const int Seconds = (Server()->Tick() - m_RoundStartTick) / ((float)Server()->TickSpeed());
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTED, _("Infected won the round in {sec:RoundDuration}"), "RoundDuration", &Seconds, NULL);
	}

	EndRound(ERoundEndReason::FINISHED);
}

void CIcGameController::BroadcastInfectionComing(int InfectionTick)
{
	if(InfectionTick <= Server()->Tick())
		return;

	int Seconds = (InfectionTick - Server()->Tick()) / Server()->TickSpeed() + 1;
	EBroadcastPriority Priority = Seconds <= 3 ? EBroadcastPriority::GAMEANNOUNCE : EBroadcastPriority::LOWEST;
	GameServer()->SendBroadcast_Localization(-1,
		Priority,
		BROADCAST_DURATION_REALTIME,
		_("Infection is coming in {sec:RemainingTime}"),
		"RemainingTime", &Seconds,
		nullptr);
}

void CIcGameController::MaybeDropPickup(CIcCharacter *pVictim)
{
	const int DropMaxLevel = pVictim->GetDropLevel();
	if(DropMaxLevel <= 0)
		return;

	pVictim->SetDropLevel(0);
	const vec2 Pos = pVictim->GetPos();
	const int ZoneIndex = GetDamageZoneValueAt(Pos);

	icArray<int, 4> BadIndices = {
		ZONE_DAMAGE_DEATH,
		ZONE_DAMAGE_DEATH_NOUNDEAD,
		// ZONE_DAMAGE_DAMAGE,
		// ZONE_DAMAGE_DAMAGE_HUMANS,
	};

	if(pVictim->IsInfected())
	{
		// An infected would drop a pickup for humans
		// but humans won't be able to get it from infected zone
		BadIndices.Add(ZONE_DAMAGE_INFECTION);
	}

	if(BadIndices.Contains(ZoneIndex))
	{
		// No drop if noone will be able to pick it up
		return;
	}

	ClientsArray HasSpawnedPickups;
	for(TEntityPtr<CIcPickup> pPickup = GameWorld()->FindFirst<CIcPickup>(); pPickup; ++pPickup)
	{
		int Owner = pPickup->GetOwner();
		if(Owner >= 0 && !HasSpawnedPickups.Contains(Owner))
		{
			HasSpawnedPickups.Add(Owner);
		}
	}

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ++ClientId)
	{
		const CIcCharacter *pCharacter = GetCharacter(ClientId);
		if(!pCharacter)
			continue;

		if(pCharacter->IsHuman() == pVictim->IsHuman())
		{
			// No drop from teammates
			continue;
		}

		const CIcPlayerClass *pClass = pCharacter->GetClass();
		if(pClass->GetUpgradeLevel() >= DropMaxLevel)
		{
			// The player already has an upgrade of this level
			continue;
		}

		SClassUpgrade Upgrade = pClass->GetNextUpgrade();
		if(!Upgrade.IsValid())
			continue;

		if(HasSpawnedPickups.Contains(ClientId))
			continue;

		CIcPickup *p = new CIcPickup(GameServer(), EICPickupType::ClassUpgrade, Pos, ClientId);
		p->SetUpgrade(Upgrade);
		p->Spawn();
	}
}

bool CIcGameController::IsInfectionStarted() const
{
	if(Config()->m_InfTrainingMode)
		return false;

	return GetInfectionStartTick() <= Server()->Tick();
}

bool CIcGameController::MapRotationEnabled() const
{
	return true;
}

void CIcGameController::OnTeamChangeRequested(int ClientId, int Team)
{
	CPlayer *pPlayer = GetPlayer(ClientId);
	// Switch team on given client and kill/respawn him
	if(CanJoinTeam(Team, ClientId))
	{
		if(CanChangeTeam(pPlayer, Team))
		{
			pPlayer->m_LastSetTeam = Server()->Tick();
			if(pPlayer->GetTeam() == TEAM_SPECTATORS || Team == TEAM_SPECTATORS)
				GameServer()->RequestVotesUpdate();
			DoTeamChange(pPlayer, Team);
			pPlayer->m_TeamChangeTick = Server()->Tick();
		}
		else
			GameServer()->SendBroadcast(ClientId, "Teams must be balanced, please join other team", EBroadcastPriority::GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE);
	}
}

bool CIcGameController::CanJoinTeam(int Team, int ClientId)
{
	if(Team != TEAM_SPECTATORS)
	{
		if(GetRoundType() == ERoundType::Survival && IsInfectionStarted())
		{
			GameServer()->SendBroadcast_Localization(ClientId,
				EBroadcastPriority::GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE,
				_("You have to wait until the survival is over"));
			return false;
		}

		return IGameController::CanJoinTeam(Team, ClientId);
	}

	if (IsGameOver())
		return true;

	CIcPlayer *pPlayer = GetPlayer(ClientId);

	if(!pPlayer) // Invalid call
		return false;

	if (pPlayer->IsHuman())
		return true;

	int NumHumans;
	int NumInfected;
	GetPlayerCounter(ClientId, NumHumans, NumInfected);
	const int NumPlayers = NumHumans + NumInfected;
	const int NumMinInfected = GetMinimumInfectedForPlayers(NumPlayers);
	if(NumInfected >= NumMinInfected)
	{
		// Let the ClientId join the specs if we'll not have to infect
		// someone after the join.
		return true;
	}

	return false;
}

bool CIcGameController::AreTurretsEnabled() const
{
	if(!Config()->m_InfTurretEnable)
		return false;

	if(GetRoundType() == ERoundType::Survival)
		return true;

	return Server()->GetActivePlayerCount() >= static_cast<uint32_t>(Config()->m_InfMinPlayersForTurrets);
}

int CIcGameController::InfTurretDuration() const
{
	if(GetRoundType() == ERoundType::Survival)
		return 20;

	return Config()->m_InfTurretDuration;
}

bool CIcGameController::MercBombsEnabled() const
{
	return GetRoundType() != ERoundType::Fun;
}

bool CIcGameController::WhiteHoleEnabled() const
{
	if(GetRoundType() == ERoundType::Survival)
		return true;

	if(GetRoundType() == ERoundType::Fast)
		return false;

	if(Server()->GetActivePlayerCount() < static_cast<uint32_t>(Config()->m_InfMinPlayersForWhiteHole))
		return false;

	return Config()->m_InfWhiteHoleProbability > 0;
}

float CIcGameController::GetWhiteHoleLifeSpan() const
{
	if(GetRoundType() == ERoundType::Survival)
		return 15;

	return Config()->m_InfWhiteHoleLifeSpan;
}

int CIcGameController::MinimumInfectedForRevival() const
{
	return Config()->m_InfRevivalMinInfected;
}

bool CIcGameController::IsClassChooserEnabled() const
{
	return Config()->m_InfClassChooser && Server()->GetTimeShiftUnit();
}

int CIcGameController::GetTaxiMode() const
{
	if(GetRoundType() == ERoundType::Survival)
		return 0;

	return Config()->m_InfTaxi;
}

int CIcGameController::GetGhoulStomackSize() const
{
	if(GetRoundType() == ERoundType::Fun)
		return Config()->m_FunRoundGhoulStomachSize;

	return Config()->m_InfGhoulStomachSize;
}

EPlayerScoreMode CIcGameController::GetPlayerScoreMode(int SnappingClient) const
{
	if(IsGameOver())
	{
		// game over.. wait for restart
		if(Server()->Tick() <= m_GameOverTick + Server()->TickSpeed() * Config()->m_InfShowScoreTime)
		{
			if((Server()->Tick() - m_GameOverTick) > Server()->TickSpeed() * (Config()->m_InfShowScoreTime / 2.0f))
			{
				return EPlayerScoreMode::Time;
			}

			return EPlayerScoreMode::Class;
		}
	}

	const CIcPlayer *pSnapPlayer = GetPlayer(SnappingClient);
	if(pSnapPlayer)
	{
		return pSnapPlayer->GetScoreMode();
	}

	return EPlayerScoreMode::Class;
}

float CIcGameController::GetTimeLimitMinutes() const
{
	if(Config()->m_InfTrainingMode)
		return 0;

	if(m_RoundTimeLimitSeconds.has_value())
		return m_RoundTimeLimitSeconds.value() / 60.0;

	float BaseTimeLimit = Config()->m_SvTimelimitInSeconds ? Config()->m_SvTimelimitInSeconds / 60.0 : Config()->m_SvTimelimit;

	switch(GetRoundType())
	{
	case ERoundType::Fun:
		return minimum<float>(BaseTimeLimit, Config()->m_FunRoundDuration);
	case ERoundType::Fast:
		return clamp<float>(BaseTimeLimit * 0.5, 1, 3);
	default:
		return BaseTimeLimit;
	}
}

int CIcGameController::GetTimeLimitSeconds() const
{
	return GetTimeLimitMinutes() * 60;
}

void CIcGameController::SetTimeLimitSeconds(float Seconds)
{
	m_RoundTimeLimitSeconds = Seconds;
}

int CIcGameController::GetInfectionDelay() const
{
	return m_RoundInfectionDelaySeconds.value_or(Config()->m_InfInitialInfectionDelay);
}

void CIcGameController::SetInfectionDelay(int Seconds)
{
	m_RoundInfectionDelaySeconds = Seconds;
}

bool CIcGameController::HeroGiftAvailable() const
{
	return Server()->Tick() >= m_HeroGiftTick;
}

std::optional<vec2> CIcGameController::GetHeroFlagPosition() const
{
	int NbPos = m_HeroFlagPositions.size();
	if(NbPos == 0)
		return std::nullopt;

	for(int Attempts = 3; Attempts > 0; Attempts--)
	{
		int Index = random_int(0, NbPos - 1);
		const vec2 Pos = m_HeroFlagPositions[Index];
		if(IsPositionAvailableForHumans(Pos))
		{
			return Pos;
		}
	}

	return std::nullopt;
}

bool CIcGameController::IsPositionAvailableForHumans(const vec2 &Position) const
{
	int DamageZoneValue = GetDamageZoneValueAt(Position);
	switch(DamageZoneValue)
	{
	case ZONE_DAMAGE_INFECTION:
	case ZONE_DAMAGE_DEATH_NOUNDEAD:
	case ZONE_DAMAGE_DAMAGE:
	case ZONE_DAMAGE_DAMAGE_HUMANS:
		return false;
	default:
		return true;
	}
}

void CIcGameController::StartFunRound()
{
	if(m_FunRoundConfigurations.empty())
		return;

	const auto &Configs = m_FunRoundConfigurations;
	const int type = random_int(0, Configs.size() - 1);
	const FunRoundConfiguration &Configuration = Configs[type];

	const char *pTitle = Config()->m_FunRoundTitle;
	char aBuf[256];

	std::vector<const char *> phrases = {
		", glhf!",
		", not ez!",
		" c:",
		" xd",
		", that's gg",
		", good luck!"};
	const char *random_phrase = phrases[random_int(0, phrases.size() - 1)];
	m_FunRoundConfiguration = Configuration;

	const char *HumanClassText = CIcGameController::GetClassPluralDisplayName(Configuration.HumanClass);
	const char *InfectedClassText = CIcGameController::GetClassPluralDisplayName(Configuration.InfectedClass);

	str_format(aBuf, sizeof(aBuf), "%s! %s vs %s%s", pTitle, InfectedClassText, HumanClassText, random_phrase);

	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
	GameServer()->SendChatTarget(-1, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CIcPlayer *pPlayer = GetPlayer(i);
		if(!pPlayer)
			continue;

		pPlayer->SetClass(Configuration.HumanClass);
	}
}

void CIcGameController::EndFunRound()
{
	m_FunRoundsPassed++;
}

void CIcGameController::StartSurvivalRound()
{
	GameServer()->m_World.m_ResetRequested = false;

	char aBuf[256];

	int WaveDisplayNumber = m_SurvivalState.Wave + 1;
	if(m_SurvivalWaves.Size() <= m_SurvivalState.Wave)
	{
		str_format(aBuf, sizeof(aBuf), "Unable to start a survival round: wave %d is not configured", WaveDisplayNumber);
		GameServer()->SendChatTarget(-1, aBuf);
		return;
	}

	// TODO: Check this (not) incremented
	if(m_SurvivalState.Wave == 0)
	{
		StartSurvivalGame();
	}

	for(int CID : m_SurvivalState.KilledPlayers)
	{
		CIcPlayer *pPlayer = GetPlayer(CID);
		if(pPlayer->IsSpectator())
		{
			DoTeamChange(pPlayer, TEAM_RED, false);
		}
	}

	m_SurvivalState.KilledPlayers.Clear();

	if(m_SurvivalWaves.Size() == 1)
	{
		str_format(aBuf, sizeof(aBuf), "The survival begins. Enjoy!");
	}
	else
	{
		const SurvivalWaveConfiguration &WaveConf = m_SurvivalWaves.At(m_SurvivalState.Wave);

		if(WaveConf.aName[0])
		{
			str_format(aBuf, sizeof(aBuf), "Wave %d: %s", WaveDisplayNumber, WaveConf.aName);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "Wave %d. Enjoy!", WaveDisplayNumber);
		}
	}
	GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	SetRoundMinimumPlayers(1);
	SetRoundMinimumInfected(0);
}

void CIcGameController::EndSurvivalRound()
{
	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	bool IsOver = false;

	if((NumHumans == 0) && (NumInfected > 0))
	{
		if(m_SurvivalState.Wave == 0)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE,
				_("The survival is over. You have failed to survive a single wave."));
		}
		else
		{
			int NumWaves = m_SurvivalState.Wave + 1;
			GameServer()->SendChatTarget_Localization_P(-1, CHATCATEGORY_SCORE, NumWaves,
				_P(
					"The survival is over after {int:NumWaves} wave.",
					"The survival is over after {int:NumWaves} waves.", NumWaves),
					"NumWaves", &NumWaves,
					nullptr
					);
		}

		IsOver = true;
	}

	if(m_SurvivalWaves.Size() == m_SurvivalState.Wave + 1)
	{
		if(NumHumans)
		{
			if(m_SurvivalWaves.Size() > 2)
			{
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE,
					_("The survival is over. You have survived!"));
			}
			else
			{
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_SCORE,
					_("The survival is over. You have survived the final wave!"));
			}
		}

		IsOver = true;
	}

	if(IsOver)
	{
		EndSurvivalGame();
		return;
	}

	m_SurvivalState.SurvivedPlayers.Clear();

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CIcCharacter *pCharacter = GetCharacter(i);
		if(pCharacter && pCharacter->IsHuman())
		{
			m_SurvivalState.SurvivedPlayers.Add(i);
		}
	}

	QueueRoundType(ERoundType::Survival);
}

void CIcGameController::EnsureFinalExplosionIsStarted()
{
	if(m_FinalExplosionState == EFinalExplosionState::NotStarted)
	{
		StartFinalExplosion();
	}
}

void CIcGameController::StartFinalExplosion()
{
	dbg_assert(m_FinalExplosionState == EFinalExplosionState::NotStarted, "Invalid final explosion start");
	for(TEntityPtr<CIcCharacter> p = GameWorld()->FindFirst<CIcCharacter>(); p; ++p)
	{
		if(p->IsInfected())
		{
			GameServer()->SendEmoticon(p->GetCid(), EMOTICON_GHOST);
		}
		else
		{
			GameServer()->SendEmoticon(p->GetCid(), EMOTICON_EYES);
		}
	}

	m_FinalExplosionState = EFinalExplosionState::Started;
}

void CIcGameController::ProgressFinalExplosion()
{
	if (m_FinalExplosionState != EFinalExplosionState::Started)
	{
		return;
	}

	bool NewExplosion = false;

	for(int j = 0; j < m_MapHeight; j++)
	{
		for(int i = 0; i < m_MapWidth; i++)
		{
			if((m_GrowingMap[j * m_MapWidth + i] & 1) && ((i > 0 && m_GrowingMap[j * m_MapWidth + i - 1] & 2) ||
															 (i < m_MapWidth - 1 && m_GrowingMap[j * m_MapWidth + i + 1] & 2) ||
															 (j > 0 && m_GrowingMap[(j - 1) * m_MapWidth + i] & 2) ||
															 (j < m_MapHeight - 1 && m_GrowingMap[(j + 1) * m_MapWidth + i] & 2)))
			{
				NewExplosion = true;
				m_GrowingMap[j * m_MapWidth + i] |= 8;
				m_GrowingMap[j * m_MapWidth + i] &= ~1;
				if(random_prob(0.1f))
				{
					vec2 TilePos = vec2(16.0f, 16.0f) + vec2(i * 32.0f, j * 32.0f);
					static const int Damage = 0;
					CreateExplosion(TilePos, -1, EDamageType::NO_DAMAGE, Damage);
					GameServer()->CreateSound(TilePos, SOUND_GRENADE_EXPLODE);
				}
			}
		}
	}

	for(int j = 0; j < m_MapHeight; j++)
	{
		for(int i = 0; i < m_MapWidth; i++)
		{
			if(m_GrowingMap[j * m_MapWidth + i] & 8)
			{
				m_GrowingMap[j * m_MapWidth + i] &= ~8;
				m_GrowingMap[j * m_MapWidth + i] |= 2;
			}
		}
	}

	for(TEntityPtr<CIcCharacter> p = GameWorld()->FindFirst<CIcCharacter>(); p; ++p)
	{
		if(p->IsHuman())
			continue;

		int tileX = static_cast<int>(round(p->m_Pos.x)) / 32;
		int tileY = static_cast<int>(round(p->m_Pos.y)) / 32;

		if(tileX < 0)
			tileX = 0;
		if(tileX >= m_MapWidth)
			tileX = m_MapWidth - 1;
		if(tileY < 0)
			tileY = 0;
		if(tileY >= m_MapHeight)
			tileY = m_MapHeight - 1;

		if(m_GrowingMap[tileY * m_MapWidth + tileX] & 2 && p->GetPlayer())
		{
			p->Die(p->GetCid(), EDamageType::GAME_FINAL_EXPLOSION);
		}
	}

	if(!NewExplosion)
	{
		m_FinalExplosionState = EFinalExplosionState::Finished;
	}
}

void CIcGameController::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = Server()->SnapNewItem<CNetObj_GameInfo>(0);
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = GameFlags_ClampToSix(m_GameFlags);
	pGameInfoObj->m_GameStateFlags = 0;
	if(m_GameOverTick != -1)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	if(m_SuddenDeath)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
	if(GameWorld()->m_Paused)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
	pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
	pGameInfoObj->m_WarmupTimer = m_Warmup;

	pGameInfoObj->m_ScoreLimit = Config()->m_SvScorelimit;

	int WholeMinutes = GetTimeLimitMinutes();
	float FractionalPart = GetTimeLimitMinutes() - WholeMinutes;

	pGameInfoObj->m_TimeLimit = WholeMinutes + (FractionalPart ? 1 : 0);
	if(FractionalPart)
	{
		pGameInfoObj->m_RoundStartTick -= (1 - FractionalPart) * 60 * Server()->TickSpeed();
	}

	pGameInfoObj->m_RoundNum = (str_length(Config()->m_SvMaprotation) && Config()->m_SvRoundsPerMap) ? Config()->m_SvRoundsPerMap : 0;
	pGameInfoObj->m_RoundCurrent = m_RoundCount+1;

	SnapMapMenu(SnappingClient, pGameInfoObj);

	CNetObj_InfClassGameInfo *pInfclassGameInfoObj = Server()->SnapNewItem<CNetObj_InfClassGameInfo>(0);
	pInfclassGameInfoObj->m_Version = 2;
	pInfclassGameInfoObj->m_Flags = 0;
	pInfclassGameInfoObj->m_TimeLimitInSeconds = GetTimeLimitSeconds();
	pInfclassGameInfoObj->m_HeroGiftTick = m_HeroGiftTick;

	int InfClassVersion = Server()->GetClientInfclassVersion(SnappingClient);
	if((InfClassVersion == 0) || (InfClassVersion > VERSION_INFC_160))
	{
		CNetObj_GameInfoEx *pGameInfoEx = Server()->SnapNewItem<CNetObj_GameInfoEx>(0);
		if(!pGameInfoEx)
			return;

		pGameInfoEx->m_Flags = GAMEINFOFLAG_PREDICT_VANILLA | GAMEINFOFLAG_DONT_MASK_ENTITIES;

		if(InfClassVersion == 0)
		{
			// We use DDRace entities to show infection and other custom infclass tiles (see CClientGameTileGetter::GetClientGameTileIndex)
			pGameInfoEx->m_Flags |= GAMEINFOFLAG_ENTITIES_DDNET;
		}

		pGameInfoEx->m_Flags2 = m_GameInfoFlags2 | GAMEINFOFLAG2_HUD_DDRACE;
		pGameInfoEx->m_Flags2 |= GAMEINFOFLAG2_NO_WEAK_HOOK;
		pGameInfoEx->m_Flags2 |= GAMEINFOFLAG2_NO_SKIN_CHANGE_FOR_FROZEN;
		pGameInfoEx->m_Version = GAMEINFO_CURVERSION;
	}

	CNetObj_GameData *pGameDataObj = Server()->SnapNewItem<CNetObj_GameData>(0);
	if(!pGameDataObj)
		return;

	pGameDataObj->m_FlagCarrierRed = FLAG_ATSTAND;
	pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
}

CPlayer *CIcGameController::CreatePlayer(int ClientId, bool IsSpectator, void *pData)
{
	CIcPlayer *pPlayer = nullptr;
	if(IsSpectator)
	{
		pPlayer = new(ClientId) CIcPlayer(this, GetNextClientUniqueId(), ClientId, TEAM_SPECTATORS);
	}
	else
	{
		const int StartTeam = Config()->m_SvTournamentMode ? TEAM_SPECTATORS : GetAutoTeam(ClientId);
		pPlayer = new(ClientId) CIcPlayer(this, GetNextClientUniqueId(), ClientId, StartTeam);
	}

	if(pData)
	{
		InfclassPlayerPersistantData *pPersistent = static_cast<InfclassPlayerPersistantData *>(pData);
		pPlayer->SetPreferredClass(pPersistent->m_PreferredClass);
		pPlayer->SetPreviouslyPickedClass(pPersistent->m_PreviouslyPickedClass);
		pPlayer->SetScoreMode(pPersistent->m_ScoreMode);
		pPlayer->SetAntiPingEnabled(pPersistent->m_AntiPing);
		pPlayer->SetInfectionTimestamp(pPersistent->m_LastInfectionTime);
	}

	PreparePlayerToJoin(pPlayer);

	return pPlayer;
}

int CIcGameController::PersistentClientDataSize() const
{
	return sizeof(InfclassPlayerPersistantData);
}

bool CIcGameController::GetClientPersistentData(int ClientId, void *pData) const
{
	const CIcPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
		return false;

	InfclassPlayerPersistantData *pPersistent = static_cast<InfclassPlayerPersistantData *>(pData);
	pPersistent->m_ScoreMode = pPlayer->GetScoreMode();
	pPersistent->m_PreferredClass = pPlayer->GetPreferredClass();
	pPersistent->m_PreviouslyPickedClass = pPlayer->GetPreviouslyPickedClass();
	pPersistent->m_AntiPing = pPlayer->GetAntiPingEnabled();
	pPersistent->m_LastInfectionTime = pPlayer->GetInfectionTimestamp();
	return true;
}

void CIcGameController::GetHelpText(dynamic_string *pBuffer, int ClientId, const char *pHelpPage) const
{
	dynamic_string &Buffer = *pBuffer;
	const char *pLanguage = GetPlayer(ClientId)->GetLanguage();

	if(!pHelpPage || str_comp_nocase(pHelpPage, "game") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Rules of the game"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("InfectionClass is a team game between humans and the infected."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("All players start as a human."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("10 seconds later, a few players become infected."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("The goal for the humans is to survive until the army cleans the map."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("The goal for the infected is to infect all humans."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("See also `/help pages`"), NULL);
	}
	else if(str_comp_nocase(pHelpPage, "translate") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("How to translate the mod"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Create an account on Crowdin and join the translation team:"), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, Config()->m_AboutTranslationUrl, NULL);
	}
	else if(str_comp_nocase(pHelpPage, "whitehole") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("White hole"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _C("White hole", "White hole pulls the infected into its center."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_LP(Buffer, pLanguage, g_Config.m_InfWhiteHoleMinimalKills,
			_CP("White hole",
				"Receive it by killing at least {int:NumKills} infected as a Scientist.",
				"Receive it by killing at least {int:NumKills} of the infected as a Scientist.", g_Config.m_InfWhiteHoleMinimalKills),
			"NumKills", &g_Config.m_InfWhiteHoleMinimalKills, nullptr);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _C("White hole", "Use the laser rifle to place it."), NULL);
	}
	else if(str_comp_nocase(pHelpPage, "msg") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Targeted chat messages"));
		Buffer.append(" ~~\n\n");
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/w <PlayerName> <My Message>” to send a private message to this player."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg !<ClassName> <My Message>” to send a private message to all players with a specific class."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Example: “/msg !medic I'm wounded!”"), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/msg !near” to send a private message to all players near you."), NULL);
	}
	else if(str_comp_nocase(pHelpPage, "mute") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Persistent player mute"));
		Buffer.append(" ~~\n\n");
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Use “/mute <PlayerName>” to mute this player."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Unlike a client mute this will persist between map changes and wears off when either you or the muted player disconnects."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Example: “/mute nameless tee”"), NULL);
		Buffer.append("\n\n");
	}
	else if(str_comp_nocase(pHelpPage, "taxi") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("How to use taxi mode"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Two or more humans can form a taxi."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("In order to use it, both humans have to disable hook protection (usually, with F3). The human being hooked becomes the driver."), NULL);
		Buffer.append("\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("To get off the taxi, jump. To drop off your passengers, enable hook protection (usually, with F3)."), NULL);
	}
	else if(str_comp_nocase(pHelpPage, "fast_round") == 0)
	{
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, _("Fast round"), NULL);
		Buffer.append(" ~~\n\n");
		Server()->Localization()->Format_L(Buffer, pLanguage,
			_("In the fast rounds *more* humans become infected initially, "
			  "the spawning rate is increased and the round time limit is decreased. "
			  "White hole is also disabled."),
			NULL);
	}
	else
	{
		bool Ok = true;
		EPlayerClass PlayerClass = CIcGameController::GetClassByName(pHelpPage, &Ok);
		if(Ok)
		{
			GetClassHelpPage(&Buffer, pLanguage, PlayerClass);
		}
	}
}

bool CIcGameController::GetClassHelpPage(dynamic_string *pOutput, const char *pLanguage, EPlayerClass PlayerClass) const
{
	dynamic_string &Buffer = *pOutput;

	auto MakeHeader = [this, &Buffer, pLanguage](const char *pText) {
		Buffer.append("~~ ");
		Server()->Localization()->Format_L(Buffer, pLanguage, pText, nullptr);
		Buffer.append(" ~~");
	};

	auto AddText = [this, &Buffer, pLanguage](const char *pSeparator, const char *pText, const char *pArgName = nullptr, const void *pArgValue = nullptr) {
		Buffer.append(pSeparator);

		if(pArgName && pArgValue)
		{
			Server()->Localization()->Format_L(Buffer, pLanguage, pText, pArgName, pArgValue, nullptr);
		}
		else
		{
			Server()->Localization()->Format_L(Buffer, pLanguage, pText, nullptr);
		}
	};

	auto AddText_Plural = [this, &Buffer, pLanguage](const char *pSeparator, int Number, const char *pText, const char *pArgName, const void *pArgValue) {
		Buffer.append(pSeparator);

		Server()->Localization()->Format_LP(Buffer, pLanguage, Number, pText, pArgName, pArgValue, nullptr);
	};

	auto AddLine = [AddText](const char *pText, const char *pArgName = nullptr, const void *pArgValue = nullptr) {
		AddText("\n\n", pText, pArgName, pArgValue);
	};

	auto AddLine_Plural = [AddText_Plural](int Number, const char *pText, const char *pArgName, const void *pArgValue) {
		AddText_Plural("\n\n", Number, pText, pArgName, pArgValue);
	};

	auto ConLine = [AddText](const char *pText, const char *pArgName = nullptr, const void *pArgValue = nullptr) {
		AddText(" ", pText, pArgName, pArgValue);
	};

	static const int HeroNumArmorGift = 4;

	MakeHeader(CIcGameController::GetClassDisplayName(PlayerClass));

	switch(PlayerClass)
	{
	case EPlayerClass::Invalid:
	case EPlayerClass::None:
	case EPlayerClass::Count:
		return false;

	case EPlayerClass::Mercenary:
		AddLine(_C("Mercenary", "The Mercenary can fly in the air using their machine gun."));
		AddLine(_C("Mercenary", "They can also create a powerful bomb with their hammer that can"
								" be charged by hitting it or with a laser rifle."));
		AddLine_Plural(g_Config.m_InfPoisonDamage,
			_CP("Mercenary",
				"Mercenary can also throw poison grenades that deal {int:NumDamagePoints} damage point"
				" and prevent the infected from healing.",
				"Mercenary can also throw poison grenades that deal {int:NumDamagePoints} damage points"
				" and prevent the infected from healing.",
				g_Config.m_InfPoisonDamage),
			"NumDamagePoints", &g_Config.m_InfPoisonDamage);
		break;
	case EPlayerClass::Medic:
		AddLine(_C("Medic", "The Medic can protect humans with the hammer by giving them armor."));
		AddLine(_C("Medic", "Grenades with medicine give armor to everybody in their range,"
							" including Heroes and the Medic themself."));
		if(g_Config.m_InfEnableTranquilizerRifle)
		{
			int Duration = g_Config.m_InfTranquilizerDose;
			AddLine(_C("Medic", "Tranqulizer rifle makes the infected fall asleep for {sec:Duration}."), "Duration", &Duration);
		}
		else
		{
			AddLine(_C("Medic", "Laser rifle revives the infected, but at the cost of 17 hp and armor."));
		}
		AddLine(_C("Medic", "Medic also has a powerful shotgun that can knock back the infected."));
		break;
	case EPlayerClass::Hero:
		AddLine(_C("Hero", "The Hero has all standard weapons."));
		AddLine(_C("Hero", "The Hero has to find a flag only visible to them. Stand still to be pointed towards it."));
		ConLine(_C("Hero", "The flag gifts a health point, {int:NumArmorGift} armor and full ammo to all humans."),
			"NumArmorGift", &HeroNumArmorGift);
		ConLine(_C("Hero", "It fully heals the Hero and it can grant a turret which you can place down with the hammer."));
		ConLine(_C("Hero", "The gift to all humans is only applied when the flag is surrounded by hearts and armor."));
		ConLine(_C("Hero", "The Hero cannot be healed by a Medic, but it can withstand a hit from an infected."));
		break;
	case EPlayerClass::Engineer:
		AddLine(_C("Engineer", "The Engineer can build walls with the hammer to block the infected."));
		AddLine(_C("Engineer", "When an infected touches the wall, it dies."));
		AddLine(_C("Engineer", "The lifespan of a wall is {sec:LifeSpan}, and walls are limited to"
							   " one per player at the same time."),
			"LifeSpan", &g_Config.m_InfBarrierLifeSpan);
		break;
	case EPlayerClass::Soldier:
		AddLine(_C("Soldier", "The Soldier creates floating bombs with the hammer."));
		AddLine_Plural(g_Config.m_InfSoldierBombs,
			_CP("Soldier",
				"Each bomb can explode {int:NumBombs} time.",
				"Each bomb can explode {int:NumBombs} times.", g_Config.m_InfSoldierBombs),
			"NumBombs", &g_Config.m_InfSoldierBombs);

		AddLine(_("Use the hammer to place the bomb and explode it multiple times."));
		break;
	case EPlayerClass::Ninja:
		AddLine(_C("Ninja", "The Ninja can throw flash grenades that can freeze the infected for"
							" three seconds."));
		AddLine_Plural(
			g_Config.m_InfNinjaJump,
			_CP("Ninja",
				"Their hammer is replaced with a katana, allowing them to dash {int:NinjaJump}"
				" time before touching the ground.",
				"Their hammer is replaced with a katana, allowing them to dash {int:NinjaJump}"
				" times before touching the ground.", g_Config.m_InfNinjaJump),
			"NinjaJump", &g_Config.m_InfNinjaJump);
		AddLine(_("They also have a laser rifle that blinds the target for a short period of time."));
		AddLine(_("Ninja gets special targets. For killing a target, extra points and abilities"
				  " are awarded."));
		break;
	case EPlayerClass::Sniper:
		AddLine(_C("Sniper", "The Sniper can lock the position in mid-air for 15 seconds with the"
							 " hammer."));
		AddLine(_C("Sniper", "The locked position increases the Sniper's rifle damage from usual"
							 " 10-13 to 30 damage points."));
		AddLine(_C("Sniper", "They can also jump two times in the air."));
		break;
	case EPlayerClass::Scientist:
		AddLine(_C("Scientist", "The Scientist can pose floating mines with the hammer."));
		AddLine_Plural(g_Config.m_InfMineLimit,
			_CP("Scientist",
				"Mines are limited to {int:NumMines} per player at the same time.",
				"Mines are limited to {int:NumMines} per player at the same time.", g_Config.m_InfMineLimit),
			"NumMines", &g_Config.m_InfMineLimit);
		AddLine(_C("Scientist", "Scientist has also grenades that teleport them."));
		AddLine(_C("Scientist", "A lucky Scientist devoted to killing can get a white hole that"
								" sucks the infected in which can be placed with the laser rifle."));
		break;
	case EPlayerClass::Biologist:
		AddLine(_C("Biologist", "The Biologist has a shotgun with bouncing bullets and can create a"
								" spring laser trap by shooting with the laser rifle."));
		AddLine(_C("Biologist", "They are also immune to continuous poison effect, and can stop it"
			" on teammates with the Hammer."));
		break;
	case EPlayerClass::Looper:
		AddLine(_C("Looper", "The Looper has a laser wall that slows down the infected and a"
							 " low-range laser rifle with a high fire rate."));
		AddLine(_C("Looper", "They can also jump two times in the air."));
		break;
	case EPlayerClass::Smoker:
		AddLine(_C("Smoker", "Smoker has a powerful hook that hurts humans and sucks their blood,"
							 " restoring the Smoker's health."));
		AddLine(_C("Smoker", "It can also infect humans and heal the infected with the hammer."));
		break;
	case EPlayerClass::Boomer:
		AddLine(_C("Boomer", "The Boomer explodes when it attacks."));
		AddLine(_C("Boomer", "All humans affected by the explosion become infected."));
		AddLine(_C("Boomer", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Hunter:
		AddLine(_C("Hunter", "The Hunter can jump two times in the air and has some resistance to"
							 " knock-backs."));
		AddLine(_C("Hunter", "It can infect humans and heal the infected with the hammer."));
		AddLine(_C("Hunter", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Bat:
		AddLine(_C("Bat", "Bat can jump endlessly in the air but it cannot infect humans."));
		AddLine(_C("Bat", "Instead, it can hammer humans to steal their health and heal itself."));
		AddLine(_C("Bat", "The hammer is also useful for healing the infected."));
		AddLine(_C("Bat", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Ghost:
		AddLine(_C("Ghost", "The Ghost is invisible until a human comes nearby, it takes damage,"
							" or it uses the hammer."));
		AddLine(_C("Ghost", "It can infect humans and heal the infected with the hammer."));
		AddLine(_C("Ghost", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Spider:
		AddLine(_C("Spider", "The Spider has a web hook that automatically grabs any human touching it."));
		AddLine(_C("Spider", "The web hook mode can be toggled by switching the weapon."));
		AddLine(_C("Spider", "In both modes the hook inflicts 1 damage point per second and can"
							 " grab a human for longer."));
		break;
	case EPlayerClass::Ghoul:
		AddLine(_C("Ghoul", "The Ghoul can devour anything that has died nearby, which makes it"
							" stronger, faster and more resistant."));
		AddLine(_C("Ghoul", "It digests the fodder over time, going back to the normal state."
							" Some nourishment is also lost on death."));
		AddLine(_C("Ghoul", "Ghoul can infect humans and heal the infected with the hammer."));
		AddLine(_C("Ghoul", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Slug:
		AddLine(_C("Slug", "The Slug can make the ground and walls toxic by spreading slime with the hammer."));
		AddLine(_C("Slug", "The slime heals the infected and deals damage to humans."));
		AddLine(_C("Slug", "Slug can infect humans and heal the infected with the hammer."));
		AddLine(_C("Slug", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Voodoo:
		AddLine(_C("Voodoo", "The Voodoo does not die immediately when killed but instead enters"
							 " Spirit mode for a short time."));
		AddLine(_C("Voodoo", "While in Spirit mode it cannot be killed. When the time is up it finally dies."));
		AddLine(_C("Voodoo", "Voodoo can infect humans and heal the infected with the hammer."));
		AddLine(_C("Voodoo", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Witch:
		AddLine(_C("Witch", "The Witch can provide a spawn point for the infected."));
		AddLine(_C("Witch", "If the Witch dies, it disappears and is replaced by another class."));
		AddLine(_C("Witch", "Witch can infect humans and heal the infected with the hammer."));
		AddLine(_C("Witch", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	case EPlayerClass::Undead:
		AddLine(_C("Undead", "The Undead cannot die. Instead of dying, it gets frozen for 10 seconds."));
		AddLine(_C("Undead", "If an infected heals it, the freeze effect disappears."));
		AddLine(_C("Undead", "Undead can infect humans and heal the infected with the hammer."));
		AddLine(_C("Undead", "It can also inflict 1 damage point per second by hooking humans."));
		break;
	}

	return true;
}

void CIcGameController::SnapMapMenu(int SnappingClient, CNetObj_GameInfo *pGameInfoObj)
{
	if(SnappingClient < 0)
		return;

	CIcPlayer *pPlayer = GetPlayer(SnappingClient);
	if(!pPlayer)
		return;

	if(pPlayer->MapMenu() != 1)
		return;

	//Generate class mask
	int ClassMask = 0;
	{
		int Defender = 0;
		int Medic = 0;
		int Hero = 0;
		int Support = 0;

		CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			switch(Iter.Player()->GetClass())
			{
			case EPlayerClass::Ninja:
			case EPlayerClass::Mercenary:
			case EPlayerClass::Sniper:
				Support++;
				break;
			case EPlayerClass::Engineer:
			case EPlayerClass::Soldier:
			case EPlayerClass::Scientist:
			case EPlayerClass::Biologist:
				Defender++;
				break;
			case EPlayerClass::Medic:
				Medic++;
				break;
			case EPlayerClass::Hero:
				Hero++;
				break;
			case EPlayerClass::Looper:
				Defender++;
				break;
			default:
				break;
			}
		}

		if(Defender < Config()->m_InfDefenderLimit)
			ClassMask |= CMapConverter::MASK_DEFENDER;
		if(Medic < Config()->m_InfMedicLimit)
			ClassMask |= CMapConverter::MASK_MEDIC;
		if(Hero < Config()->m_InfHeroLimit)
			ClassMask |= CMapConverter::MASK_HERO;
		if(Support < Config()->m_InfSupportLimit)
			ClassMask |= CMapConverter::MASK_SUPPORT;
	}

	int Item = pPlayer->m_MapMenuItem;
	int Page = CMapConverter::TIMESHIFT_MENUCLASS + 3*((Item+1) + ClassMask*CMapConverter::TIMESHIFT_MENUCLASS_MASK) + 1;

	double PageShift = static_cast<double>(Page * Server()->GetTimeShiftUnit())/1000.0f;
	double SecondsPassed = static_cast<double>(GetRoundTick()) / Server()->TickSpeed();
	double CycleShift = fmod(SecondsPassed, Server()->GetTimeShiftUnit() / 1000.0);
	int TimeShift = (PageShift + CycleShift) * Server()->TickSpeed();

	pGameInfoObj->m_RoundStartTick = Server()->Tick() - TimeShift;
	pGameInfoObj->m_TimeLimit += (TimeShift / Server()->TickSpeed()) / 60;
}

void CIcGameController::FallInLoveIfInfectedEarly(CIcCharacter *pCharacter)
{
	if(!pCharacter)
		return;

	if(IsInfectionStarted())
		return;

	const int RemainingTicks = m_RoundStartTick + Server()->TickSpeed() * GetInfectionDelay() - Server()->Tick();
	float LoveDuration = RemainingTicks / static_cast<float>(Server()->TickSpeed()) + 0.25;

	pCharacter->LoveEffect(LoveDuration);
}

void CIcGameController::RewardTheKillers(CIcCharacter *pVictim, const DeathContext &Context)
{
	// do scoreing
	if(Context.Killer < 0)
		return;

	int Weapon = DamageTypeToWeapon(Context.DamageType);
	if(Weapon == WEAPON_GAME)
		return;

	CIcPlayer *pKiller = GetPlayer(Context.Killer);
	if(!pKiller)
		return;

	CIcPlayer *pAssistant = GetPlayer(Context.Assistant);

	if(pAssistant && (pAssistant->IsHuman() == pVictim->IsHuman()))
	{
		// Do not reward the victim teammates-assistants
		pAssistant = nullptr;
	}

	if(pKiller == pVictim->GetPlayer())
	{
		if(pKiller->IsHuman())
		{
			Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCid(), EScoreEvent::HUMAN_SUICIDE, pKiller->GetClass(), Server()->ClientName(pKiller->GetCid()), Console());
		}
	}
	else
	{
		CIcCharacter *pKillerCharacter = pKiller->GetCharacter();
		if(pKillerCharacter)
		{
			// set attacker's face to happy (taunt!)
			pKillerCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		}
	}

	if(pKiller->IsHuman() == pVictim->IsHuman())
	{
		// Do not process self or team kills
		return;
	}

	if(pVictim->IsInfected())
	{
		EPlayerClass VictimClass = static_cast<EPlayerClass>(pVictim->GetPlayerClass());
		EScoreEvent ScoreEvent = EScoreEvent::KILL_INFECTED;
		bool ClassSpecialProcessingEnabled = (GetRoundType() != ERoundType::Fun) || (GetPlayerClassProbability(VictimClass) == 0);
		if(ClassSpecialProcessingEnabled)
		{
			switch(VictimClass)
			{
			case EPlayerClass::Witch:
				ScoreEvent = EScoreEvent::KILL_WITCH;
				GameServer()->SendChatTarget_Localization(pKiller->GetCid(), CHATCATEGORY_SCORE, _("You have killed a witch, +5 points"), NULL);
				break;
			case EPlayerClass::Undead:
				ScoreEvent = EScoreEvent::KILL_UNDEAD;
				GameServer()->SendChatTarget_Localization(pKiller->GetCid(), CHATCATEGORY_SCORE, _("You have killed an undead! +5 points"), NULL);
				break;
			default:
				break;
			}
		}

		Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCid(), ScoreEvent, pKiller->GetClass(), Server()->ClientName(pKiller->GetCid()), Console());
		GameServer()->SendScoreSound(pKiller->GetCid());
	}

	pKiller->GetCharacterClass()->OnKilledCharacter(pVictim, Context);
	if(pAssistant)
	{
		pAssistant->GetCharacterClass()->OnKilledCharacter(pVictim, Context);
	}

	// Always reward the freezer
	const int VictimFreezer = pVictim->GetFreezer();
	if(VictimFreezer >= 0 && VictimFreezer != Context.Killer && VictimFreezer != Context.Assistant)
	{
		CIcPlayer *pFreezer = GetPlayer(VictimFreezer);
		if(pFreezer)
		{
			pFreezer->GetCharacterClass()->OnKilledCharacter(pVictim, Context);
		}
	}
}

int CIcGameController::OnCharacterDeath(class CCharacter *pAbstractVictim, class CPlayer *pAbstractKiller, int Weapon)
{
	dbg_msg("server", "CRITICAL ERROR: CIcGameController::OnCharacterDeath(class CCharacter *, class CPlayer *, int) must never be called");
	return 0;
}

void CIcGameController::OnIcCharacterDeath(CIcCharacter *pVictim, DeathContext *pContext)
{
	const EDamageType DamageType = pContext->DamageType;
	const int Killer = pContext->Killer;
	const int Assistant = pContext->Assistant;
	const char *pDamageTypeStr = toString(DamageType);

	dbg_msg("server", "OnCharacterDeath: victim=%d damage_type=%s killer=%d assistant=%d", pVictim->GetCid(), pDamageTypeStr, Killer, Assistant);

	RewardTheKillers(pVictim, *pContext);

	int Weapon = DamageTypeToWeapon(DamageType);
	static const icArray<EDamageType, 4> BadReasonsToDie = {
		EDamageType::GAME, // Disconnect, joining spec, etc
		EDamageType::KILL_COMMAND, // Self kill
		EDamageType::GAME_FINAL_EXPLOSION,
	};
	if(!BadReasonsToDie.Contains(DamageType) && (Killer != pVictim->GetCid()))
	{
		if(pVictim->IsHuman())
		{
			const CIcPlayer *pKiller = GetPlayer(pContext->Killer);
			if(pKiller && pKiller->IsInfected() && pKiller->GetCharacter())
			{
				pVictim->GetPlayer()->SetSpecialCameraTargetCid(pKiller->GetCid(), 5.0);
			}
		}

		//Find the nearest ghoul
		for(TEntityPtr<CIcCharacter> p = GameWorld()->FindFirst<CIcCharacter>(); p; ++p)
		{
			if(p->GetPlayerClass() != EPlayerClass::Ghoul || p.data() == pVictim)
				continue;
			if(p->GetClass() && p->GetClass()->GetGhoulPercent() >= 1.0f)
				continue;

			float Len = distance(p->m_Pos, pVictim->m_Pos);

			if(p && Len < 800.0f)
			{
				int Points = (pVictim->IsInfected() ? 8 : 14);
				new CFlyingPoint(GameServer(), pVictim->m_Pos, p->GetCid(), Points, pVictim->Velocity());
			}
		}
	}

	static const icArray<EDamageType, 2> ReasonsForNoDrop = {
		EDamageType::GAME_INFECTION,
		EDamageType::GAME,
	};
	if(!ReasonsForNoDrop.Contains(DamageType))
	{
		MaybeDropPickup(pVictim);
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
		Server()->ClientName(Killer),
		Server()->ClientName(pVictim->GetCid()), Weapon);
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// It is important to SendKillMessage before GetClass()->OnCharacterDeath() to keep the correct kill order
	SendKillMessage(pVictim->GetCid(), *pContext);

	if(pVictim->GetClass())
	{
		pVictim->GetClass()->OnCharacterDeath(DamageType);
	}

	INFECTION_TYPE InfectionType = INFECTION_TYPE::REGULAR;
	bool ClassSpecialProcessingEnabled = true;

	EPlayerClass VictimClass = static_cast<EPlayerClass>(pVictim->GetPlayerClass());
	if(DamageType == EDamageType::GAME)
	{
		ClassSpecialProcessingEnabled = false;
	}
	else if((GetRoundType() == ERoundType::Fun) && !pVictim->IsHuman() && GetPlayerClassProbability(VictimClass))
	{
		ClassSpecialProcessingEnabled = false;
	}

	if(ClassSpecialProcessingEnabled)
	{
		switch(VictimClass)
		{
			case EPlayerClass::Witch:
				GameServer()->SendBroadcast_Localization(-1, EBroadcastPriority::GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The witch is dead"), NULL);
				GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
				InfectionType = INFECTION_TYPE::RESTORE_INF_CLASS;
				break;
			case EPlayerClass::Undead:
				GameServer()->SendBroadcast_Localization(-1, EBroadcastPriority::GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The undead is finally dead"), NULL);
				GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
				InfectionType = INFECTION_TYPE::RESTORE_INF_CLASS;
				break;
			default:
				break;
		}
	}
	else
	{
		// Still send the traditional 'whoosh' sound
		if(VictimClass == EPlayerClass::Witch)
		{
			GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
		}
	}

	// Do not infect on disconnect or joining spec
	bool Infect = DamageType != EDamageType::GAME;
	if(Infect)
	{
		pVictim->GetPlayer()->StartInfection(pContext->Killer, InfectionType);
	}

	bool SelfKill = false;
	switch (DamageType)
	{
	case EDamageType::TURRET_DESTRUCTION:
	case EDamageType::DEATH_TILE:
	case EDamageType::KILL_COMMAND:
		SelfKill = true;
		break;
	default:
		SelfKill = Killer == pVictim->GetCid();
		break;
	}

	int RespawnDelay = 0;
	if(SelfKill)
	{
		// Wait 3.0 secs in a case of selfkill
		RespawnDelay = Server()->TickSpeed() * 3.0f;
	}
	else
	{
		RespawnDelay = Server()->TickSpeed() * 0.5f;
	}

	if(GetRoundType() == ERoundType::Fast)
	{
		RespawnDelay *= 0.5;
	}

	if(m_Warmup > 0)
	{
		RespawnDelay = Server()->TickSpeed() * 0.2f;
	}

	pVictim->GetPlayer()->m_RespawnTick = Server()->Tick() + RespawnDelay;

	if(pContext->DamageType == EDamageType::INFECTION_TILE)
	{
		int FreezeDuration = m_Warmup > 0 ? 0 : Config()->m_InfInfzoneFreezeDuration;
		if(FreezeDuration > 0)
		{
			pVictim->Freeze(FreezeDuration, pContext->Killer, FREEZEREASON_INFECTION);
		}
	}
}

void CIcGameController::OnIcCharacterSpawned(CIcCharacter *pCharacter, const SpawnContext &Context)
{
	CIcPlayer *pPlayer = pCharacter->GetPlayer();
	if(!IsInfectionStarted() && pPlayer->RandomClassChoosen())
	{
		pCharacter->GiveRandomClassSelectionBonus();
	}

	if(pCharacter->IsInfected())
	{
		FallInLoveIfInfectedEarly(pCharacter);
		pCharacter->SetHealthArmor(10, InfectedBonusArmor());

		if(Context.SpawnType == SpawnContext::MapSpawn)
		{
			float Duration = g_Config.m_InfSpawnProtectionTime / 1000.0f;
			pCharacter->GrantSpawnProtection(Duration);
		}
	}

	if((GetRoundType() == ERoundType::Fun) && !IsInfectionStarted() && pCharacter->GetPlayerClass() == EPlayerClass::None)
	{
		pPlayer->SetClass(ChooseHumanClass(pPlayer));
	}
}

void CIcGameController::OnClassChooserRequested(CIcCharacter *pCharacter)
{
	CIcPlayer *pPlayer = pCharacter->GetPlayer();

	if(GetRoundType() == ERoundType::Fun)
	{
		pPlayer->SetRandomClassChoosen();
		// Read this as "player didn't choose this class"
		pCharacter->GiveRandomClassSelectionBonus();
		pPlayer->CloseMapMenu();
		return;
	}

	const EPlayerClass PreferredClass = pPlayer->GetPreferredClass();
	if(!IsClassChooserEnabled() || (PreferredClass != EPlayerClass::Invalid))
	{
		pPlayer->SetClass(ChooseHumanClass(pPlayer));

		if(PreferredClass == EPlayerClass::Random)
		{
			pPlayer->SetRandomClassChoosen();

			if(IsClassChooserEnabled())
			{
				pCharacter->GiveRandomClassSelectionBonus();
			}
		}
	}
	else
	{
		pPlayer->OpenMapMenu(1);
	}
}

void CIcGameController::CheckRoundFailed()
{
	if(m_Warmup)
		return;

	if(!IsWinCheckEnabled())
		return;

	if(IsGameOver())
		return;

	if(Config()->m_InfTrainingMode)
		return;

	if(GetRoundType() == ERoundType::Survival)
		return;

	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	if((NumHumans == 0) && (NumInfected == 0))
		return;

	ROUND_CANCELATION_REASON Reason = ROUND_CANCELATION_REASON::INVALID;

	if(NumInfected == 0)
	{
		if(m_RoundStartTick + Server()->TickSpeed() * (GetInfectionDelay() + 1) <= Server()->Tick())
		{
			Reason = ROUND_CANCELATION_REASON::ALL_INFECTED_LEFT_THE_GAME;
		}
	}

	if(NumHumans == 0)
	{
		CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		bool HasNonGameInfectionCauses = false;
		while(Iter.Next())
		{
			const CIcPlayer *pPlayer = Iter.Player();
			if(pPlayer->InfectionCause() != INFECTION_CAUSE::GAME)
			{
				HasNonGameInfectionCauses = true;
				break;
			}
		}

		if(HasNonGameInfectionCauses)
		{
			// Okay, inf won
		}
		else
		{
			Reason = ROUND_CANCELATION_REASON::EVERYONE_INFECTED_BY_THE_GAME;
		}
	}

	if(Reason != ROUND_CANCELATION_REASON::INVALID)
	{
		// Round failed: all infected left the game
		// The infected didn't infect anyone. Cancel the round.
		CancelTheRound(Reason);
	}
}

float CIcGameController::GetMaxInactiveTimeSeconds(const CPlayer *pPlayer) const
{
	const CIcPlayer *pInfPlayer = CIcPlayer::GetInstance(pPlayer);

	int HumanMaxInactiveTimeSecs = Config()->m_InfInactiveHumansKickTime ? Config()->m_InfInactiveHumansKickTime : Config()->m_SvInactiveKickTime * 60;
	int InfectedMaxInactiveTimeSecs = Config()->m_InfInactiveInfectedKickTime ? Config()->m_InfInactiveInfectedKickTime : Config()->m_SvInactiveKickTime * 60;

	return pInfPlayer->IsHuman() ? HumanMaxInactiveTimeSecs : InfectedMaxInactiveTimeSecs;
}

void CIcGameController::DoWincheck()
{
	if(!IsWinCheckEnabled())
		return;

	if(Config()->m_InfTrainingMode)
		return;

	if(!m_InfectedStarted)
	{
		return;
	}

	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	bool VictoryConditionsMet = false;
	bool NeedFinalExplosion = false;
	bool TimeIsOut = false;
	const int Seconds = (Server()->Tick() - m_RoundStartTick) / ((float)Server()->TickSpeed());
	if(GetTimeLimitMinutes() > 0 && Seconds >= GetTimeLimitSeconds())
	{
		TimeIsOut = true;
	}

	switch(GetRoundType())
	{
	default:
		// One infected can win in some rounds; we have a check if this is a valid situation in CheckRoundFailed()
		if(NumHumans == 0 && NumInfected >= 1)
		{
			VictoryConditionsMet = true;
		}
		else if(TimeIsOut)
		{
			VictoryConditionsMet = true;
			NeedFinalExplosion = true;
		}
		break;
	}

	if(!VictoryConditionsMet)
	{
		return;
	}

	// Start the final explosion if the time is over
	if(NeedFinalExplosion)
	{
		EnsureFinalExplosionIsStarted();
	}

	//If no more explosions, game over, decide who win
	if(!NeedFinalExplosion || (m_FinalExplosionState == EFinalExplosionState::Finished))
	{
		AnnounceTheWinner(NumHumans);
	}
}

bool CIcGameController::IsSpawnable(vec2 Pos, EZoneTele TeleZoneIndex)
{
	//First check if there is a tee too close
	CCharacter *aEnts[MAX_CLIENTS];
	int Num = GameWorld()->FindEntities(Pos, 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	
	for(int c = 0; c < Num; ++c)
	{
		if(distance(aEnts[c]->m_Pos, Pos) <= 60)
			return false;
	}

	// Check the center
	EZoneTele TeleIndex = GetTeleportZoneValueAt(Pos);
	if(GameServer()->Collision()->CheckPoint(Pos))
		return false;
	if((TeleZoneIndex != EZoneTele::Null) && (TeleIndex == TeleZoneIndex))
		return false;

	// Check the border of the tee. Kind of extrem, but more precise
	for(int i = 0; i < 16; i++)
	{
		float Angle = i * (2.0f * pi / 16.0f);
		vec2 CheckPos = Pos + vec2(cos(Angle), sin(Angle)) * 30.0f;
		TeleIndex = GetTeleportZoneValueAt(CheckPos);
		if(GameServer()->Collision()->CheckPoint(CheckPos))
			return false;
		if((TeleZoneIndex != EZoneTele::Null) && (TeleIndex == TeleZoneIndex))
			return false;
	}

	return true;
}

bool CIcGameController::TryRespawn(CIcPlayer *pPlayer, SpawnContext *pContext)
{
	// spectators can't spawn
	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		return false;

	// Deny any spawns during the World ResetRequested because the new Characters
	// are going to be destroyed during this IGameServer::Tick().
	// (and it may break the auto class selection)
	if(GameWorld()->m_ResetRequested)
	{
		return false;
	}

	bool Infect = m_InfectedStarted;
	if(Infect)
		pPlayer->StartInfection();

	if(pPlayer->IsInfected() && (m_FinalExplosionState != EFinalExplosionState::NotStarted))
		return false;

	if(GetRoundType() == ERoundType::Survival && pPlayer->IsInfected())
	{
		if(!IsInfectionStarted())
		{
			return false;
		}

		if(!pPlayer->IsBot())
		{
			GameServer()->SendBroadcast(pPlayer->GetCid(), "You are dead and have to wait for others",
				EBroadcastPriority::GAMEANNOUNCE, BROADCAST_DURATION_REALTIME);
			return false;
		}
	}

	if(m_InfectedStarted && pPlayer->IsInfected() && random_prob(Config()->m_InfProbaSpawnNearWitch / 100.0f))
	{
		CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			if(Iter.Player()->GetCid() == pPlayer->GetCid())
				continue;
			if(Iter.Player()->GetClass() != EPlayerClass::Witch)
				continue;

			const CIcCharacter *pCharacter = Iter.Player()->GetCharacter();
			if(!pCharacter || !pCharacter->IsAlive())
				continue;

			if(pCharacter->IsFrozen() || pCharacter->IsSleeping())
				continue;

			const CInfClassInfected *pInfected = CInfClassInfected::GetInstance(pCharacter);

			if(pInfected->FindWitchSpawnPosition(pContext->SpawnPos))
			{
				pContext->SpawnType = SpawnContext::WitchSpawn;
				return true;
			}
		}
	}

	int Type = (pPlayer->IsInfected() ? 0 : 1);

	if(m_avSpawnPoints[Type].size() == 0)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "The map has no spawn points");
		return false;
	}

	// get spawn point
	const std::vector<vec2> &vSpawnPoints = m_avSpawnPoints[Type];
	const std::size_t Count = vSpawnPoints.size();
	const int RandomShift = random_int(0, Count - 1);
	for(std::size_t i = 0; i < Count; i++)
	{
		int PosIndex = (i + RandomShift) % Count;
		const vec2 &Pos = vSpawnPoints[PosIndex];
		if(!IsSpawnable(Pos, EZoneTele::Null))
			continue;

		pContext->SpawnPos = Pos;
		pContext->SpawnType = SpawnContext::MapSpawn;
		return true;
	}

	return false;
}

EPlayerClass CIcGameController::ChooseHumanClass(const CIcPlayer *pPlayer) const
{
	double Probability[NB_PLAYERCLASS]{};
	auto GetClassProbabilityRef = [&Probability](EPlayerClass PlayerClass) -> double & {
		return Probability[static_cast<int>(PlayerClass)];
	};

	int AvailableClasses = 0;
	for(EPlayerClass PlayerClass : AllHumanClasses)
	{
		double &ClassProbability = GetClassProbabilityRef(PlayerClass);
		ClassProbability = GetPlayerClassEnabled(PlayerClass) ? 1.0f : 0.0f;
		if(GetRoundType() != ERoundType::Fun)
		{
			CLASS_AVAILABILITY Availability = GetPlayerClassAvailability(PlayerClass, pPlayer);
			if(Availability != CLASS_AVAILABILITY::AVAILABLE)
			{
				ClassProbability = 0.0f;
			}
		}

		if(ClassProbability > 0)
		{
			AvailableClasses++;
		}
	}

	EPlayerClass PreferredClass = pPlayer->GetPreferredClass();
	if(PreferredClass != EPlayerClass::Invalid)
	{
		if(PreferredClass != EPlayerClass::Random)
		{
			if(GetClassProbabilityRef(PreferredClass) > 0)
			{
				return PreferredClass;
			}
		}
	}

	// Random is not fair enough. We keep the last classes took by the player, and avoid to give those again
	if(GetRoundType() != ERoundType::Fun)
	{
		if(AvailableClasses > 1)
		{
			// if normal round is being played
			EPlayerClass PrevClass = pPlayer->GetPreviouslyPickedClass();
			if(PrevClass != EPlayerClass::Invalid)
			{
				GetClassProbabilityRef(PrevClass) = 0.0f;
			}
		}
	}

	int Result = random_distribution(Probability, Probability + NB_PLAYERCLASS);
	return static_cast<EPlayerClass>(Result);
}

EPlayerClass CIcGameController::ChooseInfectedClass(const CIcPlayer *pPlayer) const
{
	// if(pPlayer->InfectionType() == INFECTION_TYPE::RESTORE_INF_CLASS)
	{
		EPlayerClass PrevClass = pPlayer->GetPreviousInfectedClass();
		if(PrevClass != EPlayerClass::Invalid)
		{
			return PrevClass;
		}
	}

	//Get information about existing infected
	int nbInfected = 0;
	icArray<int, NB_PLAYERCLASS> nbClass;
	nbClass.Resize(NB_PLAYERCLASS);

	CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	int PlayersCount = 0;
	while(Iter.Next())
	{
		++PlayersCount;
		const EPlayerClass AnotherPlayerClass = Iter.Player()->GetClass();
		const int Index = static_cast<int>(AnotherPlayerClass);
		if(Iter.Player()->IsInfected())
			nbInfected++;
		nbClass[Index]++;
	}

	int InitiallyInfected = GetMinimumInfectedForPlayers(PlayersCount);

	double Probability[NB_PLAYERCLASS]{};
	auto GetClassProbabilityRef = [&Probability](EPlayerClass PlayerClass) -> double & {
		return Probability[static_cast<int>(PlayerClass)];
	};

	for(EPlayerClass PlayerClass : AllInfectedClasses)
	{
		double &ClassProbability = GetClassProbabilityRef(PlayerClass);
		ClassProbability = GetPlayerClassProbability(PlayerClass);
		if(GetRoundType() == ERoundType::Fun)
		{
			// We care only about the class enablement
			continue;
		}

		switch(PlayerClass)
		{
			case EPlayerClass::Bat:
				if(nbInfected <= InitiallyInfected)
				{
					// We can't just set the proba to 0, because it would break a config
					// with all classes except the Bat set to 0.
					ClassProbability = ClassProbability / 10000.0;
				}
				break;
			case EPlayerClass::Ghoul:
				if(nbInfected < Config()->m_InfGhoulThreshold)
					ClassProbability = 0;
				break;
			case EPlayerClass::Witch:
			case EPlayerClass::Undead:
				if((nbInfected <= 2) || nbClass[static_cast<int>(PlayerClass)] > 0)
					ClassProbability = 0;
				break;
			default:
				break;
		}
	}

	int Result = random_distribution(Probability, Probability + NB_PLAYERCLASS);
	EPlayerClass Class = static_cast<EPlayerClass>(Result);

	int Seconds = (Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed());
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "infected victim='%s' duration='%d' class='%s'",
		Server()->ClientName(pPlayer->GetCid()), Seconds, toString(Class));
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	return Class;
}

float CIcGameController::GetWeaponForce(EInfclassWeapon WID) const
{
	return m_aInfWeaponForce[static_cast<std::size_t>(WID)];
}

void CIcGameController::SetWeaponForce(EInfclassWeapon WID, float Force)
{
	m_aInfWeaponForce[static_cast<std::size_t>(WID)] = Force;
}

int CIcGameController::GetFireDelay(EInfclassWeapon WID) const
{
	return m_InfFireDelay[static_cast<int>(WID)];
}

void CIcGameController::SetFireDelay(EInfclassWeapon WID, int Time)
{
	m_InfFireDelay[static_cast<int>(WID)] = Time;
}

int CIcGameController::GetAmmoRegenTime(EInfclassWeapon WID) const
{
	return m_InfAmmoRegenTime[static_cast<int>(WID)];
}

void CIcGameController::SetAmmoRegenTime(EInfclassWeapon WID, int Time)
{
	m_InfAmmoRegenTime[static_cast<int>(WID)] = Time;
}

int CIcGameController::GetMaxAmmo(EInfclassWeapon WID) const
{
	return m_InfMaxAmmo[static_cast<int>(WID)];
}

void CIcGameController::SetMaxAmmo(EInfclassWeapon WID, int n)
{
	m_InfMaxAmmo[static_cast<int>(WID)] = n;
}

void CIcGameController::InitWeapons()
{
	SetWeaponForce(EInfclassWeapon::NONE, 0);
	SetWeaponForce(EInfclassWeapon::HAMMER, 10);
	SetWeaponForce(EInfclassWeapon::GUN, 0);
	SetWeaponForce(EInfclassWeapon::SHOTGUN, 2);
	SetWeaponForce(EInfclassWeapon::GRENADE, 0);
	SetWeaponForce(EInfclassWeapon::LASER, 0);
	SetWeaponForce(EInfclassWeapon::NINJA, 10);
	SetWeaponForce(EInfclassWeapon::ENGINEER_LASER, GetWeaponForce(EInfclassWeapon::LASER));
	SetWeaponForce(EInfclassWeapon::SOLDIER_GRENADE, GetWeaponForce(EInfclassWeapon::GRENADE));
	SetWeaponForce(EInfclassWeapon::EXPLOSIVE_LASER, GetWeaponForce(EInfclassWeapon::LASER));
	SetWeaponForce(EInfclassWeapon::TELEPORT_GUN, GetWeaponForce(EInfclassWeapon::GRENADE));
	SetWeaponForce(EInfclassWeapon::HEALING_GRENADE, GetWeaponForce(EInfclassWeapon::GRENADE));
	SetWeaponForce(EInfclassWeapon::MEDIC_LASER, GetWeaponForce(EInfclassWeapon::LASER));
	SetWeaponForce(EInfclassWeapon::MEDIC_SHOTGUN, 6);
	SetWeaponForce(EInfclassWeapon::HERO_SHOTGUN, GetWeaponForce(EInfclassWeapon::SHOTGUN));
	SetWeaponForce(EInfclassWeapon::RICOCHET_SHOTGUN, GetWeaponForce(EInfclassWeapon::SHOTGUN));
	SetWeaponForce(EInfclassWeapon::BIOLOGIST_MINE_LASER, GetWeaponForce(EInfclassWeapon::LASER));
	SetWeaponForce(EInfclassWeapon::LOOPER_LASER, GetWeaponForce(EInfclassWeapon::LASER));
	SetWeaponForce(EInfclassWeapon::LOOPER_GRENADE, GetWeaponForce(EInfclassWeapon::GRENADE));
	SetWeaponForce(EInfclassWeapon::HERO_LASER, GetWeaponForce(EInfclassWeapon::LASER));
	SetWeaponForce(EInfclassWeapon::HERO_GRENADE, GetWeaponForce(EInfclassWeapon::GRENADE));
	SetWeaponForce(EInfclassWeapon::SNIPER_RIFLE, GetWeaponForce(EInfclassWeapon::LASER));
	SetWeaponForce(EInfclassWeapon::NINJA_KATANA, GetWeaponForce(EInfclassWeapon::NINJA));
	SetWeaponForce(EInfclassWeapon::NINJA_GRENADE, GetWeaponForce(EInfclassWeapon::GRENADE));
	SetWeaponForce(EInfclassWeapon::POISON_GRENADE, GetWeaponForce(EInfclassWeapon::GRENADE));
	SetWeaponForce(EInfclassWeapon::MERCENARY_GUN, GetWeaponForce(EInfclassWeapon::GUN));
	SetWeaponForce(EInfclassWeapon::MERCENARY_UPGRADE_LASER, 0);
	SetWeaponForce(EInfclassWeapon::BLINDING_LASER, GetWeaponForce(EInfclassWeapon::LASER));
	SetWeaponForce(EInfclassWeapon::TRANQUILIZER_RIFLE, GetWeaponForce(EInfclassWeapon::LASER));

	SetFireDelay(EInfclassWeapon::NONE, 0);
	SetFireDelay(EInfclassWeapon::HAMMER, 125);
	SetFireDelay(EInfclassWeapon::GUN, 125);
	SetFireDelay(EInfclassWeapon::SHOTGUN, 500);
	SetFireDelay(EInfclassWeapon::GRENADE, 500);
	SetFireDelay(EInfclassWeapon::LASER, 800);
	SetFireDelay(EInfclassWeapon::NINJA, 800);
	SetFireDelay(EInfclassWeapon::ENGINEER_LASER, GetFireDelay(EInfclassWeapon::LASER));
	SetFireDelay(EInfclassWeapon::SOLDIER_GRENADE, GetFireDelay(EInfclassWeapon::GRENADE));
	SetFireDelay(EInfclassWeapon::EXPLOSIVE_LASER, GetFireDelay(EInfclassWeapon::LASER));
	SetFireDelay(EInfclassWeapon::TELEPORT_GUN, GetFireDelay(EInfclassWeapon::GRENADE));
	SetFireDelay(EInfclassWeapon::HEALING_GRENADE, GetFireDelay(EInfclassWeapon::GRENADE));
	SetFireDelay(EInfclassWeapon::MEDIC_LASER, GetFireDelay(EInfclassWeapon::LASER));
	SetFireDelay(EInfclassWeapon::MEDIC_SHOTGUN, 250);
	SetFireDelay(EInfclassWeapon::HERO_SHOTGUN, 250);
	SetFireDelay(EInfclassWeapon::RICOCHET_SHOTGUN, 250);
	SetFireDelay(EInfclassWeapon::BIOLOGIST_MINE_LASER, GetFireDelay(EInfclassWeapon::LASER));
	SetFireDelay(EInfclassWeapon::LOOPER_LASER, 250);
	SetFireDelay(EInfclassWeapon::LOOPER_GRENADE, GetFireDelay(EInfclassWeapon::GRENADE));
	SetFireDelay(EInfclassWeapon::HERO_LASER, GetFireDelay(EInfclassWeapon::LASER));
	SetFireDelay(EInfclassWeapon::HERO_GRENADE, GetFireDelay(EInfclassWeapon::GRENADE));
	SetFireDelay(EInfclassWeapon::SNIPER_RIFLE, GetFireDelay(EInfclassWeapon::LASER));
	SetFireDelay(EInfclassWeapon::NINJA_KATANA, GetFireDelay(EInfclassWeapon::NINJA));
	SetFireDelay(EInfclassWeapon::NINJA_GRENADE, GetFireDelay(EInfclassWeapon::GRENADE));
	SetFireDelay(EInfclassWeapon::POISON_GRENADE, GetFireDelay(EInfclassWeapon::GRENADE));
	SetFireDelay(EInfclassWeapon::MERCENARY_GUN, 50);
	SetFireDelay(EInfclassWeapon::MERCENARY_UPGRADE_LASER, 200);
	SetFireDelay(EInfclassWeapon::BLINDING_LASER, GetFireDelay(EInfclassWeapon::LASER));
	SetFireDelay(EInfclassWeapon::TRANQUILIZER_RIFLE, GetFireDelay(EInfclassWeapon::LASER));

	SetAmmoRegenTime(EInfclassWeapon::NONE, 0);
	SetAmmoRegenTime(EInfclassWeapon::HAMMER, 0);
	SetAmmoRegenTime(EInfclassWeapon::GUN, 500);
	SetAmmoRegenTime(EInfclassWeapon::SHOTGUN, 0);
	SetAmmoRegenTime(EInfclassWeapon::GRENADE, 0);
	SetAmmoRegenTime(EInfclassWeapon::LASER, 0);
	SetAmmoRegenTime(EInfclassWeapon::NINJA, 0);

	SetAmmoRegenTime(EInfclassWeapon::ENGINEER_LASER, 6000);
	SetAmmoRegenTime(EInfclassWeapon::SOLDIER_GRENADE, 7000);
	SetAmmoRegenTime(EInfclassWeapon::EXPLOSIVE_LASER, 6000);
	SetAmmoRegenTime(EInfclassWeapon::TELEPORT_GUN, 10000);
	SetAmmoRegenTime(EInfclassWeapon::HEALING_GRENADE, 0);
	SetAmmoRegenTime(EInfclassWeapon::MEDIC_LASER, 600);
	SetAmmoRegenTime(EInfclassWeapon::MEDIC_SHOTGUN, 750);
	SetAmmoRegenTime(EInfclassWeapon::HERO_SHOTGUN, 750);
	SetAmmoRegenTime(EInfclassWeapon::HERO_LASER, 3000);
	SetAmmoRegenTime(EInfclassWeapon::HERO_GRENADE, 3000);
	SetAmmoRegenTime(EInfclassWeapon::SNIPER_RIFLE, 2000);
	SetAmmoRegenTime(EInfclassWeapon::POISON_GRENADE, 5000);
	SetAmmoRegenTime(EInfclassWeapon::MERCENARY_GUN, 125);
	SetAmmoRegenTime(EInfclassWeapon::MERCENARY_UPGRADE_LASER, 4000);
	SetAmmoRegenTime(EInfclassWeapon::NINJA_KATANA, 0);
	SetAmmoRegenTime(EInfclassWeapon::NINJA_GRENADE, 15000);
	SetAmmoRegenTime(EInfclassWeapon::BIOLOGIST_MINE_LASER, 175);
	SetAmmoRegenTime(EInfclassWeapon::RICOCHET_SHOTGUN, 675);
	SetAmmoRegenTime(EInfclassWeapon::LOOPER_LASER, 500);
	SetAmmoRegenTime(EInfclassWeapon::LOOPER_GRENADE, 5000);
	SetAmmoRegenTime(EInfclassWeapon::BLINDING_LASER, 10000);
	SetAmmoRegenTime(EInfclassWeapon::TRANQUILIZER_RIFLE, 1000);

	SetMaxAmmo(EInfclassWeapon::NONE, -1);
	SetMaxAmmo(EInfclassWeapon::HAMMER, -1);
	SetMaxAmmo(EInfclassWeapon::GUN, 10);
	SetMaxAmmo(EInfclassWeapon::SHOTGUN, 10);
	SetMaxAmmo(EInfclassWeapon::GRENADE, 10);
	SetMaxAmmo(EInfclassWeapon::LASER, 10);
	SetMaxAmmo(EInfclassWeapon::NINJA, 10);
	SetMaxAmmo(EInfclassWeapon::ENGINEER_LASER, 10);
	SetMaxAmmo(EInfclassWeapon::EXPLOSIVE_LASER, 10);
	SetMaxAmmo(EInfclassWeapon::TELEPORT_GUN, 3);
	SetMaxAmmo(EInfclassWeapon::SOLDIER_GRENADE, 10);
	SetMaxAmmo(EInfclassWeapon::HEALING_GRENADE, 10);
	SetMaxAmmo(EInfclassWeapon::MEDIC_LASER, 10);
	SetMaxAmmo(EInfclassWeapon::MEDIC_SHOTGUN, 10);
	SetMaxAmmo(EInfclassWeapon::HERO_SHOTGUN, 10);
	SetMaxAmmo(EInfclassWeapon::HERO_LASER, 10);
	SetMaxAmmo(EInfclassWeapon::HERO_GRENADE, 10);
	SetMaxAmmo(EInfclassWeapon::SNIPER_RIFLE, 10);
	SetMaxAmmo(EInfclassWeapon::NINJA_KATANA, -1);
	SetMaxAmmo(EInfclassWeapon::NINJA_GRENADE, 5);
	SetMaxAmmo(EInfclassWeapon::POISON_GRENADE, 8);
	SetMaxAmmo(EInfclassWeapon::MERCENARY_GUN, 40);
	SetMaxAmmo(EInfclassWeapon::MERCENARY_UPGRADE_LASER, 10);
	SetMaxAmmo(EInfclassWeapon::BIOLOGIST_MINE_LASER, 10);
	SetMaxAmmo(EInfclassWeapon::RICOCHET_SHOTGUN, 10);
	SetMaxAmmo(EInfclassWeapon::LOOPER_LASER, 20);
	SetMaxAmmo(EInfclassWeapon::LOOPER_GRENADE, 10);
	SetMaxAmmo(EInfclassWeapon::BLINDING_LASER, 10);
	SetMaxAmmo(EInfclassWeapon::TRANQUILIZER_RIFLE, 10);

	// Infected weapons
	SetWeaponForce(EInfclassWeapon::JAWS, GetWeaponForce(EInfclassWeapon::HAMMER));
	SetWeaponForce(EInfclassWeapon::SLIME, GetWeaponForce(EInfclassWeapon::HAMMER));
	SetWeaponForce(EInfclassWeapon::INFECTED_HAMMER, GetWeaponForce(EInfclassWeapon::HAMMER));
	SetWeaponForce(EInfclassWeapon::BOOMER_EXPLOSION, 52);

	SetFireDelay(EInfclassWeapon::JAWS, GetFireDelay(EInfclassWeapon::HAMMER));
	SetFireDelay(EInfclassWeapon::SLIME, GetFireDelay(EInfclassWeapon::HAMMER));
	SetFireDelay(EInfclassWeapon::INFECTED_HAMMER, GetFireDelay(EInfclassWeapon::HAMMER));
	SetFireDelay(EInfclassWeapon::BOOMER_EXPLOSION, GetFireDelay(EInfclassWeapon::HAMMER));

	SetAmmoRegenTime(EInfclassWeapon::JAWS, 0);
	SetAmmoRegenTime(EInfclassWeapon::SLIME, 0);
	SetAmmoRegenTime(EInfclassWeapon::INFECTED_HAMMER, 0);
	SetAmmoRegenTime(EInfclassWeapon::BOOMER_EXPLOSION, 0);

	SetMaxAmmo(EInfclassWeapon::JAWS, -1);
	SetMaxAmmo(EInfclassWeapon::SLIME, -1);
	SetMaxAmmo(EInfclassWeapon::INFECTED_HAMMER, -1);
	SetMaxAmmo(EInfclassWeapon::BOOMER_EXPLOSION, -1);
}

bool CIcGameController::GetPlayerClassEnabled(EPlayerClass PlayerClass) const
{
	int Index = static_cast<int>(PlayerClass);
	if(Index < 0)
		return false;

	if(m_aClassEnabled[Index].has_value())
		return m_aClassEnabled[Index].value();

	if(GetRoundType() == ERoundType::Fun)
	{
		return PlayerClass == m_FunRoundConfiguration.HumanClass;
	}
	if(GetRoundType() == ERoundType::Survival)
	{
		switch(PlayerClass)
		{
		case EPlayerClass::Engineer:
		case EPlayerClass::Soldier:
			return false;
		default:
			break;
		}
	}

	switch(PlayerClass)
	{
	case EPlayerClass::Engineer:
		return Config()->m_InfEnableEngineer;
	case EPlayerClass::Soldier:
		return Config()->m_InfEnableSoldier;
	case EPlayerClass::Scientist:
		return Config()->m_InfEnableScientist;
	case EPlayerClass::Biologist:
		return Config()->m_InfEnableBiologist;
	case EPlayerClass::Medic:
		return Config()->m_InfEnableMedic;
	case EPlayerClass::Hero:
		return Config()->m_InfEnableHero;
	case EPlayerClass::Ninja:
		return Config()->m_InfEnableNinja;
	case EPlayerClass::Mercenary:
		return Config()->m_InfEnableMercenary;
	case EPlayerClass::Sniper:
		return Config()->m_InfEnableSniper;
	case EPlayerClass::Looper:
		return Config()->m_InfEnableLooper;
	default:
		break;
	}

	return false;
}

bool CIcGameController::SetPlayerClassEnabled(EPlayerClass PlayerClass, bool Enabled)
{
	int Index = static_cast<int>(PlayerClass);
	if(Index < 0)
		return false;

	m_aClassEnabled[Index] = Enabled;
	return true;
}

bool CIcGameController::ResetPlayerClassEnabled(EPlayerClass PlayerClass)
{
	int Index = static_cast<int>(PlayerClass);
	if(Index < 0)
		return false;

	m_aClassEnabled[Index].reset();
	return true;
}

void CIcGameController::ResetPlayerClassesEnablement()
{
	for(std::optional<bool> &Enabled : m_aClassEnabled)
		Enabled.reset();
}

bool CIcGameController::SetPlayerClassProbability(EPlayerClass PlayerClass, int Probability)
{
	switch (PlayerClass)
	{
	case EPlayerClass::Smoker:
		Config()->m_InfProbaSmoker = Probability;
		break;
	case EPlayerClass::Boomer:
		Config()->m_InfProbaBoomer = Probability;
		break;
	case EPlayerClass::Hunter:
		Config()->m_InfProbaHunter = Probability;
		break;
	case EPlayerClass::Bat:
		Config()->m_InfProbaBat = Probability;
		break;
	case EPlayerClass::Ghost:
		Config()->m_InfProbaGhost = Probability;
		break;
	case EPlayerClass::Spider:
		Config()->m_InfProbaSpider = Probability;
		break;
	case EPlayerClass::Ghoul:
		Config()->m_InfProbaGhoul = Probability;
		break;
	case EPlayerClass::Slug:
		Config()->m_InfProbaSlug = Probability;
		break;
	case EPlayerClass::Voodoo:
		Config()->m_InfProbaVoodoo = Probability;
		break;
	case EPlayerClass::Witch:
		Config()->m_InfProbaWitch = Probability;
		break;
	case EPlayerClass::Undead:
		Config()->m_InfProbaUndead = Probability;
		break;
	default:
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "controller", "WARNING: Invalid SetPlayerClassProbability() call");
		return false;
	}

	return true;
}

uint32_t CIcGameController::GetMinPlayersForClass(EPlayerClass PlayerClass) const
{
	switch(PlayerClass)
	{
	case EPlayerClass::Engineer:
		return Config()->m_InfMinPlayersForEngineer;
	default:
		return 0;
	}
}

uint32_t CIcGameController::GetClassPlayerLimit(EPlayerClass PlayerClass) const
{
	switch(PlayerClass)
	{
	case EPlayerClass::Medic:
		return Config()->m_InfMedicLimit;
	case EPlayerClass::Hero:
		return Config()->m_InfHeroLimit;
	case EPlayerClass::Witch:
		return Config()->m_InfWitchLimit;
	default:
		return Config()->m_SvMaxClients;
	}
}

int CIcGameController::GetPlayerClassProbability(EPlayerClass PlayerClass) const
{
	if(GetRoundType() == ERoundType::Fun)
	{
		return PlayerClass == m_FunRoundConfiguration.InfectedClass;
	}

	switch(PlayerClass)
	{
	case EPlayerClass::Smoker:
		return Config()->m_InfProbaSmoker;
	case EPlayerClass::Boomer:
		return Config()->m_InfProbaBoomer;
	case EPlayerClass::Hunter:
		return Config()->m_InfProbaHunter;
	case EPlayerClass::Bat:
		return Config()->m_InfProbaBat;
	case EPlayerClass::Ghost:
		return Config()->m_InfProbaGhost;
	case EPlayerClass::Spider:
		return Config()->m_InfProbaSpider;
	case EPlayerClass::Ghoul:
		return Config()->m_InfProbaGhoul;
	case EPlayerClass::Slug:
		return Config()->m_InfProbaSlug;
	case EPlayerClass::Voodoo:
		return Config()->m_InfProbaVoodoo;
	case EPlayerClass::Witch:
		return Config()->m_InfProbaWitch;
	case EPlayerClass::Undead:
		return Config()->m_InfProbaUndead;
	default:
		break;
	}

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "WARNING: Invalid GetPlayerClassProbability() call");
	return 0;
}

int CIcGameController::GetInfectedCount(EPlayerClass InfectedPlayerClass) const
{
	int Count = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CIcPlayer *pPlayer = GetPlayer(i);
		if(!pPlayer || !pPlayer->IsInGame())
			continue;

		if(!pPlayer->IsInfected())
			continue;

		if(InfectedPlayerClass != EPlayerClass::Invalid)
		{
			if(pPlayer->GetClass() != InfectedPlayerClass)
				continue;
		}

		Count++;
	}
	return Count;
}

bool CIcGameController::IsWinCheckEnabled() const
{
	return m_WinCheckEnabled.value_or(true);
}

void CIcGameController::SetWinCheckEnabled(bool Enabled)
{
	m_WinCheckEnabled = Enabled;
}

bool CIcGameController::GetVotesEnabled() const
{
	return m_VotesEnabled.value_or(!m_InfectedStarted);
}

void CIcGameController::SetVotesEnabled(bool Enabled)
{
	m_VotesEnabled = Enabled;
}

int CIcGameController::GetMinPlayers() const
{
	return m_RoundMinimumPlayers.value_or(Config()->m_InfMinPlayers);
}

void CIcGameController::SetRoundMinimumPlayers(int Number)
{
	m_RoundMinimumPlayers = Number;
}

int CIcGameController::GetMinimumInfected() const
{
	if(m_RoundMinimumInfected.has_value())
		return m_RoundMinimumInfected.value();

	int NumPlayers = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CIcPlayer *pPlayer = GetPlayer(i);
		if(!pPlayer || !pPlayer->m_IsInGame || pPlayer->IsSpectator())
		{
			continue;
		}
		++NumPlayers;
	}

	return GetMinimumInfectedForPlayers(NumPlayers);
}

void CIcGameController::SetRoundMinimumInfected(int Number)
{
	m_RoundMinimumInfected = Number;
}

void CIcGameController::ResetRoundMinimumInfected()
{
	m_RoundMinimumInfected.reset();
}

ERoundType CIcGameController::GetDefaultRoundType() const
{
	// Never return invalid
	ERoundType RoundType = fromString<ERoundType>(Config()->m_InfDefaultRoundType);
	if (RoundType == ERoundType::Invalid)
		RoundType = ERoundType::Normal;

	return RoundType;
}

ERoundType CIcGameController::GetRoundType() const
{
	return m_RoundType;
}

void CIcGameController::QueueRoundType(ERoundType RoundType)
{
	dbg_msg("controller", "Queued round: %s", toString(RoundType));
	m_QueuedRoundType = RoundType;
}

CLASS_AVAILABILITY CIcGameController::GetPlayerClassAvailability(EPlayerClass PlayerClass, const CIcPlayer *pForPlayer) const
{
	if(!GetPlayerClassEnabled(PlayerClass))
		return CLASS_AVAILABILITY::DISABLED;

	uint32_t ActivePlayerCount = Server()->GetActivePlayerCount();
	uint32_t MinPlayersForClass = GetMinPlayersForClass(PlayerClass);
	if (ActivePlayerCount < MinPlayersForClass)
		return CLASS_AVAILABILITY::NEED_MORE_PLAYERS;

	int nbSupport = 0;
	int nbDefender = 0;
	icArray<int, NB_PLAYERCLASS> nbClass;
	nbClass.Resize(NB_PLAYERCLASS);

	CIcPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		const EPlayerClass AnotherPlayerClass = Iter.Player()->GetClass();
		const int Index = static_cast<int>(AnotherPlayerClass);
		if (IsDefenderClass(AnotherPlayerClass))
			nbDefender++;
		if (IsSupportClass(AnotherPlayerClass))
			nbSupport++;
		nbClass[Index]++;
	}

	if(IsDefenderClass(PlayerClass) && (nbDefender >= Config()->m_InfDefenderLimit))
		return CLASS_AVAILABILITY::LIMIT_EXCEEDED;

	if(IsSupportClass(PlayerClass) && (nbSupport >= Config()->m_InfSupportLimit))
		return CLASS_AVAILABILITY::LIMIT_EXCEEDED;

	int ClassLimit = GetClassPlayerLimit(PlayerClass);
	if(GetRoundType() == ERoundType::Survival)
	{
		static constexpr EPlayerClass EarlyClasses[] = {
			EPlayerClass::Medic,
			EPlayerClass::Mercenary,
			EPlayerClass::Sniper,
		};

		icArray<EPlayerClass, NB_HUMANCLASS> EnabledEarlyClasses;

		uint32_t EnabledHumansClasses = 0;
		for(EPlayerClass HumanClass : AllHumanClasses)
		{
			if(GetPlayerClassEnabled(HumanClass))
			{
				EnabledHumansClasses++;
			}
			const auto FoundEarly = std::find(std::cbegin(EarlyClasses), std::cend(EarlyClasses), HumanClass);
			if (FoundEarly != std::cend(EarlyClasses))
			{
				EnabledEarlyClasses.Add(HumanClass);
			}
		}

		if(EnabledHumansClasses == 0)
		{
			dbg_msg("server/ic", "Error: No human class enabled");
			EnabledHumansClasses = 1;
		}

		ClassLimit = std::ceil(ActivePlayerCount / static_cast<float>(EnabledHumansClasses));
		uint32_t ExtraPlayers = ActivePlayerCount % EnabledHumansClasses;
		if((ClassLimit > 1) && ExtraPlayers)
		{
			if (ExtraPlayers <= EnabledEarlyClasses.Size())
			{
				if(!EnabledEarlyClasses.Contains(PlayerClass))
					ClassLimit -=1;
			}
		}
	}

	if(nbClass[static_cast<int>(PlayerClass)] >= ClassLimit)
		return CLASS_AVAILABILITY::LIMIT_EXCEEDED;

	if(PlayerClass == EPlayerClass::Hero)
	{
		if (m_HeroFlagPositions.size() == 0)
		{
			return CLASS_AVAILABILITY::DISABLED;
		}
	}
	
	return CLASS_AVAILABILITY::AVAILABLE;
}

bool CIcGameController::CanVote()
{
	return GetVotesEnabled();
}

void CIcGameController::OnPlayerVoteCommand(int ClientId, int Vote)
{
	CIcPlayer *pPlayer = GetPlayer(ClientId);
	if(!pPlayer)
	{
		return;
	}

	if(pPlayer->m_PlayerFlags & PLAYERFLAG_SCOREBOARD)
	{
		EPlayerScoreMode ScoreMode = pPlayer->GetScoreMode();
		if(Vote < 0)
		{
			// F3, next mode
			int ScoreModeValue = static_cast<int>(ScoreMode);
			ScoreModeValue++;
			ScoreMode = static_cast<EPlayerScoreMode>(ScoreModeValue);
			if(ScoreMode == EPlayerScoreMode::Count)
			{
				ScoreMode = static_cast<EPlayerScoreMode>(0);
			}
		}
		else
		{
			// F4, prev mode
			int ScoreModeValue = static_cast<int>(ScoreMode);
			if(ScoreModeValue == 0)
			{
				ScoreMode = static_cast<EPlayerScoreMode>(static_cast<int>(EPlayerScoreMode::Count) - 1);
			}
			else
			{
				ScoreModeValue--;
				ScoreMode = static_cast<EPlayerScoreMode>(ScoreModeValue);
			}
		}

		pPlayer->SetScoreMode(ScoreMode);
	}
	else
	{
		pPlayer->SetHookProtection(!pPlayer->HookProtectionEnabled(), false);
	}
}

void CIcGameController::OnPlayerClassChanged(CIcPlayer *pPlayer)
{
	SetPlayerInfected(pPlayer->GetCid(), pPlayer->IsInfected());
	Server()->ExpireServerInfo();
}
