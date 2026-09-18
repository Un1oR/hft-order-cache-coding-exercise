#include "OrderRemovalRange.h"
#include "QuantityIndex.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <ranges>
#include <vector>

namespace order_cache::test
{
namespace
{
using Records = detail::SlotPool<detail::QuantityIndexRecord>;
struct RecordBySlotId
{
	Records * records = nullptr;
	detail::QuantityIndexRecord & operator()(detail::OrderStorageSlotId slot) const noexcept
	{
		return (*records)[slot];
	}
	// The index/range must use the exact const resolver overload it constrains.
	detail::QuantityIndexRecord & operator()(detail::OrderStorageSlotId) = delete;
};
using Range = detail::OrderRemovalRange<RecordBySlotId>;
static_assert(std::ranges::view<Range>);
static_assert(std::ranges::forward_range<const Range>);
static_assert(std::ranges::sized_range<const Range>);
static_assert(!std::ranges::borrowed_range<Range>);

struct ThrowingLookup
{
	detail::QuantityIndexRecord & operator()(detail::OrderStorageSlotId) const;
};
struct ValueLookup
{
	detail::QuantityIndexRecord operator()(detail::OrderStorageSlotId) const noexcept;
};
static_assert(!detail::QuantityRecordLookup<ThrowingLookup>);
static_assert(!detail::QuantityRecordLookup<ValueLookup>);

TEST(OrderRemovalRangeTest, EmptyViewHasConstantTimeSizeAndNoDereference)
{
	const Range empty;
	EXPECT_EQ(empty.size(), 0U);
	EXPECT_EQ(empty.begin(), empty.end());
}

TEST(OrderRemovalRangeTest, CopiesRestartAndIterationSurvivesCurrentRecordDestruction)
{
	Records records;
	const auto first = records.emplace();
	const auto second = records.emplace();
	const auto third = records.emplace();
	records[first].nextOrderInRemovalBatch = second;
	records[second].nextOrderInRemovalBatch = third;
	const Range range(RecordBySlotId{&records}, first, 3);
	const auto copy = range;
	const std::array expected{first, second, third};
	EXPECT_TRUE(std::ranges::equal(range, expected));
	EXPECT_TRUE(std::ranges::equal(copy, expected));
	std::vector<detail::OrderStorageSlotId> visited;
	for (const auto slot : copy)
	{
		visited.push_back(slot);
		records.erase(slot); // ++ must not read the just-destroyed record.
	}
	EXPECT_TRUE(std::ranges::equal(visited, expected));
	EXPECT_EQ(records.size(), 0U);
}

TEST(OrderRemovalRangeTest, ThresholdRepairCompletesBeforeSparseAndDenseCleanup)
{
	Records records;
	detail::QuantityIndex index(RecordBySlotId{&records});
	for (unsigned int quantity = 0; quantity < 64; ++quantity)
	{
		const auto slot = records.emplace();
		index.insert({.quantity = quantity, .slot = slot});
	}
	for (const auto minimum : {64U, 63U, 32U, 0U})
	{
		const auto before = records.size();
		const auto removed = index.cancelAtLeast(minimum);
		EXPECT_EQ(index.size(), before - removed.size());
		EXPECT_EQ(records.size(), before); // Detached records still exist.
		std::size_t count = 0;
		for (const auto slot : removed)
		{
			EXPECT_GE(slot, minimum);
			records.erase(slot);
			++count;
		}
		EXPECT_EQ(count, removed.size());
		EXPECT_EQ(records.size(), index.size());
	}
	EXPECT_TRUE(index.empty());

	// Freed slots can be reused once the old range has been fully consumed.
	const auto reused = records.emplace();
	index.insert({.quantity = 7, .slot = reused});
	index.erase(reused);
	records.erase(reused);
	EXPECT_TRUE(index.empty());
}

TEST(OrderRemovalRangeTest, IncludedRecordsResolvePositionsDuringSparseErasure)
{
	Records records;
	detail::QuantityIndex index(RecordBySlotId{&records});
	for (unsigned int quantity = 0; quantity < 64; ++quantity)
	{
		index.insert({.quantity = quantity, .slot = records.emplace()});
	}
	// Removing the maximum moves other entries before their turn in the view.
	records[63].nextOrderInRemovalBatch = 0;
	records[0].nextOrderInRemovalBatch = 31;
	const Range included(RecordBySlotId{&records}, 63, 3);
	const auto removed = index.eraseOrders(included,
										   [](const detail::QuantityEntry & entry) noexcept
										   {
											   return entry.slot == 63 || entry.slot == 0 || entry.slot == 31;
										   });
	EXPECT_EQ(index.size(), 61U);
	for (const auto slot : removed)
	{
		records.erase(slot);
	}
	for (detail::OrderStorageSlotId slot = 0; slot < 64; ++slot)
	{
		if (records.contains(slot))
		{
			index.erase(slot); // Every survivor must have a correct reverse position.
			records.erase(slot);
		}
	}
	EXPECT_TRUE(index.empty());
}
} // namespace
} // namespace order_cache::test
