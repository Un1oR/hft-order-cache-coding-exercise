#include "support/OrderCacheTestSupport.h"

#include <limits>
#include <string>

namespace order_cache::test
{
namespace
{
using CancellationTest = CacheTest;

class CancellationRouteTest
	: public CacheTest
	, public ::testing::WithParamInterface<CancelRoute>
{
};

TEST_P(CancellationRouteTest, CancellingFromAnEmptyCacheIsANoOp)
{
	// Given a missing target, cancellation neither throws nor creates entries.
	cancelThrough(*cache, GetParam(), buy("missing", 100));
	expectOrders({});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 0U);
}

TEST_P(CancellationRouteTest, MissingTargetLeavesUnrelatedOrdersUntouched)
{
	const Orders survivors{buy("keep-buy", 10, alice, "BOND"), sell("keep-sell", 7, bob, "BOND")};
	givenOrders(survivors);
	const auto missing = buy("never-added", 1, carol, "MISSING");
	cancelThrough(*cache, GetParam(), missing);
	expectOrders(survivors);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("BOND"), 7U);
}

TEST_P(CancellationRouteTest, RemovingTheOnlyOrderLeavesAnEmptyCache)
{
	const auto target = buy("only-order", 100);
	givenOrders({target});
	// The precondition prevents an empty stub from falsely passing the last-order removal scenario.
	ASSERT_TRUE(sameOrders(snapshot(cache->getAllOrders()), {target}));
	cancelThrough(*cache, GetParam(), target);
	expectOrders({});
}

TEST_P(CancellationRouteTest, RepeatedCancellationIsIdempotent)
{
	const auto target = buy("remove-me", 100);
	const auto survivor = sell("keep-me", 7, bob, "BOND");
	givenOrders({target, survivor});
	cancelThrough(*cache, GetParam(), target);
	expectOrders({survivor});
	cancelThrough(*cache, GetParam(), target);
	expectOrders({survivor});
}

TEST_P(CancellationRouteTest, EarlierSnapshotIsNotChangedByCancellation)
{
	const Orders original{buy("remove-me", 100), sell("keep-me", 7, bob, "BOND")};
	givenOrders(original);
	const auto earlier = cache->getAllOrders();
	cancelThrough(*cache, GetParam(), original.front());
	EXPECT_TRUE(sameOrders(snapshot(earlier), original));
	expectOrders({original.back()});
}

TEST_P(CancellationRouteTest, CancelledIdCanBeReusedWithoutStaleSecondaryIndexes)
{
	const auto oldOrder = buy("reusable-id", 100, alice, "OLD");
	const auto replacement = sell("reusable-id", 23, bob, "NEW");
	const auto counterparty = buy("new-counterparty", 40, carol, "NEW");
	givenOrders({oldOrder});
	cancelThrough(*cache, GetParam(), oldOrder);

	// Reuse the ID with a different security, side, user, company, and quantity.
	givenOrders({replacement, counterparty});
	cache->cancelOrdersForUser(alice.user);
	cache->cancelOrdersForSecIdWithMinimumQty("OLD", 0);
	// Stale indexes must neither remove the replacement nor affect matching.
	expectOrders({replacement, counterparty});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("OLD"), 0U);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("NEW"), 23U);
	cache->cancelOrder("reusable-id");
	expectOrders({counterparty});
}

TEST_P(CancellationRouteTest, CancellingBeforeAnAddDoesNotBanFutureOrders)
{
	const auto future = buy("future-order", 100);
	cancelThrough(*cache, GetParam(), future);
	givenOrders({future});
	expectOrders({future});
}

INSTANTIATE_TEST_SUITE_P(AllCancellationMethods,
						 CancellationRouteTest,
						 ::testing::Values(CancelRoute::OrderId, CancelRoute::User, CancelRoute::Security),
						 [](const ::testing::TestParamInfo<CancelRoute> & caseInfo)
						 {
							 return routeName(caseInfo.param);
						 });

struct PositionCase
{
	std::string name;
	std::size_t position;
};
class CancelPositionTest
	: public CacheTest
	, public ::testing::WithParamInterface<PositionCase>
{
};

TEST_P(CancelPositionTest, CancelsExactlyTheRequestedId)
{
	const Orders original{buy("first", 10), buy("middle", 10), buy("last", 10)};
	givenOrders(original);
	const auto position = GetParam().position;
	cache->cancelOrder(original.at(position).orderId);
	Orders expected;
	for (std::size_t index = 0; index < original.size(); ++index)
	{
		if (index != position)
		{
			expected.push_back(original.at(index));
		}
	}
	expectOrders(expected);
}

INSTANTIATE_TEST_SUITE_P(InsertionPositions,
						 CancelPositionTest,
						 ::testing::Values(PositionCase{"First", 0},
										   PositionCase{"Middle", 1},
										   PositionCase{"Last", 2}),
						 [](const ::testing::TestParamInfo<PositionCase> & caseInfo)
						 {
							 return caseInfo.param.name;
						 });

TEST_F(CancellationTest, OrderIdComparisonIsExactAndDoesNotUsePrefixesOrCaseFolding)
{
	const auto target = buy("order", 50);
	const Orders survivors{buy("Order", 50), buy("order-child", 50), buy("pre-order", 50)};
	givenOrders(survivors);
	givenOrders({target});
	cache->cancelOrder("order");
	expectOrders(survivors);
}

