// prime wordle solver
// goal: find the decision tree that minimises the TOTAL number of guesses
// over every prime with DIGITS digits (so 4-digit primes by default).
//
// build: g++ -O3 -std=c++20 -fopenmp prime_wordle.cpp -o pw
// usage: ./pw [output.json]
//
// rough idea (took me a while to get this straight):
//   - precompute feedback for every (guess, target) pair
//   - do a depth-limited search over sets of remaining candidates
//   - prune HARD using a lower bound (can't beat the "perfect splitting" cost)
//   - depth 2 has its own special solver because it's basically a distinctness check

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <omp.h>
using namespace std;

#ifndef DIGITS
#define DIGITS 4
#endif
#ifndef MAX_DEPTH_CFG
#define MAX_DEPTH_CFG 4
#endif
#ifndef CACHE_BITS
#define CACHE_BITS 20
#endif

static constexpr int MAX_DEPTH = MAX_DEPTH_CFG;
static constexpr int INF = 1'000'000'000;
constexpr int pow3(const int n) { return n == 0 ? 1 : 3 * pow3(n - 1); }
static constexpr int PATTERNS = pow3(DIGITS);   // 3^DIGITS possible feedback patterns
static constexpr int GREEN = PATTERNS - 1;      // all-green = you got it
static_assert(PATTERNS <= 128, "pattern set must fit in 128 bits");  // needed for the u128 trick
static_assert(MAX_DEPTH >= 3, "MAX_DEPTH must be >= 3");

using u16 = uint16_t;
using u128 = unsigned __int128;

static int N;                       // number of primes
static int maxPat;                  // most distinct patterns any single guess can produce
static vector<int> primes;
static vector<uint8_t> fb;          // fb[g*N + t] = feedback pattern of guess g vs target t
static vector<vector<int>> lb;      // lb[d][m] = lower bound on cost of m candidates in d guesses

static const uint8_t* frow(int g) { return &fb[static_cast<size_t>(g) * N]; }

// standard wordle feedback (0 = no, 1 = yellow, 2 = green), packed in base 3
// greens first, then yellows so duplicate digits don't get double counted
static int feedback(const string& guess, const string& target) {
    int res[DIGITS] = {};
    bool used[DIGITS] = {};
    for (int i = 0; i < DIGITS; ++i)
        if (guess[i] == target[i]) { res[i] = 2; used[i] = true; }
    for (int i = 0; i < DIGITS; ++i) {
        if (res[i]) continue;
        for (int j = 0; j < DIGITS; ++j)
            if (!used[j] && guess[i] == target[j]) { res[i] = 1; used[j] = true; break; }
    }
    int p = 0, mul = 1;
    for (const int r : res) { p += mul * r; mul *= 3; }
    return p;
}

static string pattern_text(int p) {
    string r;
    for (int i = 0; i < DIGITS; ++i) { r += "nyg"[p % 3]; p /= 3; }
    return r;
}

// ---------------------------------------------------------------- hashing
// splitmix64, copied from the internet. i have no idea why it works but it scrambles bits nicely
static uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ x >> 30) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ x >> 27) * 0x94d049bb133111ebULL;
    return x ^ x >> 31;
}

// hash a set of candidates (order matters, but we always keep them sorted-ish so it's fine)
static uint64_t hash_set(const u16* s, const int m, const uint64_t seed) {
    uint64_t h = seed;
    for (int i = 0; i < m; ++i) h = mix64(h ^ static_cast<uint64_t>(s[i] + 1));
    return mix64(h ^ static_cast<uint64_t>(m));
}

// two independent 64-bit hashes so collisions are basically impossible
// (we never store the actual set, just trust this)
struct Key128 {
    uint64_t a, b;
    bool operator==(const Key128& o) const { return a == o.a && b == o.b; }
};
struct Key128Hash {
    size_t operator()(const Key128& k) const noexcept { return k.a ^ (k.b * 0x9e3779b97f4a7c15ULL); }
};
static Key128 make_key(const u16* s, int m, int d) {
    return { hash_set(s, m, 0x1234567ULL + d), hash_set(s, m, 0xabcdef99ULL * (d + 1)) };
}

