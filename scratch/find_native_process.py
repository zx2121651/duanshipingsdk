def main():
    with open('android/android/src/main/cpp/NativeBridge.cpp', 'r', encoding='utf-8') as f:
        content = f.read()
    
    # find line number of Java_com_sdk_video_RenderEngine_nativeProcessFrame
    lines = content.splitlines()
    for idx, line in enumerate(lines):
        if 'Java_com_sdk_video_RenderEngine_nativeProcessFrame' in line:
            print(f"Found Java_com_sdk_video_RenderEngine_nativeProcessFrame on line {idx+1}")
            # print surrounding 50 lines
            for i in range(max(0, idx-5), min(len(lines), idx+45)):
                print(f"{i+1}: {lines[i]}")
            break

if __name__ == '__main__':
    main()
