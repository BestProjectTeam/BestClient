/* Copyright © 2026 BestProject Team */
#include "fast_practice.h"

#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/components/bestclient/inputs.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/client/projectile_data.h>
#include <game/gamecore.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <limits>

namespace
{
	void NeutralizeInput(CNetObj_PlayerInput &Input)
	{
		Input.m_Direction = 0;
		Input.m_Jump = 0;
		Input.m_Hook = 0;

		if((Input.m_Fire & 1) != 0)
			Input.m_Fire++;
		Input.m_Fire &= INPUT_STATE_MASK;

		Input.m_NextWeapon = 0;
		Input.m_PrevWeapon = 0;
		Input.m_WantedWeapon = 0;

		if(Input.m_TargetX == 0 && Input.m_TargetY == 0)
		{
			Input.m_TargetX = 1;
			Input.m_TargetY = 0;
		}
	}

	int ReleasedFireState(int FireState)
	{
		FireState &= INPUT_STATE_MASK;
		if((FireState & 1) != 0)
			FireState = (FireState + 1) & INPUT_STATE_MASK;
		return FireState;
	}

	bool IsFrozenState(const CCharacter *pChar)
	{
		if(!pChar)
			return false;
		const CCharacterCore &Core = *pChar->Core();
		return pChar->m_FreezeTime > 0 || Core.m_DeepFrozen || Core.m_LiveFrozen || Core.m_FreezeEnd != 0;
	}

	int ClampWeaponId(int WeaponId)
	{
		return std::clamp(WeaponId, -1, NUM_WEAPONS - 1);
	}

	std::string LowercaseCopy(std::string Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return Text;
	}

	struct STrackedProjectile
	{
		int m_Owner = -1;
		int m_StartTick = 0;
		int m_Type = WEAPON_GUN;
		int m_TuneZone = 0;
		vec2 m_StartPos = vec2(0.0f, 0.0f);
		vec2 m_StartVel = vec2(0.0f, 0.0f);
	};

	bool SameProjectile(const STrackedProjectile &A, const STrackedProjectile &B)
	{
		return A.m_Owner == B.m_Owner &&
		       A.m_StartTick == B.m_StartTick &&
		       A.m_Type == B.m_Type &&
		       A.m_TuneZone == B.m_TuneZone &&
		       distance(A.m_StartPos, B.m_StartPos) < 0.01f &&
		       distance(A.m_StartVel, B.m_StartVel) < 0.01f;
	}

	bool IsTrackedExplosive(const CProjectileData &Data, int LocalClientId, int DummyClientId)
	{
		const bool PracticeOwned = Data.m_Owner == LocalClientId || (DummyClientId >= 0 && Data.m_Owner == DummyClientId);
		return PracticeOwned && (Data.m_Explosive || Data.m_Type == WEAPON_GRENADE);
	}

	void CollectTrackedProjectiles(CGameWorld &World, int LocalClientId, int DummyClientId, std::vector<STrackedProjectile> &vOut)
	{
		vOut.clear();
		for(auto *pProj = (CProjectile *)World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pProj; pProj = (CProjectile *)pProj->TypeNext())
		{
			const CProjectileData Data = pProj->GetData();
			if(!IsTrackedExplosive(Data, LocalClientId, DummyClientId))
				continue;

			STrackedProjectile Proj;
			Proj.m_Owner = Data.m_Owner;
			Proj.m_StartTick = Data.m_StartTick;
			Proj.m_Type = Data.m_Type;
			Proj.m_TuneZone = Data.m_TuneZone;
			Proj.m_StartPos = Data.m_StartPos;
			Proj.m_StartVel = Data.m_StartVel;
			vOut.push_back(Proj);
		}
	}

	vec2 CalcTrackedProjectilePos(const STrackedProjectile &Proj, int Tick, int TickSpeed, const CTuningParams *pTuning)
	{
		float Curvature = 0.0f;
		float Speed = 0.0f;
		if(Proj.m_Type == WEAPON_GRENADE)
		{
			Curvature = pTuning->m_GrenadeCurvature;
			Speed = pTuning->m_GrenadeSpeed;
		}
		else if(Proj.m_Type == WEAPON_SHOTGUN)
		{
			Curvature = pTuning->m_ShotgunCurvature;
			Speed = pTuning->m_ShotgunSpeed;
		}
		else
		{
			Curvature = pTuning->m_GunCurvature;
			Speed = pTuning->m_GunSpeed;
		}

		const float Ct = std::max(0.0f, (Tick - Proj.m_StartTick) / (float)TickSpeed);
		return CalcPos(Proj.m_StartPos, Proj.m_StartVel, Curvature, Speed, Ct);
	}
} // namespace

void CFastPractice::ConFastPracticeToggle(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CFastPractice *>(pUserData);
	pSelf->Toggle();
}

void CFastPractice::ResetPracticeState()
{
	m_Enabled = false;
	m_Ready = false;
	m_NeedsRebuild = false;
	m_RequireDummy = false;
	m_EnableLocalClientId = -1;
	m_EnableDummyClientId = -1;
	m_HasDummyAnchor = false;
	m_SuppressFireOnNextPredictTick = false;
	m_InputSuppressTicks = 0;
	m_LastClDummy = g_Config.m_ClDummy;
	m_LastResolvedLocalClientId = -1;
	m_LastResolvedDummyClientId = -1;
	m_aHasServerLockedTargets.fill(false);
	m_aServerLockedTargets.fill(ivec2(1, 0));
	m_aFrozenTargetValid.fill(false);
	m_aSpawnPosValid.fill(false);
	m_aSafePosValid.fill(false);
	m_aFastRenderValid.fill(false);
	m_aPublishedValid.fill(false);
	m_aLastEventTick.fill(-1);
	m_MainAnchor = {};
	m_DummyAnchor = {};
	m_PracticeWorld.Clear();
	m_PracticePrevWorld.Clear();
	ResetStoredInputs();
	ResetAttackTickHistory();
	ResetCommandState();
	ResetPracticeRaceStates();
}

void CFastPractice::ResetCommandState()
{
	for(auto &State : m_aPracticeCommandState)
		State = {};
}

void CFastPractice::ResetPracticeRaceStates()
{
	for(auto &State : m_aPracticeRaceState)
		State = {};
}

void CFastPractice::OnReset()
{
	ResetPracticeState();
}

void CFastPractice::OnMapLoad()
{
	ResetPracticeState();
}

void CFastPractice::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState == IClient::STATE_OFFLINE || NewState == IClient::STATE_CONNECTING || NewState == IClient::STATE_LOADING)
	{
		InvalidateBufferedInputState();
		ResetPracticeState();
	}
}

bool CFastPractice::CanEnable() const
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	if(!GameClient()->m_Snap.m_pLocalInfo || !GameClient()->m_Snap.m_pLocalCharacter)
		return false;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active || GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS)
		return false;

	if(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
		return false;

	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return false;

	if(!GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Active || GameClient()->m_aClients[LocalClientId].m_Paused)
		return false;

	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	if(ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS)
	{
		if(!GameClient()->m_Snap.m_aCharacters[ActiveClientId].m_Active || GameClient()->m_aClients[ActiveClientId].m_Paused)
			return false;
	}

	return true;
}


int CFastPractice::CurrentLocalPracticeId() const
{
	if(!m_Enabled)
		return -1;

	// Use the currently controlled connection so switching cl_dummy swaps control.
	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	if(ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS &&
		(ActiveClientId == m_EnableLocalClientId || ActiveClientId == m_EnableDummyClientId))
	{
		return ActiveClientId;
	}

	const int InactiveClientId = GameClient()->m_aLocalIds[ActiveConn ^ 1];
	if(InactiveClientId >= 0 && InactiveClientId < MAX_CLIENTS &&
		(InactiveClientId == m_EnableLocalClientId || InactiveClientId == m_EnableDummyClientId))
	{
		return InactiveClientId;
	}

	return -1;
}

bool CFastPractice::ResolvePracticeRoles(int &LocalClientId, int &DummyClientId) const
{
	LocalClientId = CurrentLocalPracticeId();
	DummyClientId = -1;

	if(LocalClientId < 0)
	{
		const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
					(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
		if(Spectating)
			LocalClientId = m_EnableLocalClientId;
	}
	if(LocalClientId < 0)
		return false;
	if(LocalClientId != m_EnableLocalClientId && LocalClientId != m_EnableDummyClientId)
		return false;

	if(m_RequireDummy)
		DummyClientId = LocalClientId == m_EnableLocalClientId ? m_EnableDummyClientId : m_EnableLocalClientId;

	return true;
}


int CFastPractice::ControlledPracticeId() const
{
	return CurrentLocalPracticeId();
}

int CFastPractice::PartnerPracticeId() const
{
	if(!m_Enabled || !m_RequireDummy)
		return -1;
	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
		return -1;
	return DummyClientId;
}

bool CFastPractice::IsPracticeParticipant(int ClientId) const
{
	if(!m_Enabled || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	return ClientId == m_EnableLocalClientId || (m_EnableDummyClientId >= 0 && ClientId == m_EnableDummyClientId);
}

int CFastPractice::CurrentPracticeDummyId() const
{
	if(!Active() || !m_RequireDummy)
		return -1;
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	if(Spectating)
		return -1;

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
		return -1;
	return DummyClientId;
}

bool CFastPractice::GetLocalRaceState(SLocalRaceState &State) const
{
	State = {};
	if(!Active() ||
		GameClient()->m_Snap.m_SpecInfo.m_Active ||
		(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS))
	{
		return false;
	}

	const int ClientId = CurrentLocalPracticeId();
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId);
	if(!pChar)
		return false;

	const SPracticeRaceState &RaceState = m_aPracticeRaceState[ClientId];
	State.m_Position = pChar->Core()->m_Pos;
	State.m_CurrentTick = m_PracticeWorld.GameTick();
	State.m_StartTick = RaceState.m_StartTick >= 0 ? RaceState.m_StartTick : GameClient()->LastRaceTick();
	State.m_Finished = RaceState.m_Finished;
	return true;
}

void CFastPractice::UpdatePracticeRaceState(int ClientId, const CCharacter *pChar, int Tick)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pChar || !Collision())
		return;

	SPracticeRaceState &RaceState = m_aPracticeRaceState[ClientId];
	if(RaceState.m_StartTick < 0 && !RaceState.m_Finished &&
		ClientId == CurrentLocalPracticeId() && GameClient()->LastRaceTick() >= 0)
	{
		RaceState.m_StartTick = GameClient()->LastRaceTick();
	}

	const auto HasTile = [&](int Index, int WantedTile) {
		return Index >= 0 && (Collision()->GetTileIndex(Index) == WantedTile || Collision()->GetFrontTileIndex(Index) == WantedTile);
	};

	const vec2 Pos = pChar->GetPos();
	bool CornerOnStart = false;
	bool CornerOnFinish = false;
	const float Offset = pChar->GetProximityRadius() / 3.0f;
	static constexpr vec2 s_aCornerOffsets[] = {
		{1.0f, -1.0f},
		{1.0f, 1.0f},
		{-1.0f, -1.0f},
		{-1.0f, 1.0f},
	};
	for(const vec2 CornerOffset : s_aCornerOffsets)
	{
		const int Index = Collision()->GetPureMapIndex(Pos + CornerOffset * Offset);
		CornerOnStart |= HasTile(Index, TILE_START);
		CornerOnFinish |= HasTile(Index, TILE_FINISH);
	}

	const auto HandleMapIndex = [&](int Index) {
		const bool WasStarted = RaceState.m_StartTick >= 0 && !RaceState.m_Finished;
		if(CornerOnStart || HasTile(Index, TILE_START))
		{
			RaceState.m_StartTick = Tick;
			RaceState.m_Finished = false;
		}
		if((CornerOnFinish || HasTile(Index, TILE_FINISH)) && WasStarted)
			RaceState.m_Finished = true;
	};

	const std::vector<int> vIndices = Collision()->GetMapIndices(pChar->m_PrevPos, Pos);
	if(vIndices.empty())
		HandleMapIndex(Collision()->GetMapIndex(Pos));
	else
	{
		for(const int Index : vIndices)
			HandleMapIndex(Index);
	}
}

