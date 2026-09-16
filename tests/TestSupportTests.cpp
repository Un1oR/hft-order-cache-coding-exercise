#include "support/MatchingOracle.h"
#include "support/OrderCacheTestSupport.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace order_cache::test
{
namespace
{
TEST(OrderComparisonTest, IgnoresOrderingButNotMultiplicity)
{
	const auto first = buy("first", 10);
	const auto second = sell("second", 7);
	EXPECT_TRUE(sameOrders({first, second}, {second, first}));
	EXPECT_FALSE(sameOrders({first, first, second}, {first, second}));
	EXPECT_FALSE(sameOrders({first}, {first, second}));
}

TEST(OrderComparisonTest, DetectsADifferenceInEveryPublicField)
{
	const auto original = buy("original", 10);
	for (unsigned int field = 0; field < 6; ++field)
	{
		auto changed = original;
		switch (field)
		{
		case 0:
			changed.orderId = "different-id";
			break;
		case 1:
			changed.security = "BOND";
			break;
		case 2:
			changed.side = "Sell";
			break;
		case 3:
			changed.quantity = 11;
			break;
		case 4:
			changed.user = "different-user";
			break;
		case 5:
			changed.company = "different-company";
			break;
		default:
			FAIL() << "Unexpected field";
		}
		SCOPED_TRACE(field);
		EXPECT_FALSE(sameOrders({original}, {changed}));
	}
}

TEST(OrderComparisonTest, ComparesAllFieldsEvenWhenIdsAreRepeated)
{
	const auto first = buy("same-id", 10);
	const auto second = buy("same-id", 20);
	EXPECT_TRUE(sameOrders({first, second}, {second, first}));
	EXPECT_FALSE(sameOrders({first, second}, {first, first}));
}

TEST(OrderComparisonTest, PublicSnapshotRoundTripsAllFields)
{
	const Orders expected{buy("buy", 11), sell("sell", 7, carol, "BOND")};
	const std::vector<Order> actual{toOrder(expected.front()), toOrder(expected.back())};
	EXPECT_TRUE(sameOrders(snapshot(actual), expected));
}

TEST(MatchingOracleTest, EmptyOneSidedAndSameCompanyBooksCannotMatch)
{
	EXPECT_EQ(maximumUnitMatching({}, "ACME"), 0U);
	EXPECT_EQ(maximumUnitMatching({buy("buy", 2)}, "ACME"), 0U);
	EXPECT_EQ(maximumUnitMatching({sell("sell", 2)}, "ACME"), 0U);
	EXPECT_EQ(maximumUnitMatching({buy("buy", 2), sell("sell", 2, alex)}, "ACME"), 0U);
}

TEST(MatchingOracleTest, AllocatesEachUnitAtMostOnceAndAllowsPartialOrders)
{
	EXPECT_EQ(maximumUnitMatching({buy("buy", 3), sell("sell", 2)}, "ACME"), 2U);
	EXPECT_EQ(maximumUnitMatching({buy("buy", 2), sell("one", 2), sell("two", 2, carol)}, "ACME"), 2U);
	EXPECT_EQ(maximumUnitMatching({sell("sell", 2), buy("one", 2), buy("two", 2, carol)}, "ACME"), 2U);
}

TEST(MatchingOracleTest, ExploresAlternativesInsteadOfReturningTheFirstAllocation)
{
	const Orders trap{buy("atlas-buy", 2),
					  buy("beacon-buy", 2, bob),
					  sell("cedar-sell", 2, carol),
					  sell("beacon-sell", 2)};
	EXPECT_EQ(maximumUnitMatching(trap, "ACME"), 4U);
}

TEST(MatchingOracleTest, FiltersSecurityBeforeApplyingTheSizeLimit)
{
	const Orders orders{buy("buy", 2),
						sell("sell", 1),
						sell("other", std::numeric_limits<unsigned int>::max(), carol, "BOND")};
	EXPECT_EQ(maximumUnitMatching(orders, "ACME"), 1U);
	EXPECT_EQ(maximumUnitMatching(orders, "MISSING"), 0U);
}

TEST(MatchingOracleTest, RejectsOversizedInputsRatherThanHangingOrOverflowing)
{
	EXPECT_THROW(maximumUnitMatching({buy("too-large", 13)}, "ACME"), std::invalid_argument);
	EXPECT_THROW(maximumUnitMatching({buy("large", 7), sell("large-sell", 6)}, "ACME"), std::invalid_argument);
	EXPECT_THROW(maximumUnitMatching({buy("max", std::numeric_limits<unsigned int>::max())}, "ACME"),
				 std::invalid_argument);
}

TEST(MatchingOracleTest, AcceptsTheDocumentedTwelveUnitBoundary)
{
	EXPECT_EQ(maximumUnitMatching({buy("buy", 6), sell("sell", 6)}, "ACME"), 6U);
}

TEST(MatchingOracleTest, RejectsInvalidSidesInTestData)
{
	auto invalid = buy("invalid", 1);
	invalid.side = "Hold";
	EXPECT_THROW(maximumUnitMatching({invalid}, "ACME"), std::invalid_argument);
}
} // namespace
} // namespace order_cache::test
