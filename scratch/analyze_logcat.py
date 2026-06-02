def main():
    with open('scratch/logcat_filtered.txt', 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    keywords = ['error', 'fail', 'invalid', 'exception', '0x502', 'gl', 'context', 'rebuilt', 'pipeline']
    
    print("=== INTERESTING LOGCAT LINES ===")
    printed = 0
    for line in lines:
        lower_line = line.lower()
        if any(kw in lower_line for kw in keywords) and 'bindtask' not in lower_line and ' wise ' not in lower_line and 'trim' not in lower_line:
            print(line.strip())
            printed += 1
            if printed > 100:
                print("... truncated ...")
                break

if __name__ == '__main__':
    main()
