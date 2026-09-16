#include "support/OrderCacheTestSupport.h"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace order_cache::test
{
namespace
{
struct MatchingCase
{
	std::string name;
	Orders orders;
	unsigned int expected;
};

std::ostream & operator<<(std::ostream & stream, const MatchingCase & scenario)
{
	return stream << scenario.name << ": " << ::testing::PrintToString(scenario.orders)
				  << "; expected=" << scenario.expected;
}

class MatchingRulesTest
	: public CacheTest
	, public ::testing::WithParamInterface<MatchingCase>
{
};

TEST_P(MatchingRulesTest, ReportsTheMatchableQuantityAndPreservesEveryOrder)
{
	const auto & scenario = GetParam();
	// For each table-driven book, matching has the expected size and leaves the book unchanged.
	givenOrders(scenario.orders);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), scenario.expected);
	expectOrders(scenario.orders);
}

// Keep each business case next to its expected result.
// clang-format off
INSTANTIATE_TEST_SUITE_P(
    MatchingRules,
    MatchingRulesTest,
    ::testing::Values(
        MatchingCase{"EmptyBook", {}, 0},
        MatchingCase{"OnlyBuy", {buy("buy", 100)}, 0},
        MatchingCase{"OnlySell", {sell("sell", 100)}, 0},
        MatchingCase{"TwoBuysAcrossCompanies", {buy("atlas", 100), buy("beacon", 100, bob)}, 0},
        MatchingCase{"TwoSellsAcrossCompanies", {sell("atlas", 100, alice), sell("beacon", 100)}, 0},
        MatchingCase{"EqualQuantities", {buy("buy", 100), sell("sell", 100)}, 100},
        MatchingCase{"BuyIsSmaller", {buy("buy", 30), sell("sell", 100)}, 30},
        MatchingCase{"SellIsSmaller", {buy("buy", 100), sell("sell", 30)}, 30},
        MatchingCase{"SingleUnit", {buy("buy", 1), sell("sell", 1)}, 1},
        MatchingCase{"SellInsertedFirst", {sell("sell", 37), buy("buy", 50)}, 37},
        MatchingCase{"DifferentUsersInSameCompany", {buy("alice", 100), sell("alex", 100, alex)}, 0},
        MatchingCase{"SameUserInSameCompany", {buy("buy", 100), sell("sell", 100, alice)}, 0},
        MatchingCase{"DifferentSecurities", {buy("buy", 100), sell("sell", 100, bob, "BOND")}, 0},
        MatchingCase{"SecurityCaseIsSignificant", {buy("buy", 100), sell("sell", 100, bob, "acme")}, 0},
        MatchingCase{"SecurityPrefixIsNotIdentity", {buy("buy", 100), sell("sell", 100, bob, "ACME.FUT")}, 0},
        MatchingCase{"CompanyPrefixIsNotIdentity", {buy("buy", 20), sell("sell", 20, {"other", "Atlas Europe"})}, 20},
        MatchingCase{"CompanyCaseIsSignificant", {buy("buy", 20), sell("sell", 20, {"other", "atlas"})}, 20},
        MatchingCase{"ZeroBuyCannotContribute", {buy("zero", 0), sell("sell", 100)}, 0},
        MatchingCase{"ZeroSellCannotContribute", {buy("buy", 100), sell("zero", 0)}, 0},
        MatchingCase{"BothZero", {buy("buy", 0), sell("sell", 0)}, 0},
        MatchingCase{"OneBuyAcrossSeveralSells", {buy("buy", 100), sell("sell-1", 20), sell("sell-2", 30, carol)}, 50},
        MatchingCase{"OneSellAcrossSeveralBuys", {sell("sell", 100), buy("buy-1", 20), buy("buy-2", 30, carol)}, 50},
        MatchingCase{"BuyQuantityCannotBeReused", {buy("buy", 50), sell("sell-1", 40), sell("sell-2", 40, carol)}, 50},
        MatchingCase{"SellQuantityCannotBeReused", {sell("sell", 50), buy("buy-1", 40), buy("buy-2", 40, carol)}, 50},
        MatchingCase{"PartialLastCounterparty", {buy("buy", 75), sell("first", 50), sell("second", 50, carol)}, 75},
        MatchingCase{"SameCompanyLiquidityIsExcluded", {buy("buy", 100), sell("blocked", 100, alex), sell("allowed", 30)}, 30},
        MatchingCase{"BothSidesDominatedByOneCompany", {buy("atlas-buy", 100), sell("atlas-sell", 100, alex),
                                                        buy("beacon-buy", 10, bob), sell("beacon-sell", 10, beth)}, 20},
        MatchingCase{"ThreeCompanyCycle", {buy("atlas-buy", 4), buy("beacon-buy", 3, bob), buy("cedar-buy", 2, carol),
                                           sell("atlas-sell", 5, alex), sell("beacon-sell", 2), sell("cedar-sell", 2, carol)}, 9},
        MatchingCase{"ZeroOrdersDoNotBlockPositiveMatches", {buy("zero-buy", 0), sell("zero-sell", 0),
                                                             buy("buy", 7), sell("sell", 5)}, 5}),
    [](const ::testing::TestParamInfo<MatchingCase> & caseInfo) { return caseInfo.param.name; });
