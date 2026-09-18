#pragma once

#include "ScopeRollback.h"
#include "SlotPool.h"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace order_cache::detail
{
struct StringHash
{
	using is_transparent = void;
	std::size_t operator()(std::string_view text) const noexcept
	{
		return std::hash<std::string_view>{}(text);
	}
};

// Append and acquire rollback together; there is no separate reservation to
// match to a later push. Commit before modifying this vector again. Rollback
// pops exactly this element; capacity growth is retained. No extra allocation.
template <class Value>
	requires std::is_nothrow_move_constructible_v<Value> && std::destructible<Value>
[[nodiscard]] auto appendPending(std::vector<Value> & values, Value value)
{
	[[maybe_unused]] const auto previousSize = values.size();
	values.push_back(std::move(value));
	return ScopeRollback(
		[=, &values]() noexcept
		{
			assert(values.size() == previousSize + 1);
			values.pop_back();
		});
}

// Existing keys are not owned by this token. Until commit, do not invalidate
// the inserted iterator by rehashing the map or removing its entry.
template <class Map, class... Args>
[[nodiscard]] auto tryEmplacePending(Map & values, Args &&... args)
{
	auto [found, inserted] = values.try_emplace(std::forward<Args>(args)...);
	return ScopeRollback(
		[&values, found]() noexcept
		{
			values.erase(found);
		},
		inserted);
}

// O(1) unordered removal. The same member locates the erased record and receives
// the moved record's position, so the two reverse indexes cannot be mixed up.
template <auto PositionMember, class Pool>
	requires requires(Pool & records, SlotId slot) {
		{ records[slot].*PositionMember } noexcept -> std::same_as<std::size_t &>;
	}
void eraseSlotBySwapPop(std::vector<SlotId> & slots, Pool & records, SlotId erasedSlot) noexcept
{
	const auto index = records[erasedSlot].*PositionMember;
	assert(index < slots.size() && slots[index] == erasedSlot);
	if (index != slots.size() - 1)
	{
		slots[index] = slots.back();
		records[slots[index]].*PositionMember = index;
	}
	slots.pop_back();
}
} // namespace order_cache::detail