// ---------------------------------------------------------------- shared memo table
// exact = proven optimal cost (-1 if we don't know yet)
// lb    = best proven lower bound (from failed searches)
// guess = the guess that achieves exact
struct MemoEntry { int exact = -1; int lb = 0; int guess = -1; };
static unordered_map<Key128, MemoEntry, Key128Hash> memo;
static mutex memo_mutex;   // one big lock, not pretty but it works

static bool memo_get(const Key128& k, MemoEntry& e) {
    lock_guard<mutex> lk(memo_mutex);
    const auto it = memo.find(k);
    if (it == memo.end()) return false;
    e = it->second;
    return true;
}
static void memo_put_exact(const Key128& k, int cost, int guess) {
    lock_guard<mutex> lk(memo_mutex);
    MemoEntry& e = memo[k];
    e.exact = cost;
    e.guess = guess;
}
static void memo_put_lb(const Key128& k, int lb) {
    lock_guard<mutex> lk(memo_mutex);
    MemoEntry& e = memo[k];
    e.lb = max(e.lb, lb);
}

// ---------------------------------------------------------------- per-thread stuff
// small direct-mapped cache for the depth-2 solver, no locking needed since each thread has its own
struct Cache2Entry { uint64_t tag; int32_t cost; int32_t guess; };
struct Ctx {
    vector<Cache2Entry> cache;
    vector<uint8_t> mark;      // scratch array, used to skip guesses that are in the state
    uint64_t calls = 0;
    Ctx() : cache(static_cast<size_t>(1) << CACHE_BITS, Cache2Entry{0, 0, 0}), mark(N, 0) {}
};
static vector<Ctx> contexts;

// ---------------------------------------------------------------- progress printing
static atomic<int> roots_done{0};
static atomic<int> global_best{INF};
static int best_root = -1;
static int roots_total = 0;
static mutex print_mutex;
static mutex best_mutex;
static atomic<long long> next_print_ms{0};
static chrono::steady_clock::time_point start_time;

static long long elapsed_ms() {
    return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start_time).count();
}

// prints at most once per second (unless force)
static void maybe_print(const bool force = false) {
    const long long now = elapsed_ms();
    long long np = next_print_ms.load();
    if (!force) {
        if (now < np) return;
        if (!next_print_ms.compare_exchange_strong(np, now + 1000)) return;
    }
    uint64_t calls = 0;
    for (auto& c : contexts) calls += c.calls;
    lock_guard<mutex> lk(print_mutex);
    int best = global_best.load();
    cerr << "\rroots " << roots_done.load() << "/" << roots_total
         << " | best " << (best >= INF ? -1 : best);
    if (best < INF) cerr << " (avg " << fixed << setprecision(6) << (double)best / N << ")";
    cerr << " | solves " << calls << " | " << fixed << setprecision(1) << now / 1000.0 << "s    " << flush;
}