bool CFastPractice::ForcePredictWeapons() const
{
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	return Active() && !Spectating;
}

bool CFastPractice::ForcePredictGrenade() const
{
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	return Active() && !Spectating;
}

bool CFastPractice::ForcePredictGunfire() const
{
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	return Active() && !Spectating;
}

bool CFastPractice::ForcePredictPlayers() const
{
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	return Active() && !Spectating;
}

void CFastPractice::PrunePracticeWorld(CGameWorld &World) const
{
	for(CCharacter *pChar = (CCharacter *)World.FindFirst(CGameWorld::ENTTYPE_CHARACTER), *pCharNext = nullptr; pChar; pChar = pCharNext)
	{
		pCharNext = (CCharacter *)pChar->TypeNext();
		const int ClientId = pChar->GetCid();
		if(ClientId != m_EnableLocalClientId && ClientId != m_EnableDummyClientId)
			pChar->Destroy();
	}

	for(CProjectile *pProj = (CProjectile *)World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE), *pProjNext = nullptr; pProj; pProj = pProjNext)
	{
		pProjNext = (CProjectile *)pProj->TypeNext();
		const int Owner = pProj->GetOwner();
		if(Owner >= 0 && Owner != m_EnableLocalClientId && Owner != m_EnableDummyClientId)
			pProj->Destroy();
	}

	for(CLaser *pLaser = (CLaser *)World.FindFirst(CGameWorld::ENTTYPE_LASER), *pLaserNext = nullptr; pLaser; pLaser = pLaserNext)
	{
		pLaserNext = (CLaser *)pLaser->TypeNext();
		const int Owner = pLaser->GetOwner();
		if(Owner >= 0 && Owner != m_EnableLocalClientId && Owner != m_EnableDummyClientId)
			pLaser->Destroy();
	}
}

void CFastPractice::SyncPracticeWorldConfig(CGameWorld &World)
{
	World.m_WorldConfig = GameClient()->m_GameWorld.m_WorldConfig;
	World.m_WorldConfig.m_PredictWeapons = true;
	World.m_WorldConfig.m_PredictFreeze = true;
	World.m_WorldConfig.m_PredictTiles = true;
	World.m_WorldConfig.m_PredictTeleports = true;
	World.m_WorldConfig.m_PredictDDRace = true;
	World.m_WorldConfig.m_PredictEvents = true;
	World.m_Teams = GameClient()->m_Teams;
}

bool CFastPractice::Rebuild()
{
	if(m_EnableLocalClientId < 0 || m_EnableLocalClientId >= MAX_CLIENTS)
		return false;

	m_PracticeWorld.CopyWorldClean(&GameClient()->m_GameWorld);
	PrunePracticeWorld(m_PracticeWorld);
	SyncPracticeWorldConfig(m_PracticeWorld);
	m_PracticeWorld.m_PredictedEvents.clear();
	ResetPracticeRaceStates();

	if(!m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
	{
		m_Ready = false;
		return false;
	}
	if(m_RequireDummy && !m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
	{
		m_Ready = false;
		return false;
	}

	m_aSpawnPosValid.fill(false);
	m_aSafePosValid.fill(false);
	m_aFastRenderValid.fill(false);
	m_aLastEventTick.fill(-1);

	CaptureAnchorsFromSnapshot();
	CaptureFrozenTargets();
	CaptureServerLockedTargets();
	m_LastClDummy = g_Config.m_ClDummy;

	const int aIds[] = {m_EnableLocalClientId, m_EnableDummyClientId};
	for(int ClientId : aIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId))
		{
			const vec2 Pos = pChar->Core()->m_Pos;
			m_aSpawnPos[ClientId] = Pos;
			m_aSpawnPosValid[ClientId] = true;
			if(IsSafeRescuePosition(Pos, pChar->GetProximityRadius()))
			{
				m_aSafePos[ClientId] = Pos;
				m_aSafePosValid[ClientId] = true;
			}
			CNetObj_PlayerInput Neutral = {};
			Neutral.m_TargetY = -1;
			pChar->SetInput(&Neutral);
			pChar->m_CanMoveInFreeze = false;
		}
	}

	ResetStoredInputs();
	const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
		StoreNeutralInput(Dummy != 0, PredTick);

	m_PracticeWorld.m_GameTick = PredTick;
	m_PracticePrevWorld.CopyWorldClean(&m_PracticeWorld);
	SyncPracticeWorldConfig(m_PracticePrevWorld);
	m_PracticePrevWorld.m_PredictedEvents.clear();

	m_Ready = true;
	m_NeedsRebuild = false;
	ResetAttackTickHistory();
	SeedPredictionHistory();
	PublishParticipantCores(m_EnableLocalClientId, m_EnableDummyClientId);
	return true;
}

void CFastPractice::CaptureAnchorsFromSnapshot()
{
	m_MainAnchor = {};
	m_DummyAnchor = {};
	m_HasDummyAnchor = false;

	const auto &&Capture = [&](int ClientId, SAnchorData &Anchor) {
		if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
			return;

		Anchor.m_Valid = true;
		Anchor.m_ClientId = ClientId;
		Anchor.m_Char = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		Anchor.m_HasDDNet = GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedData;
		if(Anchor.m_HasDDNet)
			Anchor.m_DDNet = GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData;
	};

	Capture(m_EnableLocalClientId, m_MainAnchor);
	Capture(m_EnableDummyClientId, m_DummyAnchor);
	m_HasDummyAnchor = m_DummyAnchor.m_Valid;
}

bool CFastPractice::ApplyAnchorToCharacter(CGameWorld &World, const SAnchorData &Anchor) const
{
	if(!Anchor.m_Valid)
		return false;

	CCharacter *pChar = World.GetCharacterById(Anchor.m_ClientId);
	if(!pChar)
		return false;

	CNetObj_Character CharObj = Anchor.m_Char;
	if(Anchor.m_HasDDNet)
	{
		CNetObj_DDNetCharacter DDNetObj = Anchor.m_DDNet;
		pChar->Read(&CharObj, &DDNetObj, true);
	}
	else
	{
		pChar->Read(&CharObj, nullptr, true);
	}

	CNetObj_PlayerInput NeutralInput = {};
	NeutralInput.m_TargetY = -1;
	pChar->SetInput(&NeutralInput);
	pChar->m_CanMoveInFreeze = false;
	return true;
}

void CFastPractice::ResetAttackTickHistory()
{
	m_aLastAttackTick.fill(-1);

	if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
		m_aLastAttackTick[m_EnableLocalClientId] = pChar->GetAttackTick();
	if(m_EnableDummyClientId >= 0)
		if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
			m_aLastAttackTick[m_EnableDummyClientId] = pChar->GetAttackTick();
}

void CFastPractice::ReleaseBufferedInputState()
{
	// Prevent stuck fire/weapon toggles from leaking through when roles change
	// or when fast practice gets disabled while fire is held.
	for(int Conn = 0; Conn < NUM_DUMMIES; Conn++)
	{
		NeutralizeInput(GameClient()->m_Controls.m_aInputData[Conn]);
		NeutralizeInput(GameClient()->m_Controls.m_aLastData[Conn]);
		GameClient()->m_Controls.m_aInputData[Conn].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aInputData[Conn].m_Fire);
		GameClient()->m_Controls.m_aLastData[Conn].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aLastData[Conn].m_Fire);
	}
	NeutralizeInput(GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy]);
	GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy].m_Fire = ReleasedFireState(GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy].m_Fire);

	NeutralizeInput(GameClient()->m_DummyInput);
	GameClient()->m_DummyInput.m_Fire = ReleasedFireState(GameClient()->m_DummyInput.m_Fire);
	NeutralizeInput(GameClient()->m_HammerInput);
	GameClient()->m_HammerInput.m_Fire = ReleasedFireState(GameClient()->m_HammerInput.m_Fire);
	GameClient()->m_DummyFire = 0;
}

void CFastPractice::InvalidateBufferedInputState()
{
	ReleaseBufferedInputState();
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
}

void CFastPractice::CaptureServerLockedTargets()
{
	for(int Slot = 0; Slot < NUM_DUMMIES; Slot++)
	{
		const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[Slot];
		int TargetX = Input.m_TargetX;
		int TargetY = Input.m_TargetY;
		if(TargetX == 0 && TargetY == 0)
		{
			TargetX = 1;
			TargetY = 0;
		}
		m_aServerLockedTargets[Slot] = ivec2(TargetX, TargetY);
		m_aHasServerLockedTargets[Slot] = true;
	}
}

void CFastPractice::Enable()
{
	if(m_Enabled || !CanEnable())
		return;

	const int ActiveConn = g_Config.m_ClDummy ? IClient::CONN_DUMMY : IClient::CONN_MAIN;
	const int InactiveConn = ActiveConn ^ 1;
	const int ActiveClientId = GameClient()->m_aLocalIds[ActiveConn];
	m_EnableLocalClientId = (ActiveClientId >= 0 && ActiveClientId < MAX_CLIENTS) ? ActiveClientId : GameClient()->m_Snap.m_LocalClientId;

	const int CandidateDummyId = Client()->DummyConnected() ? GameClient()->m_aLocalIds[InactiveConn] : -1;
	m_EnableDummyClientId = CandidateDummyId;
	if(m_EnableDummyClientId < 0 || m_EnableDummyClientId >= MAX_CLIENTS ||
		!GameClient()->m_Snap.m_aCharacters[m_EnableDummyClientId].m_Active || GameClient()->m_aClients[m_EnableDummyClientId].m_Paused)
	{
		m_EnableDummyClientId = -1;
	}
	m_RequireDummy = m_EnableDummyClientId >= 0;

	// Solo blocks player/dummy interaction (hammerfly, hook, collision), so refuse to start.
	if(m_RequireDummy &&
		(GameClient()->m_aClients[m_EnableLocalClientId].m_Solo || GameClient()->m_aClients[m_EnableDummyClientId].m_Solo))
	{
		EchoPractice("you and dummy must not be in solo");
		m_EnableLocalClientId = -1;
		m_EnableDummyClientId = -1;
		m_RequireDummy = false;
		return;
	}

	if(!Rebuild())
	{
		ResetPracticeState();
		return;
	}

	CaptureAnchorsFromSnapshot();
	if(!m_MainAnchor.m_Valid || (m_RequireDummy && !m_HasDummyAnchor))
	{
		ResetPracticeState();
		return;
	}

	m_Enabled = true;
	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	ResetCommandState();
	if(CCharacter *pLocal = m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pLocal);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		if(CCharacter *pDummy = m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
			TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);
	ReleaseBufferedInputState();
	PublishParticipantCores(m_EnableLocalClientId, m_EnableDummyClientId);
	GameClient()->m_PredictedTick = m_PracticeWorld.GameTick();
}

void CFastPractice::Disable()
{
	if(m_Enabled)
		GameClient()->m_PredictedDummyId = -1;
	ReleaseBufferedInputState();
	m_aHasServerLockedTargets.fill(false);
	ResetPracticeState();
}

void CFastPractice::Toggle()
{
	if(m_Enabled)
		Disable();
	else
		Enable();
}

bool CFastPractice::ConsumeKillCommand()
{
	if(!m_Enabled)
		return false;
	ResetPracticeToAnchor();
	return true;
}

