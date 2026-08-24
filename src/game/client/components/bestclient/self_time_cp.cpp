/* Copyright © 2026 BestProject Team */
#include "self_time_cp.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/camera.h>
#include <game/client/components/controls.h>
#include <game/client/components/ghost.h>
#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <generated/protocol.h>

#include <algorithm>
#include <cmath>
#include <limits>

void CSelfTimeCp::ConPlace(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CSelfTimeCp *>(pUserData)->Place();
}

void CSelfTimeCp::ConClear(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CSelfTimeCp *>(pUserData)->Clear(true);
}

void CSelfTimeCp::ConUndo(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	static_cast<CSelfTimeCp *>(pUserData)->UndoLast(true);
}

void CSelfTimeCp::OnConsoleInit()
{
	Console()->Register("BC_place_time_cp", "", CFGFLAG_CLIENT, ConPlace, this, "Place a personal time checkpoint");
	Console()->Register("BC_clear_time_cp", "", CFGFLAG_CLIENT, ConClear, this, "Clear personal time checkpoints");
	Console()->Register("BC_undo_time_cp", "", CFGFLAG_CLIENT, ConUndo, this, "Remove the last personal time checkpoint");
}

bool CSelfTimeCp::Enabled() const
{
	return g_Config.m_BcSelfTimeCp != 0;
}

bool CSelfTimeCp::CanPlace() const
{
	return Client()->State() == IClient::STATE_ONLINE && Collision()->GetWidth() > 0 && Collision()->GetHeight() > 0;
}

void CSelfTimeCp::OnReset()
{
	Clear(false);
}

void CSelfTimeCp::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE)
		Clear(false);
}

void CSelfTimeCp::Clear(bool Echo)
{
	const bool HadCheckpoints = !m_vCheckpoints.empty();
	m_vCheckpoints.clear();
	m_LastPos = vec2(0.0f, 0.0f);
	m_HasLastPos = false;
	m_CurrentRaceTick = -1;
	m_BestFinishTime = -1.0f;
	m_RaceTimeOffset = 0.0f;
	m_HasRaceTimeOffset = false;
	m_LastHitTimeCpIndex = -1;
	std::fill(std::begin(m_aServerBestTimeCp), std::end(m_aServerBestTimeCp), 0.0f);
	std::fill(std::begin(m_aHasServerBestTimeCp), std::end(m_aHasServerBestTimeCp), false);

	if(Echo && HadCheckpoints)
		GameClient()->Echo(Localize("Self timeCP cleared"));
}

void CSelfTimeCp::UndoLast(bool Echo)
{
	if(!Enabled())
		return;

	if(m_vCheckpoints.empty())
	{
		if(Echo)
			GameClient()->Echo(Localize("No self timeCP to undo"));
		return;
	}

	m_vCheckpoints.pop_back();
	if(Echo)
		GameClient()->Echo(Localize("Self timeCP undone"));
}

bool CSelfTimeCp::IsGapBoundary(int TileX, int TileY) const
{
	if(TileX < 0 || TileX >= Collision()->GetWidth() || TileY < 0 || TileY >= Collision()->GetHeight())
		return false;

	const int PixelX = TileX * 32 + 16;
	const int PixelY = TileY * 32 + 16;

	const int Index = Collision()->GetTile(PixelX, PixelY);
	if(Index == TILE_SOLID || Index == TILE_NOHOOK)
		return true;
	if(Index == TILE_FREEZE || Index == TILE_DFREEZE || Index == TILE_LFREEZE)
		return true;

	const int FrontIndex = Collision()->GetFrontTile(PixelX, PixelY);
	if(FrontIndex == TILE_SOLID || FrontIndex == TILE_NOHOOK)
		return true;
	if(FrontIndex == TILE_FREEZE || FrontIndex == TILE_DFREEZE || FrontIndex == TILE_LFREEZE)
		return true;

	return false;
}

