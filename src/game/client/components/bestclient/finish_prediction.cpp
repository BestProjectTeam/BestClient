/* Copyright © 2026 BestProject Team */
#include "finish_prediction.h"

#include "fast_practice.h"

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <generated/protocol.h>
#include <generated/protocol7.h>

#include <game/client/components/hud.h>
#include <game/client/components/hud_layout.h>
#include <game/client/gameclient.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

namespace
{
CUIRect PushHudRectFromMusicPlayer(CGameClient *pGameClient, CUIRect Rect, float HudWidth, float HudHeight, bool ForcePreview)
{
	if(ForcePreview || pGameClient == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
		return Rect;

	const vec2 Offset = pGameClient->m_MusicPlayer.GetHudPushOffsetForRect(Rect, HudWidth, HudHeight, 2.0f);
	Rect.x += Offset.x;
	Rect.y += Offset.y;
	return Rect;
}

void FormatPredictionTime(int64_t Milliseconds, bool ShowMillis, char *pBuf, size_t BufSize)
{
	const int64_t TimeCentiseconds = std::max<int64_t>(0, (Milliseconds + 5) / 10);
	str_time(TimeCentiseconds, ShowMillis ? ETimeFormat::HOURS_CENTISECS : ETimeFormat::HOURS, pBuf, BufSize);
}

bool IsFinishPredictionHardBlocked(int GameTile, int FrontTile)
{
	const auto Hard = [](int Tile) {
		return Tile == TILE_SOLID || Tile == TILE_NOHOOK || Tile == TILE_DEATH;
	};
	return Hard(GameTile) || Hard(FrontTile);
}

bool IsFinishPredictionFreezeTile(int GameTile, int FrontTile)
{
	const auto Freeze = [](int Tile) {
		return Tile == TILE_FREEZE || Tile == TILE_DFREEZE;
	};
	return Freeze(GameTile) || Freeze(FrontTile);
}

bool IsFinishPredictionSupportTile(int GameTile, int FrontTile)
{
	const auto Support = [](int Tile) {
		return Tile == TILE_SOLID || Tile == TILE_NOHOOK ||
		       Tile == TILE_UNFREEZE || Tile == TILE_DUNFREEZE;
	};
	return Support(GameTile) || Support(FrontTile);
}

constexpr int FINISH_PREDICTION_FREEZE_JUMP_COST = 26;

CFastPractice &FinishPredictionFastPractice(CGameClient *pGameClient)
{
	return pGameClient->m_FastPractice;
}
} // namespace

void CFinishPrediction::OnReset()
{
	m_vDistances.clear();
	m_vPassable.clear();
	m_vIsFreeze.clear();
	m_vFreezeJumpable.clear();
	m_vStartTiles.clear();
	m_vFinishTiles.clear();
	m_MapWidth = 0;
	m_MapHeight = 0;
	m_PathReady = false;
	m_PathBuildAttempted = false;
	m_PathBuiltWithFreezeWalls = false;
	m_PathRebuildPending = false;
	m_TrackedClientId = -1;
	m_RaceStartTick = -1;
	m_RaceStartDistance = -1.0f;
	m_LastProgress = 0.0f;
	m_SmoothedFinishTimeMs = -1;
	m_LastPredictTick = -1;
	m_FinishedRaceTick = -1;
	m_UsingFastPractice = false;
	m_CachedStateValid = false;
	m_CachedForcePreview = false;
	m_CachedStateTick = -1;
	m_CachedState = {};
}

void CFinishPrediction::OnNewSnapshot()
{
	if(g_Config.m_BcFinishPrediction != 0)
		EnsurePathData();
}

void CFinishPrediction::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_RACEFINISH)
		return;

	CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
	const int TrackedClientId = GetTrackedClientId();
	if(TrackedClientId < 0 || pMsg->m_ClientId != TrackedClientId)
		return;

	if(m_RaceStartTick >= 0)
		m_FinishedRaceTick = m_RaceStartTick;
	else if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		m_FinishedRaceTick = GameClient()->LastRaceTick();
	else
		m_FinishedRaceTick = GetGameInfoRaceStartTick();
	ResetState(false);
	m_CachedStateValid = false;
}

void CFinishPrediction::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(GameClient()->m_HudEditor.IsActive())
		return;
	if(g_Config.m_ClFocusMode && (g_Config.m_ClFocusModeHideHud || g_Config.m_ClFocusModeHideUI))
		return;

	Render(false);
}

void CFinishPrediction::ResetState(bool ClearFinishedRace)
{
	m_RaceStartTick = -1;
	m_RaceStartDistance = -1.0f;
	m_LastProgress = 0.0f;
	m_SmoothedFinishTimeMs = -1;
	m_LastPredictTick = -1;
	if(ClearFinishedRace)
		m_FinishedRaceTick = -1;
	m_CachedStateValid = false;
}

void CFinishPrediction::RequestPathRebuild()
{
	m_PathRebuildPending = true;
	m_PathReady = false;
	m_PathBuildAttempted = false;
	m_CachedStateValid = false;
}

bool CFinishPrediction::IsFreezeTileIndex(int Index) const
{
	return Index >= 0 && Index < (int)m_vIsFreeze.size() && m_vIsFreeze[Index] != 0;
}

