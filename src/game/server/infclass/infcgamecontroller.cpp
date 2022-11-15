/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.				*/
#include "infcgamecontroller.h"

#include "events-director.h"

#include "game/infclass/classes.h"

#include <game/server/gamecontext.h>
#include <game/server/infclass/classes/infcplayerclass.h>
#include <game/server/infclass/damage_type.h>
#include <game/server/infclass/death_context.h>
#include <game/server/infclass/entities/flyingpoint.h>
#include <game/server/infclass/entities/infccharacter.h>
#include <game/server/infclass/entities/ic-pickup.h>
#include <game/server/infclass/infcplayer.h>

#include <base/tl/ic_array.h>
#include <engine/shared/config.h>
#include <engine/server/mapconverter.h>
#include <engine/server/roundstatistics.h>
#include <engine/shared/network.h>
#include <game/mapitems.h>
#include <time.h>

#include <engine/message.h>
#include <game/generated/protocol.h>
#include <game/server/infclass/classes/humans/human.h>
#include <game/server/infclass/classes/infected/infected.h>

#include <array>
#include <algorithm>
#include <iostream>
#include <map>

const int InfClassModeSpecialSkip = 0x100;

static const char *gs_aRoundNames[] = {
	"classic",
	"fun",
	"fast",
	"invalid",
};

const char *toString(ROUND_TYPE RoundType)
{
	int Index = static_cast<int>(RoundType);
	if((Index < 0) || (Index >= std::size(gs_aRoundNames)))
	{
		dbg_msg("Controller", "toString(ROUND_TYPE %d): out of range!", Index);
		return "invalid";
	}
	return gs_aRoundNames[Index];
}

ROUND_TYPE GetTypeByName(const char *pName)
{
	for(int i = 0; i < static_cast<int>(ROUND_TYPE::COUNT); ++i)
	{
		if(str_comp(pName, gs_aRoundNames[i]) == 0)
		{
			return static_cast<ROUND_TYPE>(i);
		}
	}

	return ROUND_TYPE::INVALID;
}

enum class ROUND_CANCELATION_REASON
{
	INVALID,
	ALL_INFECTED_LEFT_THE_GAME,
	EVERYONE_INFECTED_BY_THE_GAME,
};

enum class ROUND_END_REASON
{
	FINISHED,
	CANCELED,
};

struct InfclassPlayerPersistantData : public CGameContext::CPersistentClientData
{
	bool m_AntiPing = false;
	PLAYERCLASS m_PreferredClass = PLAYERCLASS_INVALID;
	int m_LastInfectionTime = 0;
};

CInfClassGameController::CInfClassGameController(class CGameContext *pGameServer)
: IGameController(pGameServer), m_Teams(pGameServer)
{
	m_pGameType = "InfClassR";

	m_Teams.m_Core.m_IsInfclass = true;
	
	m_GrowingMap = 0;

	//Get zones
	m_ZoneHandle_icDamage = GameServer()->Collision()->GetZoneHandle("icDamage");
	m_ZoneHandle_icTeleport = GameServer()->Collision()->GetZoneHandle("icTele");
	m_ZoneHandle_icBonus = GameServer()->Collision()->GetZoneHandle("icBonus");

	m_ExplosionStarted = false;
	m_MapWidth = GameServer()->Collision()->GetWidth();
	m_MapHeight = GameServer()->Collision()->GetHeight();
	m_GrowingMap = new int[m_MapWidth*m_MapHeight];

	m_InfectedStarted = false;
	
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

	ReservePlayerOwnSnapItems();
}

CInfClassGameController::~CInfClassGameController()
{
	FreePlayerOwnSnapItems();

	if(m_GrowingMap) delete[] m_GrowingMap;
}

void CInfClassGameController::IncreaseCurrentRoundCounter()
{
	IGameController::IncreaseCurrentRoundCounter();

	m_MoreRoundsSuggested = false;

	MaybeSuggestMoreRounds();
}

void CInfClassGameController::OnPlayerDisconnect(CPlayer *pBasePlayer, int Type, const char *pReason)
{
	CInfClassPlayer *pPlayer = CInfClassPlayer::GetInstance(pBasePlayer);
	if(Type == CLIENTDROPTYPE_BAN) return;
	if(Type == CLIENTDROPTYPE_KICK) return;
	if(Type == CLIENTDROPTYPE_SHUTDOWN) return;	

	if(pPlayer && pPlayer->IsActuallyZombie() && m_InfectedStarted)
	{
		int NumHumans;
		int NumInfected;
		GetPlayerCounter(pPlayer->GetCID(), NumHumans, NumInfected);
		const int NumPlayers = NumHumans + NumInfected;
		const int NumFirstInfected = GetMinimumInfectedForPlayers(NumPlayers);
		
		if(NumInfected < NumFirstInfected)
		{
			Server()->Ban(pPlayer->GetCID(), 60 * Config()->m_InfLeaverBanTime, "Leaver");
		}
	}

	IGameController::OnPlayerDisconnect(pPlayer, Type, pReason);
}

void CInfClassGameController::DoPlayerInfection(CInfClassPlayer *pPlayer, CInfClassPlayer *pInfectiousPlayer, int PreviousClass)
{
	PLAYERCLASS c = ChooseInfectedClass(pPlayer);
	pPlayer->SetClass(c);

	FallInLoveIfInfectedEarly(pPlayer->GetCharacter());

	if(!pInfectiousPlayer)
	{
		if(pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsAlive())
		{
			// Still send a kill message to notify other players about the infection
			GameServer()->SendKillMessage(pPlayer->GetCID(), pPlayer->GetCID(), WEAPON_WORLD, 0);
			GameServer()->CreateSound(pPlayer->GetCharacter()->m_Pos, SOUND_PLAYER_DIE);
		}

		return;
	}

	const int InfectedByCID = pInfectiousPlayer->GetCID();
	if(!IsZombieClass(PreviousClass) && (pPlayer != pInfectiousPlayer))
	{
		if(pInfectiousPlayer->IsHuman())
		{
			GameServer()->SendChatTarget_Localization(InfectedByCID, CHATCATEGORY_SCORE,
				_("You have infected {str:VictimName}, shame on you!"),
				"VictimName", Server()->ClientName(pPlayer->GetCID()),
				nullptr);
			GameServer()->SendEmoticon(pInfectiousPlayer->GetCID(), EMOTICON_SORRY);
			CInfClassCharacter *pGuiltyCharacter = pInfectiousPlayer->GetCharacter();
			if(pGuiltyCharacter)
			{
				const float GuiltyPlayerFreeze = 3;
				pGuiltyCharacter->Freeze(GuiltyPlayerFreeze, -1, FREEZEREASON_INFECTION);
				pGuiltyCharacter->SetEmote(EMOTE_PAIN, Server()->Tick() + Server()->TickSpeed() * GuiltyPlayerFreeze);
			}
		}
		else
		{
			GameServer()->SendChatTarget_Localization(pPlayer->GetCID(), CHATCATEGORY_INFECTED,
				_("You have been infected by {str:KillerName}"),
				"KillerName", Server()->ClientName(pInfectiousPlayer->GetCID()),
				nullptr);
			GameServer()->SendChatTarget_Localization(InfectedByCID, CHATCATEGORY_SCORE,
				_("You have infected {str:VictimName}, +3 points"),
				"VictimName", Server()->ClientName(pPlayer->GetCID()),
				nullptr);
			Server()->RoundStatistics()->OnScoreEvent(InfectedByCID, SCOREEVENT_INFECTION,
				pInfectiousPlayer->GetClass(), Server()->ClientName(InfectedByCID), Console());
			GameServer()->SendScoreSound(InfectedByCID);
		}
	}

	//Search for hook
	for(TEntityPtr<CInfClassCharacter> pHook = GameWorld()->FindFirst<CInfClassCharacter>(); pHook; ++pHook)
	{
		if(
			pHook->GetPlayer() &&
			pHook->GetHookedPlayer() == pPlayer->GetCID() &&
			pHook->GetCID() != InfectedByCID
		)
		{
			Server()->RoundStatistics()->OnScoreEvent(pHook->GetCID(), SCOREEVENT_HELP_HOOK_INFECTION, pHook->GetPlayerClass(), Server()->ClientName(pHook->GetCID()), Console());
			GameServer()->SendScoreSound(pHook->GetCID());
		}
	}
}

void CInfClassGameController::OnHeroFlagCollected(int ClientID)
{
	GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The Hero found the flag!"), NULL);
	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);

	int Tick = Server()->Tick();
	if(Tick < m_HeroGiftTick)
		return;

	m_HeroGiftTick = Tick + GetHeroFlagCooldown() * Server()->TickSpeed();
}

float CInfClassGameController::GetHeroFlagCooldown() const
{
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

bool CInfClassGameController::OnEntity(const char* pName, vec2 Pivot, vec2 P0, vec2 P1, vec2 P2, vec2 P3, int PosEnv)
{
	bool res = IGameController::OnEntity(pName, Pivot, P0, P1, P2, P3, PosEnv);
	vec2 Pos = (P0 + P1 + P2 + P3)/4.0f;

	CInfCEntity *pNewEntity = nullptr;
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
		CIcPickup *p = new CIcPickup(GameServer(), IC_PICKUP_TYPE::HEALTH, Pos);
		p->SetRespawnInterval(15);
		p->Spawn();
		pNewEntity = p;
	}
	else if(str_comp(pName, "armor") == 0)
	{
		CIcPickup *p = new CIcPickup(GameServer(), IC_PICKUP_TYPE::ARMOR, Pos);
		p->SetRespawnInterval(15);
		p->Spawn();
		pNewEntity = p;
	}

	if(pNewEntity && (PosEnv >= 0))
	{
		pNewEntity->SetAnimatedPos(Pivot, Pos - Pivot, PosEnv);
	}

	return res;
}

void CInfClassGameController::HandleCharacterTiles(CInfClassCharacter *pCharacter)
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
		pCharacter->Die(pCharacter->GetCID(), DAMAGE_TYPE::DEATH_TILE);
	}
	else if(pCharacter->GetPlayerClass() != PLAYERCLASS_UNDEAD && Indices.Contains(ZONE_DAMAGE_DEATH_NOUNDEAD))
	{
		pCharacter->Die(pCharacter->GetCID(), DAMAGE_TYPE::DEATH_TILE);
	}
	else if(pCharacter->IsZombie() && Indices.Contains(ZONE_DAMAGE_DEATH_INFECTED))
	{
		pCharacter->Die(pCharacter->GetCID(), DAMAGE_TYPE::DEATH_TILE);
	}
	else if(pCharacter->IsAlive() && Indices.Contains(ZONE_DAMAGE_INFECTION))
	{
		pCharacter->OnCharacterInInfectionZone();
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

	int BonusZoneIndex = GetBonusZoneValueAt(pCharacter->GetPos());
	if(BonusZoneIndex == ZONE_BONUS_BONUS)
	{
		pCharacter->OnCharacterInBonusZoneTick();
	}
}

void CInfClassGameController::HandleLastHookers()
{
	const int CurrentTick = Server()->Tick();
	icArray<ClientsArray, MAX_CLIENTS> CharacterHookedBy;
	CharacterHookedBy.Resize(MAX_CLIENTS);

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CInfClassCharacter *pCharacter = GetCharacter(i);
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

	for(int TargetCID = 0; TargetCID < CharacterHookedBy.Size(); ++TargetCID)
	{
		ClientsArray &HookedBy = CharacterHookedBy[TargetCID];
		if(HookedBy.IsEmpty())
		{
			continue;
		}
		CInfClassCharacter *pHookedCharacter = GetCharacter(TargetCID);
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

int64_t CInfClassGameController::GetBlindCharactersMask(int ExcludeCID) const
{
	int64_t Mask = 0;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == ExcludeCID)
			continue;

		const CInfClassCharacter *pTarget = GetCharacter(i);
		if(!pTarget)
			continue;
		if(!pTarget->IsBlind())
			continue;

		Mask |= 1LL << i;
	}

	return Mask;
}

bool CInfClassGameController::HumanWallAllowedInPos(const vec2 &Pos) const
{
	const float Radius = 32.0f;

	if (GetDamageZoneValueAt(Pos) == ZONE_DAMAGE_INFECTION)
		return false;

	{ // Check for spawns
		int Type = 0; // InfectedSpawn

		// get spawn point
		for(int i = 0; i < m_SpawnPoints[Type].size(); i++)
		{
			if(distance(Pos, m_SpawnPoints[Type][i]) <= Radius)
			{
				return false;
			}
		}
	}

	return true;
}

int CInfClassGameController::GetZoneValueAt(int ZoneHandle, const vec2 &Pos, ZoneData *pData) const
{
	return GameServer()->Collision()->GetZoneValueAt(ZoneHandle, Pos, pData);
}

int CInfClassGameController::GetDamageZoneValueAt(const vec2 &Pos, ZoneData *pData) const
{
	return GetZoneValueAt(m_ZoneHandle_icDamage, Pos, pData);
}

int CInfClassGameController::GetBonusZoneValueAt(const vec2 &Pos, ZoneData *pData) const
{
	return GetZoneValueAt(m_ZoneHandle_icBonus, Pos, pData);
}

