import json
from functools import lru_cache
from math import isqrt
from pathlib import Path
import sys

PATTERNS, GGGG = 81, 80

def primes():
    return [str(n) for n in range(1000, 10000)
            if all(n % d for d in range(2, isqrt(n) + 1))]

def feedback(g, t):
    r, left = [0]*4, list(t)
    for i in range(4):
        if g[i] == t[i]:
            r[i], left[i] = 2, None
    for i in range(4):
        if not r[i] and g[i] in left:
            r[i], left[left.index(g[i])] = 1, None
    return sum(r[i] * 3**i for i in range(4))

def text(p):
    s = ""
    for _ in range(4):
        s += "nyg"[p % 3]
        p //= 3
    return s

P = primes()
N = len(P)
M = [[0]*PATTERNS for _ in range(N)]

for gi, g in enumerate(P):
    for ti, t in enumerate(P):
        M[gi][feedback(g, t)] |= 1 << ti

def score(i):
    a = [m.bit_count() for m in M[i] if m]
    # return max(a), -len(a)
    return sum(b*b for b in a)

ORDER = sorted(range(N), key=score)
CHOICE = {}

@lru_cache(None)
def can(state, depth):
    if state.bit_count() <= 1:
        CHOICE[state, depth] = (state & -state).bit_length() - 1
        return True
    if depth == 1:
        return False

    for gi in ORDER:
        if all(p == GGGG or not (child := state & mask)
               or can(child, depth - 1)
               for p, mask in enumerate(M[gi])):
            CHOICE[state, depth] = gi
            return True
    return False

def build():
    full = (1 << N) - 1
    if not can(full, 4):
        raise RuntimeError("4-guess tree not found")

    nodes, ids = [], {}

    def visit(state, depth):
        key = state, depth
        if key in ids:
            return ids[key]

        gi = CHOICE[key]
        node = len(nodes)
        ids[key] = node
        nodes.append([P[gi], {}])

        for p, mask in enumerate(M[gi]):
            if p != GGGG and (child := state & mask):
                nodes[node][1][text(p)] = visit(child, depth - 1)

        return node

    visit(full, 4)
    return nodes

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "primewordletree.json"
    Path(out).write_text(json.dumps(build(), separators=(",", ":")))
    print(f"wrote {out}")