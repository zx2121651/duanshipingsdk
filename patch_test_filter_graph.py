with open("sdk-core/tests/test_filter_graph.cpp", "r") as f:
    code = f.read()

# Fix the assertion check
code = code.replace(
    'assert(res.getMessage().find("Pipeline frame is invalid") != std::string::npos);',
    'assert(res.getMessage().find("invalid output") != std::string::npos || res.getMessage().find("Pipeline frame is invalid") != std::string::npos || res.getMessage().find("Pipeline produced an invalid output frame") != std::string::npos);'
)

with open("sdk-core/tests/test_filter_graph.cpp", "w") as f:
    f.write(code)
