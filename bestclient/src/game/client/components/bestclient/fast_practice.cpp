#include "fast_practice.h"

#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/projectile_data.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/prediction/entities/laser.h>
#include <game/client/prediction/entities/projectile.h>
#include <game/gamecore.h>
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

float EffectiveFastInputOffsetTicks()
{
	if(!g_Config.m_TcFastInput)
		return 0.0f;

	if(g_Config.m_BcFastInputMode == 0)
	{
		if(g_Config.m_TcFastInputAmount <= 0)
			return 0.0f;
		return g_Config.m_TcFastInputAmount / 20.0f;
	}

	if(g_Config.m_BcFastInputLowDelta <= 0)
		return 0.0f;
	return g_Config.m_BcFastInputLowDelta / 100.0f;
}

int FastInputPredictionTicks(float OffsetTicks)
{
	if(OffsetTicks <= 0.0f)
		return 0;
	return (int)std::ceil(OffsetTicks);
}

bool EffectiveFastInputOthers()
{
	return g_Config.m_BcFastInputMode == 0 ? g_Config.m_TcFastInputOthers != 0 : g_Config.m_BcLowDeltaOthers != 0;
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
	m_RequireDummy = false;
	m_EnableLocalClientId = -1;
	m_EnableDummyClientId = -1;
	m_PracticeWorldInitialized = false;
	m_HasDummyAnchor = false;
	m_SuppressFireOnNextPredictTick = false;
	m_InputSuppressTicks = 0;
	m_LastResolvedLocalClientId = -1;
	m_LastResolvedDummyClientId = -1;
	m_LastResolvedLocalInputConn = -1;
	m_LastResolvedDummyInputConn = -1;
	m_aHasServerLockedTargets.fill(false);
	m_aServerLockedTargets.fill(ivec2(1, 0));
	m_MainAnchor = {};
	m_DummyAnchor = {};
	m_PracticeBaseWorld.Clear();
	ResetAttackTickHistory();
	ResetCommandState();
}