void CInfClassGameController::CreateExplosion(const vec2 &Pos, int Owner, DAMAGE_TYPE DamageType, float DamageFactor)
{
	int Weapon = WEAPON_WORLD;
	GameServer()->CreateExplosion(Pos, Owner, Weapon);

	if(DamageFactor != 0)
	{
		// deal damage
		CInfClassCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = GameWorld()->FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			if(!Config()->m_InfShockwaveAffectHumans)
			{
				if(apEnts[i]->GetCID() == Owner)
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
void CInfClassGameController::CreateExplosionDisk(vec2 Pos, float InnerRadius, float DamageRadius, int Damage, float Force, int Owner, DAMAGE_TYPE DamageType)
{
	CreateExplosionDiskGfx(Pos, InnerRadius, DamageRadius, Owner);

	if(Damage > 0)
	{
		// deal damage
		CInfClassCharacter *apEnts[MAX_CLIENTS];
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

void CInfClassGameController::CreateExplosionDiskGfx(vec2 Pos, float InnerRadius, float DamageRadius, int Owner)
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

void CInfClassGameController::SendHammerDot(const vec2 &Pos, int SnapID)
{
	CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, SnapID, sizeof(CNetObj_Projectile)));

	if(!pObj)
		return;;

	pObj->m_X = Pos.x;
	pObj->m_Y = Pos.y;
	pObj->m_VelX = 0;
	pObj->m_VelY = 0;
	pObj->m_Type = WEAPON_HAMMER;
	pObj->m_StartTick = Server()->Tick();
}

void CInfClassGameController::ResetFinalExplosion()
{
	m_ExplosionStarted = false;
	
	for(int j=0; j<m_MapHeight; j++)
	{
		for(int i=0; i<m_MapWidth; i++)
		{
			if(!(m_GrowingMap[j*m_MapWidth+i] & 4))
			{
				m_GrowingMap[j*m_MapWidth+i] = 1;
			}
		}
	}
}

void CInfClassGameController::SaveRoundRules()
{
}

int CInfClassGameController::GetRoundTick() const
{
	return Server()->Tick() - m_RoundStartTick;
}

bool CInfClassGameController::IsZombieClass(int PlayerClass)
{
	return (PlayerClass > START_INFECTEDCLASS) && (PlayerClass < END_INFECTEDCLASS);
}

bool CInfClassGameController::IsDefenderClass(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_ENGINEER:
		case PLAYERCLASS_SOLDIER:
		case PLAYERCLASS_SCIENTIST:
		case PLAYERCLASS_BIOLOGIST:
		case PLAYERCLASS_LOOPER:
			return true;
		default:
			return false;
	}
}

bool CInfClassGameController::IsSupportClass(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_NINJA:
		case PLAYERCLASS_MERCENARY:
		case PLAYERCLASS_SNIPER:
			return true;
		default:
			return false;
	}
}

PLAYERCLASS CInfClassGameController::GetClassByName(const char *pClassName, bool *pOk)
{
	struct ExtraName
	{
		ExtraName(const char *pN, PLAYERCLASS Class) :
			pName(pN), PlayerClass(Class)
		{
		}

		const char *pName = nullptr;
		PLAYERCLASS PlayerClass = PLAYERCLASS_INVALID;
	};

	static const ExtraName extraNames[] = {
		ExtraName("bio", PLAYERCLASS_BIOLOGIST),
		ExtraName("bios", PLAYERCLASS_BIOLOGIST),
		ExtraName("engi", PLAYERCLASS_ENGINEER),
		ExtraName("merc", PLAYERCLASS_MERCENARY),
		ExtraName("mercs", PLAYERCLASS_MERCENARY),
		ExtraName("sci", PLAYERCLASS_SCIENTIST),
		ExtraName("random", PLAYERCLASS_RANDOM),
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

	for (int PlayerClass = 0; PlayerClass < NB_PLAYERCLASS; ++PlayerClass)
	{
		const char *pSingularName = CInfClassGameController::GetClassName(PlayerClass);
		const char *pPluralName = CInfClassGameController::GetClassPluralName(PlayerClass);
		if((str_comp(pClassName, pSingularName) == 0) || (str_comp(pClassName, pPluralName) == 0)) {
			return static_cast<PLAYERCLASS>(PlayerClass);
		}
	}

	if(pOk)
	{
		*pOk = false;
	}
	return PLAYERCLASS_INVALID;
}

const char *CInfClassGameController::GetClassName(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_NONE:
			return "none";

		case PLAYERCLASS_MERCENARY:
			return "mercenary";
		case PLAYERCLASS_MEDIC:
			return "medic";
		case PLAYERCLASS_HERO:
			return "hero";
		case PLAYERCLASS_ENGINEER:
			return "engineer";
		case PLAYERCLASS_SOLDIER:
			return "soldier";
		case PLAYERCLASS_NINJA:
			return "ninja";
		case PLAYERCLASS_SNIPER:
			return "sniper";
		case PLAYERCLASS_SCIENTIST:
			return "scientist";
		case PLAYERCLASS_BIOLOGIST:
			return "biologist";
		case PLAYERCLASS_LOOPER:
			return "looper";

		case PLAYERCLASS_SMOKER:
			return "smoker";
		case PLAYERCLASS_BOOMER:
			return "boomer";
		case PLAYERCLASS_HUNTER:
			return "hunter";
		case PLAYERCLASS_BAT:
			return "bat";
		case PLAYERCLASS_GHOST:
			return "ghost";
		case PLAYERCLASS_SPIDER:
			return "spider";
		case PLAYERCLASS_GHOUL:
			return "ghoul";
		case PLAYERCLASS_SLUG:
			return "slug";
		case PLAYERCLASS_VOODOO:
			return "voodoo";
		case PLAYERCLASS_WITCH:
			return "witch";
		case PLAYERCLASS_UNDEAD:
			return "undead";

		default:
			return "unknown";
	}
}

const char *CInfClassGameController::GetClassPluralName(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_MERCENARY:
			return "mercenaries";
		case PLAYERCLASS_MEDIC:
			return "medics";
		case PLAYERCLASS_HERO:
			return "heroes";
		case PLAYERCLASS_ENGINEER:
			return "engineers";
		case PLAYERCLASS_SOLDIER:
			return "soldiers";
		case PLAYERCLASS_NINJA:
			return "ninjas";
		case PLAYERCLASS_SNIPER:
			return "snipers";
		case PLAYERCLASS_SCIENTIST:
			return "scientists";
		case PLAYERCLASS_BIOLOGIST:
			return "biologists";
		case PLAYERCLASS_LOOPER:
			return "loopers";

		case PLAYERCLASS_SMOKER:
			return "smokers";
		case PLAYERCLASS_BOOMER:
			return "boomers";
		case PLAYERCLASS_HUNTER:
			return "hunters";
		case PLAYERCLASS_BAT:
			return "bats";
		case PLAYERCLASS_GHOST:
			return "ghosts";
		case PLAYERCLASS_SPIDER:
			return "spiders";
		case PLAYERCLASS_GHOUL:
			return "ghouls";
		case PLAYERCLASS_SLUG:
			return "slugs";
		case PLAYERCLASS_VOODOO:
			return "voodoos";
		case PLAYERCLASS_WITCH:
			return "witches";
		case PLAYERCLASS_UNDEAD:
			return "undeads";

		default:
			return "unknown";
	}
}

const char *CInfClassGameController::GetClassDisplayName(int PlayerClass, const char *pDefaultText)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_MERCENARY:
			return _("Mercenary");
		case PLAYERCLASS_MEDIC:
			return _("Medic");
		case PLAYERCLASS_HERO:
			return _("Hero");
		case PLAYERCLASS_ENGINEER:
			return _("Engineer");
		case PLAYERCLASS_SOLDIER:
			return _("Soldier");
		case PLAYERCLASS_NINJA:
			return _("Ninja");
		case PLAYERCLASS_SNIPER:
			return _("Sniper");
		case PLAYERCLASS_SCIENTIST:
			return _("Scientist");
		case PLAYERCLASS_BIOLOGIST:
			return _("Biologist");
		case PLAYERCLASS_LOOPER:
			return _("Looper");

		case PLAYERCLASS_SMOKER:
			return _("Smoker");
		case PLAYERCLASS_BOOMER:
			return _("Boomer");
		case PLAYERCLASS_HUNTER:
			return _("Hunter");
		case PLAYERCLASS_BAT:
			return _("Bat");
		case PLAYERCLASS_GHOST:
			return _("Ghost");
		case PLAYERCLASS_SPIDER:
			return _("Spider");
		case PLAYERCLASS_GHOUL:
			return _("Ghoul");
		case PLAYERCLASS_SLUG:
			return _("Slug");
		case PLAYERCLASS_VOODOO:
			return _("Voodoo");
		case PLAYERCLASS_WITCH:
			return _("Witch");
		case PLAYERCLASS_UNDEAD:
			return _("Undead");

		case PLAYERCLASS_NONE:
		default:
			return pDefaultText ? pDefaultText : _("Unknown class");
	}
}

const char *CInfClassGameController::GetClanForClass(int PlayerClass, const char *pDefaultText)
{
	switch (PlayerClass)
	{
		default:
			return GetClassDisplayName(PlayerClass, pDefaultText);
	}
}

const char *CInfClassGameController::GetClassPluralDisplayName(int PlayerClass)
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_MERCENARY:
			return _("Mercenaries");
		case PLAYERCLASS_MEDIC:
			return _("Medics");
		case PLAYERCLASS_HERO:
			return _("Heroes");
		case PLAYERCLASS_ENGINEER:
			return _("Engineers");
		case PLAYERCLASS_SOLDIER:
			return _("Soldiers");
		case PLAYERCLASS_NINJA:
			return _("Ninjas");
		case PLAYERCLASS_SNIPER:
			return _("Snipers");
		case PLAYERCLASS_SCIENTIST:
			return _("Scientists");
		case PLAYERCLASS_BIOLOGIST:
			return _("Biologists");
		case PLAYERCLASS_LOOPER:
			return _("Loopers");

		case PLAYERCLASS_SMOKER:
			return _("Smokers");
		case PLAYERCLASS_BOOMER:
			return _("Boomers");
		case PLAYERCLASS_HUNTER:
			return _("Hunters");
		case PLAYERCLASS_BAT:
			return _("Bats");
		case PLAYERCLASS_GHOST:
			return _("Ghosts");
		case PLAYERCLASS_SPIDER:
			return _("Spiders");
		case PLAYERCLASS_GHOUL:
			return _("Ghouls");
		case PLAYERCLASS_SLUG:
			return _("Slugs");
		case PLAYERCLASS_VOODOO:
			return _("Voodoos");
		case PLAYERCLASS_WITCH:
			return _("Witches");
		case PLAYERCLASS_UNDEAD:
			return _("Undeads");

		default:
			return _("Unknowns");
	}
}

PLAYERCLASS CInfClassGameController::MenuClassToPlayerClass(int MenuClass)
{
	PLAYERCLASS PlayerClass = PLAYERCLASS_INVALID;
	switch(MenuClass)
	{
		case CMapConverter::MENUCLASS_MEDIC:
			PlayerClass = PLAYERCLASS_MEDIC;
			break;
		case CMapConverter::MENUCLASS_HERO:
			PlayerClass = PLAYERCLASS_HERO;
			break;
		case CMapConverter::MENUCLASS_NINJA:
			PlayerClass = PLAYERCLASS_NINJA;
			break;
		case CMapConverter::MENUCLASS_MERCENARY:
			PlayerClass = PLAYERCLASS_MERCENARY;
			break;
		case CMapConverter::MENUCLASS_SNIPER:
			PlayerClass = PLAYERCLASS_SNIPER;
			break;
		case CMapConverter::MENUCLASS_RANDOM:
			PlayerClass = PLAYERCLASS_NONE;
			break;
		case CMapConverter::MENUCLASS_ENGINEER:
			PlayerClass = PLAYERCLASS_ENGINEER;
			break;
		case CMapConverter::MENUCLASS_SOLDIER:
			PlayerClass = PLAYERCLASS_SOLDIER;
			break;
		case CMapConverter::MENUCLASS_SCIENTIST:
			PlayerClass = PLAYERCLASS_SCIENTIST;
			break;
		case CMapConverter::MENUCLASS_BIOLOGIST:
			PlayerClass = PLAYERCLASS_BIOLOGIST;
			break;
		case CMapConverter::MENUCLASS_LOOPER:
			PlayerClass = PLAYERCLASS_LOOPER;
			break;
	}

	return PlayerClass;
}

int CInfClassGameController::DamageTypeToWeapon(DAMAGE_TYPE DamageType, TAKEDAMAGEMODE *pMode)
{
	int Weapon = WEAPON_GAME;
	TAKEDAMAGEMODE HelperMode;
	TAKEDAMAGEMODE &Mode = pMode ? *pMode : HelperMode;

	Mode = TAKEDAMAGEMODE::NOINFECTION;

	switch(DamageType)
	{
	case DAMAGE_TYPE::INVALID:
	case DAMAGE_TYPE::UNUSED1:
		break;
	case DAMAGE_TYPE::NO_DAMAGE:
		break;

	case DAMAGE_TYPE::HAMMER:
	case DAMAGE_TYPE::BITE:
	case DAMAGE_TYPE::LASER_WALL:
	case DAMAGE_TYPE::BIOLOGIST_MINE:
	case DAMAGE_TYPE::TURRET_DESTRUCTION:
	case DAMAGE_TYPE::TURRET_LASER:
	case DAMAGE_TYPE::TURRET_PLASMA:
	case DAMAGE_TYPE::WHITE_HOLE:
	case DAMAGE_TYPE::SLUG_SLIME:
		Weapon = WEAPON_HAMMER;
		break;
	case DAMAGE_TYPE::SOLDIER_BOMB:
	case DAMAGE_TYPE::MERCENARY_BOMB:
	case DAMAGE_TYPE::SCIENTIST_MINE:
	case DAMAGE_TYPE::SCIENTIST_TELEPORT:
		Mode = TAKEDAMAGEMODE::SELFHARM;
		Weapon = WEAPON_HAMMER;
		break;
	case DAMAGE_TYPE::INFECTION_HAMMER:
	case DAMAGE_TYPE::BOOMER_EXPLOSION:
		Mode = TAKEDAMAGEMODE::INFECTION;
		Weapon = WEAPON_HAMMER;
		break;
	case DAMAGE_TYPE::GUN:
	case DAMAGE_TYPE::MERCENARY_GUN:
		Weapon = WEAPON_GUN;
		break;
	case DAMAGE_TYPE::SHOTGUN:
	case DAMAGE_TYPE::MEDIC_SHOTGUN:
	case DAMAGE_TYPE::BIOLOGIST_SHOTGUN:
		Weapon = WEAPON_SHOTGUN;
		break;
	case DAMAGE_TYPE::GRENADE:
	case DAMAGE_TYPE::STUNNING_GRENADE:
	case DAMAGE_TYPE::MERCENARY_GRENADE:
		Weapon = WEAPON_GRENADE;
		break;
	case DAMAGE_TYPE::LASER:
	case DAMAGE_TYPE::SNIPER_RIFLE:
	case DAMAGE_TYPE::SCIENTIST_LASER:
	case DAMAGE_TYPE::LOOPER_LASER:
		Weapon = WEAPON_LASER;
		break;
	case DAMAGE_TYPE::NINJA:
	case DAMAGE_TYPE::DRYING_HOOK:
		Weapon = WEAPON_NINJA;
		break;

	case DAMAGE_TYPE::DEATH_TILE:
	case DAMAGE_TYPE::INFECTION_TILE:
	case DAMAGE_TYPE::DAMAGE_TILE:
		Weapon = WEAPON_WORLD;
		break;
	case DAMAGE_TYPE::GAME:
		Weapon = WEAPON_GAME;
		break;
	case DAMAGE_TYPE::KILL_COMMAND:
		Weapon = WEAPON_SELF;
		break;
	case DAMAGE_TYPE::GAME_FINAL_EXPLOSION:
	case DAMAGE_TYPE::GAME_INFECTION:
		// This is how the infection world work
		Weapon = WEAPON_WORLD;
		break;
	case DAMAGE_TYPE::MEDIC_REVIVAL:
		Weapon = WEAPON_LASER;
		Mode = TAKEDAMAGEMODE::SELFHARM;
		break;
	}

	return Weapon;
}

