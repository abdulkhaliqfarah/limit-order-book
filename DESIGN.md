# Design notes

## The matching rules

The engine implements a continuous double auction with **price–time
priority**, the model used by essentially every major equity and futures
exchange:

1. An incoming (taker) order first matches the best-priced resting (maker)
   orders on the opposite side.
2. At equal prices, the oldest resting order fills first (FIFO).
3. Trades print at the **maker's** price — the taker gets price improvement
   when their limit is more aggressive than the resting quote.
4. Partial fills are normal; an unfilled limit remainder joins the back of
   its price level's queue, while an unfilled market-order remainder is
   cancelled (immediate-or-cancel).

## Why these data structures

**Price levels in `std::map`.** The number of live price levels for one
instrument is small (hundreds), so the `log L` factor is tiny and bounded.
The win is that best bid/ask is `map.begin()` — O(1), no heap walk at read
time. The known alternative is a flat array indexed by tick offset, which
makes level access O(1) but only works over a bounded price range and wastes
memory when the price wanders; production engines typically use the array
per instrument plus a fallback. For a single book that must accept any
`int64` price, the map is the right default.

**Intrusive linked lists for level queues.** Each order stores its own
`prev`/`next` and a back-pointer to its level. This is the decision that
makes cancel O(1) end to end: given an order id, the id index hands back the
node, and unlinking is four pointer writes. A `std::deque` or `std::list`
per level would either move elements or allocate per node, and neither
gives O(1) removal of an arbitrary element without an iterator stored
somewhere — the intrusive list just stores that information in the node.

**A pool allocator, not `new`.** Matching is a stream of tiny, short-lived
objects; general-purpose `malloc` in that loop costs more than the matching
itself and fragments. The pool allocates 64K-order chunks, threads a free
list through the nodes themselves, and recycles filled/cancelled orders.
Steady-state matching performs zero heap allocation (levels still allocate
occasionally, and there are far fewer of them; pooling them too is an easy
follow-up).

**Integer prices.** Prices are `int64` ticks. Floating-point prices are a
real bug source in matching systems (equality and ordering must be exact);
integers remove the whole class and compare faster.

**Trades via an output vector.** The engine appends to a caller-owned
`std::vector<Trade>` instead of invoking callbacks or returning
allocations, keeping the interface zero-cost for the replay loop and easy
to assert on in tests.

## Measurement honesty

`replay` reports two throughput numbers because they answer different
questions. The instrumented loop wraps every message in two
`steady_clock` reads (≈ 40–80 ns of overhead on this VM), which dominates
the p50 figure at these speeds — so the p50 of 160 ns is really "engine +
clock". The `--raw` loop removes the clock and shows the engine alone
(≈ 5.2M msg/s). Both numbers are single-run results on a shared cloud VM
and move ±20% between runs; the README quotes the conservative middle.
The synthetic feed is deliberately adversarial in one respect: heavy
crossing traffic (many immediate matches) — a quiet, rest-heavy feed
benchmarks faster, and real exchange traffic sits between the two.

## What the tests are for

Matching engines fail silently — a priority bug doesn't crash, it just
trades in the wrong order. The scenario tests pin each rule individually.
The differential test exists because enumerating cases by hand misses
interactions: the naive Python model shares no code with the engine, so
agreement over 50K mixed operations (adds, cancels of live and dead ids,
market sweeps, partial fills) is meaningful evidence, not a tautology.
