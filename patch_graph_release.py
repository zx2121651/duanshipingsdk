with open('core/src/FilterEngine.cpp', 'r') as f:
    content = f.read()

# FilterEngine::removeAllFilters should properly release filters.
# But in buildCameraPipeline, we extract old filters, release old graph (which destroys the filters?! Wait, m_graph->release calls node->release(). And FilterNode::release() calls m_filter->release(). So the filters are destroyed!)
# And then we put the same filterNode in the new graph, BUT they are already released and their GL resources deleted! And we never call `initialize` on them again during graph compile because graph compile only calls `initialize` once, wait, `PipelineGraph::compile` calls `node->initialize()`!
# Let's check `PipelineGraph::compile()` in `PipelineGraph.cpp`.
