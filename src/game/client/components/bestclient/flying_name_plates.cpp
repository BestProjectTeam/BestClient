/* Copyright © 2026 BestProject Team */
#include "flying_name_plates.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

void CFlyingNamePlates::OnReset()
{
	for(auto &State : m_aStates)
		State = CState();
}

vec2 CFlyingNamePlates::AnchorPos(vec2 TeePos)
{
	return TeePos;
}

void CFlyingNamePlates::RenderLine(CGameClient &This, vec2 Anchor, vec2 NamePlatePos, ColorRGBA Color)
{
	if(distance(Anchor, NamePlatePos) < 4.0f)
		return;

	This.Graphics()->TextureClear();
	This.Graphics()->LinesBegin();
	This.Graphics()->SetColor(ColorRGBA(Color.r, Color.g, Color.b, std::clamp(Color.a * 0.75f, 0.0f, 0.85f)));
	const IGraphics::CLineItem Line(Anchor, NamePlatePos);
	This.Graphics()->LinesDraw(&Line, 1);
	This.Graphics()->LinesEnd();
	This.Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}

ColorRGBA CFlyingNamePlates::ColorForPlayer(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha) const
{
	const auto &ClientData = GameClient()->m_aClients[pPlayerInfo->m_ClientId];
	const bool OtherTeam = GameClient()->IsOtherTeam(pPlayerInfo->m_ClientId);

	if(g_Config.m_ClNamePlatesAlways == 0)
		Alpha *= std::clamp(1.0f - std::pow(distance(GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy], Position) / 200.0f, 16.0f), 0.0f, 1.0f);
	if(OtherTeam)
		Alpha *= (float)g_Config.m_ClShowOthersAlpha / 100.0f;
	if(GameClient()->m_FastPractice.Enabled() && !GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_FastPractice.IsPracticeParticipant(pPlayerInfo->m_ClientId))
		Alpha = std::min(Alpha, 0.5f);

	ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f);
	if(g_Config.m_ClNamePlatesTeamcolors)
	{
		if(GameClient()->IsTeamPlay())
		{
			if(ClientData.m_Team == TEAM_RED)
				Color = ColorRGBA(1.0f, 0.5f, 0.5f);
			else if(ClientData.m_Team == TEAM_BLUE)
				Color = ColorRGBA(0.7f, 0.7f, 1.0f);
		}
		else
		{
			const int Team = GameClient()->m_Teams.Team(pPlayerInfo->m_ClientId);
			if(Team)
				Color = GameClient()->GetDDTeamColor(Team, 0.75f);
		}
	}
	Color.a = Alpha;
	return Color;
}

void CFlyingNamePlates::Update(int ClientId, vec2 Position)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	auto &FlyingState = m_aStates[ClientId];
	const float Now = Client()->GlobalTime();
	if(FlyingState.m_LastUpdateTime == Now)
		return;

	const vec2 DefaultRenderPos = Position - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset);
	if(!g_Config.m_BcFlyingNamePlates)
	{
		FlyingState.m_CurrentPos = DefaultRenderPos;
		FlyingState.m_PrevPlayerPos = Position;
		FlyingState.m_Initialized = false;
		FlyingState.m_LastUpdateTime = Now;
		return;
	}

	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	const vec2 PlayerDelta = Position - FlyingState.m_PrevPlayerPos;
	const bool ResetState = !FlyingState.m_Initialized || distance(Position, FlyingState.m_PrevPlayerPos) > 256.0f;

	vec2 DragOffset = vec2(0.0f, 0.0f);
	if(!ResetState && Delta > 0.0001f)
	{
		const vec2 PlayerVelocity = PlayerDelta / Delta;
		const float Speed = length(PlayerVelocity);
		if(Speed > 0.001f)
		{
			const float DragScale = std::clamp(Speed / 1200.0f, 0.0f, 1.0f);
			DragOffset = normalize(PlayerVelocity) * ((float)g_Config.m_BcFlyingNamePlatesDrag * DragScale);
		}
	}

	const vec2 TargetPos = DefaultRenderPos - vec2(0.0f, (float)g_Config.m_BcFlyingNamePlatesLift) - DragOffset;
	if(ResetState)
	{
		FlyingState.m_CurrentPos = TargetPos;
	}
	else
	{
		const float FollowSpeed = 2.5f + (float)g_Config.m_BcFlyingNamePlatesFollow * 0.25f;
		FlyingState.m_CurrentPos += (TargetPos - FlyingState.m_CurrentPos) * std::min(Delta * FollowSpeed, 1.0f);

		const vec2 Anchor = AnchorPos(Position);
		const vec2 RopeDelta = FlyingState.m_CurrentPos - Anchor;
		const float RopeLen = length(RopeDelta);
		const float MaxRopeLen = std::max(24.0f, (float)g_Config.m_ClNamePlatesOffset + (float)g_Config.m_BcFlyingNamePlatesLift + (float)g_Config.m_BcFlyingNamePlatesDrag * 1.2f);
		if(RopeLen > MaxRopeLen && RopeLen > 0.001f)
			FlyingState.m_CurrentPos = Anchor + RopeDelta * (MaxRopeLen / RopeLen);
	}

	FlyingState.m_PrevPlayerPos = Position;
	FlyingState.m_Initialized = true;
	FlyingState.m_LastUpdateTime = Now;
}

void CFlyingNamePlates::RenderRopeGame(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha)
{
	if(!pPlayerInfo)
		return;
	if(!g_Config.m_BcFlyingNamePlates)
		return;
	if(g_Config.m_BcFlyingNamePlatesHideLine)
		return;
	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideNames)
		return;
	if(!(pPlayerInfo->m_Local ? g_Config.m_ClNamePlatesOwn : g_Config.m_ClNamePlates))
		return;

	Update(pPlayerInfo->m_ClientId, Position);
	const vec2 Anchor = AnchorPos(Position);
	const vec2 NamePlatePos = m_aStates[pPlayerInfo->m_ClientId].m_CurrentPos;
	if(!GameClient()->OptimizerAllowRenderPos(Anchor) || !GameClient()->OptimizerAllowRenderPos(NamePlatePos))
		return;

	RenderLine(*GameClient(), Anchor, NamePlatePos, ColorForPlayer(Position, pPlayerInfo, Alpha));
}

vec2 CFlyingNamePlates::GetRenderPos(int ClientId, vec2 TeePos) const
{
	const vec2 DefaultRenderPos = TeePos - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset);
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return DefaultRenderPos;
	if(!g_Config.m_BcFlyingNamePlates)
		return DefaultRenderPos;
	const auto &FlyingState = m_aStates[ClientId];
	if(!FlyingState.m_Initialized)
		return DefaultRenderPos;
	return FlyingState.m_CurrentPos;
}

vec2 CFlyingNamePlates::PreviewPos(vec2 NamePlateBasePos, vec2 TeeDirection) const
{
	return NamePlateBasePos - vec2(0.0f, (float)g_Config.m_BcFlyingNamePlatesLift) - TeeDirection * ((float)g_Config.m_BcFlyingNamePlatesDrag * 0.35f);
}

void CFlyingNamePlates::RenderPreviewRope(vec2 TeePos, vec2 FlyingPos, ColorRGBA Color) const
{
	if(g_Config.m_BcFlyingNamePlatesHideLine)
		return;
	RenderLine(*GameClient(), AnchorPos(TeePos), FlyingPos, Color);
}