void CFinishPrediction::MarkJumpableFreezeWalls()
{
	const int MapSize = m_MapWidth * m_MapHeight;
	m_vFreezeJumpable.assign(MapSize, 0);
	if(g_Config.m_BcFinishPredictionConsiderFreezeWalls == 0 || MapSize <= 0)
		return;

	static const ivec2 s_aDirs4[] = {
		{1, 0}, {-1, 0}, {0, 1}, {0, -1},
	};
	static const ivec2 s_aDirs8[] = {
		{1, 0}, {-1, 0}, {0, 1}, {0, -1},
		{1, 1}, {1, -1}, {-1, 1}, {-1, -1},
	};

	std::vector<unsigned char> vIsFreeze(MapSize, 0);
	std::vector<unsigned char> vBasePassable(MapSize, 0);
	std::vector<int> vFreezeIndices;
	vFreezeIndices.reserve(MapSize / 8);

	auto InBounds = [&](int X, int Y) {
		return X >= 0 && X < m_MapWidth && Y >= 0 && Y < m_MapHeight;
	};
	auto Idx = [&](int X, int Y) { return Y * m_MapWidth + X; };

	for(int Index = 0; Index < MapSize; ++Index)
	{
		const int GameTile = Collision()->GetTileIndex(Index);
		const int FrontTile = Collision()->GetFrontTileIndex(Index);
		const bool HardBlocked = IsFinishPredictionHardBlocked(GameTile, FrontTile);
		const bool Freeze = !HardBlocked && IsFinishPredictionFreezeTile(GameTile, FrontTile);
		vIsFreeze[Index] = Freeze ? 1 : 0;
		vBasePassable[Index] = (!HardBlocked && !Freeze) ? 1 : 0;
		if(Freeze)
			vFreezeIndices.push_back(Index);
	}

	std::vector<unsigned char> vVisited(MapSize, 0);
	std::vector<int> Queue;
	Queue.reserve(64);

	auto LocalReachable = [&](int StartIndex, int GoalIndex, int MaxVisit) -> bool {
		if(StartIndex == GoalIndex)
			return true;
		if(vBasePassable[StartIndex] == 0 || vBasePassable[GoalIndex] == 0)
			return false;
		std::fill(vVisited.begin(), vVisited.end(), 0);
		Queue.clear();
		Queue.push_back(StartIndex);
		vVisited[StartIndex] = 1;
		size_t Head = 0;
		int VisitedCount = 1;
		while(Head < Queue.size() && VisitedCount < MaxVisit)
		{
			const int Cur = Queue[Head++];
			const int cx = Cur % m_MapWidth;
			const int cy = Cur / m_MapWidth;
			for(const ivec2 &Dir : s_aDirs4)
			{
				const int nx = cx + Dir.x;
				const int ny = cy + Dir.y;
				if(!InBounds(nx, ny))
					continue;
				const int nIndex = Idx(nx, ny);
				if(vVisited[nIndex] || vBasePassable[nIndex] == 0)
					continue;
				if(nIndex == GoalIndex)
					return true;
				vVisited[nIndex] = 1;
				Queue.push_back(nIndex);
				++VisitedCount;
			}
		}
		return false;
	};

	for(int Index : vFreezeIndices)
	{
		const int x = Index % m_MapWidth;
		const int y = Index / m_MapWidth;

		bool HasSupport = false;
		for(const ivec2 &Dir : s_aDirs8)
		{
			const int nx = x + Dir.x;
			const int ny = y + Dir.y;
			if(!InBounds(nx, ny))
				continue;
			const int nIndex = Idx(nx, ny);
			const int GameTile = Collision()->GetTileIndex(nIndex);
			const int FrontTile = Collision()->GetFrontTileIndex(nIndex);
			if(IsFinishPredictionSupportTile(GameTile, FrontTile))
			{
				HasSupport = true;
				break;
			}
		}
		if(!HasSupport)
			continue;

		const int East = InBounds(x + 1, y) ? Idx(x + 1, y) : -1;
		const int West = InBounds(x - 1, y) ? Idx(x - 1, y) : -1;
		const int South = InBounds(x, y + 1) ? Idx(x, y + 1) : -1;
		const int North = InBounds(x, y - 1) ? Idx(x, y - 1) : -1;

		if((East >= 0 && West >= 0 && vBasePassable[East] && vBasePassable[West]) ||
			(North >= 0 && South >= 0 && vBasePassable[North] && vBasePassable[South]))
		{
			m_vFreezeJumpable[Index] = 1;
			continue;
		}

		int aWalkable[4];
		int NumWalkable = 0;
		for(const ivec2 &Dir : s_aDirs4)
		{
			const int nx = x + Dir.x;
			const int ny = y + Dir.y;
			if(!InBounds(nx, ny))
				continue;
			const int nIndex = Idx(nx, ny);
			if(vBasePassable[nIndex])
				aWalkable[NumWalkable++] = nIndex;
		}
		if(NumWalkable < 2)
			continue;

		bool NeedsJump = false;
		for(int i = 1; i < NumWalkable; ++i)
		{
			if(!LocalReachable(aWalkable[0], aWalkable[i], 48))
			{
				NeedsJump = true;
				break;
			}
		}
		if(NeedsJump)
			m_vFreezeJumpable[Index] = 1;
	}
}