bool CSelfTimeCp::FindGap(int TileX, int PreferTileY, int &TileYTop, int &TileYBottom) const
{
	if(TileX < 0 || TileX >= Collision()->GetWidth())
		return false;

	int StartY = PreferTileY;
	if(StartY < 0 || StartY >= Collision()->GetHeight() || IsGapBoundary(TileX, StartY))
	{
		StartY = -1;
		for(int Offset = 0; Offset < Collision()->GetHeight(); ++Offset)
		{
			const int Up = PreferTileY - Offset;
			const int Down = PreferTileY + Offset;
			if(Up >= 0 && !IsGapBoundary(TileX, Up))
			{
				StartY = Up;
				break;
			}
			if(Down < Collision()->GetHeight() && !IsGapBoundary(TileX, Down))
			{
				StartY = Down;
				break;
			}
		}
		if(StartY < 0)
			return false;
	}

	TileYTop = StartY;
	while(TileYTop > 0 && !IsGapBoundary(TileX, TileYTop - 1))
		--TileYTop;

	TileYBottom = StartY;
	while(TileYBottom + 1 < Collision()->GetHeight() && !IsGapBoundary(TileX, TileYBottom + 1))
		++TileYBottom;

	if(TileYTop <= 0 || !IsGapBoundary(TileX, TileYTop - 1))
		return false;
	if(TileYBottom + 1 >= Collision()->GetHeight() || !IsGapBoundary(TileX, TileYBottom + 1))
		return false;

	return TileYBottom >= TileYTop;
}

int CSelfTimeCp::FindNearestTimeCpIndex(int TileX) const
{
	if(TileX < 0 || TileX >= Collision()->GetWidth())
		return -1;

	const int Width = Collision()->GetWidth();
	const int Height = Collision()->GetHeight();
	constexpr int MaxDistance = 3; // "рядом" with a real TimeCP still links to it

	int BestIndex = -1;
	int BestDist = MaxDistance + 1;

	for(int Offset = 0; Offset <= MaxDistance; ++Offset)
	{
		for(const int Side : {0, -1, 1})
		{
			if(Offset == 0 && Side != 0)
				continue;
			if(Offset > 0 && Side == 0)
				continue;

			const int CandidateX = TileX + Side * Offset;
			if(CandidateX < 0 || CandidateX >= Width)
				continue;

			for(int TileY = 0; TileY < Height; ++TileY)
			{
				const int MapIndex = TileY * Width + CandidateX;
				const int TimeCp = maximum(Collision()->IsTimeCheckpoint(MapIndex), Collision()->IsFrontTimeCheckpoint(MapIndex));
				if(TimeCp < 0)
					continue;
				if(Offset < BestDist)
				{
					BestDist = Offset;
					BestIndex = TimeCp;
				}
				break;
			}
		}
		if(BestDist == 0)
			break;
	}

	return BestIndex;
}

int CSelfTimeCp::DetectTimeCpHit(vec2 PrevPos, vec2 Pos) const
{
	std::vector<int> vIndices = Collision()->GetMapIndices(PrevPos, Pos);
	if(vIndices.empty())
		vIndices.push_back(Collision()->GetMapIndex(Pos));

	int Hit = -1;
	for(const int MapIndex : vIndices)
	{
		const int TimeCp = maximum(Collision()->IsTimeCheckpoint(MapIndex), Collision()->IsFrontTimeCheckpoint(MapIndex));
		if(TimeCp >= 0)
			Hit = TimeCp;
	}
	return Hit;
}

vec2 CSelfTimeCp::PlacementPos() const
{
	const int Dummy = g_Config.m_ClDummy;

	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		return GameClient()->m_Camera.m_Center;

	if(!GameClient()->m_Snap.m_pLocalCharacter)
		return GameClient()->m_Camera.m_Center;

	if(g_Config.m_BcSelfTimeCpPlaceMode == 1)
	{
		// The cursor is rendered on a zoom-1 HUD projection while the map uses
		// the actual camera zoom. Convert that visible cursor position back to
		// map coordinates. This also preserves the dyncam compensation already
		// included in m_aTargetPos.
		const vec2 Center = GameClient()->m_Camera.m_Center;
		const vec2 CursorPos = GameClient()->m_Controls.m_aTargetPos[Dummy];
		return Center + (CursorPos - Center) * GameClient()->m_Camera.m_Zoom;
	}

	return GameClient()->m_LocalCharacterPos;
}

