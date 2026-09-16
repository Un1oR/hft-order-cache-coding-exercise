#include "support/OrderCacheTestSupport.h"

#include <array>
#include <ranges>
#include <string>

namespace order_cache::test
{
namespace
{
struct AssignmentExample
{
	std::string name;
	Orders orders;
	std::array<unsigned int, 3> matching;
};

std::ostream & operator<<(std::ostream & stream, const AssignmentExample & example)
{
	return stream << example.name;
}

// Data from task/ReadMe in the public Order constructor format; snapshot captures all six fields.
// clang-format off
std::vector<AssignmentExample> assignmentExamples()
{
    return {
        {.name = "ExplainedExample",
         .orders = snapshot({
          {"OrdId1", "SecId1", "Buy",  1000, "User1", "CompanyA"},
          {"OrdId2", "SecId2", "Sell", 3000, "User2", "CompanyB"},
          {"OrdId3", "SecId1", "Sell",  500, "User3", "CompanyA"},
          {"OrdId4", "SecId2", "Buy",   600, "User4", "CompanyC"},
          {"OrdId5", "SecId2", "Buy",   100, "User5", "CompanyB"},
          {"OrdId6", "SecId3", "Buy",  1000, "User6", "CompanyD"},
          {"OrdId7", "SecId2", "Buy",  2000, "User7", "CompanyE"},
          {"OrdId8", "SecId2", "Sell", 5000, "User8", "CompanyE"}}),
         .matching = {0, 2700, 0}},
        {.name = "AdditionalExampleOne",
         .orders = snapshot({
          {"OrdId1",  "SecId1", "Sell",  100, "User10", "Company2"},
          {"OrdId2",  "SecId3", "Sell",  200, "User8",  "Company2"},
          {"OrdId3",  "SecId1", "Buy",   300, "User13", "Company2"},
          {"OrdId4",  "SecId2", "Sell",  400, "User12", "Company2"},
          {"OrdId5",  "SecId3", "Sell",  500, "User7",  "Company2"},
          {"OrdId6",  "SecId3", "Buy",   600, "User3",  "Company1"},
          {"OrdId7",  "SecId1", "Sell",  700, "User10", "Company2"},
          {"OrdId8",  "SecId1", "Sell",  800, "User2",  "Company1"},
          {"OrdId9",  "SecId2", "Buy",   900, "User6",  "Company2"},
          {"OrdId10", "SecId2", "Sell", 1000, "User5",  "Company1"},
          {"OrdId11", "SecId1", "Sell", 1100, "User13", "Company2"},
          {"OrdId12", "SecId2", "Buy",  1200, "User9",  "Company2"},
          {"OrdId13", "SecId1", "Sell", 1300, "User1", "Company2"}}),
         .matching = {300, 1000, 600}},
        {.name = "AdditionalExampleTwo",
         .orders = snapshot({
          {"OrdId1",  "SecId3", "Sell",  100, "User1", "Company1"},
          {"OrdId2",  "SecId3", "Sell",  200, "User3", "Company2"},
          {"OrdId3",  "SecId1", "Buy",   300, "User2", "Company1"},
          {"OrdId4",  "SecId3", "Sell",  400, "User5", "Company2"},
          {"OrdId5",  "SecId2", "Sell",  500, "User2", "Company1"},
          {"OrdId6",  "SecId2", "Buy",   600, "User3", "Company2"},
          {"OrdId7",  "SecId2", "Sell",  700, "User1", "Company1"},
          {"OrdId8",  "SecId1", "Sell",  800, "User2", "Company1"},
          {"OrdId9",  "SecId1", "Buy",   900, "User5", "Company2"},
          {"OrdId10", "SecId1", "Sell", 1000, "User1", "Company1"},
          {"OrdId11", "SecId2", "Sell", 1100, "User6", "Company2"}}),
         .matching = {900, 600, 0}}
    };
}
// clang-format on

class AssignmentExamplesTest
	: public CacheTest
	, public ::testing::WithParamInterface<AssignmentExample>
{
};

TEST_P(AssignmentExamplesTest, ReproducesAllPublishedMatchingSizesWithoutConsumingOrders)
{
	const auto & example = GetParam();
	givenOrders(example.orders);
	for (std::size_t index = 0; index < example.matching.size(); ++index)
	{
		const auto security = "SecId" + std::to_string(index + 1);
		SCOPED_TRACE(security);
		EXPECT_EQ(cache->getMatchingSizeForSecurity(security), example.matching.at(index));
		expectOrders(example.orders);
	}
}

TEST_P(AssignmentExamplesTest, GivesTheSameResultsWhenInsertionOrderIsReversed)
{
	const auto & example = GetParam();
	for (const auto & order : std::views::reverse(example.orders))
	{
		cache->addOrder(toOrder(order));
	}
	for (std::size_t index = 0; index < example.matching.size(); ++index)
	{
		EXPECT_EQ(cache->getMatchingSizeForSecurity("SecId" + std::to_string(index + 1)), example.matching.at(index));
	}
	expectOrders(example.orders);
}

INSTANTIATE_TEST_SUITE_P(ReadMe,
						 AssignmentExamplesTest,
						 ::testing::ValuesIn(assignmentExamples()),
						 [](const ::testing::TestParamInfo<AssignmentExample> & caseInfo)
						 {
							 return caseInfo.param.name;
						 });
} // namespace
} // namespace order_cache::test
