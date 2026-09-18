#include "AllocationFailure.h"

#include <cstdlib>
#include <limits>
#include <new>

// This translation unit is linked only into the allocation-failure executable.
// Keeping replacements apart from callers also prevents allocation/deallocation
// inlining from obscuring the matching pair in compiler diagnostics.
namespace allocation_failure
{
thread_local std::size_t remaining = std::numeric_limits<std::size_t>::max();

FailAfter::FailAfter(std::size_t successfulAllocations) noexcept
{
	remaining = successfulAllocations;
}
FailAfter::~FailAfter()
{
	remaining = std::numeric_limits<std::size_t>::max();
}
} // namespace allocation_failure

// malloc/free implement the test's matching new/delete pair. No production
// allocation policy changes, and aligned allocations retain their usual path.
// Short parameter names match libc++ declarations without using reserved names.
// NOLINTBEGIN(readability-identifier-length)
void * operator new(std::size_t sz)
{
	if (allocation_failure::remaining != std::numeric_limits<std::size_t>::max())
	{
		if (allocation_failure::remaining == 0)
		{
			throw std::bad_alloc();
		}
		--allocation_failure::remaining;
	}
	for (;;)
	{
		if (void * pointer = std::malloc(sz == 0 ? 1 : sz))
		{
			return pointer;
		}
		const auto handler = std::get_new_handler();
		if (handler == nullptr)
		{
			throw std::bad_alloc();
		}
		handler();
	}
}
void * operator new[](std::size_t sz)
{
	return ::operator new(sz);
}
void operator delete(void * p) noexcept
{
	std::free(p);
}
void operator delete[](void * p) noexcept
{
	std::free(p);
}
void operator delete(void * p, [[maybe_unused]] std::size_t sz) noexcept
{
	std::free(p);
}
void operator delete[](void * p, [[maybe_unused]] std::size_t sz) noexcept
{
	std::free(p);
}
void * operator new(std::size_t sz, [[maybe_unused]] const std::nothrow_t & tag) noexcept
{
	try
	{
		return ::operator new(sz);
	}
	catch (...)
	{
		return nullptr;
	}
}
void * operator new[](std::size_t sz, const std::nothrow_t & tag) noexcept
{
	return ::operator new(sz, tag);
}
void operator delete(void * p, [[maybe_unused]] const std::nothrow_t & tag) noexcept
{
	std::free(p);
}
void operator delete[](void * p, [[maybe_unused]] const std::nothrow_t & tag) noexcept
{
	std::free(p);
}
// NOLINTEND(readability-identifier-length)
