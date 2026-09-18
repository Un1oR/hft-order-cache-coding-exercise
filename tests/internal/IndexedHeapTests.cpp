#include "IndexedHeap.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <random>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace order_cache::test
{
namespace
{
struct Entry
{
	int priority;
	std::size_t slot;
	int payload = 0;
};
struct Less
{
	bool operator()(const Entry & left, const Entry & right) const noexcept
	{
		return left.priority < right.priority;
	}
};
using Heap = detail::IndexedHeap<Entry, Less>;
constexpr auto validPosition = [](const Entry &, std::size_t) noexcept {};
constexpr auto throwingPosition = [](const Entry &, std::size_t) {};
constexpr auto wrongReturnPosition = [](const Entry &, std::size_t) noexcept
{
	return 0;
};
constexpr auto wrongArgumentPosition = [](const Entry &) noexcept {};
constexpr auto keepAll = [](const Entry &) noexcept
{
	return false;
};
constexpr auto unchanged = [](Entry &) noexcept {};
constexpr auto ignoreRemoved = [](const Entry &) noexcept {};

template <class Position>
constexpr bool rejectsEveryHeapOperation()
{
	return !requires(Heap & heap, Position position, Entry value) { heap.push(value, position); }
	&& !requires(Heap & heap, Position position, Entry value) { heap.pushLeaf(value, position); }
	&& !requires(Heap & heap, Position position, Entry value) { heap.update(0, value, position); }
	&& !requires(Heap & heap, Position position) { heap.erase(0, position); }
	&& !requires(Heap & heap, Position position) { heap.eraseIf(keepAll, position, ignoreRemoved); }
	&& !requires(Heap & heap, Position position) {
		   heap.transformEraseIf(unchanged, keepAll, position, ignoreRemoved);
	   };
}

template <class Transform, class Predicate, class Removed>
concept AcceptsBulk = requires(Heap & heap, Transform transform, Predicate predicate, Removed removed) {
	heap.transformEraseIf(transform, predicate, validPosition, removed);
};

TEST(IndexedHeapContractTest, RejectsThrowingOrMismatchedCallbacks)
{
	static_assert(!rejectsEveryHeapOperation<decltype(validPosition)>());
	static_assert(rejectsEveryHeapOperation<decltype(throwingPosition)>());
	static_assert(rejectsEveryHeapOperation<decltype(wrongReturnPosition)>());
	static_assert(rejectsEveryHeapOperation<decltype(wrongArgumentPosition)>());
	constexpr auto throwingTransform = [](Entry &) {};
	constexpr auto throwingPredicate = [](const Entry &)
	{
		return false;
	};
	constexpr auto throwingRemoved = [](const Entry &) {};
	constexpr auto wrongTransform = [](Entry &) noexcept
	{
		return 1;
	};
	constexpr auto wrongPredicate = [](const Entry &) noexcept
	{
		return 1;
	};
	static_assert(AcceptsBulk<decltype(unchanged), decltype(keepAll), decltype(ignoreRemoved)>);
	static_assert(!AcceptsBulk<decltype(throwingTransform), decltype(keepAll), decltype(ignoreRemoved)>);
	static_assert(!AcceptsBulk<decltype(unchanged), decltype(throwingPredicate), decltype(ignoreRemoved)>);
	static_assert(!AcceptsBulk<decltype(unchanged), decltype(keepAll), decltype(throwingRemoved)>);
	static_assert(!AcceptsBulk<decltype(wrongTransform), decltype(keepAll), decltype(ignoreRemoved)>);
	static_assert(!AcceptsBulk<decltype(unchanged), decltype(wrongPredicate), decltype(ignoreRemoved)>);
}

TEST(IndexedHeapContractTest, ExposesOnlyReadOnlyViewsAndDoesNotDuplicateExternalPositions)
{
	static_assert(std::same_as<decltype(std::declval<Heap &>().top()), const Entry &>);
	static_assert(std::same_as<decltype(std::declval<Heap &>()[0]), const Entry &>);
	static_assert(std::same_as<decltype(std::declval<Heap &>().entries()), std::span<const Entry>>);
	static_assert(!std::is_copy_constructible_v<Heap> && !std::is_move_constructible_v<Heap>);
}

struct CountingLess
{
	std::size_t * comparisons;
	explicit CountingLess(std::size_t & counter) noexcept
		: comparisons(&counter)
	{
	}
	CountingLess(const CountingLess &) = delete;
	CountingLess & operator=(const CountingLess &) = delete;
	CountingLess(CountingLess &&) = default;
	CountingLess & operator=(CountingLess &&) = default;
	~CountingLess() = default;
	// The adapter must use the const overload checked by NothrowLess.
	bool operator()(const Entry &, const Entry &) = delete;
	bool operator()(Entry &, Entry &) const = delete;
	bool operator()(const Entry & left, const Entry & right) const noexcept
	{
		++*comparisons;
		return left.priority > right.priority; // A min-heap exercises comparator-relative repair.
	}
};

struct ConstPosition
{
	void operator()(Entry &, std::size_t) const = delete;
	void operator()([[maybe_unused]] const Entry & entry, [[maybe_unused]] std::size_t position) const noexcept
	{
	}
};
struct ConstPredicate
{
	bool operator()(Entry &) const = delete;
	bool operator()(const Entry & entry) const noexcept
	{
		return entry.priority == 30;
	}
};
struct ConstRemoved
{
	void operator()(Entry &) const = delete;
	void operator()([[maybe_unused]] const Entry & entry) const noexcept
	{
	}
};

TEST(IndexedHeapContractTest, KeepsANoncopyableComparatorAcrossUpdatesAndRebuild)
{
	std::size_t comparisons = 0;
	detail::IndexedHeap<Entry, CountingLess> heap{CountingLess(comparisons)};
	heap.push({.priority = 10, .slot = 0}, ConstPosition{});
	heap.push({.priority = 20, .slot = 1}, ConstPosition{});
	EXPECT_EQ(heap.top().priority, 10);
	heap.update(1, {.priority = 5, .slot = 1}, ConstPosition{});
	EXPECT_EQ(heap.top().priority, 5);
	heap.update(0, {.priority = 30, .slot = 1}, ConstPosition{});
	EXPECT_EQ(heap.top().priority, 10);
	EXPECT_EQ(heap.eraseIf(ConstPredicate{}, ConstPosition{}, ConstRemoved{}), 1U);
	EXPECT_EQ(heap.top().priority, 10);
	heap.pushLeaf({.priority = 40, .slot = 2}, ConstPosition{});
	heap.erase(0, ConstPosition{});
	EXPECT_EQ(heap.top().priority, 40);
	EXPECT_GT(comparisons, 0U);
}

class IndexedHeapTest : public ::testing::Test
{
protected:
	static constexpr std::size_t count = 128;
	Heap heap;
	std::array<std::size_t, count> positions{};
	std::array<std::optional<int>, count> model{};
	std::size_t positionCalls = 0;

	auto positionUpdater()
	{
		return [this](const Entry & entry, std::size_t position) noexcept
		{
			positions[entry.slot] = position;
			++positionCalls;
		};
	}
	void add(std::size_t slot, int priority)
	{
		heap.push({.priority = priority, .slot = slot}, positionUpdater());
		model[slot] = priority;
	}
	void change(std::size_t slot, int priority)
	{
		heap.update(positions[slot], {.priority = priority, .slot = slot}, positionUpdater());
		model[slot] = priority;
	}
	void remove(std::size_t slot)
	{
		heap.erase(positions[slot], positionUpdater());
		model[slot].reset();
	}
	void check() const
	{
		const auto entries = heap.entries();
		ASSERT_TRUE(std::ranges::is_heap(entries, Less{}));
		std::array<bool, count> seen{};
		for (std::size_t index = 0; index < entries.size(); ++index)
		{
			const auto & value = entries[index];
			ASSERT_LT(value.slot, count);
			EXPECT_FALSE(seen[value.slot]);
			seen[value.slot] = true;
			EXPECT_EQ(positions[value.slot], index);
			EXPECT_EQ(model[value.slot], value.priority);
		}
		for (std::size_t slot = 0; slot < count; ++slot)
		{
			EXPECT_EQ(seen[slot], model[slot].has_value());
		}
	}
};

TEST_F(IndexedHeapTest, PushAndEraseMaintainHeapAndPositionsAcrossGrowth)
{
	for (std::size_t slot = 0; slot < count; ++slot)
	{
		add(slot, static_cast<int>(slot % 17));
		check();
	}
	remove(heap.top().slot);
	remove(heap.entries().back().slot);
	remove(heap[heap.size() / 2].slot);
	check();
	while (!heap.empty())
	{
		remove(heap.top().slot);
		check();
	}
}

TEST_F(IndexedHeapTest, ArbitraryEraseCanRepairUpwards)
{
	const std::array<int, 10> priorities{100, 50, 90, 40, 45, 80, 85, 30, 35, 44};
	for (std::size_t slot = 0; slot < priorities.size(); ++slot)
	{
		add(slot, priorities[slot]);
	}
	remove(7); // Replacing priority 30 with 44 must move above its parent 40.
	check();
	EXPECT_EQ(heap[positions[9]].priority, 44);
}

TEST_F(IndexedHeapTest, UpdateChoosesUpDownOrNoRepair)
{
	add(0, 10);
	add(1, 5);
	add(2, 3);
	change(2, 20);
	EXPECT_EQ(heap.top().slot, 2U);
	check();
	change(2, 1);
	EXPECT_EQ(heap.top().slot, 0U);
	check();
	const auto calls = positionCalls;
	heap.update(positions[1], {.priority = 5, .slot = 1, .payload = 99}, positionUpdater());
	EXPECT_EQ(positionCalls, calls); // Equal priority changes only payload.
	EXPECT_EQ(heap[positions[1]].payload, 99);
	check();
}

TEST_F(IndexedHeapTest, KnownLeafAppendAndRollbackNeverReorderThePrefix)
{
	add(0, 10);
	add(1, 0);
	const auto before = positions;
	heap.pushLeaf({.priority = 0, .slot = 2}, positionUpdater());
	model[2] = 0;
	EXPECT_EQ(positions[2], 2U);
	EXPECT_EQ(positions[0], before[0]);
	EXPECT_EQ(positions[1], before[1]);
	check();
	heap.popLeaf();
	model[2].reset();
	check();
}

TEST_F(IndexedHeapTest, BulkTransformAndFilterRestoreAllSurvivorPositionsBeforeReturning)
{
	for (std::size_t slot = 0; slot < count; ++slot)
	{
		add(slot, static_cast<int>(slot));
	}
	std::array<unsigned int, count> transformed{};
	std::array<unsigned int, count> removed{};
	const auto erased = heap.transformEraseIf(
		[&](Entry & entry) noexcept
		{
			++transformed[entry.slot];
			entry.priority = -entry.priority;
			model[entry.slot] = entry.priority;
		},
		[](const Entry & entry) noexcept
		{
			return entry.priority % 3 == 0;
		},
		positionUpdater(),
		[&](const Entry & entry) noexcept
		{
			++removed[entry.slot];
			model[entry.slot].reset();
		});
	EXPECT_EQ(erased, 43U);
	check();
	for (std::size_t slot = 0; slot < count; ++slot)
	{
		EXPECT_EQ(transformed[slot], 1U);
		EXPECT_EQ(removed[slot], slot % 3 == 0 ? 1U : 0U);
	}
	EXPECT_EQ(heap.top().slot, 1U);
	change(127, 200);
	EXPECT_EQ(heap.top().slot, 127U);
	remove(1);
	check();
}

TEST_F(IndexedHeapTest, EraseIfHandlesEmptyNoneAllAndCanBeReused)
{
	EXPECT_EQ(heap.eraseIf(keepAll, positionUpdater()), 0U);
	add(0, 0);
	add(1, 0);
	EXPECT_EQ(heap.eraseIf(keepAll, positionUpdater()), 0U);
	check();
	EXPECT_EQ(heap.eraseIf(
				  [](const Entry &) noexcept
				  {
					  return true;
				  },
				  positionUpdater()),
			  2U);
	model = {};
	check();
	add(0, 2);
	check();
}

TEST_F(IndexedHeapTest, RandomEditsAgreeWithAnIndependentModel)
{
	// NOLINTNEXTLINE(bugprone-random-generator-seed)
	std::mt19937 random(4817);
	for (std::size_t step = 0; step < 4000; ++step)
	{
		const auto slot = random() % count;
		if (!model[slot].has_value())
		{
			add(slot, static_cast<int>(random() % 100));
		}
		else if (step % 3 == 0)
		{
			remove(slot);
		}
		else
		{
			change(slot, static_cast<int>(random() % 100));
		}
		check();
	}
}
} // namespace
} // namespace order_cache::test