TEST_F(CancellationTest, UserCancellationSpansSecuritiesAndBothSidesButNotColleagues)
{
	const Orders removed{buy("alice-acme-buy", 50),
						 sell("alice-acme-sell", 70, alice),
						 buy("alice-bond-buy", 30, alice, "BOND"),
						 sell("alice-bond-sell", 90, alice, "BOND")};
	const Orders survivors{buy("alex-acme", 50, alex), sell("bob-bond", 70, bob, "BOND")};
	givenOrders(removed);
	givenOrders(survivors);
	cache->cancelOrdersForUser("alice");
	expectOrders(survivors);
}

TEST_F(CancellationTest, UserComparisonIsExactAndDoesNotUsePrefixesOrCaseFolding)
{
	const Orders survivors{buy("capitalized", 10, {.user = "Alice", .company = "Atlas"}),
						   buy("suffix", 10, {.user = "alice-junior", .company = "Atlas"}),
						   buy("prefix", 10, {.user = "not-alice", .company = "Atlas"})};
	givenOrders(survivors);
	givenOrders({buy("target", 10)});
	cache->cancelOrdersForUser("alice");
	expectOrders(survivors);
}

Orders thresholdOrders()
{
	const auto largest = std::numeric_limits<unsigned int>::max();
	return {buy("zero", 0),
			sell("one", 1),
			buy("below", 99),
			buy("equal-buy", 100),
			sell("equal-sell", 100),
			sell("above", 101),
			buy("largest-buy", largest),
			sell("largest-sell", largest)};
}

struct ThresholdCase
{
	std::string name;
	unsigned int minimum;
	std::size_t survivorCount;
};
class MinimumQuantityTest
	: public CacheTest
	, public ::testing::WithParamInterface<ThresholdCase>
{
};

TEST_P(MinimumQuantityTest, RemovesBothSidesAtOrAboveTheInclusiveBoundary)
{
	// Quantities are deliberately ordered; the table states the survivor count instead of repeating >=.
	const auto original = thresholdOrders();
	const auto unrelated = sell("other-security", std::numeric_limits<unsigned int>::max(), carol, "BOND");
	givenOrders(original);
	givenOrders({unrelated});
	cache->cancelOrdersForSecIdWithMinimumQty("ACME", GetParam().minimum);

	Orders expected;
	for (std::size_t index = 0; index < GetParam().survivorCount; ++index)
	{
		expected.push_back(original.at(index));
	}
	expected.push_back(unrelated);
	expectOrders(expected);
}

INSTANTIATE_TEST_SUITE_P(
	InclusiveBoundaries,
	MinimumQuantityTest,
	::testing::Values(ThresholdCase{"ZeroRemovesEverythingIncludingZero", 0, 0},
					  ThresholdCase{"OnePreservesOnlyZero", 1, 1},
					  ThresholdCase{"ImmediatelyBelowHundred", 99, 2},
					  ThresholdCase{"ExactlyHundredRemovesBothEqualSides", 100, 3},
					  ThresholdCase{"ImmediatelyAboveHundred", 101, 5},
					  ThresholdCase{"AboveSmallOrders", 102, 6},
					  ThresholdCase{"ImmediatelyBelowUnsignedMaximum", std::numeric_limits<unsigned int>::max() - 1, 6},
					  ThresholdCase{"ExactlyUnsignedMaximum", std::numeric_limits<unsigned int>::max(), 6}),
	[](const ::testing::TestParamInfo<ThresholdCase> & caseInfo)
	{
		return caseInfo.param.name;
	});

TEST_F(CancellationTest, ThresholdIsAppliedPerOrderRatherThanPerCompanyOrUser)
{
	const Orders survivors{buy("first-small", 60), buy("second-small", 60), sell("small-sell", 60, alice)};
	givenOrders(survivors);
	givenOrders({sell("large-sell", 120)});
	cache->cancelOrdersForSecIdWithMinimumQty("ACME", 100);
	// Alice's total of 180 does not turn three small orders into one large order.
	expectOrders(survivors);
}

TEST_F(CancellationTest, SecurityComparisonIsExactAndDoesNotUsePrefixesOrCaseFolding)
{
	const Orders survivors{buy("lowercase", 100, alice, "acme"),
						   buy("suffix", 100, alice, "ACME.FUT"),
						   sell("prefix", 100, bob, "PRE-ACME")};
	givenOrders(survivors);
	givenOrders({buy("target", 100)});
	cache->cancelOrdersForSecIdWithMinimumQty("ACME", 0);
	expectOrders(survivors);
}

TEST_F(CancellationTest, ThresholdAboveEveryQuantityLeavesTheBookUntouched)
{
	const Orders original{buy("buy", 99), sell("sell", 100)};
	givenOrders(original);
	cache->cancelOrdersForSecIdWithMinimumQty("ACME", 101);
	expectOrders(original);
}

TEST_F(CancellationTest, OverlappingCancellationMethodsLeaveOnlyTheirComplement)
{
	const auto survivor = sell("survivor", 25, carol, "BOND");
	givenOrders({buy("alice-large", 200),
				 buy("alice-small", 10),
				 sell("bob-large", 150),
				 sell("bob-small", 20),
				 buy("alex-bond", 30, alex, "BOND"),
				 survivor});
	// Cancellation sets overlap and already-removed IDs are encountered again.
	cache->cancelOrder("alice-large");
	cache->cancelOrdersForUser("alice");
	cache->cancelOrdersForSecIdWithMinimumQty("ACME", 100);
	cache->cancelOrdersForUser("bob");
	cache->cancelOrder("alex-bond");
	cache->cancelOrder("alice-large");
	expectOrders({survivor});
}
} // namespace
} // namespace order_cache::test
