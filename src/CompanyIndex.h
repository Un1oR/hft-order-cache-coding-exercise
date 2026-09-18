#pragma once

#include "IndexStorage.h"
#include "IndexedHeap.h"
#include "ScopeRollback.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace order_cache::detail
{
struct Company
{
	std::string_view name;
	std::size_t orders = 0;

	// Reverse position in CompanyIndex::storage_, the aggregate max-heap.
	std::size_t positionInIndexStorage = 0;

	// Intrusive, singly linked membership in this index's pending-removal list.
	// A company is enqueued once even when many orders (including qty 0) are
	// removed. These fields are reset by finishRemovals, not carried to a query.
	struct RemovalBatch
	{
		std::uint64_t quantity = 0;
		Company * next = nullptr;
		bool queued = false;
	};
	RemovalBatch removalBatch;
};

// Companies with live orders in one security. The dictionary resolves names;
// the indexed max-heap holds each company's total Buy + Sell quantity. Its root
// supplies the largest company volume for the matching formula in O(1).
class CompanyIndex
{
	struct Entry
	{
		std::uint64_t total;
		Company * company;
	};
	struct Less
	{
		bool operator()(const Entry & left, const Entry & right) const noexcept
		{
			return left.total < right.total;
		}
	};
	struct Position
	{
		void operator()(const Entry & entry, std::size_t index) const noexcept
		{
			entry.company->positionInIndexStorage = index;
		}
	};

public:
	CompanyIndex() = default;
	CompanyIndex(const CompanyIndex &) = delete;
	CompanyIndex & operator=(const CompanyIndex &) = delete;
	CompanyIndex(CompanyIndex &&) = delete;
	CompanyIndex & operator=(CompanyIndex &&) = delete;
	~CompanyIndex() = default;

	// Dictionary: expected O(1) lookup, worst O(c); strings/allocations separate.
	// Append a zero entry in O(1), plus O(c) if the vector grows.
	// Zero cannot exceed its nonnegative parent, so no sift-up is needed.
	// The returned token removes a new, uncommitted entry on scope exit. Perform
	// the order's add() and commit the token before another creation/removal.
	// Existing companies are never rolled back.
	[[nodiscard]] auto getOrCreate(const std::string & name)
	{
		assert(dirtyHead_ == nullptr);
		auto [found, inserted] = companies_.try_emplace(name);
		auto & company = found->second;
		ScopeRollback rollbackNode(
			[this, found]() noexcept
			{
				companies_.erase(found);
			},
			inserted);
		if (inserted)
		{
			company.name = found->first;
			storage_.pushLeaf(Entry{.total = 0, .company = &company}, Position{});
		}
		rollbackNode.commit();
		return PendingCreation(
			company,
			[this, &company]() noexcept
			{
				discardEmpty(company);
			},
			inserted);
	}

	// O(log(c + 1)) aggregate update and sift-up; qty 0 needs no repair.
	// Precondition: no pending removals, and total + quantity fits uint64_t.
	// OrderCache::addOrder checks security.total <= UINT64_MAX - quantity before
	// commit; every company total is bounded by that security total.
	void add(Company & company, unsigned int quantity) noexcept
	{
		assert(dirtyHead_ == nullptr);
		const auto total = storage_[company.positionInIndexStorage].total;
		assert(total <= std::numeric_limits<std::uint64_t>::max() - quantity);
		++company.orders;
		if (quantity != 0)
		{
			storage_.update(company.positionInIndexStorage,
							Entry{.total = total + quantity, .company = &company},
							Position{});
		}
	}

	// O(1) per order: accumulate its delta and enqueue the company once.
	// Depletion uses live order count: a zero total need not mean an empty company.
	// maximum/add require finishRemovals first; the removed quantity must be live.
	void deferRemoval(Company & company, unsigned int quantity) noexcept
	{
		assert(company.orders > 0);
		--company.orders;
		company.removalBatch.quantity += quantity;
		addCompanyAsDirtyIfNeeded(company);
	}

	// h = touched companies, d = depleted companies, c = companies before removal.
	// Sparse: O(h log(c + 1)) point repairs.
	// Dense: O(c + h) compaction and heapify.
	// Choose O(h + min(h log(c + 1), c)); map erases add expected O(d),
	// worst O(d*c). String costs are separate. No allocation or deferred cleanup.
	void finishRemovals() noexcept
	{
		if (dirtyHead_ == nullptr)
		{
			return;
		}
		const auto count = storage_.size();
		const auto levels = static_cast<std::size_t>(std::bit_width(count));
		// Rebuild once when h logarithmic repairs cost more than a full pass.
		if (dirtyCount_ > count / std::max(std::size_t{1}, levels))
		{
			// Transform/filter/rebuild is one heap operation; no mutable array
			// escapes and all survivor positions are valid when it returns.
			storage_.transformEraseIf(
				[](Entry & entry) noexcept
				{
					entry.total -= entry.company->removalBatch.quantity;
				},
				[](const Entry & entry) noexcept
				{
					return entry.company->orders == 0;
				},
				Position{});
		}
		else
		{
			for (auto * company = dirtyHead_; company != nullptr; company = company->removalBatch.next)
			{
				// Apply one delta at a time: heap repair assumes the other keys
				// still satisfy its invariant. Earlier erases may move this entry.
				if (company->orders == 0)
				{
					removeEntry(*company);
				}
				else if (company->removalBatch.quantity != 0)
				{
					const auto total = storage_[company->positionInIndexStorage].total - company->removalBatch.quantity;
					storage_.update(company->positionInIndexStorage,
									Entry{.total = total, .company = company},
									Position{});
				}
			}
		}
		// Both paths remove heap references before erasing depleted map nodes.
		while (dirtyHead_ != nullptr)
		{
			auto * company = dirtyHead_;
			dirtyHead_ = company->removalBatch.next;
			company->removalBatch = {};
			if (company->orders == 0)
			{
				companies_.erase(companies_.find(company->name));
			}
		}
		dirtyCount_ = 0;
	}

	// O(1) at the root. All removal batches must have been finished.
	[[nodiscard]] std::uint64_t maximum() const noexcept
	{
		assert(dirtyHead_ == nullptr);
		return storage_.empty() ? 0 : storage_.top().total;
	}
	// O(1); companies pending depletion remain counted until finishRemovals.
	[[nodiscard]] std::size_t size() const noexcept
	{
		return storage_.size();
	}

private:
	// An uncommitted zero entry is still the last leaf: rollback just pops it.
	// Dictionary erase is expected O(1), worst O(c), excluding string costs.
	void discardEmpty(Company & company) noexcept
	{
		assert(company.orders == 0 && !company.removalBatch.queued);
		assert(company.positionInIndexStorage == storage_.size() - 1);
		storage_.popLeaf();
		companies_.erase(companies_.find(company.name));
	}
	void addCompanyAsDirtyIfNeeded(Company & company) noexcept
	{
		if (company.removalBatch.queued)
		{
			return;
		}
		company.removalBatch.queued = true;
		company.removalBatch.next = dirtyHead_;
		dirtyHead_ = &company;
		++dirtyCount_;
	}
	void removeEntry(Company & company) noexcept
	{
		storage_.erase(company.positionInIndexStorage, Position{});
	}

	// Map nodes and their string keys remain stable across rehash.
	std::unordered_map<std::string, Company, StringHash, std::equal_to<>> companies_;
	IndexedHeap<Entry, Less> storage_;
	Company * dirtyHead_ = nullptr;
	std::size_t dirtyCount_ = 0;
};
} // namespace order_cache::detail