bool CFinishPrediction::RebuildPathData()
{
	m_vDistances.clear();
	m_vPassable.clear();
	m_vIsFreeze.clear();
	m_vFreezeJumpable.clear();
	m_vStartTiles.clear();
	m_vFinishTiles.clear();
	m_MapWidth = 0;
	m_MapHeight = 0;
	m_PathReady = false;
	m_PathBuildAttempted = true;
	m_PathBuiltWithFreezeWalls = g_Config.m_BcFinishPredictionConsiderFreezeWalls != 0;
	m_PathRebuildPending = false;
	m_CachedStateValid = false;

	if(!Collision() || Collision()->GetWidth() <= 0 || Collision()->GetHeight() <= 0)
		return false;

	m_MapWidth = Collision()->GetWidth();
	m_MapHeight = Collision()->GetHeight();
	const int MapSize = m_MapWidth * m_MapHeight;
	m_vDistances.assign(MapSize, -1);
	m_vPassable.assign(MapSize, 0);
	m_vIsFreeze.assign(MapSize, 0);
	m_vFreezeJumpable.assign(MapSize, 0);

	using TDistanceNode = std::pair<int, int>;
	std::priority_queue<TDistanceNode, std::vector<TDistanceNode>, std::greater<>> PriorityQueue;
	for(int y = 0; y < m_MapHeight; ++y)
	{
		for(int x = 0; x < m_MapWidth; ++x)
		{
			const int Index = y * m_MapWidth + x;
			const int GameTile = Collision()->GetTileIndex(Index);
			const int FrontTile = Collision()->GetFrontTileIndex(Index);
			const bool HardBlocked = IsFinishPredictionHardBlocked(GameTile, FrontTile);
			const bool Freeze = !HardBlocked && IsFinishPredictionFreezeTile(GameTile, FrontTile);
			m_vPassable[Index] = HardBlocked ? 0 : 1;
			m_vIsFreeze[Index] = Freeze ? 1 : 0;

			const bool StartTile = GameTile == TILE_START || FrontTile == TILE_START;
			const bool FinishTile = GameTile == TILE_FINISH || FrontTile == TILE_FINISH;
			if(StartTile)
				m_vStartTiles.emplace_back(x, y);
			if(FinishTile && m_vPassable[Index] != 0)
			{
				m_vFinishTiles.emplace_back(x, y);
				m_vDistances[Index] = 0;
				PriorityQueue.emplace(0, Index);
			}
		}
	}

	MarkJumpableFreezeWalls();

	if(PriorityQueue.empty())
	{
		m_vDistances.clear();
		m_vPassable.clear();
		m_vIsFreeze.clear();
		m_vFreezeJumpable.clear();
		return false;
	}

	struct SDir
	{
		ivec2 m_Dir;
		int m_Cost;
	};
	static const SDir s_aDirs[] = {
		{{1, 0}, 10},
		{{-1, 0}, 10},
		{{0, 1}, 10},
		{{0, -1}, 10},
		{{1, 1}, 14},
		{{1, -1}, 14},
		{{-1, 1}, 14},
		{{-1, -1}, 14},
	};
	const bool PreferJumpableFreeze = g_Config.m_BcFinishPredictionConsiderFreezeWalls != 0;
	while(!PriorityQueue.empty())
	{
		const auto [CurDist, Index] = PriorityQueue.top();
		PriorityQueue.pop();
		if(Index < 0 || Index >= MapSize || m_vDistances[Index] != CurDist)
			continue;
		const int TileX = Index % m_MapWidth;
		const int TileY = Index / m_MapWidth;
		for(const SDir &DirInfo : s_aDirs)
		{
			const ivec2 Dir = DirInfo.m_Dir;
			const int NextX = TileX + Dir.x;
			const int NextY = TileY + Dir.y;
			if(NextX < 0 || NextX >= m_MapWidth || NextY < 0 || NextY >= m_MapHeight)
				continue;
			const int NextIndex = NextY * m_MapWidth + NextX;
			if(m_vPassable[NextIndex] == 0)
				continue;
			if(Dir.x != 0 && Dir.y != 0)
			{
				const int SideIndexX = TileY * m_MapWidth + NextX;
				const int SideIndexY = NextY * m_MapWidth + TileX;
				if(m_vPassable[SideIndexX] == 0 || m_vPassable[SideIndexY] == 0)
					continue;
			}

			int StepCost = DirInfo.m_Cost;
			if(m_vIsFreeze[NextIndex])
			{
				if(PreferJumpableFreeze && m_vFreezeJumpable[NextIndex])
					StepCost = FINISH_PREDICTION_FREEZE_JUMP_COST;
				else
					StepCost = DirInfo.m_Cost + 6;
			}
			const int NextDistance = CurDist + StepCost;
			if(m_vDistances[NextIndex] >= 0 && m_vDistances[NextIndex] <= NextDistance)
				continue;
			m_vDistances[NextIndex] = NextDistance;
			PriorityQueue.emplace(NextDistance, NextIndex);
		}
	}

	m_PathReady = true;
	return true;
}

