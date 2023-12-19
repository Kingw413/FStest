import numpy as np
from itertools import zip_longest
import sys

def calMetric(logfile, delayfile, rate, time, num): 
    fip=fdp=delay=hir=isr = 0
    producer_num = 0
    for file in logfile:
        logs = open(file, 'r').readlines()
        fip_num, fdp_num = 0,0
        for line in logs:
            if ( ("ndn-cxx.nfd" not in line) or ("localhost") in line):
                continue
            value = line.split( )
            if ( ("do Send Interest" in line) or ("do Content Discovery" in line)):
                fip_num += 1
            if ("afterReceiveData" in line or "afterContentStoreHit" in line):
                fdp_num += 1
            if ("ndn.Producer" in line and "responding with Data" in line):
                producer_num += 1
        fip = round(fip_num/num, 4) 
        fdp = round(fdp_num/num, 4)
    for file in delayfile:
        delay0_list= []
        results = open(file, 'r').readlines()[1:]
        for line in results:
            value = line.split("\t")
            if ( value[4] == "FullDelay"):
                delay0_list.append(float(value[5]))
        if (len(results) == 0 ):
            mean_delay0, mean_delay1, mean_hop0, mean_hop1 = 0,0,0,0
        else:
            mean_delay0 = sum(delay0_list) / len(delay0_list)
        delay = round((mean_delay0), 6) 
        isr = round(len(results)/2/rate/time,5) 
        hir = round( (len(delay0_list)-producer_num)/(len(delay0_list)+0.001), 4) 
    return [fip, fdp, delay, isr, hir]

def writeMetricToFile(filename, metric):
    results_file = ['fip.txt', 'fdp.txt', 'delay.txt' , 'isr.txt', 'hir.txt']
    for i in range(len(metric)):
        file = '/home/whd/ndnSIM2.8/wireless-macspec/results/' + results_file[i]
        with open(file, 'a') as f:
            f.write(str(metric[i])+'\t')
