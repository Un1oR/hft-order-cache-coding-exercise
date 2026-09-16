# Single-Threaded OrderCache Contract

The original suite started with a verified red phase (143 failed / 16 passed
against the stub). The same public expectations now run once through
`makeOrderCache()`. Each test creates an independent cache.

The requirements come from `../task/ReadMe` and `../task/OrderCache.h`. The original contract suite
tests only `OrderCacheInterface`, created through the production factory `makeOrderCache()`.
The original contract tests do not access the concrete class or internal indexes.
Their assertions and the original assignment files remain unchanged.

The suite must build and run against the original stub, then exit with a nonzero
status because of **real result mismatches**. Not every test has to be red: the
empty state, some zero results, and test-infrastructure self-checks may pass before
the implementation exists. There are no `DISABLED_` tests, skips, inverted exit
codes, `WILL_FAIL`, artificial `FAIL()` calls in contract tests, or mocks in place
of the future implementation.

## Suite Structure

| File | Contract covered |
| --- | --- |
| `OrderCacheTests.cpp` | Addition, all six fields, object and snapshot independence, const reads, container growth |
| `CancellationTests.cpp` | All three cancellation routes, exact keys, `qty >= minQty` boundaries, repeated and overlapping cancellation, reuse of a removed ID |
| `MatchingTests.cpp` | Compatibility rules, partial and multiple pairs, prevention of quantity reuse, maximum matching, book immutability, numeric boundaries |
| `AssignmentExamplesTests.cpp` | All three books from ReadMe, every published result, forward and reverse insertion order |
| `StateTransitionTests.cpp` | Result invalidation after add/cancel, depletion and repopulation, eight reproducible histories of 160 operations |
| `ExhaustiveMatchingTests.cpp` | All 729 books for three companies with quantity 0, 1, or 2 in each Buy/Sell slot |
| `TestSupportTests.cpp` | Snapshot comparison and independent-oracle checks, including helper limits and errors |
| `support/OrderCacheTestSupport.*` | Shared fixture, readable participants/orders, field diagnostics, multiset comparison, cancellation routes |
| `support/MatchingOracle.*` | Test-only bounded exhaustive assignment of quantity units |

Each test receives a new cache in `SetUp()`. A `unique_ptr` provides teardown;
empty `TearDown()` methods and global mutable state are unnecessary.
Parameterization combines genuinely identical actions: standard books,
thresholds, cancellation routes, permutations, and seeds. Distinct scenarios are
not hidden behind a universal DSL. Parameter names describe the scenario instead
of merely numbering it. Comments explain given/when/then behavior or a specific
trap rather than narrating every line.

`sameOrders()` compares **multisets of all six fields**. It does not depend on
`getAllOrders()` order, collapse identical IDs/elements, or hide readable expected
and actual orders. Its own tests cover all six fields and repeated elements.
Repeated IDs in these self-checks are only inputs to the comparison function, not
a promise about `addOrder()` conflict behavior.

## Method Coverage and Important Failure Modes

| Method | Important checks |
| --- | --- |
| `addOrder` | Buy/Sell, 0/1/`UINT_MAX`, long and compound strings, several orders identical except for ID, input-value ownership, 512 entries |
| `cancelOrder` | Only, first, middle, and last order; exact ID comparison; unknown/removed ID; ID reuse with every field changed |
| `cancelOrdersForUser` | Both sides and multiple securities; colleague at the same company survives; exact user name; repeated cancellation and future orders |
| `cancelOrdersForSecIdWithMinimumQty` | 0, 1, 99, 100, 101, 102, `UINT_MAX-1`, `UINT_MAX`; both sides at the boundary; other securities; threshold per order rather than user/company totals |
| `getMatchingSizeForSecurity` | Same security, opposite sides/companies; smaller side; multiple counterparties; no double counting; maximum; repeated reads and mutations after reads |
| `getAllOrders` | Complete fields/contents, const interface, independent copies, state after every cancellation route and matching query |

The following counterexamples are especially important:

1. **Greedy matching does not always find the maximum.** Buys are Atlas=100 and
   Beacon=100; sells are Cedar=100 and Beacon=100. Pairing Atlas→Cedar leaves the
   incompatible Beacon→Beacon pair and yields only 100. Atlas→Beacon followed by
   Beacon→Cedar yields 200. All 24 insertion orders are checked; separate tests
   rename IDs and companies, swap Buy/Sell, and scale quantities.
2. **The answer is not simply `min(totalBuy, totalSell)`.** A dominant company has
   large quantities on both sides, but they cannot match each other.
3. **A matching query must not consume quantities.** Repeated queries must agree,
   the snapshot remains unchanged, and later threshold cancellation uses the
   original quantity rather than a computed remainder.
4. **Internal sums can exceed `unsigned int` even when the answer fits.** Cases
   include buy/sell totals and a single company's total of `UINT_MAX + 1`, with an
   expected result of either 7 or `UINT_MAX`.