void CInfClassGameController::RegisterChatCommands(IConsole *pConsole)
{
	pConsole->Register("restore_client_name", "i[clientid]", CFGFLAG_SERVER, ConRestoreClientName, this, "Set the name of a player");
	pConsole->Register("set_client_name", "i[clientid] r[name]", CFGFLAG_SERVER, ConSetClientName, this, "Set the name of a player (and also lock it)");
	pConsole->Register("lock_client_name", "i[clientid] i[lock]", CFGFLAG_SERVER, ConLockClientName, this, "Set the name of a player");

	pConsole->Register("set_health_armor", "i[clientid] i[health] i[armor]", CFGFLAG_SERVER, ConSetHealthArmor, this, "Set the player health/armor");
	pConsole->Register("set_invincible", "i[clientid] i[level]", CFGFLAG_SERVER, ConSetInvincible, this, "Set the player invincibility level (1 inv to damage, 2 inv to inf, 3 inv to death tiles");
	pConsole->Register("set_hook_protection", "i[clientid] i[protection]", CFGFLAG_SERVER, ConSetHookProtection, this, "Enable the player hook protection (0 disabled, 1 enabled)");

	pConsole->Register("inf_set_class", "i[clientid] s[classname]", CFGFLAG_SERVER, ConSetClass, this, "Set the class of a player");
	pConsole->Register("queue_round", "s[type]", CFGFLAG_SERVER, ConQueueSpecialRound, this, "Start a special round");

	pConsole->Register("start_fun_round", "", CFGFLAG_SERVER, ConStartFunRound, this, "Start fun round");
	pConsole->Register("start_special_fun_round", "s[classname] s[classname] ?s[more classes]", CFGFLAG_SERVER, ConStartSpecialFunRound, this, "Start fun round");
	pConsole->Register("clear_fun_rounds", "", CFGFLAG_SERVER, ConClearFunRounds, this, "Start fun round");
	pConsole->Register("add_fun_round", "s[classname] s[classname] ?s[more classes]", CFGFLAG_SERVER, ConAddFunRound, this, "Start fun round");

	pConsole->Register("start_fast_round", "", CFGFLAG_SERVER, ConStartFastRound, this, "Start a faster gameplay round");
	pConsole->Register("queue_fast_round", "", CFGFLAG_SERVER, ConQueueFastRound, this, "Queue a faster gameplay round");
	pConsole->Register("queue_fun_round", "", CFGFLAG_SERVER, ConQueueFunRound, this, "Queue a fun gameplay round");
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

void CInfClassGameController::ConRestoreClientName(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;

	int PlayerID = pResult->GetInteger(0);

	CInfClassPlayer *pPlayer = pSelf->GetPlayer(PlayerID);
	if(!pPlayer)
	{
		return;
	}

	pPlayer->m_ClientNameLocked = true;
	pSelf->Server()->SetClientName(PlayerID, pPlayer->GetOriginalName());
}

void CInfClassGameController::ConSetClientName(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;

	int PlayerID = pResult->GetInteger(0);
	const char *pNewName = pResult->GetString(1);

	if(pResult->NumArguments() != 2)
	{
		return;
	}

	CInfClassPlayer *pPlayer = pSelf->GetPlayer(PlayerID);
	if(!pPlayer)
	{
		return;
	}

	pPlayer->m_ClientNameLocked = true;
	pSelf->Server()->SetClientName(PlayerID, pNewName);
}

void CInfClassGameController::ConLockClientName(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;

	int PlayerID = pResult->GetInteger(0);
	int Lock = pResult->GetInteger(1);

	if(pResult->NumArguments() != 2)
	{
		return;
	}

	CInfClassPlayer *pPlayer = pSelf->GetPlayer(PlayerID);
	if(!pPlayer)
	{
		return;
	}

	pPlayer->m_ClientNameLocked = Lock != 0;
}

void CInfClassGameController::ConPreferClass(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	int ClientID = pResult->GetClientID();

	const char *pClassName = pResult->GetString(0);
	pSelf->SetPreferredClass(ClientID, pClassName);
}

void CInfClassGameController::ConAlwaysRandom(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	int ClientID = pResult->GetClientID();

	bool Random = pResult->GetInteger(0) > 0;
	pSelf->SetPreferredClass(ClientID, Random ? PLAYERCLASS_RANDOM : PLAYERCLASS_INVALID);
}

void CInfClassGameController::SetPreferredClass(int ClientID, const char *pClassName)
{
	bool Ok = false;
	PLAYERCLASS PlayerClass = GetClassByName(pClassName, &Ok);

	if(!Ok)
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT,
			_("Unable to set preferred class: Invalid class name"), nullptr);
		return;
	}
	SetPreferredClass(ClientID, PlayerClass);
}

void CInfClassGameController::SetPreferredClass(int ClientID, PLAYERCLASS Class)
{
	if(!IsHumanClass(Class))
	{
		return;
	}

	CInfClassPlayer *pPlayer = GetPlayer(ClientID);
	if(!pPlayer)
	{
		return;
	}

	pPlayer->SetPreferredClass(Class);

	switch(Class)
	{
	case PLAYERCLASS_RANDOM:
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_PLAYER,
			_("A random class will be automatically attributed to you when round starts"),
			nullptr);
		break;
	case PLAYERCLASS_INVALID:
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_PLAYER,
			_("The class selector will be displayed when round starts"),
			nullptr);
		break;
	default:
	{
		const char *pClassDisplayName = GetClassDisplayName(Class);
		const char *pTranslated = Server()->Localization()->Localize(pPlayer->GetLanguage(), pClassDisplayName);
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_PLAYER,
			_("Class {str:ClassName} will be automatically attributed to you when round starts"),
			"ClassName", pTranslated,
			nullptr);
		break;
	}
	}
}

void CInfClassGameController::ConAntiPing(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	int ClientID = pResult->GetClientID();

	int Arg = pResult->GetInteger(0);
	dbg_msg("server", "set_antiping ClientID=%d antiping=%d", ClientID, Arg);

	CInfClassPlayer *pPlayer = pSelf->GetPlayer(ClientID);
	pPlayer->SetAntiPingEnabled(Arg > 0);
}

void CInfClassGameController::ConUserSetClass(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->ConUserSetClass(pResult);
}

void CInfClassGameController::ConUserSetClass(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetClientID();
	if(!Config()->m_InfTrainingMode)
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("The command is not available (enabled only in training mode)"), nullptr);
		return;
	}

	const char *pClassName = pResult->GetString(0);

	CInfClassPlayer *pPlayer = GetPlayer(ClientID);

	if(!pPlayer)
		return;

	bool Ok = false;
	PLAYERCLASS PlayerClass = GetClassByName(pClassName, &Ok);
	if(Ok)
	{
		pPlayer->SetClass(PlayerClass);
		const char *pClassDisplayName = GetClassDisplayName(PlayerClass);
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} changed the class to {str:ClassName}"),
									"PlayerName", Server()->ClientName(ClientID),
									"ClassName", pClassDisplayName,
									nullptr);

		return;
	}

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "inf_set_class", "Unknown class");
}

void CInfClassGameController::ConSetClass(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->ConSetClass(pResult);
}

void CInfClassGameController::ConSetClass(IConsole::IResult *pResult)
{
	int PlayerID = pResult->GetInteger(0);
	const char *pClassName = pResult->GetString(1);

	CInfClassPlayer *pPlayer = GetPlayer(PlayerID);

	if(!pPlayer)
		return;

	bool Ok = false;
	PLAYERCLASS PlayerClass = GetClassByName(pClassName, &Ok);
	if(Ok)
	{
		pPlayer->SetClass(PlayerClass);
		char aBuf[256];
		const char *pClassDisplayName = GetClassDisplayName(PlayerClass);
		str_format(aBuf, sizeof(aBuf), "The admin change the class of %s to %s", GameServer()->Server()->ClientName(PlayerID), pClassDisplayName);
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

		return;
	}

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "inf_set_class", "Unknown class");
}

void CInfClassGameController::ConQueueSpecialRound(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	const char *pRoundTypeName = pResult->GetString(0);
	ROUND_TYPE Type = GetTypeByName(pRoundTypeName);
	if(Type == ROUND_TYPE::INVALID)
	{
		return;
	}
	pSelf->QueueRoundType(Type);
}

void CInfClassGameController::ConStartFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	if(pSelf->m_FunRoundConfigurations.empty())
	{
		int ClientID = pResult->GetClientID();
		const char *pErrorMessage = "Unable to start fun round: rounds configuration is empty";
		if(ClientID >= 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", pErrorMessage);
		}
		else
		{
			pSelf->GameServer()->SendChatTarget(-1, pErrorMessage);
		}
		return;
	}

	pSelf->QueueRoundType(ROUND_TYPE::FUN);
	pSelf->StartRound();
}

void CInfClassGameController::ConQueueFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;

	if(pSelf->m_FunRoundConfigurations.empty())
	{
		int ClientID = pResult->GetClientID();
		const char *pErrorMessage = "Unable to start a fun round: rounds configuration is empty";
		if(ClientID >= 0)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", pErrorMessage);
		}
		else
		{
			pSelf->GameServer()->SendChatTarget(-1, pErrorMessage);
		}
		return;
	}

	pSelf->QueueRoundType(ROUND_TYPE::FUN);
}

void CInfClassGameController::ConStartSpecialFunRound(IConsole::IResult *pResult, void *pUserData)
{
#if 0
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	FunRoundConfiguration Configuration;

	for(int argN = 0; argN < pResult->NumArguments(); ++argN)
	{
		const char *argument = pResult->GetString(argN);
		const int PlayerClass = CInfClassGameController::GetClassByName(argument);
		if((PlayerClass > START_HUMANCLASS) && (PlayerClass < END_HUMANCLASS))
		{
			Configuration.HumanClass = PlayerClass;
		}
		if((PlayerClass > START_INFECTEDCLASS) && (PlayerClass < END_INFECTEDCLASS))
		{
			Configuration.InfectedClass = PlayerClass;
		}
	}

	if(!Configuration.HumanClass || !Configuration.InfectedClass)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid special fun round configuration");
		return;
	}
	pSelf->StartFunRound(Configuration);
#endif
}

void CInfClassGameController::ConClearFunRounds(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->m_FunRoundConfigurations.clear();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "fun rounds cleared");
}

void CInfClassGameController::ConAddFunRound(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	FunRoundConfiguration Settings;

	for(int argN = 0; argN < pResult->NumArguments(); ++argN)
	{
		const char *pArgument = pResult->GetString(argN);
		bool Ok = true;
		const PLAYERCLASS PlayerClass = CInfClassGameController::GetClassByName(pArgument, &Ok);
		if(!Ok)
		{
			// Ignore other words (there can be "undeads vs heroes", ignore "vs" in such case)
			continue;
		}
		if((PlayerClass > START_HUMANCLASS) && (PlayerClass < END_HUMANCLASS))
		{
			Settings.HumanClass = PlayerClass;
		}
		if((PlayerClass > START_INFECTEDCLASS) && (PlayerClass < END_INFECTEDCLASS))
		{
			Settings.InfectedClass = PlayerClass;
		}
	}

	if((Settings.HumanClass == PLAYERCLASS_INVALID) || (Settings.InfectedClass == PLAYERCLASS_INVALID))
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid special fun round configuration");
		return;
	}
	else
	{
		char aBuf[256];
		const char *HumanClassText = CInfClassGameController::GetClassPluralDisplayName(Settings.HumanClass);
		const char *InfectedClassText = CInfClassGameController::GetClassPluralDisplayName(Settings.InfectedClass);
		str_format(aBuf, sizeof(aBuf), "Added fun round: %s vs %s", InfectedClassText, HumanClassText);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}

	pSelf->m_FunRoundConfigurations.push_back(Settings);
}

void CInfClassGameController::ConStartFastRound(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->QueueRoundType(ROUND_TYPE::FAST);
	pSelf->StartRound();
}

void CInfClassGameController::ConQueueFastRound(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->QueueRoundType(ROUND_TYPE::FAST);
}

void CInfClassGameController::ConMapRotationStatus(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->ConSmartMapRotationStatus();
}

