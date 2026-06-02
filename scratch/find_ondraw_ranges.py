def main():
    with open('sdk-core/core/src/Filters.cpp', 'r', encoding='utf-8') as f:
        content = f.read()
    
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
        # count lines before start_idx
        start_line = content[:start_idx].count('\n') + 1
        end_line = content[:curr_idx].count('\n') + 1
        
        print(f"Filter: {filter_name}")
        print(f"  Line Range: {start_line} - {end_line}")
        print("  Code:")
        print(func_body)
        print("=" * 80)

if __name__ == '__main__':
    main()