bool CFinishPrediction::EnsurePathData()
{
	if(!Collision() || Collision()->GetWidth() <= 0 || Collision()->GetHeight() <= 0)
		return false;

	const bool WantFreezeWalls = g_Config.m_BcFinishPredictionConsiderFreezeWalls != 0;
	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	if(m_PathRebuildPending || m_MapWidth != Width || m_MapHeight != Height || m_PathBuiltWithFreezeWalls != WantFreezeWalls)
	{
		m_PathBuildAttempted = false;
		m_PathReady = false;
		return RebuildPathData();
	}
	if(m_PathReady)
		return true;
	if(m_PathBuildAttempted)
		return false;
	return RebuildPathData();
}

float CFinishPrediction::GetDistanceAtPos(vec2 Pos) const
{
	if(!m_PathReady || m_vDistances.empty() || m_MapWidth <= 0 || m_MapHeight <= 0)
		return -1.0f;

	const int TileX = std::clamp((int)std::floor(Pos.x / 32.0f), 0, m_MapWidth - 1);
	const int TileY = std::clamp((int)std::floor(Pos.y / 32.0f), 0, m_MapHeight - 1);

	float BestDistance = -1.0f;
	for(int Radius = 0; Radius <= 8; ++Radius)
	{
		for(int y = std::max(0, TileY - Radius); y <= std::min(m_MapHeight - 1, TileY + Radius); ++y)
		{
			for(int x = std::max(0, TileX - Radius); x <= std::min(m_MapWidth - 1, TileX + Radius); ++x)
			{
				if(Radius > 0 && absolute(x - TileX) != Radius && absolute(y - TileY) != Radius)
					continue;
				const int Index = y * m_MapWidth + x;
				const int Dist = m_vDistances[Index];
				if(Dist < 0)
					continue;
				const float OffsetCost = distance(Pos, vec2(x * 32.0f + 16.0f, y * 32.0f + 16.0f)) / 32.0f;
				const float Total = Dist / 10.0f + OffsetCost;
				if(BestDistance < 0.0f || Total < BestDistance)
					BestDistance = Total;
			}
		}
		if(BestDistance >= 0.0f && Radius >= 2)
			break;
	}
	return BestDistance;
}

float CFinishPrediction::GetMapSpanDistance() const
{
	if(m_vDistances.empty())
		return -1.0f;
	int MaxDist = -1;
	for(int Dist : m_vDistances)
	{
		if(Dist > MaxDist)
			MaxDist = Dist;
	}
	return MaxDist >= 0 ? MaxDist / 10.0f : -1.0f;
}

float CFinishPrediction::GetStartDistance() const
{
	if(m_RaceStartDistance > 0.0f)
		return m_RaceStartDistance;

	float BestDistance = -1.0f;
	for(const ivec2 &StartTile : m_vStartTiles)
	{
		const int Index = StartTile.y * m_MapWidth + StartTile.x;
		if(Index < 0 || Index >= (int)m_vDistances.size())
			continue;
		const int Dist = m_vDistances[Index];
		if(Dist < 0)
			continue;
		const float DistanceTiles = Dist / 10.0f;
		if(BestDistance < 0.0f || DistanceTiles < BestDistance)
			BestDistance = DistanceTiles;
	}
	if(BestDistance > 0.0f)
		return BestDistance;
	return GetMapSpanDistance();
}

int CFinishPrediction::GetTrackedClientId() const
{
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		const int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
		if(SpectatorId <= SPEC_FREEVIEW || SpectatorId >= MAX_CLIENTS)
			return -1;
		return SpectatorId;
	}
	return GameClient()->m_aLocalIds[g_Config.m_ClDummy];
}

int CFinishPrediction::GetGameInfoRaceStartTick() const
{
	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return -1;
	const bool RaceFlag = (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME) != 0;
	return RaceFlag ? -GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer : -1;
}

int64_t CFinishPrediction::GetScoreboardTimeMs(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return -1;
	const CNetObj_PlayerInfo *pPlayerInfo = GameClient()->m_Snap.m_apPlayerInfos[ClientId];
	if(!pPlayerInfo)
		return -1;

	const bool Race7 = Client()->IsSixup() && GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE);
	if(Race7)
	{
		if(pPlayerInfo->m_Score == protocol7::FinishTime::NOT_FINISHED)
			return -1;
		return std::max<int64_t>(0, pPlayerInfo->m_Score);
	}

	if(GameClient()->m_GameInfo.m_TimeScore)
	{
		if(pPlayerInfo->m_Score == FinishTime::NOT_FINISHED_TIMESCORE)
			return -1;
		return std::max<int64_t>(0, pPlayerInfo->m_Score) * 1000;
	}

	return -1;
}

int64_t CFinishPrediction::GetBestTimeMs() const
{
	if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET && GameClient()->m_MapBestTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
		return (int64_t)GameClient()->m_MapBestTimeSeconds * 1000 + GameClient()->m_MapBestTimeMillis;

	int64_t BestTimeMs = -1;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		const int64_t ScoreTimeMs = GetScoreboardTimeMs(i);
		if(ScoreTimeMs <= 0)
			continue;
		if(BestTimeMs < 0 || ScoreTimeMs < BestTimeMs)
			BestTimeMs = ScoreTimeMs;
	}
	return BestTimeMs;
}

