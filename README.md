# Order cache

A single-threaded C++20 implementation of the six methods in `task/`.
The supplied `Order` value API is unchanged.

```cpp
#include "OrderCache.h"

auto cache = makeOrderCache();
cache->addOrder(Order("buy-1", "ABC", "Buy", 10, "alice", "A"));
cache->addOrder(Order("sell-1", "ABC", "Sell", 7, "bob", "B"));
auto matching = cache->getMatchingSizeForSecurity("ABC"); // 7; no orders consumed.
```

Both secondary indexes are indexed max-heaps: one orders a security's orders by
quantity; the other orders its companies by their total Buy + Sell quantity.
The concrete cache is private to `src/OrderCache.cpp`. No implementation header
or configuration type is exposed by the factory.

## Storage and lifetime

`SlotPool` owns non-moving blocks through `std::unique_ptr` and reuses released
slots through a free list. Only the directory of block pointers relocates;
live `Order` objects retain their addresses. Block capacity follows the high-water
mark and is retained until cache destruction. A dense active-slot vector makes
`getAllOrders()` depend on current occupancy, not historical capacity.

Each record owns its `Order`, direct user/security/company pointers and reverse
positions. The ID map borrows string views from the stored order and erases each
key before destroying the record. Group records occupy stable nodes in ordinary
`std::unordered_map` containers; rehash does not relocate their objects. Empty
groups are reclaimed. The same user name may appear with different companies.

Heap entries and user/active lists use `std::vector`. User and active-list erases
swap with the last slot and correct the moved record's position. Heap repairs
likewise update reverse positions as entries move, allowing cancellation by a
known ID without searching the heap.

Operations provide the strong exception guarantee: exceptions leave the logical
cache contents unchanged. RAII guards (`ScopeRollback`, `PendingCreation` and
`SlotPool::PendingSlot`) protect provisional insertions. Final non-throwing
updates and commits run only after all potentially throwing work has succeeded.
Capacity increases may remain after rollback. Snapshots own their data and
outlive the cache.

## Matching from aggregates

For one security let `B` and `S` be total buy/sell quantities, and `T[c]` the total
quantity of company `c` on both sides. Maximum matching quantity is

```text
min(B, S, B + S - max(T[c]))
```

The first two limits are supply. Each matched unit also needs at least one unit
outside any selected company, which gives the third bound. For sufficiency,
consider the flow network from buy companies to all different sell companies.
A finite cut containing no buy company costs at least `B`; one containing exactly
company `c` costs at least `B + S - T[c]`; two or more buy companies force all sell
companies onto that side and cost at least `S`. These bounds are attainable cuts.
The company heap maintains the required maximum at its root; reads never consume
orders or repair deferred state. Live zero-quantity orders keep their groups alive.

## Updates and batch cancellation

Here `n` is the target security's order count, `c` its company count, `k` the
orders removed and `h` the distinct companies touched. Logarithms below use
`size + 1`; string work, hash lookups and allocation costs are separate.

| Operation | Index work |
| --- | --- |
| Add or cancel one order | O(log(n+1) + log(c+1)) |
| Matching query | O(1) after the security lookup |
| Quantity-threshold selection/removal | O(1 + min(k log(n+1), n)) |
| Company batch repair | O(h + min(h log(c+1), c)) |
| Snapshot | O(N + copied string bytes), for N live orders |

Threshold matches are not a contiguous prefix of the heap array. A pruned
traversal counts them only up to approximately `n / bit_width(n)`. Small batches
remove roots individually; large ones filter the array and heapify once. The
traversal uses a fixed stack bounded by the bit width of `size_t`.

User cancellation first groups that user's orders by security in O(k), without
scanning unrelated orders. Each affected quantity heap chooses point erases or
one compaction and rebuild. Company changes are accumulated and applied once
per touched company, again choosing point repairs or one rebuild. Removing the
records costs O(k) in addition to index work and dictionary costs. All batch work
finishes before the public call returns; later queries pay no deferred cleanup.

## Policies and limits

Missing cancellation targets are no-ops; removed IDs may be reused. Zero
quantities and exact empty/binary string keys are accepted. Duplicate active IDs
and sides other than exactly `Buy`/`Sell` throw `std::invalid_argument`. Wide sums
are checked; an unrepresentable matching result throws `std::overflow_error`.

Hash lookups are expected O(1), not worst-case O(1). Rehash, vector/block growth,
string allocation, group recreation and copying snapshots can create slow calls.
No concurrency or bounded wall-clock latency is promised.

See [tests/README.md](tests/README.md) for the test design and contract coverage.
