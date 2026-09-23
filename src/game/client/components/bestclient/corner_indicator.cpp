/* Copyright © 2026 BestProject Team */
#include "corner_indicator.h"

#include <base/color.h>
#include <base/math.h>
#include <base/vmath.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <algorithm>

namespace
{
bool IsFrozen(const CGameClient::CClientData &Client, int CurTick)
{
	return Client.m_DeepFrozen || Client.m_LiveFrozen || (Client.m_FreezeEnd != 0 && Client.m_FreezeEnd > CurTick);
}

bool RenderLivePlayerIcon(CGameClient *pGameClient, int ClientId, vec2 Pos, float Size, float Alpha, const CTeeRenderInfo *pRenderInfoOverride)
{
	if(!in_range(ClientId, MAX_CLIENTS - 1) || !pGameClient->m_Snap.m_apPlayerInfos[ClientId] || !pGameClient->m_Snap.m_aCharacters[ClientId].m_Active)
		return false;

	CTeeRenderInfo RenderInfo = pRenderInfoOverride ? *pRenderInfoOverride : pGameClient->m_aClients[ClientId].m_RenderInfo;
	RenderInfo.m_Size = Size;
	const CNetObj_Character &Player = pGameClient->m_aClients[ClientId].m_RenderCur;
	const CNetObj_Character &Prev = pGameClient->m_aClients[ClientId].m_RenderPrev;
	const float Intra = pGameClient->m_aClients[ClientId].m_IsPredicted ? pGameClient->Client()->PredIntraGameTick(g_Config.m_ClDummy) : pGameClient->Client()->IntraGameTick(g_Config.m_ClDummy);
	const float Angle = pGameClient->m_Players.GetPlayerTargetAngle(&Prev, &Player, ClientId, Intra);
	RenderInfo.m_GotAirJump = !(Player.m_Jumped & 2);
	RenderInfo.m_FeetFlipped = false;
	pGameClient->RenderTools()->RenderTee(CAnimState::GetIdle(), &RenderInfo, Player.m_Emote, direction(Angle), Pos, Alpha);
	return true;
}
}

