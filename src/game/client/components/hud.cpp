/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "hud.h"

#include "binds.h"
#include "camera.h"
#include "controls.h"
#include "hud_layout.h" // bestclient
#include "voting.h"

#include <base/color.h>
#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/bestclient/gradient.h> // bestclient
#include <game/client/components/bestclient/hud_extras.h> // bestclient
#include <game/client/components/bestclient/corner_indicator.h> // bestclient
#include <game/client/components/bestclient/cursor_trail.h> // bestclient
#include <game/client/components/scoreboard.h>
#include <game/client/components/skins.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/layers.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
	// bestclient
	CUIRect PushHudRectFromMusicPlayer(CGameClient *pGameClient, CUIRect Rect, float HudWidth, float HudHeight, bool ForcePreview)
	{
		if(ForcePreview || pGameClient == nullptr || Rect.w <= 0.0f || Rect.h <= 0.0f)
			return Rect;

		const vec2 Offset = pGameClient->m_MusicPlayer.GetHudPushOffsetForRect(Rect, HudWidth, HudHeight, 2.0f);
		Rect.x += Offset.x;
		Rect.y += Offset.y;
		return Rect;
	}

	float GetFrozenHudScale(const HudLayout::SModuleLayout &Layout)
	{
		return std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	}

	enum
	{
		FROZEN_HUD_EXPAND_RIGHT = 0,
		FROZEN_HUD_EXPAND_LEFT,
		FROZEN_HUD_EXPAND_CENTER,
	};

	int FrozenHudExpandDir()
	{
		return std::clamp(g_Config.m_TcFrozenHudExpandDir, (int)FROZEN_HUD_EXPAND_RIGHT, (int)FROZEN_HUD_EXPAND_CENTER);
	}
	// bestclient
}

CHud::CHud()
{
	m_FPSTextContainerIndex.Reset();
	m_DDRaceEffectsTextContainerIndex.Reset();
	m_PlayerAngleTextContainerIndex.Reset();
	m_PlayerPrevAngle = -INFINITY;

	for(int i = 0; i < 2; i++)
	{
		m_aPlayerSpeedTextContainers[i].Reset();
		m_aPlayerPrevSpeed[i] = -INFINITY;
		m_aPlayerPositionContainers[i].Reset();
		m_aPlayerPrevPosition[i] = -INFINITY;
	}
}

void CHud::ResetHudContainers()
{
	for(auto &ScoreInfo : m_aScoreInfo)
	{
		TextRender()->DeleteTextContainer(ScoreInfo.m_OptionalNameTextContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextRankContainerIndex);
		TextRender()->DeleteTextContainer(ScoreInfo.m_TextScoreContainerIndex);
		Graphics()->DeleteQuadContainer(ScoreInfo.m_RoundRectQuadContainerIndex);

		ScoreInfo.Reset();
	}

	TextRender()->DeleteTextContainer(m_FPSTextContainerIndex);
	TextRender()->DeleteTextContainer(m_DDRaceEffectsTextContainerIndex);
	TextRender()->DeleteTextContainer(m_PlayerAngleTextContainerIndex);
	m_PlayerPrevAngle = -INFINITY;
	for(int i = 0; i < 2; i++)
	{
		TextRender()->DeleteTextContainer(m_aPlayerSpeedTextContainers[i]);
		m_aPlayerPrevSpeed[i] = -INFINITY;
		TextRender()->DeleteTextContainer(m_aPlayerPositionContainers[i]);
		m_aPlayerPrevPosition[i] = -INFINITY;
	}
}

void CHud::OnWindowResize()
{
	ResetHudContainers();
}

void CHud::OnReset()
{
	m_TimeCpDiff = 0.0f;
	m_DDRaceTime = 0;
	m_FinishTimeLastReceivedTick = 0;
	m_TimeCpLastReceivedTick = 0;
	m_ShowFinishTime = false;
	m_aPlayerRecord[0] = -1.0f;
	m_aPlayerRecord[1] = -1.0f;
	m_aPlayerSpeed[0] = 0;
	m_aPlayerSpeed[1] = 0;
	m_aLastPlayerSpeedChange[0] = ESpeedChange::NONE;
	m_aLastPlayerSpeedChange[1] = ESpeedChange::NONE;
	m_LastSpectatorCountTick = 0;

	ResetHudContainers();
}

void CHud::OnInit()
{
	OnReset();

	Graphics()->SetColor(1.0, 1.0, 1.0, 1.0);

	m_HudQuadContainerIndex = Graphics()->CreateQuadContainer(false);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	PrepareAmmoHealthAndArmorQuads();

	// all cursors for the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		float ScaleX, ScaleY;
		Graphics()->GetSpriteScale(g_pData->m_Weapons.m_aId[i].m_pSpriteCursor, ScaleX, ScaleY);
		m_aCursorOffset[i] = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 64.f * ScaleX, 64.f * ScaleY);
	}

	// the flags
	m_FlagOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 8.f, 16.f);

	PreparePlayerStateQuads();

	Graphics()->QuadContainerUpload(m_HudQuadContainerIndex);
}

