#include <game/client/demo_tee_selection.h>

#include <gtest/gtest.h>

#include <array>

TEST(DemoTeeSelection, CursorTracksCameraAndZoom)
{
	const vec2 UiSize(1000, 600);
	const vec2 CameraCenter(4000, 2000);
	EXPECT_EQ(DemoTeeSelection::MouseToWorld(vec2(500, 300), UiSize, CameraCenter, vec2(1500, 900)), CameraCenter);
	EXPECT_EQ(DemoTeeSelection::MouseToWorld(vec2(750, 150), UiSize, CameraCenter, vec2(1500, 900)), vec2(4375, 1775));
	EXPECT_EQ(DemoTeeSelection::MouseToWorld(vec2(750, 150), UiSize, CameraCenter, vec2(3000, 1800)), vec2(4750, 1550));
}

TEST(DemoTeeSelection, CursorUsesUiProportionsWithCustomAspect)
{
	EXPECT_EQ(DemoTeeSelection::MouseToWorld(vec2(1200, 200), vec2(1600, 800), vec2(2000, 2000), vec2(1200, 900)), vec2(2300, 1775));
}

TEST(DemoTeeSelection, SelectsPlayerFarFromRecordedAim)
{
	const std::array Candidates = {DemoTeeSelection::SCandidate{1, vec2(1000, 1000)}, DemoTeeSelection::SCandidate{7, vec2(1600, 1000)}};
	const vec2 MouseWorld = DemoTeeSelection::MouseToWorld(vec2(900, 300), vec2(1000, 600), vec2(1000, 1000), vec2(1500, 900));
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, MouseWorld, 32, 1), 7);
}

TEST(DemoTeeSelection, ChoosesNearestDistinctPlayer)
{
	const std::array Candidates = {DemoTeeSelection::SCandidate{1, vec2(0, 0)}, DemoTeeSelection::SCandidate{7, vec2(40, 0)}};
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(30, 0), 32, 1), 7);
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(0, 0), 32, 1), 1);
}

TEST(DemoTeeSelection, CyclesPlayersAtTheSamePosition)
{
	const std::array Candidates = {DemoTeeSelection::SCandidate{7, vec2(0, 0)}, DemoTeeSelection::SCandidate{1, vec2(0, 0)}, DemoTeeSelection::SCandidate{12, vec2(0, 0)}};
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(0, 0), 32, -1), 1);
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(0, 0), 32, 1), 7);
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(0, 0), 32, 7), 12);
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(0, 0), 32, 12), 1);
}

TEST(DemoTeeSelection, OverlapCyclingToleratesSmallPositionChanges)
{
	const std::array Candidates = {DemoTeeSelection::SCandidate{1, vec2(0, 0)}, DemoTeeSelection::SCandidate{7, vec2(3, 2)}, DemoTeeSelection::SCandidate{12, vec2(25, 0)}};
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(0, 0), 32, 1), 7);
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(0, 0), 32, 7), 1);
}

TEST(DemoTeeSelection, RejectsEmptySpaceAndIncludesHitBoundary)
{
	const std::array Candidates = {DemoTeeSelection::SCandidate{1, vec2(0, 0)}};
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(33, 0), 32, -1), -1);
	EXPECT_EQ(DemoTeeSelection::Pick(Candidates, vec2(32, 0), 32, -1), 1);
	EXPECT_EQ(DemoTeeSelection::Pick({}, vec2(0, 0), 32, -1), -1);
}
