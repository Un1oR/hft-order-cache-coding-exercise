#pragma once

#include "CompanyIndex.h"

#include <cstddef>
#include <string>

namespace order_cache::test
{
inline detail::Company & addCompany(detail::CompanyIndex & index, const std::string & name, unsigned int quantity)
{
	auto creation = index.getOrCreate(name);
	auto & company = creation.get();
	index.add(company, quantity);
	creation.commit();
	return company;
}
} // namespace order_cache::test
