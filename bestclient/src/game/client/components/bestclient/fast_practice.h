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
	int Sizeof() const override { return sizeof(*this); }

	bool Enabled() const { return m_Enabled; }
	bool CanEnable() const;
	void Toggle();
	void Enable();
	void Disable();
	bool ConsumeKillCommand();
	bool ConsumePracticeChatCommand(int Team, const char *pLine);
	void ResetPracticeToAnchor();

	void PrepareInputForSend(int *pData, int Size, bool Dummy) const;
	bool OverridePredict();
	bool ForcePredictWeapons() const;
	bool ForcePredictGrenade() const;
	bool ForcePredictGunfire() const;
	bool ForcePredictPlayers() const;
	void InvalidateBufferedInputState();
	bool IsPracticeParticipant(int ClientId) const;
	int CurrentPracticeDummyId() const;
	bool IsPracticeDummy(int ClientId) const;
	int PracticeDummyId() const;

	void OnReset() override;
	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnNewSnapshot() override;
	void OnRender() override;
	void OnConsoleInit() override;

private:
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
		bool m_InvincibleAddedEndlessJump = false;
		vec2 m_RescueAutoPos = vec2(0.0f, 0.0f);
		vec2 m_RescueManualPos = vec2(0.0f, 0.0f);
		vec2 m_LastTeleportPos = vec2(0.0f, 0.0f);
		vec2 m_LastDeathPos = vec2(0.0f, 0.0f);
		std::deque<vec2> m_vSafePositions;
	};

	bool m_Enabled = false;
	bool m_RequireDummy = false;
	int m_EnableLocalClientId = -1;
	int m_EnableDummyClientId = -1;
	bool m_PracticeWorldInitialized = false;
	bool m_HasDummyAnchor = false;
	bool m_SuppressFireOnNextPredictTick = false;
	int m_InputSuppressTicks = 0;
	int m_LastResolvedLocalClientId = -1;
	int m_LastResolvedDummyClientId = -1;
	int m_LastResolvedLocalInputConn = -1;
	int m_LastResolvedDummyInputConn = -1;
	std::array<ivec2, NUM_DUMMIES> m_aServerLockedTargets{};
	std::array<bool, NUM_DUMMIES> m_aHasServerLockedTargets{};

	SGhostData m_MainGhost;
	SGhostData m_DummyGhost;
	SAnchorData m_MainAnchor;
	SAnchorData m_DummyAnchor;
	std::array<int, MAX_CLIENTS> m_aLastAttackTick{};
	std::array<SPracticeCommandState, MAX_CLIENTS> m_aPracticeCommandState{};

	CGameWorld m_PracticeBaseWorld;

	static void ConFastPracticeToggle(IConsole::IResult *pResult, void *pUserData);

	void ResetPracticeState();
	void ResetCommandState();
	void SyncPracticeWorldConfig();
	int DummyClientId() const;
	bool ResolveParticipantInputs(int LocalClientId, int DummyClientId, int &LocalInputConn, int &DummyInputConn) const;
	int CurrentLocalPracticeId() const;
	bool ResolvePracticeRoles(int &LocalClientId, int &DummyClientId) const;
	void UpdateGhostData();
	void UpdateGhostForClientId(int ClientId, SGhostData &Ghost);
	int ApplyVisualFastInputPrediction(int FinalTickRegular, int LocalClientId, int DummyClientId, int LocalInputConn, int DummyInputConn);
	void CaptureAnchorsFromSnapshot();
	bool ApplyAnchorToCharacter(CGameWorld &World, const SAnchorData &Anchor) const;
	bool InitPracticeWorld();
	void PrunePracticeWorld(CGameWorld &World) const;
	bool AdvanceBaseWorldToTick(int TargetTick, int LocalClientId, int DummyClientId);
	void ResetAttackTickHistory();
	void TrackFireSound(int ClientId, CCharacter *pChar);
	static int WeaponFireSound(int Weapon);
	void MaybePlayHammerHitEffect(CCharacter *pChar);
	void RenderGhost(const SGhostData &Ghost, float Alpha) const;
	void ReleaseBufferedInputState();

	void EchoPractice(const char *pFormat, ...) const;
	static bool ParseCommandArgs(const char *pLine, std::vector<std::string> &vArgs);
	static bool ParseCoordinateToken(const char *pToken, float Base, float &Out);
	int FindClientByName(const char *pName) const;
	void NormalizeCharacterAfterReset(CCharacter *pChar, bool KeepFreezeFlags) const;
	void NormalizeWeaponSelectionInput(CCharacter *pChar) const;
	void TeleportCharacter(CCharacter *pChar, const vec2 &Pos) const;
	void StoreLastTeleport(int ClientId, const vec2 &Pos);
	void StoreLastDeathPosition(int ClientId, const vec2 &Pos);
	void CaptureServerLockedTargets();
	bool IsSafeRescueTile(int Tile) const;
	bool IsSafeRescuePosition(const vec2 &Pos, float ProximityRadius) const;
	void TrackSafeRescuePosition(int ClientId, CCharacter *pChar);
	bool FindNearestSafeRescuePosition(int ClientId, const vec2 &From, vec2 &OutPos) const;
	bool ExecutePracticeCommand(int Team, int LocalClientId, CCharacter *pChar, const std::vector<std::string> &vArgs, bool &WeaponsMutated);
};

#endif
