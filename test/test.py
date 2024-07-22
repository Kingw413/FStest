import os

def run(trace, logfile_folder, delayfile_folder, num, consumers, producers, popularity):
    for strategy in STRATEGY_VALUES:
        logfile = os.path.join(logfile_folder, f'{strategy}.log')
        delayfile = os.path.join(delayfile_folder, f'{strategy}.log')
        print(f"{logfile} 仿真开始")
        command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={num} --consumers={consumers} --producers={producers} --popularity={popularity} --rate={RATE} --time={TIME} --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
        if (os.path.exists(logfile)==False or os.path.exists(delayfile)==False):
            os.system(command)
        logs = open(logfile, 'r').readlines()
        run_times = 1
        while (logs[-1] != "end" and run_times<4):
            print(f"{logfile} 仿真失败，重试第{run_times}次")
            os.system(command)
            logs = open(logfile, 'r').readlines()
            run_times += 1  
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
            consumers = 0
            producers = indicator
            popularity = 0.7
        elif (scenario == "3_Popularity"):
            trace = "mobility-traces/1_Num/n100.tcl"
            num = 101
            consumers = 0
            producers = 100
            popularity = indicator
        elif (scenario == "4_Speed"):
            trace = "mobility-traces/" + scenario + "/" + str(indicator) +".tcl"
            num = 101
            consumers = 0
            producers = 100
            popularity = 0.7
        run(trace, logfile_folder, delayfile_folder, num, consumers, producers, popularity)

def modify_cpp_file(file_path, scenario, indicator):
    with open(file_path, 'r') as file:
        lines = file.readlines()
    if scenario == '5_Time':
        lines[25] = f'\t\t\tconst double CCAF::T({indicator});' + '\n'
    elif scenario == '6_Pth':   
        lines[24] = f'\t\t\tconst double CCAF::Pth({indicator});' + '\n'
    elif scenario == '7_CacheSize':
        lines[27] = f'\t\t\tconst int CCAF::CACHE_SIZE({indicator});' + '\n'
    with open(file_path, 'w') as file:
        file.writelines(lines)
    print(f"{scenario}={indicator} has been modified.")

def runScenario2(scenario : str, indicators : list):
    logfile_folder =  'test/logs/' + scenario 
    delayfile_folder =  'test/logs_delay/' +scenario
    os.makedirs(logfile_folder, exist_ok=True)
    os.makedirs(delayfile_folder, exist_ok=True)
    for indicator in indicators:
        modify_cpp_file('extensions/ccaf.cpp', scenario, indicator)
        logfile = os.path.join(logfile_folder, f'{indicator}.log')
        delayfile = os.path.join(delayfile_folder, f'{indicator}.log')
        if (os.path.exists(logfile) and os.path.exists(delayfile)):
            continue
        if scenario == '7_CacheSize':
            command = f'NS_LOG=ndn-cxx.nfd.CCAF:ndn.Producer ./waf --run "ccaf --num=121 --consumers=0 --producers=120 --popularity=0.7 --rate=20.0 --time=20.0 --trace=mobility-traces/1_Num/n120.tcl --delay_log={delayfile} --size={indicator}">{logfile} 2>&1'
            os.system(command)
        else:
            command = f'NS_LOG=ndn-cxx.nfd.CCAF:ndn.Producer ./waf --run "ccaf --num=101 --consumers=0 --producers=100 --popularity=0.7 --rate=20.0 --time=20.0 --trace=mobility-traces/1_Num/n100.tcl --delay_log={delayfile} --size=20">{logfile} 2>&1'
            os.system(command)

STRATEGY_VALUES =['vndn', 'dasb', 'lisic', 'prfs', 'ccaf']
RATE = 10.0
TIME = 20.0
nums =  [num for num in range(60, 181, 40)]
popularitys = [round(0.3 + i*0.3,1) for i in range(4)]
speeds = [x for x in range(80, 111, 10)]

runScenario("1_Num", nums)
print("场景1批处理任务完成。")
runScenario("3_Popularity", popularitys)
print("场景3批处理任务完成。")
runScenario("4_Speed", speeds)
print("场景4批处理任务完成。")


times = [round(0.5 + i, 1) for i in range(5)]
pth =[0.4, 0.6, 0.8, 0.9, 0.95]
runScenario2("6_Pth", pth)
runScenario2("5_Time", times)

os.system('python test/result.py')