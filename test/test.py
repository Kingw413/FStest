import os
import re
import random
import pandas as pd
STRATEGY_VALUES =['vndn', 'lsif', 'mupf', 'lisic', 'dasb', 'difs', 'prfs', 'mine']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR', 'HIR']

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

        consumer = 0
        producer = num
        rate = 10
        time = 20
        for strategy in STRATEGY_VALUES:
            logfile = os.path.join(logfile_folder, f'{strategy}.log')
            delayfile = os.path.join(delayfile_folder, f'{strategy}.log')
            print(f"num={num}_strategy={strategy} 仿真开始")
            command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={num+1} --id1={consumer} --id2={producer}  --rate={rate} --time={time} --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
            if (os.path.exists(logfile) and os.path.exists(delayfile)):
                logs = open(logfile, 'r').readlines()
                run_times = 1
                while (logs[-1] != "end" and run_times<4):
                    print(f"num={num}_strategy={strategy} 仿真失败，重试第{run_times}次")
                    os.system(command)
                    logs = open(logfile, 'r').readlines()
                    run_times += 1  
            else:
                os.system(command)
            print(f"num={num}_strategy={strategy} 仿真结束")
        print(f"num={num}仿真结束")
runScenarios('mobility-traces/1_Num')
print("场景一批处理任务完成。")