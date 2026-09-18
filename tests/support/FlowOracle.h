#pragma once

#include "OrderCacheTestSupport.h"

#include <cstdint>
#include <string>

namespace order_cache::test
{
// Independent residual-network oracle for larger books. The existing bounded
// unit-assignment oracle remains unchanged and covers exhaustive tiny books.
std::uint64_t maximumFlowMatching(const Orders & orders, const std::string & security);
} // namespace order_cache::test
