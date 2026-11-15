/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "graph.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/textrender.h>

#include <limits>

CGraph::CGraph(int MaxEntries, int Precision, bool SummaryStats) :
	m_Entries(MaxEntries * (sizeof(CEntry) + 2 * CRingBufferBase::ITEM_SIZE), CRingBufferBase::FLAG_RECYCLE),
	m_Precision(Precision),
	m_SummaryStats(SummaryStats)
{
}

void CGraph::Init(float Min, float Max)
{
	m_Entries.Clear();
	m_pFirstScaled = nullptr;
	m_RenderedTotalTime = 0;
	m_Average = 0.0f;
	SetMin(Min);
	SetMax(Max);
}

void CGraph::SetMin(float Min)
{
	m_MinRange = m_MinValue = m_MinAxis = Min;
}

void CGraph::SetMax(float Max)
{
	m_MaxRange = m_MaxValue = m_MaxAxis = Max;
}

void CGraph::Scale(int64_t WantedTotalTime)
{
	// Scale X axis for wanted total time
	if(m_Entries.First() != nullptr)
	{
		const int64_t EndTime = m_Entries.Last()->m_Time;
		const int64_t MinTime = EndTime - WantedTotalTime;
		bool ScaleTotalTime = false;

		if(m_Entries.First()->m_Time >= MinTime)
		{
			m_pFirstScaled = m_Entries.First();
		}
		else
		{
			// Advance forward from previous frame's m_pFirstScaled if still valid
			if(m_pFirstScaled == nullptr || m_pFirstScaled->m_Time < m_Entries.First()->m_Time)
			{
				m_pFirstScaled = m_Entries.First();
			}

			while(m_pFirstScaled != nullptr)
			{
				CEntry *pNext = m_Entries.Next(m_pFirstScaled);
				if(pNext == nullptr || pNext->m_Time > MinTime)
				{
					if(m_pFirstScaled->m_Time < MinTime)
						ScaleTotalTime = true;
					break;
				}
				m_pFirstScaled = pNext;
			}
		}

		m_RenderedTotalTime = (ScaleTotalTime && m_pFirstScaled) ? (EndTime - m_pFirstScaled->m_Time) : WantedTotalTime;

		// Ensure that color is applied to first line segment
		if(m_pFirstScaled)
		{
			m_pFirstScaled->m_ApplyColor = true;
			CEntry *pNext = m_Entries.Next(m_pFirstScaled);
			if(pNext != nullptr)
			{
				pNext->m_ApplyColor = true;
			}
		}
	}
	else
	{
		m_pFirstScaled = nullptr;
		m_RenderedTotalTime = 0;
	}

	// Scale Y axis
	m_Average = 0.0f;
	size_t NumValues = 0;
	m_MinAxis = m_MinRange;
	m_MaxAxis = m_MaxRange;
	m_MinValue = std::numeric_limits<float>::max();
	m_MaxValue = std::numeric_limits<float>::lowest();
	for(CEntry *pEntry = m_pFirstScaled; pEntry != nullptr; pEntry = m_Entries.Next(pEntry))
	{
		const float Value = pEntry->m_Value;
		m_Average += Value;
		++NumValues;

		if(Value > m_MaxAxis)
		{
			m_MaxAxis = Value;
		}
		else if(Value < m_MinAxis)
		{
			m_MinAxis = Value;
		}

		if(Value > m_MaxValue)
		{
			m_MaxValue = Value;
		}
		else if(Value < m_MinValue)
		{
			m_MinValue = Value;
		}
	}
	if(m_MaxValue < m_MinValue)
	{
		m_MinValue = m_MaxValue = 0.0f;
	}
	if(NumValues)
	{
		m_Average /= NumValues;
	}
}

void CGraph::Add(float Value, ColorRGBA Color)
{
	InsertAt(time_get(), Value, Color);
}

