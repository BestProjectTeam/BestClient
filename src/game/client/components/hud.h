/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_HUD_H
#define GAME_CLIENT_COMPONENTS_HUD_H
#include <base/vmath.h>

#include <engine/client.h>
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

class CHud : public CComponent
{
	float m_Width, m_Height;

	int m_HudQuadContainerIndex;
	SScoreInfo m_aScoreInfo[2];
	STextContainerIndex m_FPSTextContainerIndex;
	STextContainerIndex m_DDRaceEffectsTextContainerIndex;
	STextContainerIndex m_PlayerAngleTextContainerIndex;
	float m_PlayerPrevAngle;
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

	void RenderCursor();

	void RenderTextInfo();
	void RenderConnectionWarning();
	void RenderTeambalanceWarning();

	void PrepareAmmoHealthAndArmorQuads();
	void RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter);

	void PreparePlayerStateQuads();
	void RenderPlayerState(int ClientId);

	int m_LastSpectatorCountTick;
	void RenderSpectatorCount(bool ForcePreview = false);
	void RenderDummyActions();
	void RenderMovementInformation(bool ForcePreview = false);

	void UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, float Value, float &PrevValue);
	void RenderMovementInformationTextContainer(STextContainerIndex &TextContainer, const ColorRGBA &Color, float X, float Y);

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
		bool m_ShowDummyCoordIndicator = false;
		bool m_HasDummyInfo = false;
		bool m_ShowPosition = false;
		bool m_ShowSpeed = false;
		bool m_ShowAngle = false;
		bool m_ShowDummyPos = false;
		bool m_ShowDummySpeed = false;
		bool m_ShowDummyAngle = false;
		CMovementInformation m_Info;
		CMovementInformation m_DummyInfo;
	};
	class CMovementInformation GetMovementInformation(int ClientId, int Conn) const;
	bool HasPlayerBelowOnSameX(int ClientId, const CMovementInformation &Info) const;
	bool GetMovementInformationState(SMovementInformationState &State, bool ForcePreview) const;
	float GetMovementInformationBoxHeight(const SMovementInformationState &State, float Scale) const;
	CUIRect GetMovementInformationRect(bool ForcePreview) const;
	class SSpectatorCountState
	{
	public:
		int m_Count = 0;
		char m_aCountBuf[16] = {};
		char m_aaNameLines[6][MAX_NAME_LENGTH + 8] = {};
		int m_NumNameLines = 0;
	};
	bool GetSpectatorCountState(SSpectatorCountState &State, bool ForcePreview);
	CUIRect GetSpectatorCountRect(bool ForcePreview);

	void RenderGameTimer();
	void RenderPauseNotification();
	void RenderSuddenDeath();

	void RenderScoreHud(bool ForcePreview = false);
	CUIRect GetScoreHudRect(bool ForcePreview) const;
	int m_LastLocalClientId = -1;

	void RenderSpectatorHud();
	void RenderWarmupTimer();
	void RenderLocalTime(bool ForcePreview = false);
	CUIRect GetLocalTimeRect(bool ForcePreview) const;
	void RenderFinishPrediction(bool ForcePreview = false);
	CUIRect GetFinishPredictionRect(bool ForcePreview) const;
	void RenderFrozenHud(bool ForcePreview = false);
	CUIRect GetFrozenHudRect(bool ForcePreview) const;

	static constexpr float MOVEMENT_INFORMATION_LINE_HEIGHT = 8.0f;

public:
	CHud();
	int Sizeof() const override { return sizeof(*this); }

	void ResetHudContainers();
	void OnWindowResize() override;
	void OnReset() override;
	void OnRender() override;
	void OnInit() override;
	void OnNewSnapshot() override;
	CUIRect GetScoreHudEditorRect() const;
	void RenderScoreHudPreview();
	CUIRect GetSpectatorCountHudEditorRect();
	void RenderSpectatorCountPreview();
	CUIRect GetMovementInformationHudEditorRect() const;
	void RenderMovementInformationPreview();
	CUIRect GetLocalTimeHudEditorRect() const;
	void RenderLocalTimePreview();
	CUIRect GetFinishPredictionHudEditorRect() const;
	void RenderFinishPredictionPreview();
	CUIRect GetFrozenHudEditorRect() const;
	void RenderFrozenHudPreview();

	// DDRace

	void OnMessage(int MsgType, void *pRawMsg) override;
	void RenderNinjaBarPos(float x, float y, float Width, float Height, float Progress, float Alpha = 1.0f);

private:
	void RenderRecord();
	void RenderDDRaceEffects();
	void RenderSpeedrunTimer();
	struct SFinishPredictionState
	{
		bool m_Valid = false;
		bool m_HasPredictedTime = false;
		float m_Progress = 0.0f;
		int64_t m_CurrentTimeMs = 0;
		int64_t m_PredictedFinishTimeMs = 0;
		int64_t m_RemainingTimeMs = 0;
	};
	bool RebuildFinishPredictionPathData();
	bool EnsureFinishPredictionPathData();
	float GetFinishPredictionDistanceAtPos(vec2 Pos) const;
	float GetFinishPredictionStartDistance() const;
	int64_t GetFinishPredictionScoreboardTimeMs(int ClientId) const;
	int64_t GetFinishPredictionBestTimeMs() const;
	int64_t GetFinishPredictionPersonalBestTimeMs() const;
	int64_t GetFinishPredictionAverageTimeMs() const;
	bool GetFinishPredictionState(SFinishPredictionState &State, bool ForcePreview) const;
	float m_TimeCpDiff;
	float m_aPlayerRecord[NUM_DUMMIES];
	float m_FinishTimeDiff;
	int m_DDRaceTime;
	int m_FinishTimeLastReceivedTick;
	int m_TimeCpLastReceivedTick;
	int m_SpeedrunTimerExpiredTick;
	bool m_ShowFinishTime;
	mutable std::vector<int> m_vFinishPredictionDistances;
	mutable std::vector<unsigned char> m_vFinishPredictionPassable;
	mutable std::vector<ivec2> m_vFinishPredictionStartTiles;
	mutable std::vector<ivec2> m_vFinishPredictionFinishTiles;
	mutable int m_FinishPredictionMapWidth;
	mutable int m_FinishPredictionMapHeight;
	mutable int m_FinishPredictionRaceStartTick;
	mutable float m_FinishPredictionRaceStartDistance;
	mutable float m_FinishPredictionLastProgress;
	mutable int64_t m_FinishPredictionSmoothedFinishTimeMs;
	mutable int m_FinishPredictionLastPredictTick;

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
