/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SCROLL_BAR_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SCROLL_BAR_H

#include <base/vmath.h>

#include <game/client/ui_rect.h>

namespace BestClientScrollBar
{

struct SDragState
{
	bool m_Dragging = false;
	float m_DragOffset = 0.0f;
};

enum class EHandleStyle
{
	Chat,
	Console,
};

struct SStyle
{
	float m_Width;
	float m_OuterMargin;
	float m_RailMargin;
	EHandleStyle m_HandleStyle;
};

constexpr SStyle CHAT = {5.0f, 0.0f, 1.0f, EHandleStyle::Chat};
constexpr SStyle CONSOLE = {18.0f, 5.0f, 5.0f, EHandleStyle::Console};

int DoVertical(SDragState &State, const CUIRect &ScrollbarRect, int CurLine, int MaxScroll, vec2 MousePos, bool MouseDown, const SStyle &Style);
float DoVerticalPixels(SDragState &State, const CUIRect &ScrollbarRect, float CurOffset, float MaxOffset, vec2 MousePos, bool MouseDown, const SStyle &Style);

} // namespace BestClientScrollBar

#endif
