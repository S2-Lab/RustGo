#!/usr/bin/env python3
"""
mergeJaccard.py

Usage:
    python3 mergeJaccard.py input.txt output.txt [--jaccard J] [--size-diff D]

Reads lines of the form:
    [0] : 1 2 3 4
    [1] : 1 2 4 5 6 7
and merges any two entries whose Jaccard similarity >= J and whose size-difference <= D.
Outputs the merged groups in the same “[id] : elems...” format.
"""

import argparse
import re
import sys

def jaccard_similarity(a: set, b: set) -> float:
    inter = a & b
    uni = a | b
    if not uni:
        return 0.0
    return len(inter) / len(uni)

def block_count_diff(a: set, b: set) -> int:
    return abs(len(a) - len(b))

def load_targets(path: str):

    pat = re.compile(r'^\s*\[(\d+)\]\s*:\s*(.*)$')
    tgt = {}
    with open(path, 'r') as f:
        for line in f:
            m = pat.match(line)
            if not m:
                continue
            idx = int(m.group(1))
            nums = m.group(2).strip()
            if nums:
                vals = set(int(x) for x in nums.split())
            else:
                vals = set()
            tgt[idx] = vals
    return tgt

def merge_targets(targets: dict, j_thresh: float, d_thresh: int):

    merged = True
    while merged:
        merged = False
        keys = sorted(targets.keys())
        for i, k1 in enumerate(keys):
            for k2 in keys[i+1:]:
                a, b = targets[k1], targets[k2]
                js = jaccard_similarity(a, b)
                dd = block_count_diff(a, b)
                if js >= j_thresh and dd <= d_thresh:
                    # merge b into a, remove b
                    targets[k1] = a | b
                    del targets[k2]
                    merged = True
                    break
            if merged:
                break

def write_targets(path: str, targets: dict):

    with open(path, 'w') as f:
        for idx in sorted(targets.keys()):
            elems = sorted(targets[idx])
            line = f"[{idx}] :"
            if elems:
                line += " " + " ".join(str(x) for x in elems)
            f.write(line + "\n")

def main():
    p = argparse.ArgumentParser(description="Merge sets by Jaccard similarity")
    p.add_argument("input", help="input filename")
    p.add_argument("output", help="output filename")
    p.add_argument("--jaccard", "-j", type=float, default=0.5,
                   help="Jaccard similarity threshold (default: 0.5)")
    p.add_argument("--size-diff", "-d", type=int, default=10**9,
                   help="max block-count difference (default: no limit)")
    args = p.parse_args()

    targets = load_targets(args.input)
    if not targets:
        print("No valid entries found in", args.input, file=sys.stderr)
        sys.exit(1)

    merge_targets(targets, args.jaccard, args.size_diff)
    write_targets(args.output, targets)
    print(f"Merged {len(targets)} groups → wrote to {args.output}")

if __name__ == "__main__":
    main()
