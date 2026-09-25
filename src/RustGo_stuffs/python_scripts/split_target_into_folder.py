import os
import sys
import subprocess
import shutil

def copy_files(src, dst):
    if os.path.isdir(src):
        dst_directory = os.path.join(dst, os.path.basename(src))
        if os.path.exists(dst_directory):
            shutil.rmtree(dst_directory)  # 이미 존재하는 경우, 먼저 제거
        shutil.copytree(src, dst_directory)
    else:
        shutil.copy(src, dst)

def create_folders_and_process_file(input_file, additional_files, additional_dirs):
    base_name = os.path.splitext(input_file)[0]

    with open(input_file, 'r') as file:
        indices = set()  
        for line in file:
            if line.startswith('['):
                index = line.find(']')
                if index != -1:
                    index_num = int(line[1:index])
                    indices.add(index_num)

    folders = []
    for index in sorted(indices):
        folder_name = f'target_{index}'
        os.makedirs(folder_name, exist_ok=True)  # 폴더 생성
        folders.append(folder_name)

    subprocess.run(['python3', 'split_targets.py', input_file], check=True)

    for index in indices:
        src_file = f'{base_name}{index}.txt'
        dst_folder = f'target_{index}'
        shutil.move(src_file, os.path.join(dst_folder, src_file))

    for file_name in additional_files:
        for folder in folders:
            copy_files(file_name, folder)

    for dir_name in additional_dirs:
        for folder in folders:
            copy_files(dir_name, folder)

    print("Folders have been created and all specified files and directories have been copied successfully.")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 split_target_into_folder.py <input_file> [-additional <file1> <file2> ...] [-additional_dir <dir1> <dir2> ...]")
    else:
        input_file = sys.argv[1]
        additional_files = []
        additional_dirs = []
        current_list = None

        for arg in sys.argv[2:]:
            if arg == '-additional':
                current_list = additional_files
            elif arg == '-additional_dir':
                current_list = additional_dirs
            elif current_list is not None:
                current_list.append(arg)

        create_folders_and_process_file(input_file, additional_files, additional_dirs)
