#include "QuantityIndex.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <random>
#include <ranges>
#include <utility>
#include <vector>

namespace order_cache::test
{
namespace
{
class QuantityIndexTest : public ::testing::Test
{
protected:
	static constexpr std::size_t slotCount = 2048;
	std::array<detail::QuantityIndexRecord, slotCount> records{};
	struct RecordBySlotId
	{
		std::array<detail::QuantityIndexRecord, slotCount> * records = nullptr;
		detail::QuantityIndexRecord & operator()(detail::OrderStorageSlotId slot) const noexcept
		{
			return (*records)[slot];
		}
	};
	detail::QuantityIndex<RecordBySlotId> index{RecordBySlotId{&records}};
	std::array<std::optional<unsigned int>, slotCount> quantities{};

	void add(detail::SlotId slot, unsigned int quantity)
	{
		ASSERT_FALSE(quantities[slot].has_value());
		index.insert({.quantity = quantity, .slot = slot});
		quantities[slot] = quantity;
	}
	void erase(detail::SlotId slot)
	{
		ASSERT_TRUE(quantities[slot].has_value());
		index.erase(slot);
		quantities[slot].reset();
	}
	void cancel(unsigned int minimum)
	{
		const auto previous = quantities;
		std::array<unsigned int, slotCount> removed{};
		const auto detached = index.cancelAtLeast(minimum);
		for (const auto slot : detached)
		{
			++removed[slot];
			quantities[slot].reset();
		}
		std::size_t survivors = 0;
		for (std::size_t slot = 0; slot < slotCount; ++slot)
		{
			const auto & previousQuantity = previous[slot];
			const bool expectedRemoval = previousQuantity.has_value() && *previousQuantity >= minimum;
			ASSERT_EQ(removed[slot], expectedRemoval ? 1U : 0U) << "slot=" << slot;
			if (!expectedRemoval)
			{
				ASSERT_EQ(quantities[slot], previous[slot]);
			}
			survivors += quantities[slot].has_value() ? 1U : 0U;
		}
		ASSERT_EQ(index.size(), survivors);
	}
};

TEST_F(QuantityIndexTest, ThresholdDoesNotAssumeAContiguousHeapPrefix)
{
	const std::array<unsigned int, 7> initialQuantities{100, 90, 40, 80, 70, 30, 20};
	for (std::size_t slot = 0; slot < initialQuantities.size(); ++slot)
	{
		add(slot, initialQuantities[slot]);
	}
	cancel(60);
	erase(2);
	erase(6);
	erase(5);
	EXPECT_TRUE(index.empty());
}

TEST_F(QuantityIndexTest, SparseAndDenseRemovalRepairEverySurvivorPosition)
{
	for (std::size_t slot = 0; slot < 1024; ++slot)
	{
		add(slot, static_cast<unsigned int>(slot));
	}
	cancel(2048); // Empty result.
	cancel(1023); // One root removal, not a full rebuild.
	cancel(500); // Dense batch, including non-prefix heap entries.
	for (std::size_t slot = 0; slot < 500; slot += 3)
	{
		erase(slot);
		add(slot, 900);
	}
	cancel(600);
	cancel(0);
	EXPECT_TRUE(index.empty());
}

TEST_F(QuantityIndexTest, SelectedBatchesReadPositionsAfterEarlierElementsMove)
{
	for (std::size_t slot = 0; slot < 1000; ++slot)
	{
		add(slot, static_cast<unsigned int>(slot % 17));
	}
	for (const auto stride : {251U, 2U})
	{
		std::vector<detail::SlotId> selected;
		for (std::size_t slot = 0; slot < 1000; slot += stride)
		{
			if (quantities[slot].has_value())
			{
				selected.push_back(slot);
			}
		}
		const auto previousSize = index.size();
		auto detached = index.eraseOrders(std::views::all(selected),
										  [stride](const detail::QuantityEntry & entry) noexcept
										  {
											  return entry.slot % stride == 0;
										  });
		// Heap repair finishes before the caller releases any selected records.
		EXPECT_EQ(index.size(), previousSize - selected.size());
		EXPECT_EQ(detached.size(), selected.size());
		EXPECT_TRUE(std::ranges::equal(detached, selected));
		std::size_t removed = 0;
		for (const auto slot : detached)
		{
			ASSERT_TRUE(quantities[slot].has_value());
			quantities[slot].reset();
			++removed;
		}
		EXPECT_EQ(removed, selected.size());
	}
	for (std::size_t slot = 0; slot < 1000; ++slot)
	{
		if (quantities[slot].has_value())
		{
			erase(slot);
		}
	}
	EXPECT_TRUE(index.empty());
}

TEST_F(QuantityIndexTest, EmptySelectionLeavesTheIndexUntouched)
{
	add(0, 42);
	const auto detached = index.eraseOrders(std::views::empty<detail::OrderStorageSlotId>,
											[](const detail::QuantityEntry &) noexcept
											{
												return false;
											});
	EXPECT_TRUE(detached.empty());
	EXPECT_EQ(detached.size(), 0U);
	EXPECT_EQ(index.size(), 1U);
	erase(0);
	EXPECT_TRUE(index.empty());
}

TEST_F(QuantityIndexTest, RandomPointAndThresholdEditsMatchAnUnorderedModel)
{
	// A fixed seed makes failing model histories reproducible.
	// NOLINTNEXTLINE(bugprone-random-generator-seed)
	std::mt19937 random(2026);
	for (std::size_t step = 0; step < 4000; ++step)
	{
		const auto slot = random() % slotCount;
		if (step % 79 == 0)
		{
			cancel(static_cast<unsigned int>(random() % 200));
		}
		else if (quantities[slot].has_value())
		{
			erase(slot);
		}
		else
		{
			add(slot, static_cast<unsigned int>(random() % 200));
		}
	}
	cancel(0);
}
} // namespace
} // namespace order_cache::test
