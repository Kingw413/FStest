import os
import re
import random
import pandas as pd
STRATEGY_VALUES =['vndn', 'lsif', 'mupf', 'lisic', 'dasb', 'difs', 'prfs', 'mine']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR', 'HIR']

indicators =  [num for num in range(1, 11)]

def runScenarios():
    for num in indicators:
        trace = "mobility-traces/2_cpPairs/n" + str(num) +".tcl"
        logfile_folder =  'test/logs/2_cpPairs/n' + str(num) 
        delayfile_folder =  'test/logs_delay/2_cpPairs/n'  + str(num) 
        os.makedirs(logfile_folder, exist_ok=True)
        os.makedirs(delayfile_folder, exist_ok=True)

        consumers = [x for x in range(num)]
        producers = [y for y in range(num, num*2)]
        rate = 10.0
        time = 20.0
        popularity = 0.7
        for strategy in STRATEGY_VALUES:
            logfile = os.path.join(logfile_folder, f'{strategy}.log')
            delayfile = os.path.join(delayfile_folder, f'{strategy}.log')
            print(f"pairs={num}_strategy={strategy} 仿真开始")
            command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={num+1} --consumers={consumers} --producers={producers} --popularity={popularity} --rate={rate} --time={time} --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
            if (os.path.exists(logfile) and os.path.exists(delayfile)):
                logs = open(logfile, 'r').readlines()
                run_times = 1
                while (logs[-1] != "end" and run_times<4):
                    print(f"pairs={num}_strategy={strategy} 仿真失败，重试第{run_times}次")
                    os.system(command)
                    logs = open(logfile, 'r').readlines()
                    run_times += 1  
            else:
                os.system(command)
            print(f"pairs={num}_strategy={strategy} 仿真结束")
        print(f"pairs={num}仿真结束")

popularitys = [0.5 + i*0.1 for i in range(11)]
def runScenarios3():
    for popularity in range(popularitys):
        trace = "mobility-traces/1_Num/n100.tcl"
        logfile_folder =  'test/logs/3_Popularity/s' + str(popularity) 
        delayfile_folder =  'test/logs_delay/3_Popularity/s'  + str(popularity) 
        os.makedirs(logfile_folder, exist_ok=True)
        os.makedirs(delayfile_folder, exist_ok=True)

        consumers = [0]
        producers = [100]
        rate = 10.0
        time = 20.0
        for strategy in STRATEGY_VALUES:
            logfile = os.path.join(logfile_folder, f'{strategy}.log')
            delayfile = os.path.join(delayfile_folder, f'{strategy}.log')
            print(f"pairs={101}_strategy={strategy} 仿真开始")
            command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={101} --consumers={consumers} --producers={producers} --popularity={popularity} --rate={rate} --time={time} --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
            if (os.path.exists(logfile) and os.path.exists(delayfile)):
                logs = open(logfile, 'r').readlines()
                run_times = 1
                while (logs[-1] != "end" and run_times<4):
                    print(f"popularity={popularity}_strategy={strategy} 仿真失败，重试第{run_times}次")
                    os.system(command)
                    logs = open(logfile, 'r').readlines()
                    run_times += 1  
            else:
                os.system(command)
            print(f"popularity={popularity}_strategy={strategy} 仿真结束")
        print(f"popularity={popularity}仿真结束")

runScenarios()
print("场景2批处理任务完成。")
runScenarios3()
print("场景3批处理任务完成。")