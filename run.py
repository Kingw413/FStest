import os
import re
import random
import pandas as pd
STRATEGY_VALUES =['vndn', 'lsif', 'prfs','mupf', 'mine2']
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

def writeMetricToFile(folder_path: str, metrics_strategy: pd.DataFrame, run_time: int):
    if not os.path.exists(folder_path):
        os.makedirs(folder_path)
    file_path = os.path.join(folder_path, f'run{run_time}.csv')
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
    parts[0] = 'logs'
    logs_folder_path = os.path.join(*parts)
    parts[0] = 'logs_delay'
    delaylogs_folder_path = os.path.join(*parts)
    parts[0] = 'logs_results'
    results_folder_path = os.path.join(*parts)

    # 定义正则表达式模式
    filename_pattern1 = re.compile(r'n(\d+)\.tcl')
    filename_pattern2 = re.compile(r'n(\d+)_var(\d+)\.tcl')

    # 遍历文件夹下所有文件
    for filename in os.listdir(trace_folder_path):
        # 使用正则表达式匹配文件名中的 'n' 和 'v' 值
        match1 = filename_pattern1.match(filename)
        match2 = filename_pattern2.match(filename)
        if match1:
            n_value = match1.group(1)
            # v_value = match1.group(2)
            logfile_folder = os.path.join(logs_folder_path, f'n{n_value}_v{v_value}')
            delayfile_folder = os.path.join(delaylogs_folder_path, f'n{n_value}_v{v_value}')
            results_folder = os.path.join(results_folder_path, f'n{n_value}_v{v_value}')
        if match2:
            n_value = match2.group(1)
            v_value = match2.group(2)
            logfile_folder = os.path.join(logs_folder_path, f'n{n_value}_var{v_value}')
            delayfile_folder = os.path.join(delaylogs_folder_path, f'n{n_value}_var{v_value}')
            results_folder = os.path.join(results_folder_path, f'n{n_value}_var{v_value}')

        trace = os.path.join(trace_folder_path, filename)
        
        # 对每个文件循环调用程序10次
        for i in range(1, 11):
            # 随机生成Consumer/Producer
            random_integers = random.sample(range(0, int(n_value)), 2)
            consumer = random_integers[0]
            producer = random_integers[1]
            for strategy in STRATEGY_VALUES:
                logfile = os.path.join(logfile_folder, f'{strategy}_run{i}.log')
                delayfile = os.path.join(delayfile_folder, f'{strategy}_run{i}.log')
                # if os.path.exists(logfile) and os.path.exists(delayfile):
                #     continue
                # command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={n_value} --id1={consumer} --id2={producer}  --rate=1 --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
                # os.system(command)
                print(logfile,delayfile)
                metric = calMetric(logfile, delayfile, int(n_value), 1)
                metrics_strategy = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
                writeMetricToFile(results_folder, metrics_strategy, i)
                print(metrics_strategy)
            print(f"n={n_value}_v/var={v_value}_run={i} 仿真结束")
        print(f"n={n_value}_v/var={v_value}仿真结束")

def runScenarios2(trace_folder_path: str):
    # 构建输出log文件夹路径
    parts = trace_folder_path.split('/')
    parts[0] = 'logs'
    logs_folder_path = os.path.join(*parts)
    parts[0] = 'logs_delay'
    delaylogs_folder_path = os.path.join(*parts)
    parts[0] = 'logs_results'
    results_folder_path = os.path.join(*parts)

    for filename in os.listdir(trace_folder_path):
        filename = os.path.join(trace_folder_path,filename)
        # 遍历文件夹下所有文件
        for rate in range(1,8):
            # 使用正则表达式匹配文件名中的 'n' 和 'v' 值
            logfile_folder = os.path.join(logs_folder_path, f'rate{rate}')
            delayfile_folder = os.path.join(delaylogs_folder_path, f'rate{rate}')
            results_folder = os.path.join(results_folder_path, f'rate{rate}')

            # 对每个文件循环调用程序10次
            for i in range(1, 11):
                # 随机生成Consumer/Producer
                random_integers = random.sample(range(0, 100), 2)
                consumer = random_integers[0]
                producer = random_integers[1]

                for strategy in STRATEGY_VALUES:
                    logfile = os.path.join(logfile_folder, f'{strategy}_run{i}.log')
                    delayfile = os.path.join(delayfile_folder, f'{strategy}_run{i}.log')
                    command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num=100 --id1={consumer} --id2={producer}  --rate={rate} --trace={filename}  --delay_log={delayfile}"> {logfile} 2>&1'
                    os.system(command)
                    print(logfile,delayfile)
                    
                    metric = calMetric(logfile, delayfile, 100, rate)
                    metrics_strategy = pd.DataFrame({strategy : metric}, index=RESULTS_VALUES)
                    writeMetricToFile(results_folder, metrics_strategy, i)
                print(f"rate={rate}_run={i} 仿真结束")
            print(f"rate={rate}仿真结束")


# runScenarios2('mobility-traces/1.4_highway_changeRate')
# print("场景四批处理任务完成。")
# runScenarios('mobility-traces/1.2_highway_changeSpeed/n100')
# print("场景一批处理任务完成。")
runScenarios('mobility-traces/1.1_highway_changeNum')
print("场景二批处理任务完成。")
# runScenarios('mobility-traces/1.3_highway_changeSpeedVar/n100')
# print("场景三批处理任务完成。")

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