void CGraph::InsertAt(int64_t Time, float Value, ColorRGBA Color)
{
	CEntry *pEntry = m_Entries.Allocate(sizeof(CEntry));
	pEntry->m_Time = Time;
	pEntry->m_Value = Value;
	pEntry->m_Color = Color;

	// Determine whether the line (pPrev, pEntry) has different
	// vertex colors than the line (pPrevPrev, pPrev).
	CEntry *pPrev = m_Entries.Prev(pEntry);
	if(pPrev == nullptr)
	{
		pEntry->m_ApplyColor = true;
	}
	else
	{
		CEntry *pPrevPrev = m_Entries.Prev(pPrev);
		if(pPrevPrev == nullptr)
		{
			pEntry->m_ApplyColor = true;
		}
		else
		{
			pEntry->m_ApplyColor = Color != pPrev->m_Color || pPrev->m_Color != pPrevPrev->m_Color;
		}
	}
}

void CGraph::RenderDataLines(IGraphics *pGraphics, float x, float y, float w, float h)
{
	if(m_pFirstScaled == nullptr || m_RenderedTotalTime <= 0)
	{
		return;
	}

	IGraphics::CLineItemBatch LineItemBatch;
	pGraphics->LinesBatchBegin(&LineItemBatch);

	const int64_t StartTime = m_pFirstScaled->m_Time;
	const float TimeScale = w / (float)m_RenderedTotalTime;
	const float ValueRange = m_MaxAxis - m_MinAxis;
	const float ValueScale = ValueRange > 0.0001f ? (h / ValueRange) : 0.0f;

	CEntry *pEntry0 = m_pFirstScaled;
	int CurA = round_to_int((pEntry0->m_Time - StartTime) * TimeScale);
	int CurV = round_to_int((pEntry0->m_Value - m_MinAxis) * ValueScale);
	int MinV = CurV;
	int MaxV = CurV;
	int LastV = CurV;
	ColorRGBA CurColor1 = pEntry0->m_Color;

	CEntry *pEntry = m_Entries.Next(pEntry0);
	while(pEntry != nullptr)
	{
		const int NextA = round_to_int((pEntry->m_Time - StartTime) * TimeScale);
		const int NextV = round_to_int((pEntry->m_Value - m_MinAxis) * ValueScale);

		if(NextA == CurA && !pEntry->m_ApplyColor)
		{
			MinV = std::min(MinV, NextV);
			MaxV = std::max(MaxV, NextV);
			LastV = NextV;
		}
		else
		{
			if(MinV != MaxV)
			{
				const IGraphics::CLineItem ColItem(
					x + CurA,
					y + h - MinV,
					x + CurA,
					y + h - MaxV);
				pGraphics->LinesBatchDraw(&LineItemBatch, &ColItem, 1);
			}

			if(pEntry->m_ApplyColor)
			{
				pGraphics->LinesBatchEnd(&LineItemBatch);
				pGraphics->LinesBatchBegin(&LineItemBatch);
				pGraphics->SetColor2(CurColor1, pEntry->m_Color);
				CurColor1 = pEntry->m_Color;
			}

			const IGraphics::CLineItem ConnItem(
				x + CurA,
				y + h - LastV,
				x + NextA,
				y + h - NextV);
			pGraphics->LinesBatchDraw(&LineItemBatch, &ConnItem, 1);

			CurA = NextA;
			MinV = NextV;
			MaxV = NextV;
			LastV = NextV;
		}

		pEntry = m_Entries.Next(pEntry);
	}

	if(MinV != MaxV)
	{
		const IGraphics::CLineItem ColItem(
			x + CurA,
			y + h - MinV,
			x + CurA,
			y + h - MaxV);
		pGraphics->LinesBatchDraw(&LineItemBatch, &ColItem, 1);
	}

	pGraphics->LinesBatchEnd(&LineItemBatch);
}

