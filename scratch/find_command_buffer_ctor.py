def main():
    with open('sdk-core/core/src/rhi/GLRenderDevice.cpp', 'r', encoding='utf-8') as f:
        content = f.read()
    
    # find definition of GLCommandBuffer constructor or class GLCommandBuffer
    lines = content.splitlines()
    for idx, line in enumerate(lines):
        if 'GLCommandBuffer::GLCommandBuffer' in line:
            print(f"Found GLCommandBuffer constructor on line {idx+1}")
            for i in range(max(0, idx-5), min(len(lines), idx+15)):
                print(f"{i+1}: {lines[i]}")
            break

if __name__ == '__main__':
    main()