void CFastPractice::ResetPracticeToAnchor()
{
	if(!m_Enabled)
		return;

	if(!Rebuild())
	{
		Disable();
		return;
	}
	CaptureAnchorsFromSnapshot();
	if(!m_MainAnchor.m_Valid || (m_RequireDummy && !m_HasDummyAnchor))
	{
		Disable();
		return;
	}

	if(!ApplyAnchorToCharacter(m_PracticeWorld, m_MainAnchor))
	{
		Disable();
		return;
	}

	if(m_RequireDummy && !ApplyAnchorToCharacter(m_PracticeWorld, m_DummyAnchor))
	{
		Disable();
		return;
	}

	if(CCharacter *pMain = m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
		NormalizeCharacterAfterReset(pMain, false);
	if(m_RequireDummy)
	{
		if(CCharacter *pDummy = m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
			NormalizeCharacterAfterReset(pDummy, false);
	}

	m_PracticeWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	ResetAttackTickHistory();
	if(CCharacter *pMain = m_PracticeWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pMain);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		if(CCharacter *pDummy = m_PracticeWorld.GetCharacterById(m_EnableDummyClientId))
			TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
	ReleaseBufferedInputState();

	// Keep camera interpolation coherent after hard reset.
	FinishMutation(m_EnableLocalClientId, m_EnableDummyClientId, m_PracticeWorld.GetCharacterById(m_EnableLocalClientId), false);
}

void CFastPractice::PrepareInputForSend(int *pData, int Size, bool Dummy)
{
	if(!m_Enabled || !pData)
		return;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active || (GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS))
		return;

	CNetObj_PlayerInput Source{};
	if(Size >= (int)sizeof(CNetObj_PlayerInput))
		mem_copy(&Source, pData, sizeof(Source));
	else
		BuildLiveInput(Source, Dummy);

	if(m_LastClDummy != g_Config.m_ClDummy)
	{
		CaptureServerLockedTargets();
		CaptureFrozenTargets();
		m_LastClDummy = g_Config.m_ClDummy;
		m_SuppressFireOnNextPredictTick = true;
		m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
		ReleaseBufferedInputState();
	}

	StoreInput(Source, Dummy);

	CNetObj_PlayerInput Neutral{};
	BuildNeutralInput(Neutral, Dummy, true);
	if(Size >= (int)sizeof(CNetObj_PlayerInput))
		mem_copy(pData, &Neutral, sizeof(Neutral));
}

int CFastPractice::WeaponFireSound(int Weapon)
{
	switch(Weapon)
	{
	case WEAPON_GUN: return SOUND_GUN_FIRE;
	case WEAPON_SHOTGUN: return SOUND_SHOTGUN_FIRE;
	case WEAPON_GRENADE: return SOUND_GRENADE_FIRE;
	case WEAPON_HAMMER: return SOUND_HAMMER_FIRE;
	case WEAPON_LASER: return SOUND_LASER_FIRE;
	case WEAPON_NINJA: return SOUND_NINJA_FIRE;
	default: return -1;
	}
}

void CFastPractice::TrackFireSound(int ClientId, CCharacter *pChar)
{
	if(!pChar || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const int AttackTick = pChar->GetAttackTick();
	if(AttackTick <= m_aLastAttackTick[ClientId])
		return;

	if(g_Config.m_Debug)
		dbg_msg("fast_practice", "attack event client=%d weapon=%d attack_tick=%d prev_attack_tick=%d",
			ClientId, pChar->GetActiveWeapon(), AttackTick, m_aLastAttackTick[ClientId]);

	m_aLastAttackTick[ClientId] = AttackTick;

	if(!GameClient()->m_SuppressEvents && pChar->GetActiveWeapon() == WEAPON_HAMMER)
		MaybePlayHammerHitEffect(pChar);

	if(!g_Config.m_SndGame || GameClient()->m_SuppressEvents)
		return;

	const int SoundId = WeaponFireSound(pChar->GetActiveWeapon());
	if(SoundId < 0)
		return;

	GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SoundId, 1.0f, pChar->Core()->m_Pos);
}

void CFastPractice::MaybePlayHammerHitEffect(CCharacter *pChar)
{
	if(!pChar || pChar->GetActiveWeapon() != WEAPON_HAMMER)
		return;
	if(pChar->Core()->m_HammerHitDisabled)
		return;

	vec2 Dir = vec2((float)pChar->LatestInput()->m_TargetX, (float)pChar->LatestInput()->m_TargetY);
	if(length(Dir) < 0.001f)
		Dir = vec2((float)std::max(1, pChar->Core()->m_Direction), 0.0f);
	else
		Dir = normalize(Dir);

	const vec2 StartPos = pChar->Core()->m_Pos;
	const vec2 EndPos = StartPos + Dir * pChar->GetProximityRadius() * 1.5f;

	CEntity *apEnts[MAX_CLIENTS];
	const int Num = m_PracticeWorld.FindEntities(StartPos, pChar->GetProximityRadius() * 2.0f, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; ++i)
	{
		auto *pTarget = static_cast<CCharacter *>(apEnts[i]);
		if(!pTarget || pTarget == pChar || !pChar->CanCollide(pTarget->GetCid()))
			continue;

		vec2 ClosestPoint;
		if(!closest_point_on_line(StartPos, EndPos, pTarget->m_Pos, ClosestPoint))
			continue;
		if(distance(pTarget->m_Pos, ClosestPoint) > pChar->GetProximityRadius())
			continue;

		GameClient()->m_Effects.HammerHit(ClosestPoint, 1.0f, 1.0f);
		break;
	}
}


void CFastPractice::CaptureFrozenTargets()
{
	m_aFrozenTargetValid.fill(false);
	const int aIds[] = {GameClient()->m_Snap.m_LocalClientId, GameClient()->m_aLocalIds[0], GameClient()->m_aLocalIds[1], m_EnableLocalClientId, m_EnableDummyClientId};
	for(int ClientId : aIds)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		const int Slot = (ClientId == GameClient()->m_aLocalIds[1]) ? 1 : 0;
		const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[Slot];
		int TargetX = Input.m_TargetX;
		int TargetY = Input.m_TargetY;
		if(TargetX == 0 && TargetY == 0)
		{
			TargetX = 1;
			TargetY = 0;
		}
		m_aFrozenTarget[ClientId] = ivec2(TargetX, TargetY);
		m_aFrozenTargetValid[ClientId] = true;
	}
}

void CFastPractice::ResetStoredInputs()
{
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		m_aNextStoredInput[Dummy] = 0;
		for(int i = 0; i < INPUT_HISTORY_SIZE; i++)
			m_aaStoredInputs[Dummy][i] = {};
	}
}

void CFastPractice::StoreNeutralInput(bool Dummy, int Tick)
{
	CNetObj_PlayerInput Neutral{};
	BuildNeutralInput(Neutral, Dummy, true);
	const int Index = Dummy ? 1 : 0;
	SStoredInput &Slot = m_aaStoredInputs[Index][m_aNextStoredInput[Index] % INPUT_HISTORY_SIZE];
	Slot.m_Input = Neutral;
	Slot.m_Tick = Tick;
	m_aNextStoredInput[Index] = (m_aNextStoredInput[Index] + 1) % INPUT_HISTORY_SIZE;
}

void CFastPractice::StoreInput(const CNetObj_PlayerInput &Input, bool Dummy)
{
	const int Index = Dummy ? 1 : 0;
	const int Tick = Client()->PredGameTick(g_Config.m_ClDummy);
	SStoredInput &Slot = m_aaStoredInputs[Index][m_aNextStoredInput[Index] % INPUT_HISTORY_SIZE];
	Slot.m_Input = Input;
	Slot.m_Tick = Tick;
	m_aNextStoredInput[Index] = (m_aNextStoredInput[Index] + 1) % INPUT_HISTORY_SIZE;
}

const CNetObj_PlayerInput *CFastPractice::GetStoredInput(int Tick, bool Dummy) const
{
	const int Index = Dummy ? 1 : 0;
	for(int i = 0; i < INPUT_HISTORY_SIZE; i++)
	{
		const SStoredInput &Slot = m_aaStoredInputs[Index][i];
		if(Slot.m_Tick == Tick)
			return &Slot.m_Input;
	}
	return nullptr;
}

void CFastPractice::BuildLiveInput(CNetObj_PlayerInput &OutInput, bool Dummy) const
{
	const int Slot = Dummy ? (!g_Config.m_ClDummy) : g_Config.m_ClDummy;
	OutInput = GameClient()->m_Controls.m_aInputData[Slot];
	if(OutInput.m_TargetX == 0 && OutInput.m_TargetY == 0)
	{
		OutInput.m_TargetX = 1;
		OutInput.m_TargetY = 0;
	}
}

void CFastPractice::BuildNeutralInput(CNetObj_PlayerInput &OutInput, bool Dummy, bool UseFrozenTarget) const
{
	CNetObj_PlayerInput Source{};
	BuildLiveInput(Source, Dummy);
	OutInput = {};
	OutInput.m_Direction = 0;
	OutInput.m_Jump = 0;
	OutInput.m_Hook = 0;
	OutInput.m_WantedWeapon = 0;
	OutInput.m_Fire = ReleasedFireState(Source.m_Fire);
	OutInput.m_NextWeapon = 0;
	OutInput.m_PrevWeapon = 0;
	OutInput.m_PlayerFlags = PLAYERFLAG_PLAYING;

	int ClientId = GameClient()->m_aLocalIds[Dummy ? !g_Config.m_ClDummy : g_Config.m_ClDummy];
	if(!Dummy && ClientId < 0)
		ClientId = GameClient()->m_Snap.m_LocalClientId;

	const int Slot = g_Config.m_ClDummy ^ (int)Dummy;
	if(Slot >= 0 && Slot < NUM_DUMMIES && m_aHasServerLockedTargets[Slot])
	{
		OutInput.m_TargetX = m_aServerLockedTargets[Slot].x;
		OutInput.m_TargetY = m_aServerLockedTargets[Slot].y;
	}
	else if(UseFrozenTarget && ClientId >= 0 && ClientId < MAX_CLIENTS && m_aFrozenTargetValid[ClientId])
	{
		OutInput.m_TargetX = m_aFrozenTarget[ClientId].x;
		OutInput.m_TargetY = m_aFrozenTarget[ClientId].y;
	}
	else
	{
		OutInput.m_TargetX = Source.m_TargetX;
		OutInput.m_TargetY = Source.m_TargetY;
	}
	if(OutInput.m_TargetX == 0 && OutInput.m_TargetY == 0)
		OutInput.m_TargetX = 1;
}

void CFastPractice::FillRenderCharacter(CCharacter *pChar, CNetObj_Character &Out) const
{
	pChar->GetCore().Write(&Out);
	Out.m_Tick = pChar->GameWorld()->GameTick();
	Out.m_Weapon = pChar->GetActiveWeapon();
	Out.m_AttackTick = pChar->GetAttackTick();
	Out.m_PlayerFlags = PLAYERFLAG_PLAYING;
}

void CFastPractice::SeedPredictionHistory()
{
	// The prediction history of a participant is no longer written by the regular prediction,
	// so the leftover real positions have to be overwritten once on rebuild.
	const int BaseTick = m_PracticeWorld.GameTick();
	for(const int ClientId : {m_EnableLocalClientId, m_EnableDummyClientId})
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId);
		if(!pChar)
			continue;
		for(int i = 0; i < 200; i++)
		{
			const int Tick = BaseTick - i;
			if(Tick < 0)
				break;
			GameClient()->m_aClients[ClientId].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
			GameClient()->m_aClients[ClientId].m_aPredTick[Tick % 200] = Tick;
		}
	}
}

void CFastPractice::CachePredictedCore(int ClientId, const CCharacterCore &Core)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aPublishedPredicted[ClientId] = Core;
	m_aPublishedValid[ClientId] = true;
}

