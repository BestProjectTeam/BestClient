#include <game/map/entity_outline.h>
#include <game/map/entity_regions.h>
#include <game/map/rounded_tiles.h>

#include <gtest/gtest.h>

namespace
{
	float Cross(vec2 A, vec2 B)
	{
		return A.x * B.y - A.y * B.x;
	}

	bool InsideTriangle(vec2 P, vec2 A, vec2 B, vec2 C)
	{
		const float AB = Cross(B - A, P - A);
		const float BC = Cross(C - B, P - B);
		const float CA = Cross(A - C, P - C);
		return AB >= -0.0001f && BC >= -0.0001f && CA >= -0.0001f;
	}

	bool InsideShape(const RoundedTiles::CShape &Shape, vec2 P)
	{
		for(const auto &Q : Shape.m_vQuads)
		{
			if((Cross(Q.m_Pos[1] - Q.m_Pos[0], Q.m_Pos[2] - Q.m_Pos[0]) > 1e-6f && InsideTriangle(P, Q.m_Pos[0], Q.m_Pos[1], Q.m_Pos[2])) ||
				(Cross(Q.m_Pos[2] - Q.m_Pos[0], Q.m_Pos[3] - Q.m_Pos[0]) > 1e-6f && InsideTriangle(P, Q.m_Pos[0], Q.m_Pos[2], Q.m_Pos[3])))
				return true;
		}
		return false;
	}

	float Area(const RoundedTiles::CShape &Shape, bool Uv = false)
	{
		float Result = 0;
		for(const auto &Q : Shape.m_vQuads)
		{
			const auto &P = Uv ? Q.m_Uv : Q.m_Pos;
			Result += 0.5f * (Cross(P[1] - P[0], P[2] - P[0]) + Cross(P[2] - P[0], P[3] - P[0]));
		}
		return Result;
	}

	unsigned Mask(const std::vector<ivec2> &Tiles, ivec2 Position)
	{
		unsigned Result = 0, Bit = 0;
		for(int Y = -1; Y <= 1; ++Y)
			for(int X = -1; X <= 1; ++X)
				if(X || Y)
				{
					if(std::find(Tiles.begin(), Tiles.end(), Position + ivec2(X, Y)) != Tiles.end())
						Result |= 1u << Bit;
					++Bit;
				}
		return Result;
	}

	vec2 Boundary(const RoundedTiles::CShape &Shape, bool Vertical, float Edge, float T)
	{
		for(const auto &Q : Shape.m_vQuads)
			for(int K = 0; K < 4; ++K)
			{
				int J = (K + 1) % 4;
				vec2 A = Q.m_Uv[K], B = Q.m_Uv[J];
				if(!Vertical)
				{
					std::swap(A.x, A.y);
					std::swap(B.x, B.y);
				}
				if(std::abs(A.x - Edge) < 1e-6f && std::abs(B.x - Edge) < 1e-6f && std::abs(B.y - A.y) > 1e-6f && T >= std::min(A.y, B.y) - 1e-6f && T <= std::max(A.y, B.y) + 1e-6f)
					return mix(Q.m_Pos[K], Q.m_Pos[J], (T - A.y) / (B.y - A.y));
			}
		ADD_FAILURE() << "Missing source boundary";
		return vec2(1e6f, 1e6f);
	}
}

TEST(EntityRegions, RoundingMaskOnlyIncludesTheSameVisibleCategory)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {
		CEntityRegions::NONE, CEntityRegions::FREEZE, CEntityRegions::NONE,
		CEntityRegions::NONE, CEntityRegions::FREEZE, CEntityRegions::SOLID,
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE};
	// The solid tile on the right used to suppress the freeze tile's corner.
	EXPECT_EQ(Regions.Mask(1, 1, CEntityRegions::FREEZE), 1u << 1);
	// The map's right border repeats its last column when extended on screen.
	EXPECT_EQ(Regions.Mask(2, 1, CEntityRegions::SOLID), 1u << 4);
	EXPECT_EQ(Regions.PriorityMask(1, 1, CEntityRegions::FREEZE), (1u << 1) | (1u << 4));
	EXPECT_EQ(Regions.BlockerMask(1, 1, CEntityRegions::FREEZE), 1u << 4);
}

TEST(EntityRegions, ThroughHookOverlaySharesTheRoundedSolidSilhouette)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 1;
	Regions.m_vTypes = {CEntityRegions::SOLID};
	Regions.m_vTopLayers = {0};
	EXPECT_TRUE(Regions.ShouldRoundTile(0, 0, 0, TILE_SOLID));
	EXPECT_TRUE(Regions.ShouldRoundTile(0, 0, 1, TILE_THROUGH));
	EXPECT_TRUE(Regions.ShouldRoundTile(0, 0, 1, TILE_THROUGH_CUT));
	EXPECT_TRUE(Regions.ShouldRoundTile(0, 0, 1, TILE_THROUGH_ALL));
	EXPECT_TRUE(Regions.ShouldRoundTile(0, 0, 1, TILE_THROUGH_DIR));
	const auto Rounded = RoundedTiles::Build(0, 16, 2, 16);
	EXPECT_LT(Area(Rounded), 850.0f);
	EXPECT_NEAR(Area(Rounded, true), 1.0f, 0.001f);
	Regions.m_vTopLayers = {1};
	EXPECT_FALSE(Regions.ShouldRoundTile(0, 0, 0, TILE_SOLID));
	EXPECT_FALSE(Regions.ShouldRoundTile(0, 0, 1, TILE_THROUGH));
	EXPECT_TRUE(Regions.ShouldRoundTile(0, 0, 1, TILE_NOHOOK));
}

