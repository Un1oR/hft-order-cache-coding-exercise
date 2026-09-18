#include "support/FlowOracle.h"
#include "support/MatchingOracle.h"
#include "support/OrderCacheTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <random>
#include <string>

namespace order_cache::test
{
namespace
{
using LargeBookTest = CacheTest;

TEST_F(LargeBookTest, LargeBatchesAcrossGroupsKeepAllIndexesAndAggregatesConsistent)
{
	const std::array<std::string, 3> securities{"SEC-0", "SEC-1", "SEC-2"};
	Orders model;
	const auto check = [&]
	{
		EXPECT_TRUE(sameOrders(snapshot(cache->getAllOrders()), model));
		for (const auto & security : securities)
		{
			EXPECT_EQ(cache->getMatchingSizeForSecurity(security), maximumFlowMatching(model, security));
		}
	};
	for (unsigned int index = 0; index < 2048; ++index)
	{
		const Participant participant{.user = "user-" + std::to_string(index % 13),
									  .company = "company-" + std::to_string((index / 3) % 32)};
		const auto & security = securities[index % securities.size()];
		const auto name = "large-" + std::to_string(index);
		model.push_back(index % 2 == 0 ? buy(name, index % 256, participant, security)
									   : sell(name, index % 256, participant, security));
	}
	givenOrders(model);
	check();
	cache->cancelOrdersForSecIdWithMinimumQty("SEC-0", 128);
	std::erase_if(model,
				  [](const OrderData & order)
				  {
					  return order.security == "SEC-0" && order.quantity >= 128;
				  });
	check();
	cache->cancelOrdersForUser("user-5");
	std::erase_if(model,
				  [](const OrderData & order)
				  {
					  return order.user == "user-5";
				  });
	check();
	cache->cancelOrdersForSecIdWithMinimumQty("SEC-1", 255);
	std::erase_if(model,
				  [](const OrderData & order)
				  {
					  return order.security == "SEC-1" && order.quantity >= 255;
				  });
	check();

	// Reuse IDs and pool slots with every grouping key changed after sparse and
	// dense repairs. Old group membership must not survive reuse.
	for (unsigned int index = 0; index < 2048; index += 11)
	{
		const auto name = "large-" + std::to_string(index);
		cache->cancelOrder(name);
		std::erase_if(model,
					  [&](const OrderData & order)
					  {
						  return order.orderId == name;
					  });
		const auto replacement = sell(name, 17, Participant{.user = "replacement", .company = "new-company"}, "SEC-2");
		cache->addOrder(toOrder(replacement));
		model.push_back(replacement);
	}
	check();
	cache->cancelOrdersForUser("replacement");
	std::erase_if(model,
				  [](const OrderData & order)
				  {
					  return order.user == "replacement";
				  });
	check();
	for (const auto & security : securities)
	{
		cache->cancelOrdersForSecIdWithMinimumQty(security, 0);
	}
	expectOrders({});
	givenOrders({buy("large-0", 20), sell("large-1", 30)});
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 20U);
}

TEST_F(LargeBookTest, RepeatedGrowthAndMixedEditsAgreeWithAnIndependentFlowOracle)
{
	// A fixed seed makes failing model histories reproducible.
	// NOLINTNEXTLINE(bugprone-random-generator-seed)
	std::mt19937 random(65537);
	Orders model;
	unsigned int nextId = 0;
	for (unsigned int step = 0; step < 600; ++step)
	{
		SCOPED_TRACE(step);
		const auto security = "SEC-" + std::to_string(random() % 3);
		if (step % 37 == 0)
		{
			for (unsigned int added = 0; added < 80; ++added)
			{
				const Participant participant{.user = "user-" + std::to_string(random() % 9),
											  .company = "company-" + std::to_string(random() % 24)};
				const auto name = "generated-" + std::to_string(nextId++);
				const auto quantity = static_cast<unsigned int>(random() % 1000);
				const auto order = random() % 2 == 0 ? buy(name, quantity, participant, security)
													 : sell(name, quantity, participant, security);
				cache->addOrder(toOrder(order));
				model.push_back(order);
			}
		}
		else if (step % 11 == 0)
		{
			const auto user = "user-" + std::to_string(random() % 9);
			cache->cancelOrdersForUser(user);
			std::erase_if(model,
						  [&](const OrderData & order)
						  {
							  return order.user == user;
						  });
		}
		else if (step % 7 == 0)
		{
			const auto minimum = static_cast<unsigned int>(random() % 1100);
			cache->cancelOrdersForSecIdWithMinimumQty(security, minimum);
			std::erase_if(model,
						  [&](const OrderData & order)
						  {
							  return order.security == security && order.quantity >= minimum;
						  });
		}
		else if (!model.empty())
		{
			const auto chosen = random() % model.size();
			cache->cancelOrder(model[chosen].orderId);
			model.erase(model.begin() + static_cast<Orders::difference_type>(chosen));
		}
		ASSERT_TRUE(sameOrders(snapshot(cache->getAllOrders()), model));
		for (unsigned int index = 0; index < 3; ++index)
		{
			const auto name = "SEC-" + std::to_string(index);
			ASSERT_EQ(cache->getMatchingSizeForSecurity(name), maximumFlowMatching(model, name));
		}
	}
}

TEST(FlowOracleTest, AgreesWithUnitAssignmentOnSmallRandomBooks)
{
	// A fixed seed makes failing model histories reproducible.
	// NOLINTNEXTLINE(bugprone-random-generator-seed)
	std::mt19937 random(42);
	const std::array<Participant, 3> participants{alice, bob, carol};
	for (unsigned int book = 0; book < 200; ++book)
	{
		Orders orders;
		for (unsigned int index = 0; index < 6; ++index)
		{
			const auto & participant = participants[random() % participants.size()];
			const auto quantity = static_cast<unsigned int>(random() % 3);
			const auto name = std::to_string(index);
			orders.push_back(random() % 2 == 0 ? buy(name, quantity, participant) : sell(name, quantity, participant));
		}
		ASSERT_EQ(maximumFlowMatching(orders, "ACME"), maximumUnitMatching(orders, "ACME"));
	}
}
} // namespace
} // namespace order_cache::test
