import sys
import re

def main():
    if len(sys.argv) < 4:
        print("Usage: python3 extract.py input.txt output.txt [key1] [key2] ...")
        return

    input_file = sys.argv[1]
    output_file = sys.argv[2]
    include_keys = set()

    # 사용자가 지정한 [숫자]만 포함되도록 설정
    for key_str in sys.argv[3:]:
        match = re.match(r"\[(\d+)\]", key_str)
        if match:
            include_keys.add(int(match.group(1)))
        else:
            print(f"Invalid key format: {key_str}")

    with open(input_file, 'r') as infile, open(output_file, 'w') as outfile:
        for line in infile:
            line = line.strip()
            if not line:
                continue
            match = re.match(r"\[(\d+)\]\s*:", line)
            if match:
                key = int(match.group(1))
                if key in include_keys:
                    outfile.write(line + "\n")
            else:
                print(f"Skipping malformed line: {line}")

if __name__ == "__main__":
    main()
