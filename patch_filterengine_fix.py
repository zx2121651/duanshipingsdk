# The reviewer is concerned that `m_graph->release()` actually mutates the old graph and clears it BEFORE we can do anything, OR that releasing the nodes inside the old graph will ruin them if they are shared with the new graph. Wait!
# "Because PipelineGraph::release() calls release() on all its nodes, the underlying Filter instances being carried over to the new graph also have their OpenGL resources destroyed. Since they are never re-initialized after the rebuild..."
# The reviewer literally said: "Since they are never re-initialized after the rebuild..."
# Why did the reviewer say that?
# Let's check `PipelineGraph.cpp`:
"""
Result PipelineGraph::compile() {
...
    // Initialize all nodes
    for (auto* node : m_sortedNodes) {
        node->initialize();
    }
    m_isCompiled = true;
    return Result::ok();
}
"""
# And `buildCameraPipeline()` does:
"""
    if (m_graph) {
        m_graph->release();
    }
    m_graph = newGraph;
    Result res = m_graph->compile();
"""
# So they ARE re-initialized!
# WAIT! What if `m_graph->release()` clears `m_nodes` in the OLD graph? Yes!
# Does it affect `newGraph`? `newGraph` has its own `m_nodes` which holds shared_ptrs.
# BUT wait! When `FilterNode::initialize()` is called, does it recreate the GL shader? Yes, `Filter::initialize()` calls `createProgram`.
# But `Filter::initialize()` DOES NOT set parameters! `m_parameters` is there, but wait, `Filter::initialize()` only queries `m_positionHandle` etc.
# But what about the `Texture` handles? Like LookupTexture!
# `LookupFilter::initialize()` generates a texture! If it's released, `m_lookupTextureId` is deleted! But wait, `LookupFilter::initialize()` does NOT set the lookup texture! It only sets the uniform handle!
# Setting the texture happens via `updateParameter("lookupTexture", ...)` from OUTSIDE!
# So if we destroy the shader, the external state (like the bound OES texture or lookup textures) is NOT restored because `updateParameter` isn't called again!
# YES! The external parameters or GL texture bindings created in the filters might not be automatically restored by simply calling `initialize()`!

with open('core/src/FilterEngine.cpp', 'r') as f:
    content = f.read()

# Instead of `m_graph->release()`, we should NOT release the filters if we are keeping them.
# Or we just don't release the graph at all, just let `shared_ptr` destruct it?
# But `m_graph->release()` explicitly calls `node->release()`.
# If we don't call `m_graph->release()`, what happens? The old graph is destroyed when `m_graph = newGraph`, but `PipelineGraph::~PipelineGraph()` ALSO calls `release()`!
# So the nodes WILL BE RELEASED!
# We must extract the actual underlying `Filter` instances, and create NEW `FilterNode`s? No, if we release the old graph, it will call `release()` on the old `FilterNode`s which will release the `Filter`s!
# So to prevent the old graph from releasing the filters we want to keep, we should CLEAR the old graph's nodes?
# Wait! We can just implement a `detachNodes()` method in `PipelineGraph` so they aren't released, OR we can remove `m_graph->release()` entirely and just let the graph destructor do nothing, OR let `FilterNode::release()` do nothing if we reuse the filter?

new_bcp = """    // Create new graph
    auto newGraph = std::make_shared<PipelineGraph>();
    newGraph->addNode(m_cameraNode);
    newGraph->addNode(m_outputNode);

    // Keep existing filters
    if (m_graph) {
        std::vector<FilterPtr> rawFilters;
        for (const auto& node : m_graph->getNodes()) {
            if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(node)) {
                rawFilters.push_back(filterNode->getFilter());
            }
        }

        // Release old graph cleanly. This destroys old FilterNodes and calls filter->release().
        // Wait, if it calls filter->release(), the filter's GL state is destroyed. We MUST NOT let it destroy the filters we want to keep!
"""
