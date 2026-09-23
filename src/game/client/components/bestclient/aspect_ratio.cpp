/* Copyright © 2026 BestProject Team */
#include "aspect_ratio.h"

#include <base/color.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>

CAspectRatio::SConfig CAspectRatio::ReadConfig()
{
	return {
		g_Config.m_BcCustomAspectRatioMode,
		g_Config.m_BcCustomAspectRatio,
		g_Config.m_BcCustomAspectRatioApplyMode,
		g_Config.m_BcCustomAspectRatioNum,
		g_Config.m_BcCustomAspectRatioDen,
	};
}

bool CAspectRatio::ConfirmNeeded(const SConfig &Cfg)
{
	return Cfg.m_ApplyMode == 1 && (Cfg.m_Mode > 0 || (Cfg.m_Mode < 0 && Cfg.m_Ratio > 0));
}

void CAspectRatio::ResetToPrevious()
{
	g_Config.m_BcCustomAspectRatioMode = m_Prev.m_Mode;
	g_Config.m_BcCustomAspectRatio = m_Prev.m_Ratio;
	g_Config.m_BcCustomAspectRatioApplyMode = m_Prev.m_ApplyMode;
	g_Config.m_BcCustomAspectRatioNum = m_Prev.m_Num;
	g_Config.m_BcCustomAspectRatioDen = m_Prev.m_Den;
	m_Last = m_Prev;
	m_ConfirmActive = false;
	GameClient()->m_TClient.SetForcedAspect();
}

bool CAspectRatio::IsBlockedByFng() const
{
	const int State = Client()->State();
	if(State != IClient::STATE_ONLINE && State != IClient::STATE_DEMOPLAYBACK)
		return false;

	auto ContainsFng = [](const char *pText) -> bool {
		return pText != nullptr && pText[0] != '\0' && str_find_nocase(pText, "fng") != nullptr;
	};

	CServerInfo ServerInfo = Client()->ServerInfo();
	const CServerInfo *apInfos[3] = {&ServerInfo, nullptr, nullptr};
	int NumInfos = 1;
	if(GameClient()->m_ConnectServerInfo.has_value())
		apInfos[NumInfos++] = &*GameClient()->m_ConnectServerInfo;
	const auto *pEntry = ServerBrowser()->Find(Client()->ServerAddress());
	if(pEntry)
		apInfos[NumInfos++] = &pEntry->m_Info;

	for(int i = 0; i < NumInfos; ++i)
	{
		const CServerInfo *pInfo = apInfos[i];
		if(ContainsFng(pInfo->m_aName) || ContainsFng(pInfo->m_aGameType) || ContainsFng(pInfo->m_aCommunityId) ||
			ContainsFng(pInfo->m_aCommunityCountry) || ContainsFng(pInfo->m_aCommunityType))
			return true;

		if(pInfo->m_aCommunityId[0] != '\0')
		{
			const CCommunity *pCommunity = ServerBrowser()->Community(pInfo->m_aCommunityId);
			if(pCommunity && ContainsFng(pCommunity->Name()))
				return true;
		}
	}

	if(ContainsFng(GameClient()->m_GameInfo.m_aGameType))
		return true;

	return GameClient()->m_GameInfo.m_PredictFNG || GameClient()->m_GameInfo.m_EntitiesFNG;
}

bool CAspectRatio::UseGameNoHudAspect() const
{
	const int State = Client()->State();
	const bool IsActiveGameplay = State == IClient::STATE_ONLINE || State == IClient::STATE_DEMOPLAYBACK;
	return IsActiveGameplay && !IsBlockedByFng() && g_Config.m_BcCustomAspectRatioApplyMode == 2;
}

bool CAspectRatio::ShouldApplyCustomAspect(bool IsActiveGameplay) const
{
	return !IsBlockedByFng() && (g_Config.m_BcCustomAspectRatioApplyMode == 1 || IsActiveGameplay);
}

void CAspectRatio::BeginFrame()
{
	Graphics()->SetScreenAspectOverrideEnabled(true);
	m_HudAspectDisabled = false;
	GameClient()->m_TClient.SetForcedAspect();
}

void CAspectRatio::PrepareComponent(const CComponent *pComponent)
{
	if(UseGameNoHudAspect() && !m_HudAspectDisabled && pComponent == &GameClient()->m_Hud)
	{
		Graphics()->SetScreenAspectOverrideEnabled(false);
		m_HudAspectDisabled = true;
	}
}

void CAspectRatio::BeforeFogRect()
{
	if(UseGameNoHudAspect() && m_HudAspectDisabled)
		Graphics()->SetScreenAspectOverrideEnabled(true);
}

void CAspectRatio::AfterFogRect()
{
	if(UseGameNoHudAspect() && m_HudAspectDisabled)
		Graphics()->SetScreenAspectOverrideEnabled(false);
}

void CAspectRatio::BeginCursorGameAspect()
{
	if(UseGameNoHudAspect())
		Graphics()->SetScreenAspectOverrideEnabled(true);
}

void CAspectRatio::EndCursorGameAspect()
{
	if(UseGameNoHudAspect())
		Graphics()->SetScreenAspectOverrideEnabled(false);
}

