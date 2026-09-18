#pragma once

#include <type_traits>
#include <utility>

namespace order_cache::detail
{
// Stack-only C++20 rollback. The cleanup must be safe during stack unwinding.
// Non-movability prevents duplicated ownership; returned prvalues use guaranteed
// copy elision. commit() cancels rollback on both normal and exceptional exits.
template <class Cleanup>
	requires std::is_nothrow_invocable_v<const Cleanup &> && std::is_nothrow_move_constructible_v<Cleanup>
class ScopeRollback
{
public:
	explicit ScopeRollback(Cleanup cleanup, bool active = true) noexcept
		: cleanup_(std::move(cleanup))
		, active_(active)
	{
	}
	ScopeRollback(const ScopeRollback &) = delete;
	ScopeRollback & operator=(const ScopeRollback &) = delete;
	ScopeRollback(ScopeRollback &&) = delete;
	ScopeRollback & operator=(ScopeRollback &&) = delete;
	~ScopeRollback() noexcept
	{
		if (active_)
		{
			std::as_const(cleanup_)();
		}
	}
	void commit() noexcept
	{
		active_ = false;
	}

private:
	[[no_unique_address]] Cleanup cleanup_;
	bool active_;
};

// getOrCreate returns a reference together with responsibility for a new entry.
// Existing entries never acquire rollback ownership. Commit after the caller's
// non-throwing updates; the owner and referenced entry must outlive this token.
template <class Value, class Cleanup>
class [[nodiscard]] PendingCreation
{
public:
	PendingCreation(Value & value, Cleanup cleanup, bool created) noexcept
		: value_(value)
		, rollback_(std::move(cleanup), created)
	{
	}
	Value & get() const noexcept
	{
		return value_;
	}
	void commit() noexcept
	{
		rollback_.commit();
	}

private:
	Value & value_;
	ScopeRollback<Cleanup> rollback_;
};
} // namespace order_cache::detail
