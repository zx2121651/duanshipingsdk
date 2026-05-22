#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include "PipelineNode.h"
#include "../GLTypes.h"

namespace sdk {
namespace video {

class PipelineGraph {
public:
    PipelineGraph() = default;
    ~PipelineGraph() { release(); }

    Result addNode(NodePtr node);
    Result connect(NodePtr from, NodePtr to);

    // Validates topology, checks for cycles, and prepares the execution order
    Result compile();

    // Drives the graph from the sink nodes (outputs) pulling upstream
    Result execute(int64_t timestampNs);

    const std::vector<NodePtr>& getNodes() const { return m_nodes; }
    void detachAllNodes() { m_nodes.clear(); m_sortedNodes.clear(); m_sinkNodes.clear(); m_isCompiled = false; }
    void release();

private:
    bool detectCycleUtil(PipelineNode* node, std::unordered_set<PipelineNode*>& visited, std::unordered_set<PipelineNode*>& recStack);

    std::vector<PipelineNode*> m_sortedNodes;
    std::vector<NodePtr> m_nodes;
    std::vector<PipelineNode*> m_sinkNodes; // Nodes with no outputs (Displays/Encoders)
    bool m_isCompiled = false;
};

} // namespace video
} // namespace sdk
