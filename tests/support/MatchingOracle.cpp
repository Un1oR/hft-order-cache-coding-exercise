#include "MatchingOracle.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

namespace order_cache::test
{
namespace
{
constexpr unsigned int unknown = std::numeric_limits<unsigned int>::max();
constexpr unsigned int maximumUnits = 12;

unsigned int enumerateAssignments(const std::vector<std::string> & buyers,
								  const std::vector<std::string> & sellers,
								  std::size_t buyerIndex,
								  std::size_t usedSellers,
								  std::vector<unsigned int> & memo)
{
	if (buyerIndex == buyers.size())
	{
		return 0;
	}
	const std::size_t statesPerBuyer = std::size_t{1} << sellers.size();
	auto & result = memo.at((buyerIndex * statesPerBuyer) + usedSellers);
	if (result != unknown)
	{
		return result;
	}

	// Each buy unit remains unmatched or takes one available sell unit.
	result = enumerateAssignments(buyers, sellers, buyerIndex + 1, usedSellers, memo);
	for (std::size_t sellerIndex = 0; sellerIndex < sellers.size(); ++sellerIndex)
	{
		const std::size_t sellerBit = std::size_t{1} << sellerIndex;
		if ((usedSellers & sellerBit) == 0 && buyers.at(buyerIndex) != sellers.at(sellerIndex))
		{
			const auto candidate =
				1U + enumerateAssignments(buyers, sellers, buyerIndex + 1, usedSellers | sellerBit, memo);
			result = std::max(result, candidate);
		}
	}
	return result;
}
} // namespace

unsigned int maximumUnitMatching(const Orders & orders, const std::string & security)
{
	std::vector<std::string> buyers;
	std::vector<std::string> sellers;
	unsigned int totalUnits = 0;
	for (const auto & order : orders)
	{
		if (order.security != security)
		{
			continue;
		}
		if (order.side != "Buy" && order.side != "Sell")
		{
			throw std::invalid_argument("The oracle supports only Buy/Sell");
		}
		if (order.quantity > maximumUnits - totalUnits)
		{
			throw std::invalid_argument("The oracle is limited to 12 units of quantity");
		}
		totalUnits += order.quantity;
		auto & units = order.side == "Buy" ? buyers : sellers;
		units.insert(units.end(), order.quantity, order.company);
	}
	const std::size_t statesPerBuyer = std::size_t{1} << sellers.size();
	std::vector<unsigned int> memo(buyers.size() * statesPerBuyer, unknown);
	return enumerateAssignments(buyers, sellers, 0, 0, memo);
}
} // namespace order_cache::test
