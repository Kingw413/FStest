import os
import re
import pandas as pd
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt

STRATEGY_VALUES =['vndn', 'dasb', 'lisic', 'lsif', 'difs', 'prfs','mupf']
# 设置结果文件夹路径
results_folder = 'test/logs_results/1_Num'
indicators =  [num for num in range(20, 101, 10)]
indicators_add = [x for x in range(120, 201, 20)]
indicators = indicators +  indicators_add
index_label = 'Number of Nodes'

#排序文件夹，保证计算出的数据与index对应
def custom_sort(file_name):
    filename_pattern = re.compile(r'n(\d+)\.csv')
    match = filename_pattern.match(file_name)
    num = int(match.group(1))
    return num

# 创建一个空的DataFrame来存储所有实验数据
all_data = pd.DataFrame()
parts = results_folder.split('/')
parts[1] = 'results'
avg_results_folder = os.path.join(*parts)
parts[1]='figures'
figures_folder = os.path.join(*parts)
os.makedirs(avg_results_folder, exist_ok=True)
os.makedirs(figures_folder, exist_ok=True)

sorted_folder = sorted(os.listdir(results_folder), key=lambda x: custom_sort(x))

# 遍历当前子文件夹中的每个csv文件
for file_name in sorted_folder:
    file_path = os.path.join(results_folder, file_name)
    csv_data = pd.read_csv(file_path, index_col=0)
    all_data = pd.concat([all_data, csv_data])
final_results = all_data.groupby(level=0)

fig, axes = plt.subplots(nrows=2, ncols=2, figsize=(12,10))
for (indicator, indicator_data), ax in zip(final_results, axes.flatten()):
    metric_file_name = os.path.join(avg_results_folder, indicator + '.csv')
    print(metric_file_name)
    indicator_data.index = indicators

    indicator_data.to_csv(metric_file_name, index_label=index_label)

    # 使用 Matplotlib 绘制折线图
    # plt.figure(figsize=(10, 6))
    for col in indicator_data.columns:
        ax.plot(indicator_data.index, indicator_data[col], label=col)

    ax.set_title(f"{indicator} vs {index_label}")
    ax.set_xticks(indicators)
    ax.set_xlabel(index_label)
    ax.set_ylabel(indicator)
    ax.legend()
    ax.grid(True)
    # ax.savefig(os.path.join(figures_folder, f"{indicator}.png"))
plt.tight_layout()
plt.savefig(os.path.join(figures_folder, "sum.png"))
