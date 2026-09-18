#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace order_cache::detail
{
// These requirements check exception specifications, not callback bodies.
// Throwing despite noexcept still terminates.
template <class Position, class Value>
concept NothrowPosition = requires(const Position & position, const Value & value, std::size_t index) {
	{ position(value, index) } noexcept -> std::same_as<void>;
};
template <class Less, class Value>
concept NothrowLess = requires(const Less & less, const Value & left, const Value & right) {
	{ less(left, right) } noexcept -> std::same_as<bool>;
};
template <class Transform, class Value>
concept HeapTransform = requires(const Transform & transform, Value & value) {
	{ transform(value) } noexcept -> std::same_as<void>;
};
template <class Predicate, class Value>
concept HeapPredicate = requires(const Predicate & predicate, const Value & value) {
	{ predicate(value) } noexcept -> std::same_as<bool>;
};
template <class Removed, class Value>
concept HeapRemoved = requires(const Removed & removed, const Value & value) {
	{ removed(value) } noexcept -> std::same_as<void>;
};

// A vector-backed adapter for small index entries, not owning domain objects.
// Ordering and reverse positions are valid at every public operation boundary.
// Only const views escape; update/filter own the complete mutation and repair.
// Callbacks must not re-enter this heap, keep references to entries, or change
// the identity used by the reverse-position map. Less must remain a strict weak
// ordering. During bulk work positions are transient until the final rebuild.
//
// The caller supplies its position writer per mutation: no type erasure or
// additional owner pointer per heap. All writers must target the same index.
// Copy/move are disabled: two heaps must not share one reverse-position map.
template <class Value, class Less = std::less<>>
	requires NothrowLess<Less, Value> && std::is_nothrow_copy_constructible_v<Value>
	&& std::is_nothrow_copy_assignable_v<Value> && std::is_nothrow_move_constructible_v<Value>
	&& std::is_nothrow_move_assignable_v<Value> && std::is_nothrow_destructible_v<Value>
	&& std::is_nothrow_move_constructible_v<Less> && (!std::same_as<Value, bool>)
class IndexedHeap
{
	struct IgnoreRemoval
	{
		void operator()(const Value &) const noexcept
		{
		}
	};

public:
	IndexedHeap() = default;
	explicit IndexedHeap(Less less) noexcept
		: less_(std::move(less))
	{
	}
	IndexedHeap(const IndexedHeap &) = delete;
	IndexedHeap & operator=(const IndexedHeap &) = delete;
	IndexedHeap(IndexedHeap &&) = delete;
	IndexedHeap & operator=(IndexedHeap &&) = delete;
	~IndexedHeap() = default;

	[[nodiscard]] std::size_t size() const noexcept
	{
		return values_.size();
	}
	[[nodiscard]] bool empty() const noexcept
	{
		return values_.empty();
	}
	[[nodiscard]] const Value & top() const noexcept
	{
		assert(!empty());
		return values_.front();
	}
	[[nodiscard]] const Value & operator[](std::size_t index) const noexcept
	{
		assert(index < size());
		return values_[index];
	}
	// Read-only heap-array order for pruned traversals, not sorted order.
	// Mutations can invalidate the view/references and change numeric positions.
	[[nodiscard]] std::span<const Value> entries() const noexcept
	{
		return values_;
	}

	// O(log n), plus O(n) on vector growth. Failed allocation changes neither
	// entries nor external positions; all repair happens after push_back succeeds.
	template <NothrowPosition<Value> Position>
	void push(Value value, const Position & position)
	{
		values_.push_back(std::move(value));
		position(std::as_const(values_.back()), size() - 1);
		siftUp(size() - 1, position);
	}

	// O(1), excluding growth. Precondition: value cannot outrank its new parent.
	// Useful for a zero-total company. Only the new edge needs a debug check.
	template <NothrowPosition<Value> Position>
	void pushLeaf(Value value, const Position & position)
	{
		assert(empty() || !compare(values_[(size() - 1) / 2], value));
		values_.push_back(std::move(value));
		position(std::as_const(values_.back()), size() - 1);
	}
	// O(1) rollback of the last leaf. Its external handle belongs to the caller;
	// no surviving entry moves and no comparator/position callback is needed.
	void popLeaf() noexcept
	{
		assert(!empty());
		values_.pop_back();
	}

	// Replace the same logical entry and repair in the comparator's direction.
	// O(log n) for a changed priority, O(1) with no position writes for an equal
	// priority. No caller-side "mutate, then remember to sift" protocol.
	template <NothrowPosition<Value> Position>
	void update(std::size_t index, Value value, const Position & position) noexcept
	{
		assert(index < size());
		const bool raised = compare(values_[index], value);
		const bool lowered = !raised && compare(value, values_[index]);
		values_[index] = std::move(value);
		if (raised)
		{
			siftUp(index, position);
		}
		else if (lowered)
		{
			siftDown(index, position);
		}
	}

	// O(log n) arbitrary removal; erasing the last leaf costs O(1).
	// The caller retires the erased identity; only survivor positions are written.
	template <NothrowPosition<Value> Position>
	void erase(std::size_t index, const Position & position) noexcept
	{
		assert(index < size());
		if (index != size() - 1)
		{
			values_[index] = std::move(values_.back());
			position(std::as_const(values_[index]), index);
		}
		values_.pop_back();
		if (index < size())
		{
			repair(index, position);
		}
	}

	// O(n): transform each entry, remove matching transformed entries, then
	// heapify survivors and publish every reverse position once. No allocation.
	// A removal callback may reclaim that entry's domain object; no subsequent
	// callback or entry destructor may dereference the reclaimed object.
	template <HeapTransform<Value> Transform,
			  HeapPredicate<Value> Predicate,
			  NothrowPosition<Value> Position,
			  HeapRemoved<Value> Removed = IgnoreRemoval>
	std::size_t transformEraseIf(const Transform & transform,
								 const Predicate & predicate,
								 const Position & position,
								 const Removed & removed = {}) noexcept
	{
		const auto previousSize = size();
		std::size_t kept = 0;
		for (auto value : values_)
		{
			transform(value);
			if (predicate(std::as_const(value)))
			{
				removed(std::as_const(value));
			}
			else
			{
				values_[kept++] = std::move(value);
			}
		}
		// Removing a suffix does not shift survivors or require default construction.
		values_.erase(values_.begin() + static_cast<std::ptrdiff_t>(kept), values_.end());
		rebuild(position);
		return previousSize - kept;
	}

	template <HeapPredicate<Value> Predicate,
			  NothrowPosition<Value> Position,
			  HeapRemoved<Value> Removed = IgnoreRemoval>
	std::size_t eraseIf(const Predicate & predicate, const Position & position, const Removed & removed = {}) noexcept
	{
		return transformEraseIf([](Value &) noexcept {}, predicate, position, removed);
	}

private:
	// Match the const arguments checked by NothrowLess, including overload sets.
	bool compare(const Value & left, const Value & right) const noexcept
	{
		return less_(left, right);
	}
	template <class Position>
	void swapEntries(std::size_t left, std::size_t right, const Position & position) noexcept
	{
		std::swap(values_[left], values_[right]);
		position(std::as_const(values_[left]), left);
		position(std::as_const(values_[right]), right);
	}
	template <class Position>
	void siftUp(std::size_t index, const Position & position) noexcept
	{
		while (index > 0)
		{
			const auto parent = (index - 1) / 2;
			if (!compare(values_[parent], values_[index]))
			{
				break;
			}
			swapEntries(parent, index, position);
			index = parent;
		}
	}
	template <class Position>
	void siftDown(std::size_t index, const Position & position) noexcept
	{
		while (index < size() / 2)
		{
			auto child = (index * 2) + 1;
			if (child + 1 < size() && compare(values_[child], values_[child + 1]))
			{
				++child;
			}
			if (!compare(values_[index], values_[child]))
			{
				break;
			}
			swapEntries(index, child, position);
			index = child;
		}
	}
	template <class Position>
	void repair(std::size_t index, const Position & position) noexcept
	{
		if (index > 0 && compare(values_[(index - 1) / 2], values_[index]))
		{
			siftUp(index, position);
		}
		else
		{
			siftDown(index, position);
		}
	}
	template <class Position>
	void rebuild(const Position & position) noexcept
	{
		// The wrapper keeps Less un-copied and selects the checked const overload.
		std::make_heap(values_.begin(),
					   values_.end(),
					   [this](const Value & left, const Value & right) noexcept
					   {
						   return compare(left, right);
					   });
		for (std::size_t index = 0; index < size(); ++index)
		{
			position(std::as_const(values_[index]), index);
		}
	}

	[[no_unique_address]] const Less less_{};
	std::vector<Value> values_;
};
} // namespace order_cache::detail
