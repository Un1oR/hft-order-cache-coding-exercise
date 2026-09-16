#include "support/MatchingOracle.h"
#include "support/OrderCacheTestSupport.h"

#include <array>
#include <string>

namespace order_cache::test
{
namespace
{
TEST(ExhaustiveMatchingTest, ChecksEveryZeroOneTwoQuantityBookForThreeCompanies)
{
	const std::array<Participant, 3> participants{alice, bob, carol};
	// Six slots: Buy/Sell for each of three companies. 3^6 = 729 books, with no random sampling.
	constexpr unsigned int bookCount = 729;
	for (unsigned int bookNumber = 0; bookNumber < bookCount; ++bookNumber)
	{
		auto digits = bookNumber;
		Orders orders;
		for (const auto & participant : participants)
		{
			const auto buyQuantity = digits % 3;
			digits /= 3;
			const auto sellQuantity = digits % 3;
			digits /= 3;
			// A zero slot means no order; explicit qty=0 orders are covered by separate contract tests.
			if (buyQuantity != 0)
			{
				orders.push_back(buy(participant.company + "-buy", buyQuantity, participant));
			}
			if (sellQuantity != 0)
			{
				orders.push_back(sell(participant.company + "-sell", sellQuantity, participant));
			}
		}
		SCOPED_TRACE(::testing::Message() << "book=" << bookNumber << " " << ::testing::PrintToString(orders));
		auto cache = makeOrderCache();
		ASSERT_NE(cache, nullptr);
		addOrders(*cache, orders);
		// ASSERT limits diagnostics to the first counterexample instead of hundreds of identical failures.
		ASSERT_EQ(cache->getMatchingSizeForSecurity("ACME"), maximumUnitMatching(orders, "ACME"));
		ASSERT_TRUE(sameOrders(snapshot(cache->getAllOrders()), orders));
	}
}
} // namespace
} // namespace order_cache::test
