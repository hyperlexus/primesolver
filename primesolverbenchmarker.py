import json
import sys
from math import isqrt

path = sys.argv[1] if len(sys.argv) > 1 else "primewordletree.json"
with open(path) as f:
    tree = json.load(f)

node = 0

def primes():
    return [n for n in range(1000, 10000)
            if all(n % d for d in range(2, isqrt(n) + 1))]

def guess_result(prime: int, guess: int) -> str:
    kot = [""]*4
    prime_list: list = list(str(prime))
    guess_list: list = list(str(guess))
    for idx, i in enumerate(prime_list):
        if i in guess_list[idx]:
            kot[idx] = "g"
            prime_list[idx] = "-1"
            guess_list[idx] = "-1"

    for idx2, i2 in enumerate(guess_list):
        if guess_list[idx2] != "-1":
            try:
                index_of_in_python = prime_list.index(i2)
            except ValueError:
                index_of_in_python = -1
            if index_of_in_python != -1:
                kot[idx2] = "y"
                prime_list[index_of_in_python] = "-1"
            else:
                kot[idx2] = "n"
    return "".join(kot)

prime_list = primes()
prime_fellas = {}

for prime in prime_list:
    counter = 0
    while "fella!":
        guess, next_ = tree[node]
        result = guess_result(prime, guess)
        if result == "gggg":
            prime_fellas[prime] = counter
            break
        node = next_[result]
        counter += 1
    node = 0

print(prime_fellas)
print(sum([value + 1 for value in prime_fellas.values()]))

splist = [0] * 5

for i in prime_fellas:
    splist[prime_fellas[i]] += 1
print(splist)
print(sum([value + 1 for value in prime_fellas.values()]) / len(prime_fellas.items()))