void CAspectRatio::UpdateConfirm()
{
	constexpr int CONFIRM_SECONDS = 10;
	const SConfig Cur = ReadConfig();

	if(!m_ConfirmSeeded)
	{
		m_ConfirmSeeded = true;
		m_ConfirmActive = false;
		m_Last = Cur;
		m_Prev = Cur;
	}
	else if(m_Last != Cur)
	{
		const bool WasConfirmActive = m_ConfirmActive;
		if(ConfirmNeeded(Cur) && Cur != m_Prev)
		{
			if(!WasConfirmActive)
				m_Prev = m_Last;
			m_ConfirmActive = true;
			m_ConfirmDeadline = time_get() + time_freq() * CONFIRM_SECONDS;
		}
		else
		{
			m_ConfirmActive = false;
			m_Prev = Cur;
		}
		m_Last = Cur;
	}

	if(m_ConfirmActive && IsBlockedByFng())
		m_ConfirmActive = false;

	if(m_ConfirmActive && time_get() >= m_ConfirmDeadline)
		ResetToPrevious();
}

bool CAspectRatio::ShowConfirmOverlay() const
{
	return m_ConfirmActive && ConfirmNeeded(ReadConfig());
}

bool CAspectRatio::ConfirmWantsInput() const
{
	return ShowConfirmOverlay();
}

void CAspectRatio::ApplyMenuUiAspect(bool MenuActive)
{
	const bool IngameMenu = MenuActive && (Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK);
	const bool UseWindowAspectForUi = IngameMenu && (IsBlockedByFng() || g_Config.m_BcCustomAspectRatioApplyMode != 1);
	m_MenuRestoreGraphicsAspect = !UseWindowAspectForUi;
	Ui()->SetUseGraphicsScreenAspect(m_MenuRestoreGraphicsAspect);
}

void CAspectRatio::RestoreMenuUiAspect()
{
	Ui()->SetUseGraphicsScreenAspect(true);
}

void CAspectRatio::RenderConfirmOverlay(bool MenuActive)
{
	const bool ShowOverlay = ShowConfirmOverlay();
	if(ShowOverlay)
	{
		Ui()->SetUseGraphicsScreenAspect(false);
		Ui()->MapScreen();
		const CUIRect *pReal = Ui()->Screen();

		const float WinW = (float)Graphics()->ScreenWidth();
		const float WinH = (float)Graphics()->ScreenHeight();
		const vec2 RawMouse = Ui()->UpdatedMousePos();
		const vec2 RealMouse = vec2(RawMouse.x * pReal->w / WinW, RawMouse.y * pReal->h / WinH);

		const int SecondsLeft = std::max(0, (int)((m_ConfirmDeadline - time_get() + time_freq() - 1) / time_freq()));
		char aConfirmText[128];
		str_format(aConfirmText, sizeof(aConfirmText), Localize("Keep this aspect ratio? Reverting in %d..."), SecondsLeft);

		const float PanelW = std::min(420.0f, pReal->w - 12.0f);
		CUIRect Panel;
		Panel.x = pReal->x + (pReal->w - PanelW) * 0.5f;
		Panel.y = pReal->y + 6.0f;
		Panel.w = PanelW;
		Panel.h = 30.0f;
		Graphics()->DrawRect(Panel.x, Panel.y, Panel.w, Panel.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.82f), IGraphics::CORNER_ALL, 5.0f);
		Panel.Margin(2.0f, &Panel);

		CUIRect ApplyBtn, RevertBtn;
		Panel.VSplitRight(80.0f, &Panel, &RevertBtn);
		RevertBtn.VMargin(2.0f, &RevertBtn);
		Panel.VSplitRight(4.0f, &Panel, nullptr);
		Panel.VSplitRight(88.0f, &Panel, &ApplyBtn);
		ApplyBtn.VMargin(2.0f, &ApplyBtn);
		Panel.VSplitRight(4.0f, &Panel, nullptr);
		Ui()->DoLabel(&Panel, aConfirmText, 10.0f, TEXTALIGN_ML);

		auto RenderOverlayBtn = [&](const CUIRect &Rect, const char *pText, ColorRGBA Normal, ColorRGBA Hovered) -> bool {
			const bool Hover = RealMouse.x >= Rect.x && RealMouse.x < Rect.x + Rect.w &&
				RealMouse.y >= Rect.y && RealMouse.y < Rect.y + Rect.h;
			Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Hover ? Hovered : Normal, IGraphics::CORNER_ALL, 3.0f);
			Ui()->DoLabel(&Rect, pText, 10.5f, TEXTALIGN_MC);
			return Hover && Ui()->MouseButtonClicked(0);
		};

		if(RenderOverlayBtn(ApplyBtn, Localize("Apply"), ColorRGBA(0.18f, 0.36f, 0.18f, 1.0f), ColorRGBA(0.28f, 0.55f, 0.28f, 1.0f)))
		{
			m_ConfirmActive = false;
			m_Prev = ReadConfig();
		}
		if(RenderOverlayBtn(RevertBtn, Localize("Revert"), ColorRGBA(0.45f, 0.18f, 0.18f, 1.0f), ColorRGBA(0.7f, 0.28f, 0.28f, 1.0f)))
			ResetToPrevious();

		Ui()->SetUseGraphicsScreenAspect(m_MenuRestoreGraphicsAspect);
		Ui()->MapScreen();
	}

	if(MenuActive || ShowOverlay)
	{
		vec2 CursorPos = Ui()->MousePos();
		if(ShowOverlay && !MenuActive)
		{
			const CUIRect *pScreen = Ui()->Screen();
			const vec2 RawMouse = Ui()->UpdatedMousePos();
			CursorPos = vec2(RawMouse.x * pScreen->w / (float)Graphics()->ScreenWidth(), RawMouse.y * pScreen->h / (float)Graphics()->ScreenHeight());
		}
		RenderTools()->RenderCursor(CursorPos, 24.0f);
	}
}
