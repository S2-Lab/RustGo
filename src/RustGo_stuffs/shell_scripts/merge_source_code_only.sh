#!/bin/bash

directory="$(pwd)/target/debug/deps"
package_name=$1

llvm_ir_files=()
rlib_files=()

for file in "$directory"/*.ll; do
    if grep -q "$package_name" "$file"; then
        llvm_ir_files+=("$file")
    else
        rlib_files+=("$file")
    fi
done

llvm-link -S "${llvm_ir_files[@]}" -o "$directory/main_merged.ll"

script_dir=$(dirname "$0")
mv "$directory/main_merged.ll" "$script_dir/main_merged.ll"
echo "Merged file moved to script directory: $script_dir/main_merged.ll"

exclude_rlibs=()
for file in "${rlib_files[@]}"; do
    base_name=$(basename "$file" .ll)
    rlib_name="lib${base_name}.rlib"
    exclude_rlibs+=("$rlib_name")
done

link_command="DRLIBS=\$(find \$DLIBRARY_DIR \\( "
for rlib in "${exclude_rlibs[@]}"; do
    link_command+="-name \"$rlib\" -o "
done
link_command=${link_command::-4}" \\))"

echo "Linking command:"
echo "$link_command"