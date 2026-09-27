import json
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "primewordletree.json"
with open(path) as f:
    tree = json.load(f)

node = 0
while True:
    guess, next_ = tree[node]
    result = input(f"{guess} ").strip().lower()
    if result == "gggg": break
    node = next_[result]
