#include "OrderCache.h"

#include <gtest/gtest.h>

TEST(OrderCacheTests, StartsEmpty)
{
	const auto cache = makeOrderCache();

	EXPECT_TRUE(cache->getAllOrders().empty());
}
