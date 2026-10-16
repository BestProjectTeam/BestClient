#ifndef GAME_MAP_ROUNDED_TILES_H
#define GAME_MAP_ROUNDED_TILES_H

#include <base/vmath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Rendering only. Map coordinates, entity IDs and collision stay integral.
namespace RoundedTiles
{
	struct CQuad
	{
		std::array<vec2, 4> m_Pos; // clockwise: TL, TR, BR, BL
		std::array<vec2, 4> m_Uv; // original cell parameter, including shared edges
		std::array<vec2, 4> m_SampleUv; // texture sampling can hold artwork in place
		vec2 m_RepeatOffset = vec2(0, 0); // diagonal cell filled by an inner wedge
	};

	inline vec2 SampleUv(const CQuad &Quad, int Vertex, bool RepeatingArtwork)
	{
		// A complementary wedge lies wholly in the diagonal cell. A repeating
		// through-hook pattern uses that cell's local UVs instead of clamping
		// the source tile's edge across the newly filled area.
		return Quad.m_SampleUv[Vertex] - (RepeatingArtwork ? Quad.m_RepeatOffset : vec2(0, 0));
	}

	struct CSegment
	{
		vec2 m_A, m_B;
		int m_SharedCorner = -1;
		bool m_ButtA = false, m_ButtB = false;
	};

	struct CShape
	{
		std::vector<CQuad> m_vQuads;
		std::vector<CSegment> m_vContour;
		std::vector<vec2> m_vOutlineJoins;
	};

	// Bits in row order, omitting the center: NW N NE W E SW S SE.
	inline bool Occupied(unsigned Mask, int X, int Y)
	{
		if(X == 0 && Y == 0)
			return true;
		const int Index = (Y + 1) * 3 + X + 1;
		return (Mask & (1u << (Index > 4 ? Index - 1 : Index))) != 0;
	}

	inline int Detail(float Radius, float PixelsPerUnit)
	{
		// Power-of-two half-arc segments keep neighboring layers at the same LOD.
		int Steps = 2;
		while(Steps < 64 && Radius * PixelsPerUnit * (1.0f - std::cos(pi / (8.0f * Steps))) > 0.25f)
			Steps *= 2;
		return Steps;
	}

	inline vec2 Bilinear(const std::array<vec2, 4> &P, float U, float V)
	{
		return mix(mix(P[0], P[1], U), mix(P[3], P[2], U), V);
	}

	struct CCorner
	{
		vec2 m_Origin, m_Inward, m_Center, m_Node;
		bool m_Active = false;
		bool m_ArcX = false, m_ArcY = false;
		bool m_Inner = false;
		bool m_Complement = false;
		bool m_TrimX = false, m_TrimY = false;
		float m_Radius = 0.0f;

		vec2 Tangent(bool AlongX) const
		{
			return m_Origin + (AlongX ? vec2(m_Inward.x * m_Radius, 0) : vec2(0, m_Inward.y * m_Radius));
		}

		vec2 Edge(bool AlongX, float T) const
		{
			const vec2 End = Tangent(AlongX);
			if(!(AlongX ? m_ArcX : m_ArcY))
				return mix(m_Node, End, T);
			const vec2 A = (m_Node - m_Center) / m_Radius;
			const vec2 B = (End - m_Center) / m_Radius;
			const float Angle = std::atan2(A.x * B.y - A.y * B.x, dot(A, B)) * T;
			const float C = std::cos(Angle), S = std::sin(Angle);
			return m_Center + vec2(A.x * C - A.y * S, A.x * S + A.y * C) * m_Radius;
		}

		vec2 Map(float U, float V) const
		{
			const vec2 X = Tangent(true), Y = Tangent(false);
			const vec2 Far = m_Origin + m_Inward * m_Radius;
			// Coons patch: two curved/shifted edges and two unchanged outer edges.
			return Edge(true, U) * (1 - V) + mix(Y, Far, U) * V +
			       Edge(false, V) * (1 - U) + mix(X, Far, V) * U -
			       Bilinear({m_Node, X, Far, Y}, U, V);
		}
	};