TEST(EntityRegions, ThroughOverlayDoesNotSuppressAdjacentJunctions)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes.assign(9, CEntityRegions::FREEZE);
	Regions.m_vTypes[4] = CEntityRegions::SOLID;
	EXPECT_NE(Regions.ComplementCorners(1, 1), 0u);
}

TEST(EntityRegions, VisibleTeleporterSuppressesCoveredUnfreezeOutline)
{
	// Felian places unfreeze in the game layer underneath tele tiles. The
	// later tele layer is visible, including where its number is zero.
	EXPECT_EQ(CEntityRegions::VisibleType(TILE_UNFREEZE, 0, TILE_TELEIN), CEntityRegions::TELE);
	EXPECT_EQ(CEntityRegions::VisibleType(TILE_SOLID, TILE_FREEZE, TILE_TELEIN), CEntityRegions::TELE);
	EXPECT_EQ(CEntityRegions::VisibleType(TILE_SOLID, TILE_FREEZE, 0), CEntityRegions::FREEZE);
}

TEST(EntityRegions, BuriedUnfreezeDoesNotJoinAdjacentTeleInnerCorner)
{
	const int OnUnfreeze = CEntityRegions::VisibleRegionKey(TILE_UNFREEZE, 0, TILE_TELEINEVIL);
	const int OnAir = CEntityRegions::VisibleRegionKey(0, 0, TILE_TELEINEVIL);
	EXPECT_NE(OnUnfreeze, OnAir);
	EXPECT_NE(OnAir, CEntityRegions::VisibleRegionKey(0, 0, TILE_SOLID));
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {
		CEntityRegions::NONE, CEntityRegions::TELE, CEntityRegions::NONE,
		CEntityRegions::TELE, CEntityRegions::TELE, CEntityRegions::NONE,
		CEntityRegions::NONE, CEntityRegions::TELE, CEntityRegions::NONE};
	Regions.m_vRegionKeys = {0, OnAir, 0, OnUnfreeze, OnAir, 0, 0, OnAir, 0};
	const unsigned Mask = Regions.Mask(1, 1, CEntityRegions::TELE);
	const unsigned Blockers = Regions.BlockerMask(1, 1, CEntityRegions::TELE);
	EXPECT_FALSE(RoundedTiles::Corner(Mask, -1, -1, 16, 2, Blockers).m_Active);
}

TEST(EntityRegions, SwitchTileBlocksUnfreezeCorner)
{
	EXPECT_EQ(CEntityRegions::VisibleType(TILE_UNFREEZE, 0, 0, TILE_SWITCHTIMEDOPEN), CEntityRegions::SWITCH);
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE,
		CEntityRegions::NONE, CEntityRegions::UNFREEZE, CEntityRegions::SWITCH,
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE};
	constexpr unsigned East = 1u << 4;
	EXPECT_EQ(Regions.BlockerMask(1, 1, CEntityRegions::UNFREEZE), East);
	EXPECT_EQ(Regions.PriorityMask(1, 1, CEntityRegions::UNFREEZE), East);
	EXPECT_FALSE(RoundedTiles::Corner(Regions.Mask(1, 1, CEntityRegions::UNFREEZE), 1, -1, 16, 2,
		Regions.BlockerMask(1, 1, CEntityRegions::UNFREEZE))
			.m_Active);
}

TEST(EntityRegions, CoveredMaterialJunctionUsesMatchingInnerAndOuterCorners)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes.assign(9, CEntityRegions::FREEZE);
	Regions.m_vTypes[4] = CEntityRegions::SOLID;
	EXPECT_EQ(Regions.ComplementCorners(1, 1), 15u);
	EXPECT_EQ(Regions.ComplementCorners(0, 0) & 4u, 4u);
	EXPECT_EQ(Regions.ComplementCorners(2, 0) & 8u, 8u);
	EXPECT_EQ(Regions.ComplementCorners(0, 2) & 2u, 2u);
	EXPECT_EQ(Regions.ComplementCorners(2, 2) & 1u, 1u);
	EXPECT_EQ((Regions.TransitionCorners(1, 0) >> 4) & 12u, 12u);

	// Same visible asset over different buried layers is not a material seam.
	Regions.m_vTypes[4] = CEntityRegions::FREEZE;
	Regions.m_vRegionKeys.assign(9, CEntityRegions::FREEZE);
	Regions.m_vRegionKeys[4] = CEntityRegions::FREEZE + 2048;
	EXPECT_EQ(Regions.ComplementCorners(1, 1), 0u);
}

TEST(EntityRegions, EmptyInnerJunctionTrimsTheTwoSideTiles)
{
	// The diagonal solid owns the inward arc. The two solids touching the
	// empty cell by an edge must stop their contours at its tangents.
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {
		CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::NONE,
		CEntityRegions::SOLID, CEntityRegions::NONE, CEntityRegions::NONE,
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE};
	EXPECT_NE(Regions.TransitionCorners(1, 0) & (1u << (3 + 4)), 0u);
	EXPECT_NE(Regions.TransitionCorners(0, 1) & (1u << (1 + 4)), 0u);
	for(const ivec2 Tile : {ivec2(1, 0), ivec2(0, 1)})
	{
		const unsigned Mask = Regions.Mask(Tile.x, Tile.y, CEntityRegions::SOLID);
		const auto Shape = RoundedTiles::Build(Mask, 16, 2, 8,
			Regions.BlockerMask(Tile.x, Tile.y, CEntityRegions::SOLID), Regions.TransitionCorners(Tile.x, Tile.y));
		const vec2 Corner = Tile.x ? vec2(0, 32) : vec2(32, 0);
		const vec2 Vertex = Tile.x ? Boundary(Shape, true, 0, 1) : Boundary(Shape, false, 0, 1);
		EXPECT_LT(length(Vertex - Corner), 0.001f);
	}
	const auto OuterOnly = RoundedTiles::Build(Regions.Mask(1, 0, CEntityRegions::SOLID), 16, 0, 8,
		Regions.BlockerMask(1, 0, CEntityRegions::SOLID), Regions.TransitionCorners(1, 0));
	EXPECT_LT(length(Boundary(OuterOnly, true, 0, 1) - vec2(0, 32)), 0.001f);
}