void CSelfTimeCp::Place()
{
	if(!Enabled())
	{
		GameClient()->Echo(Localize("Self timeCP is disabled"));
		return;
	}
	if(!CanPlace())
	{
		GameClient()->Echo(Localize("Self timeCP is only available while online"));
		return;
	}

	const vec2 Pos = PlacementPos();
	const int PreferTileX = std::clamp((int)std::floor(Pos.x / 32.0f), 0, Collision()->GetWidth() - 1);
	const int PreferTileY = std::clamp((int)std::floor(Pos.y / 32.0f), 0, Collision()->GetHeight() - 1);

	int TileX = PreferTileX;
	int TileYTop = 0;
	int TileYBottom = 0;
	bool Found = FindGap(PreferTileX, PreferTileY, TileYTop, TileYBottom);
	if(!Found)
	{
		int BestDist = std::numeric_limits<int>::max();
		for(int Offset = 1; Offset <= 2; ++Offset)
		{
			for(const int Side : {-1, 1})
			{
				const int CandidateX = PreferTileX + Side * Offset;
				if(CandidateX < 0 || CandidateX >= Collision()->GetWidth())
					continue;
				int CandidateTop = 0;
				int CandidateBottom = 0;
				if(!FindGap(CandidateX, PreferTileY, CandidateTop, CandidateBottom))
					continue;
				const int Dist = absolute(CandidateX - PreferTileX);
				if(Dist < BestDist)
				{
					BestDist = Dist;
					TileX = CandidateX;
					TileYTop = CandidateTop;
					TileYBottom = CandidateBottom;
					Found = true;
				}
			}
		}
	}
	if(!Found)
	{
		GameClient()->Echo(Localize("Self timeCP needs a closed gap between tiles"));
		return;
	}

	// Same column can have several gaps (upper/lower rooms). Only replace
	// a checkpoint that occupies this exact gap, not every strip on TileX.
	auto Existing = std::find_if(m_vCheckpoints.begin(), m_vCheckpoints.end(), [=](const SCheckpoint &Checkpoint) {
		return Checkpoint.m_TileX == TileX && Checkpoint.m_TileYTop == TileYTop && Checkpoint.m_TileYBottom == TileYBottom;
	});

	SCheckpoint Checkpoint;
	Checkpoint.m_TileX = TileX;
	Checkpoint.m_TileYTop = TileYTop;
	Checkpoint.m_TileYBottom = TileYBottom;
	Checkpoint.m_LinkedTimeCpIndex = FindNearestTimeCpIndex(TileX);

	if(Existing != m_vCheckpoints.end())
	{
		Checkpoint.m_BestTime = Existing->m_BestTime;
		*Existing = Checkpoint;
		TrySeedBestFromServer(*Existing);
		if(!HasValidBest(Existing->m_BestTime) && Existing->m_LinkedTimeCpIndex < 0)
			TrySeedBestFromGhost(*Existing);
	}
	else
	{
		TrySeedBestFromServer(Checkpoint);
		if(!HasValidBest(Checkpoint.m_BestTime) && Checkpoint.m_LinkedTimeCpIndex < 0)
			TrySeedBestFromGhost(Checkpoint);
		if((int)m_vCheckpoints.size() >= MAX_SELF_TIME_CP)
			m_vCheckpoints.erase(m_vCheckpoints.begin());
		m_vCheckpoints.push_back(Checkpoint);
	}

	GameClient()->Echo(Localize("Self timeCP placed"));
}

bool CSelfTimeCp::HasValidBest(float BestTime) const
{
	// Same rule as the server: 0 means "no best split stored".
	return BestTime > 0.0f;
}