void CInfClassGameController::ConSaveMapsData(IConsole::IResult *pResult, void *pUserData)
{
	const char *pFileName = pResult->GetString(0);

	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->SaveMapRotationData(pFileName);
}

void CInfClassGameController::ConPrintMapsData(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->PrintMapRotationData();
}

void CInfClassGameController::ConResetMapData(IConsole::IResult *pResult, void *pUserData)
{
	const char *pMapName = pResult->GetString(0);

	ResetMapInfo(pMapName);
}

void CInfClassGameController::ConAddMapData(IConsole::IResult *pResult, void *pUserData)
{
	const char *pMapName = pResult->GetString(0);
	int Timestamp = pResult->GetInteger(1);

	AddMapTimestamp(pMapName, Timestamp);
}

void CInfClassGameController::ConSetMapMinMaxPlayers(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	return pSelf->ConSetMapMinMaxPlayers(pResult);
}

void CInfClassGameController::ConSetMapMinMaxPlayers(IConsole::IResult *pResult)
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

void CInfClassGameController::ConSavePosition(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	return pSelf->ConSavePosition(pResult);
}

void CInfClassGameController::ConSavePosition(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetClientID();
	if(!Config()->m_InfTrainingMode)
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("The command is not available (enabled only in training mode)"), nullptr);
		return;
	}

	CInfClassCharacter *pCharacter = GetCharacter(ClientID);
	if(!pCharacter)
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Unable to save the position: you have no character to save its position"), nullptr);
		return;
	}

	if(!pCharacter->IsAlive())
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Unable to save the position: the character state is not valid"), nullptr);
		return;
	}

	if(!pCharacter->IsGrounded())
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Unable to save the position: the character does not stand on the ground"), nullptr);
		return;
	}

	vec2 Position = pCharacter->GetPos();
	CInfClassPlayer *pPlayer = GetPlayer(ClientID);
	if(!pPlayer)
	{
		// What...
		return;
	}

	pPlayer->AddSavedPosition(Position);
}

void CInfClassGameController::ConLoadPosition(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	return pSelf->ConLoadPosition(pResult);
}

void CInfClassGameController::ConLoadPosition(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetClientID();
	if(!Config()->m_InfTrainingMode)
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("The command is not available (enabled only in training mode)"), nullptr);
		return;
	}

	CInfClassCharacter *pCharacter = GetCharacter(ClientID);
	if(!pCharacter)
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Unable to load the position: you have no character to load its position"), nullptr);
		return;
	}

	if(!pCharacter->IsAlive())
	{
		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Unable to load the position: the character state is not valid"), nullptr);
		return;
	}

	vec2 Position;
	CInfClassPlayer *pPlayer = GetPlayer(ClientID);
	if(!pPlayer)
	{
		// What...
		return;
	}

	pPlayer->LoadSavedPosition(&Position);

	pCharacter->m_Pos = Position;
	pCharacter->m_Core.m_Pos = Position;
	pCharacter->m_Core.m_Vel = vec2(0, 0);
	GameWorld()->ReleaseHooked(ClientID);
	pCharacter->m_Core.m_HookedPlayer = -1;
	pCharacter->m_Core.m_HookState = HOOK_RETRACTED;
	pCharacter->m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
	pCharacter->m_Core.m_HookPos = Position;
}

void CInfClassGameController::ConSetHealthArmor(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	return pSelf->ConSetHealthArmor(pResult);
}

void CInfClassGameController::ConSetHealthArmor(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetInteger(0);
	int Health = pResult->GetInteger(1);
	int Armor = pResult->GetInteger(2);

	CInfClassCharacter *pCharacter = GetCharacter(ClientID);
	if(!pCharacter)
	{
		return;
	}

	pCharacter->SetHealthArmor(Health, Armor);
}

void CInfClassGameController::ConSetInvincible(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	return pSelf->ConSetInvincible(pResult);
}

void CInfClassGameController::ConSetInvincible(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetInteger(0);
	int Invincible = pResult->GetInteger(1);

	CInfClassCharacter *pCharacter = GetCharacter(ClientID);
	if(!pCharacter)
	{
		return;
	}

	pCharacter->SetInvincible(Invincible);
}

void CInfClassGameController::ConSetHookProtection(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	return pSelf->ConSetHookProtection(pResult);
}

void CInfClassGameController::ConSetHookProtection(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetInteger(0);
	int Protection = pResult->GetInteger(1);

	CInfClassPlayer *pPlayer = GetPlayer(ClientID);
	if(!pPlayer)
	{
		return;
	}

	bool Automatic = false;
	pPlayer->HookProtection(Protection, Automatic);
}

void CInfClassGameController::ChatWitch(IConsole::IResult *pResult, void *pUserData)
{
	CInfClassGameController *pSelf = (CInfClassGameController *)pUserData;
	pSelf->ChatWitch(pResult);
}

void CInfClassGameController::ChatWitch(IConsole::IResult *pResult)
{
	int ClientID = pResult->GetClientID();
	const int REQUIRED_CALLERS_COUNT = 5;
	const int MIN_ZOMBIES = 2;

	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "conwitch", "ChatWitch() called");

	int MaxWitches = GetClassPlayerLimit(PLAYERCLASS_WITCH);
	const bool Winter = EventsDirector::IsWinter();
	if(Winter)
	{
		// Santa is a new Witch; allow only one Santa at time.
		MaxWitches = 1;
	}
	if(GetInfectedCount(PLAYERCLASS_WITCH) >= MaxWitches)
	{
		if(Winter)
		{
			GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("The Santa is already here"), nullptr);
			return;
		}

		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("All witches are already here"), nullptr);
		return;
	}

	int Humans = 0;
	int Infected = 0;
	GetPlayerCounter(-1, Humans, Infected);

	if(Humans + Infected < REQUIRED_CALLERS_COUNT)
	{
		if(Winter)
		{
			GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Too few players to call the Santa"), nullptr);
			return;
		}

		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Too few players to call a witch"), nullptr);
		return;
	}
	if(Infected < MIN_ZOMBIES)
	{
		if(Winter)
		{
			GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Too few infected to call the Santa"), nullptr);
			return;
		}

		GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("Too few infected to call a witch"), nullptr);
		return;
	}

	// It is possible that we had the needed callers but all witches already were there.
	// In that case even if the caller is already in the list, we still want to spawn
	// a new one without a message to the caller.
	if(m_WitchCallers.Size() < REQUIRED_CALLERS_COUNT)
	{
		if(m_WitchCallers.Contains(ClientID))
		{
			if(Winter)
			{
				GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("You have called the Santa once again"), nullptr);
				return;
			}

			GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT, _("You can't call witch twice"), nullptr);
			return;
		}

		m_WitchCallers.Add(ClientID);

		int PrintableRequiredCallers = REQUIRED_CALLERS_COUNT;
		int PrintableCallers = m_WitchCallers.Size();
		if(m_WitchCallers.Size() == 1)
		{
			if(Winter)
			{
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT,
					_("{str:PlayerName} is calling for Santa! (1/{int:RequiredCallers}) To call the Santa write: /santa"),
					"PlayerName", Server()->ClientName(ClientID),
					"RequiredCallers", &PrintableRequiredCallers,
					nullptr);
				return;
			}

			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_DEFAULT,
				_("{str:PlayerName} is calling for Witch! (1/{int:RequiredCallers}) To call witch write: /witch"),
				"PlayerName", Server()->ClientName(ClientID),
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
		int WitchId = RandomZombieToWitch();
		if(WitchId < 0)
		{
			if(Winter)
			{
				GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT,
					_("The Santa is already here"),
					nullptr);
				return;
			}
			GameServer()->SendChatTarget_Localization(ClientID, CHATCATEGORY_DEFAULT,
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
	}
}

IConsole *CInfClassGameController::Console() const
{
	return GameServer()->Console();
}

CInfClassPlayer *CInfClassGameController::GetPlayer(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return nullptr;

	return CInfClassPlayer::GetInstance(GameServer()->m_apPlayers[ClientID]);
}

CInfClassCharacter *CInfClassGameController::GetCharacter(int ClientID) const
{
	CInfClassPlayer *pPlayer = GetPlayer(ClientID);
	return pPlayer ? pPlayer->GetCharacter() : nullptr;
}

int CInfClassGameController::GetPlayerOwnCursorID(int ClientID) const
{
	return m_PlayerOwnCursorID;
}

void CInfClassGameController::SortCharactersByDistance(const ClientsArray &Input, ClientsArray *pOutput, const vec2 &Center, const float MaxDistance)
{
	struct DistanceItem
	{
		DistanceItem() = default;
		DistanceItem(int C, float D)
			: ClientID(C)
			, Distance(D)
		{
		}

		int ClientID;
		float Distance;

		bool operator<(const DistanceItem &AnotherDistanceItem) const
		{
			return Distance < AnotherDistanceItem.Distance;
		}
	};

	icArray<DistanceItem, MAX_CLIENTS> Distances;

	for(int i = 0; i < Input.Size(); ++i)
	{
		int ClientID = Input.At(i);
		const CCharacter *pChar = GetCharacter(ClientID);
		if(!pChar)
			continue;

		const vec2 &CharPos = pChar->GetPos();
		const float Distance = std::max<float>(0.f, distance(CharPos, Center) - pChar->GetProximityRadius());
		if(MaxDistance && (Distance > MaxDistance))
			continue;

		Distances.Add(DistanceItem(ClientID, Distance));

		std::sort(Distances.begin(), Distances.end());
	}

	pOutput->Clear();
	for(const DistanceItem &DistanceItem : Distances)
	{
		pOutput->Add(DistanceItem.ClientID);
	}
}

void CInfClassGameController::GetSortedTargetsInRange(const vec2 &Center, const float Radius, const ClientsArray &SkipList, ClientsArray *pOutput)
{
	ClientsArray PossibleCIDs;

	for(int ClientID = 0; ClientID < MAX_CLIENTS; ++ClientID)
	{
		if(SkipList.Contains(ClientID))
			continue;

		const CCharacter *pChar = GetCharacter(ClientID);
		if(!pChar)
			continue;

		PossibleCIDs.Add(ClientID);
	}

	SortCharactersByDistance(PossibleCIDs, pOutput, Center, Radius);
}

void CInfClassGameController::UpdateNinjaTargets()
{
	m_NinjaTargets.Clear();

	if(!m_InfectedStarted && !Config()->m_InfTrainingMode)
		return;

	int InfectedCount = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GetCharacter(i) && GetCharacter(i)->IsZombie())
		{
			InfectedCount++;
			if(GetPlayer(i)->GetClass() == PLAYERCLASS_UNDEAD)
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

void CInfClassGameController::ReservePlayerOwnSnapItems()
{
	m_PlayerOwnCursorID = Server()->SnapNewID();
}

void CInfClassGameController::FreePlayerOwnSnapItems()
{
	Server()->SnapFreeID(m_PlayerOwnCursorID);
}

void CInfClassGameController::OnInfectionTriggered()
{
	MaybeSuggestMoreRounds();
}

void CInfClassGameController::MaybeSuggestMoreRounds()
{
	if(m_MoreRoundsSuggested)
		return;

	if(Config()->m_SvSuggestMoreRounds == 0)
		return;

	if(m_RoundCount != Config()->m_SvRoundsPerMap-1)
		return;

	m_SuggestMoreRounds = true;
}

CGameWorld *CInfClassGameController::GameWorld()
{
	return &GameServer()->m_World;
}

void CInfClassGameController::StartRound()
{
	m_RoundType = m_QueuedRoundType;
	QueueRoundType(ROUND_TYPE::NORMAL);

	switch(GetRoundType())
	{
	case ROUND_TYPE::NORMAL:
		break;
	case ROUND_TYPE::FUN:
	{
		const auto Configs = m_FunRoundConfigurations;
		if (Configs.empty())
		{
			m_RoundType = ROUND_TYPE::NORMAL;
			break;
		}
		const int type = random_int(0, Configs.size() - 1);
		const FunRoundConfiguration &Configuration = Configs[type];
		StartFunRound(Configuration);
	}
		break;
	case ROUND_TYPE::FAST:
		GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
		GameServer()->SendChatTarget(-1, "Starting the 'fast' round. Good luck everyone!");
		break;
	case ROUND_TYPE::INVALID:
		break;
	}

	SaveRoundRules();

	m_RoundStarted = true;
	IGameController::StartRound();

	Server()->ResetStatistics();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CInfClassPlayer *pPlayer = GetPlayer(i);
		if(pPlayer)
		{
			Server()->SetClientMemory(i, CLIENTMEMORY_ROUNDSTART_OR_MAPCHANGE, true);
			pPlayer->ResetRoundData();
		}
	}

	m_HeroGiftTick = 0;

	OnStartRound();
}

void CInfClassGameController::EndRound()
{
	// The EndRound() override is called only from the IGameController on map skipped or changed
	EndRound(ROUND_END_REASON::CANCELED);
}

void CInfClassGameController::EndRound(ROUND_END_REASON Reason)
{
	{
		int NumHumans = 0;
		int NumInfected = 0;
		GetPlayerCounter(-1, NumHumans, NumInfected);

		const char *pWinnerTeam = Reason == ROUND_END_REASON::FINISHED ? (NumHumans > 0 ? "humans" : "zombies") : "none";
		const char *pRoundType = toString(GetRoundType());

		// Win check
		const int Seconds = (Server()->Tick() - m_RoundStartTick) / ((float)Server()->TickSpeed());

		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "round_end winner='%s' survivors='%d' duration='%d' round='%d of %d' type='%s'",
			pWinnerTeam,
			NumHumans, Seconds, m_RoundCount + 1, Config()->m_SvRoundsPerMap, pRoundType);
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}
	m_InfectedStarted = false;
	ResetFinalExplosion();
	IGameController::EndRound();

	if(Reason == ROUND_END_REASON::FINISHED)
	{
		MaybeSendStatistics();
		Server()->OnRoundIsOver();
	}

	switch(GetRoundType())
	{
	case ROUND_TYPE::NORMAL:
	case ROUND_TYPE::FAST:
		break;
	case ROUND_TYPE::FUN:
		EndFunRound();
		break;
	case ROUND_TYPE::INVALID:
		break;
	}

	m_RoundStarted = false;
}

