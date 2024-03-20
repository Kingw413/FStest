import os
import re
import pandas as pd
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# 设置结果文件夹路径
results_folder = 'test/logs_results/1_Num'
indicators =  [num for num in range(20, 101, 10)]
indicators_add = [x for x in range(120, 201, 20)]
indicators = indicators +  indicators_add
index_label = 'Number of Nodes'

STRATEGY_VALUES =['vndn', 'lsif', 'lisic', 'dasb', 'difs', 'prfs', 'mupf']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR', 'HIR', 'HC']
RATE = 10
TIME = 20

def calMetric(logfile: str, delayfile: str, num, rate, time): 
    logs = open(logfile, 'r').readlines()
    if (logs[-1] != "end"):
        return [pd.NA]*len(RESULTS_VALUES)
    fip=fdp=delay=isr=hir=hc=0
    fip_num=fdp_num=hit_num=0
    for line in logs:
        if ( ("ndn-cxx.nfd" not in line) or ("localhost") in line):
            continue
        value = line.split( )
        if ("do Send Interest" in line or "do Content Discovery" in line):
            fip_num += 1
        if ("do Send Data" in line):
            fdp_num += 1
        if ("afterContentStoreHit" in line):
            hit_num += 1
    fip = round(fip_num/num, 4) 
    fdp = round(fdp_num/num, 4)

    delay_list= hc_list = []
    delays = open(delayfile, 'r').readlines()[1:]
    for line in delays:
        value = line.split("\t")
        if ( value[4] == "LastDelay"):
            delay_list.append(float(value[5]))
            hc_list.append(float(value[8]))
    if (len(delays) == 0 ):
        mean_delay = 0
        mean_hc = 0
    else:
        mean_delay = sum(delay_list) / len(delay_list)
        mean_hc = sum(hc_list) / len(hc_list)
    delay = round((mean_delay), 6) 
    isr = round(len(delays)/2/rate/time, 5) 
    hir = round( hit_num/fip_num, 4) 
    hc = round(mean_hc, 2)
    return [fip, fdp, delay, isr, hir, hc]

def writeMetricToFile(file_path: str, metrics_strategy: pd.DataFrame):
    try:
        df = pd.read_csv(file_path, index_col=0)
    except FileNotFoundError:
        df = pd.DataFrame(index=RESULTS_VALUES, columns=[])
        df.to_csv(file_path)
    df = pd.concat([df, metrics_strategy], axis=1)
    df.to_csv(file_path)

#排序文件夹，保证计算出的数据与index对应
def custom_sort(file_name):
    filename_pattern = re.compile(r'n(\d+)\.csv')
    match = filename_pattern.match(file_name)
    num = int(match.group(1))
    return num

for num in indicators:
    initial = 1
    for strategy in STRATEGY_VALUES:
        logfile = "test/logs/1_Num/n" + str(num) + "/" + strategy +".log"
        delayfile = "test/logs_delay/1_Num/n" + str(num) + "/" + strategy +".log"
        resultfile = "test/logs_results/1_Num/n" + str(num) + ".csv"
        if (initial):
            os.remove(resultfile)
            initial = 0
        metric = calMetric(logfile, delayfile, num, RATE, TIME)
        strategy_metrics = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
        writeMetricToFile(resultfile, strategy_metrics)

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

fig, axes = plt.subplots(nrows=2, ncols=3, figsize=(20,10))
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
