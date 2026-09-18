#include "OrderCache.h"

#include "AllocationFailure.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <new>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace order_cache::test
{
namespace
{
using Fields = std::tuple<std::string, std::string, std::string, unsigned int, std::string, std::string>;
std::vector<Fields> fields(const OrderCacheInterface & cache)
{
	std::vector<Fields> result;
	for (const auto & order : cache.getAllOrders())
	{
		result.emplace_back(order.orderId(),
							order.securityId(),
							order.side(),
							order.qty(),
							order.user(),
							order.company());
	}
	std::ranges::sort(result);
	return result;
}

void checkFailures(std::size_t initialCount, bool newUser, bool newSecurity, bool newCompany)
{
	const std::string user(48, 'u');
	const std::string security(48, 's');
	const std::string company(48, 'c');
	const Order extra(std::string(48, 'i'),
					  newSecurity ? security + "new" : security,
					  "Sell",
					  3,
					  newUser ? user + "new" : user,
					  newCompany ? company + "new" : company);
	bool reachedSuccess = false;
	std::size_t failures = 0;
	for (std::size_t allocation = 0; allocation < 128; ++allocation)
	{
		SCOPED_TRACE(::testing::Message() << initialCount << '/' << newUser << '/' << newSecurity << '/' << newCompany
										  << "/allocation=" << allocation);
		auto cache = makeOrderCache();
		for (std::size_t number = 0; number < initialCount; ++number)
		{
			cache->addOrder(Order(std::to_string(number), security, "Buy", 1, user, company));
		}
		const auto before = fields(*cache);
		auto input = extra;
		bool failed = false;
		{
			allocation_failure::FailAfter fail(allocation);
			try
			{
				cache->addOrder(std::move(input));
			}
			catch (const std::bad_alloc &)
			{
				failed = true;
			}
		}
		if (!failed)
		{
			reachedSuccess = true;
			EXPECT_EQ(cache->getAllOrders().size(), initialCount + 1);
			break;
		}
		++failures;
		EXPECT_EQ(fields(*cache), before);
		EXPECT_EQ(cache->getMatchingSizeForSecurity(security), 0U);
		cache->cancelOrder(extra.orderId());
		cache->addOrder(extra); // Reuse the rejected ID, slot and group names.
		EXPECT_EQ(cache->getAllOrders().size(), initialCount + 1);
		EXPECT_EQ(cache->getMatchingSizeForSecurity(security),
				  !newSecurity && newCompany ? std::min(initialCount, std::size_t{3}) : 0U);
		cache->cancelOrder(extra.orderId());
		EXPECT_EQ(fields(*cache), before);
		cache->cancelOrdersForUser(user);
		EXPECT_TRUE(cache->getAllOrders().empty());
	}
	EXPECT_TRUE(reachedSuccess);
	EXPECT_GT(failures, 0U);
}

TEST(InsertionRollbackTest, EveryColdInsertionAllocationCanFailWithoutChangingTheBook)
{
	checkFailures(0, true, true, true);
}

TEST(InsertionRollbackTest, EveryAllocationPreservesExistingAndNewGroupCombinations)
{
	for (const bool newUser : {false, true})
	{
		for (const bool newSecurity : {false, true})
		{
			for (const bool newCompany : {false, true})
			{
				checkFailures(8, newUser, newSecurity, newCompany);
			}
		}
	}
}

TEST(InsertionRollbackTest, BlockAndDirectoryGrowthCanFailWithoutLosingLiveOrders)
{
	checkFailures(256, false, false, true);
}
TEST(InsertionRollbackTest, CancellationsDoNotAllocateWhileCollectingOrReclaimingRecords)
{
	auto cache = makeOrderCache();
	for (unsigned int number = 0; number < 128; ++number)
	{
		cache->addOrder(
			Order(std::to_string(number), "SEC", "Buy", number, number % 2 == 0 ? "even" : "odd", "company"));
	}
	const std::string security = "SEC";
	const std::string user = "even";
	const std::string single = "1";
	{
		allocation_failure::FailAfter fail(0);
		cache->cancelOrdersForSecIdWithMinimumQty(security, 127); // Sparse range.
		cache->cancelOrdersForSecIdWithMinimumQty(security, 64); // Dense range.
		cache->cancelOrdersForUser(user);
		cache->cancelOrder(single);
	}
	EXPECT_EQ(cache->getAllOrders().size(), 31U);
	{
		allocation_failure::FailAfter fail(0);
		cache->cancelOrdersForSecIdWithMinimumQty(security, 0);
	}
	EXPECT_TRUE(cache->getAllOrders().empty());
}

} // namespace
} // namespace order_cache::test
