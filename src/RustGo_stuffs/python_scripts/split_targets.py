import sys
import os

def process_file(input_file):
    data = []

    base_name = os.path.splitext(input_file)[0]

    with open(input_file, 'r') as file:
        for line in file:

            index = line.find(']')
            file_number = int(line[1:index])  
            content = line.strip()  

            if len(data) <= file_number:
                data.extend([[]] * (file_number - len(data) + 1))
            data[file_number].append(content)

    for i, contents in enumerate(data):
        output_filename = f'{base_name}{i}.txt'
        with open(output_filename, 'w') as file:
            file.write('\n'.join(contents))

    print("Files have been created successfully based on the input file name.")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python script.py <input_file>")
    else:
        input_file = sys.argv[1]
        process_file(input_file)