TEST(RoundedTiles, EmptyInnerJunctionHasOneSurface)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {
		CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::NONE,
		CEntityRegions::SOLID, CEntityRegions::NONE, CEntityRegions::NONE,
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE};
	std::vector<std::pair<ivec2, RoundedTiles::CShape>> Shapes;
	for(const ivec2 Tile : {ivec2(0, 0), ivec2(1, 0), ivec2(0, 1)})
		Shapes.emplace_back(Tile, RoundedTiles::Build(Regions.Mask(Tile.x, Tile.y, CEntityRegions::SOLID), 16, 2, 16,
						  Regions.BlockerMask(Tile.x, Tile.y, CEntityRegions::SOLID), Regions.TransitionCorners(Tile.x, Tile.y)));
	int Overlaps = 0;
	for(int Y = 0; Y < 128; ++Y)
		for(int X = 0; X < 128; ++X)
		{
			const vec2 P(16.125f + X * 0.375f, 16.125f + Y * 0.375f);
			int Cover = 0;
			for(const auto &[Tile, Shape] : Shapes)
				Cover += InsideShape(Shape, P - vec2(Tile.x * 32.0f, Tile.y * 32.0f));
			if(P.x >= 32 && P.y >= 32 && std::abs(length(P - vec2(48, 48)) - 16) < 0.6f)
				continue;
			Overlaps += Cover > 1;
		}
	EXPECT_EQ(Overlaps, 0);
}

TEST(RoundedTiles, TrimmedStraightOutlineStopsAtTheArcTangent)
{
	// North-east side tile of a three-solid/one-empty junction. Only the
	// final half of its bottom edge is exposed after the diagonal arc.
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {
		CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::SOLID,
		CEntityRegions::SOLID, CEntityRegions::NONE, CEntityRegions::NONE,
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE};
	const auto Shape = RoundedTiles::Build(Regions.Mask(1, 0, CEntityRegions::SOLID), 16, 2, 16,
		Regions.BlockerMask(1, 0, CEntityRegions::SOLID), Regions.TransitionCorners(1, 0));
	const auto Outline = RoundedTiles::Outline(Shape, 2);
	const auto Covers = [&](vec2 P) {
		for(const auto &T : Outline)
			if(Cross(T[1] - T[0], T[2] - T[0]) > 1e-7f && InsideTriangle(P, T[0], T[1], T[2]))
				return true;
		return false;
	};
	EXPECT_FALSE(Covers(vec2(3, 31)));
	// A shortened edge must have a flat cap at the tangent. Treating its
	// endpoint as a distance-field point adds the triangular purple spur.
	// The arc's inward band reaches into this side tile before its tangent.
	EXPECT_TRUE(Covers(vec2(15, 31)));
	EXPECT_FALSE(Covers(vec2(9, 31)));
	EXPECT_TRUE(Covers(vec2(20, 31)));
	EXPECT_TRUE(Covers(vec2(25, 31)));
}

TEST(RoundedTiles, FlatOutlineCapDoesNotLoseTheLastMeshCell)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {
		CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::SOLID,
		CEntityRegions::SOLID, CEntityRegions::NONE, CEntityRegions::NONE,
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE};
	const float Radius = 12.3f;
	auto Shape = RoundedTiles::Build(Regions.Mask(1, 0, CEntityRegions::SOLID), Radius, 2, 8,
		Regions.BlockerMask(1, 0, CEntityRegions::SOLID), Regions.TransitionCorners(1, 0));
	// Isolate the straight cap; the neighboring arc also covers this area.
	std::erase_if(Shape.m_vContour, [](const auto &S) { return S.m_A.y != 32 || S.m_B.y != 32; });
	const auto Outline = RoundedTiles::Outline(Shape, 2);
	const auto Covers = [&](vec2 P) {
		return std::any_of(Outline.begin(), Outline.end(), [&](const auto &T) {
			return Cross(T[1] - T[0], T[2] - T[0]) > 1e-7f && InsideTriangle(P, T[0], T[1], T[2]);
		});
	};
	EXPECT_FALSE(Covers(vec2(Radius - 0.1f, 31)));
	EXPECT_TRUE(Covers(vec2(Radius + 0.1f, 31)));
}

