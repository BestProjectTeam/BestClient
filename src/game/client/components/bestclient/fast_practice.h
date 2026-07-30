/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_FAST_PRACTICE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_FAST_PRACTICE_H

#include <engine/console.h>
#include <engine/client/enums.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/prediction/gameworld.h>
#include <game/gamecore.h>

#include <array>
#include <deque>
#include <string>
#include <vector>

class CFastPractice : public CComponent
{
public:
	struct SLocalRaceState
	{
		vec2 m_Position = vec2(0.0f, 0.0f);
		int m_CurrentTick = -1;
		int m_StartTick = -1;
		bool m_Finished = false;
	};

	int Sizeof() const override { return sizeof(*this); }

	bool Enabled() const { return m_Enabled; }
	bool Active() const { return m_Enabled && m_Ready; }
	bool CanEnable() const;
	void Toggle();
	void Enable();
	void Disable();
	bool ConsumeKillCommand();
	bool ConsumePracticeChatCommand(int Team, const char *pLine);
	void ResetPracticeToAnchor();

	void PrepareInputForSend(int *pData, int Size, bool Dummy);
	void BuildNeutralInput(CNetObj_PlayerInput &OutInput, bool Dummy, bool UseFrozenTarget = true) const;
	void SyncFromPrediction();
	bool ForcePredictWeapons() const;
	bool ForcePredictGrenade() const;
	bool ForcePredictGunfire() const;
	bool ForcePredictPlayers() const;
	void InvalidateBufferedInputState();
	bool IsPracticeParticipant(int ClientId) const;
	int ControlledPracticeId() const;
	int PartnerPracticeId() const;
	int CurrentPracticeDummyId() const;
	bool GetLocalRaceState(SLocalRaceState &State) const;

	CGameWorld &PracticeWorld() { return m_PracticeWorld; }
	CGameWorld &PracticePrevWorld() { return m_PracticePrevWorld; }
	const CGameWorld &PracticeWorld() const { return m_PracticeWorld; }
	const CGameWorld &PracticePrevWorld() const { return m_PracticePrevWorld; }

	bool GetFastInputRenderCharacter(int ClientId, CNetObj_Character &Prev, CNetObj_Character &Cur) const;

	void OnReset() override;
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnRender() override;
	void OnConsoleInit() override;

private:
	enum
	{
		INPUT_HISTORY_SIZE = 200,
	};

	struct SStoredInput
	{
		CNetObj_PlayerInput m_Input{};
		int m_Tick = -1;
	};

	struct SGhostData
	{
		bool m_Valid = false;
		int m_ClientId = -1;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_Direction = vec2(1.0f, 0.0f);
		int m_Angle = 0;
		int m_Weapon = WEAPON_HAMMER;
		int m_HookState = HOOK_RETRACTED;
		vec2 m_HookPos = vec2(0.0f, 0.0f);
	};

	struct SAnchorData
	{
		bool m_Valid = false;
		int m_ClientId = -1;
		CNetObj_Character m_Char = {};
		CNetObj_DDNetCharacter m_DDNet = {};
		bool m_HasDDNet = false;
	};

	struct SPracticeCommandState
	{
		static constexpr int MAX_SAFE_POSITIONS = 256;
		bool m_RescueManual = false;
		bool m_HasRescueAuto = false;
		bool m_HasRescueManual = false;
		bool m_HasLastTeleport = false;
		bool m_HasLastDeath = false;
		bool m_HasPendingTeleport = false;
		vec2 m_PendingTeleportPos = vec2(0.0f, 0.0f);
		bool m_InvincibleAddedEndlessJump = false;
		vec2 m_RescueAutoPos = vec2(0.0f, 0.0f);
		vec2 m_RescueManualPos = vec2(0.0f, 0.0f);
		vec2 m_LastTeleportPos = vec2(0.0f, 0.0f);
		vec2 m_LastDeathPos = vec2(0.0f, 0.0f);
		std::deque<vec2> m_vSafePositions;
	};

