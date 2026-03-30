#include <game/client/components/bestclient/clientindicator/client_indicator_sync.h>

#include <gtest/gtest.h>

TEST(ClientIndicator, CollectActiveLocalClientIdsFiltersInvalidAndInactiveIds)
{
	int aLocalIds[NUM_DUMMIES] = {3, 17};
	std::array<bool, MAX_CLIENTS> aClientActive{};
	aClientActive[3] = true;

	const auto Snapshot = BestClientIndicatorClient::CollectActiveLocalClientIds(aLocalIds, aClientActive);

	ASSERT_EQ(Snapshot.m_vClientIds.size(), 1u);
	EXPECT_EQ(Snapshot.m_vClientIds[0], 3);
	EXPECT_TRUE(Snapshot.Contains(3));
	EXPECT_FALSE(Snapshot.Contains(17));
}

TEST(ClientIndicator, CollectActiveLocalClientIdsDeduplicatesLocalSlots)
{
	int aLocalIds[NUM_DUMMIES] = {9, 9};
	std::array<bool, MAX_CLIENTS> aClientActive{};
	aClientActive[9] = true;

	const auto Snapshot = BestClientIndicatorClient::CollectActiveLocalClientIds(aLocalIds, aClientActive);

	ASSERT_EQ(Snapshot.m_vClientIds.size(), 1u);
	EXPECT_EQ(Snapshot.m_vClientIds[0], 9);
	EXPECT_TRUE(Snapshot.Contains(9));
}
