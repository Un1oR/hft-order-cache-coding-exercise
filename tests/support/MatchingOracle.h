#pragma once

#include "OrderCacheTestSupport.h"

#include <string>

namespace order_cache::test
{
// Test-only oracle: exhaustive assignment of quantity units, not the cache algorithm.
// Limited to 12 total units of the selected security; larger inputs are rejected.
unsigned int maximumUnitMatching(const Orders & orders, const std::string & security);
} // namespace order_cache::test
