#include "support/MatchingOracle.h"
#include "support/OrderCacheTestSupport.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>

namespace order_cache::test
{
namespace
{
using StateTransitionTest = CacheTest;

TEST_F(StateTransitionTest, EveryAddInvalidatesPreviouslyComputedMatching)
{
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 0U);
	givenOrders({buy("buy", 100)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 0U);
	givenOrders({sell("first-sell", 30)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 30U);
	givenOrders({sell("second-sell", 40, carol)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 70U);
	givenOrders({sell("third-sell", 60)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 100U);
	expectOrders({buy("buy", 100), sell("first-sell", 30), sell("second-sell", 40, carol), sell("third-sell", 60)});
}

struct CancelMatchCase
{
	std::string name;
	CancelRoute route;
	OrderData target;
	Orders survivors;
	unsigned int expectedMatch;
};

class MatchingAfterCancellationTest
	: public CacheTest
	, public ::testing::WithParamInterface<CancelMatchCase>
{
};

TEST_P(MatchingAfterCancellationTest, RecomputesMatchingWithoutTouchingOtherSecurities)
{
	const auto & scenario = GetParam();
	const Participant bondBuyer{.user = "bond-buyer", .company = "BondBuyCo"};
	const Participant bondSeller{.user = "bond-seller", .company = "BondSellCo"};
	const Orders otherSecurity{buy("bond-buy", 15, bondBuyer, "BOND"), sell("bond-sell", 12, bondSeller, "BOND")};
	givenOrders(
		{buy("alice-buy", 100), buy("alex-buy", 50, alex), sell("bob-sell", 80), sell("carol-sell", 100, carol)});
	givenOrders(otherSecurity);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 150U);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("BOND"), 12U);

	// Remove data through one of the public methods after computing matching.
	cancelThrough(*cache, scenario.route, scenario.target);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), scenario.expectedMatch);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("BOND"), 12U);
	auto expected = scenario.survivors;
	expected.insert(expected.end(), otherSecurity.begin(), otherSecurity.end());
	expectOrders(expected);
}

// clang-format off
INSTANTIATE_TEST_SUITE_P(
    MutationInvalidation,
    MatchingAfterCancellationTest,
    ::testing::Values(
        CancelMatchCase{"CancelBuyById", CancelRoute::OrderId, buy("alice-buy", 100),
                        {buy("alex-buy", 50, alex), sell("bob-sell", 80), sell("carol-sell", 100, carol)}, 50},
        CancelMatchCase{"CancelSellById", CancelRoute::OrderId, sell("carol-sell", 100, carol),
                        {buy("alice-buy", 100), buy("alex-buy", 50, alex), sell("bob-sell", 80)}, 80},
        CancelMatchCase{"CancelBuyerUser", CancelRoute::User, buy("alice-buy", 100),
                        {buy("alex-buy", 50, alex), sell("bob-sell", 80), sell("carol-sell", 100, carol)}, 50},
        CancelMatchCase{"CancelSellerUser", CancelRoute::User, sell("bob-sell", 80),
                        {buy("alice-buy", 100), buy("alex-buy", 50, alex), sell("carol-sell", 100, carol)}, 100},
        CancelMatchCase{"CancelInclusiveThreshold", CancelRoute::Security, buy("threshold", 80),
                        {buy("alex-buy", 50, alex)}, 0}),
    [](const ::testing::TestParamInfo<CancelMatchCase> & caseInfo) { return caseInfo.param.name; });
// clang-format on