void CFastPractice::ResetCommandState()
{
	for(auto &State : m_aPracticeCommandState)
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

int CFastPractice::DummyClientId() const
{
	if(!Client()->DummyConnected())
		return -1;

	const int DummySlot = !g_Config.m_ClDummy;
	const int DummyId = GameClient()->m_aLocalIds[DummySlot];
	if(DummyId < 0 || DummyId >= MAX_CLIENTS)
		return -1;

	if(!GameClient()->m_Snap.m_aCharacters[DummyId].m_Active || GameClient()->m_aClients[DummyId].m_Paused)
		return -1;

	return DummyId;
}

bool CFastPractice::ResolveParticipantInputs(int LocalClientId, int DummyClientId, int &LocalInputConn, int &DummyInputConn) const
{
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return false;

	// Client()->GetInput(Tick, IsDummy) expects a local slot, not a raw connection id:
	// 0 = currently controlled player, 1 = the other one.
	LocalInputConn = 0;
	DummyInputConn = -1;

	if(m_RequireDummy)
	{
		DummyInputConn = 1;
		if(DummyClientId < 0 || DummyClientId >= MAX_CLIENTS || DummyClientId == LocalClientId)
		{
			if(g_Config.m_Debug)
				dbg_msg("fast_practice", "input mapping failed: local_id=%d dummy_id=%d", LocalClientId, DummyClientId);
			return false;
		}
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
		return false;
	if(LocalClientId != m_EnableLocalClientId && LocalClientId != m_EnableDummyClientId)
		return false;

	if(m_RequireDummy)
		DummyClientId = LocalClientId == m_EnableLocalClientId ? m_EnableDummyClientId : m_EnableLocalClientId;

	return true;
}

bool CFastPractice::IsPracticeParticipant(int ClientId) const
{
	if(!m_Enabled || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	return ClientId == m_EnableLocalClientId || (m_EnableDummyClientId >= 0 && ClientId == m_EnableDummyClientId);
}

int CFastPractice::CurrentPracticeDummyId() const
{
	if(!m_Enabled || !m_RequireDummy)
		return -1;

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
		return -1;
	return DummyClientId;
}

int CFastPractice::PracticeDummyId() const
{
	return CurrentPracticeDummyId();
}

bool CFastPractice::IsPracticeDummy(int ClientId) const
{
	return ClientId >= 0 && ClientId == CurrentPracticeDummyId();
}

bool CFastPractice::ForcePredictWeapons() const
{
	return m_Enabled;
}

bool CFastPractice::ForcePredictGrenade() const
{
	return m_Enabled;
}

bool CFastPractice::ForcePredictGunfire() const
{
	return m_Enabled;
}

bool CFastPractice::ForcePredictPlayers() const
{
	return m_Enabled;
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

void CFastPractice::SyncPracticeWorldConfig()
{
	m_PracticeBaseWorld.m_WorldConfig = GameClient()->m_GameWorld.m_WorldConfig;
	m_PracticeBaseWorld.m_WorldConfig.m_PredictWeapons = true;
	m_PracticeBaseWorld.m_WorldConfig.m_PredictFreeze = true;
	m_PracticeBaseWorld.m_Teams = GameClient()->m_Teams;
}

bool CFastPractice::InitPracticeWorld()
{
	m_PracticeBaseWorld.CopyWorldClean(&GameClient()->m_GameWorld);
	PrunePracticeWorld(m_PracticeBaseWorld);
	SyncPracticeWorldConfig();
	m_PracticeWorldInitialized = true;

	if(!m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
	{
		m_PracticeWorldInitialized = false;
		return false;
	}

	if(m_RequireDummy && !m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
	{
		m_PracticeWorldInitialized = false;
		return false;
	}

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

	if(CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		m_aLastAttackTick[m_EnableLocalClientId] = pChar->GetAttackTick();
	if(m_EnableDummyClientId >= 0)
		if(CCharacter *pChar = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
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

	if(!InitPracticeWorld())
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

	CaptureServerLockedTargets();
	m_Enabled = true;
	m_PracticeBaseWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	ResetCommandState();
	ResetAttackTickHistory();
	if(CCharacter *pLocal = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pLocal);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		if(CCharacter *pDummy = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
			TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);
	ReleaseBufferedInputState();

	// Snap renderer/camera prediction state to the local practice world immediately.
	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	GameClient()->m_PrevPredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	if(CCharacter *pPredChar = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableLocalClientId))
	{
		GameClient()->m_PredictedChar = pPredChar->GetCore();
		GameClient()->m_PredictedPrevChar = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_Predicted = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_PrevPredicted = pPredChar->GetCore();
	}
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
	{
		if(CCharacter *pPredDummy = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableDummyClientId))
		{
			GameClient()->m_aClients[m_EnableDummyClientId].m_Predicted = pPredDummy->GetCore();
			GameClient()->m_aClients[m_EnableDummyClientId].m_PrevPredicted = pPredDummy->GetCore();
		}
	}
	GameClient()->m_PredictedTick = m_PracticeBaseWorld.GameTick();
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

	if(!InitPracticeWorld())
	{
		Disable();
		return;
	}

	if(!ApplyAnchorToCharacter(m_PracticeBaseWorld, m_MainAnchor))
	{
		Disable();
		return;
	}

	if(m_RequireDummy && !ApplyAnchorToCharacter(m_PracticeBaseWorld, m_DummyAnchor))
	{
		Disable();
		return;
	}

	if(CCharacter *pMain = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		NormalizeCharacterAfterReset(pMain, false);
	if(m_RequireDummy)
	{
		if(CCharacter *pDummy = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
			NormalizeCharacterAfterReset(pDummy, false);
	}

	m_PracticeBaseWorld.m_GameTick = Client()->PredGameTick(g_Config.m_ClDummy);
	ResetAttackTickHistory();
	if(CCharacter *pMain = m_PracticeBaseWorld.GetCharacterById(m_EnableLocalClientId))
		TrackSafeRescuePosition(m_EnableLocalClientId, pMain);
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
		if(CCharacter *pDummy = m_PracticeBaseWorld.GetCharacterById(m_EnableDummyClientId))
			TrackSafeRescuePosition(m_EnableDummyClientId, pDummy);
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
	ReleaseBufferedInputState();

	// Keep camera interpolation coherent after hard reset.
	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	GameClient()->m_PrevPredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	if(CCharacter *pPredChar = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableLocalClientId))
	{
		GameClient()->m_PredictedChar = pPredChar->GetCore();
		GameClient()->m_PredictedPrevChar = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_Predicted = pPredChar->GetCore();
		GameClient()->m_aClients[m_EnableLocalClientId].m_PrevPredicted = pPredChar->GetCore();
	}
	if(m_RequireDummy && m_EnableDummyClientId >= 0)
	{
		if(CCharacter *pPredDummy = GameClient()->m_PredictedWorld.GetCharacterById(m_EnableDummyClientId))
		{
			GameClient()->m_aClients[m_EnableDummyClientId].m_Predicted = pPredDummy->GetCore();
			GameClient()->m_aClients[m_EnableDummyClientId].m_PrevPredicted = pPredDummy->GetCore();
		}
	}
	GameClient()->m_PredictedTick = m_PracticeBaseWorld.GameTick();
}

void CFastPractice::PrepareInputForSend(int *pData, int Size, bool Dummy) const
{
	if(!m_Enabled || !pData || Size < (int)sizeof(CNetObj_PlayerInput))
		return;

	auto *pInput = reinterpret_cast<CNetObj_PlayerInput *>(pData);
	NeutralizeInput(*pInput);
	// Never propagate shoot toggles to the server while fast practice is active.
	pInput->m_Fire = 0;

	// Keep server-side cursor fully locked per input stream so real world
	// doesn't move when aiming inside local practice.
	const int Slot = g_Config.m_ClDummy ^ (int)Dummy;
	ivec2 LockedTarget = ivec2(1, 0);
	if(Slot >= 0 && Slot < NUM_DUMMIES && m_aHasServerLockedTargets[Slot])
		LockedTarget = m_aServerLockedTargets[Slot];
	pInput->m_TargetX = LockedTarget.x;
	pInput->m_TargetY = LockedTarget.y;
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
	const int Num = GameClient()->m_PredictedWorld.FindEntities(StartPos, pChar->GetProximityRadius() * 2.0f, apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
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

bool CFastPractice::AdvanceBaseWorldToTick(int TargetTick, int LocalClientId, int DummyClientId)
{
	if(!m_PracticeWorldInitialized)
		return false;

	if(m_PracticeBaseWorld.GameTick() > TargetTick)
		return InitPracticeWorld();

	int LocalInputConn = -1;
	int DummyInputConn = -1;
	if(!ResolveParticipantInputs(LocalClientId, DummyClientId, LocalInputConn, DummyInputConn))
		return false;

	const int FirstBaseTick = m_PracticeBaseWorld.GameTick() + 1;
	for(int Tick = m_PracticeBaseWorld.GameTick() + 1; Tick <= TargetTick; Tick++)
	{
		CCharacter *pLocalChar = m_PracticeBaseWorld.GetCharacterById(LocalClientId);
		CCharacter *pDummyChar = m_RequireDummy ? m_PracticeBaseWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
			return false;

		CNetObj_PlayerInput *pInputData = (CNetObj_PlayerInput *)Client()->GetInput(Tick, LocalInputConn);
		CNetObj_PlayerInput *pDummyInputData = pDummyChar ? (CNetObj_PlayerInput *)Client()->GetInput(Tick, DummyInputConn) : nullptr;
		CNetObj_PlayerInput LocalSuppressedInput = {};
		CNetObj_PlayerInput DummySuppressedInput = {};
			const bool SuppressTransitionTick = Tick == FirstBaseTick && m_SuppressFireOnNextPredictTick;
			const bool SuppressCooldownTick = Tick >= FirstBaseTick && m_InputSuppressTicks > 0;
			if(SuppressTransitionTick || SuppressCooldownTick)
			{
				if(pInputData)
				{
				LocalSuppressedInput = *pInputData;
				LocalSuppressedInput.m_Fire = ReleasedFireState(pLocalChar->LatestInput()->m_Fire);
				LocalSuppressedInput.m_WantedWeapon = 0;
				LocalSuppressedInput.m_NextWeapon = 0;
				LocalSuppressedInput.m_PrevWeapon = 0;
				pInputData = &LocalSuppressedInput;
			}
			if(pDummyInputData)
			{
				DummySuppressedInput = *pDummyInputData;
				DummySuppressedInput.m_Fire = ReleasedFireState(pDummyChar->LatestInput()->m_Fire);
				DummySuppressedInput.m_WantedWeapon = 0;
				DummySuppressedInput.m_NextWeapon = 0;
				DummySuppressedInput.m_PrevWeapon = 0;
					pDummyInputData = &DummySuppressedInput;
				}
				if(m_InputSuppressTicks > 0)
					m_InputSuppressTicks--;
				m_SuppressFireOnNextPredictTick = false;
			}
		const bool DummyFirst = pInputData && pDummyInputData && pDummyChar->GetCid() < pLocalChar->GetCid();

		if(DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		m_PracticeBaseWorld.m_GameTick = Tick;
		if(pInputData)
			pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);

		m_PracticeBaseWorld.Tick();

		TrackSafeRescuePosition(LocalClientId, pLocalChar);
		if(pDummyChar)
			TrackSafeRescuePosition(DummyClientId, pDummyChar);
	}

	return true;
}

bool CFastPractice::OverridePredict()
{
	if(!m_Enabled)
		return false;

	if(Client()->State() != IClient::STATE_ONLINE)
	{
		Disable();
		return false;
	}

	int LocalClientId = -1;
	int DummyClientId = -1;
	if(!ResolvePracticeRoles(LocalClientId, DummyClientId))
	{
		Disable();
		return false;
	}

	int LocalInputConn = -1;
	int DummyInputConn = -1;
	if(!ResolveParticipantInputs(LocalClientId, DummyClientId, LocalInputConn, DummyInputConn))
	{
		Disable();
		return false;
	}

	const bool InputMappingChanged = LocalClientId != m_LastResolvedLocalClientId ||
		DummyClientId != m_LastResolvedDummyClientId ||
		LocalInputConn != m_LastResolvedLocalInputConn ||
		DummyInputConn != m_LastResolvedDummyInputConn;
		if(InputMappingChanged)
		{
			if(g_Config.m_Debug)
				dbg_msg("fast_practice", "role/input remap: local_id=%d dummy_id=%d local_conn=%d dummy_conn=%d",
					LocalClientId, DummyClientId, LocalInputConn, DummyInputConn);
			m_SuppressFireOnNextPredictTick = true;
			m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);
			ReleaseBufferedInputState();
		}

	GameClient()->m_PredictedDummyId = DummyClientId;

	if(!m_PracticeWorldInitialized && !InitPracticeWorld())
	{
		Disable();
		return false;
	}

	SyncPracticeWorldConfig();

	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);

	CCharacter *pLocalChar = GameClient()->m_PredictedWorld.GetCharacterById(LocalClientId);
	CCharacter *pDummyChar = m_RequireDummy ? GameClient()->m_PredictedWorld.GetCharacterById(DummyClientId) : nullptr;
	if(!pLocalChar || (m_RequireDummy && !pDummyChar))
	{
		Disable();
		return false;
	}

	const int BaseGameTick = m_PracticeBaseWorld.GameTick();
	const int PredTick = Client()->PredGameTick(g_Config.m_ClDummy);
	const int FinalTickRegular = PredTick;
	const int FinalTickSelf = FinalTickRegular;
	const int FinalTickOthers = FinalTickSelf;

	const int LocalTickSlot = LocalInputConn;
	const int DummyTickSlot = m_RequireDummy ? DummyInputConn : (LocalTickSlot ^ 1);

	if(FinalTickSelf <= BaseGameTick)
	{
		GameClient()->m_PredictedChar = pLocalChar->GetCore();
		GameClient()->m_aClients[LocalClientId].m_Predicted = pLocalChar->GetCore();
		if(pDummyChar)
		{
			GameClient()->m_aClients[DummyClientId].m_Predicted = pDummyChar->GetCore();
		}
		GameClient()->m_PredictedTick = BaseGameTick;
		return true;
	}

	for(int Tick = BaseGameTick + 1; Tick <= FinalTickSelf; Tick++)
	{
		std::vector<STrackedProjectile> vTrackedExplosiveBefore;
		std::vector<STrackedProjectile> vTrackedExplosiveAfter;

		pLocalChar = GameClient()->m_PredictedWorld.GetCharacterById(LocalClientId);
		pDummyChar = m_RequireDummy ? GameClient()->m_PredictedWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
		{
			Disable();
			return false;
		}

		if(Tick == FinalTickSelf)
		{
			GameClient()->m_PrevPredictedWorld.CopyWorldClean(&GameClient()->m_PredictedWorld);
			GameClient()->m_PredictedPrevChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_PrevPredicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(i))
					GameClient()->m_aClients[i].m_PrevPredicted = pChar->GetCore();
		}
		if(Tick == PredTick)
		{
			GameClient()->m_PredictedPrevChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_PrevPredicted = pLocalChar->GetCore();
			if(pDummyChar)
				GameClient()->m_aClients[DummyClientId].m_PrevPredicted = pDummyChar->GetCore();
		}

		CNetObj_PlayerInput *pInputData = (CNetObj_PlayerInput *)Client()->GetInput(Tick, LocalInputConn);
		CNetObj_PlayerInput *pDummyInputData = pDummyChar ? (CNetObj_PlayerInput *)Client()->GetInput(Tick, DummyInputConn) : nullptr;
		CNetObj_PlayerInput LocalNeutralizedInput = {};
		CNetObj_PlayerInput DummyNeutralizedInput = {};
		const bool SuppressTransitionTick = Tick == BaseGameTick + 1 && (m_SuppressFireOnNextPredictTick || GameClient()->m_IsDummySwapping);
		const bool SuppressCooldownTick = Tick >= BaseGameTick + 1 && m_InputSuppressTicks > 0;
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
		const bool DummyFirst = pInputData && pDummyInputData && pDummyChar->GetCid() < pLocalChar->GetCid();

		pLocalChar->m_CanMoveInFreeze = false;
		if(pDummyChar)
			pDummyChar->m_CanMoveInFreeze = false;

		CollectTrackedProjectiles(GameClient()->m_PredictedWorld, LocalClientId, DummyClientId, vTrackedExplosiveBefore);

		if(DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		GameClient()->m_PredictedWorld.m_GameTick = Tick;
		if(pInputData)
			pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);
		GameClient()->m_PredictedWorld.Tick();

		TrackSafeRescuePosition(LocalClientId, pLocalChar);
		if(pDummyChar)
			TrackSafeRescuePosition(DummyClientId, pDummyChar);

		CollectTrackedProjectiles(GameClient()->m_PredictedWorld, LocalClientId, DummyClientId, vTrackedExplosiveAfter);
		for(const auto &TrackedProj : vTrackedExplosiveBefore)
		{
			const bool StillExists = std::any_of(vTrackedExplosiveAfter.begin(), vTrackedExplosiveAfter.end(), [&](const STrackedProjectile &Candidate) {
				return SameProjectile(TrackedProj, Candidate);
			});
			if(StillExists)
				continue;

			const int TickSpeed = Client()->GameTickSpeed();
			const int TuneZone = std::clamp(TrackedProj.m_TuneZone, 0, TuneZone::NUM - 1);
			const CTuningParams *pTuning = &GameClient()->m_aTuning[TuneZone];
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

		if(Tick == FinalTickSelf)
		{
			GameClient()->m_PredictedChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_Predicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(i))
					GameClient()->m_aClients[i].m_Predicted = pChar->GetCore();
		}
		if(Tick == PredTick)
		{
			GameClient()->m_PredictedChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_Predicted = pLocalChar->GetCore();
			if(pDummyChar)
				GameClient()->m_aClients[DummyClientId].m_Predicted = pDummyChar->GetCore();
		}

		for(int i = 0; i < MAX_CLIENTS; i++)
			if(CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(i))
			{
				GameClient()->m_aClients[i].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
				GameClient()->m_aClients[i].m_aPredTick[Tick % 200] = Tick;
			}

		if(Tick > GameClient()->m_aLastNewPredictedTick[LocalTickSlot] && Tick <= FinalTickRegular)
		{
			GameClient()->m_aLastNewPredictedTick[LocalTickSlot] = Tick;
			GameClient()->m_NewPredictedTick = true;
			const vec2 Pos = pLocalChar->Core()->m_Pos;
			const int Events = pLocalChar->Core()->m_TriggeredEvents;
			if(!GameClient()->m_SuppressEvents)
				if(Events & COREEVENT_AIR_JUMP)
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

		if(pDummyChar && Tick > GameClient()->m_aLastNewPredictedTick[DummyTickSlot])
		{
			GameClient()->m_aLastNewPredictedTick[DummyTickSlot] = Tick;
			const vec2 Pos = pDummyChar->Core()->m_Pos;
			const int Events = pDummyChar->Core()->m_TriggeredEvents;
			if(!GameClient()->m_SuppressEvents)
				if(Events & COREEVENT_AIR_JUMP)
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
	}

	m_PracticeBaseWorld.CopyWorldClean(&GameClient()->m_PredictedWorld);
	GameClient()->m_PredictedTick = ApplyVisualFastInputPrediction(FinalTickRegular, LocalClientId, DummyClientId, LocalInputConn, DummyInputConn);
	if(GameClient()->m_NewPredictedTick)
		GameClient()->m_Ghost.OnNewPredictedSnapshot();
	m_LastResolvedLocalClientId = LocalClientId;
	m_LastResolvedDummyClientId = DummyClientId;
	m_LastResolvedLocalInputConn = LocalInputConn;
	m_LastResolvedDummyInputConn = DummyInputConn;

	return true;
}

int CFastPractice::ApplyVisualFastInputPrediction(int FinalTickRegular, int LocalClientId, int DummyClientId, int LocalInputConn, int DummyInputConn)
{
	const float FastInputOffsetTicks = EffectiveFastInputOffsetTicks();
	const int FastInputTicks = FastInputPredictionTicks(FastInputOffsetTicks);
	if(FastInputTicks <= 0)
		return FinalTickRegular;

	CGameWorld VisualWorld;
	VisualWorld.CopyWorldClean(&m_PracticeBaseWorld);

	CCharacter *pLocalChar = VisualWorld.GetCharacterById(LocalClientId);
	CCharacter *pDummyChar = m_RequireDummy ? VisualWorld.GetCharacterById(DummyClientId) : nullptr;
	if(!pLocalChar || (m_RequireDummy && !pDummyChar))
		return FinalTickRegular;

	const int FinalTickSelf = FinalTickRegular + FastInputTicks;
	int FinalTickOthers = FinalTickSelf;
	if(!EffectiveFastInputOthers())
		FinalTickOthers = FinalTickRegular;

	for(int Tick = FinalTickRegular + 1; Tick <= FinalTickSelf; Tick++)
	{
		pLocalChar = VisualWorld.GetCharacterById(LocalClientId);
		pDummyChar = m_RequireDummy ? VisualWorld.GetCharacterById(DummyClientId) : nullptr;
		if(!pLocalChar || (m_RequireDummy && !pDummyChar))
			return FinalTickRegular;

		if(Tick == FinalTickSelf)
		{
			GameClient()->m_PredictedWorld.CopyWorldClean(&VisualWorld);
			GameClient()->m_PrevPredictedWorld.CopyWorldClean(&VisualWorld);
			GameClient()->m_PredictedPrevChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_PrevPredicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = VisualWorld.GetCharacterById(i))
					GameClient()->m_aClients[i].m_PrevPredicted = pChar->GetCore();
		}

		CNetObj_PlayerInput *pInputData = (CNetObj_PlayerInput *)Client()->GetInput(Tick, LocalInputConn);
		CNetObj_PlayerInput *pDummyInputData = pDummyChar ? (CNetObj_PlayerInput *)Client()->GetInput(Tick, DummyInputConn) : nullptr;
		const bool DummyFirst = pInputData && pDummyInputData && pDummyChar->GetCid() < pLocalChar->GetCid();

		pInputData = &GameClient()->m_Controls.m_aFastInput[g_Config.m_ClDummy];
		pLocalChar->m_CanMoveInFreeze = false;
		if(pDummyChar)
			pDummyChar->m_CanMoveInFreeze = false;

		if(DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);
		if(pInputData)
			pLocalChar->OnDirectInput(pInputData);
		if(pDummyInputData && !DummyFirst)
			pDummyChar->OnDirectInput(pDummyInputData);

		VisualWorld.m_GameTick = Tick;
		pLocalChar->OnPredictedInput(pInputData);
		if(pDummyInputData)
			pDummyChar->OnPredictedInput(pDummyInputData);
		VisualWorld.Tick();

		if(Tick == FinalTickSelf)
		{
			GameClient()->m_PredictedChar = pLocalChar->GetCore();
			GameClient()->m_aClients[LocalClientId].m_Predicted = pLocalChar->GetCore();
		}
		if(Tick == FinalTickOthers)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(CCharacter *pChar = VisualWorld.GetCharacterById(i))
					GameClient()->m_aClients[i].m_Predicted = pChar->GetCore();
		}

		for(int i = 0; i < MAX_CLIENTS; i++)
			if(CCharacter *pChar = VisualWorld.GetCharacterById(i))
			{
				GameClient()->m_aClients[i].m_aPredPos[Tick % 200] = pChar->Core()->m_Pos;
				GameClient()->m_aClients[i].m_aPredTick[Tick % 200] = Tick;
			}
	}

	return FinalTickSelf;
}

void CFastPractice::UpdateGhostForClientId(int ClientId, SGhostData &Ghost)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
	{
		Ghost = SGhostData{};
		return;
	}

	const CNetObj_Character &Char = GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
	Ghost.m_Valid = true;
	Ghost.m_ClientId = ClientId;
	Ghost.m_Pos = vec2(Char.m_X, Char.m_Y);
	Ghost.m_Direction = vec2(Char.m_Direction == 0 ? 1.0f : (float)Char.m_Direction, 0.0f);
	Ghost.m_Angle = Char.m_Angle;
	Ghost.m_Weapon = Char.m_Weapon;
	Ghost.m_HookState = Char.m_HookState;
	Ghost.m_HookPos = vec2(Char.m_HookX, Char.m_HookY);
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

	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
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
		if(CCharacter *pPracticeChar = m_PracticeBaseWorld.GetCharacterById(Ghost.m_ClientId))
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
	str_format_v(aBody, sizeof(aBody), pFormat, Args);
	va_end(Args);

	char aMsg[320];
	str_format(aMsg, sizeof(aMsg), "[practice local] %s", aBody);
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

bool CFastPractice::FindNearestSafeRescuePosition(int ClientId, const vec2 &From, vec2 &OutPos) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;

	const auto &State = m_aPracticeCommandState[ClientId];
	float BestDistance = std::numeric_limits<float>::max();
	bool Found = false;

	for(const vec2 &Pos : State.m_vSafePositions)
	{
		if(!IsSafeRescuePosition(Pos, CCharacterCore::PhysicalSize()))
			continue;

		const float Dist = distance(From, Pos);
		if(Dist < BestDistance)
		{
			BestDistance = Dist;
			OutPos = Pos;
			Found = true;
		}
	}

	return Found;
}

bool CFastPractice::ExecutePracticeCommand(int Team, int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs, bool &WeaponsMutated)
{
	(void)Team;
	WeaponsMutated = false;
	if(vArgs.empty() || vArgs[0].size() < 2 || vArgs[0][0] != '/')
		return false;

	std::string Cmd = vArgs[0].substr(1);
	std::transform(Cmd.begin(), Cmd.end(), Cmd.begin(), [](unsigned char c) { return (char)std::tolower(c); });

	auto &State = m_aPracticeCommandState[LocalClientId];
	const vec2 CurrentPos = pChar->Core()->m_Pos;
	const auto SaveTeleportPos = [&](const vec2 &Pos) {
		StoreLastTeleport(LocalClientId, Pos);
		StoreLastDeathPosition(LocalClientId, CurrentPos);
	};
	const auto ApplyTeleport = [&](const vec2 &Pos) {
		SaveTeleportPos(Pos);
		TeleportCharacter(pChar, Pos);
	};
	const auto GiveWeapon = [&](int Weapon, bool Remove) {
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
	};
	const auto EnsureActiveWeaponIsValid = [&]() {
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
	};
	const auto ToggleHit = [&](int Weapon) {
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
	};
	const auto ClampToPlayableBounds = [&](const vec2 &Pos) {
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
	};

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

	if(Cmd == "rescuemode")
	{
		if(vArgs.size() <= 1)
		{
			EchoPractice("rescue mode: %s", State.m_RescueManual ? "manual" : "auto");
			return true;
		}
		std::string Arg = vArgs[1];
		std::transform(Arg.begin(), Arg.end(), Arg.begin(), [](unsigned char c) { return (char)std::tolower(c); });
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

	if(Cmd == "r" || Cmd == "rescue")
	{
		const bool Frozen = IsFrozenState(pChar);
		if(State.m_RescueManual)
		{
			if(pChar->IsGrounded() && !Frozen && IsSafeRescuePosition(CurrentPos, pChar->GetProximityRadius()))
			{
				State.m_HasRescueManual = true;
				State.m_RescueManualPos = pChar->Core()->m_Pos;
				EchoPractice("manual rescue point saved");
				TrackSafeRescuePosition(LocalClientId, pChar);
			}
			else if(State.m_HasRescueManual && IsSafeRescuePosition(State.m_RescueManualPos, pChar->GetProximityRadius()))
			{
				ApplyTeleport(State.m_RescueManualPos);
			}
			else
			{
				EchoPractice("can't set manual rescue while not grounded");
			}
			return true;
		}

		if(State.m_HasRescueAuto && IsSafeRescuePosition(State.m_RescueAutoPos, pChar->GetProximityRadius()))
		{
			ApplyTeleport(State.m_RescueAutoPos);
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

	if(Cmd == "back")
	{
		if(!State.m_HasLastDeath)
		{
			EchoPractice("there is nowhere to go back to");
			return true;
		}
		ApplyTeleport(State.m_LastDeathPos);
		return true;
	}

	if(Cmd == "lasttp")
	{
		if(!State.m_HasLastTeleport)
		{
			EchoPractice("you haven't previously teleported");
			return true;
		}
		ApplyTeleport(State.m_LastTeleportPos);
		return true;
	}

	if(Cmd == "tp" || Cmd == "teleport" || Cmd == "tc" || Cmd == "telecursor")
	{
		vec2 Target = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy];
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
		ApplyTeleport(ClampToPlayableBounds(Target));
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

		ApplyTeleport(ClampToPlayableBounds(vec2(X, Y)));
		return true;
	}

	if(Cmd == "totele" || Cmd == "totelecp")
	{
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

		ApplyTeleport(vTeleOuts[0]);
		return true;
	}

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

	if(Cmd == "shotgun" || Cmd == "grenade" || Cmd == "laser" || Cmd == "rifle" || Cmd == "unshotgun" || Cmd == "ungrenade" || Cmd == "unlaser" || Cmd == "unrifle" || Cmd == "weapons" || Cmd == "unweapons")
	{
		if(Cmd == "shotgun")
			GiveWeapon(WEAPON_SHOTGUN, false);
		else if(Cmd == "grenade")
			GiveWeapon(WEAPON_GRENADE, false);
		else if(Cmd == "laser" || Cmd == "rifle")
			GiveWeapon(WEAPON_LASER, false);
		else if(Cmd == "unshotgun")
			GiveWeapon(WEAPON_SHOTGUN, true);
		else if(Cmd == "ungrenade")
			GiveWeapon(WEAPON_GRENADE, true);
		else if(Cmd == "unlaser" || Cmd == "unrifle")
			GiveWeapon(WEAPON_LASER, true);
		else if(Cmd == "weapons")
			GiveWeapon(-1, false);
		else if(Cmd == "unweapons")
			GiveWeapon(-1, true);
		EnsureActiveWeaponIsValid();
		WeaponsMutated = true;
		return true;
	}

	if(Cmd == "addweapon" || Cmd == "removeweapon")
	{
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
		GiveWeapon(WeaponId, Cmd == "removeweapon");
		EnsureActiveWeaponIsValid();
		WeaponsMutated = true;
		return true;
	}

	if(Cmd == "jetpack" || Cmd == "unjetpack" || Cmd == "infjump" || Cmd == "uninfjump" || Cmd == "endless" || Cmd == "unendless" || Cmd == "setjumps")
	{
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

	if(Cmd == "ninja" || Cmd == "unninja")
	{
		if(Cmd == "ninja")
			pChar->GiveNinja();
		else
			pChar->RemoveNinja();
		return true;
	}

	if(Cmd == "invincible" || Cmd == "invisbl" || Cmd == "invinsbl")
	{
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

	if(Cmd == "collision")
	{
		CCharacterCore Core = pChar->GetCore();
		Core.m_CollisionDisabled = !Core.m_CollisionDisabled;
		pChar->SetCore(Core);
		return true;
	}

	if(Cmd == "hookcollision")
	{
		CCharacterCore Core = pChar->GetCore();
		Core.m_HookHitDisabled = !Core.m_HookHitDisabled;
		pChar->SetCore(Core);
		return true;
	}

	if(Cmd == "hitothers")
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

		std::string Arg = vArgs[1];
		std::transform(Arg.begin(), Arg.end(), Arg.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		if(Arg == "hammer")
			ToggleHit(WEAPON_HAMMER);
		else if(Arg == "shotgun")
			ToggleHit(WEAPON_SHOTGUN);
		else if(Arg == "grenade")
			ToggleHit(WEAPON_GRENADE);
		else if(Arg == "laser")
			ToggleHit(WEAPON_LASER);
		else
			EchoPractice("unknown argument for /hitothers");
		return true;
	}

	return false;
}

bool CFastPractice::ConsumePracticeChatCommand(int Team, const char *pLine)
{
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

	CCharacter *pBaseChar = m_PracticeBaseWorld.GetCharacterById(LocalClientId);
	if(!pBaseChar)
	{
		if(!InitPracticeWorld())
		{
			Disable();
			return true;
		}
		pBaseChar = m_PracticeBaseWorld.GetCharacterById(LocalClientId);
		if(!pBaseChar)
		{
			Disable();
			return true;
		}
	}

	bool WeaponsMutated = false;
	const bool Consumed = ExecutePracticeCommand(Team, LocalClientId, pBaseChar, vArgs, WeaponsMutated);
	if(!Consumed)
		return false;
	if(!m_Enabled)
		return true;
	m_SuppressFireOnNextPredictTick = true;
	m_InputSuppressTicks = std::max(m_InputSuppressTicks, 2);

	if(WeaponsMutated)
	{
		NormalizeWeaponSelectionInput(pBaseChar);
		if(m_RequireDummy && DummyClientId >= 0)
			NormalizeWeaponSelectionInput(m_PracticeBaseWorld.GetCharacterById(DummyClientId));
	}

	GameClient()->m_PredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	GameClient()->m_PrevPredictedWorld.CopyWorldClean(&m_PracticeBaseWorld);
	if(CCharacter *pPredChar = GameClient()->m_PredictedWorld.GetCharacterById(LocalClientId))
	{
		GameClient()->m_PredictedChar = pPredChar->GetCore();
		GameClient()->m_PredictedPrevChar = pPredChar->GetCore();
		GameClient()->m_aClients[LocalClientId].m_Predicted = pPredChar->GetCore();
		GameClient()->m_aClients[LocalClientId].m_PrevPredicted = pPredChar->GetCore();
	}
	if(m_RequireDummy && DummyClientId >= 0)
	{
		if(CCharacter *pPredDummy = GameClient()->m_PredictedWorld.GetCharacterById(DummyClientId))
		{
			GameClient()->m_aClients[DummyClientId].m_Predicted = pPredDummy->GetCore();
			GameClient()->m_aClients[DummyClientId].m_PrevPredicted = pPredDummy->GetCore();
		}
	}
	GameClient()->m_PredictedTick = m_PracticeBaseWorld.GameTick();

	if(m_RequireDummy && DummyClientId >= 0 && !m_PracticeBaseWorld.GetCharacterById(DummyClientId))
	{
		Disable();
		return true;
	}

	GameClient()->m_PredictedDummyId = CurrentPracticeDummyId();
	ResetAttackTickHistory();
	return true;
}

void CFastPractice::OnConsoleInit()
{
	Console()->Register("fast_practice_toggle", "", CFGFLAG_CLIENT, ConFastPracticeToggle, this, "Toggle fast practice mode");
}
