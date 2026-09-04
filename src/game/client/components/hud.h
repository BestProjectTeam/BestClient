/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_HUD_H
#define GAME_CLIENT_COMPONENTS_HUD_H
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/storage.h>
#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <generated/protocol.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <cstdint>
#include <vector>

struct SScoreInfo
{
	SScoreInfo()
	{
		Reset();
	}

	void Reset()
	{
		m_TextRankContainerIndex.Reset();
		m_TextScoreContainerIndex.Reset();
		m_RoundRectQuadContainerIndex = -1;
		m_OptionalNameTextContainerIndex.Reset();
		m_aScoreText[0] = 0;
		m_aRankText[0] = 0;
		m_aPlayerNameText[0] = 0;
		m_ScoreTextWidth = 0.f;
		m_Initialized = false;
	}

	STextContainerIndex m_TextRankContainerIndex;
	STextContainerIndex m_TextScoreContainerIndex;
	float m_ScoreTextWidth;
	char m_aScoreText[16];
	char m_aRankText[16];
	char m_aPlayerNameText[MAX_NAME_LENGTH];
	int m_RoundRectQuadContainerIndex;
	STextContainerIndex m_OptionalNameTextContainerIndex;

	bool m_Initialized;
};

// Width (in HUD-pixel space, i.e. already scaled by the keystrokes atlas scale) that the
// keyboard keystrokes overlay occupies for a given bc_keystrokes_keyboard_preset value.
// Exposed so HudLayout can offset the mouse module's default position past the keyboard
// without drifting into it when the keyboard preset changes.
float GetKeystrokesKeyboardPresetWidthHudPx(int Preset);

class CHud : public CComponent
{
	struct SCursorTrailPoint
	{
		vec2 m_Pos;
		float m_Age;
	};
	float m_Width, m_Height;

	int m_HudQuadContainerIndex;
	IGraphics::CTextureHandle m_CursorTrailTexture;
	char m_aCursorTrailPath[IO_MAX_PATH_LENGTH] = {};
	std::vector<SCursorTrailPoint> m_vCursorTrail;
	int m_CursorTrailMode = -1;
	int m_CursorTrailFrames = -1;
	int m_CursorTrailDisableMovement = -1;
	float m_CursorTrailSampleTime = 0.0f;
	vec2 m_CursorTrailPreviousPlayerPos;
	bool m_CursorTrailAnchorValid = false;
	SScoreInfo m_aScoreInfo[2];
	float m_LastScoreHudLayoutX = 0.0f;
	float m_LastScoreHudLayoutY = 0.0f;
	STextContainerIndex m_FPSTextContainerIndex;
	STextContainerIndex m_DDRaceEffectsTextContainerIndex;
	STextContainerIndex m_PlayerAngleTextContainerIndex;
	float m_PlayerPrevAngle;
	float m_LastMovementInformationFontSize = -1.0f;
	STextContainerIndex m_aPlayerSpeedTextContainers[2];
	float m_aPlayerPrevSpeed[2];
	int m_aPlayerSpeed[2];
	enum class ESpeedChange
	{
		NONE,
		INCREASE,
		DECREASE
	};
	ESpeedChange m_aLastPlayerSpeedChange[2];
	STextContainerIndex m_aPlayerPositionContainers[2];
	float m_aPlayerPrevPosition[2];
	STextContainerIndex m_PlayerCheckpointTextContainerIndex;
	int m_PlayerPrevCheckpoint = -1;

	void RenderCursor();

	void RenderTextInfo();
	void RenderConnectionWarning();
	void RenderTeambalanceWarning();