void CornerIndicator::Render(CGameClient *pGameClient)
{
	if(!pGameClient || !g_Config.m_BcCornerIndicator)
		return;

	const int LocalId = pGameClient->m_Snap.m_LocalClientId;
	if(LocalId < 0 || pGameClient->m_Snap.m_SpecInfo.m_Active || !pGameClient->m_Snap.m_aCharacters[LocalId].m_Active)
		return;

	const int LocalTeam = pGameClient->m_Teams.Team(LocalId);
	if(LocalTeam >= TEAM_SUPER || (g_Config.m_BcCornerIndicatorOnlyInTeam && LocalTeam <= TEAM_FLOCK))
		return;

	const int CurTick = pGameClient->Client()->GameTick(g_Config.m_ClDummy);
	const bool LocalAlive = !IsFrozen(pGameClient->m_aClients[LocalId], CurTick);

	struct SIndicatorEntry
	{
		int m_ClientId;
		vec2 m_Pos;
		bool m_Frozen;
	};
	SIndicatorEntry aIndicators[MAX_CLIENTS];
	int Num = 0;
	bool AnyOtherAlive = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == LocalId || !pGameClient->m_Snap.m_apPlayerInfos[i] || pGameClient->m_Teams.Team(i) != LocalTeam)
			continue;
		if(!pGameClient->m_Snap.m_aCharacters[i].m_Active)
			continue;

		SIndicatorEntry Entry;
		Entry.m_ClientId = i;
		Entry.m_Pos = pGameClient->m_aClients[i].m_RenderPos;
		Entry.m_Frozen = IsFrozen(pGameClient->m_aClients[i], CurTick);
		if(!Entry.m_Frozen)
			AnyOtherAlive = true;
		aIndicators[Num++] = Entry;
	}
	if(Num == 0)
		return;

	const bool LastManStanding = LocalAlive && !AnyOtherAlive;
	IGraphics *pGraphics = pGameClient->Graphics();
	const CScreenRect PreviousScreen = pGraphics->GetScreen();
	const vec2 Center = pGameClient->m_Camera.m_Center;
	const float CamZoom = pGameClient->m_Camera.m_Zoom;
	const CScreenRect World = pGraphics->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, pGraphics->ScreenAspect(), CamZoom);
	pGraphics->MapScreen(World);

	const float ScreenL = World.m_TopLeft.x;
	const float ScreenT = World.m_TopLeft.y;
	const float ScreenR = World.m_BottomRight.x;
	const float ScreenB = World.m_BottomRight.y;
	const float ScreenCx = (ScreenL + ScreenR) * 0.5f;
	const float ScreenCy = (ScreenT + ScreenB) * 0.5f;
	const float ScreenHalfW = (ScreenR - ScreenL) * 0.5f;
	const float ScreenHalfH = (ScreenB - ScreenT) * 0.5f;
	const float SizeAlive = (float)g_Config.m_BcCornerIndicatorSizeAlive * CamZoom;
	const float SizeFrozen = (float)g_Config.m_BcCornerIndicatorSizeFrozen * CamZoom;
	const float SizeLastTeammate = (float)g_Config.m_BcCornerIndicatorLastTeammateSize * CamZoom;
	const float Margin = std::max(SizeAlive, std::max(SizeFrozen, SizeLastTeammate)) * 0.55f;
	const float MaxX = ScreenHalfW - Margin;
	const float MaxY = ScreenHalfH - Margin;
	if(MaxX <= 1.0f || MaxY <= 1.0f)
	{
		pGraphics->MapScreen(PreviousScreen);
		return;
	}

	const bool CustomColors = g_Config.m_BcCornerIndicatorCustomColors != 0;
	for(int i = 0; i < Num; i++)
	{
		const vec2 PlayerPos = aIndicators[i].m_Pos;
		if(PlayerPos.x > ScreenL + Margin && PlayerPos.x < ScreenR - Margin && PlayerPos.y > ScreenT + Margin && PlayerPos.y < ScreenB - Margin)
			continue;

		const vec2 Delta = PlayerPos - vec2(ScreenCx, ScreenCy);
		if(Delta.x == 0.0f && Delta.y == 0.0f)
			continue;

		const vec2 Dir = normalize(Delta);
		const float Tx = Dir.x == 0.0f ? 1e30f : MaxX / absolute(Dir.x);
		const float Ty = Dir.y == 0.0f ? 1e30f : MaxY / absolute(Dir.y);
		const float DistanceToEdge = std::min(Tx, Ty);
		const float IconX = ScreenCx + Dir.x * DistanceToEdge;
		const float IconY = ScreenCy + Dir.y * DistanceToEdge;

		float IconSize;
		unsigned ColorConfig = 0;
		if(!aIndicators[i].m_Frozen)
		{
			if(!g_Config.m_BcCornerIndicatorShowAlive)
				continue;
			IconSize = SizeAlive;
			ColorConfig = g_Config.m_BcCornerIndicatorColorAlive;
		}
		else if(LastManStanding)
		{
			if(!g_Config.m_BcCornerIndicatorIndicateLastTeammate)
				continue;
			IconSize = SizeLastTeammate;
			ColorConfig = g_Config.m_BcCornerIndicatorLastTeammateColor;
		}
		else
		{
			if(!g_Config.m_BcCornerIndicatorShowFrozen)
				continue;
			IconSize = SizeFrozen;
			ColorConfig = g_Config.m_BcCornerIndicatorColorFrozen;
		}

		if(g_Config.m_BcCornerIndicatorProximity && g_Config.m_BcCornerIndicatorProximityRange > 1)
		{
			const float EdgeDistance = std::min(ScreenHalfW, ScreenHalfH);
			const float BeyondEdge = std::max(0.0f, distance(vec2(ScreenCx, ScreenCy), PlayerPos) - EdgeDistance);
			float Amount = std::min(1.0f, BeyondEdge / (float)g_Config.m_BcCornerIndicatorProximityRange);
			Amount *= Amount;
			const float MinSize = (float)g_Config.m_BcCornerIndicatorProximityMinSize * CamZoom;
			IconSize += (MinSize - IconSize) * Amount;
		}

		const int ClientId = aIndicators[i].m_ClientId;
		CTeeRenderInfo TeeInfo = pGameClient->m_aClients[ClientId].m_RenderInfo;
		if(g_Config.m_BcCornerIndicatorNinjaFrozen && aIndicators[i].m_Frozen)
		{
			const auto &pNinja = pGameClient->m_Players.NinjaTeeRenderInfo();
			if(pNinja)
				TeeInfo = pNinja->TeeRenderInfo();
		}
		TeeInfo.m_Size = IconSize;
		if(CustomColors)
		{
			const ColorRGBA Tint = color_cast<ColorRGBA>(ColorHSLA(ColorConfig, true)).WithAlpha(1.0f);
			TeeInfo.m_ColorBody = Tint;
			TeeInfo.m_ColorFeet = Tint;
		}
		if(aIndicators[i].m_Frozen)
			TeeInfo.m_TeeRenderFlags |= TEE_NO_WEAPON;

		const CAnimState *pIdleState = CAnimState::GetIdle();
		vec2 OffsetToMid;
		CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
		const vec2 Pos(IconX, IconY + OffsetToMid.y);
		if(!RenderLivePlayerIcon(pGameClient, ClientId, Pos, IconSize, 0.95f, &TeeInfo))
			pGameClient->RenderTools()->RenderTee(pIdleState, &TeeInfo, aIndicators[i].m_Frozen ? EMOTE_PAIN : EMOTE_NORMAL, -Dir, Pos, 0.95f);
	}
	pGraphics->MapScreen(PreviousScreen);
}
