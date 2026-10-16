#ifndef GAME_MAP_ENTITY_OUTLINE_H
#define GAME_MAP_ENTITY_OUTLINE_H

#include "entity_regions.h"

#include <base/vmath.h>

#include <cstdint>
#include <vector>

// Index only boundary cells. Zooming out must not rescan empty space or the
// interiors of filled regions, nor repeat clamped border cells one by one.
class CEntityOutlineIndex
{
public:
	struct CCell
	{
		ivec2 m_Position;
		int m_Type;
		unsigned m_Mask, m_Blockers, m_Transition, m_HigherMask;

		uint64_t Key() const
		{
			return uint64_t(m_Mask) | (uint64_t(m_Blockers) << 8) | (uint64_t(m_HigherMask) << 16) | (uint64_t(m_Transition) << 40);
		}
	};

	void Build(const CEntityRegions &Regions)
	{
		m_Width = Regions.m_Width;
		m_Height = Regions.m_Height;
		m_vRows.clear();
		m_vLeft.clear();
		m_vRight.clear();
		m_vTop.clear();
		m_vBottom.clear();
		if(m_Width <= 0 || m_Height <= 0 || Regions.m_vTypes.empty())
			return;
		m_vRows.resize(m_Height);
		auto AddCell = [&](std::vector<CCell> &vCells, int X, int Y) {
			const int Type = Regions.Get(X, Y);
			if(Type == CEntityRegions::NONE || Type == CEntityRegions::SWITCH)
				return;
			const unsigned Mask = Regions.Mask(X, Y, Type);
			if(Mask == 255)
				return;
			vCells.push_back({ivec2(X, Y), Type, Mask, Regions.BlockerMask(X, Y, Type), Regions.TransitionCorners(X, Y), Regions.PriorityMask(X, Y, Type) & ~Mask});
		};
		for(int Y = 0; Y < m_Height; ++Y)
		{
			for(int X = 0; X < m_Width; ++X)
				AddCell(m_vRows[Y], X, Y);
			AddCell(m_vLeft, -1, Y);
			AddCell(m_vRight, m_Width, Y);
		}
		for(int X = 0; X < m_Width; ++X)
		{
			AddCell(m_vTop, X, -1);
			AddCell(m_vBottom, X, m_Height);
		}
	}

	// Bounds are in tile coordinates, with exclusive right/bottom edges.
	// Outside a map side the old renderer repeats a clamped row/column. Its
	// contours are straight, so one scaled copy preserves the same coverage.
	template<typename F>
	void Visit(int StartX, int StartY, int EndX, int EndY, F &&Draw) const
	{
		if(m_vRows.empty() || StartX >= EndX || StartY >= EndY)
			return;
		const int X0 = std::max(0, StartX), X1 = std::min(m_Width, EndX);
		const int Y0 = std::max(0, StartY), Y1 = std::min(m_Height, EndY);
		for(int Y = Y0; Y < Y1; ++Y)
		{
			const auto &vRow = m_vRows[Y];
			auto It = std::lower_bound(vRow.begin(), vRow.end(), X0, [](const CCell &Cell, int X) { return Cell.m_Position.x < X; });
			for(; It != vRow.end() && It->m_Position.x < X1; ++It)
				Draw(*It, vec2(It->m_Position.x * 32.0f, It->m_Position.y * 32.0f), vec2(1, 1));
		}
		auto Vertical = [&](const std::vector<CCell> &vCells, float X, float Width) {
			auto It = std::lower_bound(vCells.begin(), vCells.end(), Y0, [](const CCell &Cell, int Y) { return Cell.m_Position.y < Y; });
			for(; It != vCells.end() && It->m_Position.y < Y1; ++It)
				Draw(*It, vec2(X * 32.0f, It->m_Position.y * 32.0f), vec2(Width, 1));
		};
		auto Horizontal = [&](const std::vector<CCell> &vCells, float Y, float Height) {
			auto It = std::lower_bound(vCells.begin(), vCells.end(), X0, [](const CCell &Cell, int X) { return Cell.m_Position.x < X; });
			for(; It != vCells.end() && It->m_Position.x < X1; ++It)
				Draw(*It, vec2(It->m_Position.x * 32.0f, Y * 32.0f), vec2(1, Height));
		};
		if(StartX < 0)
			Vertical(m_vLeft, StartX, float(std::min(EndX, 0)) - StartX);
		if(EndX > m_Width)
			Vertical(m_vRight, std::max(StartX, m_Width), float(EndX) - std::max(StartX, m_Width));
		if(StartY < 0)
			Horizontal(m_vTop, StartY, float(std::min(EndY, 0)) - StartY);
		if(EndY > m_Height)
			Horizontal(m_vBottom, std::max(StartY, m_Height), float(EndY) - std::max(StartY, m_Height));
	}

private:
	int m_Width = 0, m_Height = 0;
	std::vector<std::vector<CCell>> m_vRows;
	std::vector<CCell> m_vLeft, m_vRight, m_vTop, m_vBottom;
};

#endif
