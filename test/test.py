import os
import re
import random
import pandas as pd
import csv
STRATEGY_VALUES = ['vndn']
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
    return [fip, fdp, delay, isr]

def writeMetricToFile(metrics_strategy: pd.DataFrame):
    file_path = "test.csv"
    try:
        df = pd.read_csv(file_path, index_col=0)
    except FileNotFoundError:
        df = pd.DataFrame(index=RESULTS_VALUES, columns=[])
        df.to_csv(file_path)
    df = pd.concat([df, metrics_strategy], axis=1)
    df.to_csv(file_path)


# 对每个文件循环调用程序10次
for i in range(1, 2):
    # 随机生成Consumer/Producer
    random_integers = random.sample(range(0,100), 2)
    consumer = random_integers[0]
    producer = random_integers[1]
    for strategy in STRATEGY_VALUES:
        command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run test>results/test.log  2>&1'
        os.system(command)
        metric = calMetric("results/test.log", "results/test_delay.log", 100, 1)
        metrics_strategy = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
        writeMetricToFile(metrics_strategy)
        print(metrics_strategy)
    print(f"run={i} 仿真结束")