void CHud::RenderGameTimer()
{
	// bestclient
	const bool MusicPlayerOccupiesTimerSlot = g_Config.m_BcMusicPlayer != 0 && !(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideSongPlayer);
	if(MusicPlayerOccupiesTimerSlot)
		return;
	// bestclient

	float Half = m_Width / 2.0f;

	if(!(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH))
	{
		char aBuf[32];
		int Time = 0;
		if(GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit && (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
		{
			Time = GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit * 60 - ((Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed());

			if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
				Time = 0;
		}
		else if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME)
		{
			// The Warmup timer is negative in this case to make sure that incompatible clients will not see a warmup timer
			Time = (Client()->GameTick(g_Config.m_ClDummy) + GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer) / Client()->GameTickSpeed();
		}
		else
		{
			Time = (Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_Snap.m_pGameInfoObj->m_RoundStartTick) / Client()->GameTickSpeed();
		}

		str_time((int64_t)Time * 100, ETimeFormat::DAYS, aBuf, sizeof(aBuf));
		float FontSize = 10.0f;
		static float s_TextWidthM = TextRender()->TextWidth(FontSize, "00:00", -1, -1.0f);
		static float s_TextWidthH = TextRender()->TextWidth(FontSize, "00:00:00", -1, -1.0f);
		static float s_TextWidth0D = TextRender()->TextWidth(FontSize, "0d 00:00:00", -1, -1.0f);
		static float s_TextWidth00D = TextRender()->TextWidth(FontSize, "00d 00:00:00", -1, -1.0f);
		static float s_TextWidth000D = TextRender()->TextWidth(FontSize, "000d 00:00:00", -1, -1.0f);
		float w = Time >= 3600 * 24 * 100 ? s_TextWidth000D : (Time >= 3600 * 24 * 10 ? s_TextWidth00D : (Time >= 3600 * 24 ? s_TextWidth0D : (Time >= 3600 ? s_TextWidthH : s_TextWidthM)));
		// last 60 sec red, last 10 sec blink
		if(GameClient()->m_Snap.m_pGameInfoObj->m_TimeLimit && Time <= 60 && (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0))
		{
			float Alpha = Time <= 10 && (2 * time() / time_freq()) % 2 ? 0.5f : 1.0f;
			TextRender()->TextColor(1.0f, 0.25f, 0.25f, Alpha);
		}
		TextRender()->Text(Half - w / 2, 2, FontSize, aBuf, -1.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CHud::RenderPauseNotification()
{
	if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED &&
		!(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
	{
		const char *pText = Localize("Game paused");
		float FontSize = 20.0f;
		float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text(150.0f * Graphics()->ScreenAspect() + -w / 2.0f, 50.0f, FontSize, pText, -1.0f);
	}
}

void CHud::RenderSuddenDeath()
{
	if(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_SUDDENDEATH)
	{
		float Half = m_Width / 2.0f;
		const char *pText = Localize("Sudden Death");
		float FontSize = 12.0f;
		float w = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
		TextRender()->Text(Half - w / 2, 2, FontSize, pText, -1.0f);
	}
}

void CHud::RenderScoreHud(bool ForcePreview)
{
	// bestclient
	if(!HudLayout::IsEnabled(HudLayout::MODULE_SCORE))
		return;
	if(!ForcePreview && !g_Config.m_ClShowhudScore)
		return;
	if(!ForcePreview && GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		return;
	// bestclient

	const CUIRect Rect = GetScoreHudRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_SCORE, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_SCORE);
	const float RightEdge = Rect.x + Rect.w;
	const float BaseY = Rect.y;
	const float ScoreSingleBoxHeight = 18.0f * Scale;
	const float ScoreTextSize = 14.0f * Scale;
	const float NameTextSize = 8.0f * Scale;
	const float RankTextSize = 10.0f * Scale;
	const float RowStep = 28.0f * Scale;
	const float Split = 3.0f * Scale;
	const float Rounding = 5.0f * Scale;

	auto DrawScoreBox = [&](float X, float Y, float W, float H, const ColorRGBA &Color) {
		const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, X, Y, W, H, m_Width, m_Height);
		Graphics()->TextureClear();
		Graphics()->DrawRect(X, Y, W, H, Color.WithMultipliedAlpha(ModuleAlpha), Corners, Rounding);
	};

	auto RenderPlayerName = [&](int Id, float X, float Y, float Size, const char *pName) {
		CTextCursor Cursor;
		Cursor.SetPosition(vec2(X, Y));
		Cursor.m_FontSize = Size;
		if(Id >= 0 && Id < MAX_CLIENTS && g_Config.m_BcNameplateGradient && CBcGradient::AppliesTo(Id, GameClient()))
		{
			const float Phase = CBcGradient::AnimatePhase(Client()->GlobalTime());
			const std::vector<STextColorSplit> vGradientSplits = CBcGradient::BuildAnimatedTextSplits(pName, Id, GameClient(), Phase);
			if(!vGradientSplits.empty())
			{
				for(const STextColorSplit &GradientSplit : vGradientSplits)
					Cursor.m_vColorSplits.emplace_back(Cursor.m_CharCount + GradientSplit.m_CharIndex, GradientSplit.m_Length, GradientSplit.m_Color);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
		else
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->TextEx(&Cursor, pName);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	};

	if(GameClient()->IsTeamPlay() && GameClient()->m_Snap.m_pGameDataObj)
	{
		char aScoreTeam[2][16];
		str_format(aScoreTeam[TEAM_RED], sizeof(aScoreTeam[TEAM_RED]), "%d", GameClient()->m_Snap.m_pGameDataObj->m_TeamscoreRed);
		str_format(aScoreTeam[TEAM_BLUE], sizeof(aScoreTeam[TEAM_BLUE]), "%d", GameClient()->m_Snap.m_pGameDataObj->m_TeamscoreBlue);

		const float aScoreTextWidth[2] = {
			TextRender()->TextWidth(ScoreTextSize, aScoreTeam[TEAM_RED], -1, -1.0f),
			TextRender()->TextWidth(ScoreTextSize, aScoreTeam[TEAM_BLUE], -1, -1.0f)};
		const float ScoreWidthMax = std::max({aScoreTextWidth[0], aScoreTextWidth[1], TextRender()->TextWidth(ScoreTextSize, "100", -1, -1.0f)});
		const float ImageSize = (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) ? 16.0f * Scale : Split;
		const int aFlagCarrier[2] = {
			GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierRed,
			GameClient()->m_Snap.m_pGameDataObj->m_FlagCarrierBlue};

		for(int t = 0; t < 2; ++t)
		{
			const float RowY = BaseY + t * RowStep;
			const float BoxX = RightEdge - ScoreWidthMax - ImageSize - 2.0f * Split;
			const float BoxW = ScoreWidthMax + ImageSize + 2.0f * Split;
			DrawScoreBox(
				BoxX,
				RowY,
				BoxW,
				ScoreSingleBoxHeight,
				t == TEAM_RED ? ColorRGBA(0.975f, 0.17f, 0.17f, 0.3f) : ColorRGBA(0.17f, 0.46f, 0.975f, 0.3f));

			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			TextRender()->Text(
				RightEdge - ScoreWidthMax + (ScoreWidthMax - aScoreTextWidth[t]) / 2.0f - Split,
				RowY + (ScoreSingleBoxHeight - ScoreTextSize) / 2.0f,
				ScoreTextSize,
				aScoreTeam[t],
				-1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			if(GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS)
			{
				const int BlinkTimer = (GameClient()->m_aFlagDropTick[t] != 0 &&
							       (Client()->GameTick(g_Config.m_ClDummy) - GameClient()->m_aFlagDropTick[t]) / Client()->GameTickSpeed() >= 25) ?
							       10 :
							       20;
				if(aFlagCarrier[t] == FLAG_ATSTAND || (aFlagCarrier[t] == FLAG_TAKEN && ((Client()->GameTick(g_Config.m_ClDummy) / BlinkTimer) & 1)))
				{
					Graphics()->TextureSet(t == TEAM_RED ? GameClient()->m_GameSkin.m_SpriteFlagRed : GameClient()->m_GameSkin.m_SpriteFlagBlue);
					Graphics()->SetColor(1.f, 1.f, 1.f, 1.0f);
					Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_FlagOffset, RightEdge - ScoreWidthMax - ImageSize, RowY + 1.0f * Scale);
				}
				else if(aFlagCarrier[t] >= 0)
				{
					const int Id = aFlagCarrier[t] % MAX_CLIENTS;
					const char *pName = GameClient()->m_aClients[Id].m_aName;
					const float NameWidth = TextRender()->TextWidth(NameTextSize, pName, -1, -1.0f);
					RenderPlayerName(
						Id,
						std::min(RightEdge - NameWidth - 1.0f * Scale, BoxX),
						RowY + 20.0f * Scale - 2.0f * Scale,
						NameTextSize,
						pName);

					CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Id].m_RenderInfo;
					TeeInfo.m_Size = ScoreSingleBoxHeight;
					const CAnimState *pIdleState = CAnimState::GetIdle();
					vec2 OffsetToMid;
					CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
					const vec2 TeeRenderPos(RightEdge - ScoreWidthMax - TeeInfo.m_Size / 2.0f - Split, RowY + ScoreSingleBoxHeight / 2.0f + OffsetToMid.y);
					RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
				}
			}
		}
		return;
	}

	int Local = -1;
	int aPos[2] = {1, 2};
	const CNetObj_PlayerInfo *apPlayerInfo[2] = {nullptr, nullptr};
	int i = 0;
	for(int t = 0; t < 2 && i < MAX_CLIENTS && GameClient()->m_Snap.m_apInfoByScore[i]; ++i)
	{
		if(GameClient()->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
		{
			apPlayerInfo[t] = GameClient()->m_Snap.m_apInfoByScore[i];
			if(apPlayerInfo[t]->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
				Local = t;
			++t;
		}
	}
	if(Local == -1 && GameClient()->m_Snap.m_pLocalInfo && GameClient()->m_Snap.m_pLocalInfo->m_Team != TEAM_SPECTATORS)
	{
		for(; i < MAX_CLIENTS && GameClient()->m_Snap.m_apInfoByScore[i]; ++i)
		{
			if(GameClient()->m_Snap.m_apInfoByScore[i]->m_Team != TEAM_SPECTATORS)
				++aPos[1];
			if(GameClient()->m_Snap.m_apInfoByScore[i]->m_ClientId == GameClient()->m_Snap.m_LocalClientId)
			{
				apPlayerInfo[1] = GameClient()->m_Snap.m_apInfoByScore[i];
				Local = 1;
				break;
			}
		}
	}

	char aScore[2][16];
	for(int t = 0; t < 2; ++t)
	{
		if(apPlayerInfo[t])
		{
			if(Client()->IsSixup() && GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE)
				str_time(absolute(static_cast<int64_t>(apPlayerInfo[t]->m_Score)) / 10, ETimeFormat::MINS_CENTISECS, aScore[t], sizeof(aScore[t]));
			else if(GameClient()->m_GameInfo.m_TimeScore)
			{
				const CGameClient::CClientData &ClientData = GameClient()->m_aClients[apPlayerInfo[t]->m_ClientId];
				if(GameClient()->m_ReceivedDDNetPlayerFinishTimes && ClientData.m_FinishTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
				{
					const int64_t TimeMillis = static_cast<int64_t>(ClientData.m_FinishTimeSeconds) * 1000 + ClientData.m_FinishTimeMillis % 1000;
					str_time(TimeMillis / 10, ETimeFormat::HOURS, aScore[t], sizeof(aScore[t]));
				}
				else if(apPlayerInfo[t]->m_Score != FinishTime::NOT_FINISHED_TIMESCORE)
				{
					str_time(absolute(static_cast<int64_t>(apPlayerInfo[t]->m_Score)) * 100, ETimeFormat::HOURS, aScore[t], sizeof(aScore[t]));
				}
				else
					aScore[t][0] = 0;
			}
			else
				str_format(aScore[t], sizeof(aScore[t]), "%d", apPlayerInfo[t]->m_Score);
		}
		else
			aScore[t][0] = 0;
	}

	const float aScoreTextWidth[2] = {
		TextRender()->TextWidth(ScoreTextSize, aScore[0], -1, -1.0f),
		TextRender()->TextWidth(ScoreTextSize, aScore[1], -1, -1.0f)};
	const float ScoreWidthMax = std::max({aScoreTextWidth[0], aScoreTextWidth[1], TextRender()->TextWidth(ScoreTextSize, "10", -1, -1.0f)});
	const float ImageSize = 16.0f * Scale;
	const float PosSize = 16.0f * Scale;

	for(int t = 0; t < 2; ++t)
	{
		const float RowY = BaseY + t * RowStep;
		const float BoxX = RightEdge - ScoreWidthMax - ImageSize - 2.0f * Split - PosSize;
		const float BoxW = ScoreWidthMax + ImageSize + 2.0f * Split + PosSize;
		DrawScoreBox(
			BoxX,
			RowY,
			BoxW,
			ScoreSingleBoxHeight,
			t == Local ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f));

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->Text(
			RightEdge - ScoreWidthMax + (ScoreWidthMax - aScoreTextWidth[t]) - Split,
			RowY + (ScoreSingleBoxHeight - ScoreTextSize) / 2.0f,
			ScoreTextSize,
			aScore[t],
			-1.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		if(apPlayerInfo[t])
		{
			const int Id = apPlayerInfo[t]->m_ClientId;
			if(Id >= 0 && Id < MAX_CLIENTS)
			{
				const char *pName = GameClient()->m_aClients[Id].m_aName;
				const float NameWidth = TextRender()->TextWidth(NameTextSize, pName, -1, -1.0f);
				RenderPlayerName(
					Id,
					std::min(RightEdge - NameWidth - 1.0f * Scale, BoxX),
					RowY + 20.0f * Scale - 2.0f * Scale,
					NameTextSize,
					pName);

				CTeeRenderInfo TeeInfo = GameClient()->m_aClients[Id].m_RenderInfo;
				TeeInfo.m_Size = ScoreSingleBoxHeight;
				const CAnimState *pIdleState = CAnimState::GetIdle();
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
				const vec2 TeeRenderPos(RightEdge - ScoreWidthMax - TeeInfo.m_Size / 2.0f - Split, RowY + ScoreSingleBoxHeight / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(pIdleState, &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);
			}
		}

		char aBuf[16];
		str_format(aBuf, sizeof(aBuf), "%d.", aPos[t]);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->Text(
			RightEdge - ScoreWidthMax - ImageSize - Split - PosSize,
			RowY + (ScoreSingleBoxHeight - RankTextSize) / 2.0f,
			RankTextSize,
			aBuf,
			-1.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

// bestclient
CUIRect CHud::GetScoreHudRect(bool ForcePreview) const
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_SCORE))
		return {0.0f, 0.0f, 0.0f, 0.0f};
	if(!ForcePreview && !g_Config.m_ClShowhudScore)
		return {0.0f, 0.0f, 0.0f, 0.0f};
	if(!ForcePreview && GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_SCORE, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float Width = 112.0f * Scale;
	const float Height = 56.0f * Scale;

	HudLayout::SModuleRect RawRect;
	if(!HudLayout::HasPositionOverride(HudLayout::MODULE_SCORE))
		RawRect = {m_Width - Width, 285.0f - Height, Width, Height, 5.0f * Scale};
	else
		RawRect = {Layout.m_X, Layout.m_Y, Width, Height, 5.0f * Scale};
	const auto ClampedRect = HudLayout::ClampRectToScreen(RawRect, m_Width, m_Height);
	return PushHudRectFromMusicPlayer(GameClient(), {ClampedRect.m_X, ClampedRect.m_Y, ClampedRect.m_W, ClampedRect.m_H}, m_Width, m_Height, ForcePreview);
}
// bestclient

void CHud::RenderWarmupTimer()
{
	if(GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer <= 0 ||
		(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_RACETIME) != 0)
	{
		return;
	}

	const float FontSize = 20.0f;
	const char *pTitle = Localize("Warmup");
	TextRender()->Text(150.0f * Graphics()->ScreenAspect() - TextRender()->TextWidth(FontSize, pTitle) / 2.0f, 50.0f, FontSize, pTitle);

	const int Seconds = GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer / Client()->GameTickSpeed();
	char aWarmupTime[16];
	float TextWidth;
	if(Seconds < 5)
	{
		str_format(aWarmupTime, sizeof(aWarmupTime), "%d.%d", Seconds, (GameClient()->m_Snap.m_pGameInfoObj->m_WarmupTimer * 10 / Client()->GameTickSpeed()) % 10);
		TextWidth = TextRender()->TextWidth(FontSize, "0.0"); // Calculate width with fixed string to avoid slight changes when using aWarmupTime
	}
	else
	{
		str_format(aWarmupTime, sizeof(aWarmupTime), "%d", Seconds);
		TextWidth = TextRender()->TextWidth(FontSize, aWarmupTime);
	}
	TextRender()->Text(150.0f * Graphics()->ScreenAspect() - TextWidth / 2.0f, 75.0f, FontSize, aWarmupTime);
}

void CHud::RenderTextInfo()
{
	int Showfps = g_Config.m_ClShowfps;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		Showfps = 0;
#endif
	if(Showfps)
	{
		char aBuf[16];
		const int FramesPerSecond = round_to_int(1.0f / Client()->FrameTimeAverage());
		str_format(aBuf, sizeof(aBuf), "%d", FramesPerSecond);

		static float s_TextWidth0 = TextRender()->TextWidth(12.f, "0", -1, -1.0f);
		static float s_TextWidth00 = TextRender()->TextWidth(12.f, "00", -1, -1.0f);
		static float s_TextWidth000 = TextRender()->TextWidth(12.f, "000", -1, -1.0f);
		static float s_TextWidth0000 = TextRender()->TextWidth(12.f, "0000", -1, -1.0f);
		static float s_TextWidth00000 = TextRender()->TextWidth(12.f, "00000", -1, -1.0f);
		static const float s_aTextWidth[5] = {s_TextWidth0, s_TextWidth00, s_TextWidth000, s_TextWidth0000, s_TextWidth00000};

		int DigitIndex = GetDigitsIndex(FramesPerSecond, 4);

		CTextCursor Cursor;
		Cursor.SetPosition(vec2(m_Width - 10 - s_aTextWidth[DigitIndex], 5));
		Cursor.m_FontSize = 12.0f;
		auto OldFlags = TextRender()->GetRenderFlags();
		TextRender()->SetRenderFlags(OldFlags | TEXT_RENDER_FLAG_ONE_TIME_USE);
		if(m_FPSTextContainerIndex.Valid())
			TextRender()->RecreateTextContainerSoft(m_FPSTextContainerIndex, &Cursor, aBuf);
		else
			TextRender()->CreateTextContainer(m_FPSTextContainerIndex, &Cursor, "0");
		TextRender()->SetRenderFlags(OldFlags);
		if(m_FPSTextContainerIndex.Valid())
		{
			TextRender()->RenderTextContainer(m_FPSTextContainerIndex, TextRender()->DefaultTextColor(), TextRender()->DefaultTextOutlineColor());
		}
	}
	if(g_Config.m_ClShowpred && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%d", Client()->GetPredictionTime());
		TextRender()->Text(m_Width - 10 - TextRender()->TextWidth(12, aBuf, -1, -1.0f), Showfps ? 20 : 5, 12, aBuf, -1.0f);
	}

	// bestclient
	if(GameClient()->m_FastPractice.Enabled())
	{
		constexpr const char *pLine1 = "practice mode";
		constexpr const char *pLine2 = "(you can use practice commands /tc /invincible)";
		const float Line1Size = 10.0f;
		const float Line2Size = 8.0f;
		const float Line1X = m_Width / 2.0f - TextRender()->TextWidth(Line1Size, pLine1, -1, -1.0f) / 2.0f;
		const float Line2X = m_Width / 2.0f - TextRender()->TextWidth(Line2Size, pLine2, -1, -1.0f) / 2.0f;
		TextRender()->Text(Line1X, 34.0f, Line1Size, pLine1, -1.0f);
		TextRender()->Text(Line2X, 45.0f, Line2Size, pLine2, -1.0f);
	}
	// bestclient

	if(g_Config.m_TcMiniDebug)
	{
		float FontSize = 8.0f;
		float TextHeight = 11.0f;
		char aBuf[64];
		float OffsetY = 3.0f;

		int PlayerId = GameClient()->m_Snap.m_LocalClientId;
		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
			PlayerId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

		if(g_Config.m_ClShowhudDDRace && GameClient()->m_Snap.m_aCharacters[PlayerId].m_HasExtendedData && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
			OffsetY += 50.0f;
		else if(g_Config.m_ClShowhudHealthAmmo && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
			OffsetY += 27.0f;

		vec2 Pos;
		if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
			Pos = vec2(GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x, GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].y);
		else
			Pos = GameClient()->m_aClients[PlayerId].m_RenderPos;

		str_format(aBuf, sizeof(aBuf), "X: %.2f", Pos.x / 32.0f);
		TextRender()->Text(4, OffsetY, FontSize, aBuf, -1.0f);

		OffsetY += TextHeight;
		str_format(aBuf, sizeof(aBuf), "Y: %.2f", Pos.y / 32.0f);
		TextRender()->Text(4, OffsetY, FontSize, aBuf, -1.0f);
		if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		{
			OffsetY += TextHeight;
			str_format(aBuf, sizeof(aBuf), "Angle: %d", GameClient()->m_aClients[PlayerId].m_RenderCur.m_Angle);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);

			OffsetY += TextHeight;
			str_format(aBuf, sizeof(aBuf), "VelY: %.2f", GameClient()->m_Snap.m_aCharacters[PlayerId].m_Cur.m_VelY / 256.0f * 50.0f / 32.0f);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);

			OffsetY += TextHeight;

			str_format(aBuf, sizeof(aBuf), "VelX: %.2f", GameClient()->m_Snap.m_aCharacters[PlayerId].m_Cur.m_VelX / 256.0f * 50.0f / 32.0f);
			TextRender()->Text(4.0f, OffsetY, FontSize, aBuf, -1.0f);
		}
	}
	if(g_Config.m_TcRenderCursorSpec && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW)
	{
		int CurWeapon = 1;
		Graphics()->SetColor(1.f, 1.f, 1.f, g_Config.m_TcRenderCursorSpecAlpha / 100.0f);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aCursorOffset[CurWeapon], m_Width / 2.0f, m_Height / 2.0f, 0.36f, 0.36f);
	}
	// render team in freeze text and last notify
	// bestclient
	if(g_Config.m_TcShowFrozenText > 0 && GameClient()->m_GameInfo.m_EntitiesDDRace)
	{
		int NumInTeam = 0;
		int NumFrozen = 0;
		int LocalTeamId = 0;
		GetFrozenTeamCounts(NumInTeam, NumFrozen, LocalTeamId);
		char aBuf[64];
		if(g_Config.m_TcShowFrozenText == 1)
			str_format(aBuf, sizeof(aBuf), "%d / %d", NumInTeam - NumFrozen, NumInTeam);
		else if(g_Config.m_TcShowFrozenText == 2)
			str_format(aBuf, sizeof(aBuf), "%d / %d", NumFrozen, NumInTeam);
		const float TextWidth = TextRender()->TextWidth(10.0f, aBuf);
		const CUIRect TextRect = PushHudRectFromMusicPlayer(GameClient(), {m_Width / 2.0f - TextWidth / 2.0f, 12.0f, TextWidth, 10.0f}, m_Width, m_Height, false);
		TextRender()->Text(TextRect.x, TextRect.y, 10.0f, aBuf);
	}
	// bestclient
}

void CHud::RenderConnectionWarning()
{
	if(Client()->ConnectionProblems())
	{
		const char *pText = Localize("Connection Problems…");
		float w = TextRender()->TextWidth(24, pText, -1, -1.0f);
		TextRender()->Text(150 * Graphics()->ScreenAspect() - w / 2, 50, 24, pText, -1.0f);
	}
}

void CHud::RenderTeambalanceWarning()
{
	// render prompt about team-balance
	bool Flash = time() / (time_freq() / 2) % 2 == 0;
	if(GameClient()->IsTeamPlay())
	{
		int TeamDiff = GameClient()->m_Snap.m_aTeamSize[TEAM_RED] - GameClient()->m_Snap.m_aTeamSize[TEAM_BLUE];
		if(g_Config.m_ClWarningTeambalance && (TeamDiff >= 2 || TeamDiff <= -2))
		{
			const char *pText = Localize("Please balance teams!");
			if(Flash)
				TextRender()->TextColor(1, 1, 0.5f, 1);
			else
				TextRender()->TextColor(0.7f, 0.7f, 0.2f, 1.0f);
			TextRender()->Text(5, 50, 6, pText, -1.0f);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderCursor()
{
	const float Scale = (float)g_Config.m_TcCursorScale / 100.0f;
	if(Scale <= 0.0f)
		return;

	// bestclient
	GameClient()->m_AspectRatio.BeginCursorGameAspect();
	const auto EndCursorGameAspect = [&]() {
		GameClient()->m_AspectRatio.EndCursorGameAspect();
	};
	// bestclient

	int CurWeapon = 0;
	vec2 TargetPos;
	float Alpha = 1.0f;

	const vec2 Center = GameClient()->m_Camera.m_Center;
	CScreenRect ScreenRect = Graphics()->MapScreenToWorld(Center.x, Center.y, 100.0f, 100.0f, 100.0f, 0, 0, Graphics()->ScreenAspect(), 1.0f);
	Graphics()->MapScreen(ScreenRect);

	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_pLocalCharacter)
	{
		// Render local cursor
		CurWeapon = std::max(0, GameClient()->m_aClients[GameClient()->m_Snap.m_LocalClientId].m_Predicted.m_ActiveWeapon);
		TargetPos = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy];
	}
	else
	{
		// Render spec cursor
		if(!g_Config.m_ClSpecCursor || !GameClient()->m_CursorInfo.IsAvailable())
		{
			// bestclient
			EndCursorGameAspect();
			// bestclient
			return;
		}

		bool RenderSpecCursor = (GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW) || Client()->State() == IClient::STATE_DEMOPLAYBACK;

		if(!RenderSpecCursor)
		{
			// bestclient
			EndCursorGameAspect();
			// bestclient
			return;
		}

		// Calculate factor to keep cursor on screen
		const vec2 HalfSize = Center - ScreenRect.m_TopLeft;
		const vec2 ScreenPos = (GameClient()->m_CursorInfo.WorldTarget() - Center) / GameClient()->m_Camera.m_Zoom;
		const float ClampFactor = std::max({
			1.0f,
			absolute(ScreenPos.x / HalfSize.x),
			absolute(ScreenPos.y / HalfSize.y),
		});

		CurWeapon = std::max(0, GameClient()->m_CursorInfo.Weapon() % NUM_WEAPONS);
		TargetPos = ScreenPos / ClampFactor + Center;
		if(ClampFactor != 1.0f)
			Alpha /= 2.0f;
	}

	// bestclient
	GameClient()->m_CursorTrail.Render(TargetPos, Scale, Alpha, CurWeapon, m_HudQuadContainerIndex, m_aCursorOffset);
	// bestclient

	Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aCursorOffset[CurWeapon], TargetPos.x, TargetPos.y, Scale, Scale);

	// bestclient
	EndCursorGameAspect();
	// bestclient
}

void CHud::PrepareAmmoHealthAndArmorQuads()
{
	float x = 5;
	float y = 5;
	IGraphics::CQuadItem Array[10];

	// ammo of the different weapons
	for(int i = 0; i < NUM_WEAPONS; ++i)
	{
		// 0.6
		for(int n = 0; n < 10; n++)
			Array[n] = IGraphics::CQuadItem(x + n * 12, y, 10, 10);

		m_aAmmoOffset[i] = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

		// 0.7
		if(i == WEAPON_GRENADE)
		{
			// special case for 0.7 grenade
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(1 + x + n * 12, y, 10, 10);
		}
		else
		{
			for(int n = 0; n < 10; n++)
				Array[n] = IGraphics::CQuadItem(x + n * 12, y, 12, 12);
		}

		Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
	}

	// health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_HealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty health
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 10, 10);
	m_EmptyHealthOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 10, 10);
	m_ArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// empty armor meter
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 10, 10);
	m_EmptyArmorOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// 0.7
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y + 12, 12, 12);
	Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);
}

void CHud::RenderAmmoHealthAndArmor(const CNetObj_Character *pCharacter)
{
	if(!pCharacter)
		return;

	bool IsSixupGameSkin = GameClient()->m_GameSkin.IsSixup();
	int QuadOffsetSixup = (IsSixupGameSkin ? 10 : 0);

	if(GameClient()->m_GameInfo.m_HudAmmo)
	{
		// ammo display
		float AmmoOffsetY = GameClient()->m_GameInfo.m_HudHealthArmor ? 24 : 0;
		int CurWeapon = pCharacter->m_Weapon % NUM_WEAPONS;
		// 0.7 only
		if(CurWeapon == WEAPON_NINJA)
		{
			if(!GameClient()->m_GameInfo.m_HudDDRace && Client()->IsSixup())
			{
				const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
				float NinjaProgress = std::clamp(pCharacter->m_AmmoCount - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
				RenderNinjaBarPos(5 + 10 * 12, 5, 6.f, 24.f, NinjaProgress);
			}
		}
		else if(CurWeapon >= 0 && GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon].IsValid())
		{
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponProjectiles[CurWeapon]);
			if(AmmoOffsetY > 0)
			{
				Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, std::clamp(pCharacter->m_AmmoCount, 0, 10), 0, AmmoOffsetY);
			}
			else
			{
				Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_aAmmoOffset[CurWeapon] + QuadOffsetSixup, std::clamp(pCharacter->m_AmmoCount, 0, 10));
			}
		}
	}

	if(GameClient()->m_GameInfo.m_HudHealthArmor)
	{
		// health display
		const int DisplayHealth = std::min(pCharacter->m_Health, 10);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteHealthFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_HealthOffset + QuadOffsetSixup, DisplayHealth);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteHealthEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_EmptyHealthOffset + QuadOffsetSixup + DisplayHealth, 10 - DisplayHealth);

		// armor display
		const int DisplayArmor = std::min(pCharacter->m_Armor, 10);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteArmorFull);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup, DisplayArmor);
		Graphics()->TextureSet(GameClient()->m_GameSkin.m_SpriteArmorEmpty);
		Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_ArmorOffset + QuadOffsetSixup + DisplayArmor, 10 - DisplayArmor);
	}
}