int64_t CFinishPrediction::GetPersonalBestTimeMs(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return -1;

	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const auto &ClientData = GameClient()->m_aClients[ClientId];
		if(ClientData.m_FinishTimeSeconds != FinishTime::UNSET && ClientData.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
			return (int64_t)absolute(ClientData.m_FinishTimeSeconds) * 1000 + (absolute(ClientData.m_FinishTimeMillis) % 1000);
	}

	const int64_t ScoreboardTimeMs = GetScoreboardTimeMs(ClientId);
	if(ScoreboardTimeMs > 0)
		return ScoreboardTimeMs;

	const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(ClientId == LocalClientId)
	{
		const float PlayerRecord = GameClient()->m_Hud.GetPlayerRecordSeconds(g_Config.m_ClDummy);
		return PlayerRecord > 0.0f ? (int64_t)round(PlayerRecord * 1000.0f) : -1;
	}
	return -1;
}

int64_t CFinishPrediction::GetAverageTimeMs() const
{
	int64_t Sum = 0;
	int Count = 0;
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i])
				continue;
			const auto &ClientData = GameClient()->m_aClients[i];
			if(ClientData.m_FinishTimeSeconds == FinishTime::UNSET || ClientData.m_FinishTimeSeconds == FinishTime::NOT_FINISHED_MILLIS)
				continue;
			Sum += (int64_t)absolute(ClientData.m_FinishTimeSeconds) * 1000 + (absolute(ClientData.m_FinishTimeMillis) % 1000);
			++Count;
		}
	}
	else
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const int64_t ScoreTimeMs = GetScoreboardTimeMs(i);
			if(ScoreTimeMs <= 0)
				continue;
			Sum += ScoreTimeMs;
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : -1;
}