// clang-format on

using MatchingTest = CacheTest;

TEST_F(MatchingTest, UnknownSecurityReturnsZeroWithoutCreatingOrders)
{
	const Orders original{buy("buy", 100), sell("sell", 70)};
	givenOrders(original);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("NEVER-ADDED"), 0U);
	expectOrders(original);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 70U);
}

TEST_F(MatchingTest, QueriesAreRepeatableNonConsumingAndIsolatedBySecurity)
{
	const Orders original{buy("acme-buy", 100),
						  sell("acme-sell", 70),
						  buy("bond-buy", 50, carol, "BOND"),
						  sell("bond-sell", 20, bob, "BOND")};
	givenOrders(original);
	for (int iteration = 0; iteration < 3; ++iteration)
	{
		SCOPED_TRACE(iteration);
		EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 70U);
		EXPECT_EQ(cache->getMatchingSizeForSecurity("BOND"), 20U);
		EXPECT_EQ(cache->getMatchingSizeForSecurity("MISSING"), 0U);
		expectOrders(original);
	}
}

TEST_F(MatchingTest, MatchingDoesNotReduceQuantitiesUsedBySubsequentCancellation)
{
	const auto originalBuy = buy("buy", 100);
	const auto originalSell = sell("sell", 40);
	givenOrders({originalBuy, originalSell});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 40U);

	// If matching incorrectly consumed 40, the Buy would become 60 and survive the threshold of 80.
	cache->cancelOrdersForSecIdWithMinimumQty("ACME", 80);
	expectOrders({originalSell});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 0U);
}

using InsertionOrder = std::array<std::size_t, 4>;

std::vector<InsertionOrder> allInsertionOrders()
{
	InsertionOrder order{0, 1, 2, 3};
	std::vector<InsertionOrder> result;
	do
	{
		result.push_back(order);
	}
	while (std::ranges::next_permutation(order).found);
	return result;
}

class MaximumMatchingTest
	: public CacheTest
	, public ::testing::WithParamInterface<InsertionOrder>
{
};

TEST_P(MaximumMatchingTest, FindsTheMaximumInsteadOfStoppingAtAGreedyAllocation)
{
	const Orders original{buy("first-buy-atlas", 100),
						  buy("second-buy-beacon", 100, bob),
						  sell("first-sell-cedar", 100, carol),
						  sell("second-sell-beacon", 100)};
	// Greedily pairing Atlas->Cedar leaves Beacon->Beacon and yields 100.
	// The valid reassignment Atlas->Beacon, Beacon->Cedar yields 200.
	for (const auto index : GetParam())
	{
		cache->addOrder(toOrder(original.at(index)));
	}
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 200U);
	expectOrders(original);
}

INSTANTIATE_TEST_SUITE_P(EveryInsertionPermutation,
						 MaximumMatchingTest,
						 ::testing::ValuesIn(allInsertionOrders()),
						 [](const ::testing::TestParamInfo<InsertionOrder> & caseInfo)
						 {
							 std::string name = "Order";
							 for (const auto index : caseInfo.param)
							 {
								 name += std::to_string(index);
							 }
							 return name;
						 });

