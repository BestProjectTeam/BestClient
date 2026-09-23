/* Copyright © 2026 BestProject Team */
#include "scroll_bar.h"

#include <base/color.h>

#include <engine/graphics.h>

#include <algorithm>
#include <cmath>

namespace BestClientScrollBar
{

static float HandleHeight(const CUIRect &Rail, EHandleStyle Style)
{
	if(Style == EHandleStyle::Console)
		return std::clamp(33.0f, Rail.w, Rail.h / 3.0f);
	return std::max(Rail.w, std::min(24.0f, Rail.h / 3.0f));
}

int DoVertical(SDragState &State, const CUIRect &ScrollbarRect, int CurLine, int MaxScroll, vec2 MousePos, bool MouseDown, const SStyle &Style)
{
	const float RailWidth = std::max(0.0f, Style.m_Width - 2.0f * Style.m_RailMargin);
	const float MinRailHeight = RailWidth * 3.0f;
	const float MinScrollbarHeight = MinRailHeight + 2.0f * Style.m_RailMargin;
	if(ScrollbarRect.h < MinScrollbarHeight || RailWidth <= 0.0f)
	{
		State.m_Dragging = false;
		return MaxScroll > 0 ? std::clamp(CurLine, 0, MaxScroll) : 0;
	}

	CUIRect Rail;
	ScrollbarRect.Margin(Style.m_RailMargin, &Rail);
	CUIRect Handle;
	Rail.HSplitTop(HandleHeight(Rail, Style.m_HandleStyle), &Handle, nullptr);

	const float Current = MaxScroll > 0 ? 1.0f - (float)CurLine / (float)MaxScroll : 1.0f;
	Handle.y = Rail.y + (Rail.h - Handle.h) * Current;

	const auto InsideRect = [&](const CUIRect &Rect) {
		return MousePos.x >= Rect.x && MousePos.x <= Rect.x + Rect.w && MousePos.y >= Rect.y && MousePos.y <= Rect.y + Rect.h;
	};

	if(!MouseDown)
	{
		State.m_Dragging = false;
	}
	else if(!State.m_Dragging && InsideRect(Rail))
	{
		if(InsideRect(Handle))
			State.m_DragOffset = MousePos.y - Handle.y;
		else
			State.m_DragOffset = Handle.h / 2.0f;
		State.m_Dragging = true;
	}

	float NewValue = Current;
	if(State.m_Dragging)
	{
		const float ScrollableHeight = Rail.h - Handle.h;
		if(ScrollableHeight > 0.0f)
		{
			const float Cur = MousePos.y - State.m_DragOffset;
			NewValue = std::clamp((Cur - Rail.y) / ScrollableHeight, 0.0f, 1.0f);
		}
	}

	int NewLine = 0;
	if(MaxScroll > 0)
		NewLine = std::clamp((int)std::round((1.0f - NewValue) * MaxScroll), 0, MaxScroll);

	Handle.y = Rail.y + (Rail.h - Handle.h) * (MaxScroll > 0 ? 1.0f - (float)NewLine / (float)MaxScroll : 1.0f);

	Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, Rail.w / 2.0f);
	const ColorRGBA HandleColor = State.m_Dragging ? ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f) : ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f);
	Handle.Draw(HandleColor, IGraphics::CORNER_ALL, Handle.w / 2.0f);

	return NewLine;
}

float DoVerticalPixels(SDragState &State, const CUIRect &ScrollbarRect, float CurOffset, float MaxOffset, vec2 MousePos, bool MouseDown, const SStyle &Style)
{
	const float RailWidth = std::max(0.0f, Style.m_Width - 2.0f * Style.m_RailMargin);
	const float MinRailHeight = RailWidth * 3.0f;
	const float MinScrollbarHeight = MinRailHeight + 2.0f * Style.m_RailMargin;
	if(ScrollbarRect.h < MinScrollbarHeight || RailWidth <= 0.0f)
	{
		State.m_Dragging = false;
		return MaxOffset > 0.0f ? std::clamp(CurOffset, 0.0f, MaxOffset) : 0.0f;
	}

	CUIRect Rail;
	ScrollbarRect.Margin(Style.m_RailMargin, &Rail);
	CUIRect Handle;
	Rail.HSplitTop(HandleHeight(Rail, Style.m_HandleStyle), &Handle, nullptr);

	const float Current = MaxOffset > 0.0f ? 1.0f - CurOffset / MaxOffset : 1.0f;
	Handle.y = Rail.y + (Rail.h - Handle.h) * Current;

	const auto InsideRect = [&](const CUIRect &Rect) {
		return MousePos.x >= Rect.x && MousePos.x <= Rect.x + Rect.w && MousePos.y >= Rect.y && MousePos.y <= Rect.y + Rect.h;
	};

	if(!MouseDown)
	{
		State.m_Dragging = false;
	}
	else if(!State.m_Dragging && InsideRect(Rail))
	{
		if(InsideRect(Handle))
			State.m_DragOffset = MousePos.y - Handle.y;
		else
			State.m_DragOffset = Handle.h / 2.0f;
		State.m_Dragging = true;
	}

	float NewValue = Current;
	if(State.m_Dragging)
	{
		const float ScrollableHeight = Rail.h - Handle.h;
		if(ScrollableHeight > 0.0f)
		{
			const float Cur = MousePos.y - State.m_DragOffset;
			NewValue = std::clamp((Cur - Rail.y) / ScrollableHeight, 0.0f, 1.0f);
		}
	}

	float NewOffset = 0.0f;
	if(MaxOffset > 0.0f)
		NewOffset = std::clamp((1.0f - NewValue) * MaxOffset, 0.0f, MaxOffset);

	Handle.y = Rail.y + (Rail.h - Handle.h) * (MaxOffset > 0.0f ? 1.0f - NewOffset / MaxOffset : 1.0f);

	Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, Rail.w / 2.0f);
	const ColorRGBA HandleColor = State.m_Dragging ? ColorRGBA(0.8f, 0.8f, 0.8f, 1.0f) : ColorRGBA(0.6f, 0.6f, 0.6f, 1.0f);
	Handle.Draw(HandleColor, IGraphics::CORNER_ALL, Handle.w / 2.0f);

	return NewOffset;
}

} // namespace BestClientScrollBar