TEST(RoundedTiles, InnerArcOutlineContinuesInsideBothSideTiles)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {
		CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::SOLID,
		CEntityRegions::SOLID, CEntityRegions::NONE, CEntityRegions::NONE,
		CEntityRegions::SOLID, CEntityRegions::NONE, CEntityRegions::NONE};
	for(const float Radius : {4.0f, 12.3f, 16.0f})
		for(const int Mode : {1, 2})
			for(const int Steps : {2, 4, 8, 16})
				for(const ivec2 Tile : {ivec2(1, 0), ivec2(0, 1)})
				{
					const auto Shape = RoundedTiles::Build(Regions.Mask(Tile.x, Tile.y, CEntityRegions::SOLID), Radius, Mode, Steps,
						Regions.BlockerMask(Tile.x, Tile.y, CEntityRegions::SOLID), Regions.TransitionCorners(Tile.x, Tile.y));
					const auto Outline = RoundedTiles::Outline(Shape, 2);
					const vec2 P = Tile.x ? vec2(Radius - 0.25f, 31) : vec2(31, Radius - 0.25f);
					EXPECT_TRUE(std::any_of(Outline.begin(), Outline.end(), [&](const auto &T) {
						return Cross(T[1] - T[0], T[2] - T[0]) > 1e-7f && InsideTriangle(P, T[0], T[1], T[2]);
					})) << "radius="
					    << Radius << " mode=" << Mode << " steps=" << Steps << " tile=" << Tile.x << ',' << Tile.y;
				}
}

TEST(RoundedTiles, ThroughArtworkContinuesIntoTheDiagonalWedge)
{
	const auto Shape = RoundedTiles::Build((1u << 3) | (1u << 6), 16, 2, 8, 0, 1u << 3);
	int Wedges = 0;
	for(const auto &Q : Shape.m_vQuads)
	{
		if(length(Q.m_RepeatOffset) < 0.5f)
			continue;
		++Wedges;
		for(int K = 0; K < 4; ++K)
		{
			const vec2 UV = RoundedTiles::SampleUv(Q, K, true);
			EXPECT_GE(UV.x, -0.0001f);
			EXPECT_LE(UV.x, 1.0001f);
			EXPECT_GE(UV.y, -0.0001f);
			EXPECT_LE(UV.y, 1.0001f);
			EXPECT_LT(length(UV + Q.m_RepeatOffset - Q.m_Pos[K] / 32.0f), 0.0001f);
		}
	}
	EXPECT_GT(Wedges, 0);
}

TEST(RoundedTiles, TwoMaterialsPartitionCoveredCornerWithoutGaps)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes.assign(9, CEntityRegions::FREEZE);
	Regions.m_vTypes[4] = CEntityRegions::SOLID;
	for(float Radius : {0.16f, 8.0f, 16.0f})
		for(int Mode = 0; Mode < 3; ++Mode)
		{
			std::vector<std::pair<ivec2, RoundedTiles::CShape>> Shapes;
			for(int Y = 0; Y < 3; ++Y)
				for(int X = 0; X < 3; ++X)
				{
					const int Type = Regions.Get(X, Y);
					Shapes.emplace_back(ivec2(X, Y), RoundedTiles::Build(Regions.Mask(X, Y, Type), Radius, Mode, 16,
										 Regions.BlockerMask(X, Y, Type), Regions.TransitionCorners(X, Y)));
				}
			int Mismatches = 0;
			std::vector<ivec2> FirstMismatches;
			int FirstCover = -1;
			for(int Y = 0; Y < 64; ++Y)
				for(int X = 0; X < 64; ++X)
				{
					const vec2 P(32.125f + X * 0.5f, 32.125f + Y * 0.5f);
					int Cover = 0;
					for(const auto &[Tile, Shape] : Shapes)
						Cover += InsideShape(Shape, P - vec2(Tile.x * 32.0f, Tile.y * 32.0f));
					if(Cover != 1)
					{
						++Mismatches;
						if(FirstMismatches.empty())
							FirstCover = Cover;
						if(FirstMismatches.size() < 4)
							FirstMismatches.emplace_back(X, Y);
					}
				}
			EXPECT_EQ(Mismatches, 0) << Radius << "," << Mode << " first " << (FirstMismatches.empty() ? -1 : FirstMismatches[0].x) << "," << (FirstMismatches.empty() ? -1 : FirstMismatches[0].y) << " cover " << FirstCover;
		}
}

TEST(RoundedTiles, SharedMaterialArcHasExactlyOneOutlineOwner)
{
	for(const auto Pair : {std::pair{CEntityRegions::FREEZE, CEntityRegions::SOLID},
		    std::pair{CEntityRegions::SOLID, CEntityRegions::FREEZE}})
	{
		CEntityRegions Regions;
		Regions.m_Width = Regions.m_Height = 3;
		Regions.m_vTypes.assign(9, Pair.first);
		Regions.m_vTypes[4] = Pair.second;
		int Before = 0, After = 0;
		for(const ivec2 Tile : {ivec2(0, 0), ivec2(1, 1)})
		{
			const int SharedCorner = Tile == ivec2(0, 0) ? 2 : 0;
			const int Type = Regions.Get(Tile.x, Tile.y);
			const unsigned Mask = Regions.Mask(Tile.x, Tile.y, Type);
			auto Shape = RoundedTiles::Build(Mask, 16, 2, 8,
				Regions.BlockerMask(Tile.x, Tile.y, Type), Regions.TransitionCorners(Tile.x, Tile.y));
			Before += std::count_if(Shape.m_vContour.begin(), Shape.m_vContour.end(), [&](const auto &S) { return S.m_SharedCorner == SharedCorner; });
			RoundedTiles::RemoveLowerPriorityEdges(Shape, Regions.PriorityMask(Tile.x, Tile.y, Type) & ~Mask);
			After += std::count_if(Shape.m_vContour.begin(), Shape.m_vContour.end(), [&](const auto &S) { return S.m_SharedCorner == SharedCorner; });
		}
		EXPECT_EQ(Before, 32);
		EXPECT_EQ(After, 16);
	}
}

