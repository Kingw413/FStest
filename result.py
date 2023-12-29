import os
import pandas as pd
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt

STRATEGY_VALUES =['vndn', 'lsif', 'prfs','mupf', 'mine2']
# 设置结果文件夹路径
results_folder = ['logs_results/1.1_highway_changeNum/v90', 'logs_results/1.2_highway_changeSpeed/n100', 'logs_results/1.3_highway_changeSpeedVar/n100', 'logs_results/1.4_highway_changeRate']
indicators = [
    [n for n in range(20, 201, 20)],
    [v for v in range(60, 121, 10)],
    [var for var in range(10, 61, 10)],
    [rate for rate in range(1, 8, 1)]
]
index_label = ['Number of Vehicles','Vehicle Speed', 'Vehicle Speed Variation', 'Number of Interests per Second']

#排序文件夹，保证计算出的数据与index对应
def custom_sort(file_name,i:int):
    if i==0:
        result = int(file_name.split('_')[0].split('n')[1])
    elif i==1:
        result = int(file_name.split('_')[1].split('v')[1])
    elif i==2:
        result = int(file_name.split('_')[1].split('var')[1] )
    else:
        result = int(file_name.split('rate')[1])
    return result

for i in range(4):
    # 创建一个空的DataFrame来存储所有实验数据
    all_data = pd.DataFrame()
    parts = results_folder[i].split('/')
    parts[0] = 'results'
    avg_results_folder = os.path.join(*parts)
    parts[0]='figures'
    figures_folder = os.path.join(*parts)
    os.makedirs(avg_results_folder, exist_ok=True)
    os.makedirs(figures_folder, exist_ok=True)


    sorted_folder = sorted(os.listdir(results_folder[i]), key=lambda x: custom_sort(x, i))
    # 遍历每个子文件夹
    for subdir in sorted_folder:
        subdir_path = os.path.join(results_folder[i], subdir)

        # 确保是文件夹而非文件
        if os.path.isdir(subdir_path):
            # 创建一个空的DataFrame来存储当前场景下的所有运行数据
            scene_data = pd.DataFrame()

            # 遍历当前子文件夹中的每个csv文件
            for file_name in os.listdir(subdir_path):
                if file_name.endswith('.csv'):
                    file_path = os.path.join(subdir_path, file_name)

                    csv_data = pd.read_csv(file_path, index_col=0)
                    scene_data = pd.concat([scene_data, csv_data])

            # 计算当前场景下每个指标下每个方法的平均值
            method_means = scene_data.groupby(level=0).mean().round(decimals=3)
            
            # 将当前场景的平均值添加到all_data
            all_data = pd.concat([all_data, method_means])
    final_results = all_data.groupby(level=0)

    fig, axes = plt.subplots(nrows=2, ncols=2, figsize=(12,10))
    for (indicator, indicator_data), ax in zip(final_results, axes.flatten()):
        metric_file_name = os.path.join(avg_results_folder, indicator + '.csv')
        print(metric_file_name)
        indicator_data.index = indicators[i]
        # 创建 MultiIndex，其中第一层为注释行，第二层为原始的列名 
        multi_index = pd.MultiIndex.from_product([[indicator], indicator_data.columns], names=['', ''])
       
        indicator_data.columns = multi_index
        indicator_data.to_csv(metric_file_name, index_label=index_label[i])

        # 使用 Matplotlib 绘制折线图
        # plt.figure(figsize=(10, 6))
        for col in indicator_data.columns.levels[1]:
            ax.plot(indicator_data.index, indicator_data[(indicator, col)], label=col)

        ax.set_title(f"{indicator} vs {index_label[i]}")
        ax.set_xlabel(index_label[i])
        ax.set_ylabel(indicator)
        ax.legend()
        ax.grid(True)
        # ax.savefig(os.path.join(figures_folder, f"{indicator}.png"))
    plt.tight_layout()
    plt.savefig(os.path.join(figures_folder, "sum.png"))