bool CFinishPrediction::GetState(SFinishPredictionState &State, bool ForcePreview)
{
	const int CurrentGameTick = Client()->GameTick(g_Config.m_ClDummy);
	if(m_CachedStateValid && m_CachedForcePreview == ForcePreview && m_CachedStateTick == CurrentGameTick)
	{
		State = m_CachedState;
		return State.m_Valid;
	}

	State = {};
	if(ForcePreview)
	{
		State.m_Valid = true;
		State.m_HasPredictedTime = true;
		State.m_ShowPlayerName = true;
		str_copy(State.m_aPlayerName, "nameless tee", sizeof(State.m_aPlayerName));
		State.m_Progress = 0.051f;
		State.m_CurrentTimeMs = 68420;
		State.m_PredictedFinishTimeMs = 118300;
		State.m_RemainingTimeMs = State.m_PredictedFinishTimeMs - State.m_CurrentTimeMs;
		m_CachedState = State;
		m_CachedStateValid = true;
		m_CachedForcePreview = true;
		m_CachedStateTick = CurrentGameTick;
		return true;
	}

	if(g_Config.m_BcFinishPrediction == 0)
	{
		ResetState();
		m_TrackedClientId = -1;
		m_CachedState = State;
		m_CachedStateValid = true;
		m_CachedForcePreview = false;
		m_CachedStateTick = CurrentGameTick;
		return false;
	}

	const int TrackedClientId = GetTrackedClientId();
	if(TrackedClientId < 0)
	{
		ResetState();
		m_TrackedClientId = -1;
		m_CachedState = State;
		m_CachedStateValid = true;
		m_CachedForcePreview = false;
		m_CachedStateTick = CurrentGameTick;
		return false;
	}

	if(m_TrackedClientId != TrackedClientId)
	{
		ResetState();
		m_TrackedClientId = TrackedClientId;
	}

	const bool Spectating = GameClient()->m_Snap.m_SpecInfo.m_Active;
	if(Spectating)
	{
		str_copy(State.m_aPlayerName, GameClient()->m_aClients[TrackedClientId].m_aName, sizeof(State.m_aPlayerName));
		State.m_ShowPlayerName = State.m_aPlayerName[0] != '\0';
	}

	CFastPractice &FastPractice = FinishPredictionFastPractice(GameClient());
	const bool UsingFastPractice = !Spectating && FastPractice.Active();
	if(m_UsingFastPractice != UsingFastPractice)
	{
		ResetState();
		m_UsingFastPractice = UsingFastPractice;
	}

	int CurrentTick = CurrentGameTick;
	int RaceStartTick = Spectating ? GetGameInfoRaceStartTick() : GameClient()->LastRaceTick();
	vec2 LocalPos(0.0f, 0.0f);
	bool RaceFinished = m_FinishedRaceTick == RaceStartTick && RaceStartTick >= 0;

	if(Spectating)
	{
		if(!GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Active)
		{
			m_CachedState = State;
			m_CachedStateValid = true;
			m_CachedForcePreview = false;
			m_CachedStateTick = CurrentGameTick;
			return false;
		}
		LocalPos = vec2(GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Cur.m_X,
			GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Cur.m_Y);
	}
	else if(UsingFastPractice)
	{
		CFastPractice::SLocalRaceState PracticeRaceState;
		if(!FastPractice.GetLocalRaceState(PracticeRaceState))
		{
			ResetState();
			m_CachedState = State;
			m_CachedStateValid = true;
			m_CachedForcePreview = false;
			m_CachedStateTick = CurrentGameTick;
			return false;
		}
		CurrentTick = PracticeRaceState.m_CurrentTick;
		RaceStartTick = PracticeRaceState.m_StartTick;
		LocalPos = PracticeRaceState.m_Position;
		RaceFinished = PracticeRaceState.m_Finished;
	}
	else
	{
		if(!GameClient()->m_Snap.m_pLocalCharacter)
		{
			ResetState();
			m_CachedState = State;
			m_CachedStateValid = true;
			m_CachedForcePreview = false;
			m_CachedStateTick = CurrentGameTick;
			return false;
		}
		LocalPos = vec2(GameClient()->m_Snap.m_pLocalCharacter->m_X, GameClient()->m_Snap.m_pLocalCharacter->m_Y);
	}

	auto CacheState = [&](bool Valid) {
		State.m_Valid = Valid;
		m_CachedState = State;
		m_CachedStateValid = true;
		m_CachedForcePreview = false;
		m_CachedStateTick = CurrentGameTick;
		return Valid;
	};

	if(RaceStartTick < 0)
	{
		if(g_Config.m_BcFinishPredictionShowAlways == 0)
			return CacheState(false);

		State.m_CurrentTimeMs = 0;
		State.m_PredictedFinishTimeMs = 0;
		State.m_RemainingTimeMs = 0;
		State.m_HasPredictedTime = false;
		State.m_Progress = 0.0f;
		if(EnsurePathData())
		{
			const float CurrentDistance = GetDistanceAtPos(LocalPos);
			const float StartDistance = std::max(GetStartDistance(), CurrentDistance);
			if(CurrentDistance >= 0.0f && StartDistance > 0.0f)
			{
				State.m_Progress = std::clamp(1.0f - CurrentDistance / StartDistance, 0.0f, 1.0f);
				if(CurrentDistance <= 0.5f)
					State.m_Progress = 1.0f;
			}
		}
		return CacheState(true);
	}

	if(RaceFinished)
	{
		ResetState(false);
		if(g_Config.m_BcFinishPredictionShowAlways == 0)
			return CacheState(false);
		State.m_Progress = 1.0f;
		State.m_CurrentTimeMs = 0;
		State.m_PredictedFinishTimeMs = 0;
		State.m_RemainingTimeMs = 0;
		return CacheState(true);
	}

	if(!EnsurePathData())
	{
		if(g_Config.m_BcFinishPredictionShowAlways == 0)
			return CacheState(false);
		State.m_HasPredictedTime = false;
		State.m_Progress = 0.0f;
		State.m_CurrentTimeMs = std::max<int64_t>(0, (int64_t)(CurrentTick - RaceStartTick) * 1000 / std::max(1, Client()->GameTickSpeed()));
		State.m_PredictedFinishTimeMs = 0;
		State.m_RemainingTimeMs = 0;
		return CacheState(true);
	}

	State.m_CurrentTimeMs = std::max<int64_t>(0, (int64_t)(CurrentTick - RaceStartTick) * 1000 / std::max(1, Client()->GameTickSpeed()));
	const float CurrentDistance = GetDistanceAtPos(LocalPos);
	if(CurrentDistance < 0.0f)
	{
		State.m_Progress = std::max(0.0f, m_LastProgress);
		return CacheState(true);
	}

	if(m_RaceStartTick != RaceStartTick)
	{
		m_FinishedRaceTick = -1;
		m_RaceStartTick = RaceStartTick;
		m_RaceStartDistance = std::max(CurrentDistance, GetStartDistance());
		m_LastProgress = 0.0f;
		m_SmoothedFinishTimeMs = -1;
		m_LastPredictTick = -1;
	}

	const float StartDistance = m_RaceStartDistance > 0.0f ?
					     std::max(m_RaceStartDistance, 1.0f) :
					     std::max(GetStartDistance(), CurrentDistance);
	if(StartDistance <= 0.0f)
	{
		State.m_Progress = std::max(0.0f, m_LastProgress);
		return CacheState(true);
	}

	float Progress = std::clamp(1.0f - CurrentDistance / StartDistance, 0.0f, 1.0f);
	if(CurrentDistance <= 0.5f)
		Progress = 1.0f;
	Progress = std::max(m_LastProgress, Progress);
	State.m_Progress = Progress;
	m_LastProgress = State.m_Progress;

	const int64_t CurrentPacePrediction = State.m_Progress > 0.015f && State.m_CurrentTimeMs > 1500 ?
						      (int64_t)(State.m_CurrentTimeMs / std::max(State.m_Progress, 0.015f)) :
						      -1;
	const int64_t BestTimeMs = GetBestTimeMs();
	const int64_t PersonalBestTimeMs = GetPersonalBestTimeMs(TrackedClientId);
	const int64_t AverageTimeMs = GetAverageTimeMs();

	int64_t ReferenceTimeMs = -1;
	if(BestTimeMs > 0 && AverageTimeMs > 0 && PersonalBestTimeMs > 0)
		ReferenceTimeMs = (BestTimeMs + AverageTimeMs + PersonalBestTimeMs) / 3;
	else if(BestTimeMs > 0 && AverageTimeMs > 0)
		ReferenceTimeMs = (BestTimeMs + AverageTimeMs) / 2;
	else if(PersonalBestTimeMs > 0 && AverageTimeMs > 0)
		ReferenceTimeMs = (PersonalBestTimeMs + AverageTimeMs) / 2;
	else if(BestTimeMs > 0 && PersonalBestTimeMs > 0)
		ReferenceTimeMs = (BestTimeMs + PersonalBestTimeMs) / 2;
	else if(BestTimeMs > 0)
		ReferenceTimeMs = BestTimeMs;
	else if(PersonalBestTimeMs > 0)
		ReferenceTimeMs = PersonalBestTimeMs;
	else if(AverageTimeMs > 0)
		ReferenceTimeMs = AverageTimeMs;

	if(State.m_Progress >= 0.999f)
	{
		State.m_PredictedFinishTimeMs = State.m_CurrentTimeMs;
		State.m_HasPredictedTime = true;
		m_SmoothedFinishTimeMs = State.m_PredictedFinishTimeMs;
		m_LastPredictTick = CurrentTick;
	}
	else if(CurrentPacePrediction > 0 && ReferenceTimeMs > 0)
	{
		const float ProgressConfidence = std::clamp((State.m_Progress - 0.04f) / 0.34f, 0.0f, 1.0f);
		const float TimeConfidence = std::clamp(State.m_CurrentTimeMs / 45000.0f, 0.0f, 1.0f);
		const float Blend = std::clamp(ProgressConfidence * 0.78f + TimeConfidence * 0.22f, 0.0f, 0.96f);
		State.m_PredictedFinishTimeMs = (int64_t)mix((float)ReferenceTimeMs, (float)CurrentPacePrediction, Blend);
		State.m_HasPredictedTime = true;
	}
	else if(CurrentPacePrediction > 0)
	{
		State.m_PredictedFinishTimeMs = CurrentPacePrediction;
		State.m_HasPredictedTime = true;
	}
	else if(ReferenceTimeMs > 0)
	{
		State.m_PredictedFinishTimeMs = ReferenceTimeMs;
		State.m_HasPredictedTime = true;
	}
	else
		State.m_PredictedFinishTimeMs = 0;

	if(State.m_HasPredictedTime)
	{
		State.m_PredictedFinishTimeMs = std::max<int64_t>(State.m_PredictedFinishTimeMs, State.m_CurrentTimeMs);
		if(State.m_Progress < 0.999f)
		{
			if(m_SmoothedFinishTimeMs < 0)
			{
				m_SmoothedFinishTimeMs = State.m_PredictedFinishTimeMs;
				m_LastPredictTick = CurrentTick;
			}
			else if(m_LastPredictTick != CurrentTick)
			{
				const int TickDelta = std::max(1, CurrentTick - std::max(0, m_LastPredictTick));
				const float Follow = State.m_PredictedFinishTimeMs < m_SmoothedFinishTimeMs ? 0.075f : 0.045f;
				const float Blend = std::clamp(TickDelta * Follow, 0.035f, 0.30f);
				m_SmoothedFinishTimeMs = (int64_t)mix((float)m_SmoothedFinishTimeMs, (float)State.m_PredictedFinishTimeMs, Blend);
				m_LastPredictTick = CurrentTick;
			}
			State.m_PredictedFinishTimeMs = std::max<int64_t>(m_SmoothedFinishTimeMs, State.m_CurrentTimeMs);
		}
		State.m_RemainingTimeMs = std::max<int64_t>(0, State.m_PredictedFinishTimeMs - State.m_CurrentTimeMs);
	}
	else
		State.m_RemainingTimeMs = 0;

	return CacheState(true);
}