void CHud::PreparePlayerStateQuads()
{
	float x = 5;
	float y = 5 + 24;
	IGraphics::CQuadItem Array[10];

	// Quads for displaying the available and used jumps
	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	for(int i = 0; i < 10; ++i)
		Array[i] = IGraphics::CQuadItem(x + i * 12, y, 12, 12);
	m_AirjumpEmptyOffset = Graphics()->QuadContainerAddQuads(m_HudQuadContainerIndex, Array, 10);

	// Quads for displaying weapons
	for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
	{
		const CDataWeaponspec &WeaponSpec = g_pData->m_Weapons.m_aId[Weapon];
		float ScaleX, ScaleY;
		Graphics()->GetSpriteScale(WeaponSpec.m_pSpriteBody, ScaleX, ScaleY);
		constexpr float HudWeaponScale = 0.25f;
		float Width = WeaponSpec.m_VisualSize * ScaleX * HudWeaponScale;
		float Height = WeaponSpec.m_VisualSize * ScaleY * HudWeaponScale;
		m_aWeaponOffset[Weapon] = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, Width, Height);
	}

	// Quads for displaying capabilities
	m_EndlessJumpOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_EndlessHookOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_JetpackOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGrenadeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportGunOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_TeleportLaserOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying prohibited capabilities
	m_SoloOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_CollisionDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HookHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_HammerHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GunHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_ShotgunHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_GrenadeHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LaserHitDisabledOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying freeze status
	m_DeepFrozenOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LiveFrozenOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying dummy actions
	m_DummyHammerOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_DummyCopyOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);

	// Quads for displaying team modes
	m_PracticeModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_LockModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
	m_Team0ModeOffset = Graphics()->QuadContainerAddSprite(m_HudQuadContainerIndex, 0.f, 0.f, 12.f, 12.f);
}