void CFastPractice::CachePrevPredictedCore(int ClientId, const CCharacterCore &Core)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aPublishedPrevPredicted[ClientId] = Core;
}

void CFastPractice::CacheRegularPredictedCore(int ClientId, const CCharacterCore &Core)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aPublishedRegularPredicted[ClientId] = Core;
}

void CFastPractice::RepublishCachedCores() const
{
	const int ControlledId = ControlledPracticeId();
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!IsPracticeParticipant(ClientId) || !m_aPublishedValid[ClientId])
			continue;
		CGameClient::CClientData &ClientData = GameClient()->m_aClients[ClientId];
		ClientData.m_Predicted = m_aPublishedPredicted[ClientId];
		ClientData.m_PrevPredicted = m_aPublishedPrevPredicted[ClientId];
		ClientData.m_RegularPredicted = m_aPublishedRegularPredicted[ClientId];
		if(ClientId == ControlledId)
		{
			GameClient()->m_PredictedChar = m_aPublishedPredicted[ClientId];
			GameClient()->m_PredictedPrevChar = m_aPublishedPrevPredicted[ClientId];
		}
	}
}

void CFastPractice::PublishParticipantCores(int LocalClientId, int DummyClientId)
{
	for(const int ClientId : {LocalClientId, DummyClientId})
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			continue;
		if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId))
		{
			CachePredictedCore(ClientId, pChar->GetCore());
			CachePrevPredictedCore(ClientId, pChar->GetCore());
			CacheRegularPredictedCore(ClientId, pChar->GetCore());
		}
	}
	RepublishCachedCores();
}

void CFastPractice::StorePredictionState(int Tick, int FinalTickRegular, int FinalTickOthers, int FinalTickSelf)
{
	(void)FinalTickOthers;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!IsPracticeParticipant(ClientId))
			continue;
		CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId);
		if(!pChar)
			continue;

		if(Tick == FinalTickSelf || Tick == FinalTickRegular)
			CachePredictedCore(ClientId, pChar->GetCore());
		if(Tick == FinalTickRegular)
			CacheRegularPredictedCore(ClientId, pChar->GetCore());

		GameClient()->m_aClients[ClientId].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
		GameClient()->m_aClients[ClientId].m_aPredTick[Tick % 200] = Tick;

		if(Tick > FinalTickRegular)
		{
			FillRenderCharacter(pChar, m_aFastRenderCur[ClientId]);
			m_aFastRenderValid[ClientId] = true;
		}
	}
	RepublishCachedCores();
}

bool CFastPractice::GetFastInputRenderCharacter(int ClientId, CNetObj_Character &Prev, CNetObj_Character &Cur) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !m_aFastRenderValid[ClientId])
		return false;
	Prev = m_aFastRenderPrev[ClientId];
	Cur = m_aFastRenderCur[ClientId];
	return true;
}

bool CFastPractice::IsDeathTile(const CCharacter *pChar) const
{
	if(!pChar || !Collision())
		return false;
	const vec2 Pos = pChar->Core()->m_Pos;
	const float Radius = pChar->GetProximityRadius();
	const int Index = Collision()->GetPureMapIndex(Pos);
	if(Index >= 0 && (Collision()->GetTileIndex(Index) == TILE_DEATH || Collision()->GetFrontTileIndex(Index) == TILE_DEATH))
		return true;
	for(const vec2 Offset : {vec2(Radius, 0), vec2(-Radius, 0), vec2(0, Radius), vec2(0, -Radius)})
	{
		const int Corner = Collision()->GetPureMapIndex(Pos + Offset);
		if(Corner >= 0 && (Collision()->GetTileIndex(Corner) == TILE_DEATH || Collision()->GetFrontTileIndex(Corner) == TILE_DEATH))
			return true;
	}
	return false;
}

void CFastPractice::ResetCharacterToSaved(int ClientId, CCharacter *pChar, int Tick)
{
	if(!pChar || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	vec2 SavedPos = pChar->Core()->m_Pos;
	if(m_aSafePosValid[ClientId])
		SavedPos = m_aSafePos[ClientId];
	else if(m_aSafePosValid[ClientId] == false && m_aPracticeCommandState[ClientId].m_HasRescueAuto)
		SavedPos = m_aPracticeCommandState[ClientId].m_RescueAutoPos;
	else if(m_aSpawnPosValid[ClientId])
		SavedPos = m_aSpawnPos[ClientId];

	StoreLastDeathPosition(ClientId, pChar->Core()->m_Pos);
	if(!GameClient()->m_SuppressEvents)
		GameClient()->m_Effects.PlayerSpawn(pChar->Core()->m_Pos, 1.0f, 0.6f);
	TeleportCharacter(pChar, SavedPos);
	(void)Tick;
}

void CFastPractice::PlayCoreEvents(CCharacter *pChar, int Tick)
{
	if(!pChar)
		return;
	const int ClientId = pChar->GetCid();
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	if(Tick <= m_aLastEventTick[ClientId])
		return;
	m_aLastEventTick[ClientId] = Tick;

	const vec2 Pos = pChar->Core()->m_Pos;
	const int Events = pChar->Core()->m_TriggeredEvents;
	if(!GameClient()->m_SuppressEvents && (Events & COREEVENT_AIR_JUMP))
		GameClient()->m_Effects.AirJump(Pos, 1.0f, 1.0f);
	if(g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
	{
		if(Events & COREEVENT_GROUND_JUMP)
			GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_PLAYER_JUMP, 1.0f, Pos);
		if(Events & COREEVENT_HOOK_ATTACH_PLAYER)
			GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_PLAYER, 1.0f, Pos);
		if(Events & COREEVENT_HOOK_ATTACH_GROUND)
			GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_ATTACH_GROUND, 1.0f, Pos);
		if(Events & COREEVENT_HOOK_HIT_NOHOOK)
			GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_HOOK_NOATTACH, 1.0f, Pos);
	}
}

