#pragma once

#include "IndexedHeap.h"
#include "OrderRemovalRange.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <ranges>
#include <tuple>
#include <utility>

namespace order_cache::detail
{
struct QuantityEntry
{
	unsigned int quantity;
	OrderStorageSlotId slot;

	bool operator<(const QuantityEntry & other) const noexcept
	{
		return std::tie(quantity, slot) < std::tie(other.quantity, other.slot);
	}
	bool operator==(const QuantityEntry &) const = default;
};

template <class Selected>
concept QuantitySelected = requires(const Selected & selected, const QuantityEntry & entry) {
	{ selected(entry) } noexcept -> std::same_as<bool>;
};

// One indexed max-heap. A typed, borrowed resolver connects slot IDs to just
// the record's position/link metadata. Bind it once; no type erasure or virtual
// dispatch. The record store must outlive this index and its removal ranges.
template <QuantityRecordLookup RecordBySlotId>
class QuantityIndex
{
public:
	explicit QuantityIndex(RecordBySlotId recordBySlotId) noexcept
		: recordBySlotId_(std::move(recordBySlotId))
	{
	}

	[[nodiscard]] std::size_t size() const noexcept
	{
		return heap_.size();
	}
	[[nodiscard]] bool empty() const noexcept
	{
		return heap_.empty();
	}

	// O(log n), plus O(n) relocation on growth. push_back is the only throwing
	// step; position updates and heap repair start only after it succeeds.
	void insert(QuantityEntry entry)
	{
		heap_.push(entry, positionUpdater());
	}

	// O(log n); resolve the current position rather than accepting a stale handle.
	void erase(OrderStorageSlotId slot) noexcept
	{
		const auto position = recordBySlotId_(slot).quantityIndexPosition;
		assert(heap_[position].slot == slot);
		heap_.erase(position, positionUpdater());
	}

	// O(1 + min(k log(n + 1), n)) index work, then O(k) caller-owned cleanup.
	// Collect removed identities in the records' existing intrusive links. The
	// returned range sees a fully repaired heap and does not allocate or own nodes.
	[[nodiscard]] auto cancelAtLeast(unsigned int minimum) noexcept
	{
		OrderStorageSlotId head = noSlot;
		std::size_t count = 0;
		const auto collect = [&](const QuantityEntry & entry) noexcept
		{
			recordBySlotId_(entry.slot).nextOrderInRemovalBatch = head;
			head = entry.slot;
			++count;
		};
		if (!heap_.empty() && heap_.top().quantity >= minimum)
		{
			if (largeThresholdBatch(minimum))
			{
				heap_.eraseIf(
					[minimum](const QuantityEntry & entry) noexcept
					{
						return entry.quantity >= minimum;
					},
					positionUpdater(),
					collect);
			}
			else
			{
				while (!heap_.empty() && heap_.top().quantity >= minimum)
				{
					const auto entry = heap_.top();
					heap_.erase(0, positionUpdater());
					collect(entry);
				}
			}
		}
		return OrderRemovalRange(recordBySlotId_, head, count);
	}

	// O(min(k log(n + 1), n)) index work, then O(k) caller-owned cleanup.
	// The independent range contains each selected slot exactly once, matching
	// the predicate. Iteration must not throw; the resolver reads current reverse
	// positions after earlier erases. Consume the returned view before reusing
	// records or changing its links. Heap mutations are eager, not in the view.
	template <std::ranges::view Orders, QuantitySelected Selected>
		requires std::ranges::forward_range<Orders> && std::ranges::sized_range<Orders>
		&& std::same_as<std::ranges::range_value_t<Orders>, OrderStorageSlotId>
		&& std::is_nothrow_move_constructible_v<Orders>
	[[nodiscard]] Orders eraseOrders(Orders orders, const Selected & selected) noexcept
	{
		if (std::ranges::size(orders) > rebuildLimit())
		{
			heap_.eraseIf(selected, positionUpdater());
		}
		else
		{
			for (const auto slot : orders)
			{
				erase(slot);
			}
		}
		return orders;
	}

private:
	[[nodiscard]] std::size_t rebuildLimit() const noexcept
	{
		const auto levels = static_cast<std::size_t>(std::bit_width(size()));
		return size() / std::max(std::size_t{1}, levels);
	}

	[[nodiscard]] bool largeThresholdBatch(unsigned int minimum) const noexcept
	{
		const auto values = heap_.entries();
		// A depth-first heap traversal needs at most one pending sibling per
		// level: size_t's bit width is a hard bound, not a guessed small size.
		std::array<std::size_t, std::numeric_limits<std::size_t>::digits> pending{};
		std::size_t depth = 1;
		std::size_t count = 0;
		const auto limit = rebuildLimit();
		while (depth != 0)
		{
			const auto index = pending[--depth];
			if (values[index].quantity < minimum)
			{
				continue; // A rejected parent prunes its entire subtree.
			}
			if (++count > limit)
			{
				return true;
			}
			if (index < values.size() / 2)
			{
				const auto left = (index * 2) + 1;
				if (left + 1 < values.size())
				{
					assert(depth < pending.size());
					pending[depth++] = left + 1;
				}
				assert(depth < pending.size());
				pending[depth++] = left;
			}
		}
		return false;
	}

	auto positionUpdater() const noexcept
	{
		return [this](const QuantityEntry & entry, std::size_t position) noexcept
		{
			recordBySlotId_(entry.slot).quantityIndexPosition = position;
		};
	}

	[[no_unique_address]] const RecordBySlotId recordBySlotId_;
	IndexedHeap<QuantityEntry> heap_;
};
} // namespace order_cache::detail