void CHud::RenderPlayerState(const int ClientId)
{
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);

	// pCharacter contains the predicted character for local players or the last snap for players who are spectated
	CCharacterCore *pCharacter = &GameClient()->m_aClients[ClientId].m_Predicted;
	CNetObj_Character *pPlayer = &GameClient()->m_aClients[ClientId].m_RenderCur;
	int TotalJumpsToDisplay = 0;
	if(g_Config.m_ClShowhudJumpsIndicator)
	{
		int AvailableJumpsToDisplay;
		if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
		{
			const bool Grounded = Collision()->IsOnGround(vec2(pPlayer->m_X, pPlayer->m_Y), CCharacterCore::PhysicalSize());
			int UsedJumps = pCharacter->m_JumpedTotal;
			if(pCharacter->m_Jumps > 1)
			{
				UsedJumps += !Grounded;
			}
			else if(pCharacter->m_Jumps == 1)
			{
				// If the player has only one jump, each jump is the last one
				UsedJumps = pPlayer->m_Jumped & 2;
			}
			else if(pCharacter->m_Jumps == -1)
			{
				// The player has only one ground jump
				UsedJumps = !Grounded;
			}

			if(pCharacter->m_EndlessJump && UsedJumps >= absolute(pCharacter->m_Jumps))
			{
				UsedJumps = absolute(pCharacter->m_Jumps) - 1;
			}

			int UnusedJumps = absolute(pCharacter->m_Jumps) - UsedJumps;
			if(!(pPlayer->m_Jumped & 2) && UnusedJumps <= 0)
			{
				// In some edge cases when the player just got another number of jumps, UnusedJumps is not correct
				UnusedJumps = 1;
			}
			TotalJumpsToDisplay = std::clamp(absolute(pCharacter->m_Jumps), 0, 10);
			AvailableJumpsToDisplay = std::clamp(UnusedJumps, 0, TotalJumpsToDisplay);
		}
		else
		{
			TotalJumpsToDisplay = AvailableJumpsToDisplay = absolute(GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Jumps);
		}

		// render available and used jumps
		int JumpsOffsetY = ((GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
				    (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));
		if(JumpsOffsetY > 0)
		{
			Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjump);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay, 0, JumpsOffsetY);
			Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjumpEmpty);
			Graphics()->RenderQuadContainerEx(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay, 0, JumpsOffsetY);
		}
		else
		{
			Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjump);
			Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_AirjumpOffset, AvailableJumpsToDisplay);
			Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudAirjumpEmpty);
			Graphics()->RenderQuadContainer(m_HudQuadContainerIndex, m_AirjumpEmptyOffset + AvailableJumpsToDisplay, TotalJumpsToDisplay - AvailableJumpsToDisplay);
		}
	}

	float x = 5 + 12;
	float y = (5 + 12 + (GameClient()->m_GameInfo.m_HudHealthArmor && g_Config.m_ClShowhudHealthAmmo ? 24 : 0) +
		   (GameClient()->m_GameInfo.m_HudAmmo && g_Config.m_ClShowhudHealthAmmo ? 12 : 0));

	// render weapons
	{
		constexpr float aWeaponWidth[NUM_WEAPONS] = {16, 12, 12, 12, 12, 12};
		constexpr float aWeaponInitialOffset[NUM_WEAPONS] = {-3, -4, -1, -1, -2, -4};
		bool InitialOffsetAdded = false;
		for(int Weapon = 0; Weapon < NUM_WEAPONS; ++Weapon)
		{
			if(!pCharacter->m_aWeapons[Weapon].m_Got)
				continue;
			if(!InitialOffsetAdded)
			{
				x += aWeaponInitialOffset[Weapon];
				InitialOffsetAdded = true;
			}
			if(pPlayer->m_Weapon != Weapon)
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.4f);
			Graphics()->QuadsSetRotation(pi * 7 / 4);
			Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpritePickupWeapons[Weapon]);
			Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_aWeaponOffset[Weapon], x, y);
			Graphics()->QuadsSetRotation(0);
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
			x += aWeaponWidth[Weapon];
		}
		if(pCharacter->m_aWeapons[WEAPON_NINJA].m_Got)
		{
			const int Max = g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000;
			float NinjaProgress = std::clamp(pCharacter->m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Client()->GameTickSpeed() / 1000 - Client()->GameTick(g_Config.m_ClDummy), 0, Max) / (float)Max;
			if(NinjaProgress > 0.0f && GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo)
			{
				RenderNinjaBarPos(x, y - 12, 6.f, 24.f, NinjaProgress);
			}
		}
	}

	// render capabilities
	x = 5;
	y += 12;
	if(TotalJumpsToDisplay > 0)
	{
		y += 12;
	}
	bool HasCapabilities = false;
	if(pCharacter->m_EndlessJump)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudEndlessJump);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessJumpOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_EndlessHook)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudEndlessHook);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_EndlessHookOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_Jetpack)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudJetpack);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_JetpackOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportGun);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGunOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunGrenade && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportGrenade);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportGrenadeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HasTelegunLaser && pCharacter->m_aWeapons[WEAPON_LASER].m_Got)
	{
		HasCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeleportLaser);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_TeleportLaserOffset, x, y);
	}

	// render prohibited capabilities
	x = 5;
	if(HasCapabilities)
	{
		y += 12;
	}
	bool HasProhibitedCapabilities = false;
	if(pCharacter->m_Solo)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudSolo);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_SoloOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_CollisionDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudCollisionDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_CollisionDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HookHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudHookHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HookHitDisabledOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_HammerHitDisabled)
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudHammerHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_HammerHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_HasTelegunGun && pCharacter->m_aWeapons[WEAPON_GUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudGunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_ShotgunHitDisabled && pCharacter->m_aWeapons[WEAPON_SHOTGUN].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudShotgunHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_ShotgunHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_GrenadeHitDisabled && pCharacter->m_aWeapons[WEAPON_GRENADE].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudGrenadeHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_GrenadeHitDisabledOffset, x, y);
		x += 12;
	}
	if((pCharacter->m_LaserHitDisabled && pCharacter->m_aWeapons[WEAPON_LASER].m_Got))
	{
		HasProhibitedCapabilities = true;
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLaserHitDisabled);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LaserHitDisabledOffset, x, y);
	}

	// render dummy actions and freeze state
	x = 5;
	if(HasProhibitedCapabilities)
	{
		y += 12;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_LOCK_MODE)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLockMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LockModeOffset, x, y);
		x += 12;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_PRACTICE_MODE)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudPracticeMode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_PracticeModeOffset, x, y);
		x += 12;
	}
	if(GameClient()->m_Snap.m_aCharacters[ClientId].m_HasExtendedDisplayInfo && GameClient()->m_Snap.m_aCharacters[ClientId].m_ExtendedData.m_Flags & CHARACTERFLAG_TEAM0_MODE)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudTeam0Mode);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_Team0ModeOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_DeepFrozen)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDeepFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DeepFrozenOffset, x, y);
		x += 12;
	}
	if(pCharacter->m_LiveFrozen)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudLiveFrozen);
		Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_LiveFrozenOffset, x, y);
	}
}

void CHud::RenderNinjaBarPos(const float x, float y, const float Width, const float Height, float Progress, const float Alpha)
{
	Progress = std::clamp(Progress, 0.0f, 1.0f);

	// what percentage of the end pieces is used for the progress indicator and how much is the rest
	// half of the ends are used for the progress display
	const float RestPct = 0.5f;
	const float ProgPct = 0.5f;

	const float EndHeight = Width; // to keep the correct scale - the width of the sprite is as long as the height
	const float BarWidth = Width;
	const float WholeBarHeight = Height;
	const float MiddleBarHeight = WholeBarHeight - (EndHeight * 2.0f);
	const float EndProgressHeight = EndHeight * ProgPct;
	const float EndRestHeight = EndHeight * RestPct;
	const float ProgressBarHeight = WholeBarHeight - (EndProgressHeight * 2.0f);
	const float EndProgressProportion = EndProgressHeight / ProgressBarHeight;
	const float MiddleProgressProportion = MiddleBarHeight / ProgressBarHeight;

	// beginning piece
	float BeginningPieceProgress = 1;
	if(Progress <= 1)
	{
		if(Progress <= (EndProgressProportion + MiddleProgressProportion))
		{
			BeginningPieceProgress = 0;
		}
		else
		{
			BeginningPieceProgress = (Progress - EndProgressProportion - MiddleProgressProportion) / EndProgressProportion;
		}
	}
	// empty
	Graphics()->WrapClamp();
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 0, ProgPct - ProgPct * (1.0f - BeginningPieceProgress), 1);
	IGraphics::CQuadItem QuadEmptyBeginning(x, y, BarWidth, EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress));
	Graphics()->QuadsDrawTL(&QuadEmptyBeginning, 1);
	Graphics()->QuadsEnd();
	// full
	if(BeginningPieceProgress > 0.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * (1.0f - BeginningPieceProgress), 1, RestPct + ProgPct * (1.0f - BeginningPieceProgress), 0, 1, 0, 1, 1);
		IGraphics::CQuadItem QuadFullBeginning(x, y + (EndRestHeight + EndProgressHeight * (1.0f - BeginningPieceProgress)), BarWidth, EndProgressHeight * BeginningPieceProgress);
		Graphics()->QuadsDrawTL(&QuadFullBeginning, 1);
		Graphics()->QuadsEnd();
	}

	// middle piece
	y += EndHeight;

	float MiddlePieceProgress = 1;
	if(Progress <= EndProgressProportion + MiddleProgressProportion)
	{
		if(Progress <= EndProgressProportion)
		{
			MiddlePieceProgress = 0;
		}
		else
		{
			MiddlePieceProgress = (Progress - EndProgressProportion) / MiddleProgressProportion;
		}
	}

	const float FullMiddleBarHeight = MiddleBarHeight * MiddlePieceProgress;
	const float EmptyMiddleBarHeight = MiddleBarHeight - FullMiddleBarHeight;

	// empty ninja bar
	if(EmptyMiddleBarHeight > 0.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmpty);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// select the middle portion of the sprite so we don't get edge bleeding
		if(EmptyMiddleBarHeight <= EndHeight)
		{
			// prevent pixel puree, select only a small slice
			// Subset: btm_r, top_r, top_m, btm_m | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 0, 1.0f - (EmptyMiddleBarHeight / EndHeight), 1);
		}
		else
		{
			// Subset: btm_r, top_r, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
			Graphics()->QuadsSetSubsetFree(1, 1, 1, 0, 0, 0, 0, 1);
		}
		IGraphics::CQuadItem QuadEmpty(x, y, BarWidth, EmptyMiddleBarHeight);
		Graphics()->QuadsDrawTL(&QuadEmpty, 1);
		Graphics()->QuadsEnd();
	}

	// full ninja bar
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFull);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// select the middle portion of the sprite so we don't get edge bleeding
	if(FullMiddleBarHeight <= EndHeight)
	{
		// prevent pixel puree, select only a small slice
		// Subset: btm_m, top_m, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(1.0f - (FullMiddleBarHeight / EndHeight), 1, 1.0f - (FullMiddleBarHeight / EndHeight), 0, 1, 0, 1, 1);
	}
	else
	{
		// Subset: btm_l, top_l, top_r, btm_r | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, 1, 0, 1, 1);
	}
	IGraphics::CQuadItem QuadFull(x, y + EmptyMiddleBarHeight, BarWidth, FullMiddleBarHeight);
	Graphics()->QuadsDrawTL(&QuadFull, 1);
	Graphics()->QuadsEnd();

	// ending piece
	y += MiddleBarHeight;
	float EndingPieceProgress = 1;
	if(Progress <= EndProgressProportion)
	{
		EndingPieceProgress = Progress / EndProgressProportion;
	}
	// empty
	if(EndingPieceProgress < 1.0f)
	{
		Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarEmptyRight);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
		// Subset: btm_l, top_l, top_m, btm_m | it is rotated 90 degrees clockwise
		Graphics()->QuadsSetSubsetFree(0, 1, 0, 0, ProgPct - ProgPct * EndingPieceProgress, 0, ProgPct - ProgPct * EndingPieceProgress, 1);
		IGraphics::CQuadItem QuadEmptyEnding(x, y, BarWidth, EndProgressHeight * (1.0f - EndingPieceProgress));
		Graphics()->QuadsDrawTL(&QuadEmptyEnding, 1);
		Graphics()->QuadsEnd();
	}
	// full
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudNinjaBarFullLeft);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.f, 1.f, 1.f, Alpha);
	// Subset: btm_m, top_m, top_l, btm_l | it is mirrored on the horizontal axe and rotated 90 degrees counterclockwise
	Graphics()->QuadsSetSubsetFree(RestPct + ProgPct * EndingPieceProgress, 1, RestPct + ProgPct * EndingPieceProgress, 0, 0, 0, 0, 1);
	IGraphics::CQuadItem QuadFullEnding(x, y + (EndProgressHeight * (1.0f - EndingPieceProgress)), BarWidth, EndRestHeight + EndProgressHeight * EndingPieceProgress);
	Graphics()->QuadsDrawTL(&QuadFullEnding, 1);
	Graphics()->QuadsEnd();

	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->SetColor(1.f, 1.f, 1.f, 1.f);
	Graphics()->WrapNormal();
}

// bestclient
void CHud::RenderSpectatorCount(bool ForcePreview)
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_SPECTATOR_COUNT))
		return;

	HudExtras::SSpectatorCountState State;
	if(!HudExtras::GetSpectatorCountState(GameClient(), Client(), m_LastSpectatorCountTick, State, ForcePreview))
		return;

	const CUIRect Rect = GetSpectatorCountRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_SPECTATOR_COUNT, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_SPECTATOR_COUNT);
	const float Fontsize = 6.0f * Scale;
	const float LineHeight = Fontsize + 2.0f * Scale;
	const float PaddingX = 2.0f * Scale;
	const float PaddingY = 2.0f * Scale;
	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Rect.x, Rect.y, Rect.w, Rect.h, m_Width, m_Height);
	if(Layout.m_BackgroundEnabled)
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)).WithMultipliedAlpha(ModuleAlpha), Corners, 5.0f * Scale);
	else
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f * ModuleAlpha), Corners, 5.0f * Scale);

	float y = Rect.y + PaddingY;
	const float X = Rect.x + PaddingX;

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->Text(X, y, Fontsize, FontIcon::EYE, -1.0f);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	TextRender()->Text(X + Fontsize + 3.0f * Scale, y, Fontsize, State.m_aCountBuf, -1.0f);

	y += LineHeight;
	for(int i = 0; i < State.m_NumNameLines; i++)
	{
		TextRender()->Text(X, y, Fontsize, State.m_aaNameLines[i], -1.0f);
		y += LineHeight;
	}
}

