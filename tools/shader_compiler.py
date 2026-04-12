import os
import sys
import glob

def compile_shaders(input_dir, output_file):
    header_content = """#pragma once
#include <string>
#include <unordered_map>

namespace sdk {
namespace video {

struct GeneratedShaders {
    static const std::unordered_map<std::string, std::string>& get() {
        static const std::unordered_map<std::string, std::string> shaders = {
"""

    shader_files = glob.glob(os.path.join(input_dir, "*.*"))
    for file_path in shader_files:
        filename = os.path.basename(file_path)
        with open(file_path, 'r', encoding='utf-8') as f:
            content = f.read()
            # Escape for C++ string literal
            content = content.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n"\n            "')
            header_content += f'            {{"{filename}", \n            "{content}"}},\n'

    header_content += """        };
        return shaders;
    }
};

} // namespace video
} // namespace sdk
"""

    with open(output_file, 'w', encoding='utf-8') as f:
        f.write(header_content)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python shader_compiler.py <input_dir> <output_file>")
        sys.exit(1)
    compile_shaders(sys.argv[1], sys.argv[2])
