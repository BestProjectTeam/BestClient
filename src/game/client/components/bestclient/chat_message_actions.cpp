/* Copyright © 2026 BestProject Team */
#include "chat_message_actions.h"

#include <base/color.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <algorithm>

namespace BestClientChatMessageActions
{

static bool Inside(const CUIRect &Rect, vec2 Pos)
{
	return Pos.x >= Rect.x && Pos.x <= Rect.x + Rect.w && Pos.y >= Rect.y && Pos.y <= Rect.y + Rect.h;
}

SLayout BuildLayout(vec2 AnchorPos, float FontSize, bool ReplyEnabled)
{
	SLayout Layout;
	Layout.m_ReplyEnabled = ReplyEnabled;

	const float ItemFont = std::clamp(FontSize * 0.72f, 7.5f, 10.0f);
	const float Pad = 2.0f;
	const float RowH = ItemFont + 3.0f;
	const float PanelW = std::max(36.0f, ItemFont * 3.4f);
	const float PanelH = Pad * 2.0f + RowH * 2.0f;

	Layout.m_Panel.w = PanelW;
	Layout.m_Panel.h = PanelH;
	Layout.m_Panel.x = AnchorPos.x;
	Layout.m_Panel.y = AnchorPos.y;
	if(Layout.m_Panel.y < 0.0f)
		Layout.m_Panel.y = 0.0f;

	Layout.m_Reply = {Layout.m_Panel.x + Pad, Layout.m_Panel.y + Pad, PanelW - Pad * 2.0f, RowH};
	Layout.m_Copy = {Layout.m_Panel.x + Pad, Layout.m_Panel.y + Pad + RowH, PanelW - Pad * 2.0f, RowH};
	return Layout;
}

static void DrawMenuItem(ITextRender *pTextRender, const CUIRect &Rect, const char *pLabel, bool Enabled, bool Hovered, float Alpha, float FontSize)
{
	if(Hovered)
	{
		const float Rounding = std::min(Rect.h * 0.35f, 2.5f);
		Rect.Draw(ColorRGBA(0.42f, 0.42f, 0.42f, 0.85f * Alpha), IGraphics::CORNER_ALL, Rounding);
	}

	CTextCursor Cursor;
	Cursor.SetPosition(vec2(Rect.x + 2.0f, Rect.y + (Rect.h - FontSize) * 0.5f));
	Cursor.m_FontSize = FontSize;
	if(Enabled)
		pTextRender->TextColor(0.92f, 0.92f, 0.92f, Alpha);
	else
		pTextRender->TextColor(0.38f, 0.38f, 0.38f, Alpha);
	pTextRender->TextEx(&Cursor, pLabel);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
}

void Render(ITextRender *pTextRender, const SLayout &Layout, vec2 MousePos, float Alpha)
{
	const float Rounding = 3.0f;
	Layout.m_Panel.Draw(ColorRGBA(0.52f, 0.52f, 0.52f, 0.55f * Alpha), IGraphics::CORNER_ALL, Rounding);
	CUIRect Inner;
	Layout.m_Panel.Margin(0.75f, &Inner);
	Inner.Draw(ColorRGBA(0.17f, 0.17f, 0.17f, 0.96f * Alpha), IGraphics::CORNER_ALL, std::max(1.0f, Rounding - 0.75f));

	const float ItemFont = std::max(7.5f, Layout.m_Reply.h - 3.0f);
	DrawMenuItem(pTextRender, Layout.m_Reply, "Reply", Layout.m_ReplyEnabled, Inside(Layout.m_Reply, MousePos), Alpha, ItemFont);
	DrawMenuItem(pTextRender, Layout.m_Copy, "Copy", true, Inside(Layout.m_Copy, MousePos), Alpha, ItemFont);
}

EAction HitTest(const SLayout &Layout, vec2 MousePos)
{
	if(Layout.m_ReplyEnabled && Inside(Layout.m_Reply, MousePos))
		return EAction::Reply;
	if(Inside(Layout.m_Copy, MousePos))
		return EAction::Copy;
	return EAction::None;
}

bool PanelContains(const SLayout &Layout, vec2 MousePos)
{
	return Inside(Layout.m_Panel, MousePos);
}

void DrawMessageHighlight(IGraphics *pGraphics, float X, float Y, float W, float H, float Rounding, float Alpha)
{
	pGraphics->TextureClear();
	pGraphics->DrawRect(X, Y, W, H, ColorRGBA(0.58f, 0.58f, 0.58f, 0.30f * Alpha), IGraphics::CORNER_ALL, Rounding);
}

} // namespace BestClientChatMessageActions
