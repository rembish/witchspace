"""Differential test: commander block checksum (77c5), original (emulated) vs core.

usage: cmdrtest.py [n] [path/to/ep_cmdrdump]

The default block, then n random and n mutated-default blocks. Also checks that the core's
default block is the EXE's.
"""

import os
import random
import subprocess
import sys
from typing import Final

from eliteemu import Elite

HERE: Final = os.path.dirname(os.path.abspath(__file__))
N: Final = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
DUMP: Final = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "build", "ep_cmdrdump")
BASE: Final = 0x82DB  # the commander block
SIZE: Final = 226


def main() -> None:
    e = Elite()
    default = e.rb(BASE, SIZE)
    rng = random.Random(1)
    blocks = [default]
    for _ in range(N):
        blocks.append(bytes(rng.getrandbits(8) for _ in range(SIZE)))
        mutated = bytearray(default)
        for _ in range(rng.randint(1, 6)):
            mutated[rng.randrange(SIZE)] = rng.getrandbits(8)
        blocks.append(bytes(mutated))
    want: list[str] = []
    for b in blocks:
        e.wb(BASE, b)
        want.append(str(e.call(0x77C5)["bx"]))
    got = subprocess.run(
        [DUMP], input="".join(b.hex() + "\n" for b in blocks), capture_output=True, text=True, check=True
    ).stdout.split()
    bad = sum(1 for a, b in zip(want, got, strict=False) if a != b)
    d = subprocess.run([DUMP, "default"], capture_output=True, text=True, check=True).stdout.split()
    same_default = d[0] == default.hex()
    print(
        f"{len(want)} checksums, {bad} differ; default block {'matches' if same_default else 'DIFFERS'}, "
        f"default checksum {'valid' if d[1] == '1' else 'invalid'}"
    )
    sys.exit(1 if bad or not same_default or len(got) != len(want) else 0)


if __name__ == "__main__":
    main()