// ---------------------------------------------------------------- lower bounds
// if a guess can split things into at most B non-green groups, then a tree of depth d
// can solve at most 1 answer on level 1, B on level 2, B^2 on level 3, ...
// so the cheapest possible thing is to cram as many answers into the early levels as we can.
// lb[d][m] = that cheapest total, or INF if m answers just can't fit in d levels.
static void build_lb(int B) {
    lb.assign(MAX_DEPTH + 1, vector<int>(N + 1, INF));
    for (int d = 1; d <= MAX_DEPTH; ++d) {
        lb[d][0] = 0;
        for (int m = 1; m <= N; ++m) {
            long long remaining = m, cost = 0, cap = 1;
            for (int level = 1; level <= d && remaining > 0; ++level) {
                long long take = min(remaining, cap);
                cost += take * level;
                remaining -= take;
                cap = min<long long>(cap * B, 1'000'000'000LL);   // clamp so it doesn't overflow
            }
            if (remaining == 0) lb[d][m] = (int)cost;
        }
    }
}

// ---------------------------------------------------------------- depth-2 solver
// with only 2 guesses left, every class after the first guess has to be a single word.
// so we need a guess where all feedbacks are different (no two targets share a pattern).
//   guess is one of the candidates     -> cost 2m-1  (it gets itself in 1 guess, rest in 2)
//   guess is not a candidate           -> cost 2m
// the u128 is a bitset over the patterns, that's why PATTERNS <= 128
static int solve2_compute(const u16* s, int m, Ctx& ctx, int& guess) {
    guess = -1;
    if (m > maxPat) return INF;   // pigeonhole, can't possibly work

    // try the somewhat cheaper guesses from inside the state first
    for (int i = 0; i < m; ++i) {
        int g = s[i];
        const uint8_t* row = frow(g);
        u128 seen = 0;
        bool ok = true;
        for (int j = 0; j < m; ++j) {
            u128 bit = (u128)1 << row[s[j]];
            if (seen & bit) { ok = false; break; }
            seen |= bit;
        }
        if (ok) { guess = g; return 2 * m - 1; }
    }

    // okay, nothing inside works, try everything else
    for (int j = 0; j < m; ++j) ctx.mark[s[j]] = 1;
    int result = INF;
    for (int g = 0; g < N; ++g) {
        if (ctx.mark[g]) continue;
        const uint8_t* row = frow(g);
        u128 seen = 0;
        bool ok = true;
        for (int j = 0; j < m; ++j) {
            u128 bit = (u128)1 << row[s[j]];
            if (seen & bit) { ok = false; break; }
            seen |= bit;
        }
        if (ok) { guess = g; result = 2 * m; break; }
    }
    for (int j = 0; j < m; ++j) ctx.mark[s[j]] = 0;   // cleanup
    return result;
}

// ez cases
static int solve2(const u16* s, int m, Ctx& ctx, int* guess = nullptr) {
    if (m == 0) { if (guess) *guess = -1; return 0; }
    if (m == 1) { if (guess) *guess = s[0]; return 1; }
    if (m == 2) { if (guess) *guess = s[0]; return 3; }   // guess one, if wrong then the other
    uint64_t h = hash_set(s, m, 0x51ed270b0ULL) | 1;      // |1 so tag is never 0 (0 = empty slot)
    Cache2Entry& e = ctx.cache[h >> (64 - CACHE_BITS)];
    if (e.tag == h) { if (guess) *guess = e.guess; return e.cost; }
    int g;
    int c = solve2_compute(s, m, ctx, g);
    e.tag = h; e.cost = c; e.guess = g;
    if (guess) *guess = g;
    return c;
}

static int solve(const u16* s, int m, int d, int limit, Ctx& ctx);

// quick optimistic estimate for guess g (no recursion!): m + sum of lower bounds of each class.
// returns false if some class is too big to ever fit in d-1 guesses.
// sq = sum of squared class sizes, used as a tiebreaker (smaller = more even split)
static bool scan_guess(const u16* s, int m, int g, int d, int& lower, long long& sq) {
    const uint8_t* row = frow(g);
    uint16_t cnt[PATTERNS] = {};
    for (int j = 0; j < m; ++j) cnt[row[s[j]]]++;
    lower = m; sq = 0;
    bool feas = true;
    for (int j = 0; j < m; ++j) {
        int p = row[s[j]];
        int c = cnt[p];
        if (!c) continue;    // already handled this pattern
        cnt[p] = 0;
        if (p == GREEN) continue;
        int l = lb[d - 1][c];
        if (l >= INF) feas = false; else lower += l;
        sq += (long long)c * c;
    }
    return feas;
}

// real cost of playing g at this node. exact if it's < limit, otherwise just returns limit
// (meaning "not good enough, don't care how bad")
static int eval_guess(const u16* s, int m, int g, int d, int limit, Ctx& ctx) {
    const uint8_t* row = frow(g);

    // counting sort the candidates into their feedback classes
    int cnt[PATTERNS] = {};
    for (int j = 0; j < m; ++j) cnt[row[s[j]]]++;
    int pos[PATTERNS];
    int run = 0;
    for (int p = 0; p < GREEN; ++p) { pos[p] = run; run += cnt[p]; }
    vector<u16> buf(run > 0 ? run : 1);
    {
        int fill[PATTERNS];
        memcpy(fill, pos, sizeof(fill));
        for (int j = 0; j < m; ++j) {
            int p = row[s[j]];
            if (p == GREEN) continue;    // the guess itself, already solved
            buf[fill[p]++] = s[j];
        }
    }

    struct Cls { int off, size, lb; };
    vector<Cls> cls;
    int lbsum = m;   // start at m because every candidate costs at least this one guess
    for (int p = 0; p < GREEN; ++p) {
        int c = cnt[p];
        if (!c) continue;
        int l = lb[d - 1][c];
        if (l >= INF) return limit;
        lbsum += l;
        if (c >= 2) cls.push_back({pos[p], c, l});   // singletons are already exact (cost 1 extra)
    }
    if (lbsum >= limit) return limit;

    // biggest classes first -> fast fail
    sort(cls.begin(), cls.end(), [](const Cls& a, const Cls& b) {
        return a.size != b.size ? a.size > b.size : a.off < b.off;
    });

    for (const Cls& c : cls) {
        int rem = limit - (lbsum - c.lb);    // this class has to cost strictly less than this
        if (rem <= c.lb) return limit;
        int cost = solve(&buf[c.off], c.size, d - 1, rem, ctx);
        if (cost >= rem) return limit;
        lbsum += cost - c.lb;    // swap the optimistic estimate for the real cost
    }
    return lbsum;
}

// returns the exact optimum if it's < limit, else returns limit
static int solve(const u16* s, int m, int d, int limit, Ctx& ctx) {
    ++ctx.calls;
    if (m == 0) return 0;
    if (m == 1) return 1;
    if (d <= 1) return limit;    // 1 guess left and more than 1 candidate = impossible
    if (d == 2) { int c = solve2(s, m, ctx); return c < limit ? c : limit; }

    int lb0 = lb[d][m];
    if (lb0 >= limit) return limit;

    if ((ctx.calls & 0x3FFF) == 0) maybe_print();

    Key128 key = make_key(s, m, d);
    {
        MemoEntry e;
        if (memo_get(key, e)) {
            if (e.exact >= 0) return e.exact < limit ? e.exact : limit;
            if (e.lb >= limit) return limit;
        }
    }

    int best = limit, bestg = -1;

    // a depth-2 tree is also a valid depth-d tree. if it hits the lower bound we can stop right here
    if (m <= maxPat) {
        int g2;
        int c2 = solve2(s, m, ctx, &g2);
        if (c2 == lb0) {
            memo_put_exact(key, c2, g2);
            return c2 < limit ? c2 : limit;
        }
        if (c2 < best) { best = c2; bestg = g2; }
    }

    // score every guess cheaply, then only fully evaluate the promising ones (in order)
    struct Cand { int lower; long long sq; int g; };
    vector<Cand> cands;
    cands.reserve(N);
    for (int g = 0; g < N; ++g) {
        int lower; long long sq;
        if (scan_guess(s, m, g, d, lower, sq) && lower < best) cands.push_back({lower, sq, g});
    }
    sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.lower != b.lower) return a.lower < b.lower;
        if (a.sq != b.sq) return a.sq < b.sq;
        return a.g < b.g;
    });

    for (const Cand& c : cands) {
        if (c.lower >= best) break;    // sorted, so everything after is worse too
        int cost = eval_guess(s, m, c.g, d, best, ctx);
        if (cost < best) { best = cost; bestg = c.g; }
    }

    if (best < limit) memo_put_exact(key, best, bestg);
    else memo_put_lb(key, limit);    // failed, so at least `limit` is needed
    return best;
}

