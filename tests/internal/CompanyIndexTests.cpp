#include "CompanyIndex.h"
#include "CompanyTestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace order_cache::test
{
namespace
{
TEST(CompanyIndexTest, HandlesZeroQuantityAsALiveOrder)
{
	detail::CompanyIndex index;
	auto & company = addCompany(index, "zero", 0);
	EXPECT_EQ(index.size(), 1U);
	EXPECT_EQ(index.maximum(), 0U);
	index.deferRemoval(company, 0);
	index.finishRemovals();
	EXPECT_EQ(index.size(), 0U);
	EXPECT_EQ(index.maximum(), 0U);
}

TEST(CompanyIndexTest, LeaderChangesAndDepletionAgreeWithALinearMaximum)
{
	detail::CompanyIndex index;
	std::array<detail::Company *, 40> companies{};
	std::array<std::uint64_t, 40> totals{};
	for (std::size_t position = 0; position < companies.size(); ++position)
	{
		const auto quantity = static_cast<unsigned int>(position + 1);
		auto & company = addCompany(index, "company-" + std::to_string(position), quantity);
		companies[position] = &company;
		totals[position] = quantity;
		EXPECT_EQ(index.maximum(), std::ranges::max(totals));
	}
	for (std::size_t position = 0; position < companies.size(); ++position)
	{
		auto & company = *companies[position];
		index.add(company, 1000);
		EXPECT_EQ(index.maximum(), totals[position] + 1000);
		index.deferRemoval(company, 1000);
		index.finishRemovals();
		EXPECT_EQ(index.maximum(), 40U);
	}
	for (std::size_t position = companies.size(); position > 0; --position)
	{
		const auto selected = position - 1;
		index.deferRemoval(*companies[selected], static_cast<unsigned int>(totals[selected]));
		totals[selected] = 0;
		index.finishRemovals();
		EXPECT_EQ(index.maximum(), std::ranges::max(totals));
		EXPECT_EQ(index.size(), selected);
	}
}

TEST(CompanyIndexTest, DenseBatchCombinesRepeatedDeltasAndReclaimsEmptyCompanies)
{
	detail::CompanyIndex index;
	std::array<detail::Company *, 32> companies{};
	for (std::size_t position = 0; position < companies.size(); ++position)
	{
		auto & company = addCompany(index, std::to_string(position), 10);
		companies[position] = &company;
		index.add(company, 20);
		index.add(company, 30);
	}
	for (std::size_t position = 0; position < companies.size(); ++position)
	{
		index.deferRemoval(*companies[position], 30);
		if (position % 2 == 0)
		{
			index.deferRemoval(*companies[position], 20);
			index.deferRemoval(*companies[position], 10);
		}
	}
	index.finishRemovals();
	EXPECT_EQ(index.maximum(), 30U);
	EXPECT_EQ(index.size(), 16U);
	for (std::size_t position = 1; position < companies.size(); position += 2)
	{
		index.add(*companies[position], 100);
		EXPECT_EQ(index.maximum(), 130U);
		index.deferRemoval(*companies[position], 100);
		index.finishRemovals();
		EXPECT_EQ(index.maximum(), 30U);
	}
}

} // namespace
} // namespace order_cache::test
