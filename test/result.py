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
    fip=fdp=0
    fip_num=fdp_num=hit_num=sat_by_pro_num = 0
    for line in logs:
        if ("localhost" in line):
            continue
        if ("do Send Interest" in line):
            fip_num += 1
        if ("do Send Data" in line):
            fdp_num += 1
        if ("afterContentStoreHit" in line):
            hit_num += 1
        if ("responding with Data" in line):
            sat_by_pro_num += 1
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
        isr = 0
        hir = 0
    else:
        mean_delay = round (sum(delay_list) / len(delay_list), 6)
        mean_hc =round( sum(hc_list) / len(hc_list), 2)
        isr = round(len(delays)/2/rate/time, 5) 
        # hir = round( hit_num/(fip_num+hit_num), 4) 
        hir = round((len(delays)/2 - sat_by_pro_num)/ (len(delays)/2), 4)
    return [fip, fdp, mean_delay, isr, hir, mean_hc]

def writeResultToFile(logs_folder:str, delays_folder:str, results_folder: str, indicators, scenario:str):
    for indicator in indicators:
        resultfile = results_folder +"/" + str(indicator) + ".csv"
        if (os.path.exists(resultfile)):
            os.remove(resultfile)
        for strategy in STRATEGY_VALUES:
            '''
            if (strategy == "mupf"):
                parts = logs_folder.split('/')
                parts.pop(0)
                temp_logfile_folder = os.path.join(*parts)
                parts2 = delays_folder.split('/')
                parts2.pop(0)
                temp_delayfile_folder = os.path.join(*parts2)
                logfile = temp_logfile_folder +"/" + str(indicator) + "/" + strategy +".log"
                delayfile = temp_delayfile_folder +"/" + str(indicator) + "/" + strategy +".log"
            else:      
                logfile = logs_folder +"/" + str(indicator) + "/" + strategy +".log"
                delayfile = delays_folder +"/" + str(indicator) + "/" + strategy +".log"
            '''
            logfile = logs_folder +"/" + str(indicator) + "/" + strategy +".log"
            delayfile = delays_folder +"/" + str(indicator) + "/" + strategy +".log"
            if scenario == "1_Num":
                num = indicator
            else:
                num = 100
            metric = calMetric(logfile, delayfile, num, RATE, TIME)
            strategy_metrics = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
            try:
                df = pd.read_csv(resultfile, index_col=0)
            except FileNotFoundError:
                df = pd.DataFrame(index=RESULTS_VALUES, columns=[])
                df.to_csv(resultfile)
            df = pd.concat([df, strategy_metrics], axis=1)
            df.to_csv(resultfile)

def resultAndPlot(scenario:str, indicators:list, index_label):
    logs_folder =  'test/logs/' + scenario
    delays_folder =  'test/logs_delay/' +scenario
    results_folder =  'test/logs_results/' +scenario
    avg_results_folder =  'test/results/' +scenario
    figures_folder =  'test/figures/' + str(scenario) 
    os.makedirs(logs_folder, exist_ok=True)
    os.makedirs(delays_folder, exist_ok=True)
    os.makedirs(results_folder, exist_ok=True)
    os.makedirs(avg_results_folder, exist_ok=True)
    writeResultToFile(logs_folder, delays_folder ,results_folder, indicators, scenario)

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
            ax.plot(indicator_data.index, indicator_data[col], 'o-', linewidth=2, label=col)
        ax.set_title(f"{indicator} vs {index_label}")
        ax.set_xticks(indicators)
        ax.set_xlabel(index_label)
        ax.set_ylabel(indicator)
        ax.legend()
        ax.grid(True)
        # ax.savefig(os.path.join(figures_folder, f"{indicator}.png"))
    plt.tight_layout()
    plt.savefig(figures_folder+".png")


STRATEGY_VALUES =['vndn', 'dasb', 'lisic', 'difs', 'prfs', 'mine']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR', 'HIR', 'HC']
RATE = 10.0
TIME = 20.0
nums =  [num for num in range(40, 161, 20)]
pairs = [x for x in range(1, 11)]
popularitys = [round(0.5 + i*0.1,1) for i in range(11)]
speeds = [x for x in range(80, 121, 10)]
resultAndPlot("1_Num", nums, "Number of Nodes")
print("场景1批处理任务完成。")
resultAndPlot("2_cpPairs", pairs, "Number of Pairs")
print("场景2批处理任务完成。")
resultAndPlot("3_Popularity", popularitys, "Popularity")
print("场景3批处理任务完成。")
resultAndPlot("4_Speed", speeds, "MaxSpeed")
print("场景4批处理任务完成。")