bool CSelfTimeCp::TrySeedBestFromServer(SCheckpoint &Checkpoint)
{
	if(Checkpoint.m_LinkedTimeCpIndex < 0 || Checkpoint.m_LinkedTimeCpIndex >= MAX_CHECKPOINTS)
		return false;
	if(!m_aHasServerBestTimeCp[Checkpoint.m_LinkedTimeCpIndex])
		return false;
	if(!HasValidBest(m_aServerBestTimeCp[Checkpoint.m_LinkedTimeCpIndex]))
		return false;

	Checkpoint.m_BestTime = m_aServerBestTimeCp[Checkpoint.m_LinkedTimeCpIndex];
	return true;
}

bool CSelfTimeCp::TrySeedBestFromGhost(SCheckpoint &Checkpoint)
{
	if(HasValidBest(Checkpoint.m_BestTime))
		return true;

	float GhostTime = 0.0f;
	if(!GameClient()->m_Ghost.TryGetOwnGhostTimeAtX(Checkpoint.m_TileX * 32.0f + 16.0f, &GhostTime))
		return false;
	if(!HasValidBest(GhostTime))
		return false;

	Checkpoint.m_BestTime = GhostTime;
	return true;
}

void CSelfTimeCp::ApplyServerBest(int TimeCpIndex, float BestTime)
{
	if(TimeCpIndex < 0 || TimeCpIndex >= MAX_CHECKPOINTS)
		return;
	if(!HasValidBest(BestTime))
		return;

	m_aServerBestTimeCp[TimeCpIndex] = BestTime;
	m_aHasServerBestTimeCp[TimeCpIndex] = true;

	for(SCheckpoint &Checkpoint : m_vCheckpoints)
	{
		if(Checkpoint.m_LinkedTimeCpIndex == TimeCpIndex)
			Checkpoint.m_BestTime = BestTime;
	}
}

float CSelfTimeCp::KnownPersonalBestSeconds() const
{
	const int LocalId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return -1.0f;

	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[LocalId];
		if(ClientData.m_FinishTimeSeconds != FinishTime::UNSET && ClientData.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
			return (float)absolute(ClientData.m_FinishTimeSeconds) + (absolute(ClientData.m_FinishTimeMillis) % 1000) / 1000.0f;
	}

	return -1.0f;
}

void CSelfTimeCp::SyncBestFinishFromRecord()
{
	const float PersonalBest = KnownPersonalBestSeconds();
	if(PersonalBest <= 0.0f)
		return;

	if(m_BestFinishTime < 0.0f || PersonalBest < m_BestFinishTime)
		m_BestFinishTime = PersonalBest;
}

int CSelfTimeCp::CurrentRaceStartTick() const
{
	return GameClient()->LastRaceTick();
}

float CSelfTimeCp::CurrentRaceTimeSeconds() const
{
	const int StartTick = CurrentRaceStartTick();
	if(StartTick < 0)
		return -1.0f;

	return maximum(0.0f, (Client()->GameTick(g_Config.m_ClDummy) - StartTick) / (float)maximum(1, Client()->GameTickSpeed()));
}

float CSelfTimeCp::AlignedRaceTimeSeconds() const
{
	const float LocalTime = CurrentRaceTimeSeconds();
	if(LocalTime < 0.0f)
		return -1.0f;
	if(!m_HasRaceTimeOffset)
		return LocalTime;
	return maximum(0.0f, LocalTime + m_RaceTimeOffset);
}

void CSelfTimeCp::ResetCurrentRun(int RaceTick)
{
	m_CurrentRaceTick = RaceTick;
	m_HasLastPos = false;
	m_LastHitTimeCpIndex = -1;
	for(SCheckpoint &Checkpoint : m_vCheckpoints)
	{
		Checkpoint.m_CurrentRunTime = -1.0f;
		Checkpoint.m_LastTouchedRaceTick = -1;
	}
}

