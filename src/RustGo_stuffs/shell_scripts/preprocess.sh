#!/bin/bash
PROCESSED_LLS=""
while IFS= read -r line;
do
    processed=${line//\"/}
    if [[ "$processed" == *"build_script_build"* ]]; then
        continue
    fi
    
    STR_ARRAY=(`echo $processed | tr " " "\n"`)

    prev=""
    lib=0
    for str in "${STR_ARRAY[@]}"
    do
        if [[ "$str" == *".so"* ]]; then
            if [[ "$prev" == "-o" ]]; then
                lib=1
                break
            fi
        fi
        prev=$str
    done

    if [[ $lib == 1 ]]; then
        continue
    fi 

    for ll_file in $(ls *.ll);
    do
        if [[ "$PROCESSED_LLS" == *"$ll_file"* ]]; then
            continue;
        fi

        # Here we skip the error linking llvm-ir code (fdeflate) 
        if [[ "fdeflate-ef2821f2912e6868.ll" == *"$ll_file"* || "clap-97467da07f257d87.ll" == *"$ll_file"* || "oxipng-bca7c3d1a62aadf7.ll" == *"$ll_file"* ]]; then
            continue;
        fi 

        stripped_ll=${ll_file/%.ll/}
        stripped_ll=${stripped_ll/#./}
        if [[ "$processed" == *"$stripped_ll"* ]]; then
            PROCESSED_LLS="$PROCESSED_LLS $ll_file"
        fi
    done
    echo $PROCESSED_LLS

done < ERASAN_linking_commands.txt