void CInfClassGameController::DoTeamChange(CPlayer *pBasePlayer, int Team, bool DoChatMsg)
{
	CInfClassPlayer *pPlayer = CInfClassPlayer::GetInstance(pBasePlayer);
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	int OldTeam = pPlayer->GetTeam();
	IGameController::DoTeamChange(pPlayer, Team, false);

	int ClientID = pPlayer->GetCID();

	if(DoChatMsg)
	{
		if(Team == TEAM_SPECTATORS)
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} joined the spectators"), "PlayerName", Server()->ClientName(ClientID), NULL);
		}
		else
		{
			GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_PLAYER, _("{str:PlayerName} joined the game"), "PlayerName", Server()->ClientName(ClientID), NULL);
		}
	}

	if((Team != TEAM_SPECTATORS) && IsInfectionStarted() && !pPlayer->IsZombie())
	{
		PLAYERCLASS c = ChooseInfectedClass(pPlayer);
		pPlayer->SetClass(c);
	}
}

void CInfClassGameController::GetPlayerCounter(int ClientException, int& NumHumans, int& NumInfected)
{
	NumHumans = 0;
	NumInfected = 0;
	
	//Count type of players
	CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if(Iter.ClientID() == ClientException) continue;
		
		if(Iter.Player()->IsZombie()) NumInfected++;
		else NumHumans++;
	}
}

int CInfClassGameController::GetMinimumInfectedForPlayers(int PlayersNumber) const
{
	if(GetRoundType() == ROUND_TYPE::FAST)
	{
		//  7 | 3 vs 4 | 3.01
		//  8 | 3 vs 5 | 3.44
		//  9 | 3 vs 6 | 3.87
		// 10 | 4 vs 6 | 4.30
		// 11 | 4 vs 7 | 4.73
		// 12 | 5 vs 7 | 5.16
		return PlayersNumber * 0.43;
	}

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

	int FirstInfectedLimit = Config()->m_InfFirstInfectedLimit;
	if(FirstInfectedLimit && NumFirstInfected > FirstInfectedLimit)
	{
		NumFirstInfected = FirstInfectedLimit;
	}

	return NumFirstInfected;
}

void CInfClassGameController::SetAvailabilities(std::vector<int> value)
{
	static const int ValuesNumber = NB_HUMANCLASS;
	if(value.size() < ValuesNumber)
		value.resize(ValuesNumber, 0);

	for(int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; PlayerClass++)
	{
		const int ClassEnabledValue = value.at(PlayerClass - START_HUMANCLASS - 1);
		Server()->SetPlayerClassEnabled(PlayerClass, ClassEnabledValue);
	}
}

void CInfClassGameController::SetProbabilities(std::vector<int> value)
{
	int *extraConfigValues[] =
		{
			// The order is still important!
			&g_Config.m_InfGhoulThreshold,
			&g_Config.m_InfGhoulStomachSize,
		};
	static const int ExtraValuesNumber = sizeof(extraConfigValues) / sizeof(extraConfigValues[0]);
	static const int ValuesNumber = NB_INFECTEDCLASS + ExtraValuesNumber;
	if(value.size() < ValuesNumber)
		value.resize(ValuesNumber, 0);

	for(int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; PlayerClass++)
	{
		const int ClassProbability = value.at(PlayerClass - START_INFECTEDCLASS - 1);
		Server()->SetPlayerClassProbability(PlayerClass, ClassProbability);
	}
	for(int i = 0; i < ExtraValuesNumber; ++i)
	{
		const int newValue = value.at(NB_INFECTEDCLASS + i);
		*extraConfigValues[i] = newValue;
	}
}

int CInfClassGameController::GetMinimumInfected() const
{
	int NumPlayers = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CInfClassPlayer *pPlayer = GetPlayer(i);
		if(!pPlayer || !pPlayer->m_IsInGame || pPlayer->IsSpectator())
		{
			continue;
		}
		++NumPlayers;
	}

	return GetMinimumInfectedForPlayers(NumPlayers);
}

int CInfClassGameController::InfectedBonusArmor() const
{
	const float Factor = clamp<float>(m_InfBalanceBoostFactor, 0, 1);
	return Factor * 10;
}

void CInfClassGameController::SendKillMessage(int Victim, const DeathContext &Context)
{
	DAMAGE_TYPE DamageType = Context.DamageType;
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
	if(Context.DamageType == DAMAGE_TYPE::DAMAGE_TILE)
	{
		DamageType = DAMAGE_TYPE::DEATH_TILE;
	}

	// Substitute the weapon for clients for better UI icon
	if(DamageType == DAMAGE_TYPE::DEATH_TILE)
		VanillaWeapon = WEAPON_NINJA;

	int DamageTypeInt = static_cast<int>(DamageType);
	dbg_msg("inf-proto", "Sent kill message victim=%d, damage_type=%d, killer=%d, assistant=%d", Victim, DamageTypeInt, Killer, Assistant);

	CNetMsg_Inf_KillMsg InfClassMsg;
	InfClassMsg.m_Killer = Killer;
	InfClassMsg.m_Victim = Victim;
	InfClassMsg.m_Assistant = Assistant;
	InfClassMsg.m_InfDamageType = DamageTypeInt;
	InfClassMsg.m_Weapon = VanillaWeapon;

	CMsgPacker InfCPacker(InfClassMsg.MsgID(), false);
	InfClassMsg.Pack(&InfCPacker);

	CNetMsg_Sv_KillMsg VanillaMsg;
	VanillaMsg.m_Killer = Killer;
	VanillaMsg.m_Victim = Victim;
	VanillaMsg.m_Weapon = VanillaWeapon;
	VanillaMsg.m_ModeSpecial = InfClassModeSpecialSkip;

	CMsgPacker VanillaPacker(VanillaMsg.MsgID(), false);
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
		CInfClassPlayer *pKiller = GetPlayer(Killer);
		CInfClassPlayer *pVictim = GetPlayer(Victim);
		CInfClassPlayer *pAssistant = GetPlayer(Assistant);
		if(pKiller && (pKiller != pVictim))
			pKiller->OnKill();
		if(pVictim)
			pVictim->OnDeath();
		if(pAssistant && (pAssistant != pVictim))
			pAssistant->OnAssist();
	}
}

int CInfClassGameController::RandomZombieToWitch()
{
	ClientsArray UnsafeInfected;
	ClientsArray SafeInfected;

	m_WitchCallers.Clear();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CInfClassPlayer *pPlayer = GetPlayer(i);
		if(!pPlayer)
			continue;
		if(pPlayer->GetClass() == PLAYERCLASS_WITCH)
			continue;
		if(!pPlayer->IsActuallyZombie())
			continue;

		UnsafeInfected.Add(i);
		
		if(!IsSafeWitchCandidate(i))
			continue;
		
		SafeInfected.Add(i);
	}

	if(UnsafeInfected.IsEmpty())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "witch", "Unable to find any suitable player");
		return -1;
	}

	const ClientsArray &Candidates = SafeInfected.IsEmpty() ? UnsafeInfected : SafeInfected;
	int id = random_int(0, Candidates.Size() - 1);
	char aBuf[512];
	/* debug */
	str_format(aBuf, sizeof(aBuf), "going through MAX_CLIENTS=%d, zombie_count=%d, random_int=%d, id=%d", MAX_CLIENTS, static_cast<int>(UnsafeInfected.Size()), id, UnsafeInfected[id]);
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "witch", aBuf);
	/* /debug */
	CInfClassPlayer *pPlayer = GetPlayer(Candidates[id]);
	pPlayer->SetClass(PLAYERCLASS_WITCH);
	return Candidates[id];
}

bool CInfClassGameController::IsSafeWitchCandidate(int ClientID) const
{
	constexpr double MaxInactiveSeconds = 5;
	constexpr double SafeRadius = 1000;

	const CInfClassPlayer *pPlayer = GetPlayer(ClientID);
	if(!pPlayer)
		return false;

	if(Server()->Tick() > pPlayer->m_LastActionTick + MaxInactiveSeconds * Server()->TickSpeed())
		return false;

	const CInfClassCharacter *pCharacter = GetCharacter(ClientID);
	if(pCharacter && pCharacter->IsAlive())
	{
		icArray<CInfClassCharacter *, MAX_CLIENTS> aCharsNearby;
		int Num = GameServer()->m_World.FindEntities(pCharacter->GetPos(), SafeRadius,
			reinterpret_cast<CEntity**>(aCharsNearby.begin()),
			aCharsNearby.Capacity(),
			CGameWorld::ENTTYPE_CHARACTER);
		aCharsNearby.Resize(Num);

		for(const CInfClassCharacter *pCharNearby : aCharsNearby)
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

void CInfClassGameController::Tick()
{
	IGameController::Tick();
	
	//Check session
	{
		CInfClassPlayerIterator<PLAYERITER_ALL> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			//Update session
			IServer::CClientSession* pSession = Server()->GetClientSession(Iter.ClientID());
			if(pSession)
			{
				if(!Server()->GetClientMemory(Iter.ClientID(), CLIENTMEMORY_SESSION_PROCESSED))
				{
					//The client already participated to this round,
					//and he exit the game as infected.
					//To avoid cheating, we assign to him the same class again.
					if(pSession->m_RoundId == m_RoundId)
					{
						const PLAYERCLASS ClassFromSession = static_cast<PLAYERCLASS>(pSession->m_Class);
						if(IsInfectedClass(ClassFromSession) == IsInfectionStarted())
						{
							Iter.Player()->SetClass(ClassFromSession);
						}
					}

					Server()->SetClientMemory(Iter.ClientID(), CLIENTMEMORY_SESSION_PROCESSED, true);
				}
				
				pSession->m_Class = Iter.Player()->GetClass();
				pSession->m_RoundId = GameServer()->m_pController->GetRoundId();
			}
		}
	}

	CheckRoundFailed();

	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	const int NumPlayers = NumHumans + NumInfected;

	m_InfectedStarted = false;

	//If the game can start ...
	if(!m_Warmup && m_GameOverTick == -1 && NumPlayers >= Config()->m_InfMinPlayers)
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
	}
	else
	{
		m_RoundStartTick = Server()->Tick();
	}

	UpdateNinjaTargets();
	HandleLastHookers();

	if(GameWorld()->m_Paused)
	{
		m_HeroGiftTick++;
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
}

void CInfClassGameController::RoundTickBeforeInitialInfection()
{
	int InitialInfectionTick = m_RoundStartTick + Server()->TickSpeed() * GetInfectionDelay();
	BroadcastInfectionComing(InitialInfectionTick);
}

void CInfClassGameController::RoundTickAfterInitialInfection()
{
	int InitialInfectionTick = m_RoundStartTick + Server()->TickSpeed() * GetInfectionDelay();
	bool StartInfectionTrigger = InitialInfectionTick == Server()->Tick();

	if(StartInfectionTrigger)
	{
		OnInfectionTriggered();
		m_InfUnbalancedTick = -1;
	}

	CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		CInfClassPlayer *pPlayer = Iter.Player();
		// Set the player class if not set yet
		if(pPlayer->GetClass() == PLAYERCLASS_NONE)
		{
			if(StartInfectionTrigger)
			{
				pPlayer->SetClass(ChooseHumanClass(pPlayer));
				pPlayer->SetRandomClassChoosen();
				CInfClassCharacter *pCharacter = Iter.Player()->GetCharacter();
				if(pCharacter)
				{
					pCharacter->GiveRandomClassSelectionBonus();
				}
			}
			else
			{
				pPlayer->KillCharacter(); // Infect the player
				pPlayer->StartInfection();
				pPlayer->m_DieTick = m_RoundStartTick;
			}
		}
	}

	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	const int NumPlayers = NumHumans + NumInfected;
	const int NumMinimumInfected = GetMinimumInfectedForPlayers(NumPlayers);

	const bool GameBalanced = NumInfected >= NumMinimumInfected;
	const int PlayersToInfect = maximum<int>(0, NumMinimumInfected - NumInfected);
	int NewInfected = 0;
	if(StartInfectionTrigger)
	{
		if(!GameBalanced)
		{
			NewInfected = InfectHumans(PlayersToInfect);
		}
	}
	else
	{
		if(GameBalanced)
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
				// Force balance
				NewInfected = InfectHumans(PlayersToInfect);
				GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION,
					_("Someone was infected to balance the game."), nullptr);
			}
			else
			{
				BroadcastInfectionComing(BalancingTick);
			}
		}
	}

	NumInfected += NewInfected;
	NumHumans -= NewInfected;

	UpdateBalanceFactors(NumHumans, NumInfected);

	if(StartInfectionTrigger)
	{
		Iter.Reset();
		while(Iter.Next())
		{
			CInfClassPlayer *pPlayer = Iter.Player();
			if(pPlayer->IsZombie() || pPlayer->IsInfectionStarted())
			{
				pPlayer->KillCharacter(); // Infect the player
				pPlayer->m_DieTick = m_RoundStartTick;
				continue;
			}
		}
	}
}

