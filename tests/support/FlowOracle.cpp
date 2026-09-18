#include "FlowOracle.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

namespace order_cache::test
{
std::uint64_t maximumFlowMatching(const Orders & orders, const std::string & security)
{
	std::map<std::string, std::size_t> companies;
	for (const auto & order : orders)
	{
		if (order.security == security && !companies.contains(order.company))
		{
			companies.emplace(order.company, companies.size());
		}
	}
	if (companies.size() > 64)
	{
		throw std::invalid_argument("Flow oracle is bounded to 64 companies");
	}
	const auto count = companies.size();
	const auto sink = (count * 2) + 1;
	const auto nodeCount = sink + 1;
	std::vector<std::vector<std::uint64_t>> residual(nodeCount, std::vector<std::uint64_t>(nodeCount));
	for (const auto & order : orders)
	{
		if (order.security != security)
		{
			continue;
		}
		const auto company = companies.at(order.company);
		if (order.side == "Buy")
		{
			residual[0][company + 1] += order.quantity;
		}
		else if (order.side == "Sell")
		{
			residual[count + company + 1][sink] += order.quantity;
		}
		else
		{
			throw std::invalid_argument("Flow oracle received an unknown side");
		}
	}
	for (std::size_t buyer = 0; buyer < count; ++buyer)
	{
		for (std::size_t seller = 0; seller < count; ++seller)
		{
			if (buyer != seller)
			{
				residual[buyer + 1][count + seller + 1] = residual[0][buyer + 1];
			}
		}
	}
	std::uint64_t total = 0;
	for (;;)
	{
		std::vector<std::size_t> parent(nodeCount, nodeCount);
		std::queue<std::size_t> pending;
		pending.push(0);
		parent[0] = 0;
		while (!pending.empty() && parent[sink] == nodeCount)
		{
			const auto source = pending.front();
			pending.pop();
			for (std::size_t target = 0; target < nodeCount; ++target)
			{
				if (parent[target] == nodeCount && residual[source][target] != 0)
				{
					parent[target] = source;
					pending.push(target);
				}
			}
		}
		if (parent[sink] == nodeCount)
		{
			return total;
		}
		auto amount = std::numeric_limits<std::uint64_t>::max();
		for (auto node = sink; node != 0; node = parent[node])
		{
			amount = std::min(amount, residual[parent[node]][node]);
		}
		for (auto node = sink; node != 0; node = parent[node])
		{
			residual[parent[node]][node] -= amount;
			residual[node][parent[node]] += amount;
		}
		total += amount;
	}
}
} // namespace order_cache::test