TEST_F(MatchingTest, SplittingAndMergingSameCompanyOrdersDoesNotChangeTheTotal)
{
	const Orders split{buy("part-one", 17), buy("part-two", 23, alex), sell("sell", 35)};
	givenOrders(split);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 35U);
	cache->cancelOrder("part-one");
	cache->cancelOrder("part-two");
	givenOrders({buy("merged", 40)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 35U);
	expectOrders({buy("merged", 40), split.back()});
}

TEST_F(MatchingTest, SwappingAllSidesPreservesTheMaximum)
{
	Orders orders{buy("atlas", 100), buy("beacon", 100, bob), sell("cedar", 100, carol), sell("beacon-sell", 100)};
	for (auto & order : orders)
	{
		order.side = order.side == "Buy" ? "Sell" : "Buy";
	}
	givenOrders(orders);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 200U);
	expectOrders(orders);
}

TEST_F(MatchingTest, RenamingIdsAndParticipantsDoesNotIntroducePriority)
{
	const Orders renamed{buy("z-buy", 100, {.user = "new-alice", .company = "Zulu"}),
						 buy("a-buy", 100, {.user = "new-bob", .company = "Alpha"}),
						 sell("z-sell", 100, {.user = "new-carol", .company = "Middle"}),
						 sell("a-sell", 100, {.user = "new-beth", .company = "Alpha"})};
	givenOrders(renamed);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 200U);
	expectOrders(renamed);
}

TEST_F(MatchingTest, ScalingAllQuantitiesScalesTheMaximum)
{
	const Orders scaled{buy("atlas", 700),
						buy("beacon", 700, bob),
						sell("cedar", 700, carol),
						sell("beacon-sell", 700)};
	givenOrders(scaled);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 1400U);
	expectOrders(scaled);
}

TEST_F(MatchingTest, UnmatchableAdditionalLiquidityCannotDecreaseAnExistingMatch)
{
	givenOrders({buy("buy", 50), sell("sell", 30)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 30U);
	givenOrders({sell("same-company-extra", 100, alice)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 30U);
	expectOrders({buy("buy", 50), sell("sell", 30), sell("same-company-extra", 100, alice)});
}

constexpr unsigned int largestQuantity = std::numeric_limits<unsigned int>::max();

// Only representable results. The assignment does not define matching totals above UINT_MAX.
// clang-format off
INSTANTIATE_TEST_SUITE_P(
    UnsignedArithmetic,
    MatchingRulesTest,
    ::testing::Values(
        MatchingCase{"LargestSinglePair", {buy("buy", largestQuantity), sell("sell", largestQuantity)}, largestQuantity},
        MatchingCase{"SplitResultReachesUnsignedMaximum", {buy("buy", largestQuantity), sell("almost-all", largestQuantity - 5),
                                                            sell("remaining-five", 5, carol)}, largestQuantity},
        MatchingCase{"BuyTotalExceedsReturnTypeButResultFits", {buy("large", largestQuantity), buy("one", 1, carol),
                                                               sell("sell", largestQuantity)}, largestQuantity},
        MatchingCase{"SellTotalExceedsReturnTypeButResultFits", {sell("large", largestQuantity), sell("one", 1, carol),
                                                                buy("buy", largestQuantity)}, largestQuantity},
        MatchingCase{"CompanyBuyTotalMustNotWrap", {buy("large", largestQuantity), buy("one", 1, alex), sell("sell", 7)}, 7},
        MatchingCase{"CompanySellTotalMustNotWrap", {sell("large", largestQuantity), sell("one", 1, beth), buy("buy", 7)}, 7},
        MatchingCase{"HugeBlockedLiquidityStillCannotMatch", {buy("buy", largestQuantity), sell("blocked", largestQuantity, alex)}, 0}),
    [](const ::testing::TestParamInfo<MatchingCase> & caseInfo) { return caseInfo.param.name; });
// clang-format on
} // namespace
} // namespace order_cache::test
