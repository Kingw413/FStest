import os
import re
import random
import pandas as pd
STRATEGY_VALUES =['vndn', 'lsif', 'mupf', 'lisic', 'dasb', 'difs', 'prfs']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR']
TIME=100
def calMetric(logfile: str, delayfile: str, num=100, rate=1.0): 
    logs = open(logfile, 'r').readlines()
    if (logs[-1] != "end"):
        return [pd.NA]*len(RESULTS_VALUES)
    fip=fdp=delay=isr = 0
    fip_num=fdp_num= 0
    for line in logs:
        if ( ("ndn-cxx.nfd" not in line) or ("localhost") in line):
            continue
        value = line.split( )
        if ("do Send Interest" in line or "do Content Discovery" in line):
            fip_num += 1
        if ("do Send Data" in line):
            fdp_num += 1
        # if ("ndn.Producer" in line and "responding with Data" in line):
        #     producer_num += 1
    fip = round(fip_num/num, 4) 
    fdp = round(fdp_num/num, 4)

    delay_list= []
    delays = open(delayfile, 'r').readlines()[1:]
    for line in delays:
        value = line.split("\t")
        if ( value[4] == "LastDelay"):
            delay_list.append(float(value[5]))
    if (len(delays) == 0 ):
        mean_delay = 0
    else:
        mean_delay = sum(delay_list) / len(delay_list)
    delay = round((mean_delay), 6) 
    isr = round(len(delays)/2/rate/TIME, 5) 
    # hir = round( (len(delay_list)-producer_num)/(len(delay_list)+0.001), 4) 
    return [fip, fdp, delay, isr]

def writeMetricToFile(file_path: str, metrics_strategy: pd.DataFrame, run_time: int):
    # if not os.path.exists(folder_path):
    #     os.makedirs(folder_path)
    # file_path = os.path.join(folder_path, f'.csv')
    # file_path = folder_path + ".csv"
    try:
        df = pd.read_csv(file_path, index_col=0)
    except FileNotFoundError:
        df = pd.DataFrame(index=RESULTS_VALUES, columns=[])
        df.to_csv(file_path)
    df = pd.concat([df, metrics_strategy], axis=1)
    df.to_csv(file_path)

def runScenarios(trace_folder_path: str):
    # 构建输出log文件夹路径
    parts = trace_folder_path.split('/')

    filename_pattern = re.compile(r'n(\d+)\.tcl')

    # 遍历文件夹下所有文件
    for filename in os.listdir(trace_folder_path):
        # 使用正则表达式匹配文件名中的 'n' 和 'v' 值
        match = filename_pattern.match(filename)
        num = int(match.group(1))
        if (num<101):
            continue
        trace = os.path.join(trace_folder_path, filename)
        logfile_folder =  'test/logs/' + parts[1] +  "/n"  + str(num) 
        delayfile_folder =  'test/logs_delay/' + parts[1] +  "/n"  + str(num) 
        # results_folder =  'test/logs_results/' + parts[1] +  "/n"  + str(num) 
        results_file =  'test/logs_results/' + parts[1] +  "/n"  + str(num) +".csv"
        os.makedirs(logfile_folder, exist_ok=True)
        os.makedirs(delayfile_folder, exist_ok=True)
        # os.makedirs(results_folder, exist_ok=True)

        consumer = 0
        producer = num
        for strategy in STRATEGY_VALUES:
            logfile = os.path.join(logfile_folder, f'{strategy}.log')
            delayfile = os.path.join(delayfile_folder, f'{strategy}.log')
            print(f"num={num}_strategy={strategy} 仿真开始")
            command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={num+1} --id1={consumer} --id2={producer}  --rate=1 --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
            os.system(command)
            # print(logfile,delayfile)
            
            metric = calMetric(logfile, delayfile, num, 1)
            metrics_strategy = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
            writeMetricToFile(results_file, metrics_strategy, 1)
            print(f"num={num}_strategy={strategy} 仿真结束")
        print(f"num={num}仿真结束")

runScenarios('mobility-traces/1_Num')
print("场景一批处理任务完成。")


# logfile_folder = 'logs/1.2_highway_changeNum/v90/n40_v90'
# delayfile_folder = 'logs_delay/1.2_highway_changeNum/v90/n40_v90'
# results_folder = 'results/1.2_highway_changeNum/v90/n40_v90'
# for i in range(1,11):
#     for strategy in STRATEGY_VALUES:
#         logfile = os.path.join(logfile_folder, f'{strategy}_run{i}.log')
#         delayfile = os.path.join(delayfile_folder, f'{strategy}_run{i}.log')
#         print(logfile,delayfile)
#         metric = calMetric(logfile, delayfile, 40, 1)
#         metrics_strategy = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
#         print(metrics_strategy)
#         writeMetricToFile(results_folder, metrics_strategy, i)
