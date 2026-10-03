"""Differential test: commodity prices and arrival quantities, original (emulated) vs core.

usage: markettest.py [arrivals] [path/to/ep_marketdump]

Prices: market_prices (97d8) for every government, economy and tech level (ds:832c..832e),
buying and selling price per commodity (ds:8d0a, 4 bytes each). Quantities: the market
table (8ea2) with the "market drawn" flag ds:839d clear, repeatedly from the start-up
generator state; quantities are the second byte of each cargo pair at ds:8379. Equipment
(9161) with a pattern of owned items (ds:8356): count ds:acb0, prices at ds:8d0a. Selling price
(8e6b) for every possible buying price.
"""
import os
import subprocess
import sys

from eliteemu import Elite

HERE = os.path.dirname(os.path.abspath(__file__))
ARRIVALS = int(sys.argv[1]) if len(sys.argv) > 1 else 200
DUMP = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "build", "ep_marketdump")


def original():
    e = Elite()
    lines = []
    for gov in range(8):
        for eco in range(8):
            for tech in range(256):
                e.wb(0x832C, bytes([gov, eco, tech]))
                e.call(0x97D8)
                row = " ".join(f"{e.r16(0x8D0A + 4 * k)}/{e.r16(0x8D0C + 4 * k)}" for k in range(17))
                lines.append(f"{gov} {eco} {tech} {row}")
    e = Elite()
    for _ in range(ARRIVALS):
        e.w8(0x839D, 0)
        e.call(0x8EA2)
        lines.append("q " + " ".join(str(e.r8(0x837A + 2 * k)) for k in range(17)))
    for gov in range(8):
        for eco in range(8):
            for tech in range(256):
                e.wb(0x832C, bytes([gov, eco, tech]))
                e.wb(0x8356, bytes(int((gov + eco + tech + k) % 3 == 0) for k in range(14)))
                e.wb(0x8D0A, bytes(4 * 14))
                e.call(0x9161)
                n = e.r8(0xACB0)
                row = "".join(f" {e.r16(0x8D0A + 4 * k)}/{e.r16(0x8D0C + 4 * k)}" for k in range(n))
                lines.append(f"e {gov} {eco} {tech} {n}{row}")
    for v in range(0, 0x10000, 16):
        row = []
        for k in range(v, v + 16):
            e.w16(0x8D08, k)
            row.append(str(e.call(0x8E6B)["ax"]))
        lines.append("s " + " ".join(row))
    return lines


def main():
    want = original()
    got = subprocess.run([DUMP, str(ARRIVALS)], capture_output=True, text=True,
                         check=True).stdout.splitlines()
    bad = [(a, b) for a, b in zip(want, got) if a != b]
    for a, b in bad[:6]:
        print(f"original: {a}\ncore:     {b}\n")
    print(f"{len(want)} lines, {len(bad)} differ" + ("" if len(got) == len(want) else
          f" (core printed {len(got)})"))
    sys.exit(1 if bad or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