void CGraph::Render(IGraphics *pGraphics, ITextRender *pTextRender, float x, float y, float w, float h, const char *pDescription)
{
	const float FontSize = 12.0f;
	const float Spacing = 2.0f;
	const float HeaderHeight = FontSize + 2.0f * Spacing;

	pGraphics->TextureClear();

	pGraphics->QuadsBegin();
	pGraphics->SetColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.7f));
	const IGraphics::CQuadItem ItemHeader(x, y, w, HeaderHeight);
	pGraphics->QuadsDrawTL(&ItemHeader, 1);
	const IGraphics::CQuadItem ItemFooter(x, y + h - HeaderHeight, w, HeaderHeight);
	pGraphics->QuadsDrawTL(&ItemFooter, 1);
	pGraphics->SetColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.6f));
	const IGraphics::CQuadItem ItemGraph(x, y + HeaderHeight, w, h - 2.0f * HeaderHeight);
	pGraphics->QuadsDrawTL(&ItemGraph, 1);
	pGraphics->QuadsEnd();

	pGraphics->LinesBegin();
	pGraphics->SetColor(ColorRGBA(0.5f, 0.5f, 0.5f, 0.75f));
	const IGraphics::CLineItem aLineItems[] = {
		IGraphics::CLineItem(ItemGraph.m_X, ItemGraph.m_Y + (ItemGraph.m_Height * 3) / 4, ItemGraph.m_X + ItemGraph.m_Width, ItemGraph.m_Y + (ItemGraph.m_Height * 3) / 4),
		IGraphics::CLineItem(ItemGraph.m_X, ItemGraph.m_Y + ItemGraph.m_Height / 2, ItemGraph.m_X + ItemGraph.m_Width, ItemGraph.m_Y + ItemGraph.m_Height / 2),
		IGraphics::CLineItem(ItemGraph.m_X, ItemGraph.m_Y + ItemGraph.m_Height / 4, ItemGraph.m_X + ItemGraph.m_Width, ItemGraph.m_Y + ItemGraph.m_Height / 4)};
	pGraphics->LinesDraw(aLineItems, std::size(aLineItems));
	pGraphics->LinesEnd();

	RenderDataLines(pGraphics, ItemGraph.m_X, ItemGraph.m_Y + 1.0f, ItemGraph.m_Width, ItemGraph.m_Height + 2.0f);

	const bool GeometryChanged = (x != m_LastX || y != m_LastY || w != m_LastW || h != m_LastH);
	m_LastX = x;
	m_LastY = y;
	m_LastW = w;
	m_LastH = h;

	const int64_t Now = time_get();
	const bool TimeToUpdate = (Now - m_LastTextUpdateTime >= time_freq() / 25) || GeometryChanged;

	if(GeometryChanged || str_comp(pDescription, m_aCachedDesc) != 0)
	{
		str_copy(m_aCachedDesc, pDescription, sizeof(m_aCachedDesc));
		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_X = ItemHeader.m_X + Spacing;
		Cursor.m_Y = ItemHeader.m_Y + Spacing;
		if(m_DescTextContainer.Valid())
			pTextRender->RecreateTextContainerSoft(m_DescTextContainer, &Cursor, pDescription);
		else
			pTextRender->CreateTextContainer(m_DescTextContainer, &Cursor, pDescription);
	}

	if(TimeToUpdate)
	{
		m_LastTextUpdateTime = Now;
		char aBuf[128];

		if(m_SummaryStats)
		{
			str_format(aBuf, sizeof(aBuf), "Avg. %.*f   ↓ %.*f   ↑ %.*f", m_Precision, m_Average, m_Precision, m_MinValue, m_Precision, m_MaxValue);
			if(GeometryChanged || str_comp(aBuf, m_aCachedStats) != 0)
			{
				str_copy(m_aCachedStats, aBuf, sizeof(m_aCachedStats));
				CTextCursor Cursor;
				Cursor.m_FontSize = FontSize;
				Cursor.m_X = ItemFooter.m_X + Spacing;
				Cursor.m_Y = ItemFooter.m_Y + ItemFooter.m_Height - FontSize - Spacing;
				if(m_StatsTextContainer.Valid())
					pTextRender->RecreateTextContainerSoft(m_StatsTextContainer, &Cursor, aBuf);
				else
					pTextRender->CreateTextContainer(m_StatsTextContainer, &Cursor, aBuf);
			}
		}

		str_format(aBuf, sizeof(aBuf), "%.*f", m_Precision, m_MaxAxis);
		if(GeometryChanged || str_comp(aBuf, m_aCachedMaxAxis) != 0)
		{
			str_copy(m_aCachedMaxAxis, aBuf, sizeof(m_aCachedMaxAxis));
			const float TextW = pTextRender->TextWidth(FontSize, aBuf);
			CTextCursor Cursor;
			Cursor.m_FontSize = FontSize;
			Cursor.m_X = ItemHeader.m_X + ItemHeader.m_Width - TextW - Spacing;
			Cursor.m_Y = ItemHeader.m_Y + Spacing;
			if(m_MaxAxisTextContainer.Valid())
				pTextRender->RecreateTextContainerSoft(m_MaxAxisTextContainer, &Cursor, aBuf);
			else
				pTextRender->CreateTextContainer(m_MaxAxisTextContainer, &Cursor, aBuf);
		}

		str_format(aBuf, sizeof(aBuf), "%.*f", m_Precision, m_MinAxis);
		if(GeometryChanged || str_comp(aBuf, m_aCachedMinAxis) != 0)
		{
			str_copy(m_aCachedMinAxis, aBuf, sizeof(m_aCachedMinAxis));
			const float TextW = pTextRender->TextWidth(FontSize, aBuf);
			CTextCursor Cursor;
			Cursor.m_FontSize = FontSize;
			Cursor.m_X = ItemFooter.m_X + ItemFooter.m_Width - TextW - Spacing;
			Cursor.m_Y = ItemFooter.m_Y + ItemFooter.m_Height - FontSize - Spacing;
			if(m_MinAxisTextContainer.Valid())
				pTextRender->RecreateTextContainerSoft(m_MinAxisTextContainer, &Cursor, aBuf);
			else
				pTextRender->CreateTextContainer(m_MinAxisTextContainer, &Cursor, aBuf);
		}
	}

	if(m_DescTextContainer.Valid())
		pTextRender->RenderTextContainer(m_DescTextContainer, pTextRender->DefaultTextColor(), pTextRender->DefaultTextOutlineColor());
	if(m_SummaryStats && m_StatsTextContainer.Valid())
		pTextRender->RenderTextContainer(m_StatsTextContainer, pTextRender->DefaultTextColor(), pTextRender->DefaultTextOutlineColor());
	if(m_MaxAxisTextContainer.Valid())
		pTextRender->RenderTextContainer(m_MaxAxisTextContainer, pTextRender->DefaultTextColor(), pTextRender->DefaultTextOutlineColor());
	if(m_MinAxisTextContainer.Valid())
		pTextRender->RenderTextContainer(m_MinAxisTextContainer, pTextRender->DefaultTextColor(), pTextRender->DefaultTextOutlineColor());
}

void CGraph::DropTextContainers(ITextRender *pTextRender)
{
	if(pTextRender != nullptr)
	{
		pTextRender->DeleteTextContainer(m_DescTextContainer);
		pTextRender->DeleteTextContainer(m_StatsTextContainer);
		pTextRender->DeleteTextContainer(m_MaxAxisTextContainer);
		pTextRender->DeleteTextContainer(m_MinAxisTextContainer);
	}
	m_aCachedDesc[0] = '\0';
	m_aCachedStats[0] = '\0';
	m_aCachedMaxAxis[0] = '\0';
	m_aCachedMinAxis[0] = '\0';
	m_LastX = m_LastY = m_LastW = m_LastH = -1.0f;
	m_LastTextUpdateTime = 0;
}
