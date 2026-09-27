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
    r, left = [0] * 4, list(t)
    for i in range(4):
        if g[i] == t[i]:
            r[i], left[i] = 2, None
    for i in range(4):
        if not r[i] and g[i] in left:
            r[i], left[left.index(g[i])] = 1, None
    return sum(r[i] * 3 ** i for i in range(4))


def text(p):
    s = ""
    for _ in range(4):
        s += "nyg"[p % 3]
        p //= 3
    return s


P = primes()
N = len(P)
M = [[0] * PATTERNS for _ in range(N)]

for gi, g in enumerate(P):
    for ti, t in enumerate(P):
        M[gi][feedback(g, t)] |= 1 << ti

CHOICE = {}
inf = float('inf')


@lru_cache(None)
def minn(state, depth):
    statebitcount = state.bit_count()
    if statebitcount == 0:
        return 0
    if statebitcount == 1:
        gi = (state & -state).bit_length() - 1
        CHOICE[state, depth] = gi
        return 1
    if depth == 0:
        return inf

    candidates = []
    for gi in range(N):
        sq_sum = 0
        for mask in M[gi]:
            c = (state & mask).bit_count()
            if c:
                sq_sum += c * c

        in_state = 1 if (state & (1 << gi)) else 0
        candidates.append((sq_sum, -in_state, gi))

    candidates.sort()

    eval_candidates = [gi for _, _, gi in candidates[:20]]

    best = inf
    best_gi = -1

    for information in eval_candidates:
        curr_cost = statebitcount
        possible = True

        for p, trinary_mask in enumerate(M[information]):
            if p == GGGG:
                continue
            sub_tree = state & trinary_mask
            if not sub_tree:
                continue

            cost = minn(sub_tree, depth - 1)
            if cost == inf:
                possible = False
                break

            curr_cost += cost
            if curr_cost >= best:
                possible = False
                break

        if possible and curr_cost < best:
            best = curr_cost
            best_gi = information

    if best_gi != -1:
        CHOICE[state, depth] = best_gi

    return best


def build():
    full = (1 << N) - 1
    total_guesses = minn(full, 4)
    if total_guesses == inf:
        raise RuntimeError("gekackt")

    print(f"total: {total_guesses}, average: {total_guesses / N:.3f}")

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