import sys
from itertools import combinations
from collections import defaultdict

def parse_input(file_path):
    targets = {}
    with open(file_path, 'r') as f:
        for line in f:
            if ':' in line:
                target, blocks = line.strip().split(':', 1)
                target_id = target.strip()[1:-1]  # "[3]" → "3"
                block_list = set(map(int, blocks.strip().split()))
                targets[target_id] = block_list
    return targets

def jaccard_similarity(set1, set2):
    if not set1 and not set2:
        return 1.0
    intersection = len(set1 & set2)
    union = len(set1 | set2)
    return intersection / union

class UnionFind:
    def __init__(self):
        self.parent = {}

    def find(self, x):
        if x not in self.parent:
            self.parent[x] = x
        if self.parent[x] != x:
            self.parent[x] = self.find(self.parent[x])
        return self.parent[x]

    def union(self, x, y):
        self.parent[self.find(x)] = self.find(y)

    def groups(self):
        group_dict = defaultdict(list)
        for item in self.parent:
            root = self.find(item)
            group_dict[root].append(item)
        return list(group_dict.values())

def process_similarity(targets, threshold):
    results = []
    uf = UnionFind()

    for (id1, blocks1), (id2, blocks2) in combinations(targets.items(), 2):
        sim = jaccard_similarity(blocks1, blocks2)
        results.append((id1, id2, sim))
        if sim >= threshold:
            uf.union(id1, id2)

    for tid in targets:
        uf.find(tid)

    return results, uf.groups()

def write_output(results, groups, output_path, multiqueue_path):
    with open(output_path, 'w') as fout, open(multiqueue_path, 'w') as fmulti:
        for id1, id2, sim in results:
            fout.write(f"[{id1}] vs [{id2}] : {sim:.4f}\n")

        fout.write("\nShared input groups (Jaccard similarity above threshold):\n")
        for group in groups:
            if len(group) > 1:
                group_sorted = sorted(group, key=int)
                group_line = ' '.join(f"[{gid}]" for gid in group_sorted)
                fout.write(f"Group: {group_line}\n")
                fmulti.write(f"{group_line}\n")  # 추가로 MultiQueueOptimized.txt에 기록

def main():
    if len(sys.argv) != 4:
        print("Usage: python3 jaccardSimilarity.py input.txt output.txt threshold")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    threshold = float(sys.argv[3])
    multiqueue_file = "MultiQueueOptimized.txt"

    targets = parse_input(input_file)
    results, groups = process_similarity(targets, threshold)
    write_output(results, groups, output_file, multiqueue_file)

if __name__ == "__main__":
    main()
