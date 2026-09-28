// Prime Wordle solver
//
// Wordle, but the answer is a 4-digit prime. Feedback works the same way
// (green / yellow / grey). I want the strategy that minimises the TOTAL number
// of guesses if you try every prime as the answer once (so same as the average).
//
// How it works:
//   - work out the feedback for every (guess, answer) pair up front
//   - solve(candidates, guesses left): try every guess, split the candidates by
//     the feedback they'd give, then solve each group with one fewer guess left
//   - that's way too slow if you do it naively, so:
//       * remember results we've already computed (memo)
//       * lower bound: skip guesses that can't beat the best one found so far
//       * try the most promising guesses first so we find good ones early

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <cmath>
using namespace std;

const int DIGITS = 4;
const int MAX_GUESSES = 4;
const int INF = 1e9;

int PATTERNS = 1;              // 3^DIGITS (aka 81)
int GREEN;                     // pattern number for all green
int N;                         // how many primes
int maxBranches;               // most different feedbacks any one guess gives (not counting green)
vector<int> primes;
vector<string> words;
vector<vector<int>> fb;        // fb[guess][answer] = feedback pattern as a base 3 number

// 0 = grey, 1 = yellow, 2 = green
int getFeedback(const string& guess, const string& answer) {
    int res[DIGITS] = {};
    bool used[DIGITS] = {};

    // greens first
    for (int i = 0; i < DIGITS; i++) {
        if (guess[i] == answer[i]) {
            res[i] = 2;
            used[i] = true;
        }
    }
    // then yellows, each digit in the answer can only be matched once
    for (int i = 0; i < DIGITS; i++) {
        if (res[i] != 0) continue;
        for (int j = 0; j < DIGITS; j++) {
            if (!used[j] && guess[i] == answer[j]) {
                res[i] = 1;
                used[j] = true;
                break;
            }
        }
    }

    int p = 0, mult = 1;
    for (int i = 0; i < DIGITS; i++) {
        p += res[i] * mult;
        mult *= 3;
    }
    return p;
}

string patternToString(int p) {
    string s = "";
    for (int i = 0; i < DIGITS; i++) {
        s += "nyg"[p % 3];
        p /= 3;
    }
    return s;
}

// TIP Lower bound on the total guesses for m candidates when we have d guesses left.
// Best case: the first guess is right (1 candidate, cost 1), then it splits the rest into
// maxBranches groups that are each solved by the second guess, then maxBranches^2 on
// the third guess, and so on. Fill the early levels as much as possible.
// Returns INF if they can't fit in d guesses at all.
int lowerBound(int m, int d) {
    if (m == 0) return 0;
    long long left = m;
    long long total = 0;
    long long capacity = 1;
    for (int level = 1; level <= d && left > 0; level++) {
        long long take = min(left, capacity);
        total += take * level;
        left -= take;
        capacity = min(capacity * maxBranches, 1000000000LL);
    }
    if (left > 0) return INF;
    return (int)total;
}

// memo: for each (candidate set, guesses left) remember the best cost and which guess did it.
// bound = we know the cost is at least this (from searches that gave up)
struct Result {
    int exact = -1;
    int bound = 0;
    int guess = -1;
};
map<vector<int>, Result> memo[MAX_GUESSES + 1];

// split candidates into groups by what feedback guess g would give
vector<vector<int>> splitByFeedback(const vector<int>& cands, int g) {
    vector<vector<int>> groups(PATTERNS);
    for (int t : cands) {
        groups[fb[g][t]].push_back(t);
    }
    return groups;
}

// Minimum total guesses to solve all of cands with at most d guesses.
// If the answer is >= limit we don't care how much bigger, just return limit.
int solve(const vector<int>& cands, int d, int limit) {
    int m = cands.size();
    if (m == 0) return 0;
    if (m == 1) return 1;
    if (d <= 1) return limit;          // one guess left but more than one candidate

    int lb = lowerBound(m, d);
    if (lb >= limit) return limit;

    // have we seen this one before?
    auto it = memo[d].find(cands);
    if (it != memo[d].end()) {
        if (it->second.exact >= 0) return min(it->second.exact, limit);
        if (it->second.bound >= limit) return limit;
    }

    // look at every guess and figure out the best case for each one
    struct Option {
        int optimistic;    // best case cost if the guess splits things nicely
        long long sumSq;   // sum of squares of group sizes, smaller = more even split
        int guess;
    };
    vector<Option> options;
    for (int g = 0; g < N; g++) {
        vector<vector<int>> groups = splitByFeedback(cands, g);
        int optimistic = m;   // everyone needs at least this guess
        long long sumSq = 0;
        bool possible = true;
        for (int p = 0; p < GREEN; p++) {
            int size = groups[p].size();
            if (size == 0) continue;
            int b = lowerBound(size, d - 1);
            if (b >= INF) {
                possible = false;
                break;
            }
            optimistic += b;
            sumSq += (long long)size * size;
        }
        if (possible && optimistic < limit) {
            options.push_back({optimistic, sumSq, g});
        }
    }

    // most promising first
    sort(options.begin(), options.end(), [](const Option& a, const Option& b) {
        if (a.optimistic != b.optimistic) return a.optimistic < b.optimistic;
        if (a.sumSq != b.sumSq) return a.sumSq < b.sumSq;
        return a.guess < b.guess;
    });

    int best = limit;
    int bestGuess = -1;

    for (const Option& opt : options) {
        if (opt.optimistic >= best) break;    // sorted, so nothing after this can win either

        vector<vector<int>> groups = splitByFeedback(cands, opt.guess);

        // cost so far = m (one guess each) + extra guesses needed inside each group.
        // start with the optimistic numbers and swap in the real ones one group at a time.
        int total = opt.optimistic;
        bool worse = false;

        // biggest groups first, they're the ones most likely to be too expensive
        vector<int> order;
        for (int p = 0; p < GREEN; p++) {
            if (groups[p].size() >= 2) order.push_back(p);
        }
        sort(order.begin(), order.end(), [&](int a, int b) {
            return groups[a].size() > groups[b].size();
        });

        for (int p : order) {
            int guess_lb = lowerBound(groups[p].size(), d - 1);
            int allowed = best - (total - guess_lb);   // this group has to cost less than this
            if (allowed <= guess_lb) { worse = true; break; }
            int cost = solve(groups[p], d - 1, allowed);
            if (cost >= allowed) { worse = true; break; }
            total += cost - guess_lb;
        }

        if (!worse && total < best) {
            best = total;
            bestGuess = opt.guess;
        }
    }

    if (best < limit) {
        memo[d][cands].exact = best;
        memo[d][cands].guess = bestGuess;
    } else {
        memo[d][cands].bound = max(memo[d][cands].bound, limit);
    }
    return best;
}

