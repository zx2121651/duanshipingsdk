#include <algorithm>
#include "../../include/pipeline/PipelineGraph.h"
#define LOG_TAG "PipelineGraph"
#include "../../include/Log.h"
#include <iostream>

namespace sdk {
namespace video {

Result PipelineGraph::addNode(NodePtr node) {
    if (node) {
        m_nodes.push_back(node);
        m_isCompiled = false;
        return Result::ok();
    }
    return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Null node");
}

Result PipelineGraph::connect(NodePtr from, NodePtr to) {
    if (!from || !to) return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Null node connect");
    from->addOutput(to);
    m_isCompiled = false;
    return Result::ok();
}

bool PipelineGraph::detectCycleUtil(PipelineNode* node, std::unordered_set<PipelineNode*>& visited, std::unordered_set<PipelineNode*>& recStack) {
    if (!visited.count(node)) {
        visited.insert(node);
        recStack.insert(node);

        for (auto& neighbor : node->getOutputs()) {
            if (!visited.count(neighbor.get()) && detectCycleUtil(neighbor.get(), visited, recStack)) {
                return true;
            } else if (recStack.count(neighbor.get())) {
                return true;
            }
        }
    }
    recStack.erase(node);
    return false;
}


void topoSortUtil(PipelineNode* node, std::unordered_set<PipelineNode*>& visited, std::vector<PipelineNode*>& sorted) {
    visited.insert(node);
    for (auto& neighbor : node->getOutputs()) {
        if (!visited.count(neighbor.get())) {
            topoSortUtil(neighbor.get(), visited, sorted);
        }
    }
    sorted.push_back(node);
}
Result PipelineGraph::compile() {
    m_isCompiled = false;
    std::unordered_set<PipelineNode*> visited;
    std::unordered_set<PipelineNode*> recStack;

    m_sinkNodes.clear();

    if (m_nodes.empty()) {
        return Result::error(ErrorCode::ERR_GRAPH_NO_SINK, "Invalid Graph: No nodes added");
    }

    for (auto& node : m_nodes) {
        if (detectCycleUtil(node.get(), visited, recStack)) {
            return Result::error(ErrorCode::ERR_GRAPH_CYCLE_DETECTED, "Cycle detected in Pipeline Graph topology");
        }

        if (node->getOutputs().empty()) {
            m_sinkNodes.push_back(node.get());
        }
    }

    if (m_sinkNodes.empty() && !m_nodes.empty()) {
        return Result::error(ErrorCode::ERR_GRAPH_NO_SINK, "Invalid Graph: No sink nodes (outputs) found");
    }

    // Perform topological sorting
    m_sortedNodes.clear();
    std::unordered_set<PipelineNode*> topoVisited;
    for (auto& node : m_nodes) {
        if (!topoVisited.count(node.get())) {
            topoSortUtil(node.get(), topoVisited, m_sortedNodes);
        }
    }
    std::reverse(m_sortedNodes.begin(), m_sortedNodes.end());
    // Initialize all nodes
    for (auto* node : m_sortedNodes) {
        auto res = node->initialize();
        if (!res.isOk()) {
            return Result::error(ErrorCode::ERR_GRAPH_NODE_INIT_FAILED,
                "Node '" + node->getName() + "' failed to initialize: " + res.getMessage());
        }
    }
    m_isCompiled = true;
    return Result::ok();
}

Result PipelineGraph::execute(int64_t timestampNs) {
    if (!m_isCompiled) {
        return Result::error(ErrorCode::ERR_RENDER_INVALID_STATE, "PipelineGraph executed before successful compile()");
    }

    if (m_sinkNodes.empty()) {
        return Result::error(ErrorCode::ERR_GRAPH_NO_SINK, "PipelineGraph has no sink nodes and cannot be executed");
    }

    // In a pure pull model, we just ask the sinks to pull.
    for (auto* sink : m_sinkNodes) {
        auto res = sink->pullFrame(timestampNs);
        if (!res.isOk()) {
            LOGE("Execution failed at sink node '%s': %s", sink->getName().c_str(), res.getMessage().c_str());
            return Result::error(res.getErrorCode(), res.getMessage());
        }
    }

    return Result::ok();
}

void PipelineGraph::release() {
    for (auto& node : m_nodes) {
        node->release();
    }
    m_nodes.clear();
    m_sortedNodes.clear();
    m_sinkNodes.clear();
    m_isCompiled = false;
}

} // namespace video
} // namespace sdk
