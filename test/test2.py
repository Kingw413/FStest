import os
import re
import random
import pandas as pd

def run(trace, logfile_folder, delayfile_folder, num, consumers, producers, popularity):
    for strategy in STRATEGY_VALUES:
        logfile = os.path.join(logfile_folder, f'{strategy}.log')
        delayfile = os.path.join(delayfile_folder, f'{strategy}.log')
        print(f"{logfile} 仿真开始")
        command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={num} --consumers={consumers} --producers={producers} --popularity={popularity} --rate={RATE} --time={TIME} --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
        if (os.path.exists(logfile) and os.path.exists(delayfile)):
            logs = open(logfile, 'r').readlines()
            run_times = 1
            while (logs[-1] != "end" and run_times<4):
                print(f"{logfile} 仿真失败，重试第{run_times}次")
                os.system(command)
                logs = open(logfile, 'r').readlines()
                run_times += 1  
        else:
            os.system(command)
        print(f"{logfile} 仿真结束")

def runScenario(scenario: str, indicators: list):
    print(f"{scenario} 仿真开始")
    for indicator in indicators:
        logfile_folder =  'test/logs/' + scenario +  "/"  + str(indicator) 
        delayfile_folder =  'test/logs_delay/' +scenario +  "/"  + str(indicator) 
        os.makedirs(logfile_folder, exist_ok=True)
        os.makedirs(delayfile_folder, exist_ok=True)
        if scenario == "1_Num":
            trace = "mobility-traces/" + scenario + "/n" + str(indicator) +".tcl"
            num = indicator+1
            consumers = [0]
            producers = [indicator]
            popularity = 0.7
        elif (scenario == "2_cpPairs"):
            trace = "mobility-traces/" + scenario + "/n" + str(indicator) +".tcl"
            num = 100
            consumers = [x for x in range(indicator)]
            producers = [y for y in range(indicator, indicator*2)]
            popularity = 0.7
        elif (scenario == "3_Popularity"):
            trace = "mobility-traces/1_Num/n100.tcl"
            num = 100
            consumers = [0]
            producers = [100]
            popularity = indicator
        run(trace, logfile_folder, delayfile_folder, num, consumers, producers, popularity)

STRATEGY_VALUES =['vndn', 'dasb', 'lisic',  'mupf','prfs', 'mine']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR', 'HIR']
RATE = 10.0
TIME = 20.0
nums =  [num for num in range(40, 161, 20)]
pairs = [x for x in range(1, 11)]
popularitys = [round(0.5 + i*0.1,1) for i in range(11)]
runScenario("1_Num", nums)
print("场景1批处理任务完成。")
runScenario("2_cpPairs", pairs)
print("场景2批处理任务完成。")
runScenario("3_Popularity", popularitys)
print("场景3批处理任务完成。")

""" 
def runScenarios(scenario: str, nums):
    for num in nums:
        # 构建输出log文件夹路径
        logfile_folder =  'test/logs/' + scenario +  "/"  + str(num) 
        delayfile_folder =  'test/logs_delay/' +scenario +  "/"  + str(num) 
        os.makedirs(logfile_folder, exist_ok=True)
        os.makedirs(delayfile_folder, exist_ok=True)
        trace = "mobility-traces/1_Num/n" + str(num) +".tcl"
        consumers = [0]
        producers = [num]
        popularity = 0.7
        run(trace, logfile_folder, delayfile_folder, consumers, producers, popularity)
        print(f"num={num}仿真结束")

 """

""" 
def runScenarios2(scenario: str, pairs: list):
    for pair in pairs:
        logfile_folder =  'test/logs/' + scenario +  "/"  + str(pair) 
        delayfile_folder =  'test/logs_delay/' +scenario +  "/"  + str(pair) 
        os.makedirs(logfile_folder, exist_ok=True)
        os.makedirs(delayfile_folder, exist_ok=True)
        trace = "mobility-traces/" + scenario + "/n" + str(pair) +".tcl"
        consumers = [x for x in range(pair)]
        producers = [y for y in range(pair, pair*2)]
        popularity = 0.7
        run(trace, logfile_folder, delayfile_folder, consumers, producers, popularity)
        print(f"pairs={pair}仿真结束")

 """

""" 
def runScenarios3(scenario: str, popularitys: list):
    for popularity in popularitys:
        trace = "mobility-traces/1_Num/n100.tcl"
        logfile_folder =  'test/logs/3_Popularity/' + str(popularity) 
        delayfile_folder =  'test/logs_delay/3_Popularity/'  + str(popularity) 
        os.makedirs(logfile_folder, exist_ok=True)
        os.makedirs(delayfile_folder, exist_ok=True)
        consumers = [0]
        producers = [100]
        run(trace, logfile_folder, delayfile_folder, consumers, producers, popularity)
        print(f"popularity={popularity}仿真结束")

 """

