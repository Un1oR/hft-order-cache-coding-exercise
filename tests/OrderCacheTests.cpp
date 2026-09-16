#include "support/OrderCacheTestSupport.h"

#include <algorithm>
#include <limits>
#include <string>

namespace order_cache::test
{
namespace
{
using StorageTest = CacheTest;

TEST_F(StorageTest, StartsEmpty)
{
	// A new cache has no orders when read through the const interface.
	expectOrders({});
}

struct RoundTripCase
{
	std::string name;
	OrderData order;
};

class OrderRoundTripTest
	: public CacheTest
	, public ::testing::WithParamInterface<RoundTripCase>
{
};

TEST_P(OrderRoundTripTest, PreservesEveryPublicField)
{
	// Adding a distinctive order with a unique ID preserves all six fields.
	const auto & order = GetParam().order;
	givenOrders({order});
	expectOrders({order});
}

INSTANTIATE_TEST_SUITE_P(
	ValidOrders,
	OrderRoundTripTest,
	::testing::Values(
		RoundTripCase{"Buy", buy("alice-buy", 125)},
		RoundTripCase{"Sell", sell("bob-sell", 75)},
		RoundTripCase{"OneUnit", buy("smallest-positive-buy", 1)},
		RoundTripCase{"ZeroQuantity", sell("zero-volume-sell", 0)},
		RoundTripCase{"LargestQuantity", buy("largest-buy", std::numeric_limits<unsigned int>::max())},
		RoundTripCase{"PunctuationAndSpaces",
					  {"order / 007", "Bond: USD / 2030", "Sell", 37, "Mary Jane", "North & Co."}},
		RoundTripCase{
			"LongIdentifiers",
			{std::string(257, 'o'), std::string(193, 's'), "Buy", 42, std::string(131, 'u'), std::string(149, 'c')}}),
	[](const ::testing::TestParamInfo<RoundTripCase> & caseInfo)
	{
		return caseInfo.param.name;
	});

TEST_F(StorageTest, KeepsDistinctIdsWithOtherwiseIdenticalFields)
{
	// Only the IDs differ; deduplicating by the other fields is invalid.
	const Orders orders{buy("first-request", 100), buy("second-request", 100), buy("third-request", 100)};
	givenOrders(orders);
	expectOrders(orders);
}

TEST_F(StorageTest, KeepsAllSecuritiesSidesUsersAndCompanies)
{
	const Orders orders{buy("alice-acme", 100),
						sell("bob-acme", 80),
						buy("alex-bond", 17, alex, "BOND"),
						sell("carol-bond", 23, carol, "BOND"),
						sell("alice-acme-sell", 31, alice)};
	// After adding interleaved groups, no index substitutes for the complete cache contents.
	givenOrders(orders);
	expectOrders(orders);
}

TEST_F(StorageTest, OwnsTheAddedOrderAfterCallerOverwritesItsCopy)
{
	const auto expected = buy("original-request", 100);
	auto original = toOrder(expected);
	cache->addOrder(original);

	// Replacing the caller's object leaves the original value in the cache.
	original = toOrder(sell("unrelated-replacement", 1, carol, "BOND"));
	expectOrders({expected});
}

TEST_F(StorageTest, OwnsStringsAfterConstructorArgumentsLeaveScope)
{
	const OrderData expected{.orderId = "temporary-request",
							 .security = "temporary-security",
							 .side = "Buy",
							 .quantity = 11,
							 .user = "temporary-user",
							 .company = "temporary-company"};
	{
		auto temporary = expected;
		cache->addOrder(toOrder(temporary));
		temporary.security.assign(1024, 'x');
		temporary.user.clear();
	}
	// The cache does not depend on the lifetime of constructor arguments or their buffers.
	expectOrders({expected});
}

TEST_F(StorageTest, RepeatedConstReadsPreserveTheContents)
{
	const Orders orders{buy("buy", 100), sell("sell", 40)};
	givenOrders(orders);
	const OrderCacheInterface & view = *cache;
	for (int readIndex = 0; readIndex < 3; ++readIndex)
	{
		SCOPED_TRACE(readIndex);
		EXPECT_TRUE(sameOrders(snapshot(view.getAllOrders()), orders));
	}
	expectOrders(orders);
}

TEST_F(StorageTest, EditingReturnedOrdersAndVectorDoesNotEditTheCache)
{
	const Orders orders{buy("buy", 100), sell("sell", 40)};
	givenOrders(orders);
	auto returned = cache->getAllOrders();
	ASSERT_EQ(returned.size(), 2U);

	// Mutate an order in the snapshot, then clear and refill the vector itself.
	returned.front() = toOrder(buy("local-only", 999, carol));
	returned.clear();
	returned.push_back(toOrder(sell("also-local-only", 999)));
	// None of the snapshot operations changes the actual cache.
	expectOrders(orders);
}

TEST_F(StorageTest, EarlierSnapshotSurvivesAdditionAndCacheDestruction)
{
	const Orders original{buy("original", 10)};
	givenOrders(original);
	const auto earlier = cache->getAllOrders();
	givenOrders({sell("later", 7)});
	cache.reset();

	EXPECT_TRUE(sameOrders(snapshot(earlier), original));
}

TEST_F(StorageTest, IndependentInstancesDoNotShareStorageOrMatchingState)
{
	const Orders firstOrders{buy("shared-id", 100), sell("first-sell", 60)};
	givenOrders(firstOrders);
	auto second = makeOrderCache();
	ASSERT_NE(second, nullptr);
	const Orders secondOrders{sell("shared-id", 7, carol, "BOND")};
	addOrders(*second, secondOrders);

	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 60U);
	EXPECT_EQ(second->getMatchingSizeForSecurity("ACME"), 0U);
	second->cancelOrder("shared-id");
	second.reset();
	expectOrders(firstOrders);
}

TEST_F(StorageTest, GrowsBeyondASmallContainerAndRetainsEveryOrder)
{
	Orders expected;
	for (unsigned int index = 0; index < 512; ++index)
	{
		expected.push_back(buy("request-" + std::to_string(index),
							   index + 1,
							   index % 2 == 0 ? alice : bob,
							   index % 3 == 0 ? "BOND" : "ACME"));
	}
	// Check retention across container growth and rehashing.
	givenOrders(expected);
	expectOrders(expected);
	for (unsigned int index = 0; index < 512; index += 2)
	{
		cache->cancelOrder("request-" + std::to_string(index));
	}
	std::erase_if(expected,
				  [](const OrderData & order)
				  {
					  return order.user == alice.user;
				  });
	expectOrders(expected);
}
} // namespace
} // namespace order_cache::test