void CFastPractice::TickPracticeWorld()
{
	if(!Active())
		return;

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return;
	}

	CCharacter *pLocalChar = m_PracticeWorld.GetCharacterById(LocalClientId);
	if(!pLocalChar)
	{
		m_NeedsRebuild = true;
		return;
	}

	CCharacter *pDummyChar = (m_RequireDummy && DummyClientId >= 0) ? m_PracticeWorld.GetCharacterById(DummyClientId) : nullptr;
	if(m_RequireDummy && !pDummyChar)
	{
		m_NeedsRebuild = true;
		return;
	}

	// Cloud input keeps its offset outside of BcInputs, mirror CGameClient::OnPredict here.
	const bool CloudInputMode = GameClient()->IsCloudInputMode();
	const float FastInputOffsetTicks = CloudInputMode ? 0.0f : BcInputs::EffectiveOffsetTicks();
	const int FastInputTicks = CloudInputMode ? GameClient()->m_CloudInput.SelfTickOffset() : BcInputs::PredictionTicks(FastInputOffsetTicks);
	const int FastInputOthersTicks = CloudInputMode ? GameClient()->m_CloudInput.OthersTickOffset() : (BcInputs::AnyOthers() ? BcInputs::PredictionTicksOthers(FastInputOffsetTicks) : 0);
	const int FinalTickRegular = Client()->PredGameTick(g_Config.m_ClDummy);
	const int FinalTickSelf = FinalTickRegular + FastInputTicks;
	const int FinalTickOthers = FinalTickRegular + FastInputOthersTicks;
	const bool FastInputOverrun = FinalTickSelf > FinalTickRegular;

	if(m_PracticeWorld.GameTick() > FinalTickRegular || FinalTickRegular - m_PracticeWorld.GameTick() > Client()->GameTickSpeed() * 3)
	{
		m_NeedsRebuild = true;
		return;
	}

	if(FastInputOverrun)
		std::fill(std::begin(m_aFastRenderValid), std::end(m_aFastRenderValid), false);

	const int LocalTee = g_Config.m_ClDummy ^ GameClient()->m_IsDummySwapping;
	const int LocalInputConn = 0;
	const int DummyInputConn = m_RequireDummy ? 1 : -1;
	const int BaseGameTick = m_PracticeWorld.GameTick();

	CGameWorld RegularWorld;
	bool HasRegularWorld = false;
	std::vector<STrackedProjectile> vTrackedExplosiveBefore;
	std::vector<STrackedProjectile> vTrackedExplosiveAfter;

	SyncPracticeWorldConfig(m_PracticeWorld);
	if(FastInputOverrun && BaseGameTick == FinalTickRegular)
	{
		StorePredictionState(FinalTickRegular, FinalTickRegular, FinalTickOthers, FinalTickSelf);
		RegularWorld.CopyWorldClean(&m_PracticeWorld);
		SyncPracticeWorldConfig(RegularWorld);
		HasRegularWorld = true;
	}

	for(int Tick = BaseGameTick + 1; Tick <= FinalTickSelf; Tick++)
	{
		pLocalChar = m_PracticeWorld.GetCharacterById(LocalClientId);
		pDummyChar = (m_RequireDummy && DummyClientId >= 0) ? m_PracticeWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
		{
			Disable();
			return;
		}

		if(Tick == FinalTickRegular)
		{
			m_PracticePrevWorld.CopyWorldClean(&m_PracticeWorld);
			SyncPracticeWorldConfig(m_PracticePrevWorld);
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
			{
				if(!IsPracticeParticipant(ClientId))
					continue;
				if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId))
				{
					CachePrevPredictedCore(ClientId, pChar->GetCore());
					FillRenderCharacter(pChar, m_aFastRenderPrev[ClientId]);
				}
			}
		}

		if(Tick > FinalTickRegular)
		{
			// Mirror CGameClient::OnPredict: m_PrevPredicted must end up at FinalTickSelf - 1 so that
			// every mix(m_PrevPredicted, m_Predicted, PredIntraTick) consumer spans exactly one tick.
			for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
			{
				if(!IsPracticeParticipant(ClientId))
					continue;
				if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(ClientId))
				{
					CachePrevPredictedCore(ClientId, pChar->GetCore());
					FillRenderCharacter(pChar, m_aFastRenderPrev[ClientId]);
				}
			}
		}

		const CNetObj_PlayerInput *pInputData = GetStoredInput(Tick, GameClient()->m_IsDummySwapping != 0);
		const CNetObj_PlayerInput *pDummyInputData = !pDummyChar ? nullptr : GetStoredInput(Tick, (GameClient()->m_IsDummySwapping ^ 1) != 0);
		CNetObj_PlayerInput LiveInput{};
		CNetObj_PlayerInput LiveDummyInput{};
		CNetObj_PlayerInput LocalNeutralizedInput{};
		CNetObj_PlayerInput DummyNeutralizedInput{};

		if(!pInputData)
		{
			BuildLiveInput(LiveInput, GameClient()->m_IsDummySwapping != 0);
			pInputData = &LiveInput;
		}
		if(pDummyChar && !pDummyInputData)
		{
			BuildLiveInput(LiveDummyInput, (GameClient()->m_IsDummySwapping ^ 1) != 0);
			pDummyInputData = &LiveDummyInput;
		}

		if(FastInputTicks > 0 && Tick > FinalTickRegular)
			pInputData = CloudInputMode ? &GameClient()->m_CloudInput.Input(LocalTee) : &GameClient()->m_Controls.m_aFastInput[LocalTee];

		const bool SuppressTransitionTick = Tick == BaseGameTick + 1 && (m_SuppressFireOnNextPredictTick || GameClient()->m_IsDummySwapping);
		const bool SuppressCooldownTick = m_InputSuppressTicks > 0;
		if(SuppressTransitionTick || SuppressCooldownTick)
		{
			if(pInputData)
			{
				LocalNeutralizedInput = *pInputData;
				LocalNeutralizedInput.m_Fire = ReleasedFireState(pLocalChar->LatestInput()->m_Fire);
				LocalNeutralizedInput.m_WantedWeapon = 0;
				LocalNeutralizedInput.m_NextWeapon = 0;
				LocalNeutralizedInput.m_PrevWeapon = 0;
				pInputData = &LocalNeutralizedInput;
			}
			if(pDummyInputData)
			{
				DummyNeutralizedInput = *pDummyInputData;
				DummyNeutralizedInput.m_Fire = ReleasedFireState(pDummyChar->LatestInput()->m_Fire);
				DummyNeutralizedInput.m_WantedWeapon = 0;
				DummyNeutralizedInput.m_NextWeapon = 0;
				DummyNeutralizedInput.m_PrevWeapon = 0;
				pDummyInputData = &DummyNeutralizedInput;
			}
			if(m_InputSuppressTicks > 0)
				m_InputSuppressTicks--;
			m_SuppressFireOnNextPredictTick = false;
		}

		if(pDummyChar && g_Config.m_ClDummyHammer)
		{
			// Match vanilla hammerfly: reuse m_HammerInput (Fire only advances in OnSnapInput
			// every ~25 sends). Applying the same Fire value across ticks/repredicts does not
			// create new CountInput presses — unlike inventing Tick%25 edges on fast-input
			// overrun ticks, which were replayed every frame then discarded on restore, so the
			// dummy looked like it was spamming hammer without actually flying.
			DummyNeutralizedInput = GameClient()->m_HammerInput;
			DummyNeutralizedInput.m_WantedWeapon = WEAPON_HAMMER + 1;
			DummyNeutralizedInput.m_PlayerFlags = PLAYERFLAG_PLAYING;
			const vec2 Dir = pLocalChar->Core()->m_Pos - pDummyChar->Core()->m_Pos;
			DummyNeutralizedInput.m_TargetX = (int)Dir.x;
			DummyNeutralizedInput.m_TargetY = (int)Dir.y;
			if(DummyNeutralizedInput.m_TargetX == 0 && DummyNeutralizedInput.m_TargetY == 0)
				DummyNeutralizedInput.m_TargetY = -1;
			pDummyInputData = &DummyNeutralizedInput;
		}

		const bool DummyFirst = pInputData && pDummyInputData && pDummyChar && pDummyChar->GetCid() < pLocalChar->GetCid();
		const bool RegularTick = Tick <= FinalTickRegular;
		const bool PredictEvents = m_PracticeWorld.m_WorldConfig.m_PredictEvents;
		if(!RegularTick)
			m_PracticeWorld.m_WorldConfig.m_PredictEvents = false;

		pLocalChar->m_CanMoveInFreeze = false;
		if(pDummyChar)
			pDummyChar->m_CanMoveInFreeze = false;
		if(g_Config.m_ClPredictFreeze == 2 && FinalTickRegular - 1 - FinalTickRegular % 2 <= Tick)
			pLocalChar->m_CanMoveInFreeze = true;

		CollectTrackedProjectiles(m_PracticeWorld, LocalClientId, DummyClientId, vTrackedExplosiveBefore);

		if(DummyFirst && pDummyInputData)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		m_PracticeWorld.m_GameTick = Tick;
		UpdatePracticeRaceState(LocalClientId, pLocalChar, Tick);
		if(pDummyChar)
			UpdatePracticeRaceState(DummyClientId, pDummyChar, Tick);
		if(pInputData)
			pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);
		m_PracticeWorld.Tick();
		m_PracticeWorld.m_WorldConfig.m_PredictEvents = PredictEvents;

		if(Tick == FinalTickRegular)
		{
			for(auto *pPrevProj = (CProjectile *)m_PracticePrevWorld.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pPrevProj; pPrevProj = (CProjectile *)pPrevProj->TypeNext())
			{
				if(pPrevProj->m_DestroyTick >= 0)
					continue;
				const CProjectileData PrevData = pPrevProj->GetData();
				bool Found = false;
				for(auto *pCurProj = (CProjectile *)m_PracticeWorld.FindFirst(CGameWorld::ENTTYPE_PROJECTILE); pCurProj; pCurProj = (CProjectile *)pCurProj->TypeNext())
				{
					const CProjectileData CurData = pCurProj->GetData();
					if(SameProjectile(
						   STrackedProjectile{PrevData.m_Owner, PrevData.m_StartTick, PrevData.m_Type, PrevData.m_TuneZone, PrevData.m_StartPos, PrevData.m_StartVel},
						   STrackedProjectile{CurData.m_Owner, CurData.m_StartTick, CurData.m_Type, CurData.m_TuneZone, CurData.m_StartPos, CurData.m_StartVel}))
					{
						Found = true;
						break;
					}
				}
				if(!Found)
					pPrevProj->m_DestroyTick = Tick;
			}
		}

		TrackSafeRescuePosition(LocalClientId, pLocalChar);
		if(pDummyChar)
			TrackSafeRescuePosition(DummyClientId, pDummyChar);
		if(IsSafeRescuePosition(pLocalChar->Core()->m_Pos, pLocalChar->GetProximityRadius()))
		{
			m_aSafePos[LocalClientId] = pLocalChar->Core()->m_Pos;
			m_aSafePosValid[LocalClientId] = true;
		}

		if(RegularTick)
		{
			PlayCoreEvents(pLocalChar, Tick);
			PlayCoreEvents(pDummyChar, Tick);

			auto ResetIfDead = [&](int ClientId, CCharacter *pChar) {
				if(!pChar || !IsDeathTile(pChar))
					return;
				ResetCharacterToSaved(ClientId, pChar, Tick);
			};
			ResetIfDead(LocalClientId, pLocalChar);
			if(pDummyChar)
				ResetIfDead(DummyClientId, pDummyChar);
			pLocalChar = m_PracticeWorld.GetCharacterById(LocalClientId);
			pDummyChar = (m_RequireDummy && DummyClientId >= 0) ? m_PracticeWorld.GetCharacterById(DummyClientId) : nullptr;
		}

		CollectTrackedProjectiles(m_PracticeWorld, LocalClientId, DummyClientId, vTrackedExplosiveAfter);
		for(const auto &TrackedProj : vTrackedExplosiveBefore)
		{
			const bool StillExists = std::any_of(vTrackedExplosiveAfter.begin(), vTrackedExplosiveAfter.end(), [&](const STrackedProjectile &Candidate) {
				return SameProjectile(TrackedProj, Candidate);
			});
			if(StillExists)
				continue;
			const int TickSpeed = Client()->GameTickSpeed();
			const int TuneZone = std::clamp(TrackedProj.m_TuneZone, 0, TuneZone::NUM - 1);
			const CTuningParams *pTuning = GameClient()->GetTuning(TuneZone);
			vec2 PrevPos = CalcTrackedProjectilePos(TrackedProj, Tick - 1, TickSpeed, pTuning);
			vec2 CurPos = CalcTrackedProjectilePos(TrackedProj, Tick, TickSpeed, pTuning);
			vec2 ImpactPos = CurPos;
			Collision()->IntersectLine(PrevPos, CurPos, &ImpactPos, nullptr);
			if(!GameClient()->m_SuppressEvents)
				GameClient()->m_Effects.Explosion(ImpactPos, 1.0f);
			if(g_Config.m_SndGame && !GameClient()->m_SuppressEvents)
				GameClient()->m_Sounds.PlayAndRecord(CSounds::CHN_WORLD, SOUND_GRENADE_EXPLODE, 1.0f, ImpactPos);
		}

		TrackFireSound(LocalClientId, pLocalChar);
		if(pDummyChar)
			TrackFireSound(DummyClientId, pDummyChar);

		StorePredictionState(Tick, FinalTickRegular, FinalTickOthers, FinalTickSelf);

		if(RegularTick && Tick == FinalTickRegular)
		{
			RegularWorld.CopyWorldClean(&m_PracticeWorld);
			SyncPracticeWorldConfig(RegularWorld);
			HasRegularWorld = true;
		}

		if(Tick > GameClient()->m_aLastNewPredictedTick[LocalInputConn] && Tick <= FinalTickRegular)
		{
			GameClient()->m_aLastNewPredictedTick[LocalInputConn] = Tick;
			GameClient()->m_NewPredictedTick = true;
		}
		if(pDummyChar && DummyInputConn >= 0 && Tick > GameClient()->m_aLastNewPredictedTick[DummyInputConn])
			GameClient()->m_aLastNewPredictedTick[DummyInputConn] = Tick;
	}

	if(FastInputOverrun && HasRegularWorld)
	{
		m_PracticeWorld.CopyWorldClean(&RegularWorld);
		SyncPracticeWorldConfig(m_PracticeWorld);
	}
	if(!FastInputOverrun)
		std::fill(std::begin(m_aFastRenderValid), std::end(m_aFastRenderValid), false);

	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	GameClient()->m_PredictedTick = FinalTickRegular;
	if(GameClient()->m_NewPredictedTick)
		GameClient()->m_Ghost.OnNewPredictedSnapshot();
	m_LastResolvedLocalClientId = LocalClientId;
	m_LastResolvedDummyClientId = DummyClientId;
}

void CFastPractice::SyncFromPrediction()
{
	if(!m_Enabled)
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Disable();
		return;
	}

	if(m_LastClDummy != g_Config.m_ClDummy)
	{
		CaptureServerLockedTargets();
		CaptureFrozenTargets();
		m_LastClDummy = g_Config.m_ClDummy;
		m_SuppressFireOnNextPredictTick = true;
		m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
		ReleaseBufferedInputState();
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active || (GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS))
	{
		GameClient()->m_PredictedDummyId = -1;
		UpdateGhostData();
		return;
	}

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return;
	}

	auto &Pending = m_aPracticeCommandState[LocalClientId];
	if(Pending.m_HasPendingTeleport)
	{
		if(CCharacter *pChar = m_PracticeWorld.GetCharacterById(LocalClientId))
		{
			ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(Pending.m_PendingTeleportPos));
			FinishMutation(LocalClientId, DummyClientId, pChar, false);
		}
		Pending.m_HasPendingTeleport = false;
	}

	if(m_NeedsRebuild || !m_Ready)
	{
		if(!Rebuild())
		{
			Disable();
			return;
		}
	}

	TickPracticeWorld();
	if(m_NeedsRebuild)
	{
		if(!Rebuild())
			Disable();
		else
			TickPracticeWorld();
	}
	// TickPracticeWorld can bail out or simulate zero ticks on a frame where the regular
	// prediction already clobbered m_Predicted, so always restore the last practice state.
	RepublishCachedCores();
	UpdateGhostData();
}



void CFastPractice::UpdateGhostForClientId(int ClientId, SGhostData &Ghost)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		Ghost = SGhostData{};
		return;
	}

	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
	{
		const CNetObj_Character &Char = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		Ghost.m_Valid = true;
		Ghost.m_ClientId = ClientId;
		Ghost.m_Pos = vec2(Char.m_X, Char.m_Y);
		Ghost.m_Direction = vec2(Char.m_Direction == 0 ? 1.0f : (float)Char.m_Direction, 0.0f);
		Ghost.m_Angle = Char.m_Angle;
		Ghost.m_Weapon = Char.m_Weapon;
		Ghost.m_HookState = Char.m_HookState;
		Ghost.m_HookPos = vec2(Char.m_HookX, Char.m_HookY);
		return;
	}

	Ghost = SGhostData{};
}

void CFastPractice::UpdateGhostData()
{
	if(m_Enabled)
	{
		UpdateGhostForClientId(m_EnableLocalClientId, m_MainGhost);
		UpdateGhostForClientId(m_EnableDummyClientId, m_DummyGhost);
	}
	else
	{
		UpdateGhostForClientId(GameClient()->m_aLocalIds[0], m_MainGhost);
		UpdateGhostForClientId(GameClient()->m_aLocalIds[1], m_DummyGhost);
	}
}

