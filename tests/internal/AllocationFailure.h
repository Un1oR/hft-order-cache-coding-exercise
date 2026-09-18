#pragma once

#include <cstddef>

namespace allocation_failure
{
// Armed only around the cache call. Framework assertions and snapshots run
// after this scope, so their allocations cannot consume the failure counter.
class FailAfter
{
public:
	explicit FailAfter(std::size_t successfulAllocations) noexcept;
	~FailAfter();
	FailAfter(const FailAfter &) = delete;
	FailAfter & operator=(const FailAfter &) = delete;
	FailAfter(FailAfter &&) = delete;
	FailAfter & operator=(FailAfter &&) = delete;
};
} // namespace allocation_failure
