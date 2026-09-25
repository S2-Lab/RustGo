import subprocess
import os
import shutil
import re
from collections import defaultdict
import sys


def extract_function_signatures(ll_file):
    """Extracts function signatures for precise conflict detection."""
    functions = {}
    func_pattern = re.compile(r"define\s+(?:internal|private)?\s+(\S+)\s+@(\w+)\((.*)\)")

    try:
        with open(ll_file, 'r') as f:
            for line in f:
                match = func_pattern.search(line)
                if match:
                    return_type = match.group(1)
                    func_name = match.group(2)
                    parameters = match.group(3)
                    functions[func_name] = (return_type, parameters)
    except Exception as e:
        print(f"Error reading file {ll_file}: {e}")

    return functions


def link_llvm_ir(files, output_file):
    """Link all the given .bc files into one output file using llvm-link-11 with -S flag."""
    try:
        command = ['llvm-link-11', '-S', '-o', output_file] + files
        print(f"Running command: {' '.join(command)}")
        subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        print(f"Successfully linked files into {output_file}")
    except subprocess.CalledProcessError as e:
        print(f"Error during llvm-link-11: {e.stderr.decode('utf-8')}")
        sys.exit(1)


def resolve_function_conflicts(file_funcs, mandatory_files):
    """Resolves function conflicts and ensures mandatory files are always included."""
    func_to_files = defaultdict(list)
    exclude_files = set()
    conflict_funcs = {}

    # Map each function to the files it is defined in
    for ll_file, functions in file_funcs.items():
        for func, signature in functions.items():
            func_to_files[func].append((ll_file, signature))

    # Process conflicting functions
    for func, files in func_to_files.items():
        if len(files) > 1:  # Conflict exists
            conflict_funcs[func] = files
            selected_file = None
            for file, _ in files:
                if any(mf in file for mf in mandatory_files):
                    selected_file = file
                    break
            if not selected_file:
                selected_file = files[0][0]  # Default to the first file if no mandatory file exists
            print(f"Function {func} selected from {selected_file}, others excluded.")
            for file, _ in files:
                if file != selected_file:
                    exclude_files.add(file)

    return exclude_files, conflict_funcs


def generate_rlib_variable(excluded_files, deps_directory):
    """Generates a shell variable for excluded .bc files as rlib."""
    rlib_entries = []
    for ll_file in excluded_files:
        base_name = os.path.splitext(os.path.basename(ll_file))[0]
        rlib_file = os.path.join(deps_directory, f"lib{base_name}.rlib")
        rlib_entries.append(f'-name "{os.path.basename(rlib_file)}"')
    if not rlib_entries:
        return 'DRLIBS=""'  # Return an empty variable if no files are excluded
    # Join rlib entries with `-o` and properly format for shell output
    rlib_variable = " \\\n    -o ".join(rlib_entries)
    return f'DLIBRARY_DIR="{deps_directory}"\nDRLIBS=$(find $DLIBRARY_DIR \\\n    {rlib_variable} \\\n)'


def print_conflicts(conflict_funcs, selected_files):
    """Prints the conflicting functions and the files they are defined in."""
    print("\n### Conflicting Functions and Files:")
    for func, files in conflict_funcs.items():
        print(f"- Function: {func}")
        for file, signature in files:
            selected_indicator = "(selected)" if file in selected_files else ""
            print(f"  - File: {os.path.basename(file)} {selected_indicator}")
            print(f"    - Signature: Return Type: {signature[0]}, Parameters: {signature[1]}")


def main(program_name):
    current_directory = os.getcwd()
    cargo_target_dir = os.environ.get(
        'CARGO_TARGET_DIR', os.path.join(current_directory, 'target')
    )
    input_directory = os.path.join(
        cargo_target_dir, 'x86_64-unknown-linux-gnu', 'debug', 'deps'
    )
    output_file = 'total.bc'
    temp_output_path = os.path.join(input_directory, output_file)

    # Load all .bc files
    ll_files = [os.path.join(input_directory, f) for f in os.listdir(input_directory) if f.endswith('.bc')]
    if not ll_files:
        print("No .bc files found in the directory. Please check the path or file extensions.")
        sys.exit(1)

    # Program-specific .bc files
    program_ll_files = [f for f in ll_files if program_name in os.path.basename(f)]
    if not program_ll_files:
        print(f"No .bc files found containing the program name '{program_name}'.")
        sys.exit(1)

    # Extract function signatures
    file_funcs = {f: extract_function_signatures(f) for f in ll_files}

    # Resolve conflicts
    exclude_files, conflict_funcs = resolve_function_conflicts(file_funcs, program_ll_files)

    # Ensure program-specific files are not excluded
    for program_file in program_ll_files:
        exclude_files.discard(program_file)

    # Print conflicts
    print_conflicts(conflict_funcs, [f for f in ll_files if f not in exclude_files])

    # Final list of files to merge
    files_to_link = [f for f in ll_files if f not in exclude_files]

    # Link IR files
    if files_to_link:
        link_llvm_ir(files_to_link, temp_output_path)

        # Move the result file
        try:
            shutil.move(temp_output_path, os.path.join(current_directory, output_file))
            print(f"Moved {output_file} to current directory.")
        except Exception as e:
            print(f"Error moving file: {str(e)}")
            sys.exit(1)
    else:
        print("No files to link.")

    # Generate rlib variable
    rlib_variable = generate_rlib_variable(exclude_files, input_directory)

    # Print excluded files for DRLIBS
    print("\n### Shell Variable for Excluded Files as rlib:")
    print(rlib_variable)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python script.py <program_name>")
        sys.exit(1)
    program_name = sys.argv[1]
    main(program_name)
