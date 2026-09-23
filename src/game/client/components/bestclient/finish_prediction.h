/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_FINISH_PREDICTION_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_FINISH_PREDICTION_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <vector>

class CFinishPrediction : public CComponent
{
public:
	struct SFinishPredictionState
	{
		bool m_Valid = false;
		bool m_HasPredictedTime = false;
		bool m_ShowPlayerName = false;
		float m_Progress = 0.0f;
		int64_t m_CurrentTimeMs = 0;
		int64_t m_PredictedFinishTimeMs = 0;
		int64_t m_RemainingTimeMs = 0;
		char m_aPlayerName[MAX_NAME_LENGTH] = "";
	};

	int Sizeof() const override { return sizeof(*this); }

	CUIRect GetHudEditorRect() const { return GetRect(true); }
	void RenderPreview() { Render(true); }

	void OnReset() override;
	void OnRender() override;
	void OnNewSnapshot() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void RequestPathRebuild();

private:
	CUIRect GetRect(bool ForcePreview) const;
	void Render(bool ForcePreview);

	bool RebuildPathData();
	bool EnsurePathData();
	void MarkJumpableFreezeWalls();
	bool IsFreezeTileIndex(int Index) const;
	float GetDistanceAtPos(vec2 Pos) const;
	float GetStartDistance() const;
	float GetMapSpanDistance() const;
	int GetTrackedClientId() const;
	int GetGameInfoRaceStartTick() const;
	int64_t GetScoreboardTimeMs(int ClientId) const;
	int64_t GetBestTimeMs() const;
	int64_t GetPersonalBestTimeMs(int ClientId) const;
	int64_t GetAverageTimeMs() const;
	bool GetState(SFinishPredictionState &State, bool ForcePreview);
	void ResetState(bool ClearFinishedRace = true);

	std::vector<int> m_vDistances;
	std::vector<unsigned char> m_vPassable;
	std::vector<unsigned char> m_vIsFreeze;
	std::vector<unsigned char> m_vFreezeJumpable;
	std::vector<ivec2> m_vStartTiles;
	std::vector<ivec2> m_vFinishTiles;
	int m_MapWidth = 0;
	int m_MapHeight = 0;
	bool m_PathReady = false;
	bool m_PathBuildAttempted = false;
	bool m_PathBuiltWithFreezeWalls = false;
	bool m_PathRebuildPending = false;
	int m_TrackedClientId = -1;
	int m_RaceStartTick = -1;
	float m_RaceStartDistance = -1.0f;
	float m_LastProgress = 0.0f;
	int64_t m_SmoothedFinishTimeMs = -1;
	int m_LastPredictTick = -1;
	int m_FinishedRaceTick = -1;
	bool m_UsingFastPractice = false;

	bool m_CachedStateValid = false;
	bool m_CachedForcePreview = false;
	int m_CachedStateTick = -1;
	SFinishPredictionState m_CachedState;
};

#endif