// ---------------------------------------------------------------- rebuilding the tree
// the search only gives us a number, so walk back through the memo to get the actual tree
struct TreeNode { int guess; vector<pair<int, int>> edges; };   // edges: (pattern, child node)
static vector<TreeNode> tree_nodes;
static unordered_map<Key128, int, Key128Hash> tree_ids;    // reuse identical subtrees

static int build_tree(const vector<u16>& S, int d, Ctx& ctx) {
    int m = (int)S.size();
    Key128 key = make_key(S.data(), m, d);
    auto it = tree_ids.find(key);
    if (it != tree_ids.end()) return it->second;

    int guess = -1;
    if (m == 1) {
        guess = S[0];
    } else if (d == 2) {
        int c = solve2(S.data(), m, ctx, &guess);
        if (c >= INF) throw runtime_error("depth-2 state infeasible during reconstruction");
    } else {
        MemoEntry e;
        if (!memo_get(key, e) || e.exact < 0) {
            // not in the memo (or only a bound), just solve it again with no limit
            int c = solve(S.data(), m, d, INF, ctx);
            if (c >= INF) throw runtime_error("reconstruction failed");
            memo_get(key, e);
        }
        if (e.exact < 0 || e.guess < 0) throw runtime_error("missing optimal choice");
        guess = e.guess;
    }

    int node = (int)tree_nodes.size();
    tree_ids.emplace(key, node);
    tree_nodes.push_back({guess, {}});

    const uint8_t* row = frow(guess);
    vector<vector<u16>> cls(PATTERNS);
    for (u16 t : S) {
        int p = row[t];
        if (p != GREEN) cls[p].push_back(t);
    }
    for (int p = 0; p < GREEN; ++p) {
        if (cls[p].empty()) continue;
        int child = build_tree(cls[p], d - 1, ctx);
        tree_nodes[node].edges.emplace_back(p, child);
    }
    return node;
}

