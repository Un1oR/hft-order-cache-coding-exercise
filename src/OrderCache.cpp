#include "OrderCache.h"

namespace
{
class OrderCache final : public OrderCacheInterface
{
public:
	void addOrder([[maybe_unused]] Order order) override
	{
	}

	void cancelOrder([[maybe_unused]] const std::string & orderId) override
	{
	}

	void cancelOrdersForUser([[maybe_unused]] const std::string & user) override
	{
	}

	void cancelOrdersForSecIdWithMinimumQty([[maybe_unused]] const std::string & securityId,
											[[maybe_unused]] unsigned int minQty) override
	{
	}

	unsigned int getMatchingSizeForSecurity([[maybe_unused]] const std::string & securityId) override
	{
		return 0;
	}

	[[nodiscard]] std::vector<Order> getAllOrders() const override
	{
		return {};
	}
};
} // namespace

std::unique_ptr<OrderCacheInterface> makeOrderCache()
{
	return std::make_unique<OrderCache>();
}
