/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SELF_TIME_CP_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SELF_TIME_CP_H

#include <base/vmath.h>

#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <vector>

class CSelfTimeCp : public CComponent
{
	enum
	{
		MAX_SELF_TIME_CP = 64,
	};

	struct SCheckpoint
	{
		int m_TileX = 0;
		int m_TileYTop = 0;
		int m_TileYBottom = 0;
		float m_CurrentRunTime = -1.0f;
		float m_BestTime = -1.0f;
		int m_LinkedTimeCpIndex = -1; // real map TimeCP index, or -1
		int m_LastTouchedRaceTick = -1;
	};

	static void ConPlace(IConsole::IResult *pResult, void *pUserData);
	static void ConClear(IConsole::IResult *pResult, void *pUserData);
	static void ConUndo(IConsole::IResult *pResult, void *pUserData);

	std::vector<SCheckpoint> m_vCheckpoints;
	float m_aServerBestTimeCp[MAX_CHECKPOINTS] = {};
	bool m_aHasServerBestTimeCp[MAX_CHECKPOINTS] = {};
	vec2 m_LastPos = vec2(0.0f, 0.0f);
	bool m_HasLastPos = false;
	int m_CurrentRaceTick = -1;
	float m_BestFinishTime = -1.0f;
	float m_RaceTimeOffset = 0.0f;
	bool m_HasRaceTimeOffset = false;
	int m_LastHitTimeCpIndex = -1;

	void Clear(bool Echo);
	void UndoLast(bool Echo);
	void Place();
	void ResetCurrentRun(int RaceTick);
	void UpdateTouches();
	void CommitBestFromRun();
	void SyncBestFinishFromRecord();
	void ApplyServerBest(int TimeCpIndex, float BestTime);
	bool HasValidBest(float BestTime) const;
	bool TrySeedBestFromGhost(SCheckpoint &Checkpoint);
	bool TrySeedBestFromServer(SCheckpoint &Checkpoint);
	int FindNearestTimeCpIndex(int TileX) const;
	int DetectTimeCpHit(vec2 PrevPos, vec2 Pos) const;
	float KnownPersonalBestSeconds() const;
	float AlignedRaceTimeSeconds() const;
	bool IsGapBoundary(int TileX, int TileY) const;
	bool FindGap(int TileX, int PreferTileY, int &TileYTop, int &TileYBottom) const;
	bool IsTouchingCheckpoint(const SCheckpoint &Checkpoint, vec2 PrevPos, vec2 Pos) const;
	float CurrentRaceTimeSeconds() const;
	int CurrentRaceStartTick() const;
	vec2 PlacementPos() const;
	bool Enabled() const;
	bool CanPlace() const;

public:
	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;
	void OnReset() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_SELF_TIME_CP_H
