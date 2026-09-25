import sys

def read_and_sort_file(input_path, output_path):
    with open(input_path, 'r') as file:
        lines = file.readlines()

    sorted_lines = []
    for line in lines:
        index, values = line.split(':')
        sorted_values = sorted(map(int, values.strip().split()))
        sorted_line = f"{index.strip()} : {' '.join(map(str, sorted_values))}"
        sorted_lines.append(sorted_line)

    with open(output_path, 'w') as file:
        for line in sorted_lines:
            file.write(f"{line}\n")

def main():
    if len(sys.argv) != 3:
        print("Usage: python script.py input.txt output.txt")
        sys.exit(1)
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    
    read_and_sort_file(input_path, output_path)

if __name__ == '__main__':
    main()
