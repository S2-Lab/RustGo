import sys
import re

def parse_line(line):
    match = re.match(r'\[(\d+)\]\s*:\s*(.*)', line)
    if match:
        index = int(match.group(1))
        values = list(map(int, match.group(2).split()))
        return index, values
    return None, None

def read_input(file_path):
    data = []
    with open(file_path, 'r') as f:
        for line in f:
            index, values = parse_line(line.strip())
            if values is not None:
                data.append((index, values))
    return data

def write_output(file_path, data):
    with open(file_path, 'w') as f:
        for i, (_, values) in enumerate(data):
            values_str = ' '.join(map(str, values))
            f.write(f"[{i}] : {values_str}\n")

def re_sort(input_path, output_path, priority_indices):
    data = read_input(input_path)
    index_to_values = {index: values for index, values in data}

    priority_items = [(i, index_to_values[i]) for i in priority_indices if i in index_to_values]

    remaining_items = [item for item in data if item[0] not in priority_indices]

    sorted_data = priority_items + remaining_items
    write_output(output_path, sorted_data)

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python3 re_sort.py input.txt output.txt [index1] [index2] ...")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    try:
        indices = list(map(int, sys.argv[3:]))
    except ValueError:
        print("All indices must be integers.")
        sys.exit(1)

    re_sort(input_file, output_file, indices)