void CFastPractice::OnNewSnapshot()
{
	UpdateGhostData();

	if(!m_Enabled)
		return;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Disable();
		return;
	}

	if(GameClient()->m_Snap.m_SpecInfo.m_Active || (GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS))
		GameClient()->m_PredictedDummyId = -1;
	else
		GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();

	// The snapshot just overwrote m_Predicted / m_RegularPredicted via CCharacterCore::ReadDDNet
	// with the real server state. Restore the practice state so nothing renders a mixed frame.
	RepublishCachedCores();
}

void CFastPractice::RenderGhost(const SGhostData &Ghost, float Alpha) const
{
	if(!Ghost.m_Valid || Ghost.m_ClientId < 0 || Ghost.m_ClientId >= MAX_CLIENTS)
		return;
	if(!GameClient()->m_aClients[Ghost.m_ClientId].m_Active)
		return;

	CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Ghost.m_ClientId].m_RenderInfo;
	vec2 Dir = Ghost.m_Direction;
	if(length(Dir) < 0.001f)
		Dir = vec2(1.0f, 0.0f);
	else
		Dir = normalize(Dir);

	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, Dir, Ghost.m_Pos, Alpha);
}

void CFastPractice::OnRender()
{
	if(!m_Enabled)
		return;

	const auto RenderRealMarker = [&](const SGhostData &Ghost, float Alpha) {
		if(!Ghost.m_Valid)
			return;
		if(CCharacter *pPracticeChar = m_PracticeWorld.GetCharacterById(Ghost.m_ClientId))
		{
			// Skip overlay when the real and local worlds are visually on top of each other.
			if(distance(pPracticeChar->Core()->m_Pos, Ghost.m_Pos) < 10.0f)
				return;
		}
		RenderGhost(Ghost, Alpha);
	};

	RenderRealMarker(m_MainGhost, 0.28f);
	RenderRealMarker(m_DummyGhost, 0.22f);
}

void CFastPractice::EchoPractice(const char *pFormat, ...) const
{
	char aBody[256];
	va_list Args;
	va_start(Args, pFormat);
	str_format_v(aBody, sizeof(aBody), Localize(pFormat), Args);
	va_end(Args);

	char aMsg[320];
	str_format(aMsg, sizeof(aMsg), Localize("practice local: %s"), aBody);
	GameClient()->Echo(aMsg);
}

bool CFastPractice::ParseCommandArgs(const char *pLine, std::vector<std::string> &vArgs)
{
	vArgs.clear();
	if(!pLine)
		return false;

	const char *p = pLine;
	while(*p)
	{
		while(*p == ' ' || *p == '\t')
			++p;
		if(*p == '\0')
			break;

		std::string Token;
		bool InQuotes = false;
		if(*p == '"')
		{
			InQuotes = true;
			++p;
		}

		while(*p)
		{
			if(InQuotes)
			{
				if(*p == '"')
				{
					++p;
					break;
				}
				if(*p == '\\' && p[1] == '"')
				{
					Token.push_back('"');
					p += 2;
					continue;
				}
				Token.push_back(*p++);
				continue;
			}

			if(*p == ' ' || *p == '\t')
				break;
			Token.push_back(*p++);
		}

		if(!Token.empty())
			vArgs.push_back(Token);

		while(*p == ' ' || *p == '\t')
			++p;
	}

	return !vArgs.empty();
}

bool CFastPractice::ParseCoordinateToken(const char *pToken, float Base, float &Out)
{
	if(!pToken || pToken[0] == '\0')
		return false;

	const bool Relative = pToken[0] == '~';
	const char *pValue = Relative ? pToken + 1 : pToken;
	float Parsed = 0.0f;
	if(pValue[0] != '\0' && !str_tofloat(pValue, &Parsed))
		return false;

	if(std::isnan(Parsed) || std::isinf(Parsed))
		return false;

	Out = (Relative ? Base : 0.0f) + Parsed * 32.0f;
	return true;
}

int CFastPractice::FindClientByName(const char *pName) const
{
	if(!pName || pName[0] == '\0')
		return -1;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!GameClient()->m_aClients[ClientId].m_Active)
			continue;
		if(str_comp(GameClient()->m_aClients[ClientId].m_aName, pName) == 0)
			return ClientId;
	}

	return -1;
}

void CFastPractice::NormalizeCharacterAfterReset(CCharacter *pChar, bool KeepFreezeFlags) const
{
	if(!pChar)
		return;

	CCharacterCore Core = pChar->GetCore();
	Core.m_Vel = vec2(0.0f, 0.0f);
	Core.m_Jumped = 0;
	Core.m_JumpedTotal = 0;
	Core.m_HookState = HOOK_RETRACTED;
	Core.m_HookPos = Core.m_Pos;
	Core.m_HookDir = vec2(0.0f, 0.0f);
	Core.m_HookTick = 0;
	Core.m_NewHook = false;
	Core.SetHookedPlayer(-1);
	Core.m_AttachedPlayers.clear();
	if(!KeepFreezeFlags)
	{
		Core.m_DeepFrozen = false;
		Core.m_LiveFrozen = false;
		Core.m_IsInFreeze = false;
		Core.m_FreezeEnd = 0;
	}
	pChar->SetCore(Core);
	pChar->m_Pos = Core.m_Pos;
	pChar->m_PrevPos = Core.m_Pos;
	pChar->m_PrevPrevPos = Core.m_Pos;
	pChar->ResetHook();
	pChar->ResetVelocity();
	if(!KeepFreezeFlags)
	{
		pChar->Unfreeze();
		pChar->m_FreezeTime = 0;
	}
	pChar->m_CanMoveInFreeze = false;
	pChar->m_FrozenLastTick = false;

	CNetObj_PlayerInput NeutralInput = {};
	NeutralInput.m_TargetY = -1;
	pChar->SetInput(&NeutralInput);
	pChar->ResetInput();
}

void CFastPractice::NormalizeWeaponSelectionInput(CCharacter *pChar) const
{
	if(!pChar)
		return;

	CNetObj_PlayerInput Input = *pChar->LatestInput();
	Input.m_WantedWeapon = 0;
	Input.m_NextWeapon = 0;
	Input.m_PrevWeapon = 0;
	pChar->SetInput(&Input);
}

void CFastPractice::TeleportCharacter(CCharacter *pChar, const vec2 &Pos) const
{
	if(!pChar)
		return;

	CCharacterCore Core = pChar->GetCore();
	Core.m_Pos = Pos;
	pChar->SetCore(Core);
	pChar->m_Pos = Pos;
	pChar->m_PrevPos = Pos;
	pChar->m_PrevPrevPos = Pos;
	NormalizeCharacterAfterReset(pChar, false);
}

void CFastPractice::SaveTeleportHistory(int ClientId, const vec2 &CurrentPos, const vec2 &TargetPos)
{
	StoreLastTeleport(ClientId, TargetPos);
	StoreLastDeathPosition(ClientId, CurrentPos);
}

void CFastPractice::ApplyPracticeTeleport(int ClientId, CCharacter *pChar, const vec2 &TargetPos)
{
	if(!pChar)
		return;

	SaveTeleportHistory(ClientId, pChar->Core()->m_Pos, TargetPos);
	TeleportCharacter(pChar, TargetPos);
}

void CFastPractice::GivePracticeWeapon(CCharacter *pChar, int Weapon, bool Remove) const
{
	if(!pChar)
		return;

	if(Weapon == -1)
	{
		pChar->GiveWeapon(WEAPON_SHOTGUN, Remove);
		pChar->GiveWeapon(WEAPON_GRENADE, Remove);
		pChar->GiveWeapon(WEAPON_LASER, Remove);
	}
	else
	{
		pChar->GiveWeapon(Weapon, Remove);
	}
}

void CFastPractice::EnsureActiveWeaponIsValid(CCharacter *pChar) const
{
	if(!pChar)
		return;

	const CCharacterCore Core = pChar->GetCore();
	if(Core.m_ActiveWeapon >= WEAPON_HAMMER && Core.m_ActiveWeapon < NUM_WEAPONS && Core.m_aWeapons[Core.m_ActiveWeapon].m_Got)
		return;

	if(Core.m_aWeapons[WEAPON_HAMMER].m_Got)
	{
		pChar->SetActiveWeapon(WEAPON_HAMMER);
		return;
	}
	if(Core.m_aWeapons[WEAPON_GUN].m_Got)
	{
		pChar->SetActiveWeapon(WEAPON_GUN);
		return;
	}
	for(int Weapon = WEAPON_SHOTGUN; Weapon < NUM_WEAPONS; Weapon++)
	{
		if(Core.m_aWeapons[Weapon].m_Got)
		{
			pChar->SetActiveWeapon(Weapon);
			return;
		}
	}
}

void CFastPractice::TogglePracticeHit(CCharacter *pChar, int Weapon) const
{
	if(!pChar)
		return;

	CCharacterCore Core = pChar->GetCore();
	switch(Weapon)
	{
	case WEAPON_HAMMER: Core.m_HammerHitDisabled = !Core.m_HammerHitDisabled; break;
	case WEAPON_SHOTGUN: Core.m_ShotgunHitDisabled = !Core.m_ShotgunHitDisabled; break;
	case WEAPON_GRENADE: Core.m_GrenadeHitDisabled = !Core.m_GrenadeHitDisabled; break;
	case WEAPON_LASER: Core.m_LaserHitDisabled = !Core.m_LaserHitDisabled; break;
	default: return;
	}
	pChar->SetCore(Core);
}

vec2 CFastPractice::ClampToPracticePlayableBounds(const vec2 &Pos) const
{
	float MinX = -std::numeric_limits<float>::max();
	float MinY = -std::numeric_limits<float>::max();
	float MaxX = std::numeric_limits<float>::max();
	float MaxY = std::numeric_limits<float>::max();
	if(CMapItemLayerTilemap *pGameLayer = Layers()->GameLayer())
	{
		constexpr float OuterKillTileBoundaryDistance = 201.0f * 32.0f;
		MinX = (-OuterKillTileBoundaryDistance) + 1.0f;
		MinY = (-OuterKillTileBoundaryDistance) + 1.0f;
		MaxX = (-OuterKillTileBoundaryDistance) + (pGameLayer->m_Width * 32.0f) + (OuterKillTileBoundaryDistance * 2.0f) - 1.0f;
		MaxY = (-OuterKillTileBoundaryDistance) + (pGameLayer->m_Height * 32.0f) + (OuterKillTileBoundaryDistance * 2.0f) - 1.0f;
	}
	return vec2(std::clamp(Pos.x, MinX, MaxX), std::clamp(Pos.y, MinY, MaxY));
}

void CFastPractice::StoreLastTeleport(int ClientId, const vec2 &Pos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	auto &State = m_aPracticeCommandState[ClientId];
	State.m_HasLastTeleport = true;
	State.m_LastTeleportPos = Pos;
}

void CFastPractice::StoreLastDeathPosition(int ClientId, const vec2 &Pos)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	auto &State = m_aPracticeCommandState[ClientId];
	State.m_HasLastDeath = true;
	State.m_LastDeathPos = Pos;
}

bool CFastPractice::IsSafeRescueTile(int Tile) const
{
	return Tile != TILE_DEATH &&
	       Tile != TILE_FREEZE &&
	       Tile != TILE_DFREEZE &&
	       Tile != TILE_LFREEZE;
}

