#!/bin/bash

directory="`pwd`/target/debug/deps"

declare -A func_files
declare -A file_funcs

conflicting_files=()
non_conflicting_files=()
conflict_details=()
binary_linked_files=()

for file in "$directory"/*.ll; do
    funcs=$(grep -oP 'define [^@]*@\K\w+' "$file")
    if [ -z "$funcs" ]; then
        non_conflicting_files+=("$file")
        continue
    fi
    for func in $funcs; do
        func_files[$func]+="$file "
        file_funcs[$file]+="$func "
    done
done

binary_files=$(find "$directory" -type f -executable)
for bin in $binary_files; do
    bin_name=$(basename "$bin")
    for file in "$directory"/*.ll; do
        if [[ "$file" == *"$bin_name.ll" ]]; then
            non_conflicting_files+=("$file")
            binary_linked_files+=("$file")
            break
        fi
    done
done

for file in "${!file_funcs[@]}"; do
    conflict=0
    for func in ${file_funcs[$file]}; do
        if [[ $(wc -w <<< "${func_files[$func]}") -gt 1 ]]; then
            conflict=1
            conflict_details+=("$func: ${func_files[$func]}")
            break
        fi
    done
    if [[ $conflict -eq 1 ]]; then
        conflicting_files+=("$file")
    elif [[ ! " ${binary_linked_files[@]} " =~ " ${file} " ]]; then
        non_conflicting_files+=("$file")
    fi
done

non_conflicting_files=($(printf "%s\n" "${non_conflicting_files[@]}" | sort -u))

merged_conflict_files=()
if [ ${#conflicting_files[@]} -gt 0 ]; then
    ll_files_to_merge+=("${conflicting_files[0]}")
    for file in "${conflicting_files[@]:1}"; do
        base_name=$(basename "$file" .ll)
        rlib_name="lib${base_name}.rlib"
        rlib_files+=("$rlib_name")
    done
fi

ll_files_to_merge+=("${non_conflicting_files[@]}")

if [ ${#ll_files_to_merge[@]} -eq 0 ]; then
    echo "No LLVM IR files to merge."
else
    output_file="$directory/merged.ll"
    if llvm-link -S "${ll_files_to_merge[@]}" -o "$output_file"; then
        echo "Merged file created at $output_file"
        script_dir=$(dirname "$0")
        mv "$output_file" "$script_dir/merged.ll"
        echo "Merged file moved to script directory: $script_dir/merged.ll"
    else
        echo "Failed to merge LLVM IR files"
    fi
fi

for file in "${binary_linked_files[@]}"; do
    base_name=$(basename "$file" .ll)
    rlib_name="lib${base_name}.rlib"
    rlib_files=("${rlib_files[@]/$rlib_name}")
done

link_command="DLIBRARY_DIR=\"\$(pwd)/target/debug/deps\""
link_command+="\nDRLIBS=\$(find \$DLIBRARY_DIR \\( "
for rlib in "${rlib_files[@]}"; do
    link_command+="-name \"$rlib\" -o "
done
link_command=${link_command::-4}" \\))"

echo -e "\n[Linking command]"
echo -e "$link_command"


echo -e "\n[Conflicting LLVM IR files]"
printf "%s\n" "${conflicting_files[@]}"
echo -e "\n[Non-conflicting LLVM IR files]"
printf "%s\n" "${non_conflicting_files[@]}"
echo -e "\n[Conflict details (function: files)]"
printf "%s\n" "${conflict_details[@]}"

echo -e "\n[Final Summary]"
echo "Files merged into ll code: ${ll_files_to_merge[@]}\n"
echo "Files converted to rlib: ${rlib_files[@]}"