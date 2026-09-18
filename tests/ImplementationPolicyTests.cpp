#include "support/OrderCacheTestSupport.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace order_cache::test
{
namespace
{
// These are explicit implementation policies, not retroactive assignment rules.
using ImplementationPolicyTest = CacheTest;

TEST_F(ImplementationPolicyTest, DuplicateActiveIdIsRejectedWithoutChangingTheOriginal)
{
	const Orders original{buy("duplicate", 10), sell("counterparty", 7)};
	givenOrders(original);
	EXPECT_THROW(cache->addOrder(toOrder(sell("duplicate", 900, carol, "OTHER"))), std::invalid_argument);
	expectOrders(original);
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), 7U);
}

TEST_F(ImplementationPolicyTest, InvalidSideIsRejectedWithoutCreatingAnyOrder)
{
	EXPECT_THROW(cache->addOrder(Order("invalid", "SEC", "buy", 10, "user", "company")), std::invalid_argument);
	expectOrders({});
	givenOrders({buy("invalid", 10)});
	expectOrders({buy("invalid", 10)});
}

TEST_F(ImplementationPolicyTest, UnrepresentableMatchingThrowsInsteadOfWrapping)
{
	constexpr auto maximum = std::numeric_limits<unsigned int>::max();
	const Orders original{buy("buy-1", maximum),
						  buy("buy-2", maximum),
						  sell("sell-1", maximum),
						  sell("sell-2", maximum)};
	givenOrders(original);
	EXPECT_THROW(cache->getMatchingSizeForSecurity("ACME"), std::overflow_error);
	expectOrders(original);
	cache->cancelOrder("buy-2");
	EXPECT_EQ(cache->getMatchingSizeForSecurity("ACME"), maximum);
}

TEST_F(ImplementationPolicyTest, EmptyAndEmbeddedNullKeysUseExactStringLengths)
{
	const std::string binaryId("id\0suffix", 9);
	const std::string binaryUser("user\0suffix", 11);
	const Orders original{buy(binaryId, 3, Participant{.user = binaryUser, .company = "A"}, ""),
						  sell("", 2, Participant{.user = "", .company = "B"}, ""),
						  buy("id", 1)};
	givenOrders(original);
	EXPECT_EQ(cache->getMatchingSizeForSecurity(""), 2U);
	cache->cancelOrdersForUser("user");
	expectOrders(original);
	cache->cancelOrdersForUser(binaryUser);
	expectOrders({original[1], original[2]});
	cache->cancelOrder("");
	expectOrders({original[2]});
}

TEST(FactoryPolicyTest, DefaultFactoryRemainsUsable)
{
	auto cache = makeOrderCache();
	ASSERT_NE(cache, nullptr);
	cache->addOrder(Order("default", "SEC", "Buy", 1, "user", "company"));
	EXPECT_EQ(cache->getAllOrders().size(), 1U);
}
} // namespace
} // namespace order_cache::test
