import subprocess
import os
import shutil
import re
import sys
from collections import defaultdict

def extract_functions(ll_file):
    """Extracts all defined functions from the LLVM IR (.ll) file, including internal and private."""
    functions = set()
    func_pattern = re.compile(r"define\s+(?:internal|private)?\s+\S+\s+@(\w+)\(")

    try:
        with open(ll_file, 'r') as f:
            for line in f:
                match = func_pattern.search(line)
                if match:
                    functions.add(match.group(1))
    except Exception as e:
        print(f"Error reading file {ll_file}: {e}")

    return functions

def link_llvm_ir(files, output_file):
    """Link all the given .ll files into one output file using llvm-link with -S flag."""
    try:
        command = ['llvm-link', '-S', '-o', output_file] + files
        print(f"Running command: {' '.join(command)}")
        subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        print(f"Successfully linked files into {output_file}")
    except subprocess.CalledProcessError as e:
        print(f"Error during llvm-link: {e.stderr.decode('utf-8')}")
        sys.exit(1)

def write_function_list_to_file(file_funcs, output_filename="functions_list.txt"):
    """Writes the list of functions for each .ll file into a single output file."""
    try:
        with open(output_filename, 'w') as out_file:
            for ll_file, functions in file_funcs.items():
                out_file.write(f"[{os.path.basename(ll_file)}]\n")
                for func in sorted(functions):
                    out_file.write(f"{func}\n")
                out_file.write("\n")
        print(f"Function list successfully written to {output_filename}")
    except Exception as e:
        print(f"Error writing function list to file: {e}")
        sys.exit(1)

def resolve_function_conflicts(file_funcs, primary_files):
    """Resolves function conflicts and ensures the primary files are prioritized."""
    func_to_file = defaultdict(set)
    exclude_files = set()

    # Map each function to the files it is defined in
    for ll_file, functions in file_funcs.items():
        for func in functions:
            func_to_file[func].add(ll_file)

    # Add files with conflicting functions to the exclude list, except the primary files
    for func, files in func_to_file.items():
        if len(files) > 1:
            for file in files:
                if file not in primary_files:  # Exclude primary files from being removed
                    exclude_files.add(file)

    return exclude_files

def main(program_name):
    current_directory = os.getcwd()
    input_directory = os.path.join(current_directory, 'target', 'x86_64-unknown-linux-gnu', 'debug', 'deps')
    output_file = 'total.ll'
    temp_output_path = os.path.join(input_directory, output_file)

    dlibrary_dir = os.path.join(current_directory, 'target', 'x86_64-unknown-linux-gnu', 'debug', 'deps')

    ll_files = []
    primary_files = []
    for f in os.listdir(input_directory):
        full_path = os.path.join(input_directory, f)
        if f.endswith('.ll') and os.path.isfile(full_path):
            if program_name in f:
                primary_files.append(full_path)
            ll_files.append(full_path)

    if not primary_files:
        print(f"No .ll files found for program '{program_name}'. Please check the path or file extensions.")
        sys.exit(1)

    print("\n### Detected .ll files in the directory:")
    for f in ll_files:
        print(f)

    print(f"\n### Total .ll files detected for program '{program_name}': {len(primary_files)}")

    func_to_file = defaultdict(set)
    file_funcs = {}
    exclude_files = set()

    for ll_file in ll_files:
        functions = extract_functions(ll_file)
        file_funcs[ll_file] = functions

    exclude_files = resolve_function_conflicts(file_funcs, primary_files)

    for primary_file in primary_files:
        if primary_file in exclude_files:
            exclude_files.remove(primary_file)

    files_to_link = [f for f in primary_files if f not in exclude_files]

    write_function_list_to_file(file_funcs)

    if files_to_link:
        link_llvm_ir(files_to_link, temp_output_path)

        try:
            shutil.move(temp_output_path, os.path.join(current_directory, output_file))
            print(f"Moved {output_file} to current directory.")
        except Exception as e:
            print(f"Error moving file: {str(e)}")
            sys.exit(1)
    else:
        print("No files to link.")

    non_colliding_files = [os.path.basename(f) for f in files_to_link]
    print("\n### Non-colliding .ll files (linked):")
    for f in non_colliding_files:
        print(f)

    colliding_files = [os.path.basename(f) for f in exclude_files]
    print("\n### Colliding .ll files (excluded):")
    for f in colliding_files:
        print(f)

    if exclude_files:
        print(f'\n### Linking colliding files as rlib libraries:')
        print(f'DLIBRARY_DIR="{dlibrary_dir}"')
        
        rlib_files = []
        for f in exclude_files:
            rlib_name = f'lib{os.path.splitext(os.path.basename(f))[0]}.rlib'
            rlib_files.append(rlib_name)
        
        print(f'DRLIBS=$(find $DLIBRARY_DIR \\')
        for rlib in rlib_files[:-1]:
            print(f'    -name "{rlib}" \\')
        print(f'    -name "{rlib_files[-1]}" \\)')

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python script.py <program_name>")
        sys.exit(1)
    program_name = sys.argv[1]
    main(program_name)
