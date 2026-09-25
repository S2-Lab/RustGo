import sys

def read_file(file_path):
    sets = []
    with open(file_path, 'r') as file:
        for line in file:
            index, values = line.split(':')
            sets.append((index.strip(), set(map(int, values.strip().split()))))
    return sets

def remove_subsets(sets):
    # Sort sets by length in descending order
    sets.sort(key=lambda x: len(x[1]), reverse=True)
    unique_sets = []
    
    for i, (index_i, s1) in enumerate(sets):
        is_subset = False
        for j, (index_j, s2) in enumerate(sets):
            if i != j and s1.issubset(s2):
                is_subset = True
                break
        if not is_subset:
            unique_sets.append(s1)
    
    return unique_sets

def write_file(file_path, sets):
    with open(file_path, 'w') as file:
        for i, s in enumerate(sets):
            file.write(f'[{i}] : {" ".join(map(str, sorted(s)))}\n')

def main():
    if len(sys.argv) != 3:
        print("Usage: python script.py input.txt output.txt")
        sys.exit(1)    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    sets = read_file(input_file)
    unique_sets = remove_subsets(sets)
    write_file(output_file, unique_sets)

if __name__ == '__main__':
    main()
