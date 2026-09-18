#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace order_cache::detail
{
using SlotId = std::size_t;
inline constexpr SlotId noSlot = std::numeric_limits<SlotId>::max();

// Blocks never move or shrink. Only the block directory reallocates; live objects
// keep their addresses. Freed slots are reused before the high-water mark grows.
template <class Value, std::size_t BlockSize = 256>
	requires(BlockSize > 0) && std::destructible<Value>
class SlotPool
{
	struct Slot
	{
		std::optional<Value> value;
		// Intrusive free-list link, used only while value is disengaged.
		SlotId nextFreeSlot = noSlot;
	};

	using Block = std::array<Slot, BlockSize>;

public:
	class [[nodiscard]] PendingSlot
	{
	public:
		PendingSlot(const PendingSlot &) = delete;
		PendingSlot & operator=(const PendingSlot &) = delete;
		PendingSlot(PendingSlot &&) = delete;
		PendingSlot & operator=(PendingSlot &&) = delete;
		~PendingSlot() noexcept
		{
			if (pool_ != nullptr)
			{
				pool_->erase(slot_);
			}
		}
		[[nodiscard]] SlotId slotId() const noexcept
		{
			return slot_;
		}
		void commit() noexcept
		{
			pool_ = nullptr;
		}

	private:
		friend class SlotPool;
		PendingSlot(SlotPool & pool, SlotId slot) noexcept
			: pool_(&pool)
			, slot_(slot)
		{
		}
		SlotPool * pool_;
		SlotId slot_;
	};

	SlotPool() = default;
	SlotPool(const SlotPool &) = delete;
	SlotPool & operator=(const SlotPool &) = delete;
	SlotPool(SlotPool &&) = delete;
	SlotPool & operator=(SlotPool &&) = delete;
	~SlotPool() = default;

	void reserve(std::size_t count)
	{
		if (count == 0)
		{
			return;
		}
		const auto needed = ((count - 1) / BlockSize) + 1;
		if (needed > noSlot / BlockSize)
		{
			throw std::length_error("Slot pool capacity overflow");
		}
		blocks_.reserve(needed);
		while (blocks_.size() < needed)
		{
			blocks_.push_back(std::make_unique<Block>());
		}
	}

	template <class... Args>
		requires std::constructible_from<Value, Args...>
	SlotId emplace(Args &&... args)
	{
		if (freeHead_ != noSlot)
		{
			const auto result = freeHead_;
			auto & slot = rawSlot(result);
			// Do not consume the free-list entry if Value's constructor throws.
			slot.value.emplace(std::forward<Args>(args)...);
			freeHead_ = slot.nextFreeSlot;
			++size_;
			return result;
		}
		if (used_ == capacity())
		{
			if (used_ > noSlot - BlockSize)
			{
				throw std::length_error("Slot pool capacity overflow");
			}
			// Let vector grow geometrically; reserve(size + 1) here would turn
			// directory growth into repeated linear copies.
			blocks_.push_back(std::make_unique<Block>());
		}
		rawSlot(used_).value.emplace(std::forward<Args>(args)...);
		++size_;
		return used_++;
	}

	// Construction and rollback ownership are one operation. A failed constructor
	// does not consume a slot; an uncommitted token returns it to the free list.
	// The pool must outlive the token; do not erase/reuse its slot before commit.
	template <class... Args>
		requires std::constructible_from<Value, Args...>
	[[nodiscard]] PendingSlot emplacePending(Args &&... args)
	{
		return PendingSlot(*this, emplace(std::forward<Args>(args)...));
	}

	void erase(SlotId slotId) noexcept
	{
		assert(contains(slotId));
		auto & slot = rawSlot(slotId);
		slot.value.reset();
		slot.nextFreeSlot = freeHead_;
		freeHead_ = slotId;
		--size_;
	}

	[[nodiscard]] bool contains(SlotId slotId) const noexcept
	{
		return slotId < used_ && rawSlot(slotId).value.has_value();
	}
	Value & operator[](SlotId slotId) noexcept
	{
		assert(slotId < used_);
		auto & value = rawSlot(slotId).value;
		assert(value.has_value());
		return *value;
	}
	const Value & operator[](SlotId slotId) const noexcept
	{
		assert(slotId < used_);
		auto & value = rawSlot(slotId).value;
		assert(value.has_value());
		return *value;
	}
	[[nodiscard]] std::size_t size() const noexcept
	{
		return size_;
	}
	[[nodiscard]] std::size_t capacity() const noexcept
	{
		return blocks_.size() * BlockSize;
	}

private:
	Slot & rawSlot(SlotId slotId) noexcept
	{
		return (*blocks_[slotId / BlockSize])[slotId % BlockSize];
	}
	[[nodiscard]] const Slot & rawSlot(SlotId slotId) const noexcept
	{
		return (*blocks_[slotId / BlockSize])[slotId % BlockSize];
	}

	std::vector<std::unique_ptr<Block>> blocks_;
	std::size_t used_ = 0;
	std::size_t size_ = 0;
	SlotId freeHead_ = noSlot;
};
} // namespace order_cache::detail
