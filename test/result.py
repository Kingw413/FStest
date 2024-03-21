import os
import re
import pandas as pd
import matplotlib 
matplotlib.use('Agg')
import matplotlib.pyplot as plt

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

def writeResultToFile(logs_folder:str, delay_folder:str, results_folder: str, indicators):
    for num in indicators:
        initial = 1
        for strategy in STRATEGY_VALUES:
            logfile = logs_folder +"/n" + str(num) + "/" + strategy +".log"
            delayfile = delay_folder +"/n" + str(num) + "/" + strategy +".log"
            resultfile = results_folder +"/" + str(num) + ".csv"
            if (initial and os.path.exists(resultfile)):
                os.remove(resultfile)
                initial = 0
            metric = calMetric(logfile, delayfile, num, RATE, TIME)
            strategy_metrics = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
            try:
                df = pd.read_csv(resultfile, index_col=0)
            except FileNotFoundError:
                df = pd.DataFrame(index=RESULTS_VALUES, columns=[])
                df.to_csv(resultfile)
            df = pd.concat([df, strategy_metrics], axis=1)
            df.to_csv(resultfile)

def resultAndPlot(results_folder:str, avg_results_folder:str, figures_folder:str, indicators:list):
    # 创建一个空的DataFrame来存储所有实验数据
    all_data = pd.DataFrame()
    for indicator in indicators:
        file_path = results_folder + "/" + str(indicator) + ".csv"
        print(file_path)
        csv_data = pd.read_csv(file_path, index_col=0)
        all_data = pd.concat([all_data, csv_data])
    final_results = all_data.groupby(level=0)

    fig, axes = plt.subplots(nrows=2, ncols=3, figsize=(20,10))
    for (indicator, indicator_data), ax in zip(final_results, axes.flatten()):
        metric_file_name = os.path.join(avg_results_folder, indicator + '.csv')
        print(metric_file_name)
        indicator_data.index = indicators
        indicator_data.to_csv(metric_file_name, index_label=index_label)

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
    plt.savefig(figures_folder+".png")


logs_folder = 'test/logs/2_cpPairs'
delay_folder = 'test/logs_delay/2_cpPairs'
results_folder = 'test/logs_results/2_cpPairs'
avg_results_folder = 'test/results/2_cpPairs'
figures_folder = 'test/figures/2_cpPairs'
os.makedirs(results_folder, exist_ok=True)
os.makedirs(avg_results_folder, exist_ok=True)
# indicators =  [num for num in range(20, 101, 10)] + [x for x in range(120, 201, 20)]
indicators = [x for x in range(1, 11)]
# indicators = [round(0.5 + i*0.1,1) for i in range(11)]
# index_label = 'Number of Nodes'
index_label = 'Number of Pairs'
# index_label = 'Popularity'

STRATEGY_VALUES =['vndn', 'lsif', 'lisic', 'dasb', 'difs', 'prfs', 'mupf', 'mine']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR', 'HIR', 'HC']
RATE = 10.0
TIME = 20.0
writeResultToFile(logs_folder, delay_folder ,results_folder, indicators)
resultAndPlot(results_folder, avg_results_folder, figures_folder, indicators)