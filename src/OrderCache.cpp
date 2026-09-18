#include "OrderCache.h"

#include "CompanyIndex.h"
#include "IndexStorage.h"
#include "QuantityIndex.h"
#include "ScopeRollback.h"
#include "SlotPool.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace order_cache::detail
{
template <class Map, class... Args>
[[nodiscard]] auto getOrCreate(Map & groups, const std::string & name, Args &&... args)
{
	auto [found, inserted] = groups.try_emplace(name, std::forward<Args>(args)...);
	auto & group = found->second;
	if (inserted)
	{
		group.name = found->first;
	}
	return PendingCreation(
		group,
		[&groups, &group]() noexcept
		{
			assert(group.empty());
			// C++20 has heterogeneous find, but not erase(string_view).
			// This avoids allocating a temporary owning string during rollback.
			groups.erase(groups.find(group.name));
		},
		inserted);
}

class OrderCache final : public OrderCacheInterface
{
	struct User;
	struct Security;

	struct OrderRecord : QuantityIndexRecord
	{
		Order order;
		User * user;
		Security * security;
		Company * company;
		bool buy;

		// Reverse positions in User::orderSlots and activeOrderSlots_. The
		// quantity-heap position and removal link live in QuantityIndexRecord.
		std::size_t userIndexPosition = 0;
		std::size_t activeIndexPosition = 0;

		OrderRecord(Order value, User & owner, Security & instrument, Company & employer, bool isBuy)
			: order(std::move(value))
			, user(&owner)
			, security(&instrument)
			, company(&employer)
			, buy(isBuy)
		{
		}
	};

	using OrderStorage = SlotPool<OrderRecord>;
	struct RecordBySlotId
	{
		OrderStorage * records = nullptr;
		QuantityIndexRecord & operator()(OrderStorageSlotId slot) const noexcept
		{
			return (*records)[slot];
		}
	};

	struct User
	{
		std::string_view name;
		std::vector<OrderStorageSlotId> orderSlots;

		// Non-owning intrusive membership in Batch::users, once per cancellation.
		// finishBatch clears this state before reclaiming an empty user.
		struct BatchState
		{
			User * nextDirty = nullptr;
			bool dirty = false;
		};
		BatchState batchState;

		[[nodiscard]] bool empty() const noexcept
		{
			return orderSlots.empty();
		}
	};

	struct Security
	{
		std::string_view name;
		QuantityIndex<RecordBySlotId> quantities;
		CompanyIndex companies;
		std::uint64_t buy = 0;
		std::uint64_t sell = 0;
		std::uint64_t total = 0;

		// Non-owning intrusive links for touched securities and this security's
		// included order records. Both are cleared before group reclamation.
		struct BatchState
		{
			Security * nextDirty = nullptr;
			OrderStorageSlotId includedOrdersHead = noSlot;
			std::size_t includedOrdersCount = 0;
			bool dirty = false;
		};
		BatchState batchState;

		explicit Security(OrderStorage & records)
			: quantities(RecordBySlotId{&records})
		{
		}

		// O(1) sized view; its deletion-safe iterators permit caller-owned cleanup.
		[[nodiscard]] auto includedOrderSlots(OrderStorage & records) const noexcept
		{
			return OrderRemovalRange(RecordBySlotId{&records},
									 batchState.includedOrdersHead,
									 batchState.includedOrdersCount);
		}

		// Quantity-zero orders are still live; totals cannot determine emptiness.
		[[nodiscard]] bool empty() const noexcept
		{
			return quantities.empty();
		}
	};

	struct Batch
	{
		Security * securities = nullptr;
		User * users = nullptr;

		void addUserAsDirtyIfNeeded(User & user) noexcept
		{
			if (user.batchState.dirty)
			{
				return;
			}
			user.batchState.dirty = true;
			user.batchState.nextDirty = users;
			users = &user;
		}

		void addSecurityAsDirtyIfNeeded(Security & security) noexcept
		{
			if (security.batchState.dirty)
			{
				return;
			}
			security.batchState.dirty = true;
			security.batchState.nextDirty = securities;
			securities = &security;
		}

		// Each record is included once from the user's unique slot list.
		void addOrder(OrderStorageSlotId slot, OrderRecord & record) noexcept
		{
			auto & security = *record.security;
			addSecurityAsDirtyIfNeeded(security);
			record.nextOrderInRemovalBatch = security.batchState.includedOrdersHead;
			security.batchState.includedOrdersHead = slot;
			++security.batchState.includedOrdersCount;
		}

		// Detach the list head before the caller can destroy its group.
		Security * popDirtySecurity() noexcept
		{
			auto * result = securities;
			if (result != nullptr)
			{
				securities = result->batchState.nextDirty;
			}
			return result;
		}
		User * popDirtyUser() noexcept
		{
			auto * result = users;
			if (result != nullptr)
			{
				users = result->batchState.nextDirty;
			}
			return result;
		}
	};

public:
	// n/c below count orders/companies in the affected security; N counts all live orders.
	OrderCache() = default;

	// Common: expected O(1) dictionary lookups and O(1) user/live-list appends.
	// Two heap updates: O(log(n + 1) + log(c + 1)).
	// Growth/rehash and string copies add slow paths. Pending insertions own
	// their rollback until every allocation has succeeded.
	void addOrder(Order order) override
	{
		const bool isBuy = order.sideRef() == "Buy";
		if (!isBuy && order.sideRef() != "Sell")
		{
			throw std::invalid_argument("Order side must be Buy or Sell");
		}
		if (byId_.contains(order.orderIdRef()))
		{
			throw std::invalid_argument("An active order already has this ID");
		}

		// Acquisitions unwind in reverse dependency order: list entries -> ID ->
		// slot -> company -> security -> user. Tokens own only new entries;
		// existing groups are never removed on a failed add.
		auto userCreation = getOrCreate(users_, order.userRef());
		auto & user = userCreation.get();
		auto securityCreation = getOrCreate(securities_, order.securityIdRef(), orderStorage_);
		auto & security = securityCreation.get();
		if (security.total > std::numeric_limits<std::uint64_t>::max() - order.qty())
		{
			throw std::overflow_error("Security quantity exceeds the internal sum type");
		}
		auto companyCreation = security.companies.getOrCreate(order.companyRef());
		auto & company = companyCreation.get();
		auto slotCreation = orderStorage_.emplacePending(std::move(order), user, security, company, isBuy);
		const auto slot = slotCreation.slotId();
		auto & record = orderStorage_[slot];
		// ID keys borrow from the stable stored Order. Each helper couples one
		// insertion with its rollback; no reserve/unguarded-push protocol remains.
		auto idCreation = tryEmplacePending(byId_, record.order.orderIdRef(), slot);
		auto userSlotCreation = appendPending(user.orderSlots, slot);
		auto activeSlotCreation = appendPending(activeOrderSlots_, slot);
		record.userIndexPosition = user.orderSlots.size() - 1;
		record.activeIndexPosition = activeOrderSlots_.size() - 1;
		security.quantities.insert(entry(slot));

		// The last throwing step succeeded. Publish aggregates and commit guards.
		// On failure, list entries and ID would unwind before the owning slot.
		const auto quantity = record.order.qty();
		security.total += quantity;
		(isBuy ? security.buy : security.sell) += quantity;
		security.companies.add(company, quantity);

		activeSlotCreation.commit();
		userSlotCreation.commit();
		idCreation.commit();
		slotCreation.commit();
		companyCreation.commit();
		securityCreation.commit();
		userCreation.commit();
	}

	// Expected O(1) ID lookup and swap-pop list erases, plus two heap repairs:
	// O(log(n + 1)) for the order and O(log(c + 1)) for its company.
	// Hash lookups can degrade to linear; allocator/string costs are separate.
	void cancelOrder(const std::string & orderId) override
	{
		const auto found = byId_.find(orderId);
		if (found == byId_.end())
		{
			return;
		}
		const auto slot = found->second;
		const auto & record = orderStorage_[slot];
		record.security->quantities.erase(slot);
		Batch batch;
		eraseDetachedOrderAndTrackGroups(slot, batch);
		finishBatch(batch);
	}

	// O(k) to group this user's k records. Per touched security, eraseOrders
	// chooses point erases or compaction; finishBatch repairs each distinct
	// company once. No scan of unrelated securities/users and no new storage.
	void cancelOrdersForUser(const std::string & name) override
	{
		const auto found = users_.find(std::string_view(name));
		if (found == users_.end())
		{
			return;
		}
		auto & user = found->second;
		Batch batch;
		// Group the user's k slots by security in O(k), without allocations or a
		// sort. Each security then chooses point erases or a single compaction.
		for (const auto slot : user.orderSlots)
		{
			batch.addOrder(slot, orderStorage_[slot]);
		}
		for (auto * security = batch.securities; security != nullptr; security = security->batchState.nextDirty)
		{
			for (const auto slot : security->quantities.eraseOrders(security->includedOrderSlots(orderStorage_),
																	[this, &user](QuantityEntry value) noexcept
																	{
																		return orderStorage_[value.slot].user == &user;
																	}))
			{
				eraseDetachedOrderAndTrackGroups(slot, batch);
			}
			security->batchState.includedOrdersHead = noSlot;
			security->batchState.includedOrdersCount = 0;
		}
		finishBatch(batch);
	}

	// Expected O(1) security lookup, then QuantityIndex::cancelAtLeast + O(k) detach
	// work and batched company repairs. See QuantityIndex for sparse/dense
	// bounds: a large removal is paid by this call, not by a later query.
	void cancelOrdersForSecIdWithMinimumQty(const std::string & name, unsigned int minimum) override
	{
		const auto found = securities_.find(std::string_view(name));
		if (found == securities_.end())
		{
			return;
		}
		Batch batch;
		for (const auto slot : found->second.quantities.cancelAtLeast(minimum))
		{
			eraseDetachedOrderAndTrackGroups(slot, batch);
		}
		finishBatch(batch);
	}

	// Expected O(1) security lookup (worst linear in securities), followed by
	// O(1) arithmetic and company-heap root access.
	unsigned int getMatchingSizeForSecurity(const std::string & name) override
	{
		const auto found = securities_.find(std::string_view(name));
		if (found == securities_.end())
		{
			return 0;
		}
		const auto & security = found->second;
		// Complete bipartite compatibility except each company's own diagonal:
		// M = min(B, S, B + S - max_c(B_c + S_c)). Every match involving a
		// dominant company needs one unit outside it. No pair simulation is needed.
		const auto matching = std::min({security.buy, security.sell, security.total - security.companies.maximum()});
		if (matching > std::numeric_limits<unsigned int>::max())
		{
			throw std::overflow_error("Matching quantity does not fit the interface return type");
		}
		return static_cast<unsigned int>(matching);
	}

	// O(N + copied string bytes), with allocation, for N live orders. Neither
	// quantity-index order nor the pool's high-water capacity affects the scan.
	[[nodiscard]] std::vector<Order> getAllOrders() const override
	{
		std::vector<Order> result;
		result.reserve(activeOrderSlots_.size());
		// Iterate live slots, not the pool's historical high-water capacity.
		for (const auto slot : activeOrderSlots_)
		{
			result.push_back(orderStorage_[slot].order);
		}
		return result;
	}

private:
	template <std::size_t OrderRecord::* PositionMember>
	void eraseOrderSlotBySwapPop(std::vector<OrderStorageSlotId> & slots, OrderStorageSlotId slot) noexcept
	{
		eraseSlotBySwapPop<PositionMember>(slots, orderStorage_, slot);
	}

	[[nodiscard]] QuantityEntry entry(OrderStorageSlotId slot) const noexcept
	{
		return {.quantity = orderStorage_[slot].order.qty(), .slot = slot};
	}

	// The quantity index has already removed this order. Detach the remaining
	// indexes, retire its slot, and enqueue touched groups for aggregate cleanup.
	void eraseDetachedOrderAndTrackGroups(OrderStorageSlotId slot, Batch & batch) noexcept
	{
		auto & record = orderStorage_[slot];
		auto & security = *record.security;
		auto & user = *record.user;
		batch.addSecurityAsDirtyIfNeeded(security);
		batch.addUserAsDirtyIfNeeded(user);
		const auto quantity = record.order.qty();
		security.total -= quantity;
		(record.buy ? security.buy : security.sell) -= quantity;
		security.companies.deferRemoval(*record.company, quantity);

		eraseOrderSlotBySwapPop<&OrderRecord::userIndexPosition>(user.orderSlots, slot);
		eraseOrderSlotBySwapPop<&OrderRecord::activeIndexPosition>(activeOrderSlots_, slot);
		byId_.erase(record.order.orderIdRef());
		orderStorage_.erase(slot);
	}

	void finishBatch(Batch & batch) noexcept
	{
		// Updating each touched company once avoids k heap repairs for k orders
		// from one company. Empty groups are reclaimed, not kept forever by name.
		while (auto * security = batch.popDirtySecurity())
		{
			security->companies.finishRemovals();
			security->batchState = {};
			if (security->empty())
			{
				// C++20 heterogeneous lookup avoids an allocating string conversion.
				securities_.erase(securities_.find(security->name));
			}
		}
		while (auto * user = batch.popDirtyUser())
		{
			user->batchState = {};
			if (user->empty())
			{
				users_.erase(users_.find(user->name));
			}
		}
	}

	// Map nodes and pool blocks keep their addresses across rehash/growth;
	// no pointer depends on vector element storage.
	OrderStorage orderStorage_;
	std::vector<OrderStorageSlotId> activeOrderSlots_;
	std::unordered_map<std::string_view, OrderStorageSlotId, StringHash, std::equal_to<>> byId_;
	std::unordered_map<std::string, User, StringHash, std::equal_to<>> users_;
	std::unordered_map<std::string, Security, StringHash, std::equal_to<>> securities_;
};
} // namespace order_cache::detail

std::unique_ptr<OrderCacheInterface> makeOrderCache()
{
	return std::make_unique<order_cache::detail::OrderCache>();
}