bool CFastPractice::IsSafeRescuePosition(const vec2 &Pos, float ProximityRadius) const
{
	const float HalfSize = std::max(6.0f, ProximityRadius * 0.5f);
	if(Collision()->TestBox(Pos, vec2(HalfSize * 2.0f, HalfSize * 2.0f)))
		return false;

	const vec2 aSamplePoints[] = {
		Pos,
		Pos + vec2(-HalfSize, -HalfSize),
		Pos + vec2(HalfSize, -HalfSize),
		Pos + vec2(-HalfSize, HalfSize),
		Pos + vec2(HalfSize, HalfSize),
	};
	for(const vec2 &SamplePos : aSamplePoints)
	{
		const int X = round_to_int(SamplePos.x);
		const int Y = round_to_int(SamplePos.y);
		if(!IsSafeRescueTile(Collision()->GetTile(X, Y)) ||
			!IsSafeRescueTile(Collision()->GetFrontTile(X, Y)))
		{
			return false;
		}
	}

	const bool Grounded =
		Collision()->CheckPoint(Pos.x + HalfSize, Pos.y + HalfSize + 5.0f) ||
		Collision()->CheckPoint(Pos.x - HalfSize, Pos.y + HalfSize + 5.0f);
	return Grounded;
}

void CFastPractice::TrackSafeRescuePosition(int ClientId, CCharacter *pChar)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !pChar)
		return;
	if(IsFrozenState(pChar) || !pChar->IsGrounded())
		return;

	const vec2 Pos = pChar->Core()->m_Pos;
	if(!IsSafeRescuePosition(Pos, pChar->GetProximityRadius()))
		return;

	auto &State = m_aPracticeCommandState[ClientId];
	if(!State.m_vSafePositions.empty() && distance(State.m_vSafePositions.front(), Pos) < 48.0f)
		return;

	State.m_vSafePositions.push_front(Pos);
	if((int)State.m_vSafePositions.size() > SPracticeCommandState::MAX_SAFE_POSITIONS)
		State.m_vSafePositions.pop_back();
	State.m_HasRescueAuto = true;
	State.m_RescueAutoPos = Pos;
}

void CFastPractice::FinishMutation(int LocalClientId, int DummyClientId, CCharacter *pChar, bool WeaponsMutated)
{
	if(WeaponsMutated)
	{
		NormalizeWeaponSelectionInput(pChar);
		if(m_RequireDummy && DummyClientId >= 0)
			NormalizeWeaponSelectionInput(m_PracticeWorld.GetCharacterById(DummyClientId));
	}

	// Teleports/resets zero the character Fire counter. Matching it to the current live
	// (released) Fire prevents CountInput from treating the next real input as a fresh press
	// (which looked like a phantom shot/hammer after /tc).
	auto SyncFireAfterMutation = [&](CCharacter *pTarget, bool Dummy) {
		if(!pTarget)
			return;
		CNetObj_PlayerInput Live{};
		BuildLiveInput(Live, Dummy);
		CNetObj_PlayerInput Input = *pTarget->LatestInput();
		Input.m_Fire = ReleasedFireState(Live.m_Fire);
		Input.m_WantedWeapon = 0;
		Input.m_NextWeapon = 0;
		Input.m_PrevWeapon = 0;
		pTarget->SetInput(&Input);
		pTarget->ResetInput();
	};
	SyncFireAfterMutation(pChar, GameClient()->m_IsDummySwapping != 0);
	if(m_RequireDummy && DummyClientId >= 0)
		SyncFireAfterMutation(m_PracticeWorld.GetCharacterById(DummyClientId), (GameClient()->m_IsDummySwapping ^ 1) != 0);

	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
	m_PracticePrevWorld.CopyWorldClean(&m_PracticeWorld);
	SyncPracticeWorldConfig(m_PracticePrevWorld);
	StorePredictionState(m_PracticeWorld.GameTick(), m_PracticeWorld.GameTick(), m_PracticeWorld.GameTick(), m_PracticeWorld.GameTick());
	PublishParticipantCores(LocalClientId, DummyClientId);
	GameClient()->m_PredictedTick = m_PracticeWorld.GameTick();
}

bool CFastPractice::ExecutePracticeMetaCommand(const std::string &Cmd)
{
	if(Cmd == "unpractice")
	{
		Disable();
		EchoPractice("practice mode disabled");
		return true;
	}
	if(Cmd == "practice")
	{
		EchoPractice("practice mode already enabled");
		return true;
	}
	if(Cmd == "practicecmdlist")
	{
		EchoPractice("available commands: /r /back /rescuemode /tp /teleport /tpxy /lasttp /tc /telecursor /totele /totelecp /solo /unsolo /deep /undeep /livefreeze /unlivefreeze /shotgun /grenade /laser /rifle /unshotgun /ungrenade /unlaser /unrifle /weapons /unweapons /addweapon /removeweapon /jetpack /unjetpack /infjump /uninfjump /setjumps /ninja /unninja /endless /unendless /invincible /collision /hookcollision /hitothers /practice /unpractice");
		return true;
	}
	if(Cmd == "kill")
	{
		ResetPracticeToAnchor();
		return true;
	}
	return false;
}

bool CFastPractice::ExecutePracticeRescueCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs)
{
	auto &State = m_aPracticeCommandState[LocalClientId];
	if(Cmd == "rescuemode")
	{
		if(vArgs.size() <= 1)
		{
			EchoPractice("rescue mode: %s", State.m_RescueManual ? Localize("manual") : Localize("auto"));
			return true;
		}

		const std::string Arg = LowercaseCopy(vArgs[1]);
		if(Arg == "auto")
		{
			State.m_RescueManual = false;
			EchoPractice("rescue mode changed to auto");
		}
		else if(Arg == "manual")
		{
			State.m_RescueManual = true;
			EchoPractice("rescue mode changed to manual");
		}
		else if(Arg == "list")
		{
			EchoPractice("available rescue modes: auto, manual");
		}
		else
		{
			EchoPractice("unknown argument. check '/rescuemode list'");
		}
		return true;
	}

	if(Cmd != "r" && Cmd != "rescue")
		return false;

	const vec2 CurrentPos = pChar->Core()->m_Pos;
	const bool Frozen = IsFrozenState(pChar);
	if(State.m_RescueManual)
	{
		if(pChar->IsGrounded() && !Frozen && IsSafeRescuePosition(CurrentPos, pChar->GetProximityRadius()))
		{
			State.m_HasRescueManual = true;
			State.m_RescueManualPos = CurrentPos;
			EchoPractice("manual rescue point saved");
			TrackSafeRescuePosition(LocalClientId, pChar);
		}
		else if(State.m_HasRescueManual && IsSafeRescuePosition(State.m_RescueManualPos, pChar->GetProximityRadius()))
		{
			ApplyPracticeTeleport(LocalClientId, pChar, State.m_RescueManualPos);
		}
		else
		{
			EchoPractice("can't set manual rescue while not grounded");
		}
		return true;
	}

	if(State.m_HasRescueAuto && IsSafeRescuePosition(State.m_RescueAutoPos, pChar->GetProximityRadius()))
	{
		ApplyPracticeTeleport(LocalClientId, pChar, State.m_RescueAutoPos);
		return true;
	}
	if(IsSafeRescuePosition(CurrentPos, pChar->GetProximityRadius()))
	{
		TrackSafeRescuePosition(LocalClientId, pChar);
		EchoPractice("safe position updated");
	}
	else
	{
		EchoPractice("no safe rescue position found");
	}
	return true;
}

bool CFastPractice::ExecutePracticeTeleportCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs)
{
	const auto &State = m_aPracticeCommandState[LocalClientId];
	if(Cmd == "back")
	{
		if(!State.m_HasLastDeath)
		{
			EchoPractice("there is nowhere to go back to");
			return true;
		}
		ApplyPracticeTeleport(LocalClientId, pChar, State.m_LastDeathPos);
		return true;
	}

	if(Cmd == "lasttp")
	{
		if(!State.m_HasLastTeleport)
		{
			EchoPractice("you haven't previously teleported");
			return true;
		}
		ApplyPracticeTeleport(LocalClientId, pChar, State.m_LastTeleportPos);
		return true;
	}

	if(Cmd == "tp" || Cmd == "teleport" || Cmd == "tc" || Cmd == "telecursor")
	{
		vec2 Target = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy];
		if(Cmd == "tc" || Cmd == "telecursor")
		{
			Target = GameClient()->m_Camera.m_Center;

			const vec2 CursorTarget = vec2((float)pChar->Core()->m_Input.m_TargetX, (float)pChar->Core()->m_Input.m_TargetY);
			vec2 TargetCameraOffset(0.0f, 0.0f);
			const float CursorLength = length(CursorTarget);
			if(CursorLength > 0.0001f)
			{
				const float OffsetAmount = maximum(CursorLength - (float)GameClient()->m_Camera.Deadzone(), 0.0f) * ((float)GameClient()->m_Camera.FollowFactor() / 100.0f);
				TargetCameraOffset = normalize_pre_length(CursorTarget, CursorLength) * OffsetAmount;
			}

			Target = pChar->Core()->m_Pos + (CursorTarget - TargetCameraOffset) * GameClient()->m_Camera.m_Zoom + TargetCameraOffset;
		}

		if(vArgs.size() > 1)
		{
			const int TargetId = FindClientByName(vArgs[1].c_str());
			if(TargetId < 0 || !GameClient()->m_Snap.m_aCharacters[TargetId].m_Active)
			{
				EchoPractice("no player with this name found");
				return true;
			}
			Target = vec2((float)GameClient()->m_Snap.m_aCharacters[TargetId].m_Cur.m_X, (float)GameClient()->m_Snap.m_aCharacters[TargetId].m_Cur.m_Y);
		}

		ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(Target));
		return true;
	}

	if(Cmd == "tpxy")
	{
		if(vArgs.size() < 3)
		{
			EchoPractice("usage: /tpxy x y");
			return true;
		}

		float X = 0.0f;
		float Y = 0.0f;
		if(!ParseCoordinateToken(vArgs[1].c_str(), pChar->Core()->m_Pos.x, X))
		{
			EchoPractice("invalid X coordinate");
			return true;
		}
		if(!ParseCoordinateToken(vArgs[2].c_str(), pChar->Core()->m_Pos.y, Y))
		{
			EchoPractice("invalid Y coordinate");
			return true;
		}

		ApplyPracticeTeleport(LocalClientId, pChar, ClampToPracticePlayableBounds(vec2(X, Y)));
		return true;
	}

	if(Cmd != "totele" && Cmd != "totelecp")
		return false;

	if(vArgs.size() < 2)
	{
		EchoPractice("usage: /%s <index>", Cmd.c_str());
		return true;
	}

	int TeleIndex = 0;
	if(!str_toint(vArgs[1].c_str(), &TeleIndex) || TeleIndex <= 0)
	{
		EchoPractice("invalid teleporter index");
		return true;
	}

	const auto &vTeleOuts = Cmd == "totele" ? Collision()->TeleOuts(TeleIndex - 1) : Collision()->TeleCheckOuts(TeleIndex - 1);
	if(vTeleOuts.empty())
	{
		EchoPractice("there is no teleporter with that index on the map");
		return true;
	}

	ApplyPracticeTeleport(LocalClientId, pChar, vTeleOuts[0]);
	return true;
}