TEST_F(StateTransitionTest, DepletedSecurityCanBeRepopulatedAndMatchedAgain)
{
	givenOrders({buy("old-buy", 100), sell("old-sell", 70)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 70U);
	cache->cancelOrdersForSecIdWithMinimumQty("ACME", 0);
	expectOrders({});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 0U);
	givenOrders({buy("new-buy", 9, carol), sell("new-sell", 14)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 9U);
	expectOrders({buy("new-buy", 9, carol), sell("new-sell", 14)});
}

TEST_F(StateTransitionTest, RemovingAnIncompatibleOrderDoesNotRemoveCompatibleLiquidity)
{
	const auto compatible = sell("compatible", 40);
	const auto buyer = buy("buyer", 100);
	givenOrders({buyer, compatible, sell("blocked", 100, alex)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 40U);
	cache->cancelOrder("blocked");
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 40U);
	expectOrders({buyer, compatible});
}

TEST_F(StateTransitionTest, ReaddingTheSameIdOnTheOppositeSideChangesMatching)
{
	givenOrders({buy("reused", 100), sell("counterparty", 60)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 60U);
	cache->cancelOrder("reused");
	givenOrders({sell("reused", 100, alice)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 0U);
	expectOrders({sell("reused", 100, alice), sell("counterparty", 60)});
}

class DeterministicHistoryTest
	: public CacheTest
	, public ::testing::WithParamInterface<std::uint32_t>
{
};

TEST_P(DeterministicHistoryTest, AgreesWithAnIndependentSmallModelAfterEveryOperation)
{
	// Reproducible generator: mt19937 plus modulo, without random_device or implementation-defined distributions.
	std::mt19937 random(GetParam());
	const std::array<Participant, 4> participants{alice, alex, bob, carol};
	const std::array<std::string, 3> securities{"ACME", "BOND", "FUND"};
	Orders model;
	unsigned int nextId = 0;
	std::ostringstream history;
	history << "seed=" << GetParam() << '\n';

	for (unsigned int step = 0; step < 160; ++step)
	{
		const auto previousModel = model;
		const auto previousSnapshot = cache->getAllOrders();
		auto operation = random() % 8;
		// At most six orders of 0..2 units explicitly bounds the small exhaustive oracle.
		if (model.size() == 6 && operation < 3)
		{
			operation = 3;
		}
		history << step << ": ";
		if (operation < 3)
		{
			const auto & participant = participants.at(random() % participants.size());
			const auto & security = securities.at(random() % securities.size());
			const auto quantity = static_cast<unsigned int>(random() % 3);
			const auto orderId = "generated-" + std::to_string(nextId++);
			const auto order = random() % 2 == 0 ? buy(orderId, quantity, participant, security)
												 : sell(orderId, quantity, participant, security);
			history << "add " << order;
			cache->addOrder(toOrder(order));
			model.push_back(order);
		}
		else if (operation == 3)
		{
			const auto orderId =
				!model.empty() && random() % 2 == 0 ? model.at(random() % model.size()).orderId : "missing-order";
			history << "cancelOrder " << orderId;
			cache->cancelOrder(orderId);
			std::erase_if(model,
						  [&orderId](const OrderData & order)
						  {
							  return order.orderId == orderId;
						  });
		}
		else if (operation == 4)
		{
			const auto & user = participants.at(random() % participants.size()).user;
			history << "cancelUser " << user;
			cache->cancelOrdersForUser(user);
			std::erase_if(model,
						  [&user](const OrderData & order)
						  {
							  return order.user == user;
						  });
		}
		else if (operation == 5)
		{
			const auto & security = securities.at(random() % securities.size());
			const auto minimum = static_cast<unsigned int>(random() % 4);
			history << "cancelSecurity " << security << " minimum=" << minimum;
			cache->cancelOrdersForSecIdWithMinimumQty(security, minimum);
			std::erase_if(model,
						  [&security, minimum](const OrderData & order)
						  {
							  return order.security == security && order.quantity >= minimum;
						  });
		}
		else
		{
			history << (operation == 6 ? "matching query" : "const snapshot");
		}
		history << '\n';
		SCOPED_TRACE(history.str());
		// The simple model stores values in a vector and does not reproduce implementation indexes.
		ASSERT_TRUE(sameOrders(snapshot(cache->getAllOrders()), model));
		ASSERT_TRUE(sameOrders(snapshot(previousSnapshot), previousModel));
		for (const auto & security : securities)
		{
			SCOPED_TRACE(security);
			ASSERT_EQ(cache->getMatchingSizeForSecurity(security), maximumUnitMatching(model, security));
		}
		ASSERT_EQ(cache->getMatchingSizeForSecurity("MISSING"), 0U);
		// Check state after every query as well: matching must not consume orders.
		ASSERT_TRUE(sameOrders(snapshot(cache->getAllOrders()), model));
	}
}

INSTANTIATE_TEST_SUITE_P(FixedSeeds,
						 DeterministicHistoryTest,
						 ::testing::Values(0U, 1U, 7U, 42U, 2026U, 65537U, 0xC0FFEEU, 0xDEADBEEFU),
						 [](const ::testing::TestParamInfo<std::uint32_t> & caseInfo)
						 {
							 return "Seed" + std::to_string(caseInfo.param);
						 });
} // namespace
} // namespace order_cache::test