CUIRect CHud::GetSpectatorCountRect(bool ForcePreview)
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_SPECTATOR_COUNT))
		return {0.0f, 0.0f, 0.0f, 0.0f};

	HudExtras::SSpectatorCountState State;
	if(!HudExtras::GetSpectatorCountState(GameClient(), Client(), m_LastSpectatorCountTick, State, ForcePreview))
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_SPECTATOR_COUNT, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float Fontsize = 6.0f * Scale;
	const float LineHeight = Fontsize + 2.0f * Scale;
	const float PaddingX = 2.0f * Scale;
	const float PaddingY = 2.0f * Scale;
	float MaxLineWidth = Fontsize + 3.0f * Scale + TextRender()->TextWidth(Fontsize, State.m_aCountBuf);
	for(int i = 0; i < State.m_NumNameLines; i++)
		MaxLineWidth = std::max(MaxLineWidth, TextRender()->TextWidth(Fontsize, State.m_aaNameLines[i]));

	const float BoxHeight = PaddingY * 2.0f + LineHeight * (1 + State.m_NumNameLines);
	const float BoxWidth = PaddingX * 2.0f + MaxLineWidth;

	HudLayout::SModuleRect RawRect;
	if(!HudLayout::HasPositionOverride(HudLayout::MODULE_SPECTATOR_COUNT))
	{
		float Y = 285.0f - BoxHeight - 4.0f;
		const CUIRect MovementInfoRect = GetMovementInformationRect(false);
		if(MovementInfoRect.h > 0.0f)
			Y -= 4.0f + MovementInfoRect.h;
		const CUIRect ScoreRect = GetScoreHudRect(false);
		if(ScoreRect.h > 0.0f)
			Y -= ScoreRect.h;
		if(!HudLayout::HasPositionOverride(HudLayout::MODULE_DUMMY_ACTIONS))
		{
			const CUIRect DummyActionsRect = GetDummyActionsRect(ForcePreview);
			if(DummyActionsRect.h > 0.0f)
				Y -= 4.0f + DummyActionsRect.h;
		}
		RawRect = {m_Width - BoxWidth, Y, BoxWidth, BoxHeight, 5.0f * Scale};
	}
	else
	{
		float X = Layout.m_X;
		float Y = Layout.m_Y;
		if(Layout.m_Mode == HudLayout::POSITION_MODE_BOTTOM_RIGHT)
		{
			X -= BoxWidth;
			Y -= BoxHeight;
		}
		RawRect = {X, Y, BoxWidth, BoxHeight, 5.0f * Scale};
	}
	const auto ClampedRect = HudLayout::ClampRectToScreen(RawRect, m_Width, m_Height);
	return PushHudRectFromMusicPlayer(GameClient(), {ClampedRect.m_X, ClampedRect.m_Y, ClampedRect.m_W, ClampedRect.m_H}, m_Width, m_Height, ForcePreview);
}
// bestclient

void CHud::RenderDummyActions(bool ForcePreview)
{
	// bestclient
	if(!HudLayout::IsEnabled(HudLayout::MODULE_DUMMY_ACTIONS))
		return;
	// bestclient
	if(!ForcePreview)
	{
		if(!g_Config.m_ClShowhudDummyActions || (GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)) || !Client()->DummyConnected())
			return;
	}

	const CUIRect Rect = GetDummyActionsRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	// bestclient
	const auto Layout = HudLayout::Get(HudLayout::MODULE_DUMMY_ACTIONS, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_DUMMY_ACTIONS);
	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Rect.x, Rect.y, Rect.w, Rect.h, m_Width, m_Height);
	if(Layout.m_BackgroundEnabled)
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)).WithMultipliedAlpha(ModuleAlpha), Corners, 5.0f * Scale);
	else
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f * ModuleAlpha), Corners, 5.0f * Scale);

	float y = Rect.y + 2.0f * Scale;
	float x = Rect.x + 2.0f * Scale;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, g_Config.m_ClDummyHammer ? 1.0f : 0.4f);
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDummyHammer);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyHammerOffset, x, y);
	y += 13.0f * Scale;
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, g_Config.m_ClDummyCopyMoves ? 1.0f : 0.4f);
	Graphics()->TextureSet(GameClient()->m_HudSkin.m_SpriteHudDummyCopy);
	Graphics()->RenderQuadContainerAsSprite(m_HudQuadContainerIndex, m_DummyCopyOffset, x, y);
}

// bestclient
CUIRect CHud::GetDummyActionsRect(bool ForcePreview) const
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_DUMMY_ACTIONS))
		return {0.0f, 0.0f, 0.0f, 0.0f};
	if(!ForcePreview)
	{
		if(!g_Config.m_ClShowhudDummyActions || !Client()->DummyConnected())
			return {0.0f, 0.0f, 0.0f, 0.0f};
		if(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
			return {0.0f, 0.0f, 0.0f, 0.0f};
	}

	const auto Layout = HudLayout::Get(HudLayout::MODULE_DUMMY_ACTIONS, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float BoxWidth = 16.0f * Scale;
	const float BoxHeight = 29.0f * Scale;

	HudLayout::SModuleRect RawRect;
	if(!HudLayout::HasPositionOverride(HudLayout::MODULE_DUMMY_ACTIONS))
	{
		float StartY = 285.0f - BoxHeight - 4.0f * Scale;
		const CUIRect MovementInfoRect = GetMovementInformationRect(false);
		if(MovementInfoRect.h > 0.0f)
			StartY -= 4.0f * Scale + MovementInfoRect.h;
		const CUIRect ScoreRect = GetScoreHudRect(false);
		if(ScoreRect.h > 0.0f)
			StartY -= ScoreRect.h;
		RawRect = {m_Width - BoxWidth, StartY, BoxWidth, BoxHeight, 5.0f * Scale};
	}
	else
	{
		float X = Layout.m_X;
		float Y = Layout.m_Y;
		if(Layout.m_Mode == HudLayout::POSITION_MODE_BOTTOM_RIGHT)
		{
			X -= BoxWidth;
			Y -= BoxHeight;
		}
		RawRect = {X, Y, BoxWidth, BoxHeight, 5.0f * Scale};
	}
	const auto ClampedRect = HudLayout::ClampRectToScreen(RawRect, m_Width, m_Height);
	return PushHudRectFromMusicPlayer(GameClient(), {ClampedRect.m_X, ClampedRect.m_Y, ClampedRect.m_W, ClampedRect.m_H}, m_Width, m_Height, ForcePreview);
}
// bestclient

inline int CHud::GetDigitsIndex(int Value, int Max)
{
	if(Value < 0)
	{
		Value *= -1;
	}
	int DigitsIndex = std::log10((Value ? Value : 1));
	if(DigitsIndex > Max)
	{
		DigitsIndex = Max;
	}
	if(DigitsIndex < 0)
	{
		DigitsIndex = 0;
	}
	return DigitsIndex;
}

inline float CHud::GetMovementInformationBoxHeight() const
{
	// bestclient
	const bool ShowCheckpoint = g_Config.m_BcShowCorrectCheckpoint != 0;
	// bestclient
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && (GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW || (GameClient()->m_Snap.m_SpecInfo.m_SpectatorId >= 0 && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId < MAX_CLIENTS && GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId].m_SpecCharPresent)))
	{
		float BoxHeight = g_Config.m_ClShowhudPlayerPosition ? 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT + 2.0f : 0.0f;
		// bestclient
		if(ShowCheckpoint)
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT + (BoxHeight <= 0.0f ? 2.0f : 0.0f);
		// bestclient
		return BoxHeight;
	}

	const bool ShowPos = g_Config.m_ClShowhudPlayerPosition != 0;
	const bool ShowSpeed = g_Config.m_ClShowhudPlayerSpeed != 0;
	const bool ShowAngle = g_Config.m_ClShowhudPlayerAngle != 0;
	float BoxHeight = 3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT * (ShowPos + ShowSpeed) + 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT * ShowAngle;

	if(Client()->DummyConnected())
	{
		if(ShowPos && g_Config.m_TcShowhudDummyPosition)
			BoxHeight += 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
		if(ShowSpeed && g_Config.m_TcShowhudDummySpeed)
			BoxHeight += 2.0f * MOVEMENT_INFORMATION_LINE_HEIGHT;
		if(ShowAngle && g_Config.m_TcShowhudDummyAngle)
			BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT;
	}

	if(ShowPos || ShowSpeed || ShowAngle)
		BoxHeight += 2.0f;
	// bestclient
	if(ShowCheckpoint)
		BoxHeight += MOVEMENT_INFORMATION_LINE_HEIGHT + (BoxHeight <= 0.0f ? 2.0f : 0.0f);
	// bestclient
	return BoxHeight;
}

void CHud::UpdateMovementInformationTextContainer(STextContainerIndex &TextContainer, float FontSize, float Value, float &PrevValue)
{
	Value = std::round(Value * 100.0f) / 100.0f; // Round to 2dp
	if(TextContainer.Valid() && PrevValue == Value)
		return;
	PrevValue = Value;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%.2f", Value);

	CTextCursor Cursor;
	Cursor.m_FontSize = FontSize;
	TextRender()->RecreateTextContainer(TextContainer, &Cursor, aBuf);
}

void CHud::RenderMovementInformationTextContainer(STextContainerIndex &TextContainer, const ColorRGBA &Color, float X, float Y)
{
	if(TextContainer.Valid())
	{
		TextRender()->RenderTextContainer(TextContainer, Color, TextRender()->DefaultTextOutlineColor(), X - TextRender()->GetBoundingBoxTextContainer(TextContainer).m_W, Y);
	}
}

