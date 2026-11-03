#!/usr/bin/env python3
"""Differential test: a naive Python model of the order book vs the C++ engine.

The model is deliberately dumb -- plain dicts and linear scans, written
straight from the matching rules -- so it is easy to trust. This script
generates a random operation stream, runs it through both implementations,
and diffs the full trade log and final book state.

Usage: python3 scripts/model.py <path-to-check-binary> [num_ops] [seed]
Exit code 0 = the two implementations agree exactly.
"""

import random
import subprocess
import sys


class Model:
    def __init__(self):
        self.orders = {}  # id -> [side, price, qty] for resting orders
        self.levels = {}  # (side, price) -> list of ids, FIFO order

    def _rest(self, oid, side, price, qty):
        self.orders[oid] = [side, price, qty]
        self.levels.setdefault((side, price), []).append(oid)

    def _remove(self, oid):
        side, price, _ = self.orders.pop(oid)
        queue = self.levels[(side, price)]
        queue.remove(oid)
        if not queue:
            del self.levels[(side, price)]

    def add(self, oid, side, otype, price, qty):
        trades = []
        remaining = qty
        opp = "S" if side == "B" else "B"
        while remaining > 0:
            prices = [p for (s, p) in self.levels if s == opp]
            if not prices:
                break
            best = min(prices) if opp == "S" else max(prices)
            if otype == "L":
                if side == "B" and best > price:
                    break
                if side == "S" and best < price:
                    break
            queue = self.levels[(opp, best)]
            while remaining > 0 and queue:
                maker_id = queue[0]
                maker = self.orders[maker_id]
                fill = min(remaining, maker[2])
                trades.append((maker_id, oid, best, fill))
                remaining -= fill
                maker[2] -= fill
                if maker[2] == 0:
                    self._remove(maker_id)
        if remaining > 0 and otype == "L":
            self._rest(oid, side, price, remaining)
        return trades

    def cancel(self, oid):
        if oid in self.orders:
            self._remove(oid)

    def dump(self):
        lines = []
        totals = {}
        for (side, price), queue in self.levels.items():
            totals[(side, price)] = sum(self.orders[i][2] for i in queue)
        for side in ("B", "S"):
            for price in sorted(p for (s, p) in totals if s == side):
                lines.append(f"BOOK {side} {price} {totals[(side, price)]}")
        lines.append(f"LIVE {len(self.orders)}")
        return lines


def gen_ops(rng, n):
    ops = []
    live = []
    next_id = 1
    mid = 10000
    for _ in range(n):
        if live and rng.random() < 0.12:
            oid = live.pop(rng.randrange(len(live)))
            ops.append(f"C {oid}")
            continue
        side = rng.choice("BS")
        mid += rng.randint(-2, 2)
        offset = rng.randint(-8, 12)
        price = mid + offset if side == "B" else mid - offset
        price = max(9500, min(10500, price))
        otype = "M" if rng.random() < 0.03 else "L"
        qty = rng.randint(1, 500)
        ops.append(f"A {next_id} {side} {otype} {price} {qty}")
        live.append(next_id)
        next_id += 1
    return ops


def main():
    binary = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 20000
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 42

    rng = random.Random(seed)
    ops = gen_ops(rng, n)
    ops_text = "\n".join(ops) + "\n"

    model = Model()
    expected = []
    for line in ops:
        parts = line.split()
        if parts[0] == "A":
            _, oid, side, otype, price, qty = parts
            for maker, taker, p, q in model.add(
                int(oid), side, otype, int(price), int(qty)
            ):
                expected.append(f"T {maker} {taker} {p} {q}")
        else:
            model.cancel(int(parts[1]))
    expected += model.dump()

    proc = subprocess.run(
        [binary], input=ops_text, capture_output=True, text=True, check=True
    )
    actual = proc.stdout.strip().split("\n")

    if actual == expected:
        print(f"AGREE: {n} ops, {sum(1 for l in expected if l.startswith('T '))} trades, "
              f"final book identical")
        return 0
    for i, (a, e) in enumerate(zip(actual, expected)):
        if a != e:
            print(f"first divergence at output line {i}:\n  C++:    {a}\n  model:  {e}")
            break
    print(f"line counts: C++ {len(actual)} vs model {len(expected)}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