bool CSelfTimeCp::IsTouchingCheckpoint(const SCheckpoint &Checkpoint, vec2 PrevPos, vec2 Pos) const
{
	const float Left = Checkpoint.m_TileX * 32.0f;
	const float Right = Left + 32.0f;
	const float Top = Checkpoint.m_TileYTop * 32.0f;
	const float Bottom = (Checkpoint.m_TileYBottom + 1) * 32.0f;

	if(Pos.y < Top || Pos.y > Bottom)
		return false;

	if(Pos.x >= Left && Pos.x <= Right)
		return true;

	const float MinX = minimum(PrevPos.x, Pos.x);
	const float MaxX = maximum(PrevPos.x, Pos.x);
	return MinX <= Right && MaxX >= Left;
}

void CSelfTimeCp::UpdateTouches()
{
	if(!Enabled() || m_vCheckpoints.empty() || Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalCharacter)
		return;

	SyncBestFinishFromRecord();

	const int RaceTick = CurrentRaceStartTick();
	if(RaceTick < 0)
	{
		if(m_CurrentRaceTick != -1)
			ResetCurrentRun(-1);
		return;
	}

	if(m_CurrentRaceTick != RaceTick)
		ResetCurrentRun(RaceTick);

	const vec2 Pos = GameClient()->m_LocalCharacterPos;
	const vec2 PrevPos = m_HasLastPos ? m_LastPos : Pos;
	m_LastPos = Pos;
	m_HasLastPos = true;

	const int HitTimeCp = DetectTimeCpHit(PrevPos, Pos);
	if(HitTimeCp >= 0)
		m_LastHitTimeCpIndex = HitTimeCp;

	const float Time = AlignedRaceTimeSeconds();
	if(Time < 0.0f)
		return;

	for(SCheckpoint &Checkpoint : m_vCheckpoints)
	{
		if(Checkpoint.m_LastTouchedRaceTick == RaceTick)
			continue;
		if(!IsTouchingCheckpoint(Checkpoint, PrevPos, Pos))
			continue;

		Checkpoint.m_CurrentRunTime = Time;
		Checkpoint.m_LastTouchedRaceTick = RaceTick;

		// Real TimeCP logic: Diff = current - PB split. Never show raw current time.
		TrySeedBestFromServer(Checkpoint);
		if(!HasValidBest(Checkpoint.m_BestTime) && Checkpoint.m_LinkedTimeCpIndex < 0)
			TrySeedBestFromGhost(Checkpoint);

		if(HasValidBest(Checkpoint.m_BestTime))
		{
			GameClient()->m_Hud.ShowSelfTimeCpDiff(Time - Checkpoint.m_BestTime);
		}
		else if(Checkpoint.m_LinkedTimeCpIndex < 0 && Time > 0.0f)
		{
			// Unlinked only: remember this split silently; show from the next run.
			Checkpoint.m_BestTime = Time;
		}
		// Linked without server PB split yet: wait for Sv_DDRaceTime.
	}
}

void CSelfTimeCp::CommitBestFromRun()
{
	for(SCheckpoint &Checkpoint : m_vCheckpoints)
	{
		// Keep server-linked baselines; only update purely personal strips.
		if(Checkpoint.m_LinkedTimeCpIndex >= 0)
			continue;
		if(HasValidBest(Checkpoint.m_CurrentRunTime))
			Checkpoint.m_BestTime = Checkpoint.m_CurrentRunTime;
	}
}

void CSelfTimeCp::OnUpdate()
{
	if(Enabled())
		SyncBestFinishFromRecord();
	UpdateTouches();
}

