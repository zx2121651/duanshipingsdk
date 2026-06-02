def main():
    with open('sdk-core/core/src/rhi/GLRenderDevice.h', 'r', encoding='utf-8') as f:
        content = f.read()
    
    lines = content.splitlines()
    for idx, line in enumerate(lines):
        if 'class GLCommandBuffer' in line:
            print(f"Found GLCommandBuffer in GLRenderDevice.h on line {idx+1}")
            for i in range(max(0, idx-5), min(len(lines), idx+30)):
                print(f"{i+1}: {lines[i]}")
            break

if __name__ == '__main__':
    main()
