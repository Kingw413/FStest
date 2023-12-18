import os
import re
import random

# 定义文件夹路径
folder_path = 'mobility-traces/1.2_highway_changeNum/highway_v=90_n'

# 定义需要赋值的变量（strategy）
strategy_values = ['vndn', 'dasb', 'lsif', 'lisic', 'difs', 'mupf', 'prfs', 'mine']  # 用实际的策略值替换

# 定义正则表达式模式
filename_pattern1 = re.compile(r'highway_n=(\d+)_v=(\d+)\.tcl')
filename_pattern2 = re.compile(r'highway_n=(\d+)_var=(\d+)\.tcl')

# 遍历文件夹下所有文件
for filename in os.listdir(folder_path):
    if os.path.isfile(os.path.join(folder_path, filename)):
        # 使用正则表达式匹配文件名中的 'n' 和 'v' 值
        match1 = filename_pattern1.match(filename)
        match2 = filename_pattern2.match(filename)
        if match1:
            a = True
            n_value = match1.group(1)
            v_value = match1.group(2)
        if match2:
            a =False
            n_value = match2.group(1)
            var_value = match2.group(2)

        trace = os.path.join(folder_path, filename)
        # 对每个文件循环调用程序20次
        for i in range(1, 11):
            # 生成两个不相同的随机整数
            random_integers = random.sample(range(0, int(n_value)), 2)
            # 分别获取两个随机整数
            consumer = random_integers[0]
            producer = random_integers[1]
            for strategy in strategy_values:
                if a:
                    logfile = f"logs/1.2_highway_changeNum/n{n_value}_v{v_value}/{strategy}_run{i}.log"
                    delayfile = f"logs/1.2_highway_changeNum/n{n_value}_v{v_value}/{strategy}_delay_run{i}.log"
                else:
                    logfile = f"logs/n{n_value}_var{var_value}_{strategy}_run{i}.log"
                    delayfile = f"logs/n{n_value}_var{var_value}_{strategy}_delay_run{i}.log"

                command = f'NS_LOG=ndn-cxx.nfd.{strategy.upper()}:ndn.Producer ./waf --run "{strategy} --num={n_value} --id1={consumer} --id2={producer} --trace={trace}  --delay_log={delayfile}"> {logfile} 2>&1'
                os.system(command)
        print(f"n={n_value}_v={v_value}仿真结束")
print("批处理任务完成。")