TEST(RoundedTiles, NeighborOutlineStopsAtCoveredArcTangent)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes.assign(9, CEntityRegions::FREEZE);
	Regions.m_vTypes[4] = CEntityRegions::SOLID;
	const unsigned Mask = Regions.Mask(1, 0, CEntityRegions::FREEZE);
	const auto Shape = RoundedTiles::Build(Mask, 8, 2, 8,
		Regions.BlockerMask(1, 0, CEntityRegions::FREEZE), Regions.TransitionCorners(1, 0));
	int BottomEdges = 0;
	for(const auto &S : Shape.m_vContour)
		if(S.m_A.y == 32 && S.m_B.y == 32 && S.m_A.x != S.m_B.x)
		{
			++BottomEdges;
			EXPECT_FLOAT_EQ(std::min(S.m_A.x, S.m_B.x), 8);
			EXPECT_FLOAT_EQ(std::max(S.m_A.x, S.m_B.x), 24);
		}
	EXPECT_EQ(BottomEdges, 1);
}

TEST(RoundedTiles, DifferentCategoryBlocksCurvesAcrossItsCell)
{
	constexpr unsigned East = 1u << 4;
	constexpr unsigned South = 1u << 6;
	constexpr unsigned SouthEast = 1u << 7;
	// Three tiles of one category used to expand into a fourth category.
	const auto WithoutBlocker = RoundedTiles::Build(East | South, 16, 1, 16);
	const auto WithBlocker = RoundedTiles::Build(East | South, 16, 1, 16, SouthEast);
	EXPECT_GT(Area(WithoutBlocker), 1024);
	EXPECT_FLOAT_EQ(Area(WithBlocker), 1024);
	// A neighboring category sharing the top-right edge prevents an outer
	// curve from cutting a hole between the two visible tiles.
	EXPECT_TRUE(RoundedTiles::Corner(0, 1, -1, 16, 0).m_Active);
	EXPECT_FALSE(RoundedTiles::Corner(0, 1, -1, 16, 0, East).m_Active);
}

TEST(RoundedTiles, OutlineDoesNotInventAnInnerArcAtCategoryJunction)
{
	constexpr unsigned East = 1u << 4;
	constexpr unsigned NorthEast = 1u << 2;
	const auto TileCorner = RoundedTiles::Corner(East, 1, -1, 16, 2, NorthEast);
	const auto OldOutlineCorner = RoundedTiles::Corner(East | NorthEast, 1, -1, 16, 2, NorthEast);
	EXPECT_FALSE(TileCorner.m_Active);
	EXPECT_TRUE(OldOutlineCorner.m_Active);

	auto Shape = RoundedTiles::Build(0, 16, 2, 16, East);
	const auto CountRightEdges = [](const RoundedTiles::CShape &S) {
		return std::count_if(S.m_vContour.begin(), S.m_vContour.end(), [](const RoundedTiles::CSegment &Edge) {
			return Edge.m_A.x == 32 && Edge.m_B.x == 32 && Edge.m_A.y != Edge.m_B.y;
		});
	};
	EXPECT_GT(CountRightEdges(Shape), 0);
	RoundedTiles::RemoveLowerPriorityEdges(Shape, East);
	EXPECT_EQ(CountRightEdges(Shape), 0);
}

TEST(RoundedTiles, BlockedInnerJunctionJoinsStraightOutlineBands)
{
	// Three outlined tiles surround a different category to the north-west.
	// Their north and west bands end in different cells; the south-east tile
	// must bridge them without changing any tile artwork or creating an arc.
	constexpr unsigned North = 1u << 1;
	constexpr unsigned West = 1u << 3;
	constexpr unsigned NorthWest = 1u << 0;
	auto Shape = RoundedTiles::Build(North | West, 16, 2, 4, NorthWest);
	ASSERT_EQ(Shape.m_vOutlineJoins.size(), 1u);
	EXPECT_EQ(RoundedTiles::Build(North | West, 16, 0, 4).m_vOutlineJoins.size(), 1u);
	EXPECT_LT(length(Shape.m_vOutlineJoins[0]), 0.001f);
	const auto CoversJoin = [](const RoundedTiles::CShape &S) {
		for(const auto &T : RoundedTiles::Outline(S, 2))
			if(Cross(T[1] - T[0], T[2] - T[0]) > 0.0001f && InsideTriangle(vec2(0.5f, 0.5f), T[0], T[1], T[2]))
				return true;
		return false;
	};
	EXPECT_TRUE(CoversJoin(Shape));
	Shape.m_vOutlineJoins.clear();
	EXPECT_FALSE(CoversJoin(Shape));
	Shape = RoundedTiles::Build(North | West, 16, 2, 4, NorthWest);
	RoundedTiles::RemoveLowerPriorityEdges(Shape, NorthWest);
	EXPECT_TRUE(Shape.m_vOutlineJoins.empty());
}

