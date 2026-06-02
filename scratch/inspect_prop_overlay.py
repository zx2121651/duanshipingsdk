def main():
    with open('sdk-core/core/src/PropOverlayFilter.cpp', 'r', encoding='utf-8') as f:
        content = f.read()
    
    # find lines containing onDraw in PropOverlayFilter.cpp
    lines = content.splitlines()
    for idx, line in enumerate(lines):
        if 'onDraw' in line:
            print(f"Found onDraw on line {idx+1}")
            # print surrounding 50 lines
            for i in range(max(0, idx-5), min(len(lines), idx+45)):
                print(f"{i+1}: {lines[i]}")
            break

if __name__ == '__main__':
    main()
