import os

root_folder = 'logs/1.3_highway_changeSpeedVar'

# 获取root_folder下的所有子文件夹
subfolders = [f.path for f in os.scandir(root_folder) if f.is_dir()]

# 遍历每个子文件夹
for subfolder in subfolders:
    # 获取子文件夹的基本名称
    base_folder_name = os.path.basename(subfolder)
    parts = base_folder_name.split('_')
    # 获取'n'的值
    n_value = parts[1].split('=')[1]

    # 构建新的文件夹名称
    new_folder_name = f'n{n_value}'

    # 构建新的文件夹路径
    new_folder_path = os.path.join(root_folder, new_folder_name)

    # 重命名文件夹
    os.rename(subfolder, new_folder_path)

    # 获取文件夹下的所有文件
    files = [f for f in os.listdir(new_folder_path) if os.path.isfile(os.path.join(new_folder_path, f))]

    # 遍历每个文件
    for file in files:
        base_file_name = os.path.basename(file)
        # 分割文件名
        file_parts = base_file_name.split('_')

        # 获取'v'的值
        v_value = file_parts[2].split('=')[1]

        # 构建新的文件名
        new_file_name = f'n{n_value}_v{v_value}'

        # 构建新的文件路径
        new_file_path = os.path.join(new_folder_path, new_file_name)

        # 重命名文件
        os.rename(os.path.join(new_folder_path, file), new_file_path)
