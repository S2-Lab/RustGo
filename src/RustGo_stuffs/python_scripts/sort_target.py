import sys

def parse_line(line):
    try:
        index_part, values_part = line.split(':', 1)
        values = values_part.strip()
        return values
    except ValueError:
        return None

def main(input_file, output_file):
    lines = []

    with open(input_file, 'r') as f:
        for line in f:
            parsed = parse_line(line)
            if parsed is not None:
                lines.append(parsed)

    with open(output_file, 'w') as f:
        for i, values in enumerate(lines):
            f.write(f'[{i}] : {values}\n')

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python3 sort_target.py input.txt output.txt")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    main(input_path, output_path)