// The tree we output. Each node has a guess and one child per possible feedback.
struct Node {
    int guess;
    vector<pair<int, int>> children;   // (feedback pattern, index of child node)
};
vector<Node> tree;

// walk back through the memo to build the actual tree
int buildTree(const vector<int>& cands, int d) {
    int m = cands.size();
    int guess;
    if (m == 1) {
        guess = cands[0];
    } else {
        // the memo only has entries for things solve() finished properly, so if this one
        // is missing just solve it again (no limit this time)
        if (memo[d][cands].exact < 0) solve(cands, d, INF);
        guess = memo[d][cands].guess;
    }

    int me = tree.size();
    tree.push_back({guess, {}});

    vector<vector<int>> groups = splitByFeedback(cands, guess);
    for (int p = 0; p < GREEN; p++) {
        if (groups[p].empty()) continue;
        int child = buildTree(groups[p], d - 1);
        tree[me].children.push_back({p, child});
    }
    return me;
}

int main(int argc, char** argv) {
    string outputFile = "primewordletree_optimal.json";
    if (argc > 1) outputFile = argv[1];

    // 4 digit primes
    for (int i = 0; i < DIGITS; i++) PATTERNS *= 3;
    GREEN = PATTERNS - 1;
    int low = 1;
    for (int i = 1; i < DIGITS; i++) low *= 10;
    for (int n = low; n < low * 10; n++) {
        if (n < 2) continue;
        bool isPrime = true;
        for (int d = 2; d * d <= n; d++) {
            if (n % d == 0) { isPrime = false; break; }
        }
        if (isPrime) {
            primes.push_back(n);
            words.push_back(to_string(n));
        }
    }
    N = primes.size();
    cout << "There are " << N << " primes" << endl;

    // feedback for every pair
    fb.assign(N, vector<int>(N));
    maxBranches = 0;
    for (int g = 0; g < N; g++) {
        vector<bool> seen(PATTERNS, false);
        for (int t = 0; t < N; t++) {
            fb[g][t] = getFeedback(words[g], words[t]);
            seen[fb[g][t]] = true;
        }
        int count = 0;
        for (int p = 0; p < GREEN; p++) if (seen[p]) count++;   // don't count green
        maxBranches = max(maxBranches, count);
    }
    cout << "Max branches: " << maxBranches << endl;

    vector<int> all(N);
    for (int i = 0; i < N; i++) all[i] = i;

    cout << "computing..." << endl;
    int optimum = solve(all, MAX_GUESSES, INF);
    if (optimum >= INF) {
        cout << "what the helly " << INF << endl;
        return 1;
    }
    cout << "total: " << optimum << " (average " << (double)optimum / N << ")" << endl;

    buildTree(all, MAX_GUESSES);

    // check the tree by playing every prime through it
    long long total = 0;
    vector<int> howMany(MAX_GUESSES + 1, 0);
    for (int t = 0; t < N; t++) {
        int node = 0;
        int solvedAt = -1;
        for (int step = 1; step <= MAX_GUESSES; step++) {
            int g = tree[node].guess;
            if (g == t) { solvedAt = step; break; }
            int p = fb[g][t];
            int next = -1;
            for (auto& c : tree[node].children) {
                if (c.first == p) next = c.second;
            }
            if (next == -1) break;
            node = next;
        }
        if (solvedAt == -1) {
            cout << "Tree failed on " << primes[t] << "!!" << endl;
            return 1;
        }
        total += solvedAt;
        howMany[solvedAt]++;
    }
    cout << "Checked total: " << total << (total == optimum ? " (matches)" : " (DOES NOT MATCH)") << endl;
    for (int i = 1; i <= MAX_GUESSES; i++) {
        cout << "  solved in " << i << ": " << howMany[i] << endl;
    }

    // [ [guess, {pattern: child}], ... ]
    ofstream out(outputFile);
    out << "[";
    for (int i = 0; i < (int)tree.size(); i++) {
        if (i > 0) out << ",";
        out << "[\"" << primes[tree[i].guess] << "\",{";
        for (int j = 0; j < (int)tree[i].children.size(); j++) {
            if (j > 0) out << ",";
            out << "\"" << patternToString(tree[i].children[j].first) << "\":" << tree[i].children[j].second;
        }
        out << "}]";
    }
    out << "]";
    cout << "tree size (like the hit program) is " << tree.size() << ", into " << outputFile << endl;
    return 0;
}