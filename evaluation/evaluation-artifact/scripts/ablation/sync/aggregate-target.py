import sys


def merge_numbers_to_output(input_file, output_file):
    data = {}
    with open(input_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if ':' in line:
                section, nums = line.split(':', 1)
                section = section.strip()  
                num_list = list(map(int, nums.split()))
                data[section] = num_list

    merged_set = set()
    for num_list in data.values():
        merged_set.update(num_list)
    
    merged_sorted = sorted(merged_set)
    

    data["[0]"] = merged_sorted

    with open(output_file, 'w', encoding='utf-8') as f:
        numbers_str = " ".join(map(str, data["[0]"]))
        f.write(f"[0] : {numbers_str}\n")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: python3 {sys.argv[0]} INPUT OUTPUT", file=sys.stderr)
        sys.exit(1)

    merge_numbers_to_output(sys.argv[1], sys.argv[2])