	void PrepareAmmoHealthAndArmorQuads();
	void RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter);

	void PreparePlayerStateQuads();
	void RenderPlayerState(int ClientId);

	int m_LastSpectatorCountTick;
	class SSpectatorCountState
	{
	public:
		int m_Count = 0;
		char m_aCountBuf[16] = {};
		char m_aaNameLines[6][MAX_NAME_LENGTH + 8] = {};
		int m_NumNameLines = 0;
	};
	bool GetSpectatorCountState(SSpectatorCountState &State, bool ForcePreview);
	CUIRect GetSpectatorCountRect(bool ForcePreview = false);
	void RenderSpectatorCount(bool ForcePreview = false);
	CUIRect GetDummyActionsRect(bool ForcePreview = false) const;
	void RenderDummyActions(bool ForcePreview = false);
	CUIRect GetMovementInformationRect(bool ForcePreview = false) const;
	void RenderMovementInformation(bool ForcePreview = false);

	void UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, float Value, float &PrevValue);
	void UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, int Value, int &PrevValue);
	void RenderMovementInformationTextContainer(STextContainerIndex &TextContainer, const ColorRGBA &Color, float X, float Y);
	int GetCheckpointId() const;

	class CMovementInformation
	{
	public:
		vec2 m_Pos;
		vec2 m_Speed;
		float m_Angle = 0.0f;
	};
	class SMovementInformationState
	{
	public:
		int m_ClientId = -1;
		bool m_HasValidClientId = false;
		bool m_PosOnly = false;
		bool m_HasDummyInfo = false;
		bool m_ShowPosition = false;
		bool m_ShowCheckpoint = false;
		bool m_ShowSpeed = false;
		bool m_ShowAngle = false;
		bool m_ShowDummyPos = false;
		bool m_ShowDummySpeed = false;
		bool m_ShowDummyAngle = false;
		int m_Checkpoint = -1;
		CMovementInformation m_Info;
		CMovementInformation m_DummyInfo;
	};
	class CMovementInformation GetMovementInformation(int ClientId, int Conn) const;
	bool HasPlayerBelowOnSameX(int ClientId, const CMovementInformation &Info) const;
	bool GetMovementInformationState(SMovementInformationState &State, bool ForcePreview) const;
	float GetMovementInformationBoxHeight(const SMovementInformationState &State, float Scale) const;
	void RenderPlayerBelowIndicator();
	float m_PlayerBelowIndicatorPhase = 0.0f;

	void RenderGameTimer();
	void RenderPauseNotification();
	void RenderSuddenDeath();

	CUIRect GetScoreHudRect(bool ForcePreview = false) const;
	void RenderScoreHud(bool ForcePreview = false);
	int m_LastLocalClientId = -1;

	void RenderSpectatorHud();
	void RenderWarmupTimer();
	CUIRect GetLocalTimeRect(bool ForcePreview = false) const;
	void RenderLocalTime(bool ForcePreview = false);
	void RenderSpeedrunTimer();
	int m_SpeedrunTimerExpiredTick;

	CUIRect GetFinishPredictionRect(bool ForcePreview = false) const;
	void RenderFinishPrediction(bool ForcePreview = false);

	void GetFrozenTeamCounts(int &NumInTeam, int &NumFrozen, int &LocalTeamId, int *pNumUnfreezing = nullptr) const;
	CUIRect GetFrozenHudRect(bool ForcePreview = false) const;
	void RenderFrozenHud(bool ForcePreview = false);

	CUIRect GetNotifyLastRect(bool ForcePreview = false) const;
	void RenderNotifyLast(bool ForcePreview = false);

	CUIRect GetKeystrokesKeyboardRectInternal(bool IgnoreModuleEnabled) const;
	void RenderKeystrokesKeyboardInternal(bool ForcePreview, bool IgnoreModuleEnabled);
	CUIRect GetKeystrokesMouseRectInternal(bool IgnoreModuleEnabled) const;
	void RenderKeystrokesMouseInternal(bool ForcePreview, bool IgnoreModuleEnabled);
	void RenderKeystrokesKeyboard(bool ForcePreview = false) { RenderKeystrokesKeyboardInternal(ForcePreview, false); }
	void RenderKeystrokesMouse(bool ForcePreview = false) { RenderKeystrokesMouseInternal(ForcePreview, false); }
	int GetKeystrokesTrackedClientId() const;
	const CNetObj_PlayerInput *GetKeystrokesTrackedInput() const;
	IGraphics::CTextureHandle m_KeystrokesKeyboardTexture;
	IGraphics::CTextureHandle m_KeystrokesMouseTexture;
	int64_t m_KeystrokesMouse1EndTime = 0;
	int64_t m_KeystrokesWheelUpEndTime = 0;
	int64_t m_KeystrokesWheelDownEndTime = 0;

	static constexpr float MOVEMENT_INFORMATION_LINE_HEIGHT = 8.0f;

public:
	void ReloadCursorTrail();
	// HUD editor integration (bestclient/hud_editor.h): rect getters return the
	// current on-screen box for a module so the editor can draw a drag handle over
	// it, and the *Preview variants render the module with placeholder data so it
	// stays visible in the editor even without a live match to pull real data from.
	CUIRect GetScoreHudEditorRect() const { return GetScoreHudRect(true); }
	void RenderScoreHudPreview() { RenderScoreHud(true); }
	CUIRect GetSpectatorCountHudEditorRect() { return GetSpectatorCountRect(true); }
	void RenderSpectatorCountPreview() { RenderSpectatorCount(true); }
	CUIRect GetDummyActionsHudEditorRect() const { return GetDummyActionsRect(true); }
	void RenderDummyActionsPreview() { RenderDummyActions(true); }
	CUIRect GetMovementInformationHudEditorRect() { return GetMovementInformationRect(true); }
	void RenderMovementInformationPreview() { RenderMovementInformation(true); }
	CUIRect GetLocalTimeHudEditorRect() const { return GetLocalTimeRect(true); }
	void RenderLocalTimePreview() { RenderLocalTime(true); }
	CUIRect GetFinishPredictionHudEditorRect() const { return GetFinishPredictionRect(true); }
	void RenderFinishPredictionPreview() { RenderFinishPrediction(true); }
	CUIRect GetFrozenHudEditorRect() const { return GetFrozenHudRect(true); }
	void RenderFrozenHudPreview() { RenderFrozenHud(true); }
	CUIRect GetNotifyLastHudEditorRect() const { return GetNotifyLastRect(true); }
	void RenderNotifyLastPreview() { RenderNotifyLast(true); }
	CUIRect GetKeystrokesKeyboardHudEditorRect() const;
	void RenderKeystrokesKeyboardPreview() { RenderKeystrokesKeyboardInternal(true, true); }
	CUIRect GetKeystrokesMouseHudEditorRect() const { return GetKeystrokesMouseRectInternal(true); }
	void RenderKeystrokesMousePreview() { RenderKeystrokesMouseInternal(true, true); }

	CHud();
	int Sizeof() const override { return sizeof(*this); }

	void ResetHudContainers();
	void OnWindowResize() override;
	void OnReset() override;
	void OnRender() override;
	void OnInit() override;
	void OnNewSnapshot() override;

	// DDRace

	void OnMessage(int MsgType, void *pRawMsg) override;
	void RenderNinjaBarPos(float x, float y, float Width, float Height, float Progress, float Alpha = 1.0f);
	void ShowTimeCpDiff(float Diff);
	void ShowSelfTimeCpDiff(float Diff);

