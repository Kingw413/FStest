import os
import pandas as pd

# 设置结果文件夹路径
num_folder = 'results/1.1_highway_changeSpeed/n100'
speed_folder = 'results/1.2_highway_changeNum/v90'
var_folder = 'results/1.3_highway_changeSpeedVar/n100'
rate_folder = 'results/1.4_highway_changeRate'
num_indicators = [n for n in range(20,201,20)]
speed_indicators = [v for v in range(60,121,10)]
var_indicators = [var for var in range(10,61,10)]
rate_indicators = [rate for rate in range(1,11,1)]
results_folder = [num_folder, speed_folder, var_folder,rate_folder]
indicators = [num_indicators, speed_indicators, var_indicators, rate_indicators]
index_label = ['Node Number', 'Speed', 'Var', 'Rate']

for i in range(4):
    # 创建一个空的DataFrame来存储所有实验数据
    all_data = pd.DataFrame()

    # 遍历每个子文件夹
    for subdir in os.listdir(results_folder[i]):
        subdir_path = os.path.join(results_folder[i], subdir)

        # 确保是文件夹而非文件
        if os.path.isdir(subdir_path):
            # 创建一个空的DataFrame来存储当前场景下的所有运行数据
            scene_data = pd.DataFrame()

            # 遍历当前子文件夹中的每个csv文件
            for file_name in os.listdir(subdir_path):
                if file_name.endswith('.csv'):
                    file_path = os.path.join(subdir_path, file_name)

                    # 读取CSV文件
                    csv_data = pd.read_csv(file_path, index_col=0)
                    # 将每次运行的数据添加到scene_data
                    scene_data = pd.concat([scene_data, csv_data])

            # 计算当前场景下每个指标下每个方法的平均值
            method_means = scene_data.groupby(level=0).mean()
            
            # 将当前场景的平均值添加到all_data
            all_data = pd.concat([all_data, method_means])

    # 将结果写入csv文件
    all_data.to_csv('average_results.csv')
    final_results = all_data.groupby(level=0)
    for indicator, indicator_data in final_results:
        fine_name = results_folder[i] +'/average_'+ indicator + '.csv'
        indicator_data.index = indicators[i]
        indicator_data.to_csv(fine_name, index_label=index_label[i])
