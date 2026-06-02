import os

def search_files(directory, extensions):
    results = []
    for root, dirs, files in os.walk(directory):
        for file in files:
            if any(file.endswith(ext) for ext in extensions):
                path = os.path.join(root, file)
                try:
                    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                        lines = f.readlines()
                    for idx, line in enumerate(lines):
                        if 'glGetError' in line or 'glError' in line or '0x502' in line or 'GL_INVALID_OPERATION' in line:
                            results.append((path, idx+1, line.strip()))
                except Exception as e:
                    pass
    return results

def main():
    print("=== SEARCHING FOR GL ERROR CHECKS ===")
    results = search_files('sdk-core/core', ['.cpp', '.h'])
    for res in results:
        print(f"{res[0]}:{res[1]}: {res[2]}")

if __name__ == '__main__':
    main()