5. **Removal must clear every index.** After an ID is reused with new fields,
   cancellation by the old user/security must not remove the replacement.

## Oracle Independence and Reproducibility

The oracle expands each small order quantity into individual units. Each Buy unit
is either left unmatched or paired with one still-free Sell unit from another
company; the best result across all assignments is selected. Memoization only
eliminates repeated enumeration states. The oracle contains no production cache
implementation, production indexes, company-aggregation formula, or production
flow algorithm. It is limited to **12 total units** for the selected security;
larger inputs produce a test-helper error instead of unbounded work. Large examples
and boundary arithmetic use explicit expected values.

There are 729 books because `3^6` covers Buy/Sell for three companies, with each
slot holding quantity 0..2. A zero slot is not added. Scenarios with an explicitly
added zero-quantity order are covered separately. On the first mismatch, the
exhaustive test stops and prints the book number and complete contents instead of
producing hundreds of identical failures.

Histories use `std::mt19937` with seeds 0, 1, 7, 42, 2026, 65537, `0xC0FFEE`, and
`0xDEADBEEF`. Modulo selection keeps them reproducible without `random_device` or
implementation-dependent distributions. The model contains at most six orders of
0..2 units. After **every** step, the suite checks every field, the previous
snapshot, matching for three securities, an unknown security, and data immutability
after reads. Diagnostics include the seed and full history up to the failure. These
histories supplement the explicit examples; they neither replace them nor prove
correctness for every possible book.

## Adopted Interpretations and Unspecified Policies

The assignment does not formalize every aspect. This suite adopts the following
interpretations; they should be discussed separately rather than treated as
verbatim requirements:

| Interpretation | Rationale and scope |
| --- | --- |
| Matching returns the **maximum possible** total quantity | "Total qty that can match" is read as the achievable maximum, without FIFO or iteration priority. The greedy counterexample deliberately fixes this interpretation. |
| Matching is a query, not execution/consumption | The `get...` method returns a number, while separate methods perform removal. The original signature is not `const`, so this interpretation is documented explicitly. |
| Cancelling a missing target is a safe no-op | An empty set is removed; no exception type or policy is specified. Cancellation is therefore idempotent. |
| Quantity 0 is accepted and stored | The type is unsigned and there is no `qty > 0` restriction; such an order contributes no match and is removed by threshold 0. |
| String keys use exact comparison | The suite adds no unspecified normalization of case, whitespace, or prefixes. |
| A removed ID may be reused | Uniqueness applies to orders currently in the cache; lifetime registration of used IDs is not specified. |

The original contract suite **does not fix** the order of `getAllOrders()`, FIFO, data structures,
active duplicate-ID policy (ignore/replace/throw), empty identifiers, unknown
sides, one user associated with conflicting companies, out-of-memory behavior, or
a matching result that mathematically exceeds `UINT_MAX`. The last case cannot be
expressed by the return type without an additional policy
(exception/saturation/modular arithmetic), so the tests do not choose one for the
assignment author.

The original contract suite does not test multithreading, a concrete class, copying the internal
implementation, allocators, or wall-clock performance. The 30-second CTest limit
guards against hangs; it is not a latency requirement. Mocks are unnecessary: the
contract has no external dependencies, and a mock cache would only test itself.

## Running the Suite and Working Through TDD

Use the repository's standard Clang/libc++/lld path with ASan+UBSan:

```sh
./scripts/check.sh format
./scripts/check.sh build
./scripts/check.sh tidy
./scripts/check.sh test
```

In the initial state, the last step must be red **because of assertions**, not
compilation errors, formatting errors, sanitizer crashes, or missing tests. Do not
turn that exit code into CI success: it is the intentional TDD red phase.

Individual groups and diagnostics:

```sh
ctest --preset sanitize -N
ctest --preset sanitize -R 'StorageTest|OrderRoundTripTest' --output-on-failure
ctest --preset sanitize -R 'Cancellation|CancelPosition|MinimumQuantity' --output-on-failure
ctest --preset sanitize -R 'Matching|AssignmentExamples' --output-on-failure
ctest --preset sanitize -R 'DeterministicHistoryTest' --output-on-failure
ctest --preset sanitize -R 'OrderComparisonTest|MatchingOracleTest' --output-on-failure
./build/sanitize/order_cache_tests --gtest_filter='*MaximumMatchingTest*'
./build/sanitize/order_cache_tests --gtest_shuffle --gtest_random_seed=2026 --gtest_repeat=3
```

For another supported build environment, omit the Clang preset:

```sh
cmake -S . -B build/local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/local
ctest --test-dir build/local --output-on-failure
```

A practical implementation order is storage/snapshots → cancellation and its
boundaries → simple matching rules → maximum and order independence → invalidation
and histories → large-quantity arithmetic. The tests remain black-box checks at
every stage.