private:
	void RenderRecord();
	void RenderDDRaceEffects();
	float m_TimeCpDiff;
	float m_aPlayerRecord[NUM_DUMMIES];
	float m_FinishTimeDiff;
	int m_DDRaceTime;
	int m_FinishTimeLastReceivedTick;
	int m_TimeCpLastReceivedTick;
	bool m_ShowFinishTime;
	bool m_SelfTimeCpDiff = false;

	struct SFinishPredictionState
	{
		bool m_Valid = false;
		bool m_HasPredictedTime = false;
		float m_Progress = 0.0f;
		int64_t m_CurrentTimeMs = 0;
		int64_t m_PredictedFinishTimeMs = 0;
		int64_t m_RemainingTimeMs = 0;
	};
	bool RebuildFinishPredictionPathData() const;
	bool EnsureFinishPredictionPathData() const;
	float GetFinishPredictionDistanceAtPos(vec2 Pos) const;
	float GetFinishPredictionStartDistance() const;
	int64_t GetFinishPredictionScoreboardTimeMs(int ClientId) const;
	int64_t GetFinishPredictionBestTimeMs() const;
	int64_t GetFinishPredictionPersonalBestTimeMs() const;
	int64_t GetFinishPredictionAverageTimeMs() const;
	bool GetFinishPredictionState(SFinishPredictionState &State, bool ForcePreview) const;
	void ResetFinishPredictionState(bool ClearFinishedRace = true) const;
	mutable std::vector<int> m_vFinishPredictionDistances;
	mutable std::vector<unsigned char> m_vFinishPredictionPassable;
	mutable std::vector<ivec2> m_vFinishPredictionStartTiles;
	mutable std::vector<ivec2> m_vFinishPredictionFinishTiles;
	mutable int m_FinishPredictionMapWidth = 0;
	mutable int m_FinishPredictionMapHeight = 0;
	mutable int m_FinishPredictionRaceStartTick = -1;
	mutable float m_FinishPredictionRaceStartDistance = -1.0f;
	mutable float m_FinishPredictionLastProgress = 0.0f;
	mutable int64_t m_FinishPredictionSmoothedFinishTimeMs = -1;
	mutable int m_FinishPredictionLastPredictTick = -1;
	mutable int m_FinishPredictionFinishedRaceTick = -1;
	mutable bool m_FinishPredictionUsingFastPractice = false;

	inline int GetDigitsIndex(int Value, int Max);

	// Quad Offsets
	int m_aAmmoOffset[NUM_WEAPONS];
	int m_HealthOffset;
	int m_EmptyHealthOffset;
	int m_ArmorOffset;
	int m_EmptyArmorOffset;
	int m_aCursorOffset[NUM_WEAPONS];
	int m_FlagOffset;
	int m_AirjumpOffset;
	int m_AirjumpEmptyOffset;
	int m_aWeaponOffset[NUM_WEAPONS];
	int m_EndlessJumpOffset;
	int m_EndlessHookOffset;
	int m_JetpackOffset;
	int m_TeleportGrenadeOffset;
	int m_TeleportGunOffset;
	int m_TeleportLaserOffset;
	int m_SoloOffset;
	int m_CollisionDisabledOffset;
	int m_HookHitDisabledOffset;
	int m_HammerHitDisabledOffset;
	int m_GunHitDisabledOffset;
	int m_ShotgunHitDisabledOffset;
	int m_GrenadeHitDisabledOffset;
	int m_LaserHitDisabledOffset;
	int m_DeepFrozenOffset;
	int m_LiveFrozenOffset;
	int m_DummyHammerOffset;
	int m_DummyCopyOffset;
	int m_PracticeModeOffset;
	int m_Team0ModeOffset;
	int m_LockModeOffset;
};

#endif