TEST(RoundedTiles, StairOutlineHasOneFourConnectedRasterComponent)
{
	const std::vector<ivec2> Tiles = {ivec2(1, 0), ivec2(0, 1), ivec2(1, 1)};
	const auto CountComponents = [&](bool AddJoin) {
		std::array<bool, 16 * 16> Pixels{};
		for(const ivec2 Tile : Tiles)
		{
			const unsigned Blocker = Tile == ivec2(1, 0) ? 1u << 3 : Tile == ivec2(0, 1) ? 1u << 1 :
												       1u << 0;
			auto Shape = RoundedTiles::Build(Mask(Tiles, Tile), 16, 2, 4, Blocker);
			if(!AddJoin)
				Shape.m_vOutlineJoins.clear();
			const auto Triangles = RoundedTiles::Outline(Shape, 2);
			for(int Y = 0; Y < 16; ++Y)
				for(int X = 0; X < 16; ++X)
				{
					const vec2 P(30.0f + (X + 0.5f) / 4.0f - Tile.x * 32.0f, 30.0f + (Y + 0.5f) / 4.0f - Tile.y * 32.0f);
					for(const auto &T : Triangles)
						if(Cross(T[1] - T[0], T[2] - T[0]) > 0.0001f && InsideTriangle(P, T[0], T[1], T[2]))
						{
							Pixels[Y * 16 + X] = true;
							break;
						}
				}
		}
		int Components = 0;
		for(int I = 0; I < 16 * 16; ++I)
		{
			if(!Pixels[I])
				continue;
			++Components;
			std::vector<int> Queue{I};
			Pixels[I] = false;
			for(size_t Head = 0; Head < Queue.size(); ++Head)
			{
				const int Current = Queue[Head];
				for(const ivec2 D : {ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1)})
				{
					const int X = Current % 16 + D.x, Y = Current / 16 + D.y;
					if(X < 0 || X >= 16 || Y < 0 || Y >= 16 || !Pixels[Y * 16 + X])
						continue;
					Pixels[Y * 16 + X] = false;
					Queue.push_back(Y * 16 + X);
				}
			}
		}
		return Components;
	};
	EXPECT_EQ(CountComponents(false), 2);
	EXPECT_EQ(CountComponents(true), 1);
}

TEST(RoundedTiles, DisabledIsOneSquare)
{
	for(unsigned M = 0; M < 256; ++M)
	{
		const auto S = RoundedTiles::Build(M, 0, 2, 4);
		ASSERT_EQ(S.m_vQuads.size(), 1u);
		EXPECT_FLOAT_EQ(Area(S), 1024);
		EXPECT_FLOAT_EQ(Area(S, true), 1);
	}
}

TEST(RoundedTiles, MaximumIsCircleAndPreservesEntireUv)
{
	const auto S = RoundedTiles::Build(0, 16, 2, 32);
	EXPECT_NEAR(Area(S), pi * 256, 0.2f);
	EXPECT_NEAR(Area(S, true), 1, 1e-5f);
	for(const auto &Q : S.m_vQuads)
		for(int K = 0; K < 4; ++K)
		{
			const vec2 Uv = Q.m_Uv[K];
			if(Uv.x == 0 || Uv.x == 1 || Uv.y == 0 || Uv.y == 1)
				EXPECT_NEAR(length(Q.m_Pos[K] - vec2(16, 16)), 16, 1e-4f);
		}
}

TEST(RoundedTiles, InnerJunctionFillsHoleWithoutOverlap)
{
	const std::vector<ivec2> Tiles = {ivec2(0, 0), ivec2(0, 1), ivec2(1, 1)};
	for(float R : {0.16f, 8.0f, 16.0f})
	{
		float Sum = 0;
		for(const auto &P : Tiles)
			Sum += Area(RoundedTiles::Build(Mask(Tiles, P), R, 1, 32));
		EXPECT_NEAR(Sum, 3072 + R * R * (1 - pi / 4), 0.1f);
	}
}

TEST(RoundedTiles, InnerJunctionMatchesCircularUnion)
{
	// NW, SW and SE are filled; NE is the missing cell. The rounded inner
	// boundary in NE is a quarter-circle centered at (48, 16).
	const std::vector<ivec2> Tiles = {ivec2(0, 0), ivec2(0, 1), ivec2(1, 1)};
	std::vector<std::pair<ivec2, RoundedTiles::CShape>> Shapes;
	for(const auto &Tile : Tiles)
		Shapes.emplace_back(Tile, RoundedTiles::Build(Mask(Tiles, Tile), 16, 2, 16));
	int Mismatches = 0;
	for(int Y = 0; Y < 64; ++Y)
		for(int X = 0; X < 64; ++X)
		{
			const vec2 P(32.125f + X * 0.25f, 16.125f + Y * 0.25f);
			const float D = length(P - vec2(48, 16));
			if(std::abs(D - 16) < 0.5f)
				continue;
			bool Filled = false;
			for(const auto &[Tile, Shape] : Shapes)
				Filled |= InsideShape(Shape, P - vec2(Tile.x * 32.0f, Tile.y * 32.0f));
			Mismatches += Filled != (D > 16);
		}
	EXPECT_EQ(Mismatches, 0);
}

TEST(RoundedTiles, InnerJunctionOutlineStaysInsideUnion)
{
	const std::vector<ivec2> Tiles = {ivec2(0, 0), ivec2(0, 1), ivec2(1, 1)};
	int OutsideTriangles = 0;
	for(const auto &Tile : Tiles)
	{
		const auto Shape = RoundedTiles::Build(Mask(Tiles, Tile), 16, 2, 16);
		for(const auto &Triangle : RoundedTiles::Outline(Shape, 2))
		{
			const vec2 P = (Triangle[0] + Triangle[1] + Triangle[2]) / 3.0f + vec2(Tile.x * 32.0f, Tile.y * 32.0f);
			if(P.x > 32 && P.x < 48 && P.y > 16 && P.y < 32 && length(P - vec2(48, 16)) < 15.5f)
				++OutsideTriangles;
		}
	}
	EXPECT_EQ(OutsideTriangles, 0);
}

