import subprocess
import re

def main():
    result = subprocess.run(['adb', 'logcat', '-d', '-v', 'time'], capture_output=True, text=True, encoding='utf-8', errors='ignore')
    lines = result.stdout.splitlines()
    
    # 1. Find all PIDs for com.sdk.video.sample
    pids = set()
    pid_pattern = re.compile(r'PROCESS STARTED \((?P<pid>\d+)\) for package com.sdk.video.sample')
    for line in lines:
        match = pid_pattern.search(line)
        if match:
            pids.add(match.group('pid'))
            
    for line in lines:
        if 'com.sdk.video.sample' in line:
            m = re.search(r'\s+(\d+)-(\d+)\s+', line)
            if m:
                pids.add(m.group(1))
            m2 = re.search(r'\((\d+)\):', line)
            if m2:
                pids.add(m2.group(1))
                
    print(f"Detected PIDs for com.sdk.video.sample: {pids}")
    
    # 2. Write all lines belonging to these PIDs or mentioning the package
    with open('scratch/logcat_filtered.txt', 'w', encoding='utf-8') as f:
        f.write("=== LOGS ===\n")
        printed_count = 0
        for line in lines:
            belongs_to_pid = False
            for pid in pids:
                if f"({pid})" in line or f" {pid}-" in line or line.rstrip().endswith(pid):
                    belongs_to_pid = True
                    break
            
            if belongs_to_pid or 'com.sdk.video.sample' in line:
                f.write(line + '\n')
                printed_count += 1
                
        f.write(f"Total printed lines: {printed_count}\n")
    print(f"Wrote {printed_count} lines to scratch/logcat_filtered.txt")

if __name__ == '__main__':
    main()