CHud::CMovementInformation CHud::GetMovementInformation(int ClientId, int Conn) const
{
	CMovementInformation Out;
	if(ClientId == SPEC_FREEVIEW)
	{
		Out.m_Pos = GameClient()->m_Camera.m_Center / 32.0f;
	}
	else if(GameClient()->m_aClients[ClientId].m_SpecCharPresent)
	{
		Out.m_Pos = GameClient()->m_aClients[ClientId].m_SpecChar / 32.0f;
	}
	else
	{
		const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
		const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
		const float IntraTick = Client()->IntraGameTick(Conn);

		// To make the player position relative to blocks we need to divide by the block size
		Out.m_Pos = mix(vec2(pPrevChar->m_X, pPrevChar->m_Y), vec2(pCurChar->m_X, pCurChar->m_Y), IntraTick) / 32.0f;

		const vec2 Vel = mix(vec2(pPrevChar->m_VelX, pPrevChar->m_VelY), vec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

		float VelspeedX = Vel.x / 256.0f * Client()->GameTickSpeed();
		if(Vel.x >= -1.0f && Vel.x <= 1.0f)
		{
			VelspeedX = 0.0f;
		}
		float VelspeedY = Vel.y / 256.0f * Client()->GameTickSpeed();
		if(Vel.y >= -128.0f && Vel.y <= 128.0f)
		{
			VelspeedY = 0.0f;
		}
		// We show the speed in Blocks per Second (Bps) and therefore have to divide by the block size
		Out.m_Speed.x = VelspeedX / 32.0f;
		float VelspeedLength = length(vec2(Vel.x, Vel.y) / 256.0f) * Client()->GameTickSpeed();
		// Todo: Use Velramp tuning of each individual player
		// Since these tuning parameters are almost never changed, the default values are sufficient in most cases
		float Ramp = VelocityRamp(VelspeedLength, GameClient()->m_aTuning[Conn].m_VelrampStart, GameClient()->m_aTuning[Conn].m_VelrampRange, GameClient()->m_aTuning[Conn].m_VelrampCurvature);
		Out.m_Speed.x *= Ramp;
		Out.m_Speed.y = VelspeedY / 32.0f;

		float Angle = GameClient()->m_Players.GetPlayerTargetAngle(pPrevChar, pCurChar, ClientId, IntraTick);
		if(Angle < 0.0f)
		{
			Angle += 2.0f * pi;
		}
		Out.m_Angle = Angle * 180.0f / pi;
	}
	return Out;
}

void CHud::RenderMovementInformation(bool ForcePreview)
{
	// bestclient
	if(!HudLayout::IsEnabled(HudLayout::MODULE_MOVEMENT_INFO))
		return;
	const CUIRect Rect = GetMovementInformationRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	const auto Layout = HudLayout::Get(HudLayout::MODULE_MOVEMENT_INFO, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_MOVEMENT_INFO);
	// bestclient

	const int ClientId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
	const bool PosOnly = !ForcePreview && (ClientId == SPEC_FREEVIEW || (ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_SpecCharPresent));
	// bestclient
	const bool ShowCheckpoint = g_Config.m_BcShowCorrectCheckpoint != 0;
	// bestclient
	if(!ForcePreview && !g_Config.m_ClShowhudPlayerPosition && (PosOnly || (!g_Config.m_ClShowhudPlayerSpeed && !g_Config.m_ClShowhudPlayerAngle)) && !ShowCheckpoint)
	{
		return;
	}
	const float LineSpacer = 1.0f * Scale;
	const float Fontsize = 6.0f * Scale;
	const float LineHeight = MOVEMENT_INFORMATION_LINE_HEIGHT * Scale;

	// bestclient
	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Rect.x, Rect.y, Rect.w, Rect.h, m_Width, m_Height);
	if(Layout.m_BackgroundEnabled)
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)).WithMultipliedAlpha(ModuleAlpha), Corners, 5.0f * Scale);
	else
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f * ModuleAlpha), Corners, 5.0f * Scale);
	float y = Rect.y + LineSpacer * 2.0f;
	const float StartX = Rect.x;
	const float RightX = Rect.x + Rect.w - 2.0f * Scale;
	(void)StartX;
	// bestclient

	bool HasDummyInfo = false;
	CMovementInformation DummyInfo{};

	if(Client()->DummyConnected())
	{
		int DummyClientId = -1;

		if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			const int SpectId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

			if(SpectId == GameClient()->m_aLocalIds[0])
			{
				DummyClientId = GameClient()->m_aLocalIds[1];
			}
			else if(SpectId == GameClient()->m_aLocalIds[1])
			{
				DummyClientId = GameClient()->m_aLocalIds[0];
			}
			else
			{
				DummyClientId = GameClient()->m_aLocalIds[1 - (g_Config.m_ClDummy ? 1 : 0)];
			}
		}
		else
		{
			DummyClientId = GameClient()->m_aLocalIds[1 - (g_Config.m_ClDummy ? 1 : 0)];
		}

		if(DummyClientId >= 0 && DummyClientId < MAX_CLIENTS &&
			GameClient()->m_aClients[DummyClientId].m_Active)
		{
			DummyInfo = GetMovementInformation(
				DummyClientId,
				DummyClientId == GameClient()->m_aLocalIds[1]);
			HasDummyInfo = true;
		}
	}

	const bool ShowDummyPos = HasDummyInfo && g_Config.m_ClShowhudPlayerPosition && g_Config.m_TcShowhudDummyPosition;
	const bool ShowDummySpeed = HasDummyInfo && !PosOnly && g_Config.m_ClShowhudPlayerSpeed && g_Config.m_TcShowhudDummySpeed;
	const bool ShowDummyAngle = HasDummyInfo && !PosOnly && g_Config.m_ClShowhudPlayerAngle && g_Config.m_TcShowhudDummyAngle;

	CMovementInformation Info;
	if(ForcePreview && (ClientId < 0 || ClientId >= MAX_CLIENTS))
	{
		Info.m_Pos = vec2(163.03f, 51.53f);
		Info.m_Speed = vec2(0.0f, 0.0f);
		Info.m_Angle = 17.69f;
	}
	else
	{
		Info = GetMovementInformation(ClientId, g_Config.m_ClDummy);
	}

	const float LeftX = Rect.x + 2.0f * Scale;

	if(g_Config.m_ClShowhudPlayerPosition)
	{
		TextRender()->Text(LeftX, y, Fontsize, Localize("Position:"), -1.0f);
		y += LineHeight;

		TextRender()->Text(LeftX, y, Fontsize, "X:", -1.0f);
		UpdateMovementInformationTextContainer(m_aPlayerPositionContainers[0], Fontsize, Info.m_Pos.x, m_aPlayerPrevPosition[0]);

		ColorRGBA TextColor = TextRender()->DefaultTextColor();
		if(ShowDummyPos && fabsf(Info.m_Pos.x - DummyInfo.m_Pos.x) < 0.01f)
			TextColor = ColorRGBA(0.2f, 1.0f, 0.2f, 1.0f);

		RenderMovementInformationTextContainer(m_aPlayerPositionContainers[0], TextColor, RightX, y);
		y += LineHeight;

		TextRender()->Text(LeftX, y, Fontsize, "Y:", -1.0f);
		UpdateMovementInformationTextContainer(m_aPlayerPositionContainers[1], Fontsize, Info.m_Pos.y, m_aPlayerPrevPosition[1]);
		RenderMovementInformationTextContainer(m_aPlayerPositionContainers[1], TextRender()->DefaultTextColor(), RightX, y);
		y += LineHeight;

		if(ShowDummyPos)
		{
			char aBuf[32];

			TextRender()->Text(LeftX, y, Fontsize, "DX:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Pos.x);

			ColorRGBA DummyTextColor = TextRender()->DefaultTextColor();
			if(fabsf(Info.m_Pos.x - DummyInfo.m_Pos.x) < 0.01f)
				DummyTextColor = ColorRGBA(0.2f, 1.0f, 0.2f, 1.0f);

			TextRender()->TextColor(DummyTextColor);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			y += LineHeight;

			TextRender()->Text(LeftX, y, Fontsize, "DY:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Pos.y);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			y += LineHeight;
		}
	}

	// bestclient
	auto RenderCheckpoint = [&]() {
		if(!ShowCheckpoint)
			return;
		const int LiveCheckpoint = HudExtras::GetCheckpointId(GameClient());
		const int Checkpoint = (ForcePreview && LiveCheckpoint < 0) ? 1 : LiveCheckpoint;
		TextRender()->Text(LeftX, y, Fontsize, Localize("Checkpoint:"), -1.0f);
		if(Checkpoint < 0)
		{
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, "No Info", -1, -1.0f), y, Fontsize, "No Info", -1.0f);
		}
		else
		{
			char aBuf[32];
			str_format(aBuf, sizeof(aBuf), "%d", Checkpoint);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
		}
		y += LineHeight;
	};
	// bestclient

	if(PosOnly)
	{
		// bestclient
		RenderCheckpoint();
		// bestclient
		return;
	}

	if(g_Config.m_ClShowhudPlayerSpeed)
	{
		TextRender()->Text(LeftX, y, Fontsize, Localize("Speed:"), -1.0f);
		y += LineHeight;

		const char aaCoordinates[][4] = {"X:", "Y:"};
		for(int i = 0; i < 2; i++)
		{
			ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
			if(m_aLastPlayerSpeedChange[i] == ESpeedChange::INCREASE)
				Color = ColorRGBA(0.0f, 1.0f, 0.0f, 1.0f);
			if(m_aLastPlayerSpeedChange[i] == ESpeedChange::DECREASE)
				Color = ColorRGBA(1.0f, 0.5f, 0.5f, 1.0f);
			TextRender()->Text(LeftX, y, Fontsize, aaCoordinates[i], -1.0f);
			UpdateMovementInformationTextContainer(m_aPlayerSpeedTextContainers[i], Fontsize, i == 0 ? Info.m_Speed.x : Info.m_Speed.y, m_aPlayerPrevSpeed[i]);
			RenderMovementInformationTextContainer(m_aPlayerSpeedTextContainers[i], Color, RightX, y);
			y += LineHeight;
		}

		if(ShowDummySpeed)
		{
			char aBuf[32];

			TextRender()->Text(LeftX, y, Fontsize, "DX:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Speed.x);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			y += LineHeight;

			TextRender()->Text(LeftX, y, Fontsize, "DY:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Speed.y);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			y += LineHeight;
		}

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if(g_Config.m_ClShowhudPlayerAngle)
	{
		TextRender()->Text(LeftX, y, Fontsize, Localize("Angle:"), -1.0f);
		y += LineHeight;

		UpdateMovementInformationTextContainer(m_PlayerAngleTextContainerIndex, Fontsize, Info.m_Angle, m_PlayerPrevAngle);
		RenderMovementInformationTextContainer(m_PlayerAngleTextContainerIndex, TextRender()->DefaultTextColor(), RightX, y);
		y += LineHeight;

		if(ShowDummyAngle)
		{
			char aBuf[32];

			TextRender()->Text(LeftX, y, Fontsize, "DA:", -1.0f);
			str_format(aBuf, sizeof(aBuf), "%.2f", DummyInfo.m_Angle);
			TextRender()->Text(RightX - TextRender()->TextWidth(Fontsize, aBuf), y, Fontsize, aBuf, -1.0f);
			y += LineHeight;
		}
	}

	// bestclient
	RenderCheckpoint();
	// bestclient
}

void CHud::RenderSpectatorHud()
{
	if(!g_Config.m_ClShowhudSpectator)
		return;

	// TClient
	float AdjustedHeight = m_Height - (g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight : 0.0f);
	// draw the box
	Graphics()->DrawRect(m_Width - 180.0f, AdjustedHeight - 15.0f, 180.0f, 15.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_TL, 5.0f);

	// draw the text
	char aBuf[128];
	if(GameClient()->m_MultiViewActivated)
	{
		str_copy(aBuf, Localize("Multi-View"));
	}
	else if(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
	{
		const auto &Player = GameClient()->m_aClients[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId];
		if(g_Config.m_ClShowIds)
			str_format(aBuf, sizeof(aBuf), Localize("Following %d: %s", "Spectating"), Player.ClientId(), Player.m_aName);
		else
			str_format(aBuf, sizeof(aBuf), Localize("Following %s", "Spectating"), Player.m_aName);
	}
	else
	{
		str_copy(aBuf, Localize("Free-View"));
	}
	TextRender()->Text(m_Width - 174.0f, AdjustedHeight - 15.0f + (15.f - 8.f) / 2.f, 8.0f, aBuf, -1.0f);

	// draw the camera info
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Camera.SpectatingPlayer() && GameClient()->m_Camera.CanUseAutoSpecCamera() && g_Config.m_ClSpecAutoSync)
	{
		bool AutoSpecCameraEnabled = GameClient()->m_Camera.m_AutoSpecCamera;
		const char *pLabelText = Localize("AUTO", "Spectating Camera Mode Icon");
		const float TextWidth = TextRender()->TextWidth(6.0f, pLabelText);

		constexpr float RightMargin = 4.0f;
		constexpr float IconWidth = 6.0f;
		constexpr float Padding = 3.0f;
		const float TagWidth = IconWidth + TextWidth + Padding * 3.0f;
		const float TagX = m_Width - RightMargin - TagWidth;
		Graphics()->DrawRect(TagX, m_Height - 12.0f, TagWidth, 10.0f, ColorRGBA(1.0f, 1.0f, 1.0f, AutoSpecCameraEnabled ? 0.50f : 0.10f), IGraphics::CORNER_ALL, 2.5f);
		TextRender()->TextColor(1, 1, 1, AutoSpecCameraEnabled ? 1.0f : 0.65f);
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->Text(TagX + Padding, m_Height - 10.0f, 6.0f, FontIcon::CAMERA, -1.0f);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		TextRender()->Text(TagX + Padding + IconWidth + Padding, m_Height - 10.0f, 6.0f, pLabelText, -1.0f);
		TextRender()->TextColor(1, 1, 1, 1);
	}
}

void CHud::RenderLocalTime(bool ForcePreview)
{
	// bestclient
	if(!HudLayout::IsEnabled(HudLayout::MODULE_LOCAL_TIME))
		return;
	if(!ForcePreview && !g_Config.m_ClShowLocalTimeAlways && !GameClient()->m_Scoreboard.IsActive())
		return;

	const CUIRect Rect = GetLocalTimeRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_LOCAL_TIME, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_LOCAL_TIME);
	const bool Seconds = g_Config.m_TcShowLocalTimeSeconds;
	char aTimeStr[16];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), Seconds ? "%H:%M.%S" : "%H:%M");
	const float FontSize = 5.0f * Scale;
	const float Padding = 5.0f * Scale;
	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Rect.x, Rect.y, Rect.w, Rect.h, m_Width, m_Height);
	if(Layout.m_BackgroundEnabled)
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, color_cast<ColorRGBA>(ColorHSLA(Layout.m_BackgroundColor, true)).WithMultipliedAlpha(ModuleAlpha), Corners, 3.75f * Scale);
	TextRender()->Text(Rect.x + Padding, Rect.y + (Rect.h - FontSize) / 2.f, FontSize, aTimeStr, -1.0f);
	// bestclient
}

// bestclient
CUIRect CHud::GetLocalTimeRect(bool ForcePreview) const
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_LOCAL_TIME))
		return {0.0f, 0.0f, 0.0f, 0.0f};
	if(!ForcePreview && !g_Config.m_ClShowLocalTimeAlways && !GameClient()->m_Scoreboard.IsActive())
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_LOCAL_TIME, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const bool HasOverride = HudLayout::HasPositionOverride(HudLayout::MODULE_LOCAL_TIME);
	const float x = HasOverride ? Layout.m_X : (m_Width / 7.0f) * 3.0f;
	const float y = HasOverride ? Layout.m_Y : 0.0f;
	const bool Seconds = g_Config.m_TcShowLocalTimeSeconds;
	char aTimeStr[16];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), Seconds ? "%H:%M.%S" : "%H:%M");
	const float FontSize = 5.0f * Scale;
	const float Padding = 5.0f * Scale;
	const float Width = std::round(TextRender()->TextBoundingBox(FontSize, aTimeStr).m_W);
	return PushHudRectFromMusicPlayer(GameClient(), {x - Width - Padding * 3.0f, y, Width + Padding * 2.0f, 12.5f * Scale}, m_Width, m_Height, ForcePreview);
}
// bestclient

