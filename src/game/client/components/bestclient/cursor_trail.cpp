/* Copyright © 2026 BestProject Team */
#include "cursor_trail.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <algorithm>

void CCursorTrail::OnInit()
{
	OnReset();
	Reload();
}

void CCursorTrail::OnReset()
{
	m_vPoints.clear();
	m_Mode = -1;
	m_Frames = -1;
	m_DisableMovement = -1;
	m_SampleTime = 0.0f;
	m_AnchorValid = false;
	m_AnchorSource = -1;
}

void CCursorTrail::Reload()
{
	m_vPoints.clear();
	if(!m_Texture.IsNullTexture())
		Graphics()->UnloadTexture(&m_Texture);
	m_Texture = IGraphics::CTextureHandle();
	str_copy(m_aPath, g_Config.m_BcCursorTrailTrailImage, sizeof(m_aPath));
	if(m_aPath[0] != '\0')
		m_Texture = Graphics()->LoadTexture(m_aPath, IStorage::TYPE_ALL);
}

void CCursorTrail::Render(vec2 TrailTargetPos, float Scale, float Alpha, int CurWeapon, int HudQuadContainerIndex, const int *pCursorOffsets)
{
	vec2 PlayerPos{};
	bool HasPlayerPos = false;
	int AnchorSource = -1;
	if(g_Config.m_BcCursorTrailDisableMovement && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW)
		{
			AnchorSource = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
			if(GameClient()->m_Snap.m_SpecInfo.m_UsePosition)
			{
				PlayerPos = GameClient()->m_Snap.m_SpecInfo.m_Position;
				HasPlayerPos = true;
			}
			else if(AnchorSource >= 0 && AnchorSource < MAX_CLIENTS && GameClient()->m_Snap.m_aCharacters[AnchorSource].m_Active)
			{
				const auto &Char = GameClient()->m_Snap.m_aCharacters[AnchorSource];
				PlayerPos = mix(vec2(Char.m_Prev.m_X, Char.m_Prev.m_Y), vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y), Client()->IntraGameTick(g_Config.m_ClDummy));
				HasPlayerPos = true;
			}
		}
		else if(!GameClient()->m_Snap.m_SpecInfo.m_Active && GameClient()->m_Snap.m_pLocalCharacter)
		{
			AnchorSource = -2;
			PlayerPos = GameClient()->m_LocalCharacterPos;
			HasPlayerPos = true;
		}
	}
	if(HasPlayerPos)
	{
		if(m_AnchorSource != AnchorSource)
		{
			m_AnchorValid = false;
			m_AnchorSource = AnchorSource;
		}
		if(!m_AnchorValid)
		{
			m_PreviousPlayerPos = PlayerPos;
			m_AnchorValid = true;
		}
		else
		{
			const vec2 PlayerDelta = PlayerPos - m_PreviousPlayerPos;
			for(SCursorTrailPoint &Point : m_vPoints)
				Point.m_Pos += PlayerDelta;
			m_PreviousPlayerPos = PlayerPos;
		}
	}
	else
	{
		m_AnchorValid = false;
		m_AnchorSource = -1;
	}

	if(str_comp(m_aPath, g_Config.m_BcCursorTrailTrailImage) != 0)
		Reload();
	if(m_Mode != g_Config.m_BcCursorTrailMode || m_Frames != g_Config.m_BcCursorTrailNumberOfFrames || m_DisableMovement != g_Config.m_BcCursorTrailDisableMovement)
	{
		m_vPoints.clear();
		m_SampleTime = 0.0f;
		m_Mode = g_Config.m_BcCursorTrailMode;
		m_Frames = g_Config.m_BcCursorTrailNumberOfFrames;
		m_DisableMovement = g_Config.m_BcCursorTrailDisableMovement;
	}
	if(!g_Config.m_BcCursorTrail)
	{
		m_vPoints.clear();
		m_SampleTime = 0.0f;
	}
	else if(g_Config.m_BcCursorTrailMode == 1 && (g_Config.m_BcCursorTrailTrailImage[0] == '\0' || m_Texture.IsNullTexture()))
	{
		m_vPoints.clear();
		m_SampleTime = 0.0f;
	}
	else if(g_Config.m_BcCursorTrailMode == 0 || (g_Config.m_BcCursorTrailTrailImage[0] != '\0' && !m_Texture.IsNullTexture()))
	{
		const float TrailSize = Scale * g_Config.m_BcCursorTrailTrailSize / 100.0f;
		for(SCursorTrailPoint &Point : m_vPoints)
			Point.m_Age += Client()->RenderFrameTime();
		constexpr float Lifetime = 0.2f;
		m_vPoints.erase(std::remove_if(m_vPoints.begin(), m_vPoints.end(), [](const SCursorTrailPoint &Point) { return Point.m_Age >= Lifetime; }), m_vPoints.end());
		m_SampleTime += Client()->RenderFrameTime();
		const float SampleInterval = 1.0f / g_Config.m_BcCursorTrailSamplingFps;
		if(m_SampleTime >= SampleInterval && (m_vPoints.empty() || m_vPoints.front().m_Pos != TrailTargetPos))
		{
			m_vPoints.insert(m_vPoints.begin(), {TrailTargetPos, 0.0f});
			m_SampleTime = 0.0f;
		}
		while((int)m_vPoints.size() > g_Config.m_BcCursorTrailNumberOfFrames)
			m_vPoints.pop_back();

		const float TrailAlphaMultiplier = g_Config.m_BcCursorTrailOpacity / 100.0f;
		for(int i = (int)m_vPoints.size() - 1; i >= 0; --i)
		{
			const SCursorTrailPoint &Point = m_vPoints[i];
			const float TrailAlpha = Alpha * TrailAlphaMultiplier * (1.0f - Point.m_Age / Lifetime);
			const vec2 &Position = Point.m_Pos;
			if(g_Config.m_BcCursorTrailMode == 0)
			{
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, TrailAlpha);
				Graphics()->TextureSet(GameClient()->m_GameSkin.m_aSpriteWeaponCursors[CurWeapon]);
				Graphics()->RenderQuadContainerAsSprite(HudQuadContainerIndex, pCursorOffsets[CurWeapon], Position.x, Position.y, TrailSize, TrailSize);
			}
			else
			{
				Graphics()->TextureSet(m_Texture);
				Graphics()->QuadsSetSubset(0, 0, 1, 1);
				IGraphics::CQuadItem Quad(Position.x, Position.y, 64.0f * TrailSize, 64.0f * TrailSize);
				Graphics()->QuadsBegin();
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, TrailAlpha);
				Graphics()->QuadsDraw(&Quad, 1);
				Graphics()->QuadsEnd();
			}
		}
	}
}