	inline CCorner Corner(unsigned Mask, int X, int Y, float Radius, int Mode, unsigned BlockerMask = 0)
	{
		CCorner C;
		C.m_Origin = vec2(X > 0 ? 32 : 0, Y > 0 ? 32 : 0);
		C.m_Inward = vec2(-X, -Y);
		C.m_Node = C.m_Origin;
		C.m_Radius = Radius;
		const bool H = Occupied(Mask, X, 0), V = Occupied(Mask, 0, Y), D = Occupied(Mask, X, Y);
		const int Count = 1 + H + V + D;
		// Diagonally touching regions keep separate convex corners.
		if(Radius > 0 && !H && !V && !Occupied(BlockerMask, X, 0) && !Occupied(BlockerMask, 0, Y) && Mode != 1)
		{
			C.m_Active = C.m_ArcX = C.m_ArcY = true;
			C.m_Center = C.m_Origin + C.m_Inward * Radius;
			C.m_Node = C.m_Origin + C.m_Inward * (Radius * (1 - std::sqrt(0.5f)));
		}
		else if(Radius > 0 && Count == 3 && Mode != 0 &&
			!Occupied(BlockerMask, !H ? X : (!V ? 0 : X), !H ? 0 : Y))
		{
			C.m_Active = C.m_Inner = true;
			const vec2 Missing(!H ? X : (!V ? -X : X), !H ? -Y : Y);
			C.m_Center = C.m_Origin + Missing * Radius;
			C.m_Node = C.m_Origin + Missing * (Radius * (1 - std::sqrt(0.5f)));
			C.m_ArcX = !V;
			C.m_ArcY = !H;
		}
		return C;
	}

