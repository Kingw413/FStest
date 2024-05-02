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
        elif (scenario == "2_cpPairs"):
            trace = "mobility-traces/1_Num/n100.tcl"
            num = 101
            candidate_consumers = [x for x in range(1, 100)]
            maps = {1:[0], 2:[0,6], 3:[0,6,50], 4:[0, 37, 93, 24], 5:[0, 42, 36, 43, 61], 6:[0, 55, 93, 49, 59, 62], 7:[0, 3, 22, 61, 91, 17, 20], 8:[0, 98, 78, 38, 67, 76, 27, 42], 9:[0, 30, 39, 33, 92, 90, 37, 54, 38], 10:[0, 63, 89, 6, 23, 10, 97, 52, 35, 35]}
            # consumers = [0] + list(np.random.choice(candidate_consumers, indicator-1))
            consumers = maps[indicator]
            candidate_producers = [x for x in range(1, 100) if x not in consumers]
            # producers = [100] + list(np.random.choice(candidate_producers, indicator-1))
            producers = [100]
            consumers = ','.join(map(str, consumers))
            producers = ','.join(map(str, producers))
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
    with open(file_path, 'w') as file:
        file.writelines(lines)
    print(f"{scenario}={indicator} has been modified.")

def runScenario2(scenario : str, indicators : list):
    for indicator in indicators:
        modify_cpp_file('extensions/ccaf.cpp', scenario, indicator)
        logfile = os.path.join(f'test/logs/{scenario}', f'{indicator}.log')
        delayfile = os.path.join(f'test/logs_delay/{scenario}', f'{indicator}.log')
        command = f'NS_LOG=ndn-cxx.nfd.CCAF:ndn.Producer ./waf --run "ccaf --num=101 --consumers=0 --producers=100 --popularity=0.7 --rate=20.0 --time=20.0 --trace=mobility-traces/1_Num/n100.tcl --delay_log={delayfile}">{logfile} 2>&1'
        os.system(command)

STRATEGY_VALUES =['vndn', 'dasb', 'lisic', 'prfs', 'mine']
RESULTS_VALUES = ['FIP', 'FDP', 'ISD' , 'ISR', 'HIR']
RATE = 10.0
TIME = 20.0
nums =  [num for num in range(40, 201, 10)]
pairs = [x for x in range(1, 11)]
popularitys = [round(0.2 + i*0.2,1) for i in range(6)]
speeds = [x for x in range(80, 121, 10)]

# runScenario("1_Num", nums)
# print("场景1批处理任务完成。")
# runScenario("3_Popularity", popularitys)
# print("场景3批处理任务完成。")
# runScenario("4_Speed", speeds)
# print("场景4批处理任务完成。")
# runScenario("2_cpPairs", pairs)
# print("场景2批处理任务完成。")

times = [round(5.0 - i*0.5, 1) for i in range(10)]
pth = [round(0.5 + i*0.1, 1) for i in range(5)]
runScenario2("5_Time", times)
runScenario2("6_Pth", pth)

os.system('python test/result.py')