TEST(RoundedTiles, InnerJunctionKeepsArtworkInOriginalCell)
{
	// The missing south-east neighbor expands this tile into its cell. The
	// original border/art must remain at its original map coordinates. UVs
	// outside the cell are clamped by the sampler per fragment, not at mesh
	// vertices: vertex clamping blurs the border at lower mesh detail.
	const auto Shape = RoundedTiles::Build(255u ^ 128u, 16, 1, 16);
	float MaxUvError = 0.0f;
	bool Extrapolates = false;
	for(const auto &Q : Shape.m_vQuads)
		for(int K = 0; K < 4; ++K)
		{
			const vec2 Expected = Q.m_Pos[K] / 32.0f;
			MaxUvError = std::max(MaxUvError, length(Q.m_SampleUv[K] - Expected));
			Extrapolates |= Q.m_SampleUv[K].x > 1.0f || Q.m_SampleUv[K].y > 1.0f;
		}
	EXPECT_LT(MaxUvError, 1e-5f);
	EXPECT_TRUE(Extrapolates);
}

TEST(RoundedTiles, OutlineHasNoPointSourcesAtCategoryJunctions)
{
	// A zero-length contour at a square junction paints an isolated triangular
	// spot when the surrounding straight edge is covered by another category.
	for(unsigned Mask = 0; Mask < 256; ++Mask)
		for(int Mode = 0; Mode < 3; ++Mode)
		{
			const auto Shape = RoundedTiles::Build(Mask, 16, Mode, 4);
			for(const auto &Segment : Shape.m_vContour)
				EXPECT_GT(length(Segment.m_B - Segment.m_A), 0.001f) << Mask << "," << Mode;
		}
}

TEST(RoundedTiles, ModesAndInterior)
{
	EXPECT_FLOAT_EQ(Area(RoundedTiles::Build(0, 16, 1, 4)), 1024);
	EXPECT_EQ(RoundedTiles::Build(255, 16, 2, 4).m_vQuads.size(), 1u);
	EXPECT_TRUE(RoundedTiles::Build(255, 16, 2, 4).m_vContour.empty());
	const auto Inner = RoundedTiles::Corner(255 ^ 128, 1, 1, 16, 1);
	EXPECT_TRUE(Inner.m_Inner);
	EXPECT_FALSE(RoundedTiles::Corner(255 ^ 128, 1, 1, 16, 0).m_Active);
}

TEST(RoundedTiles, EveryNeighborhoodHasPositiveGeometryAndFullUv)
{
	for(int Mode = 0; Mode < 3; ++Mode)
		for(float R : {0.16f, 8.0f, 15.84f, 16.0f})
			for(unsigned M = 0; M < 256; ++M)
			{
				const auto S = RoundedTiles::Build(M, R, Mode, 4);
				ASSERT_NEAR(Area(S, true), 1, 2e-5f) << M << "," << Mode << "," << R;
				for(const auto &Q : S.m_vQuads)
				{
					ASSERT_GE(Cross(Q.m_Pos[1] - Q.m_Pos[0], Q.m_Pos[2] - Q.m_Pos[0]), -0.0001f) << M << "," << Mode;
					ASSERT_GE(Cross(Q.m_Pos[2] - Q.m_Pos[0], Q.m_Pos[3] - Q.m_Pos[0]), -0.0001f) << M << "," << Mode;
				}
			}
}

TEST(RoundedTiles, SharedEdgesStayWatertight)
{
	// Exhaust the occupancy of a 3x3 neighborhood, including three-way joins.
	for(unsigned Bits = 0; Bits < 512; ++Bits)
	{
		std::vector<ivec2> Tiles;
		for(int I = 0; I < 9; ++I)
			if(Bits & (1u << I))
				Tiles.emplace_back(I % 3, I / 3);
		for(const auto &P : Tiles)
			for(const ivec2 D : {ivec2(1, 0), ivec2(0, 1)})
			{
				if(std::find(Tiles.begin(), Tiles.end(), P + D) == Tiles.end())
					continue;
				for(int Mode = 0; Mode < 3; ++Mode)
				{
					const auto A = RoundedTiles::Build(Mask(Tiles, P), 16, Mode, 4);
					const auto B = RoundedTiles::Build(Mask(Tiles, P + D), 16, Mode, 4);
					for(int I = 0; I <= 16; ++I)
					{
						const vec2 PA = Boundary(A, D.x != 0, 1, I / 16.0f);
						const vec2 PB = Boundary(B, D.x != 0, 0, I / 16.0f) + vec2(D.x * 32, D.y * 32);
						ASSERT_LT(length(PA - PB), 0.0001f) << Bits << "," << Mode;
					}
				}
			}
	}
}

TEST(RoundedTiles, OutlineIsInsideAndSingleCovered)
{
	const auto Shape = RoundedTiles::Build(0, 16, 2, 16);
	for(float Width : {1.0f, 2.0f, 8.0f, 16.0f})
	{
		const auto Triangles = RoundedTiles::Outline(Shape, Width);
		float Sum = 0;
		for(const auto &T : Triangles)
		{
			const float A = Cross(T[1] - T[0], T[2] - T[0]) * 0.5f;
			ASSERT_GE(A, -1e-5f);
			Sum += A;
			for(const auto &P : T)
				ASSERT_LE(length(P - vec2(16, 16)), 16.0001f);
		}
		EXPECT_LE(Sum, Area(Shape) + 0.01f);
		EXPECT_NEAR(Sum, pi * (256 - (16 - Width) * (16 - Width)), 1.5f);
	}
}

