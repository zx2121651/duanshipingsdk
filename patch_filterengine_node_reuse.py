with open('core/src/FilterEngine.cpp', 'r') as f:
    content = f.read()

# When we actually WANT to remove filters (removeAllFilters), we SHOULD release them!
# removeAllFilters does that correctly by calling node->release() then m_graph->release().
# Wait, m_graph->release() ALSO calls node->release()! So it calls it twice?
# Let's fix removeAllFilters.
old_removeall = """Result FilterEngine::removeAllFilters() {
    if (m_graph) {
        if (m_initialized) {
            for (const auto& node : m_graph->getNodes()) {
                node->release();
            }
        }
        m_graph->release();
        m_graph = std::make_shared<PipelineGraph>();
    }
    m_isGraphDirty = true;
    return Result::ok();
}"""
new_removeall = """Result FilterEngine::removeAllFilters() {
    if (m_graph) {
        m_graph->release();
        m_graph = std::make_shared<PipelineGraph>();
    }
    m_isGraphDirty = true;
    return Result::ok();
}"""
content = content.replace(old_removeall, new_removeall)

# In FilterEngine::release(), we also have double release.
old_release = """void FilterEngine::release() {
    if (m_initialized) {
        if (m_graph) {
            for (const auto& node : m_graph->getNodes()) {
                node->release();
            }
            m_graph->release();
        }
        m_frameBufferPool.clear();
        m_initialized = false;
    }
}"""
new_release = """void FilterEngine::release() {
    if (m_initialized) {
        if (m_graph) {
            m_graph->release();
            m_graph.reset();
        }
        m_frameBufferPool.clear();
        m_initialized = false;
    }
}"""
content = content.replace(old_release, new_release)

with open('core/src/FilterEngine.cpp', 'w') as f:
    f.write(content)
