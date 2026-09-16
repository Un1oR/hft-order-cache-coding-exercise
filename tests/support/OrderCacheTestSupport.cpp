#include "OrderCacheTestSupport.h"

#include <algorithm>
#include <iomanip>
#include <tuple>

namespace order_cache::test
{
namespace
{
void sortOrders(Orders & orders)
{
	std::ranges::sort(
		orders,
		[](const OrderData & left, const OrderData & right)
		{
			return std::tie(left.orderId, left.security, left.side, left.quantity, left.user, left.company)
				< std::tie(right.orderId, right.security, right.side, right.quantity, right.user, right.company);
		});
}
} // namespace

std::ostream & operator<<(std::ostream & stream, const OrderData & order)
{
	return stream << "Order{orderId=" << std::quoted(order.orderId) << ", security=" << std::quoted(order.security)
				  << ", side=" << std::quoted(order.side) << ", qty=" << order.quantity
				  << ", user=" << std::quoted(order.user) << ", company=" << std::quoted(order.company) << '}';
}

Order toOrder(const OrderData & data)
{
	return {data.orderId, data.security, data.side, data.quantity, data.user, data.company};
}

Orders snapshot(const std::vector<Order> & orders)
{
	Orders result;
	result.reserve(orders.size());
	for (const auto & order : orders)
	{
		result.push_back({.orderId = order.orderId(),
						  .security = order.securityId(),
						  .side = order.side(),
						  .quantity = order.qty(),
						  .user = order.user(),
						  .company = order.company()});
	}
	return result;
}

::testing::AssertionResult sameOrders(Orders actual, Orders expected)
{
	// Compare multisets without imposing output order or losing duplicates.
	sortOrders(actual);
	sortOrders(expected);
	if (actual == expected)
	{
		return ::testing::AssertionSuccess();
	}
	return ::testing::AssertionFailure() << "Cache contents differ.\nExpected (" << expected.size()
										 << "): " << ::testing::PrintToString(expected) << "\nActual (" << actual.size()
										 << "): " << ::testing::PrintToString(actual);
}

OrderData buy(const std::string & orderId,
			  unsigned int quantity,
			  const Participant & participant,
			  const std::string & security)
{
	return {.orderId = orderId,
			.security = security,
			.side = "Buy",
			.quantity = quantity,
			.user = participant.user,
			.company = participant.company};
}

OrderData sell(const std::string & orderId,
			   unsigned int quantity,
			   const Participant & participant,
			   const std::string & security)
{
	return {.orderId = orderId,
			.security = security,
			.side = "Sell",
			.quantity = quantity,
			.user = participant.user,
			.company = participant.company};
}

void addOrders(OrderCacheInterface & cache, const Orders & orders)
{
	for (const auto & order : orders)
	{
		cache.addOrder(toOrder(order));
	}
}

void CacheTest::SetUp()
{
	cache = makeOrderCache();
	ASSERT_NE(cache, nullptr) << "The factory must return an OrderCacheInterface instance";
}

void CacheTest::givenOrders(const Orders & orders)
{
	addOrders(*cache, orders);
}

void CacheTest::expectOrders(const Orders & expected) const
{
	const OrderCacheInterface & view = *cache;
	EXPECT_TRUE(sameOrders(snapshot(view.getAllOrders()), expected));
}

std::string routeName(CancelRoute route)
{
	switch (route)
	{
	case CancelRoute::OrderId:
		return "ByOrderId";
	case CancelRoute::User:
		return "ByUser";
	case CancelRoute::Security:
		return "BySecurity";
	}
	return "UnknownRoute";
}

void cancelThrough(OrderCacheInterface & cache, CancelRoute route, const OrderData & target)
{
	switch (route)
	{
	case CancelRoute::OrderId:
		cache.cancelOrder(target.orderId);
		break;
	case CancelRoute::User:
		cache.cancelOrdersForUser(target.user);
		break;
	case CancelRoute::Security:
		cache.cancelOrdersForSecIdWithMinimumQty(target.security, target.quantity);
		break;
	}
}
} // namespace order_cache::test
