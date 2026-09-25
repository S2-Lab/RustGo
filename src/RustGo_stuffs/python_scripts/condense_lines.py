
import sys

def read_file(file_path):
    with open(file_path, 'r') as file:
        lines = file.readlines()
    return [line.strip() for line in lines]

def process_lines(lines):
    unique_lines = {}
    for line in lines:
        key, values = line.split(':')
        values = values.strip()
        if values not in unique_lines:
            unique_lines[values] = []
        unique_lines[values].append(key.strip())
    return unique_lines

def write_condensed(unique_lines, output_path):
    sorted_keys = sorted(unique_lines.keys(), key=lambda x: min(int(index.replace('[', '').replace(']', '')) for index in unique_lines[x]))
    with open(output_path, 'w') as file:
        for new_index, key in enumerate(sorted_keys):
            file.write(f"[{new_index}] : {key}\n")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python script.py input.txt output.txt")
        sys.exit(1)
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    
    lines = read_file(input_path)
    unique_lines = process_lines(lines)
    write_condensed(unique_lines, output_path)