	struct SPracticeRaceState
	{
		int m_StartTick = -1;
		bool m_Finished = false;
	};

	bool m_Enabled = false;
	bool m_Ready = false;
	bool m_NeedsRebuild = false;
	bool m_RequireDummy = false;
	int m_EnableLocalClientId = -1;
	int m_EnableDummyClientId = -1;
	bool m_HasDummyAnchor = false;
	bool m_SuppressFireOnNextPredictTick = false;
	int m_InputSuppressTicks = 0;
	int m_LastClDummy = 0;
	int m_LastResolvedLocalClientId = -1;
	int m_LastResolvedDummyClientId = -1;
	std::array<ivec2, NUM_DUMMIES> m_aServerLockedTargets{};
	std::array<bool, NUM_DUMMIES> m_aHasServerLockedTargets{};
	std::array<ivec2, MAX_CLIENTS> m_aFrozenTarget{};
	std::array<bool, MAX_CLIENTS> m_aFrozenTargetValid{};
	std::array<vec2, MAX_CLIENTS> m_aSpawnPos{};
	std::array<bool, MAX_CLIENTS> m_aSpawnPosValid{};
	std::array<vec2, MAX_CLIENTS> m_aSafePos{};
	std::array<bool, MAX_CLIENTS> m_aSafePosValid{};
	SStoredInput m_aaStoredInputs[NUM_DUMMIES][INPUT_HISTORY_SIZE]{};
	int m_aNextStoredInput[NUM_DUMMIES]{};
	CNetObj_Character m_aFastRenderPrev[MAX_CLIENTS]{};
	CNetObj_Character m_aFastRenderCur[MAX_CLIENTS]{};
	std::array<bool, MAX_CLIENTS> m_aFastRenderValid{};
	std::array<int, MAX_CLIENTS> m_aLastEventTick{};
	// Cached practice-world cores. The regular prediction in CGameClient::OnPredict overwrites
	// m_aClients[].m_Predicted on every repredict, so the practice state has to be re-published
	// every frame, not only on frames where the practice world actually advanced a tick.
	std::array<CCharacterCore, MAX_CLIENTS> m_aPublishedPredicted{};
	std::array<CCharacterCore, MAX_CLIENTS> m_aPublishedPrevPredicted{};
	std::array<CCharacterCore, MAX_CLIENTS> m_aPublishedRegularPredicted{};
	std::array<bool, MAX_CLIENTS> m_aPublishedValid{};

	SGhostData m_MainGhost;
	SGhostData m_DummyGhost;
	SAnchorData m_MainAnchor;
	SAnchorData m_DummyAnchor;
	std::array<int, MAX_CLIENTS> m_aLastAttackTick{};
	std::array<SPracticeCommandState, MAX_CLIENTS> m_aPracticeCommandState{};
	std::array<SPracticeRaceState, MAX_CLIENTS> m_aPracticeRaceState{};

	CGameWorld m_PracticeWorld;
	CGameWorld m_PracticePrevWorld;

	static void ConFastPracticeToggle(IConsole::IResult *pResult, void *pUserData);