	inline CShape Build(unsigned Mask, float Radius, int Mode, int Steps, unsigned BlockerMask = 0, unsigned TransitionCorners = 0)
	{
		CShape Shape;
		Radius = std::clamp(Radius, 0.0f, 16.0f);
		Steps = std::clamp(Steps, 2, 64);
		const unsigned ComplementCorners = TransitionCorners & 15u;
		const unsigned TrimCorners = (TransitionCorners >> 4) & 15u;
		const auto MakeCorner = [&](int I, int X, int Y) {
			// A covered material corner must round even if its neighbor would
			// normally block it. An empty corner still follows the selected mode.
			const bool Covered = ((ComplementCorners & (1u << I)) && Occupied(BlockerMask, X, Y)) ||
					     ((TrimCorners & (1u << I)) && (Occupied(BlockerMask, X, 0) || Occupied(BlockerMask, 0, Y)));
			return Corner(Mask, X, Y, Radius, Covered ? 2 : Mode, Covered ? 0 : BlockerMask);
		};
		std::array<CCorner, 4> Corners = {
			MakeCorner(0, -1, -1), MakeCorner(1, 1, -1), MakeCorner(2, 1, 1), MakeCorner(3, -1, 1)};
		for(int I = 0; I < 4; ++I)
		{
			Corners[I].m_Complement = (ComplementCorners & (1u << I)) && Corners[I].m_Active;
			const int X = I == 1 || I == 2 ? 1 : -1;
			const int Y = I >= 2 ? 1 : -1;
			if(TrimCorners & (1u << I))
			{
				const bool MissingY = !Occupied(Mask, 0, Y);
				const bool MissingX = !Occupied(Mask, X, 0);
				// An open-sky concave corner only has an arc in inner/all mode.
				// A covered material junction has a complementary arc even in
				// outer-only mode, so its two side tiles still need trimming.
				if(Mode != 0 || (MissingY && Occupied(BlockerMask, 0, Y)) || (MissingX && Occupied(BlockerMask, X, 0)))
				{
					Corners[I].m_TrimX = MissingY;
					Corners[I].m_TrimY = MissingX;
					// Keep the arc as a distance source: its inward band reaches
					// into this side tile before the straight-edge tangent. Only
					// the diagonal tile adds geometry into the empty cell.
				}
			}
		}
		// A covered concave corner keeps its original square and draws the
		// complementary wedge into the other tile. Moving its corner vertex
		// like an open-sky junction would leave a transparent sliver between
		// the two materials.
		auto MeshCorners = Corners;
		for(auto &C : MeshCorners)
			if((C.m_Complement || C.m_TrimX || C.m_TrimY) && C.m_Inner)
				C.m_Active = false;
		bool Active = false;
		for(const auto &C : MeshCorners)
			Active |= C.m_Active;
		if(!Active)
			Shape.m_vQuads.push_back({{vec2(0, 0), vec2(32, 0), vec2(32, 32), vec2(0, 32)}, {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)}, {vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1)}});
		else
		{
			const float Grid[] = {0, Radius, 32 - Radius, 32};
			for(int Y = 0; Y < 3; ++Y)
				for(int X = 0; X < 3; ++X)
				{
					if(Grid[X] == Grid[X + 1] || Grid[Y] == Grid[Y + 1])
						continue;
					const int CornerIndex = Y == 0 ? (X == 0 ? 0 : 1) : (X == 0 ? 3 : 2);
					const CCorner *pC = X != 1 && Y != 1 ? &MeshCorners[CornerIndex] : nullptr;
					const int N = pC && pC->m_Active ? Steps : 1;
					for(int J = 0; J < N; ++J)
						for(int I = 0; I < N; ++I)
						{
							CQuad Q;
							for(int K = 0; K < 4; ++K)
							{
								const float U = (I + (K == 1 || K == 2)) / float(N);
								const float V = (J + (K >= 2)) / float(N);
								vec2 P(mix(Grid[X], Grid[X + 1], U), mix(Grid[Y], Grid[Y + 1], V));
								Q.m_Pos[K] = pC && pC->m_Active ? pC->Map(X == 0 ? U : 1 - U, Y == 0 ? V : 1 - V) : P;
								Q.m_Uv[K] = P / 32.0f;
								// Let the texture sampler clamp the extrapolated UV per fragment.
								// Clamping only mesh vertices interpolates across the cell edge,
								// smearing the artwork as the mesh LOD changes with zoom.
								Q.m_SampleUv[K] = pC && pC->m_Inner ? Q.m_Pos[K] / 32.0f : Q.m_Uv[K];
							}
							Shape.m_vQuads.push_back(Q);
						}
				}
		}
		for(const auto &C : Corners)
		{
			if(!C.m_Complement || !C.m_Inner)
				continue;
			const vec2 Sign = (C.m_Center - C.m_Origin) / Radius;
			vec2 Previous = C.m_Center - vec2(Sign.x * Radius, 0);
			for(int I = 1; I <= 2 * Steps; ++I)
			{
				const float A = (pi * 0.5f) * I / (2 * Steps);
				const vec2 Next = C.m_Center - vec2(Sign.x * std::cos(A), Sign.y * std::sin(A)) * Radius;
				vec2 P = Previous, Q = Next;
				if((P.x - C.m_Origin.x) * (Q.y - C.m_Origin.y) - (P.y - C.m_Origin.y) * (Q.x - C.m_Origin.x) < 0)
					std::swap(P, Q);
				CQuad Wedge;
				Wedge.m_Pos = {C.m_Origin, P, Q, Q};
				Wedge.m_RepeatOffset = Sign;
				for(int K = 0; K < 4; ++K)
					Wedge.m_Uv[K] = Wedge.m_SampleUv[K] = Wedge.m_Pos[K] / 32.0f;
				Shape.m_vQuads.push_back(Wedge);
				Previous = Next;
			}
		}

		// Full arcs are included even for the diagonally opposite junction tile:
		// a wide inward outline can reach that tile without crossing one of its edges.
		for(int CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			const auto &C = Corners[CornerIndex];
			if(!C.m_Active)
			{
				const int X = C.m_Inward.x < 0 ? 1 : -1;
				const int Y = C.m_Inward.y < 0 ? 1 : -1;
				// Three tiles meet at this point, but the inner arc is disabled:
				// either outer-only mode or a different region in the diagonal cell.
				// The two straight contour bands then terminate in different tiles
				// and touch only diagonally in the raster. Add their common endpoint
				// to this tile's inward outline band to close that small gap.
				if(Occupied(Mask, X, 0) && Occupied(Mask, 0, Y) && !Occupied(Mask, X, Y))
					Shape.m_vOutlineJoins.push_back(C.m_Origin);
				continue;
			}
			const vec2 Sign = (C.m_Center - C.m_Origin) / Radius;
			vec2 Previous = C.m_Center - vec2(Sign.x * Radius, 0);
			for(int I = 1; I <= 2 * Steps; ++I)
			{
				const float A = (pi * 0.5f) * I / (2 * Steps);
				vec2 Next = C.m_Center - vec2(Sign.x * std::cos(A), Sign.y * std::sin(A)) * Radius;
				Shape.m_vContour.push_back({Previous, Next,
					(C.m_Complement || C.m_TrimX || C.m_TrimY) ? CornerIndex : -1});
				Previous = Next;
			}
		}
		const int DX[] = {0, 1, 0, -1}, DY[] = {-1, 0, 1, 0};
		for(int Side = 0; Side < 4; ++Side)
		{
			if(Occupied(Mask, DX[Side], DY[Side]))
				continue;
			const auto &A = Corners[Side], &B = Corners[(Side + 1) % 4];
			const bool AlongX = Side % 2 == 0;
			const vec2 Start = A.m_Active || (AlongX ? A.m_TrimX : A.m_TrimY) ? A.Tangent(AlongX) : A.m_Origin;
			const vec2 End = B.m_Active || (AlongX ? B.m_TrimX : B.m_TrimY) ? B.Tangent(AlongX) : B.m_Origin;
			// At a 100% radius, opposite arcs can meet with no straight edge.
			// A zero-length edge acts as a point-distance source for the outline
			// rasterizer and leaves a colored speck at the junction.
			if(length(End - Start) > 0.001f)
				Shape.m_vContour.push_back({Start, End, -1,
					A.m_Active || (AlongX ? A.m_TrimX : A.m_TrimY),
					B.m_Active || (AlongX ? B.m_TrimX : B.m_TrimY)});
		}
		return Shape;
	}

	// Two categories sharing a side use the same tile silhouette. Only the
	// higher-priority category draws the line along their common edge.
	inline void RemoveLowerPriorityEdges(CShape &Shape, unsigned HigherMask)
	{
		if(!HigherMask)
			return;
		const bool Top = Occupied(HigherMask, 0, -1);
		const bool Right = Occupied(HigherMask, 1, 0);
		const bool Bottom = Occupied(HigherMask, 0, 1);
		const bool Left = Occupied(HigherMask, -1, 0);
		std::erase_if(Shape.m_vContour, [&](const CSegment &S) {
			if(S.m_SharedCorner >= 0)
			{
				const int X = S.m_SharedCorner == 1 || S.m_SharedCorner == 2 ? 1 : -1;
				const int Y = S.m_SharedCorner >= 2 ? 1 : -1;
				if(Occupied(HigherMask, X, 0) || Occupied(HigherMask, 0, Y) || Occupied(HigherMask, X, Y))
					return true;
			}
			const float Epsilon = 0.001f;
			return (Top && std::abs(S.m_A.y) < Epsilon && std::abs(S.m_B.y) < Epsilon) ||
			       (Right && std::abs(S.m_A.x - 32) < Epsilon && std::abs(S.m_B.x - 32) < Epsilon) ||
			       (Bottom && std::abs(S.m_A.y - 32) < Epsilon && std::abs(S.m_B.y - 32) < Epsilon) ||
			       (Left && std::abs(S.m_A.x) < Epsilon && std::abs(S.m_B.x) < Epsilon);
		});
		std::erase_if(Shape.m_vOutlineJoins, [&](vec2 P) {
			return Occupied(HigherMask, P.x > 16 ? 1 : -1, P.y > 16 ? 1 : -1);
		});
	}

	inline float BandDistance(const CShape &Shape, vec2 P, float Width)
	{
		float Result = 1e10f;
		for(const auto &S : Shape.m_vContour)
		{
			const vec2 D = S.m_B - S.m_A;
			const float LengthSquared = std::max(dot(D, D), 1e-12f);
			const float Projection = dot(P - S.m_A, D) / LengthSquared;
			const float T = std::clamp(Projection, 0.0f, 1.0f);
			float Band = length(P - (S.m_A + D * T)) - Width;
			// Flat caps need a continuous signed half-plane, not an infinite
			// distance beyond the endpoint. Interpolating an infinite value while
			// clipping a mesh cell cuts off the last part of the straight outline.
			if(S.m_ButtA || S.m_ButtB)
			{
				const float SegmentLength = std::sqrt(LengthSquared);
				if(S.m_ButtA)
					Band = std::max(Band, -Projection * SegmentLength);
				if(S.m_ButtB)
					Band = std::max(Band, (Projection - 1.0f) * SegmentLength);
			}
			Result = std::min(Result, Band);
		}
		for(const vec2 Join : Shape.m_vOutlineJoins)
			Result = std::min(Result, length(P - Join) - Width);
		return Result;
	}

	// Clip a disjoint tessellation to the inward distance band. Unlike overlapping
	// edge strips, this remains single-covered at concave joins and large widths.
	inline std::vector<std::array<vec2, 3>> Outline(const CShape &Shape, float Width)
	{
		std::vector<std::array<vec2, 3>> Result;
		if((Shape.m_vContour.empty() && Shape.m_vOutlineJoins.empty()) || Width <= 0)
			return Result;
		Width = std::min(Width, 16.0f);
		// Common straight walls need only a few quads, including at large zoom-out.
		const bool Straight = Shape.m_vOutlineJoins.empty() && Shape.m_vQuads.size() == 1 && std::all_of(Shape.m_vContour.begin(), Shape.m_vContour.end(), [](const CSegment &S) {
			// The center-sampled fast path draws a whole edge band. A segment
			// shortened to meet an arc must use the precise clipped mesh or it
			// paints a rectangular stub across the missing tangent interval.
			return ((S.m_A.y == S.m_B.y && (S.m_A.y == 0 || S.m_A.y == 32) && std::min(S.m_A.x, S.m_B.x) == 0 && std::max(S.m_A.x, S.m_B.x) == 32) ||
				(S.m_A.x == S.m_B.x && (S.m_A.x == 0 || S.m_A.x == 32) && std::min(S.m_A.y, S.m_B.y) == 0 && std::max(S.m_A.y, S.m_B.y) == 32));
		});
		if(Straight)
		{
			const float Grid[] = {0, Width, 32 - Width, 32};
			for(int Y = 0; Y < 3; ++Y)
				for(int X = 0; X < 3; ++X)
				{
					if(Grid[X + 1] <= Grid[X] || Grid[Y + 1] <= Grid[Y])
						continue;
					if(BandDistance(Shape, vec2((Grid[X] + Grid[X + 1]) * 0.5f, (Grid[Y] + Grid[Y + 1]) * 0.5f), Width) > 0)
						continue;
					const vec2 A(Grid[X], Grid[Y]), B(Grid[X + 1], Grid[Y]), C(Grid[X + 1], Grid[Y + 1]), D(Grid[X], Grid[Y + 1]);
					Result.push_back({A, B, C});
					Result.push_back({A, C, D});
				}
			return Result;
		}
		for(const auto &Q : Shape.m_vQuads)
		{
			// The signed distance band is 1-Lipschitz. Skip patches whose
			// entire convex hull is farther than the outline width; most of the
			// increasingly fine patches at high zoom lie in the tile interior.
			const vec2 Center = (Q.m_Pos[0] + Q.m_Pos[1] + Q.m_Pos[2] + Q.m_Pos[3]) * 0.25f;
			const float Reach = std::max({length(Q.m_Pos[0] - Center), length(Q.m_Pos[1] - Center), length(Q.m_Pos[2] - Center), length(Q.m_Pos[3] - Center)});
			if(BandDistance(Shape, Center, Width) > Reach)
				continue;
			const float Size = std::max({length(Q.m_Pos[1] - Q.m_Pos[0]), length(Q.m_Pos[3] - Q.m_Pos[0]), length(Q.m_Pos[2] - Q.m_Pos[1]), length(Q.m_Pos[2] - Q.m_Pos[3])});
			const int N = std::max(2, (int)std::ceil(Size / 2.0f));
			for(int Y = 0; Y < N; ++Y)
				for(int X = 0; X < N; ++X)
				{
					std::array<vec2, 4> P;
					for(int K = 0; K < 4; ++K)
						P[K] = Bilinear(Q.m_Pos, (X + (K == 1 || K == 2)) / float(N), (Y + (K >= 2)) / float(N));
					for(int T = 0; T < 2; ++T)
					{
						const std::array<vec2, 3> Tri = {P[0], P[T + 1], P[T + 2]};
						std::array<vec2, 4> Polygon;
						int Count = 0;
						for(int K = 0; K < 3; ++K)
						{
							const vec2 A = Tri[K], B = Tri[(K + 1) % 3];
							const float DA = -BandDistance(Shape, A, Width), DB = -BandDistance(Shape, B, Width);
							if(DA >= 0)
								Polygon[Count++] = A;
							if((DA >= 0) != (DB >= 0))
								Polygon[Count++] = mix(A, B, DA / (DA - DB));
						}
						for(int K = 1; K + 1 < Count; ++K)
							Result.push_back({Polygon[0], Polygon[K], Polygon[K + 1]});
					}
				}
		}
		return Result;
	}
}

#endif