int CInfClassGameController::InfectHumans(int NumHumansToInfect)
{
	if(NumHumansToInfect == 0)
		return 0;

	CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	ClientsArray FairCandidates;
	ClientsArray UnfairCandidates;

	icArray<CInfClassPlayer *, 64> Humans;

	while(Iter.Next())
	{
		CInfClassPlayer *pPlayer = Iter.Player();
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

	const auto Sorter = [](const CInfClassPlayer *p1, const CInfClassPlayer *p2) -> bool {
		const int ts1 = p1->GetInfectionTimestamp();
		const int ts2 = p2->GetInfectionTimestamp();
		if(ts1 == 0)
		{
			if(ts2 == 0)
			{
				return false;
			}
			return true;
		}

		return ts1 < ts2;
	};

	std::stable_sort(Humans.begin(), Humans.end(), Sorter);

	int Timestamp = time_timestamp();

	int NewInfected = 0;
	for(CInfClassPlayer *pPlayer : Humans)
	{
		pPlayer->SetInfectionTimestamp(Timestamp);

		pPlayer->KillCharacter(); // Infect the player
		pPlayer->StartInfection();
		pPlayer->m_DieTick = m_RoundStartTick;
		NewInfected++;

		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTION,
			_("{str:VictimName} has been infected"),
				"VictimName", Server()->ClientName(pPlayer->GetCID()),
				nullptr);

		if(NewInfected >= NumHumansToInfect)
		{
			break;
		}
	}

	return NewInfected;
}

void CInfClassGameController::UpdateBalanceFactors(int NumHumans, int NumInfected)
{
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

void CInfClassGameController::MaybeSendStatistics()
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

	if(GetRoundType() != ROUND_TYPE::NORMAL)
		return;

	Server()->SendStatistics();
}

void CInfClassGameController::CancelTheRound(ROUND_CANCELATION_REASON Reason)
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

	EndRound(ROUND_END_REASON::CANCELED);
}

void CInfClassGameController::AnnounceTheWinner(int NumHumans)
{
	if(NumHumans)
	{
		GameServer()->SendChatTarget_Localization_P(-1, CHATCATEGORY_HUMANS, NumHumans,
			_P("One human won the round",
			   "{int:NumHumans} humans won the round"),
			"NumHumans", &NumHumans,
			nullptr);

		char aBuf[256];
		CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			if(Iter.Player()->IsHuman())
			{
				//TAG_SCORE
				Server()->RoundStatistics()->OnScoreEvent(Iter.ClientID(), SCOREEVENT_HUMAN_SURVIVE, Iter.Player()->GetClass(), Server()->ClientName(Iter.ClientID()), Console());
				Server()->RoundStatistics()->SetPlayerAsWinner(Iter.ClientID());
				GameServer()->SendScoreSound(Iter.ClientID());
				Iter.Player()->m_WinAsHuman++;

				GameServer()->SendChatTarget_Localization(Iter.ClientID(), CHATCATEGORY_SCORE, _("You have survived, +5 points"), NULL);
				str_format(aBuf, sizeof(aBuf), "survived player='%s'", Server()->ClientName(Iter.ClientID()));
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
			}
		}
	}
	else
	{
		const int Seconds = (Server()->Tick() - m_RoundStartTick) / ((float)Server()->TickSpeed());
		GameServer()->SendChatTarget_Localization(-1, CHATCATEGORY_INFECTED, _("Infected won the round in {sec:RoundDuration}"), "RoundDuration", &Seconds, NULL);
	}

	EndRound(ROUND_END_REASON::FINISHED);
}

void CInfClassGameController::BroadcastInfectionComing(int InfectionTick)
{
	if(InfectionTick <= Server()->Tick())
		return;

	int Seconds = (InfectionTick - Server()->Tick()) / Server()->TickSpeed() + 1;
	int Priority = Seconds <= 3 ? BROADCAST_PRIORITY_GAMEANNOUNCE : BROADCAST_PRIORITY_LOWEST;
	GameServer()->SendBroadcast_Localization(-1,
		Priority,
		BROADCAST_DURATION_REALTIME,
		_("Infection is coming in {sec:RemainingTime}"),
		"RemainingTime", &Seconds,
		nullptr);
}

bool CInfClassGameController::IsInfectionStarted() const
{
	if(Config()->m_InfTrainingMode)
		return false;

	return (m_RoundStartTick + Server()->TickSpeed() * GetInfectionDelay() <= Server()->Tick());
}

bool CInfClassGameController::CanJoinTeam(int Team, int ClientID)
{
	if(Team != TEAM_SPECTATORS)
	{
		return IGameController::CanJoinTeam(Team, ClientID);
	}

	if (IsGameOver())
		return true;

	CInfClassPlayer *pPlayer = GetPlayer(ClientID);

	if(!pPlayer) // Invalid call
		return false;

	if (pPlayer->IsHuman())
		return true;

	int NumHumans;
	int NumInfected;
	GetPlayerCounter(ClientID, NumHumans, NumInfected);
	const int NumPlayers = NumHumans + NumInfected;
	const int NumMinInfected = GetMinimumInfectedForPlayers(NumPlayers);
	if(NumInfected >= NumMinInfected)
	{
		// Let the ClientID join the specs if we'll not have to infect
		// someone after the join.
		return true;
	}

	return false;
}

bool CInfClassGameController::AreTurretsEnabled() const
{
	if(!Config()->m_InfTurretEnable)
		return false;

	return Server()->GetActivePlayerCount() >= Config()->m_InfMinPlayersForTurrets;
}

bool CInfClassGameController::MercBombsEnabled() const
{
	return GetRoundType() != ROUND_TYPE::FUN;
}

bool CInfClassGameController::WhiteHoleEnabled() const
{
	if(GetRoundType() == ROUND_TYPE::FAST)
		return false;

	if(Server()->GetActivePlayerCount() < Config()->m_InfMinPlayersForWhiteHole)
		return false;

	return Config()->m_InfWhiteHoleProbability > 0;
}

bool CInfClassGameController::IsClassChooserEnabled() const
{
	return Config()->m_InfClassChooser;
}

float CInfClassGameController::GetTimeLimit() const
{
	if(Config()->m_InfTrainingMode)
		return 0;

	float BaseTimeLimit = Config()->m_SvTimelimitInSeconds ? Config()->m_SvTimelimitInSeconds / 60.0 : Config()->m_SvTimelimit;

	switch(GetRoundType())
	{
	case ROUND_TYPE::FUN:
		return minimum<float>(BaseTimeLimit, Config()->m_FunRoundDuration);
	case ROUND_TYPE::FAST:
		return clamp<float>(BaseTimeLimit * 0.5, 1, 3);
	default:
		return BaseTimeLimit;
	}
}

float CInfClassGameController::GetInfectionDelay() const
{
	return Config()->m_InfInitialInfectionDelay;
}

bool CInfClassGameController::HeroGiftAvailable() const
{
	return Server()->Tick() >= m_HeroGiftTick;
}

bool CInfClassGameController::GetHeroFlagPosition(vec2 *pFlagPosition) const
{
	int NbPos = m_HeroFlagPositions.size();

	for(int Attempts = 3; Attempts > 0; Attempts--)
	{
		int Index = random_int(0, NbPos - 1);
		const vec2 Pos = m_HeroFlagPositions[Index];
		*pFlagPosition = Pos;
		if(IsPositionAvailableForHumans(Pos))
		{
			return true;
		}
	}

	return false;
}

bool CInfClassGameController::IsPositionAvailableForHumans(const vec2 &Position) const
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

void CInfClassGameController::StartFunRound(const FunRoundConfiguration &Configuration)
{
	const char *pTitle = Config()->m_FunRoundTitle;
	char aBuf[256];

	// zombies
	std::vector<int> InfectedProbabilities;
	for(int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; PlayerClass++)
	{
		InfectedProbabilities.push_back(GetPlayerClassProbability(PlayerClass));
	}
	const auto extraConfigValues =
		{
			// The order is still important!
			Config()->m_InfGhoulThreshold,
			Config()->m_InfGhoulStomachSize,
		};
	for(const int &extraValue : extraConfigValues)
	{
		InfectedProbabilities.push_back(extraValue);
	}

	// humans
	std::vector<int> HumanAvailabilities;
	for(int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; PlayerClass++)
	{
		HumanAvailabilities.push_back(GetPlayerClassEnabled(PlayerClass));
	}

	std::vector<const char *> phrases = {
		", glhf!",
		", not ez!",
		" c:",
		" xd",
		", that's gg",
		", good luck!"};
	const char *random_phrase = phrases[random_int(0, phrases.size() - 1)];
	SetProbabilities(std::vector<int>());
	SetAvailabilities(std::vector<int>());
	Config()->m_InfGhoulStomachSize = Config()->m_FunRoundGhoulStomachSize;

	Server()->SetPlayerClassEnabled(Configuration.HumanClass, true);
	Server()->SetPlayerClassProbability(Configuration.InfectedClass, 100);
	const char *HumanClassText = CInfClassGameController::GetClassPluralDisplayName(Configuration.HumanClass);
	const char *InfectedClassText = CInfClassGameController::GetClassPluralDisplayName(Configuration.InfectedClass);

	str_format(aBuf, sizeof(aBuf), "%s! %s vs %s%s", pTitle, InfectedClassText, HumanClassText, random_phrase);

	GameServer()->CreateSoundGlobal(SOUND_CTF_CAPTURE);
	GameServer()->SendChatTarget(-1, aBuf);

	m_DefaultAvailabilities = HumanAvailabilities;
	m_DefaultProbabilities = InfectedProbabilities;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		CInfClassPlayer *pPlayer = GetPlayer(i);
		if(!pPlayer)
			continue;

		pPlayer->SetClass(Configuration.HumanClass);
	}
}

void CInfClassGameController::EndFunRound()
{
	SetAvailabilities(m_DefaultAvailabilities);
	SetProbabilities(m_DefaultProbabilities);
	m_FunRoundsPassed++;
}

void CInfClassGameController::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = m_GameFlags;
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

	int WholeMinutes = GetTimeLimit();
	float FractionalPart = GetTimeLimit() - WholeMinutes;

	pGameInfoObj->m_TimeLimit = WholeMinutes + (FractionalPart ? 1 : 0);
	if(FractionalPart)
	{
		pGameInfoObj->m_RoundStartTick -= (1 - FractionalPart) * 60 * Server()->TickSpeed();
	}

	pGameInfoObj->m_RoundNum = (str_length(Config()->m_SvMaprotation) && Config()->m_SvRoundsPerMap) ? Config()->m_SvRoundsPerMap : 0;
	pGameInfoObj->m_RoundCurrent = m_RoundCount+1;

	SnapMapMenu(SnappingClient, pGameInfoObj);

	CNetObj_InfClassGameInfo *pInfclassGameInfoObj = (CNetObj_InfClassGameInfo *)Server()->SnapNewItem(NETOBJTYPE_INFCLASSGAMEINFO, 0, sizeof(CNetObj_InfClassGameInfo));
	pInfclassGameInfoObj->m_Version = 1;
	pInfclassGameInfoObj->m_Flags = 0;
	pInfclassGameInfoObj->m_TimeLimitInSeconds = GetTimeLimit() * 60;
	pInfclassGameInfoObj->m_HeroGiftTick = m_HeroGiftTick;

	CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
	if(!pGameDataObj)
		return;

	//Search for witch
	CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		if(Iter.Player()->GetClass() == PLAYERCLASS_WITCH)
		{
			pGameDataObj->m_FlagCarrierRed = Iter.ClientID();
		}
	}
	
	pGameDataObj->m_FlagCarrierBlue = FLAG_ATSTAND;
}

CPlayer *CInfClassGameController::CreatePlayer(int ClientID, bool IsSpectator, void *pData)
{
	CInfClassPlayer *pPlayer = nullptr;
	if(IsSpectator)
	{
		pPlayer = new(ClientID) CInfClassPlayer(this, ClientID, TEAM_SPECTATORS);
	}
	else
	{
		const int StartTeam = Config()->m_SvTournamentMode ? TEAM_SPECTATORS : GetAutoTeam(ClientID);
		pPlayer = new(ClientID) CInfClassPlayer(this, ClientID, StartTeam);
	}

	if(pData)
	{
		InfclassPlayerPersistantData *pPersistent = static_cast<InfclassPlayerPersistantData *>(pData);
		pPlayer->SetPreferredClass(pPersistent->m_PreferredClass);
		pPlayer->SetAntiPingEnabled(pPersistent->m_AntiPing);
		pPlayer->SetInfectionTimestamp(pPersistent->m_LastInfectionTime);
	}

	if(IsInfectionStarted())
	{
		PLAYERCLASS c = ChooseInfectedClass(pPlayer);
		pPlayer->SetClass(c);
	}

	return pPlayer;
}

int CInfClassGameController::PersistentClientDataSize() const
{
	return sizeof(InfclassPlayerPersistantData);
}

bool CInfClassGameController::GetClientPersistentData(int ClientID, void *pData) const
{
	const CInfClassPlayer *pPlayer = GetPlayer(ClientID);
	if(!pPlayer)
		return false;

	InfclassPlayerPersistantData *pPersistent = static_cast<InfclassPlayerPersistantData *>(pData);
	pPersistent->m_PreferredClass = pPlayer->GetPreferredClass();
	pPersistent->m_AntiPing = pPlayer->GetAntiPingEnabled();
	pPersistent->m_LastInfectionTime = pPlayer->GetInfectionTimestamp();
	return true;
}

