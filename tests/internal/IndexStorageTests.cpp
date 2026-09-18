#include "IndexStorage.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace order_cache::test
{
namespace
{
TEST(IndexStorageTest, PendingAppendOwnsExactlyOneInsertion)
{
	std::vector<int> values{1};
	{
		auto pending = detail::appendPending(values, 2);
		EXPECT_EQ(values, (std::vector<int>{1, 2}));
	}
	EXPECT_EQ(values, (std::vector<int>{1}));
	{
		auto pending = detail::appendPending(values, 3);
		pending.commit();
	}
	EXPECT_EQ(values, (std::vector<int>{1, 3}));
	EXPECT_THROW(
		{
			auto pending = detail::appendPending(values, 4);
			throw std::runtime_error("abort");
		},
		std::runtime_error);
	EXPECT_EQ(values, (std::vector<int>{1, 3}));
}

TEST(IndexStorageTest, PendingMapInsertionNeverRemovesAnExistingEntry)
{
	std::unordered_map<std::string, int> values{{"existing", 1}};
	{
		auto pending = detail::tryEmplacePending(values, "new", 2);
		EXPECT_EQ(values.at("new"), 2);
	}
	EXPECT_FALSE(values.contains("new"));
	{
		auto pending = detail::tryEmplacePending(values, "existing", 99);
	}
	EXPECT_EQ(values.at("existing"), 1);
	{
		auto pending = detail::tryEmplacePending(values, "committed", 3);
		pending.commit();
	}
	EXPECT_EQ(values.at("committed"), 3);
}

struct Record
{
	std::size_t position = 0;
	std::size_t unrelatedPosition = 123;
};

TEST(IndexStorageTest, SwapPopUpdatesOnlyTheMovedRecordsSelectedPosition)
{
	detail::SlotPool<Record> records;
	std::vector<detail::SlotId> slots;
	slots.reserve(5);
	for (std::size_t number = 0; number < 5; ++number)
	{
		slots.push_back(records.emplace(Record{.position = number}));
	}
	detail::eraseSlotBySwapPop<&Record::position>(slots, records, 1);
	EXPECT_EQ(slots, (std::vector<detail::SlotId>{0, 4, 2, 3}));
	EXPECT_EQ(records[4].position, 1U);
	EXPECT_EQ(records[4].unrelatedPosition, 123U);
	detail::eraseSlotBySwapPop<&Record::position>(slots, records, 3); // Last slot.
	detail::eraseSlotBySwapPop<&Record::position>(slots, records, 0); // First slot.
	EXPECT_EQ(slots, (std::vector<detail::SlotId>{2, 4}));
	EXPECT_EQ(records[2].position, 0U);
	detail::eraseSlotBySwapPop<&Record::position>(slots, records, 4);
	detail::eraseSlotBySwapPop<&Record::position>(slots, records, 2);
	EXPECT_TRUE(slots.empty());
}
} // namespace
} // namespace order_cache::test