CUIRect CFinishPrediction::GetRect(bool ForcePreview) const
{
	if(!ForcePreview && !HudLayout::IsEnabled(HudLayout::MODULE_FINISH_PREDICTION))
		return {};

	SFinishPredictionState State;
	if(!const_cast<CFinishPrediction *>(this)->GetState(State, ForcePreview))
		return {};

	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_FINISH_PREDICTION, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float NameFontSize = 4.75f * Scale;
	const float TitleFontSize = 5.25f * Scale;
	const float ProgressFontSize = 4.75f * Scale;
	const float PaddingX = 6.0f * Scale;
	const float PaddingY = 4.0f * Scale;
	const float Gap = 1.5f * Scale;
	const bool ShowName = State.m_ShowPlayerName && State.m_aPlayerName[0] != '\0';
	const bool ShowTime = g_Config.m_BcFinishPredictionShowTime != 0;
	const bool ShowPercentage = g_Config.m_BcFinishPredictionShowPercentage != 0;
	const bool ShowMillis = g_Config.m_BcFinishPredictionShowMillis != 0;
	if(!ShowName && !ShowTime && !ShowPercentage)
		return {};

	char aSampleTopLine[64];
	char aProgress[32];
	str_format(aSampleTopLine, sizeof(aSampleTopLine), "%s %s", Localize("Finish"), ShowMillis ? "00:00:00.00" : "00:00:00");
	str_copy(aProgress, "100.0%", sizeof(aProgress));

	const float NameWidth = ShowName ? TextRender()->TextWidth(NameFontSize, State.m_aPlayerName, -1, -1.0f) : 0.0f;
	const float TopWidth = ShowTime ? TextRender()->TextWidth(TitleFontSize, aSampleTopLine, -1, -1.0f) : 0.0f;
	const float ProgressWidth = ShowPercentage ? TextRender()->TextWidth(ProgressFontSize, aProgress, -1, -1.0f) : 0.0f;
	const float RectWidth = std::max({NameWidth, TopWidth, ProgressWidth}) + PaddingX * 2.0f;

	int VisibleLines = 0;
	if(ShowName)
		++VisibleLines;
	if(ShowTime)
		++VisibleLines;
	if(ShowPercentage)
		++VisibleLines;
	const float ContentHeight = (ShowName ? NameFontSize : 0.0f) + (ShowTime ? TitleFontSize : 0.0f) + (ShowPercentage ? ProgressFontSize : 0.0f) + (VisibleLines > 1 ? Gap * (VisibleLines - 1) : 0.0f);
	const float RectHeight = PaddingY * 2.0f + ContentHeight;
	CUIRect Rect = {Layout.m_X, Layout.m_Y, RectWidth, RectHeight};
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, HudWidth - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, HudHeight - Rect.h));
	return PushHudRectFromMusicPlayer(GameClient(), Rect, HudWidth, HudHeight, ForcePreview);
}