void CInfClassGameController::SnapMapMenu(int SnappingClient, CNetObj_GameInfo *pGameInfoObj)
{
	if(SnappingClient < 0)
		return;

	CInfClassPlayer *pPlayer = GetPlayer(SnappingClient);
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

		CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			switch(Iter.Player()->GetClass())
			{
				case PLAYERCLASS_NINJA:
				case PLAYERCLASS_MERCENARY:
				case PLAYERCLASS_SNIPER:
					Support++;
					break;
				case PLAYERCLASS_ENGINEER:
				case PLAYERCLASS_SOLDIER:
				case PLAYERCLASS_SCIENTIST:
				case PLAYERCLASS_BIOLOGIST:
					Defender++;
					break;
				case PLAYERCLASS_MEDIC:
					Medic++;
					break;
				case PLAYERCLASS_HERO:
					Hero++;
					break;
				case PLAYERCLASS_LOOPER:
					Defender++;
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

void CInfClassGameController::FallInLoveIfInfectedEarly(CInfClassCharacter *pCharacter)
{
	if(!pCharacter)
		return;

	if(IsInfectionStarted())
		return;

	const int RemainingTicks = m_RoundStartTick + Server()->TickSpeed() * GetInfectionDelay() - Server()->Tick();
	float LoveDuration = RemainingTicks / static_cast<float>(Server()->TickSpeed()) + 0.25;

	pCharacter->LoveEffect(LoveDuration);
}

void CInfClassGameController::RewardTheKiller(CInfClassCharacter *pVictim, CInfClassPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return;

	if(pKiller != pVictim->GetPlayer())
	{
		pKiller->IncreaseNumberKills();

		CInfClassCharacter *pKillerCharacter = pKiller->GetCharacter();
		if(pKillerCharacter)
		{
			// set attacker's face to happy (taunt!)
			pKillerCharacter->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
			pKillerCharacter->CheckSuperWeaponAccess();

			if(pKillerCharacter->GetPlayerClass() == PLAYERCLASS_MERCENARY)
			{
				pKillerCharacter->AddAmmo(WEAPON_LASER, 3);
			}
		}
	}

	if(pKiller->IsHuman())
	{
		CInfClassHuman *pKillerHuman = CInfClassHuman::GetInstance(pKiller);

		if(pKiller == pVictim->GetPlayer())
		{
			Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), SCOREEVENT_HUMAN_SUICIDE, pKiller->GetClass(), Server()->ClientName(pKiller->GetCID()), Console());
		}
		else
		{
			if(pVictim->IsZombie())
			{
				int ScoreEvent = SCOREEVENT_KILL_INFECTED;
				bool ClassSpecialProcessingEnabled = (GetRoundType() != ROUND_TYPE::FUN) || (GetPlayerClassProbability(pVictim->GetPlayerClass()) == 0);
				if(ClassSpecialProcessingEnabled)
				{
					switch(pVictim->GetPlayerClass())
					{
						case PLAYERCLASS_WITCH:
							ScoreEvent = SCOREEVENT_KILL_WITCH;
							GameServer()->SendChatTarget_Localization(pKiller->GetCID(), CHATCATEGORY_SCORE, _("You have killed a witch, +5 points"), NULL);
							break;
						case PLAYERCLASS_UNDEAD:
							ScoreEvent = SCOREEVENT_KILL_UNDEAD;
							GameServer()->SendChatTarget_Localization(pKiller->GetCID(), CHATCATEGORY_SCORE, _("You have killed an undead! +5 points"), NULL);
							break;
						default:
							break;
					}
				}

				Server()->RoundStatistics()->OnScoreEvent(pKiller->GetCID(), ScoreEvent, pKiller->GetClass(), Server()->ClientName(pKiller->GetCID()), Console());
				GameServer()->SendScoreSound(pKiller->GetCID());
			}
		
			pKillerHuman->OnKilledCharacter(pVictim->GetCID(), false);
		}
	}
}

int CInfClassGameController::OnCharacterDeath(class CCharacter *pAbstractVictim, class CPlayer *pAbstractKiller, int Weapon)
{
	dbg_msg("server", "CRITICAL ERROR: CInfClassGameController::OnCharacterDeath(class CCharacter *, class CPlayer *, int) must never be called");
	return 0;
}

void CInfClassGameController::OnCharacterDeath(CInfClassCharacter *pVictim, const DeathContext &Context)
{
	const DAMAGE_TYPE DamageType = Context.DamageType;
	const int Killer = Context.Killer;
	const int Assistant = Context.Assistant;

	dbg_msg("server", "OnCharacterDeath: victim: %d, DT: %d, killer: %d, assistant: %d", pVictim->GetCID(), static_cast<int>(DamageType), Killer, Assistant);

	CInfClassPlayer *pKiller = GetPlayer(Killer);
	int Weapon = DamageTypeToWeapon(DamageType);
	RewardTheKiller(pVictim, pKiller, Weapon);

	//Add bonus point for ninja
	if(pVictim->IsZombie() && pVictim->IsFrozen() && pVictim->m_LastFreezer >= 0)
	{
		CInfClassPlayer *pFreezer = GetPlayer(pVictim->m_LastFreezer);
		if(pFreezer && pFreezer->GetCharacterClass() && pFreezer != pKiller)
		{
			pFreezer->GetCharacterClass()->OnKilledCharacter(pVictim->GetCID(), true);
		}
	}

	static const icArray<DAMAGE_TYPE, 4> BadReasonsToDie = {
		DAMAGE_TYPE::GAME, // Disconnect, joining spec, etc
		DAMAGE_TYPE::KILL_COMMAND, // Self kill
		DAMAGE_TYPE::GAME_FINAL_EXPLOSION,
	};
	if(!BadReasonsToDie.Contains(DamageType) && (Killer != pVictim->GetCID()))
	{
		//Find the nearest ghoul
		for(TEntityPtr<CInfClassCharacter> p = GameWorld()->FindFirst<CInfClassCharacter>(); p; ++p)
		{
			if(p->GetPlayerClass() != PLAYERCLASS_GHOUL || p.data() == pVictim)
				continue;
			if(p->GetClass() && p->GetClass()->GetGhoulPercent() >= 1.0f)
				continue;

			float Len = distance(p->m_Pos, pVictim->m_Pos);

			if(p && Len < 800.0f)
			{
				int Points = (pVictim->IsZombie() ? 8 : 14);
				new CFlyingPoint(GameServer(), pVictim->m_Pos, p->GetCID(), Points, pVictim->m_Core.m_Vel);
			}
		}
	}

	// It is important to SendKillMessage before GetClass()->OnCharacterDeath() to keep the correct kill order
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "kill killer='%s' victim='%s' weapon=%d",
		Server()->ClientName(Killer),
		Server()->ClientName(pVictim->GetCID()), Weapon);
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	SendKillMessage(pVictim->GetCID(), Context);

	if(pVictim->GetClass())
	{
		pVictim->GetClass()->OnCharacterDeath(DamageType);
	}

	INFECTION_TYPE InfectionType = INFECTION_TYPE::REGULAR;
	bool ClassSpecialProcessingEnabled = true;

	if((GetRoundType() == ROUND_TYPE::FUN) && !pVictim->IsHuman() && GetPlayerClassProbability(pVictim->GetPlayerClass()))
	{
		ClassSpecialProcessingEnabled = false;
	}

	if(ClassSpecialProcessingEnabled)
	{
		switch(pVictim->GetPlayerClass())
		{
			case PLAYERCLASS_WITCH:
				GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The witch is dead"), NULL);
				GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
				InfectionType = INFECTION_TYPE::RESTORE_INF_CLASS;
				break;
			case PLAYERCLASS_UNDEAD:
				GameServer()->SendBroadcast_Localization(-1, BROADCAST_PRIORITY_GAMEANNOUNCE, BROADCAST_DURATION_GAMEANNOUNCE, _("The undead is finally dead"), NULL);
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
		if(pVictim->GetPlayerClass() == PLAYERCLASS_WITCH)
		{
			GameServer()->CreateSoundGlobal(SOUND_CTF_RETURN);
		}
	}

	if(DamageType != DAMAGE_TYPE::GAME)
	{
		// Do not infect on disconnect or joining spec
		pVictim->GetPlayer()->StartInfection(pKiller, InfectionType);
	}

	bool SelfKill = false;
	switch (DamageType)
	{
	case DAMAGE_TYPE::TURRET_DESTRUCTION:
	case DAMAGE_TYPE::DEATH_TILE:
	case DAMAGE_TYPE::KILL_COMMAND:
		SelfKill = true;
		break;
	default:
		SelfKill = Killer == pVictim->GetCID();
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

	if(GetRoundType() == ROUND_TYPE::FAST)
	{
		RespawnDelay *= 0.5;
	}

	if(m_Warmup > 0)
	{
		RespawnDelay = Server()->TickSpeed() * 0.2f;
	}

	pVictim->GetPlayer()->m_RespawnTick = Server()->Tick() + RespawnDelay;

	if(Context.DamageType == DAMAGE_TYPE::INFECTION_TILE)
	{
		int FreezeDuration = m_Warmup > 0 ? 0 : Config()->m_InfInfzoneFreezeDuration;
		if(FreezeDuration > 0)
		{
			pVictim->Freeze(FreezeDuration, Context.Killer, FREEZEREASON_INFECTION);
		}
	}
}

void CInfClassGameController::OnCharacterSpawned(CInfClassCharacter *pCharacter)
{
	IGameController::OnCharacterSpawn(pCharacter);

	pCharacter->SetTeams(&m_Teams);

	CInfClassPlayer *pPlayer = pCharacter->GetPlayer();
	if(!IsInfectionStarted() && pPlayer->RandomClassChoosen())
	{
		pCharacter->GiveRandomClassSelectionBonus();
	}

	if((GetRoundType() == ROUND_TYPE::FUN) && !IsInfectionStarted() && pCharacter->GetPlayerClass() == PLAYERCLASS_NONE)
	{
		if(pPlayer)
		{
			pPlayer->SetClass(ChooseHumanClass(pPlayer));
		}
	}

	if(pCharacter->IsZombie())
	{
		FallInLoveIfInfectedEarly(pCharacter);
		pCharacter->SetHealthArmor(10, InfectedBonusArmor());
	}
}

void CInfClassGameController::OnClassChooserRequested(CInfClassCharacter *pCharacter)
{
	CInfClassPlayer *pPlayer = pCharacter->GetPlayer();

	if(GetRoundType() == ROUND_TYPE::FUN)
	{
		pPlayer->SetRandomClassChoosen();
		// Read this as "player didn't choose this class"
		pCharacter->GiveRandomClassSelectionBonus();
		pPlayer->CloseMapMenu();
		return;
	}

	const int PreferredClass = pPlayer->GetPreferredClass();
	if(!IsClassChooserEnabled() || (PreferredClass != PLAYERCLASS_INVALID))
	{
		pPlayer->SetClass(ChooseHumanClass(pPlayer));

		if(PreferredClass == PLAYERCLASS_RANDOM)
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

void CInfClassGameController::CheckRoundFailed()
{
	if(m_Warmup)
		return;

	if(IsGameOver())
		return;

	if(Config()->m_InfTrainingMode)
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
		CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		bool HasNonGameInfectionCauses = false;
		while(Iter.Next())
		{
			const CInfClassPlayer *pPlayer = Iter.Player();
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

void CInfClassGameController::DoWincheck()
{
	if(Config()->m_InfTrainingMode)
		return;

	int NumHumans = 0;
	int NumInfected = 0;
	GetPlayerCounter(-1, NumHumans, NumInfected);

	// One infected can win in some rounds; we have a check if this is a valid situation in CheckRoundFailed()
	if(m_InfectedStarted && NumHumans == 0 && NumInfected >= 1)
	{
		AnnounceTheWinner(NumHumans);
		return;
	}

	const int Seconds = (Server()->Tick() - m_RoundStartTick) / ((float)Server()->TickSpeed());
	//Start the final explosion if the time is over
	if(m_InfectedStarted && !m_ExplosionStarted && GetTimeLimit() > 0 && Seconds >= GetTimeLimit() * 60)
	{
		for(TEntityPtr<CInfClassCharacter> p = GameWorld()->FindFirst<CInfClassCharacter>(); p; ++p)
		{
			if(p->IsZombie())
			{
				GameServer()->SendEmoticon(p->GetCID(), EMOTICON_GHOST);
			}
			else
			{
				GameServer()->SendEmoticon(p->GetCID(), EMOTICON_EYES);
			}
		}
		m_ExplosionStarted = true;
	}

	//Do the final explosion
	if(m_ExplosionStarted)
	{
		bool NewExplosion = false;

		for(int j=0; j<m_MapHeight; j++)
		{
			for(int i=0; i<m_MapWidth; i++)
			{
				if((m_GrowingMap[j*m_MapWidth+i] & 1) && (
					(i > 0 && m_GrowingMap[j*m_MapWidth+i-1] & 2) ||
					(i < m_MapWidth-1 && m_GrowingMap[j*m_MapWidth+i+1] & 2) ||
					(j > 0 && m_GrowingMap[(j-1)*m_MapWidth+i] & 2) ||
					(j < m_MapHeight-1 && m_GrowingMap[(j+1)*m_MapWidth+i] & 2)
				))
				{
					NewExplosion = true;
					m_GrowingMap[j*m_MapWidth+i] |= 8;
					m_GrowingMap[j*m_MapWidth+i] &= ~1;
					if(random_prob(0.1f))
					{
						vec2 TilePos = vec2(16.0f, 16.0f) + vec2(i*32.0f, j*32.0f);
						static const int Damage = 0;
						CreateExplosion(TilePos, -1, DAMAGE_TYPE::NO_DAMAGE, Damage);
						GameServer()->CreateSound(TilePos, SOUND_GRENADE_EXPLODE);
					}
				}
			}
		}

		for(int j=0; j<m_MapHeight; j++)
		{
			for(int i=0; i<m_MapWidth; i++)
			{
				if(m_GrowingMap[j*m_MapWidth+i] & 8)
				{
					m_GrowingMap[j*m_MapWidth+i] &= ~8;
					m_GrowingMap[j*m_MapWidth+i] |= 2;
				}
			}
		}

		for(TEntityPtr<CInfClassCharacter> p = GameWorld()->FindFirst<CInfClassCharacter>(); p; ++p)
		{
			if(p->IsHuman())
				continue;

			int tileX = static_cast<int>(round(p->m_Pos.x))/32;
			int tileY = static_cast<int>(round(p->m_Pos.y))/32;

			if(tileX < 0) tileX = 0;
			if(tileX >= m_MapWidth) tileX = m_MapWidth-1;
			if(tileY < 0) tileY = 0;
			if(tileY >= m_MapHeight) tileY = m_MapHeight-1;

			if(m_GrowingMap[tileY*m_MapWidth+tileX] & 2 && p->GetPlayer())
			{
				p->Die(p->GetCID(), DAMAGE_TYPE::GAME_FINAL_EXPLOSION);
			}
		}

		//If no more explosions, game over, decide who win
		if(!NewExplosion)
		{
			AnnounceTheWinner(NumHumans);
		}
	}
}

bool CInfClassGameController::IsSpawnable(vec2 Pos, int TeleZoneIndex)
{
	//First check if there is a tee too close
	CCharacter *aEnts[MAX_CLIENTS];
	int Num = GameWorld()->FindEntities(Pos, 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	
	for(int c = 0; c < Num; ++c)
	{
		if(distance(aEnts[c]->m_Pos, Pos) <= 60)
			return false;
	}
	
	//Check the center
	int TeleIndex = GameServer()->Collision()->GetZoneValueAt(m_ZoneHandle_icTeleport, Pos);
	if(GameServer()->Collision()->CheckPoint(Pos))
		return false;
	if(TeleZoneIndex && TeleIndex == TeleZoneIndex)
		return false;
	
	//Check the border of the tee. Kind of extrem, but more precise
	for(int i=0; i<16; i++)
	{
		float Angle = i * (2.0f * pi / 16.0f);
		vec2 CheckPos = Pos + vec2(cos(Angle), sin(Angle)) * 30.0f;
		TeleIndex = GameServer()->Collision()->GetZoneValueAt(m_ZoneHandle_icTeleport, CheckPos);
		if(GameServer()->Collision()->CheckPoint(CheckPos))
			return false;
		if(TeleZoneIndex && TeleIndex == TeleZoneIndex)
			return false;
	}
	
	return true;
}

bool CInfClassGameController::TryRespawn(CInfClassPlayer *pPlayer, SpawnContext *pContext)
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

	if(m_InfectedStarted)
		pPlayer->StartInfection();
		
	if(pPlayer->IsZombie() && m_ExplosionStarted)
		return false;

	if(m_InfectedStarted && pPlayer->IsZombie() && random_prob(Config()->m_InfProbaSpawnNearWitch / 100.0f))
	{
		CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
		while(Iter.Next())
		{
			if(Iter.Player()->GetCID() == pPlayer->GetCID())
				continue;
			if(Iter.Player()->GetClass() != PLAYERCLASS_WITCH)
				continue;

			CInfClassCharacter *pCharacter = Iter.Player()->GetCharacter();
			if(!pCharacter)
				continue;

			if(pCharacter->IsFrozen())
				continue;

			CInfClassInfected *pInfected = CInfClassInfected::GetInstance(pCharacter);

			if(pInfected->FindWitchSpawnPosition(pContext->SpawnPos))
			{
				pContext->SpawnType = SpawnContext::WitchSpawn;
				return true;
			}
		}
	}
			
	int Type = (pPlayer->IsZombie() ? 0 : 1);

	if(m_SpawnPoints[Type].size() == 0)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "The map has no spawn points");
		return false;
	}

	// get spawn point
	int RandomShift = random_int(0, m_SpawnPoints[Type].size()-1);
	for(int i = 0; i < m_SpawnPoints[Type].size(); i++)
	{
		int I = (i + RandomShift)%m_SpawnPoints[Type].size();
		if(IsSpawnable(m_SpawnPoints[Type][I], 0))
		{
			pContext->SpawnPos = m_SpawnPoints[Type][I];
			pContext->SpawnType = SpawnContext::MapSpawn;
			return true;
		}
	}
	
	return false;
}

PLAYERCLASS CInfClassGameController::ChooseHumanClass(const CInfClassPlayer *pPlayer) const
{
	//Get information about existing humans
	int nbSupport = 0;
	int nbDefender = 0;
	std::map<int, int> nbClass;
	for (int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; ++PlayerClass)
		nbClass[PlayerClass] = 0;

	CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		const int AnotherPlayerClass = Iter.Player()->GetClass();
		if ((AnotherPlayerClass < START_HUMANCLASS + 1) || (AnotherPlayerClass > END_HUMANCLASS - 1))
			continue;
		if (IsDefenderClass(AnotherPlayerClass))
			nbDefender++;
		if (IsSupportClass(AnotherPlayerClass))
			nbSupport++;
		nbClass[AnotherPlayerClass]++;
	}

	double Probability[NB_HUMANCLASS];

	auto GetClassProbabilityRef = [&Probability](int PlayerClass) -> double & {
		return Probability[PlayerClass - START_HUMANCLASS - 1];
	};

	int AvailableClasses = 0;
	for(int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; ++PlayerClass)
	{
		double &ClassProbability = GetClassProbabilityRef(PlayerClass);
		ClassProbability = GetPlayerClassEnabled(PlayerClass) ? 1.0f : 0.0f;
		if(GetRoundType() != ROUND_TYPE::FUN)
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

	// Random is not fair enough. We keep the last classes took by the player, and avoid to give those again
	if(GetRoundType() != ROUND_TYPE::FUN)
	{
		if(AvailableClasses > 1)
		{
			// if normal round is being played
			int PrevClass = pPlayer->GetPreviousHumanClass();
			if(PrevClass != PLAYERCLASS_INVALID)
			{
				GetClassProbabilityRef(PrevClass) = 0.0f;
			}
		}
	}

	PLAYERCLASS PreferredClass = pPlayer->GetPreferredClass();
	if(PreferredClass != PLAYERCLASS_INVALID)
	{
		if(PreferredClass != PLAYERCLASS_RANDOM)
		{
			if(GetClassProbabilityRef(PreferredClass) > 0)
			{
				return PreferredClass;
			}
		}
	}

	int Result = START_HUMANCLASS + 1 + random_distribution(Probability, Probability + NB_HUMANCLASS);
	return static_cast<PLAYERCLASS>(Result);
}

PLAYERCLASS CInfClassGameController::ChooseInfectedClass(const CInfClassPlayer *pPlayer) const
{
	// if(pPlayer->InfectionType() == INFECTION_TYPE::RESTORE_INF_CLASS)
	{
		PLAYERCLASS PrevClass = pPlayer->GetPreviousInfectedClass();
		if(PrevClass != PLAYERCLASS_INVALID)
		{
			return PrevClass;
		}
	}

	//Get information about existing infected
	int nbInfected = 0;
	std::map<int, int> nbClass;
	for (int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; ++PlayerClass)
		nbClass[PlayerClass] = 0;

	CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	int PlayersCount = 0;
	while(Iter.Next())
	{
		++PlayersCount;
		const int AnotherPlayerClass = Iter.Player()->GetClass();
		if(Iter.Player()->IsZombie()) nbInfected++;
		nbClass[AnotherPlayerClass]++;
	}

	int InitiallyInfected = GetMinimumInfectedForPlayers(PlayersCount);
	
	double Probability[NB_INFECTEDCLASS];
	for (int PlayerClass = START_INFECTEDCLASS + 1; PlayerClass < END_INFECTEDCLASS; ++PlayerClass)
	{
		double &ClassProbability = Probability[PlayerClass - START_INFECTEDCLASS - 1];
		ClassProbability = GetPlayerClassProbability(PlayerClass);
		if(GetRoundType() == ROUND_TYPE::FUN)
		{
			// We care only about the class enablement
			continue;
		}

		switch(PlayerClass)
		{
			case PLAYERCLASS_BAT:
				if(nbInfected <= InitiallyInfected)
				{
					// We can't just set the proba to 0, because it would break a config
					// with all classes except the Bat set to 0.
					ClassProbability = ClassProbability / 10000.0;
				}
				break;
			case PLAYERCLASS_GHOUL:
				if(nbInfected < Config()->m_InfGhoulThreshold)
					ClassProbability = 0;
				break;
			case PLAYERCLASS_WITCH:
			case PLAYERCLASS_UNDEAD:
				if ((nbInfected <= 2) || nbClass[PlayerClass] > 0)
					ClassProbability = 0;
				break;
			default:
				break;
		}
	}
	
	int Class = START_INFECTEDCLASS + 1 + random_distribution(Probability, Probability + NB_INFECTEDCLASS);

	int Seconds = (Server()->Tick()-m_RoundStartTick)/((float)Server()->TickSpeed());
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "infected victim='%s' duration='%d' class='%d'", 
		Server()->ClientName(pPlayer->GetCID()), Seconds, Class);
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	return static_cast<PLAYERCLASS>(Class);
}

bool CInfClassGameController::GetPlayerClassEnabled(int PlayerClass) const
{
	switch(PlayerClass)
	{
		case PLAYERCLASS_ENGINEER:
			return Config()->m_InfEnableEngineer;
		case PLAYERCLASS_SOLDIER:
			return Config()->m_InfEnableSoldier;
		case PLAYERCLASS_SCIENTIST:
			return Config()->m_InfEnableScientist;
		case PLAYERCLASS_BIOLOGIST:
			return Config()->m_InfEnableBiologist;
		case PLAYERCLASS_MEDIC:
			return Config()->m_InfEnableMedic;
		case PLAYERCLASS_HERO:
			return Config()->m_InfEnableHero;
		case PLAYERCLASS_NINJA:
			return Config()->m_InfEnableNinja;
		case PLAYERCLASS_MERCENARY:
			return Config()->m_InfEnableMercenary;
		case PLAYERCLASS_SNIPER:
			return Config()->m_InfEnableSniper;
		case PLAYERCLASS_LOOPER:
			return Config()->m_InfEnableLooper;
		default:
			return false;
	}
}

int CInfClassGameController::GetMinPlayersForClass(int PlayerClass) const
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_ENGINEER:
			return Config()->m_InfMinPlayersForEngineer;
		default:
			return 0;
	}
}

