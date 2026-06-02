def main():
    with open('sdk-core/core/src/Filters.cpp', 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Let's find all function definitions of onDraw
    import re
    on_draw_matches = re.finditer(r'void\s+(\w+Filter)::onDraw\((.*?)\)\s*\{', content)
    
    for match in on_draw_matches:
        filter_name = match.group(1)
        start_idx = match.start()
        # Find closing brace of onDraw
        brace_count = 1
        curr_idx = match.end()
        while brace_count > 0 and curr_idx < len(content):
            if content[curr_idx] == '{':
                brace_count += 1
            elif content[curr_idx] == '}':
                brace_count -= 1
            curr_idx += 1
        
        func_body = content[start_idx:curr_idx]
        has_draw = 'draw(' in func_body or 'drawQuad(' in func_body or 'glDraw' in func_body
        print(f"Filter: {filter_name}")
        print(f"  Has draw call: {has_draw}")
        print("  Body:")
        print("\n".join("    " + line for line in func_body.splitlines()[:15]))
        if len(func_body.splitlines()) > 15:
            print("    ...")
        print("-" * 50)

if __name__ == '__main__':
    main()
