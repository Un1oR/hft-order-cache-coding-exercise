#include "CompanyIndex.h"
#include "CompanyTestSupport.h"
#include "OrderCache.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace order_cache::test
{
namespace
{
TEST(CompanyBatchTest, UncommittedCreationPreservesEntriesAndMaximum)
{
	detail::CompanyIndex index;
	auto & first = addCompany(index, "first", 1);
	addCompany(index, "second", 2);
	EXPECT_THROW(
		{
			auto pending = index.getOrCreate("third");
			EXPECT_EQ(pending.get().orders, 0U);
			throw std::runtime_error("abort before publication");
		},
		std::runtime_error);
	EXPECT_EQ(index.size(), 2U);
	EXPECT_EQ(index.maximum(), 2U);
	{
		auto existing = index.getOrCreate("first");
		EXPECT_EQ(&existing.get(), &first);
	} // An existing entry is not owned by the creation token.
	EXPECT_EQ(index.size(), 2U);
	index.add(first, 10);
	EXPECT_EQ(index.maximum(), 11U);
	addCompany(index, "third", 30);
	EXPECT_EQ(index.maximum(), 30U);
}

TEST(CompanyBatchTest, ZeroEntryAppendsWithoutMovingExistingEntries)
{
	detail::CompanyIndex index;
	auto & leader = addCompany(index, "leader", 100);
	auto & zero = addCompany(index, "zero", 0);
	const auto leaderPosition = leader.positionInIndexStorage;
	const auto zeroPosition = zero.positionInIndexStorage;
	auto & last = addCompany(index, "last", 0);
	EXPECT_EQ(last.positionInIndexStorage, 2U);
	EXPECT_EQ(leader.positionInIndexStorage, leaderPosition);
	EXPECT_EQ(zero.positionInIndexStorage, zeroPosition);
	index.add(last, 200);
	EXPECT_EQ(index.maximum(), 200U);
	index.deferRemoval(last, 200);
	index.finishRemovals();
	EXPECT_EQ(index.maximum(), 100U);
	EXPECT_EQ(index.size(), 3U); // A zero-quantity order still keeps last alive.
}

TEST(CompanyBatchTest, SparseErasesReadPositionsAfterEarlierRepairs)
{
	detail::CompanyIndex index;
	std::array<detail::Company *, 64> companies{};
	for (std::size_t number = 0; number < companies.size(); ++number)
	{
		companies[number] = &addCompany(index, std::to_string(number), static_cast<unsigned int>(number + 1));
	}
	// Sparse enough for point repairs. The list removes the root first, which
	// can move another pending entry; subsequent erases must use its new position.
	index.deferRemoval(*companies[0], 1);
	index.deferRemoval(*companies[31], 32);
	index.deferRemoval(*companies[63], 64);
	index.finishRemovals();
	EXPECT_EQ(index.size(), 61U);
	EXPECT_EQ(index.maximum(), 63U);
	for (std::size_t number = 1; number < 63; ++number)
	{
		if (number == 31)
		{
			continue;
		}
		index.add(*companies[number], 1000);
		EXPECT_EQ(index.maximum(), number + 1001);
		index.deferRemoval(*companies[number], 1000);
		index.finishRemovals();
		EXPECT_EQ(index.maximum(), 63U);
	}
}

TEST(CompanyBatchTest, RepeatedDepletionPreservesMatchingAndReusesBatchLinks)
{
	auto cache = makeOrderCache();
	cache->addOrder(Order("sell", "SEC", "Sell", 100, "keeper", "outside"));
	for (int repeat = 0; repeat < 3; ++repeat)
	{
		for (unsigned int number = 0; number < 64; ++number)
		{
			const auto name = std::to_string(number);
			cache->addOrder(Order("buy-" + name, "SEC", "Buy", 1, "bulk", "company-" + name));
			cache->addOrder(Order("zero-" + name, "SEC", "Buy", 0, "bulk", "company-" + name));
		}
		EXPECT_EQ(cache->getMatchingSizeForSecurity("SEC"), 64U);
		cache->cancelOrdersForUser("bulk");
		EXPECT_EQ(cache->getMatchingSizeForSecurity("SEC"), 0U);
		EXPECT_EQ(cache->getAllOrders().size(), 1U);
		cache->addOrder(Order("same", "SEC", "Buy", 2, "bulk", "outside"));
		EXPECT_EQ(cache->getMatchingSizeForSecurity("SEC"), 0U);
		cache->addOrder(Order("other", "SEC", "Buy", 1, "bulk", "other"));
		EXPECT_EQ(cache->getMatchingSizeForSecurity("SEC"), 1U);
		cache->cancelOrdersForUser("bulk");
	}
	cache->cancelOrdersForSecIdWithMinimumQty("SEC", 0);
	EXPECT_TRUE(cache->getAllOrders().empty());
}
} // namespace
} // namespace order_cache::test
