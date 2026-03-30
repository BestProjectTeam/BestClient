/* (c) BestClient */
#include "cava_hud.h"

#include <base/color.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/hud_layout.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

namespace
{
static float ApproachAnim(float Current, float Target, float Delta, float Speed)
{
	const float Step = 1.0f - expf(-maximum(0.0f, Speed) * maximum(0.0f, Delta));
	return mix(Current, Target, std::clamp(Step, 0.0f, 1.0f));
}

} // namespace

void CCavaHud::OnReset()
{
	for(float &Level : m_vLevels)
		Level = 0.0f;
}

void CCavaHud::OnShutdown()
{
}

void CCavaHud::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE)
		OnReset();
}

void CCavaHud::EnsureBars()
{
	const int Bars = std::clamp(g_Config.m_BcCavaBars, 4, 128);
	if(m_LastBars != Bars)
	{
		m_LastBars = Bars;
		m_vLevels.assign(Bars, 0.0f);
		m_vScratch.assign(Bars, 0.0f);
	}
}

void CCavaHud::UpdateLevelsFromAudio(float Delta)
{
	EnsureBars();
	if(m_vLevels.empty())
		return;

	if(!(GameClient() && GameClient()->m_AudioVisualizer.GetBands((int)m_vLevels.size(), m_vScratch.data())))
	{
		for(float &Level : m_vLevels)
			Level = ApproachAnim(Level, 0.0f, Delta, 7.0f);
		return;
	}

	// CAudioVisualizer already applies gain + attack/decay smoothing; keep HUD as a thin renderer
	// to avoid double-smoothing (which can flatten the frequency response).
	for(int i = 0; i < (int)m_vLevels.size(); ++i)
		m_vLevels[i] = std::clamp(m_vScratch[i], 0.0f, 1.0f);
}

void CCavaHud::OnUpdate()
{
	const bool InGameOnServer = Client()->State() == IClient::STATE_ONLINE && GameClient() && !GameClient()->m_Menus.IsActive();
	if(!InGameOnServer || GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CAVA))
	{
		if(!m_vLevels.empty())
			OnReset();
		return;
	}

	if(g_Config.m_BcCavaEnable == 0 && g_Config.m_DbgCava == 0)
	{
		if(!m_vLevels.empty())
			OnReset();
		return;
	}

	const float Delta = std::clamp(Client()->RenderFrameTime(), 0.0f, 0.1f);
	if(g_Config.m_BcCavaEnable == 0)
	{
		// Debug mode: keep bars allocated and fade out levels.
		EnsureBars();
		for(float &Level : m_vLevels)
			Level = ApproachAnim(Level, 0.0f, Delta, 7.0f);
		return;
	}
	UpdateLevelsFromAudio(Delta);
}

void CCavaHud::OnRender()
{
	const bool InGameOnServer = Client()->State() == IClient::STATE_ONLINE && GameClient() && !GameClient()->m_Menus.IsActive();
	if(!InGameOnServer || GameClient()->m_BestClient.IsComponentDisabled(CBestClient::COMPONENT_VISUALS_CAVA))
		return;

	if(g_Config.m_BcCavaEnable == 0 && g_Config.m_DbgCava == 0)
		return;

	EnsureBars();

	const float HudWidth = 300.0f * Graphics()->ScreenAspect();
	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	Graphics()->MapScreen(0.0f, 0.0f, HudWidth, HudHeight);

	float X = HudLayout::CanvasXToHud((float)g_Config.m_BcCavaX, HudWidth);
	float W = HudLayout::CanvasXToHud((float)g_Config.m_BcCavaW, HudWidth);
	float Y = (float)g_Config.m_BcCavaY;
	float H = (float)g_Config.m_BcCavaH;
	if(g_Config.m_BcCavaDockBottom)
	{
		X = 0.0f;
		W = HudWidth;
		Y = HudHeight - H;
	}

	W = std::clamp(W, 1.0f, maximum(1.0f, HudWidth));
	H = std::clamp(H, 1.0f, maximum(1.0f, HudHeight));
	X = std::clamp(X, 0.0f, maximum(0.0f, HudWidth - W));
	Y = std::clamp(Y, 0.0f, maximum(0.0f, HudHeight - H));

	const int Bars = (int)m_vLevels.size();
	if(Bars <= 0 || W <= 0.0f || H <= 0.0f)
		return;

	const bool CaptureOk = GameClient() && GameClient()->m_AudioVisualizer.IsCaptureValid();
	const bool HasSignal = GameClient() && GameClient()->m_AudioVisualizer.HasSignal();

	unsigned PackedBar = g_Config.m_BcCavaColor;
	if((PackedBar & 0xFF000000u) == 0)
		PackedBar |= 0xFF000000u;
	const ColorRGBA BarColor = color_cast<ColorRGBA>(ColorHSLA(PackedBar, true));

	unsigned PackedBg = g_Config.m_BcCavaBackgroundColor;
	if((PackedBg & 0xFF000000u) == 0)
		PackedBg |= 0xFF000000u;
	const ColorRGBA BgColor = color_cast<ColorRGBA>(ColorHSLA(PackedBg, true));

	Graphics()->TextureClear();
	if(g_Config.m_BcCavaBackground)
	{
		Graphics()->DrawRect(X, Y, W, H, BgColor, IGraphics::CORNER_ALL, 0.0f);
	}

	const float Gap = 1.0f;
	const float TotalGap = Gap * (Bars - 1);
	const float BarW = maximum(0.5f, (W - TotalGap) / (float)Bars);
	const float BarMaxH = maximum(0.0f, H);
	const float BaseY = Y + H;

	for(int i = 0; i < Bars; ++i)
	{
		const float Level = std::clamp(m_vLevels[i], 0.0f, 1.0f);
		const float Bh = maximum(0.5f, BarMaxH * Level);
		const float Bx = X + i * (BarW + Gap);
		const float By = BaseY - Bh;
		Graphics()->DrawRect(Bx, By, BarW, Bh, BarColor, IGraphics::CORNER_ALL, 0.0f);
	}

	if(g_Config.m_DbgCava >= 1)
	{
		// Outline + debug text
		Graphics()->DrawRect(X, Y, W, H, ColorRGBA(1.0f, 0.2f, 0.2f, 0.25f), IGraphics::CORNER_ALL, 0.0f);
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "dbg_cava=%d capture=%s signal=%s bars=%d rect=%.1f %.1f %.1f %.1f",
			g_Config.m_DbgCava,
			CaptureOk ? "ok" : "no",
			HasSignal ? "yes" : "no",
			Bars, X, Y, W, H);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		TextRender()->Text(X + 2.0f, maximum(0.0f, Y - 10.0f), 6.0f, aBuf, -1.0f);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}