TEST(RoundedTiles, DetailMeetsPixelError)
{
	for(float Pixels : {0.1f, 1.0f, 4.0f, 32.0f, 100.0f})
	{
		int N = RoundedTiles::Detail(16, Pixels);
		EXPECT_LE(16 * Pixels * (1 - std::cos(pi / (8 * N))), 0.2501f);
	}
}

TEST(EntityOutlineIndex, EmptyMapAndEmptyView)
{
	CEntityOutlineIndex Index;
	CEntityRegions Regions;
	Index.Build(Regions);
	int Count = 0;
	auto Draw = [&](const auto &, vec2, vec2) { ++Count; };
	Index.Visit(-100000, -100000, 100000, 100000, Draw);
	EXPECT_EQ(Count, 0);
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes.assign(9, CEntityRegions::NONE);
	Regions.m_vTypes[4] = CEntityRegions::SOLID;
	Index.Build(Regions);
	Index.Visit(1, 1, 1, 2, Draw);
	Index.Visit(1, 2, 2, 1, Draw);
	EXPECT_EQ(Count, 0);
	Index.Visit(1, 1, 2, 2, Draw);
	EXPECT_EQ(Count, 1);
}

TEST(EntityOutlineIndex, NeighborhoodMatchesRegionQueries)
{
	for(unsigned Mask = 0; Mask < 256; ++Mask)
	{
		CEntityRegions Regions;
		Regions.m_Width = Regions.m_Height = 3;
		Regions.m_vTypes.assign(9, CEntityRegions::FREEZE);
		Regions.m_vTypes[4] = CEntityRegions::SOLID;
		for(int Y = -1; Y <= 1; ++Y)
			for(int X = -1; X <= 1; ++X)
				if(RoundedTiles::Occupied(Mask, X, Y))
					Regions.m_vTypes[(Y + 1) * 3 + X + 1] = CEntityRegions::SOLID;
		CEntityOutlineIndex Index;
		Index.Build(Regions);
		int Count = 0;
		Index.Visit(1, 1, 2, 2, [&](const auto &Cell, vec2 Position, vec2 Scale) {
			++Count;
			EXPECT_EQ(Cell.m_Type, CEntityRegions::SOLID);
			EXPECT_EQ(Cell.m_Mask, Regions.Mask(1, 1, Cell.m_Type));
			EXPECT_EQ(Cell.m_Blockers, Regions.BlockerMask(1, 1, Cell.m_Type));
			EXPECT_EQ(Cell.m_Transition, Regions.TransitionCorners(1, 1));
			EXPECT_EQ(Cell.m_HigherMask, Regions.PriorityMask(1, 1, Cell.m_Type) & ~Cell.m_Mask);
			EXPECT_EQ(Position, vec2(32, 32));
			EXPECT_EQ(Scale, vec2(1, 1));
		});
		EXPECT_EQ(Count, Mask == 255 ? 0 : 1);
	}
}

TEST(EntityOutlineIndex, HugeZoomDoesNotRepeatClampedBorderCells)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::SOLID,
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE,
		CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::SOLID};
	CEntityOutlineIndex Index;
	Index.Build(Regions);
	int Count = 0, BorderCount = 0;
	Index.Visit(-100000, -100000, 100000, 100000, [&](const auto &Cell, vec2 Position, vec2 Scale) {
		++Count;
		if(Cell.m_Position.x < 0 || Cell.m_Position.x >= 3)
		{
			++BorderCount;
			EXPECT_GT(Scale.x, 99990);
			EXPECT_EQ(Scale.y, 1);
			const auto Shape = RoundedTiles::Build(Cell.m_Mask, 16, 2, 4, Cell.m_Blockers, Cell.m_Transition);
			ASSERT_EQ(Shape.m_vQuads.size(), 1u);
			for(const auto &Segment : Shape.m_vContour)
				EXPECT_EQ(Segment.m_A.y, Segment.m_B.y);
			EXPECT_EQ(Position.y, Cell.m_Position.y * 32.0f);
		}
	});
	EXPECT_EQ(Count, 10);
	EXPECT_EQ(BorderCount, 4);
}

TEST(EntityOutlineIndex, BorderRunCoverageMatchesScalarRendering)
{
	CEntityRegions Regions;
	Regions.m_Width = Regions.m_Height = 3;
	Regions.m_vTypes = {CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::SOLID,
		CEntityRegions::NONE, CEntityRegions::NONE, CEntityRegions::NONE,
		CEntityRegions::SOLID, CEntityRegions::SOLID, CEntityRegions::SOLID};
	CEntityOutlineIndex Index;
	Index.Build(Regions);
	int Count = 0;
	Index.Visit(-20, 0, -10, 3, [&](const auto &Cell, vec2 Position, vec2 Scale) {
		++Count;
		EXPECT_EQ(Position.x, -640);
		EXPECT_EQ(Scale, vec2(10, 1));
		const auto Shape = RoundedTiles::Build(Cell.m_Mask, 16, 2, 4, Cell.m_Blockers, Cell.m_Transition);
		for(int X = -20; X < -10; ++X)
			for(float Y : {0.5f, 2.5f, 16.0f, 29.5f, 31.5f})
			{
				const vec2 Point = vec2(X * 32.0f + 16, Cell.m_Position.y * 32.0f + Y);
				const vec2 Local = (Point - Position) / Scale;
				EXPECT_EQ(RoundedTiles::BandDistance(Shape, Local, 2) <= 0,
					RoundedTiles::BandDistance(Shape, vec2(16, Y), 2) <= 0);
			}
	});
	EXPECT_EQ(Count, 2);
}
