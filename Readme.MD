 Prime Wordle solver

Wordle, but the answer is a 4-digit prime. feedback works the same way\
(green / yellow / grey). we want the strategy that minimises the TOTAL number\
of guesses if you try every prime as the answer once (so same as the average).

How it works:
- work out the feedback for every (guess, answer) pair up front
- solve(candidates, guesses left): try every guess, split the candidates by\
   the feedback they'd give, then solve each group with one fewer guess left
- that's way too slow if you try and brute force it, so:
  * remember results we've already computed (memo)
  * skip guesses that can't beat the best one found so far
  * try the most promising guesses first so we find good ones early

the python code is 440 bytes but runs slightly worse.\
the c code is pretty good but MIGHT segfault :D
the c++ code is the GOAT and finds the optimal tree, with an average of 3.3808 guesses.
primesolveruser.py takes the json and runs it.

### build:
python: just run lmao its python\
c: `cmake --build ./cmake-build-debug --target primesolver -j 10`
c++: `g++ -O2 -std=c++17 main.cpp -o primesolver_deluxe`