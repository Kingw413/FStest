import os
import re
import random
import pandas as pd
STRATEGY_VALUES =['vndn', 'lsif', 'mupf', 'lisic', 'dasb', 'difs', 'prfs', 'mime']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR', 'HIR']

def calMetric(logfile: str, delayfile: str, num, rate, time): 
    logs = open(logfile, 'r').readlines()
    if (logs[-1] != "end"):
        return [pd.NA]*len(RESULTS_VALUES)
    fip=fdp=delay=isr=hir=0
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
    isr = round(len(delays)/2/rate/time, 5) 
    hir = round( hit_num/fip_num, 4) 
    return [fip, fdp, delay, isr, hir]

def writeMetricToFile(file_path: str, metrics_strategy: pd.DataFrame, run_time: int):
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
        rate = 10
        time = 20
        for strategy in STRATEGY_VALUES:
            logfile = os.path.join(logfile_folder, f'{strategy}.log')
            delayfile = os.path.join(delayfile_folder, f'{strategy}.log')
            print(f"num={num}_strategy={strategy} 仿真开始")
            command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={num+1} --id1={consumer} --id2={producer}  --rate={rate} --time={time} --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
            os.system(command)
            # print(logfile,delayfile)
            
            metric = calMetric(logfile, delayfile, num, rate, time)
            metrics_strategy = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
            writeMetricToFile(results_file, metrics_strategy, 1)
            print(f"num={num}_strategy={strategy} 仿真结束")
        print(f"num={num}仿真结束")

runScenarios('mobility-traces/1_Num')
print("场景一批处理任务完成。")