void CSelfTimeCp::OnRender()
{
	if(!Enabled() || m_vCheckpoints.empty() || Client()->State() != IClient::STATE_ONLINE)
		return;

	float aPoints[4];
	Graphics()->MapScreenToWorld(
		GameClient()->m_Camera.m_Center.x, GameClient()->m_Camera.m_Center.y,
		100.0f, 100.0f, 100.0f, 0, 0,
		Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
	Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);

	ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_BcSelfTimeCpColor, true));
	Color.a = std::min(Color.a, 0.45f);
	const ColorRGBA EdgeColor = Color.WithAlpha(std::min(Color.a + 0.25f, 0.75f));

	for(const SCheckpoint &Checkpoint : m_vCheckpoints)
	{
		const float X = Checkpoint.m_TileX * 32.0f;
		for(int TileY = Checkpoint.m_TileYTop; TileY <= Checkpoint.m_TileYBottom; ++TileY)
		{
			const float Y = TileY * 32.0f;
			Graphics()->DrawRect(X + 3.0f, Y + 3.0f, 26.0f, 26.0f, Color, IGraphics::CORNER_NONE, 0.0f);
			Graphics()->DrawRect(X + 13.0f, Y + 6.0f, 6.0f, 20.0f, EdgeColor, IGraphics::CORNER_NONE, 0.0f);
		}
	}
}

void CSelfTimeCp::OnMessage(int MsgType, void *pRawMsg)
{
	if(!Enabled())
		return;

	if(MsgType == NETMSGTYPE_SV_DDRACETIME || MsgType == NETMSGTYPE_SV_DDRACETIMELEGACY)
	{
		CNetMsg_Sv_DDRaceTime *pMsg = (CNetMsg_Sv_DDRaceTime *)pRawMsg;
		if(pMsg->m_Finish != 0 || pMsg->m_Time <= 0)
			return;

		const float ServerTime = pMsg->m_Time / 100.0f;
		const float Diff = pMsg->m_Check / 100.0f;
		const float BestTime = ServerTime - Diff;

		const float LocalTime = CurrentRaceTimeSeconds();
		if(LocalTime >= 0.0f)
		{
			m_RaceTimeOffset = ServerTime - LocalTime;
			m_HasRaceTimeOffset = true;
		}

		if(m_LastHitTimeCpIndex >= 0 && HasValidBest(BestTime))
			ApplyServerBest(m_LastHitTimeCpIndex, BestTime);

		// Linked personal CPs use the exact same Diff as the real TimeCP.
		for(SCheckpoint &Checkpoint : m_vCheckpoints)
		{
			if(Checkpoint.m_LinkedTimeCpIndex != m_LastHitTimeCpIndex)
				continue;
			if(!HasValidBest(BestTime))
				continue;

			Checkpoint.m_BestTime = BestTime;
			Checkpoint.m_CurrentRunTime = ServerTime;
			Checkpoint.m_LastTouchedRaceTick = m_CurrentRaceTick;
			GameClient()->m_Hud.ShowSelfTimeCpDiff(Diff);
		}
		return;
	}

	if(MsgType == NETMSGTYPE_SV_RECORD || MsgType == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_Record *pMsg = (CNetMsg_Sv_Record *)pRawMsg;
		if(MsgType == NETMSGTYPE_SV_RECORD || GameClient()->m_GameInfo.m_RaceRecordMessage)
		{
			if(pMsg->m_PlayerTimeBest > 0)
			{
				const float PersonalBest = pMsg->m_PlayerTimeBest / 100.0f;
				if(m_BestFinishTime < 0.0f || PersonalBest < m_BestFinishTime)
					m_BestFinishTime = PersonalBest;
			}
		}
		return;
	}

	if(MsgType != NETMSGTYPE_SV_RACEFINISH)
		return;

	CNetMsg_Sv_RaceFinish *pMsg = (CNetMsg_Sv_RaceFinish *)pRawMsg;
	const int LocalClientId = GameClient()->m_aLocalIds[g_Config.m_ClDummy];
	if(pMsg->m_ClientId != LocalClientId || pMsg->m_Time <= 0)
		return;

	SyncBestFinishFromRecord();

	const float FinishTime = pMsg->m_Time / 1000.0f;
	if(m_BestFinishTime < 0.0f || FinishTime < m_BestFinishTime)
	{
		m_BestFinishTime = FinishTime;
		CommitBestFromRun();
	}
}