	void ResetPracticeState();
	void ResetCommandState();
	void ResetPracticeRaceStates();
	void UpdatePracticeRaceState(int ClientId, const CCharacter *pChar, int Tick);
	void SyncPracticeWorldConfig(CGameWorld &World);
	bool ResolvePracticeRoles(int &LocalClientId, int &DummyClientId) const;
	int CurrentLocalPracticeId() const;
	void UpdateGhostData();
	void UpdateGhostForClientId(int ClientId, SGhostData &Ghost);
	void CaptureAnchorsFromSnapshot();
	bool ApplyAnchorToCharacter(CGameWorld &World, const SAnchorData &Anchor) const;
	bool Rebuild();
	void PrunePracticeWorld(CGameWorld &World) const;
	void ResetAttackTickHistory();
	void TrackFireSound(int ClientId, CCharacter *pChar);
	static int WeaponFireSound(int Weapon);
	void MaybePlayHammerHitEffect(CCharacter *pChar);
	void RenderGhost(const SGhostData &Ghost, float Alpha) const;
	void ReleaseBufferedInputState();
	void CaptureServerLockedTargets();
	void CaptureFrozenTargets();
	void ResetStoredInputs();
	void StoreNeutralInput(bool Dummy, int Tick);
	void StoreInput(const CNetObj_PlayerInput &Input, bool Dummy);
	const CNetObj_PlayerInput *GetStoredInput(int Tick, bool Dummy) const;
	void BuildLiveInput(CNetObj_PlayerInput &OutInput, bool Dummy) const;
	void TickPracticeWorld();
	void StorePredictionState(int Tick, int FinalTickRegular, int FinalTickOthers, int FinalTickSelf);
	void SeedPredictionHistory();
	void CachePredictedCore(int ClientId, const CCharacterCore &Core);
	void CachePrevPredictedCore(int ClientId, const CCharacterCore &Core);
	void CacheRegularPredictedCore(int ClientId, const CCharacterCore &Core);
	void RepublishCachedCores() const;
	void FinishMutation(int LocalClientId, int DummyClientId, CCharacter *pChar, bool WeaponsMutated);
	void FillRenderCharacter(CCharacter *pChar, CNetObj_Character &Out) const;
	bool IsDeathTile(const CCharacter *pChar) const;
	void ResetCharacterToSaved(int ClientId, CCharacter *pChar, int Tick);
	void PlayCoreEvents(CCharacter *pChar, int Tick);
	void PublishParticipantCores(int LocalClientId, int DummyClientId);

	[[gnu::format(printf, 2, 3)]]
	void EchoPractice(const char *pFormat, ...) const;
	static bool ParseCommandArgs(const char *pLine, std::vector<std::string> &vArgs);
	static bool ParseCoordinateToken(const char *pToken, float Base, float &Out);
	int FindClientByName(const char *pName) const;
	void NormalizeCharacterAfterReset(CCharacter *pChar, bool KeepFreezeFlags) const;
	void NormalizeWeaponSelectionInput(CCharacter *pChar) const;
	void TeleportCharacter(CCharacter *pChar, const vec2 &Pos) const;
	void SaveTeleportHistory(int ClientId, const vec2 &CurrentPos, const vec2 &TargetPos);
	void ApplyPracticeTeleport(int ClientId, CCharacter *pChar, const vec2 &TargetPos);
	void GivePracticeWeapon(CCharacter *pChar, int Weapon, bool Remove) const;
	void EnsureActiveWeaponIsValid(CCharacter *pChar) const;
	void TogglePracticeHit(CCharacter *pChar, int Weapon) const;
	vec2 ClampToPracticePlayableBounds(const vec2 &Pos) const;
	void StoreLastTeleport(int ClientId, const vec2 &Pos);
	void StoreLastDeathPosition(int ClientId, const vec2 &Pos);
	bool IsSafeRescueTile(int Tile) const;
	bool IsSafeRescuePosition(const vec2 &Pos, float ProximityRadius) const;
	void TrackSafeRescuePosition(int ClientId, CCharacter *pChar);
	bool ExecutePracticeMetaCommand(const std::string &Cmd);
	bool ExecutePracticeRescueCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs);
	bool ExecutePracticeTeleportCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs);
	bool ExecutePracticeStateCommand(CCharacter *pChar, const std::string &Cmd);
	bool ExecutePracticeWeaponCommand(CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs, bool &WeaponsMutated);
	bool ExecutePracticeMovementCommand(int LocalClientId, CCharacter *pChar, const std::string &Cmd, const std::vector<std::string> &vArgs);
	bool ExecutePracticeInvincibleCommand(int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs);
	bool ExecutePracticeCollisionCommand(CCharacter *pChar, const std::string &Cmd);
	bool ExecutePracticeHitOthersCommand(CCharacter *pChar, const std::vector<std::string> &vArgs);
	bool ExecutePracticeCommand(int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs, bool &WeaponsMutated);
};

#endif