// bestclient
CUIRect CHud::GetMovementInformationRect(bool ForcePreview) const
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_MOVEMENT_INFO))
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const int ClientId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
	const bool PosOnly = !ForcePreview && (ClientId == SPEC_FREEVIEW || (ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_SpecCharPresent));
	const bool ShowCheckpoint = g_Config.m_BcShowCorrectCheckpoint != 0;
	if(!ForcePreview && !g_Config.m_ClShowhudPlayerPosition && (PosOnly || (!g_Config.m_ClShowhudPlayerSpeed && !g_Config.m_ClShowhudPlayerAngle)) && !ShowCheckpoint)
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_MOVEMENT_INFO, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float BoxWidth = 62.0f * Scale;
	float BoxHeight = GetMovementInformationBoxHeight() * Scale;
	if(ForcePreview && BoxHeight <= 0.0f)
		BoxHeight = (3.0f * MOVEMENT_INFORMATION_LINE_HEIGHT + 2.0f) * Scale;

	if(BoxHeight <= 0.0f)
		return {0.0f, 0.0f, 0.0f, 0.0f};

	HudLayout::SModuleRect RawRect;
	if(!HudLayout::HasPositionOverride(HudLayout::MODULE_MOVEMENT_INFO))
	{
		RawRect = {m_Width - BoxWidth, 285.0f - BoxHeight - 4.0f * Scale, BoxWidth, BoxHeight, 5.0f * Scale};
		const CUIRect ScoreRect = GetScoreHudRect(false);
		if(ScoreRect.h > 0.0f)
			RawRect.m_Y -= ScoreRect.h;
	}
	else
	{
		float X = Layout.m_X;
		float Y = Layout.m_Y;
		if(Layout.m_Mode == HudLayout::POSITION_MODE_BOTTOM_RIGHT)
		{
			X -= BoxWidth;
			Y -= BoxHeight;
		}
		RawRect = {X, Y, BoxWidth, BoxHeight, 5.0f * Scale};
	}
	const auto ClampedRect = HudLayout::ClampRectToScreen(RawRect, m_Width, m_Height);
	return PushHudRectFromMusicPlayer(GameClient(), {ClampedRect.m_X, ClampedRect.m_Y, ClampedRect.m_W, ClampedRect.m_H}, m_Width, m_Height, ForcePreview);
}
// bestclient

// bestclient
void CHud::GetFrozenTeamCounts(int &NumInTeam, int &NumFrozen, int &LocalTeamId, int *pNumUnfreezing) const
{
	NumInTeam = 0;
	NumFrozen = 0;
	LocalTeamId = 0;
	if(pNumUnfreezing)
		*pNumUnfreezing = 0;
	if(GameClient()->m_Snap.m_LocalClientId >= 0 && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId >= 0)
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active == 1 && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != -1)
			LocalTeamId = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId);
		else
			LocalTeamId = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId);
	}
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;
		if(GameClient()->m_Teams.Team(i) == LocalTeamId)
		{
			NumInTeam++;
			if(GameClient()->m_aClients[i].m_FreezeEnd > 0 || GameClient()->m_aClients[i].m_DeepFrozen)
			{
				NumFrozen++;
				if(pNumUnfreezing && !GameClient()->m_aClients[i].m_RegularPredicted.m_IsInFreeze)
					(*pNumUnfreezing)++;
			}
		}
	}
}
// bestclient

// bestclient
CUIRect CHud::GetNotifyLastRect(bool ForcePreview) const
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_NOTIFY_LAST))
		return {0.0f, 0.0f, 0.0f, 0.0f};
	if(!ForcePreview && g_Config.m_TcNotifyWhenLast <= 0)
		return {0.0f, 0.0f, 0.0f, 0.0f};
	if(!ForcePreview)
	{
		if(!GameClient()->m_GameInfo.m_EntitiesDDRace)
			return {0.0f, 0.0f, 0.0f, 0.0f};
		int NumInTeam = 0, NumFrozen = 0, NumUnfreezing = 0, LocalTeamId = 0;
		GetFrozenTeamCounts(NumInTeam, NumFrozen, LocalTeamId, &NumUnfreezing);
		if(NumInTeam <= 1 || NumInTeam - NumFrozen != 1 || NumUnfreezing != 0)
			return {0.0f, 0.0f, 0.0f, 0.0f};
	}

	const auto Layout = HudLayout::Get(HudLayout::MODULE_NOTIFY_LAST, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float FontSize = std::clamp(g_Config.m_TcNotifyWhenLastSize * Scale, 1.0f, 50.0f * Scale);
	const char *pText = g_Config.m_TcNotifyWhenLastText[0] != '\0' ? g_Config.m_TcNotifyWhenLastText : "Last!";
	const float TextWidth = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
	CUIRect Rect;
	Rect.x = Layout.m_X;
	Rect.y = Layout.m_Y;
	Rect.w = std::max(TextWidth, 8.0f);
	Rect.h = FontSize + 2.0f * Scale;
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, m_Width - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, m_Height - Rect.h));
	return PushHudRectFromMusicPlayer(GameClient(), Rect, m_Width, m_Height, ForcePreview);
}
// bestclient

// bestclient
void CHud::RenderNotifyLast(bool ForcePreview)
{
	const CUIRect Rect = GetNotifyLastRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;
	const auto Layout = HudLayout::Get(HudLayout::MODULE_NOTIFY_LAST, m_Width, m_Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_NOTIFY_LAST);
	const float FontSize = std::clamp(g_Config.m_TcNotifyWhenLastSize * Scale, 1.0f, 50.0f * Scale);
	const char *pText = g_Config.m_TcNotifyWhenLastText[0] != '\0' ? g_Config.m_TcNotifyWhenLastText : "Last!";
	TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcNotifyWhenLastColor)).WithMultipliedAlpha(ModuleAlpha));
	TextRender()->Text(Rect.x, Rect.y, FontSize, pText, -1.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	(void)Layout;
}
// bestclient

// bestclient
CUIRect CHud::GetFrozenHudRect(bool ForcePreview) const
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_FROZEN_HUD))
		return {0.0f, 0.0f, 0.0f, 0.0f};
	int NumInTeam, NumFrozen, LocalTeamId;
	GetFrozenTeamCounts(NumInTeam, NumFrozen, LocalTeamId);
	if(ForcePreview && NumInTeam <= 0)
		NumInTeam = 3;
	if(!ForcePreview && (NumInTeam <= 0 || !GameClient()->m_GameInfo.m_EntitiesDDRace || g_Config.m_TcShowFrozenHud <= 0 ||
				 GameClient()->m_Scoreboard.IsShown() || (LocalTeamId == 0 && g_Config.m_TcFrozenHudTeamOnly)))
		return {0.0f, 0.0f, 0.0f, 0.0f};

	const auto Layout = HudLayout::Get(HudLayout::MODULE_FROZEN_HUD, m_Width, m_Height);
	const float Scale = GetFrozenHudScale(Layout);
	const float TeeSize = g_Config.m_TcFrozenHudTeeSize * Scale;
	int MaxTees = (int)(8.3f * (m_Width / m_Height) * 13.0f / TeeSize);
	if(!g_Config.m_ClShowfps && !g_Config.m_ClShowpred)
		MaxTees = (int)(9.5f * (m_Width / m_Height) * 13.0f / TeeSize);
	MaxTees = std::max(MaxTees, 1);
	const int MaxRows = std::max(g_Config.m_TcFrozenMaxRows, 1);
	const int TotalRows = std::max(1, std::min(MaxRows, (NumInTeam + MaxTees - 1) / MaxTees));
	const int ExpandDir = FrozenHudExpandDir();
	CUIRect Rect;
	Rect.w = TeeSize * std::min(NumInTeam, MaxTees);
	Rect.h = TeeSize + 3.0f * Scale + (TotalRows - 1) * TeeSize;
	Rect.y = Layout.m_Y;
	if(ExpandDir == FROZEN_HUD_EXPAND_LEFT)
		Rect.x = Layout.m_X - Rect.w + TeeSize / 2.0f;
	else if(ExpandDir == FROZEN_HUD_EXPAND_CENTER)
		Rect.x = Layout.m_X - Rect.w / 2.0f;
	else
		Rect.x = Layout.m_X - TeeSize / 2.0f;
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, m_Width - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, m_Height - Rect.h));
	return PushHudRectFromMusicPlayer(GameClient(), Rect, m_Width, m_Height, ForcePreview);
}
// bestclient

// bestclient
void CHud::RenderFrozenHud(bool ForcePreview)
{
	if(!HudLayout::IsEnabled(HudLayout::MODULE_FROZEN_HUD))
		return;
	int NumInTeam, NumFrozen, LocalTeamId;
	GetFrozenTeamCounts(NumInTeam, NumFrozen, LocalTeamId);
	if(ForcePreview && NumInTeam <= 0)
	{
		NumInTeam = 3;
		NumFrozen = 1;
	}
	if(!ForcePreview && (NumInTeam <= 0 || !GameClient()->m_GameInfo.m_EntitiesDDRace || g_Config.m_TcShowFrozenHud <= 0 ||
				 GameClient()->m_Scoreboard.IsShown() || (LocalTeamId == 0 && g_Config.m_TcFrozenHudTeamOnly)))
		return;

	const CUIRect Rect = GetFrozenHudRect(ForcePreview);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const auto Layout = HudLayout::Get(HudLayout::MODULE_FROZEN_HUD, m_Width, m_Height);
	const float Scale = GetFrozenHudScale(Layout);
	const float ModuleAlpha = HudLayout::AlphaFactor(HudLayout::MODULE_FROZEN_HUD);
	CTeeRenderInfo FreezeInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find("x_ninja");
	FreezeInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	FreezeInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	FreezeInfo.m_BloodColor = pSkin->m_BloodColor;
	FreezeInfo.m_SkinMetrics = pSkin->m_Metrics;
	FreezeInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
	FreezeInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	FreezeInfo.m_CustomColoredSkin = false;

	const float TeeSize = g_Config.m_TcFrozenHudTeeSize * Scale;
	int MaxTees = (int)(8.3f * (m_Width / m_Height) * 13.0f / TeeSize);
	if(!g_Config.m_ClShowfps && !g_Config.m_ClShowpred)
		MaxTees = (int)(9.5f * (m_Width / m_Height) * 13.0f / TeeSize);
	MaxTees = std::max(MaxTees, 1);
	const int MaxRows = std::max(g_Config.m_TcFrozenMaxRows, 1);
	const bool Overflow = NumInTeam > MaxTees * MaxRows;
	const int ExpandDir = FrozenHudExpandDir();
	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Rect.x, Rect.y, Rect.w, Rect.h, m_Width, m_Height);
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.4f * ModuleAlpha);
	Graphics()->DrawRectExt(Rect.x, Rect.y, Rect.w, Rect.h, 5.0f * Scale, Corners);
	Graphics()->QuadsEnd();

	float ProgressiveOffset = 0.0f;
	int NumDisplayed = 0;
	int NumInRow = 0;
	int CurrentRow = 0;
	const int PreviewClientId = GameClient()->m_Snap.m_LocalClientId >= 0 ? GameClient()->m_Snap.m_LocalClientId : 0;

	for(int OverflowIndex = 0; OverflowIndex < 1 + Overflow; OverflowIndex++)
	{
		for(int i = 0; i < MAX_CLIENTS && NumDisplayed < MaxTees * MaxRows; i++)
		{
			const bool PreviewTee = ForcePreview && !GameClient()->m_Snap.m_apPlayerInfos[i] && i < NumInTeam;
			if(!PreviewTee && !GameClient()->m_Snap.m_apPlayerInfos[i])
				continue;
			if(!PreviewTee && GameClient()->m_Teams.Team(i) != LocalTeamId)
				continue;

			bool Frozen = PreviewTee ? (i < NumFrozen) : (GameClient()->m_aClients[i].m_FreezeEnd > 0 || GameClient()->m_aClients[i].m_DeepFrozen);
			CTeeRenderInfo TeeInfo = GameClient()->m_aClients[PreviewTee ? PreviewClientId : i].m_RenderInfo;
			if(Frozen && !g_Config.m_TcShowFrozenHudSkins)
				TeeInfo = FreezeInfo;
			if(Overflow && Frozen && OverflowIndex == 0)
				continue;
			if(Overflow && !Frozen && OverflowIndex == 1)
				continue;

			TeeInfo.m_Size = TeeSize;
			const CAnimState *pIdleState = CAnimState::GetIdle();
			vec2 OffsetToMid;
			CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
			float TeeX;
			if(ExpandDir == FROZEN_HUD_EXPAND_LEFT)
				TeeX = Rect.x + Rect.w - ProgressiveOffset - TeeSize / 2.0f;
			else
				TeeX = Rect.x + ProgressiveOffset + TeeSize / 2.0f;
			vec2 TeeRenderPos(TeeX, Rect.y + CurrentRow * TeeSize + TeeSize / 2.0f + OffsetToMid.y);
			RenderTools()->RenderTee(pIdleState, &TeeInfo, Frozen ? EMOTE_PAIN : EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos, 1.0f);

			NumDisplayed++;
			NumInRow++;
			ProgressiveOffset += TeeSize;
			if(NumInRow >= MaxTees)
			{
				NumInRow = 0;
				ProgressiveOffset = 0.0f;
				CurrentRow++;
			}
		}
	}
}
// bestclient

