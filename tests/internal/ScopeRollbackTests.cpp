#include "ScopeRollback.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <type_traits>

namespace order_cache::test
{
namespace
{
TEST(ScopeRollbackTest, NormalScopeExitRollsBackUnlessCommitted)
{
	int count = 0;
	{
		detail::ScopeRollback rollback(
			[&count]() noexcept
			{
				++count;
			});
		static_assert(!std::is_move_constructible_v<decltype(rollback)>);
		static_assert(!std::is_copy_constructible_v<decltype(rollback)>);
		static_assert(std::is_nothrow_destructible_v<decltype(rollback)>);
	}
	EXPECT_EQ(count, 1);
	{
		detail::ScopeRollback rollback(
			[&count]() noexcept
			{
				++count;
			});
		rollback.commit();
		rollback.commit();
	}
	EXPECT_EQ(count, 1);
}

TEST(ScopeRollbackTest, ExceptionUnwindsInReverseAcquisitionOrder)
{
	int order = 0;
	EXPECT_THROW(
		{
			detail::ScopeRollback first(
				[&order]() noexcept
				{
					order = (order * 10) + 1;
				});
			detail::ScopeRollback second(
				[&order]() noexcept
				{
					order = (order * 10) + 2;
				});
			throw std::runtime_error("abort transaction");
		},
		std::runtime_error);
	EXPECT_EQ(order, 21);
}

TEST(ScopeRollbackTest, CreationOwnsOnlyNewEntriesAndCanCommitThem)
{
	int value = 7;
	int rollbacks = 0;
	const auto cleanup = [&rollbacks]() noexcept
	{
		++rollbacks;
	};
	{
		detail::PendingCreation existing(value, cleanup, false);
		EXPECT_EQ(&existing.get(), &value);
	}
	EXPECT_EQ(rollbacks, 0);
	{
		detail::PendingCreation created(value, cleanup, true);
		EXPECT_EQ(&created.get(), &value);
	}
	EXPECT_EQ(rollbacks, 1);
	{
		detail::PendingCreation published(value, cleanup, true);
		published.commit();
	}
	EXPECT_EQ(rollbacks, 1);
}

struct OverloadedCleanup
{
	int * calls;
	void operator()()
	{
		throw std::runtime_error("wrong overload");
	}
	void operator()() const noexcept
	{
		++*calls;
	}
};

TEST(ScopeRollbackTest, InvokesTheConstOverloadCheckedByItsConstraint)
{
	int calls = 0;
	{
		detail::ScopeRollback rollback(OverloadedCleanup{&calls});
	}
	EXPECT_EQ(calls, 1);
}

struct ThrowingCleanup
{
	void operator()() const
	{
	}
};
template <class Cleanup>
concept AcceptsCleanup = requires { typename detail::ScopeRollback<Cleanup>; };
static_assert(!AcceptsCleanup<ThrowingCleanup>);
} // namespace
} // namespace order_cache::test
