


#!/bin/bash

directory="`pwd`/target/debug/deps"

declare -A func_files
declare -A file_funcs

conflicting_files=()
non_conflicting_files=()
conflict_details=()
hashbrown_indexmap_files=()

for file in "$directory"/*.ll; do
    funcs=$(grep -oP 'define [^@]*@\K\w+' "$file")
    if [ -z "$funcs" ]; then
        echo "No functions found in $file"
        non_conflicting_files+=("$file")
        continue
    fi
    for func in $funcs; do
        func_files[$func]+="$file "
        file_funcs[$file]+="$func "
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
    elif [[ "$file" == *"hashbrown"* || "$file" == *"indexmap"* ]]; then
        hashbrown_indexmap_files+=("$file")
    else
        non_conflicting_files+=("$file")
    fi
done

echo "Conflicting LLVM IR files:"
printf "%s\n" "${conflicting_files[@]}"
echo "Non-conflicting LLVM IR files (excluding hashbrown and indexmap):"
printf "%s\n" "${non_conflicting_files[@]}"
echo "hashbrown and indexmap LLVM IR files:"
printf "%s\n" "${hashbrown_indexmap_files[@]}"
echo "Conflict details (function: files):"
printf "%s\n" "${conflict_details[@]}"

if [ ${#non_conflicting_files[@]} -eq 0 ]; then
    echo "No non-conflicting LLVM IR files to merge."
else
    output_file="$directory/merged.ll"
    llvm-link -S "${non_conflicting_files[@]}" -o "$output_file"
    echo "Merged file created at $output_file"

    script_dir=$(dirname "$0")
    mv "$output_file" "$script_dir/merged.ll"
    echo "Merged file moved to script directory: $script_dir/merged.ll"
fi

rlib_files=()
for file in "${conflicting_files[@]}"; do
    base_name=$(basename "$file" .ll)
    rlib_name="lib${base_name}.rlib"
    rlib_files+=("$rlib_name")
done

for file in "${hashbrown_indexmap_files[@]}"; do
    base_name=$(basename "$file" .ll)
    rlib_name="lib${base_name}.rlib"
    rlib_files+=("$rlib_name")
done

link_command="DRLIBS=\$(find \$DLIBRARY_DIR \\( "
for rlib in "${rlib_files[@]}"; do
    link_command+="-name \"$rlib\" -o "
done
link_command=${link_command::-4}" \\))"

echo "Linking command:"
echo "$link_command"
