#pragma once

#include "SlotPool.h"

#include <concepts>
#include <cstddef>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

namespace order_cache::detail
{
using OrderStorageSlotId = SlotId;

// Only the quantity index's bookkeeping is exposed to its record resolver.
// The owning cache record adds Order, user/security/company pointers and other
// reverse indexes; no virtual inheritance or allocation is involved.
struct QuantityIndexRecord
{
	std::size_t quantityIndexPosition = 0;

	// Intrusive membership in one removal batch, never ownership. Also used to
	// collect threshold removals without allocating a second array of slot IDs.
	OrderStorageSlotId nextOrderInRemovalBatch = noSlot;
};

template <class Lookup>
concept QuantityRecordLookup = std::copyable<Lookup> && std::is_nothrow_copy_constructible_v<Lookup>
	&& std::is_nothrow_move_constructible_v<Lookup> && requires(const Lookup & lookup, OrderStorageSlotId slot) {
		   { lookup(slot) } noexcept -> std::same_as<QuantityIndexRecord &>;
	   };

// Non-owning, O(1) sized view of the order-record links. Before cleanup, copies
// can restart. While consuming it, the caller may destroy the current record:
// each iterator caches its successor before handing the current slot out.
// The resolver/storage outlive the view. Consume a removal result before reusing
// any of its slots or rewriting its links. This is not a lazy heap mutation.
template <QuantityRecordLookup Lookup>
class OrderRemovalRange : public std::ranges::view_interface<OrderRemovalRange<Lookup>>
{
	class Iterator
	{
	public:
		using value_type = OrderStorageSlotId;
		using difference_type = std::ptrdiff_t;
		using iterator_concept = std::forward_iterator_tag;

		Iterator() = default;
		Iterator(const Lookup & lookup, OrderStorageSlotId slot) noexcept
			: lookup_(&lookup)
			, slot_(slot)
			, next_(nextSlot(slot))
		{
		}

		value_type operator*() const noexcept
		{
			return slot_;
		}
		Iterator & operator++() noexcept
		{
			slot_ = next_;
			next_ = nextSlot(slot_);
			return *this;
		}
		Iterator operator++(int) noexcept
		{
			const auto previous = *this;
			++*this;
			return previous;
		}
		bool operator==(const Iterator &) const = default;
		bool operator==([[maybe_unused]] std::default_sentinel_t sentinel) const noexcept
		{
			return slot_ == noSlot;
		}

	private:
		OrderStorageSlotId nextSlot(OrderStorageSlotId slot) const noexcept
		{
			return slot == noSlot ? noSlot : (*lookup_)(slot).nextOrderInRemovalBatch;
		}
		const Lookup * lookup_ = nullptr;
		OrderStorageSlotId slot_ = noSlot;
		OrderStorageSlotId next_ = noSlot;
	};

public:
	OrderRemovalRange() = default;
	OrderRemovalRange(Lookup lookup, OrderStorageSlotId head, std::size_t count) noexcept
		: lookup_(std::move(lookup))
		, head_(head)
		, count_(count)
	{
	}

	[[nodiscard]] auto begin() const noexcept
	{
		return Iterator(lookup_, head_);
	}
	[[nodiscard]] auto end() const noexcept
	{
		return std::default_sentinel;
	}
	[[nodiscard]] std::size_t size() const noexcept
	{
		return count_;
	}

private:
	[[no_unique_address]] Lookup lookup_{};
	OrderStorageSlotId head_ = noSlot;
	std::size_t count_ = 0;
};
} // namespace order_cache::detail
