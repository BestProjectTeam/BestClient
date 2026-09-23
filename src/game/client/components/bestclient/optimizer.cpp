/* Copyright © 2026 BestProject Team */
#include "optimizer.h"

#include <base/math.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/mapitems.h>

#include <algorithm>

void COptimizer::ComputeHalfExtents(float &HalfW, float &HalfH) const
{
	HalfW = 0.0f;
	HalfH = 0.0f;

	if(!m_FpsFog)
		return;

	if(g_Config.m_BcOptimizerFpsFogMode == 0)
	{
		const float Radius = (float)g_Config.m_BcOptimizerFpsFogRadiusTiles * 32.0f;
		HalfW = Radius;
		HalfH = Radius;
		return;
	}

	float Width = 0.0f;
	float Height = 0.0f;
	Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &Width, &Height);
	const float Percent = std::clamp(g_Config.m_BcOptimizerFpsFogZoomPercent, 1, 120) / 100.0f;
	HalfW = Width * Percent * 0.5f;
	HalfH = Height * Percent * 0.5f;
}

void COptimizer::SyncHighDetailFromOptimizer()
{
	const bool WantDisableHighDetail = m_Enabled && g_Config.m_BcOptimizerDisableHighDetail != 0;
	if(WantDisableHighDetail)
	{
		if(g_Config.m_GfxHighDetail)
		{
			g_Config.m_GfxHighDetail = 0;
			m_RestoreHighDetailOnRelease = true;
		}
	}
	else if(m_RestoreHighDetailOnRelease)
	{
		g_Config.m_GfxHighDetail = 1;
		m_RestoreHighDetailOnRelease = false;
	}
}

void COptimizer::OnOptimizerDisableHighDetailToggled()
{
	if(g_Config.m_BcOptimizerDisableHighDetail)
	{
		if(g_Config.m_GfxHighDetail)
			m_RestoreHighDetailOnRelease = true;
		g_Config.m_GfxHighDetail = 0;
	}
	else
	{
		g_Config.m_GfxHighDetail = 1;
		m_RestoreHighDetailOnRelease = false;
	}
}

void COptimizer::OnGfxHighDetailToggled()
{
	if(g_Config.m_GfxHighDetail)
	{
		g_Config.m_BcOptimizerDisableHighDetail = 0;
		m_RestoreHighDetailOnRelease = false;
	}
	else
	{
		g_Config.m_BcOptimizerDisableHighDetail = 1;
	}
}

void COptimizer::RefreshFrame()
{
	m_Enabled = g_Config.m_BcOptimizer != 0;
	m_DisableParticles = m_Enabled && g_Config.m_BcOptimizerDisableParticles != 0;
	m_DisableQuads = m_Enabled && g_Config.m_BcOptimizerDisableQuads != 0;
	m_FpsFog = m_Enabled && g_Config.m_BcOptimizerFpsFog != 0;
	m_CullMapTiles = m_FpsFog && g_Config.m_BcOptimizerFpsFogCullMapTiles != 0;
	m_Center = GameClient()->m_Camera.m_Center;

	SyncHighDetailFromOptimizer();

	if(m_FpsFog)
		ComputeHalfExtents(m_HalfW, m_HalfH);
	else
	{
		m_HalfW = 0.0f;
		m_HalfH = 0.0f;
	}

	if(m_DisableParticles && !m_WasDisableParticles)
	{
		GameClient()->m_Particles.OnReset();
		GameClient()->m_3DParticles.OnReset();
	}
	m_WasDisableParticles = m_DisableParticles;
}

void COptimizer::OnConsoleInit()
{
	Console()->Chain("bc_optimizer", ConchainOptimizer, this);
	Console()->Chain("bc_optimizer_disable_high_detail", ConchainDisableHighDetail, this);
	Console()->Chain("gfx_high_detail", ConchainGfxHighDetail, this);
}

void COptimizer::ConchainOptimizer(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	COptimizer *pSelf = (COptimizer *)pUserData;
	pSelf->m_Enabled = g_Config.m_BcOptimizer != 0;
	pSelf->SyncHighDetailFromOptimizer();
}

void COptimizer::ConchainDisableHighDetail(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	COptimizer *pSelf = (COptimizer *)pUserData;
	if(pResult->NumArguments())
		pSelf->OnOptimizerDisableHighDetailToggled();
}

void COptimizer::ConchainGfxHighDetail(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	COptimizer *pSelf = (COptimizer *)pUserData;
	if(pResult->NumArguments())
		pSelf->OnGfxHighDetailToggled();
}

void COptimizer::RenderFpsFogRect()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!m_FpsFog || g_Config.m_BcOptimizerFpsFogRenderRect == 0)
		return;

	if(m_HalfW <= 0.0f || m_HalfH <= 0.0f)
		return;

	const vec2 Center = m_Center;
	const CScreenRect PrevScreen = Graphics()->GetScreen();

	if(const CMapItemGroup *pGameGroup = GameClient()->Layers()->GameGroup())
	{
		const int ParallaxZoom = std::clamp(std::max(pGameGroup->m_ParallaxX, pGameGroup->m_ParallaxY), 0, 100);
		const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(
			Center.x, Center.y,
			pGameGroup->m_ParallaxX, pGameGroup->m_ParallaxY, (float)ParallaxZoom,
			pGameGroup->m_OffsetX, pGameGroup->m_OffsetY,
			Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
		Graphics()->MapScreen(WorldScreen);
	}
	else
	{
		float Width = 0.0f;
		float Height = 0.0f;
		Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &Width, &Height);
		Graphics()->MapScreen(CScreenRect(Center.x - Width * 0.5f, Center.y - Height * 0.5f, Width, Height));
	}

	const vec2 TL{Center.x - m_HalfW, Center.y - m_HalfH};
	const vec2 TR{Center.x + m_HalfW, Center.y - m_HalfH};
	const vec2 BR{Center.x + m_HalfW, Center.y + m_HalfH};
	const vec2 BL{Center.x - m_HalfW, Center.y + m_HalfH};

	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(1.0f, 0.65f, 0.05f, 0.8f);
	const IGraphics::CLineItem aLines[] = {
		IGraphics::CLineItem(TL.x, TL.y, TR.x, TR.y),
		IGraphics::CLineItem(TR.x, TR.y, BR.x, BR.y),
		IGraphics::CLineItem(BR.x, BR.y, BL.x, BL.y),
		IGraphics::CLineItem(BL.x, BL.y, TL.x, TL.y),
	};
	Graphics()->LinesDraw(aLines, std::size(aLines));
	Graphics()->LinesEnd();
	Graphics()->MapScreen(PrevScreen);
}