int CInfClassGameController::GetClassPlayerLimit(int PlayerClass) const
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_MEDIC:
			return Config()->m_InfMedicLimit;
		case PLAYERCLASS_HERO:
			return Config()->m_InfHeroLimit;
		case PLAYERCLASS_WITCH:
			return Config()->m_InfWitchLimit;
		default:
			return Config()->m_SvMaxClients;
	}
}

int CInfClassGameController::GetPlayerClassProbability(int PlayerClass) const
{
	switch (PlayerClass)
	{
		case PLAYERCLASS_SMOKER:
			return Config()->m_InfProbaSmoker;
		case PLAYERCLASS_BOOMER:
			return Config()->m_InfProbaBoomer;
		case PLAYERCLASS_HUNTER:
			return Config()->m_InfProbaHunter;
		case PLAYERCLASS_BAT:
			return Config()->m_InfProbaBat;
		case PLAYERCLASS_GHOST:
			return Config()->m_InfProbaGhost;
		case PLAYERCLASS_SPIDER:
			return Config()->m_InfProbaSpider;
		case PLAYERCLASS_GHOUL:
			return Config()->m_InfProbaGhoul;
		case PLAYERCLASS_SLUG:
			return Config()->m_InfProbaSlug;
		case PLAYERCLASS_VOODOO:
			return Config()->m_InfProbaVoodoo;
		case PLAYERCLASS_WITCH:
			return Config()->m_InfProbaWitch;
		case PLAYERCLASS_UNDEAD:
			return Config()->m_InfProbaUndead;
		default:
			break;
	}

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "WARNING: Invalid GetPlayerClassProbability() call");
	return 0;
}

int CInfClassGameController::GetInfectedCount(int InfectedPlayerClass) const
{
	int Count = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CInfClassPlayer *pPlayer = GetPlayer(i);
		if(!pPlayer)
			continue;

		if(!pPlayer->IsActuallyZombie())
			continue;

		if(InfectedPlayerClass != PLAYERCLASS_INVALID)
		{
			if(pPlayer->GetClass() != InfectedPlayerClass)
				continue;
		}

		Count++;
	}
	return Count;
}

ROUND_TYPE CInfClassGameController::GetRoundType() const
{
	return m_RoundType;
}

void CInfClassGameController::QueueRoundType(ROUND_TYPE RoundType)
{
	dbg_msg("controller", "Queued round: %s", toString(RoundType));
	m_QueuedRoundType = RoundType;
}

CLASS_AVAILABILITY CInfClassGameController::GetPlayerClassAvailability(int PlayerClass, const CInfClassPlayer *pForPlayer) const
{
	if(!GetPlayerClassEnabled(PlayerClass))
		return CLASS_AVAILABILITY::DISABLED;

	int ActivePlayerCount = Server()->GetActivePlayerCount();
	int MinPlayersForClass = GetMinPlayersForClass(PlayerClass);
	if (ActivePlayerCount < MinPlayersForClass)
		return CLASS_AVAILABILITY::NEED_MORE_PLAYERS;

	int nbSupport = 0;
	int nbDefender = 0;
	std::map<int, int> nbClass;
	for (int PlayerClass = START_HUMANCLASS + 1; PlayerClass < END_HUMANCLASS; ++PlayerClass)
		nbClass[PlayerClass] = 0;

	CInfClassPlayerIterator<PLAYERITER_INGAME> Iter(GameServer()->m_apPlayers);
	while(Iter.Next())
	{
		const int AnotherPlayerClass = Iter.Player()->GetClass();
		if ((AnotherPlayerClass < START_HUMANCLASS + 1) || (AnotherPlayerClass > END_HUMANCLASS - 1))
			continue;
		if (IsDefenderClass(AnotherPlayerClass))
			nbDefender++;
		if (IsSupportClass(AnotherPlayerClass))
			nbSupport++;
		nbClass[AnotherPlayerClass]++;
	}

	if(IsDefenderClass(PlayerClass) && (nbDefender >= Config()->m_InfDefenderLimit))
		return CLASS_AVAILABILITY::LIMIT_EXCEEDED;

	if(IsSupportClass(PlayerClass) && (nbSupport >= Config()->m_InfSupportLimit))
		return CLASS_AVAILABILITY::LIMIT_EXCEEDED;

	if (nbClass[PlayerClass] >= GetClassPlayerLimit(PlayerClass))
		return CLASS_AVAILABILITY::LIMIT_EXCEEDED;
	
	return CLASS_AVAILABILITY::AVAILABLE;
}

bool CInfClassGameController::CanVote()
{
	return !m_InfectedStarted;
}
