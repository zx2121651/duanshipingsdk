def main():
    with open('sdk-core/core/src/rhi/GLRenderDevice.cpp', 'r', encoding='utf-8') as f:
        content = f.read()
    
    lines = content.splitlines()
    for i in range(280, min(len(lines), 350)):
        print(f"{i+1}: {lines[i]}")

if __name__ == '__main__':
    main()