void CHud::OnNewSnapshot()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return;

	int ClientId = -1;
	if(GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		ClientId = GameClient()->m_Snap.m_LocalClientId;
	else if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		ClientId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

	if(ClientId == -1)
		return;

	const CNetObj_Character *pPrevChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Prev;
	const CNetObj_Character *pCurChar = &GameClient()->m_Snap.m_aCharacters[ClientId].m_Cur;
	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
	ivec2 Vel = mix(ivec2(pPrevChar->m_VelX, pPrevChar->m_VelY), ivec2(pCurChar->m_VelX, pCurChar->m_VelY), IntraTick);

	CCharacter *pChar = GameClient()->m_PredictedWorld.GetCharacterById(ClientId);
	if(pChar && pChar->IsGrounded())
		Vel.y = 0;

	int aVels[2] = {Vel.x, Vel.y};

	for(int i = 0; i < 2; i++)
	{
		int AbsVel = abs(aVels[i]);
		if(AbsVel > m_aPlayerSpeed[i])
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::INCREASE;
		}
		if(AbsVel < m_aPlayerSpeed[i])
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::DECREASE;
		}
		if(AbsVel < 2)
		{
			m_aLastPlayerSpeedChange[i] = ESpeedChange::NONE;
		}
		m_aPlayerSpeed[i] = AbsVel;
	}
}

void CHud::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return;

	m_Width = 300.0f * Graphics()->ScreenAspect();
	m_Height = 300.0f;
	// bestclient
	if(m_LastHudWidth >= 0.0f && absolute(m_LastHudWidth - m_Width) > 0.01f)
		ResetHudContainers();
	m_LastHudWidth = m_Width;
	// bestclient
	Graphics()->MapScreenToSize(m_Width, m_Height);

	// bestclient
	const bool HudEditorActive = GameClient()->m_HudEditor.IsActive();
	// bestclient

#if defined(CONF_VIDEORECORDER)
	if((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud))
#else
	if(g_Config.m_ClShowhud)
#endif
	{
		// bestclient
		const bool FocusModeActive = g_Config.m_ClFocusMode;
		const bool HideHudInFocusMode = g_Config.m_ClFocusModeHideHud;
		const bool HideUIInFocusMode = g_Config.m_ClFocusModeHideUI;

		if(FocusModeActive && HideHudInFocusMode)
		{
			RenderCursor();
			return;
		}
		// bestclient

		if(GameClient()->m_Snap.m_pLocalCharacter && !GameClient()->m_Snap.m_SpecInfo.m_Active && !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			if(g_Config.m_ClShowhudHealthAmmo)
			{
				RenderAmmoHealthAndArmor(GameClient()->m_Snap.m_pLocalCharacter);
			}
			if(GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_HasExtendedData && g_Config.m_ClShowhudDDRace && GameClient()->m_GameInfo.m_HudDDRace)
			{
				RenderPlayerState(GameClient()->m_Snap.m_LocalClientId);
			}
			// bestclient
			if(!HudEditorActive && !(FocusModeActive && HideUIInFocusMode))
				RenderSpectatorCount();
			if(!HudEditorActive)
				RenderMovementInformation();
			// bestclient
			RenderDDRaceEffects();
		}
		else if(GameClient()->m_Snap.m_SpecInfo.m_Active)
		{
			int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
			if(SpectatorId != SPEC_FREEVIEW && g_Config.m_ClShowhudHealthAmmo)
			{
				RenderAmmoHealthAndArmor(&GameClient()->m_Snap.m_aCharacters[SpectatorId].m_Cur);
			}
			if(SpectatorId != SPEC_FREEVIEW &&
				GameClient()->m_Snap.m_aCharacters[SpectatorId].m_HasExtendedData &&
				g_Config.m_ClShowhudDDRace &&
				(!GameClient()->m_MultiViewActivated || GameClient()->m_MultiViewShowHud) &&
				GameClient()->m_GameInfo.m_HudDDRace)
			{
				RenderPlayerState(SpectatorId);
			}
			// bestclient
			if(!HudEditorActive)
				RenderMovementInformation();
			// bestclient
			RenderSpectatorHud();
		}

		if(g_Config.m_ClShowhudTimer)
			RenderGameTimer();
		RenderPauseNotification();
		RenderSuddenDeath();
		// bestclient
		if(!HudEditorActive)
		{
			RenderScoreHud();
			if(!(FocusModeActive && HideUIInFocusMode))
				RenderDummyActions();
			RenderFrozenHud();
			RenderNotifyLast();
			if(!(FocusModeActive && HideUIInFocusMode))
				RenderLocalTime();
			GameClient()->m_Voting.Render();
		}
		// bestclient
		CornerIndicator::Render(GameClient());
		// bestclient
		RenderWarmupTimer();
		RenderTextInfo();
		GameClient()->m_TClient.RenderCenterLines();
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK)
			RenderConnectionWarning();
		RenderTeambalanceWarning();
		// bestclient
		GameClient()->m_VoiceChat.RenderHudMuteStatusIndicator(m_Width, m_Height);
		GameClient()->m_VoiceChat.RenderHudTalkingIndicator(m_Width, m_Height);
		// bestclient
		if(g_Config.m_ClShowRecord)
			RenderRecord();
	}
	// bestclient
	GameClient()->m_BestClient.RenderSpecMovedNotify();
	HudExtras::RenderPlayerBelowIndicator(GameClient(), Graphics(), TextRender(), m_Width, m_Height, Client()->RenderFrameTime());
	// bestclient
	RenderCursor();
}

void CHud::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_DDRACETIME || MsgType == NETMSGTYPE_SV_DDRACETIMELEGACY)
	{
		CNetMsg_Sv_DDRaceTime *pMsg = (CNetMsg_Sv_DDRaceTime *)pRawMsg;

		m_DDRaceTime = pMsg->m_Time;

		m_ShowFinishTime = pMsg->m_Finish != 0;

		if(!m_ShowFinishTime)
		{
			m_TimeCpDiff = (float)pMsg->m_Check / 100;
			m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
		else
		{
			m_FinishTimeDiff = (float)pMsg->m_Check / 100;
			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
		}
	}
	else if(MsgType == NETMSGTYPE_SV_RECORD || MsgType == NETMSGTYPE_SV_RECORDLEGACY)
	{
		CNetMsg_Sv_Record *pMsg = (CNetMsg_Sv_Record *)pRawMsg;

		// NETMSGTYPE_SV_RACETIME on old race servers
		if(MsgType == NETMSGTYPE_SV_RECORDLEGACY && GameClient()->m_GameInfo.m_DDRaceRecordMessage)
		{
			m_DDRaceTime = pMsg->m_ServerTimeBest; // First value: m_Time

			m_FinishTimeLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);

			if(pMsg->m_PlayerTimeBest) // Second value: m_Check
			{
				m_TimeCpDiff = (float)pMsg->m_PlayerTimeBest / 100;
				m_TimeCpLastReceivedTick = Client()->GameTick(g_Config.m_ClDummy);
			}
		}
		else if(MsgType == NETMSGTYPE_SV_RECORD || GameClient()->m_GameInfo.m_RaceRecordMessage)
		{
			// ignore m_ServerTimeBest, it's handled by the game client
			m_aPlayerRecord[g_Config.m_ClDummy] = (float)pMsg->m_PlayerTimeBest / 100;
		}
	}
}

void CHud::RenderDDRaceEffects()
{
	if(m_DDRaceTime)
	{
		char aBuf[64];
		char aTime[32];
		if(m_ShowFinishTime && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			str_time(m_DDRaceTime, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "Finish time: %s", aTime);

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_FinishTimeLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			TextRender()->TextColor(1, 1, 1, Alpha);
			CTextCursor Cursor;
			Cursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(12, aBuf) / 2, 20));
			Cursor.m_FontSize = 12.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);
			if(m_FinishTimeDiff != 0.0f && m_DDRaceEffectsTextContainerIndex.Valid())
			{
				if(m_FinishTimeDiff < 0)
				{
					str_time_float(-m_FinishTimeDiff, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "-%s", aTime);
					TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
				}
				else
				{
					str_time_float(m_FinishTimeDiff, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
					str_format(aBuf, sizeof(aBuf), "+%s", aTime);
					TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
				}
				CTextCursor DiffCursor;
				DiffCursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf) / 2, 34));
				DiffCursor.m_FontSize = 10.0f;
				TextRender()->AppendTextContainer(m_DDRaceEffectsTextContainerIndex, &DiffCursor, aBuf);
			}
			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else if(g_Config.m_ClShowhudTimeCpDiff && !m_ShowFinishTime && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
		{
			if(m_TimeCpDiff < 0)
			{
				str_time_float(-m_TimeCpDiff, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "-%s", aTime);
			}
			else
			{
				str_time_float(m_TimeCpDiff, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
				str_format(aBuf, sizeof(aBuf), "+%s", aTime);
			}

			// calculate alpha (4 sec 1 than get lower the next 2 sec)
			float Alpha = 1.0f;
			if(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 4 < Client()->GameTick(g_Config.m_ClDummy) && m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6 > Client()->GameTick(g_Config.m_ClDummy))
			{
				// lower the alpha slowly to blend text out
				Alpha = ((float)(m_TimeCpLastReceivedTick + Client()->GameTickSpeed() * 6) - (float)Client()->GameTick(g_Config.m_ClDummy)) / (float)(Client()->GameTickSpeed() * 2);
			}

			if(m_TimeCpDiff > 0)
				TextRender()->TextColor(1.0f, 0.5f, 0.5f, Alpha); // red
			else if(m_TimeCpDiff < 0)
				TextRender()->TextColor(0.5f, 1.0f, 0.5f, Alpha); // green
			else if(!m_TimeCpDiff)
				TextRender()->TextColor(1, 1, 1, Alpha); // white

			CTextCursor Cursor;
			Cursor.SetPosition(vec2(150 * Graphics()->ScreenAspect() - TextRender()->TextWidth(10, aBuf) / 2, 20));
			Cursor.m_FontSize = 10.0f;
			TextRender()->RecreateTextContainer(m_DDRaceEffectsTextContainerIndex, &Cursor, aBuf);

			if(m_DDRaceEffectsTextContainerIndex.Valid())
			{
				auto OutlineColor = TextRender()->DefaultTextOutlineColor();
				OutlineColor.a *= Alpha;
				TextRender()->RenderTextContainer(m_DDRaceEffectsTextContainerIndex, TextRender()->DefaultTextColor(), OutlineColor);
			}
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	}
}

void CHud::RenderRecord()
{
	if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET && GameClient()->m_MapBestTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
	{
		char aBuf[64];
		TextRender()->Text(5, 75, 6, Localize("Server best:"), -1.0f);
		char aTime[32];
		int64_t TimeCentiseconds = static_cast<int64_t>(GameClient()->m_MapBestTimeSeconds) * 100 + static_cast<int64_t>(GameClient()->m_MapBestTimeMillis) / 10;
		str_time(TimeCentiseconds, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
		str_format(aBuf, sizeof(aBuf), "%s%s", GameClient()->m_MapBestTimeSeconds > 3600 ? "" : "   ", aTime);
		TextRender()->Text(53, 75, 6, aBuf, -1.0f);
	}

	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes)
	{
		const int PlayerTimeSeconds = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_FinishTimeSeconds;
		if(PlayerTimeSeconds != FinishTime::NOT_FINISHED_MILLIS)
		{
			char aBuf[64];
			TextRender()->Text(5, 82, 6, Localize("Personal best:"), -1.0f);
			char aTime[32];
			const int PlayerTimeMillis = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_FinishTimeMillis;
			int64_t TimeCentiseconds = static_cast<int64_t>(PlayerTimeSeconds) * 100 + static_cast<int64_t>(PlayerTimeMillis) / 10;
			str_time(TimeCentiseconds, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "%s%s", PlayerTimeSeconds > 3600 ? "" : "   ", aTime);
			TextRender()->Text(53, 82, 6, aBuf, -1.0f);
		}
	}
	else
	{
		const float PlayerRecord = m_aPlayerRecord[g_Config.m_ClDummy];
		if(PlayerRecord > 0.0f)
		{
			char aBuf[64];
			TextRender()->Text(5, 82, 6, Localize("Personal best:"), -1.0f);
			char aTime[32];
			str_time_float(PlayerRecord, ETimeFormat::HOURS_CENTISECS, aTime, sizeof(aTime));
			str_format(aBuf, sizeof(aBuf), "%s%s", PlayerRecord > 3600 ? "" : "   ", aTime);
			TextRender()->Text(53, 82, 6, aBuf, -1.0f);
		}
	}
}