int main(int argc, char** argv) {
    string output = argc > 1 ? argv[1] : "primewordletree_optimal.json";

    // ---- generate the primes (slow trial division is fine, it's only 4 digits)
    int lo = 1, hi;
    for (int i = 1; i < DIGITS; ++i) lo *= 10;
    hi = lo * 10 - 1;
    for (int n = lo; n <= hi; ++n) {
        bool prime = n > 1;
        for (int d = 2; d * d <= n; ++d) if (n % d == 0) { prime = false; break; }
        if (prime) primes.push_back(n);
    }
    N = static_cast<int>(primes.size());
    cerr << "Primes: " << N << "\n";

    // ---- feedback table (N*N bytes, ~1MB for 4 digits so whatever)
    fb.assign(static_cast<size_t>(N) * N, 0);
    vector<string> str(N);
    for (int i = 0; i < N; ++i) str[i] = to_string(primes[i]);
    maxPat = 0;
    for (int g = 0; g < N; ++g) {
        vector<bool> seen(PATTERNS, false);
        for (int t = 0; t < N; ++t) {
            int p = feedback(str[g], str[t]);
            fb[(size_t)g * N + t] = (uint8_t)p;
            seen[p] = true;
        }
        int c = 0;
        for (bool x : seen) c += x;
        maxPat = max(maxPat, c);
    }
    build_lb(maxPat - 1);   // -1 because green doesn't count as a branch

    int threads = omp_get_max_threads();
    contexts.resize(threads);

    vector<u16> full(N);
    for (int i = 0; i < N; ++i) full[i] = (u16)i;

    // ---- rank all the possible first guesses by their optimistic score
    struct Root { int lower; long long sq; int g; };
    vector<Root> roots;
    for (int g = 0; g < N; ++g) {
        int lower; long long sq;
        if (scan_guess(full.data(), N, g, MAX_DEPTH, lower, sq)) roots.push_back({lower, sq, g});
    }
    ranges::sort(roots, [](const Root& a, const Root& b) {
        if (a.lower != b.lower) return a.lower < b.lower;
        if (a.sq != b.sq) return a.sq < b.sq;
        return a.g < b.g;
    });
    roots_total = static_cast<int>(roots.size());
    cerr << "Root guesses to examine: " << roots_total << "\n";
    cerr << "Searching...\n";
    start_time = chrono::steady_clock::now();

    // ---- the actual search, one root guess per thread at a time
    #pragma omp parallel for schedule(dynamic, 1)
    for (int i = 0; i < roots_total; ++i) {
        int incumbent = global_best.load();
        if (roots[i].lower < incumbent) {    // otherwise it can't beat what we already have
            Ctx& ctx = contexts[omp_get_thread_num()];
            int cost = eval_guess(full.data(), N, roots[i].g, MAX_DEPTH, incumbent, ctx);
            if (cost < global_best.load()) {
                lock_guard<mutex> lk(best_mutex);
                if (cost < global_best.load()) {    // check again, another thread might have beaten us
                    global_best.store(cost);
                    best_root = roots[i].g;
                }
            }
        }
        ++roots_done;
        maybe_print();
    }
    maybe_print(true);

    int optimum = global_best.load();
    cerr << "\n\n";
    if (optimum >= INF) { cerr << "No tree exists within depth " << MAX_DEPTH << ".\n"; return 1; }

    // ---- rebuild the tree from the memo
    Ctx& ctx0 = contexts[0];
    memo_put_exact(make_key(full.data(), N, MAX_DEPTH), optimum, best_root);
    build_tree(full, MAX_DEPTH, ctx0);

    // ---- sanity check: actually play every prime through the tree and see if it works
    vector<int> dist(MAX_DEPTH + 1, 0);
    long long total = 0;
    for (int t = 0; t < N; ++t) {
        int node = 0, solved = -1;
        for (int step = 1; step <= MAX_DEPTH; ++step) {
            int g = tree_nodes[node].guess;
            if (g == t) { solved = step; break; }
            int p = fb[(size_t)g * N + t], next = -1;
            for (auto& e : tree_nodes[node].edges) if (e.first == p) { next = e.second; break; }
            if (next < 0) break;
            node = next;
        }
        if (solved < 0) throw runtime_error("verification: prime " + to_string(primes[t]) + " not solved");
        total += solved; ++dist[solved];
    }
    cerr << "Verified total: " << total << " (average " << fixed << setprecision(12) << (double)total / N << ")\n";
    if (total != optimum) throw runtime_error("verified total != proven optimum");
    for (int d = 1; d <= MAX_DEPTH; ++d)
        cerr << "  " << d << " guess" << (d == 1 ? " " : "es") << ": " << dist[d] << " primes\n";

    // ---- dump to json: [ [guess, {pattern: childIndex, ...}], ... ]
    ofstream out(output);
    if (!out) { cerr << "Could not open " << output << "\n"; return 1; }
    out << '[';
    for (size_t i = 0; i < tree_nodes.size(); ++i) {
        if (i) out << ',';
        out << "[\"" << primes[tree_nodes[i].guess] << "\",{";
        for (size_t j = 0; j < tree_nodes[i].edges.size(); ++j) {
            if (j) out << ',';
            out << "\"" << pattern_text(tree_nodes[i].edges[j].first) << "\":" << tree_nodes[i].edges[j].second;
        }
        out << "}]";
    }
    out << ']';
    cerr << "Tree nodes: " << tree_nodes.size() << "\nWrote: " << output << "\n";
    return 0;
}