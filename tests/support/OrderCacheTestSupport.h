#pragma once

#include "OrderCache.h"

#include <gtest/gtest.h>

#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace order_cache::test
{
// Participant names make both the user and company explicit.
struct Participant
{
	std::string user;
	std::string company;
};

inline const Participant alice{"alice", "Atlas"};
inline const Participant alex{"alex", "Atlas"};
inline const Participant bob{"bob", "Beacon"};
inline const Participant beth{"beth", "Beacon"};
inline const Participant carol{"carol", "Cedar"};

// Independent snapshot of all six public Order fields; does not access internals.
struct OrderData
{
	std::string orderId;
	std::string security;
	std::string side;
	unsigned int quantity;
	std::string user;
	std::string company;

	bool operator==(const OrderData &) const = default;
};

using Orders = std::vector<OrderData>;

std::ostream & operator<<(std::ostream & stream, const OrderData & order);
Order toOrder(const OrderData & data);
Orders snapshot(const std::vector<Order> & orders);
::testing::AssertionResult sameOrders(Orders actual, Orders expected);

OrderData buy(const std::string & orderId,
			  unsigned int quantity,
			  const Participant & participant = alice,
			  const std::string & security = "ACME");
OrderData sell(const std::string & orderId,
			   unsigned int quantity,
			   const Participant & participant = bob,
			   const std::string & security = "ACME");
void addOrders(OrderCacheInterface & cache, const Orders & orders);

class CacheTest : public ::testing::Test
{
protected:
	void SetUp() override;
	void givenOrders(const Orders & orders);
	void expectOrders(const Orders & expected) const;

	// Each test owns an independent cache; teardown uses RAII.
	std::unique_ptr<OrderCacheInterface> cache;
};

// Shared routes cover contracts that must hold for every cancellation method.
enum class CancelRoute
{
	OrderId,
	User,
	Security
};
std::string routeName(CancelRoute route);
void cancelThrough(OrderCacheInterface & cache, CancelRoute route, const OrderData & target);
} // namespace order_cache::test