bool CFastPractice::ExecutePracticeStateCommand(CCharacter *pChar, const std::string &Cmd)
{
	if(Cmd == "solo" || Cmd == "unsolo")
	{
		pChar->SetSolo(Cmd == "solo");
		return true;
	}

	if(Cmd == "deep" || Cmd == "undeep")
	{
		CCharacterCore Core = pChar->GetCore();
		if(Cmd == "deep")
		{
			Core.m_DeepFrozen = true;
			pChar->SetCore(Core);
			pChar->Freeze();
		}
		else
		{
			Core.m_DeepFrozen = false;
			pChar->SetCore(Core);
			pChar->Unfreeze();
		}
		NormalizeCharacterAfterReset(pChar, Cmd == "deep");
		return true;
	}

	if(Cmd == "livefreeze" || Cmd == "unlivefreeze")
	{
		CCharacterCore Core = pChar->GetCore();
		Core.m_LiveFrozen = Cmd == "livefreeze";
		pChar->SetCore(Core);
		if(Cmd == "unlivefreeze")
			pChar->Unfreeze();
		NormalizeCharacterAfterReset(pChar, Cmd == "livefreeze");
		return true;
	}

	if(Cmd == "ninja" || Cmd == "unninja")
	{
		if(Cmd == "ninja")
			pChar->GiveNinja();
		else
			pChar->RemoveNinja();
		return true;
	}

	return false;
}

bool CFastPractice::ExecutePracticeWeaponCommand(CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs, bool &WeaponsMutated)
{
	if(Cmd == "shotgun" || Cmd == "grenade" || Cmd == "laser" || Cmd == "rifle" || Cmd == "unshotgun" || Cmd == "ungrenade" || Cmd == "unlaser" || Cmd == "unrifle" || Cmd == "weapons" || Cmd == "unweapons")
	{
		if(Cmd == "shotgun")
			GivePracticeWeapon(pChar, WEAPON_SHOTGUN, false);
		else if(Cmd == "grenade")
			GivePracticeWeapon(pChar, WEAPON_GRENADE, false);
		else if(Cmd == "laser" || Cmd == "rifle")
			GivePracticeWeapon(pChar, WEAPON_LASER, false);
		else if(Cmd == "unshotgun")
			GivePracticeWeapon(pChar, WEAPON_SHOTGUN, true);
		else if(Cmd == "ungrenade")
			GivePracticeWeapon(pChar, WEAPON_GRENADE, true);
		else if(Cmd == "unlaser" || Cmd == "unrifle")
			GivePracticeWeapon(pChar, WEAPON_LASER, true);
		else if(Cmd == "weapons")
			GivePracticeWeapon(pChar, -1, false);
		else if(Cmd == "unweapons")
			GivePracticeWeapon(pChar, -1, true);
		EnsureActiveWeaponIsValid(pChar);
		WeaponsMutated = true;
		return true;
	}

	if(Cmd != "addweapon" && Cmd != "removeweapon")
		return false;

	if(vArgs.size() < 2)
	{
		EchoPractice("usage: /%s <weapon-id>", Cmd.c_str());
		return true;
	}

	int WeaponId = 0;
	if(!str_toint(vArgs[1].c_str(), &WeaponId) || WeaponId != ClampWeaponId(WeaponId))
	{
		EchoPractice("invalid weapon id");
		return true;
	}

	GivePracticeWeapon(pChar, WeaponId, Cmd == "removeweapon");
	EnsureActiveWeaponIsValid(pChar);
	WeaponsMutated = true;
	return true;
}

bool CFastPractice::ExecutePracticeMovementCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs)
{
	if(Cmd != "jetpack" && Cmd != "unjetpack" && Cmd != "infjump" && Cmd != "uninfjump" && Cmd != "endless" && Cmd != "unendless" && Cmd != "setjumps")
		return false;

	auto &State = m_aPracticeCommandState[LocalClientId];
	CCharacterCore Core = pChar->GetCore();
	if(Cmd == "jetpack")
		Core.m_Jetpack = true;
	else if(Cmd == "unjetpack")
		Core.m_Jetpack = false;
	else if(Cmd == "infjump")
	{
		Core.m_EndlessJump = true;
		State.m_InvincibleAddedEndlessJump = false;
	}
	else if(Cmd == "uninfjump")
	{
		Core.m_EndlessJump = false;
		State.m_InvincibleAddedEndlessJump = false;
	}
	else if(Cmd == "endless")
		Core.m_EndlessHook = true;
	else if(Cmd == "unendless")
		Core.m_EndlessHook = false;
	else if(Cmd == "setjumps")
	{
		if(vArgs.size() < 2)
		{
			EchoPractice("usage: /setjumps <count>");
			return true;
		}
		int Jumps = 0;
		if(!str_toint(vArgs[1].c_str(), &Jumps))
		{
			EchoPractice("invalid jumps value");
			return true;
		}
		Core.m_Jumps = Jumps;
		Core.m_Jumped = 0;
		Core.m_JumpedTotal = 0;
	}

	pChar->SetCore(Core);
	return true;
}

bool CFastPractice::ExecutePracticeInvincibleCommand(int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs)
{
	auto &State = m_aPracticeCommandState[LocalClientId];
	bool Invincible = false;
	CCharacterCore Core = pChar->GetCore();
	if(vArgs.size() > 1)
	{
		int Value = 0;
		if(!str_toint(vArgs[1].c_str(), &Value))
		{
			EchoPractice("invalid value, use 0 or 1");
			return true;
		}
		Invincible = Value != 0;
	}
	else
	{
		Invincible = !Core.m_Invincible;
	}

	if(Invincible)
	{
		pChar->SetSuper(false);
		Core = pChar->GetCore();
		Core.m_DeepFrozen = false;
		Core.m_LiveFrozen = false;
		Core.m_IsInFreeze = false;
		Core.m_FreezeEnd = 0;
		if(!Core.m_EndlessJump)
		{
			Core.m_EndlessJump = true;
			State.m_InvincibleAddedEndlessJump = true;
		}
		else
		{
			State.m_InvincibleAddedEndlessJump = false;
		}
		pChar->SetCore(Core);
		pChar->Unfreeze();
		pChar->m_FreezeTime = 0;
	}

	Core = pChar->GetCore();
	if(!Invincible && State.m_InvincibleAddedEndlessJump)
	{
		Core.m_EndlessJump = false;
		State.m_InvincibleAddedEndlessJump = false;
	}
	Core.m_Invincible = Invincible;
	pChar->SetCore(Core);
	return true;
}

bool CFastPractice::ExecutePracticeCollisionCommand(CCharacter *pChar, const std::string &Cmd)
{
	if(Cmd != "collision" && Cmd != "hookcollision")
		return false;

	CCharacterCore Core = pChar->GetCore();
	if(Cmd == "collision")
		Core.m_CollisionDisabled = !Core.m_CollisionDisabled;
	else
		Core.m_HookHitDisabled = !Core.m_HookHitDisabled;
	pChar->SetCore(Core);
	return true;
}

bool CFastPractice::ExecutePracticeHitOthersCommand(CCharacter *pChar, const std::vector<std::string> &vArgs)
{
	if(vArgs.size() <= 1 || str_comp_nocase(vArgs[1].c_str(), "all") == 0)
	{
		CCharacterCore Core = pChar->GetCore();
		const bool IsDisabled = Core.m_HammerHitDisabled && Core.m_ShotgunHitDisabled && Core.m_GrenadeHitDisabled && Core.m_LaserHitDisabled;
		Core.m_HammerHitDisabled = !IsDisabled;
		Core.m_ShotgunHitDisabled = !IsDisabled;
		Core.m_GrenadeHitDisabled = !IsDisabled;
		Core.m_LaserHitDisabled = !IsDisabled;
		pChar->SetCore(Core);
		return true;
	}

	const std::string Arg = LowercaseCopy(vArgs[1]);
	if(Arg == "hammer")
		TogglePracticeHit(pChar, WEAPON_HAMMER);
	else if(Arg == "shotgun")
		TogglePracticeHit(pChar, WEAPON_SHOTGUN);
	else if(Arg == "grenade")
		TogglePracticeHit(pChar, WEAPON_GRENADE);
	else if(Arg == "laser")
		TogglePracticeHit(pChar, WEAPON_LASER);
	else
		EchoPractice("unknown argument for /hitothers");
	return true;
}

bool CFastPractice::ExecutePracticeCommand(int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs, bool &WeaponsMutated)
{
	WeaponsMutated = false;
	if(vArgs.empty() || vArgs[0].size() < 2 || vArgs[0][0] != '/')
		return false;

	const std::string Cmd = LowercaseCopy(vArgs[0].substr(1));
	if(ExecutePracticeMetaCommand(Cmd))
		return true;
	if(ExecutePracticeRescueCommand(LocalClientId, pChar, Cmd, vArgs))
		return true;
	if(ExecutePracticeTeleportCommand(LocalClientId, pChar, Cmd, vArgs))
		return true;
	if(ExecutePracticeStateCommand(pChar, Cmd))
		return true;
	if(ExecutePracticeWeaponCommand(pChar, Cmd, vArgs, WeaponsMutated))
		return true;
	if(ExecutePracticeMovementCommand(LocalClientId, pChar, Cmd, vArgs))
		return true;
	if(Cmd == "invincible" || Cmd == "invisbl" || Cmd == "invinsbl")
		return ExecutePracticeInvincibleCommand(LocalClientId, pChar, vArgs);
	if(ExecutePracticeCollisionCommand(pChar, Cmd))
		return true;
	if(Cmd == "hitothers")
		return ExecutePracticeHitOthersCommand(pChar, vArgs);
	return false;
}

bool CFastPractice::ConsumePracticeChatCommand(int Team, const char *pLine)
{
	(void)Team;
	if(!m_Enabled || !pLine || pLine[0] != '/')
		return false;

	std::vector<std::string> vArgs;
	if(!ParseCommandArgs(pLine, vArgs))
		return false;

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return true;
	}

	// In spectator mode the practice world is not updated, so handle teleport commands
	// by storing a pending teleport that will be applied when the player leaves spec.
	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active ||
				(GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team == TEAM_SPECTATORS);
	if(Spectating && !vArgs.empty() && vArgs[0].size() >= 2 && vArgs[0][0] == '/')
	{
		const std::string Cmd = LowercaseCopy(vArgs[0].substr(1));
		if(Cmd == "tc" || Cmd == "telecursor")
		{
			vec2 Target = GameClient()->m_Camera.m_Center;
			auto &State = m_aPracticeCommandState[LocalClientId];
			State.m_HasPendingTeleport = true;
			State.m_PendingTeleportPos = Target;
			return true;
		}
		if(Cmd == "tp" || Cmd == "teleport")
		{
			vec2 Target = GameClient()->m_Camera.m_Center;
			if(vArgs.size() > 1)
			{
				const int TargetId = FindClientByName(vArgs[1].c_str());
				if(TargetId < 0 || !GameClient()->m_Snap.m_aCharacters[TargetId].m_Active)
				{
					EchoPractice("no player with this name found");
					return true;
				}
				Target = vec2((float)GameClient()->m_Snap.m_aCharacters[TargetId].m_Cur.m_X, (float)GameClient()->m_Snap.m_aCharacters[TargetId].m_Cur.m_Y);
			}
			auto &State = m_aPracticeCommandState[LocalClientId];
			State.m_HasPendingTeleport = true;
			State.m_PendingTeleportPos = Target;
			return true;
		}
	}

	CCharacter *pBaseChar = m_PracticeWorld.GetCharacterById(LocalClientId);
	if(!pBaseChar)
	{
		EchoPractice("practice character is not available");
		return true;
	}

	bool WeaponsMutated = false;
	const bool Consumed = ExecutePracticeCommand(LocalClientId, pBaseChar, vArgs, WeaponsMutated);
	if(!Consumed)
		return false;
	if(!m_Enabled)
		return true;
	FinishMutation(LocalClientId, DummyClientId, pBaseChar, WeaponsMutated);

	if(m_RequireDummy && DummyClientId >= 0 && !m_PracticeWorld.GetCharacterById(DummyClientId))
	{
		Disable();
		return true;
	}

	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	return true;
}

void CFastPractice::OnConsoleInit()
{
	Console()->Register("fast_practice_toggle", "", CFGFLAG_CLIENT, ConFastPracticeToggle, this, "Toggle fast practice mode");
}
