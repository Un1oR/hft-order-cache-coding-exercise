#include "SlotPool.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace order_cache::test
{
namespace
{
struct Lifetimes
{
	int created = 0;
	int destroyed = 0;
};

struct alignas(64) Immovable
{
	Lifetimes & lifetimes;
	int value;

	Immovable(Lifetimes & counts, int number, bool fail = false)
		: lifetimes(counts)
		, value(number)
	{
		if (fail)
		{
			throw std::runtime_error("Requested construction failure");
		}
		++lifetimes.created;
	}
	Immovable(const Immovable &) = delete;
	Immovable & operator=(const Immovable &) = delete;
	Immovable(Immovable &&) = delete;
	Immovable & operator=(Immovable &&) = delete;
	~Immovable()
	{
		++lifetimes.destroyed;
	}
};

TEST(SlotPoolTest, ReusesFreedSlotsBeforeGrowing)
{
	detail::SlotPool<int, 2> pool;
	const auto first = pool.emplace(10);
	const auto second = pool.emplace(20);
	EXPECT_EQ(pool.capacity(), 2U);
	pool.erase(first);
	EXPECT_FALSE(pool.contains(first));
	EXPECT_EQ(pool.emplace(30), first);
	EXPECT_EQ(pool[first], 30);
	EXPECT_EQ(pool[second], 20);
	EXPECT_EQ(pool.capacity(), 2U);
	EXPECT_EQ(pool.size(), 2U);
	pool.erase(first);
	pool.erase(second);
	EXPECT_EQ(pool.emplace(40), second);
	EXPECT_EQ(pool.emplace(50), first);
}

TEST(SlotPoolTest, GrowthAndReservationPreserveImmovableObjectAddresses)
{
	Lifetimes lifetimes;
	detail::SlotPool<Immovable, 3> pool;
	const auto first = pool.emplace(lifetimes, 42);
	const auto * address = &pool[first];
	for (int value = 0; value < 100; ++value)
	{
		pool.emplace(lifetimes, value);
		ASSERT_EQ(&pool[first], address);
	}
	pool.reserve(1000);
	EXPECT_GE(pool.capacity(), 1000U);
	EXPECT_EQ(&pool[first], address);
	EXPECT_EQ(pool[first].value, 42);
	EXPECT_EQ(reinterpret_cast<std::uintptr_t>(address) % alignof(Immovable), 0U);
	const auto & view = pool;
	EXPECT_EQ(&view[first], address);
}

TEST(SlotPoolTest, DestroysEachConstructedObjectExactlyOnce)
{
	Lifetimes lifetimes;
	{
		detail::SlotPool<Immovable, 2> pool;
		const auto first = pool.emplace(lifetimes, 1);
		pool.emplace(lifetimes, 2);
		pool.erase(first);
		EXPECT_EQ(lifetimes.destroyed, 1);
		pool.emplace(lifetimes, 3);
		pool.emplace(lifetimes, 4);
		EXPECT_EQ(lifetimes.created, 4);
	}
	EXPECT_EQ(lifetimes.created, lifetimes.destroyed);
}

TEST(SlotPoolTest, ConstructorFailurePreservesUnusedAndFreedSlots)
{
	Lifetimes lifetimes;
	detail::SlotPool<Immovable, 2> pool;
	EXPECT_THROW(pool.emplace(lifetimes, 1, true), std::runtime_error);
	EXPECT_EQ(pool.size(), 0U);
	const auto first = pool.emplace(lifetimes, 2);
	EXPECT_EQ(first, 0U);
	pool.erase(first);
	EXPECT_THROW(pool.emplace(lifetimes, 3, true), std::runtime_error);
	EXPECT_EQ(pool.size(), 0U);
	EXPECT_FALSE(pool.contains(first));
	EXPECT_EQ(pool.emplace(lifetimes, 4), first);
	EXPECT_EQ(pool[first].value, 4);
}

TEST(SlotPoolTest, RejectsUnrepresentableReservationWithoutLosingObjects)
{
	detail::SlotPool<int, 2> pool;
	const auto slot = pool.emplace(7);
	EXPECT_THROW(pool.reserve(detail::noSlot), std::length_error);
	EXPECT_EQ(pool[slot], 7);
	EXPECT_EQ(pool.size(), 1U);
	EXPECT_FALSE(pool.contains(detail::noSlot));
}

TEST(SlotPoolTest, RandomReuseNeverAliasesLiveObjects)
{
	detail::SlotPool<unsigned int, 7> pool;
	std::vector<std::pair<detail::SlotId, unsigned int>> live;
	// A fixed seed makes failing model histories reproducible.
	// NOLINTNEXTLINE(bugprone-random-generator-seed)
	std::mt19937 random(2026);
	for (unsigned int step = 0; step < 5000; ++step)
	{
		if (live.empty() || random() % 3 != 0)
		{
			const auto slot = pool.emplace(step);
			for (const auto & [other, value] : live)
			{
				ASSERT_NE(slot, other);
				ASSERT_EQ(pool[other], value);
			}
			live.emplace_back(slot, step);
		}
		else
		{
			const auto index = random() % live.size();
			pool.erase(live[index].first);
			live[index] = live.back();
			live.pop_back();
		}
		ASSERT_EQ(pool.size(), live.size());
	}
}
} // namespace
} // namespace order_cache::test

namespace order_cache::test
{
namespace
{
struct ThrowingDestructor
{
	~ThrowingDestructor() noexcept(false);
};

template <class Value, std::size_t BlockSize>
concept ValidSlotPool = requires { typename detail::SlotPool<Value, BlockSize>; };
static_assert(ValidSlotPool<int, 1>);
static_assert(!ValidSlotPool<int, 0>);
static_assert(!ValidSlotPool<ThrowingDestructor, 1>);

template <class Pool, class Arg>
concept CanEmplace = requires(Pool & pool, Arg && arg) { pool.emplace(std::forward<Arg>(arg)); };
template <class Pool, class Arg>
concept CanEmplacePending = requires(Pool & pool, Arg && arg) { pool.emplacePending(std::forward<Arg>(arg)); };
static_assert(CanEmplace<detail::SlotPool<int>, int>);
static_assert(CanEmplacePending<detail::SlotPool<int>, int>);
static_assert(!CanEmplace<detail::SlotPool<int>, Lifetimes>);
static_assert(!CanEmplacePending<detail::SlotPool<int>, Lifetimes>);
static_assert(!std::copy_constructible<detail::SlotPool<int>::PendingSlot>);
static_assert(!std::move_constructible<detail::SlotPool<int>::PendingSlot>);

TEST(SlotPoolTest, PendingSlotReturnsAnUncommittedObjectToTheFreeList)
{
	detail::SlotPool<int, 2> pool;
	detail::SlotId rejected = detail::noSlot;
	{
		auto pending = pool.emplacePending(42);
		rejected = pending.slotId();
		EXPECT_EQ(pool[rejected], 42);
		EXPECT_EQ(pool.size(), 1U);
	}
	EXPECT_FALSE(pool.contains(rejected));
	EXPECT_EQ(pool.size(), 0U);
	{
		auto pending = pool.emplacePending(7);
		EXPECT_EQ(pending.slotId(), rejected);
		pending.commit();
	}
	EXPECT_EQ(pool[rejected], 7);
	EXPECT_EQ(pool.size(), 1U);
}
} // namespace
} // namespace order_cache::test
