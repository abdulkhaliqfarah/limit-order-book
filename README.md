# Limit Order Book — Matching Engine

A single-instrument limit order book / matching engine in C++20. It follows
the rules real exchanges use (strict price-time priority, trades print at the
maker's price) and processes a binary order feed modelled on Nasdaq ITCH.

The matching loop is written to avoid the usual costs: no allocation, no
searching, no floating point. On a synthetic feed it replays a few million
messages per second on one thread (numbers below).

## Performance

5,000,000-message synthetic feed (random-walking mid, ~12% cancels, 3% market
orders, 4.34M trades), single thread, shared 4-vCPU cloud VM, `-O3
-march=native`:

| Metric | Result |
|---|---|
| Throughput, per-message latency instrumentation on | ≈ 3.0M messages/sec |
| Throughput, raw loop (no instrumentation) | ≈ 5.2M messages/sec |
| Per-message latency p50 | 160 ns |
| Per-message latency p90 | 290 ns |
| Per-message latency p99 | < 1 µs |

Reproduce: `./build/gen_feed 5000000 1234 /tmp/feed.bin && ./build/replay /tmp/feed.bin`

## How it's built

- Prices are `int64` ticks (1 tick = $0.01). Integer prices keep ordering and
  equality exact; floats in a matching engine are a bug farm.
- Each price level holds a FIFO queue of orders as an intrusive linked list.
  Every order points back at its level, so cancel-by-id is O(1) pointer
  surgery instead of a lookup plus a scan.
- Orders come from a chunked pool with a free list, never from `new` in the
  matching loop. Filled and cancelled orders go back to the pool, so in
  steady state the engine allocates nothing per message.
- Price levels live in ordered maps keyed by price, so best bid/ask is just
  `begin()`. An `unordered_map` from order id to node handles cancel routing.
- Feed messages are fixed 32-byte structs (checked with `static_assert`), so
  decoding is a memcpy and a switch.
- Market orders are immediate-or-cancel: they sweep what's there and the
  remainder is dropped rather than rested.

Complexity (L = live price levels, kept small by tick granularity):

| Operation | Cost |
|---|---|
| Add, no match | O(log L) |
| Add, matching | O(log L) per level consumed |
| Cancel | **O(1)** |
| Best bid / ask | O(1) |

See [DESIGN.md](DESIGN.md) for the tradeoffs behind these choices and what I
would change at larger scale.

## Correctness

Matching engines fail quietly when they're wrong (no crash, just trades in
the wrong order), so I test two ways:

1. Scenario tests (`tests/test_order_book.cpp`, 55 checks): price priority
   across levels, FIFO within a level, fills at the maker's price, partial
   fills, a partially filled maker keeping its queue spot, cancelling the
   middle order of a level, market-order sweeps, cancelling ids that don't
   exist.
2. Differential testing (`scripts/model.py`): a separate, deliberately naive
   Python implementation of the same rules (dicts and linear scans, nothing
   shared with the C++ code). Both run the same 50,000-operation random
   streams and the trade logs and final book states have to match exactly.
   They do, across seeds.

```bash
python3 scripts/model.py ./build/check 50000 42
# AGREE: 50000 ops, 42425 trades, final book identical
```

## Build & run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-march=native"
cmake --build build -j
ctest --test-dir build            # scenario tests
./build/gen_feed 5000000 1234 /tmp/feed.bin
./build/replay /tmp/feed.bin       # latency percentiles + throughput
./build/replay /tmp/feed.bin --raw # uninstrumented throughput
```

Requires CMake ≥ 3.20 and a C++20 compiler. No external dependencies.

## Layout

```
include/lob/order_book.hpp   engine API and data structures
include/lob/feed.hpp         32-byte binary message format + feed handler
src/order_book.cpp           matching, resting, cancel, order pool
tools/gen_feed.cpp           deterministic synthetic feed generator
tools/replay.cpp             benchmark / latency measurement
tools/check.cpp              stdin ops driver used by the differential test
tests/test_order_book.cpp    scenario tests
scripts/model.py             naive reference model + differential runner
```

## TODO / ideas

- Price levels as a flat array over a bounded tick range (O(1) level access,
  closer to what production engines do per instrument).
- Multiple instruments sharded across threads behind a lock-free queue.
- Real Nasdaq ITCH 5.0 decoding instead of the simplified 32-byte format.
- Cancel/replace with proper queue-priority rules.