void CFinishPrediction::Render(bool ForcePreview)
{
	if(!ForcePreview && !HudLayout::IsEnabled(HudLayout::MODULE_FINISH_PREDICTION))
		return;
	if(!ForcePreview && g_Config.m_BcFinishPrediction == 0)
		return;

	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(HudWidth, HudHeight);

	CUIRect Rect = GetRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	SFinishPredictionState State;
	if(!GetState(State, ForcePreview))
		return;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_FINISH_PREDICTION, HudWidth, HudHeight);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_FINISH_PREDICTION);
	const float NameFontSize = 4.75f * Scale;
	const float TitleFontSize = 5.25f * Scale;
	const float ProgressFontSize = 4.75f * Scale;
	const float PaddingX = 6.0f * Scale;
	const float PaddingY = 4.0f * Scale;
	const float Gap = 1.5f * Scale;
	const ColorRGBA BackgroundColor = color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)).WithMultipliedAlpha(ModuleAlpha);
	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Rect.x, Rect.y, Rect.w, Rect.h, HudWidth, HudHeight);
	const bool ShowName = State.m_ShowPlayerName && State.m_aPlayerName[0] != '\0';
	const bool ShowTime = g_Config.m_BcFinishPredictionShowTime != 0;
	const bool ShowRemaining = g_Config.m_BcFinishPredictionTimeMode == 0;
	const bool ShowPercentage = g_Config.m_BcFinishPredictionShowPercentage != 0;
	const bool ShowMillis = g_Config.m_BcFinishPredictionShowMillis != 0;

	if(Layout.m_BackgroundEnabled)
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, BackgroundColor, Corners, 5.0f * Scale);

	char aTime[32];
	char aLabel[32];
	char aProgress[32];
	FormatPredictionTime(ShowRemaining ? State.m_RemainingTimeMs : State.m_PredictedFinishTimeMs, ShowMillis, aTime, sizeof(aTime));
	str_copy(aLabel, ShowRemaining ? Localize("Left") : Localize("Finish"), sizeof(aLabel));
	str_format(aProgress, sizeof(aProgress), "%.1f%%", State.m_Progress * 100.0f);

	float TextY = Rect.y + PaddingY;
	if(ShowName)
	{
		const float NameWidth = TextRender()->TextWidth(NameFontSize, State.m_aPlayerName, -1, -1.0f);
		TextRender()->TextColor(0.92f, 0.94f, 1.0f, ModuleAlpha);
		TextRender()->Text(Rect.x + std::max(PaddingX, (Rect.w - NameWidth) * 0.5f), TextY, NameFontSize, State.m_aPlayerName, -1.0f);
		TextY += NameFontSize + Gap;
	}
	if(ShowTime)
	{
		char aTopLine[64];
		str_format(aTopLine, sizeof(aTopLine), "%s %s", aLabel, aTime);
		const float TopWidth = TextRender()->TextWidth(TitleFontSize, aTopLine, -1, -1.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, ModuleAlpha);
		TextRender()->Text(Rect.x + std::max(PaddingX, (Rect.w - TopWidth) * 0.5f), TextY, TitleFontSize, aTopLine, -1.0f);
		TextY += TitleFontSize + Gap;
	}
	if(ShowPercentage)
	{
		const float ProgressWidth = TextRender()->TextWidth(ProgressFontSize, aProgress, -1, -1.0f);
		TextRender()->TextColor(0.78f, 0.88f, 1.0f, ModuleAlpha);
		TextRender()->Text(Rect.x + std::max(PaddingX, (Rect.w - ProgressWidth) * 0.5f), TextY, ProgressFontSize, aProgress, -1.0f);
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}
