#include "../../include/pipeline/PipelineGraph.h"
#include <iostream>

namespace sdk {
namespace video {

void PipelineGraph::addNode(NodePtr node) {
    if (node) {
        m_nodes.push_back(node);
        m_isCompiled = false;
    }
}

bool PipelineGraph::connect(NodePtr from, NodePtr to) {
    if (!from || !to) return false;
    from->addOutput(to);
    m_isCompiled = false;
    return true;
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
    std::unordered_set<PipelineNode*> visited;
    std::unordered_set<PipelineNode*> recStack;

    m_sinkNodes.clear();

    for (auto& node : m_nodes) {
        if (detectCycleUtil(node.get(), visited, recStack)) {
            return Result::error(-2002, "Cycle detected in Pipeline Graph topology");
        }

        if (node->getOutputs().empty()) {
            m_sinkNodes.push_back(node.get());
        }
    }

    if (m_sinkNodes.empty() && !m_nodes.empty()) {
        return Result::error(-2002, "Invalid Graph: No sink nodes (outputs) found");
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
        node->initialize();
    }   m_isCompiled = true;
    return Result::ok();
}

void PipelineGraph::execute(int64_t timestampNs) {
    if (!m_isCompiled) {
        std::cerr << "PipelineGraph executed before successful compile()" << std::endl;
        return;
    }

    // In a pure pull model, we just ask the sinks to pull.
    for (auto* sink : m_sinkNodes) {
        sink->pullFrame(timestampNs);
    }
}

void PipelineGraph::release() {
    for (auto& node : m_nodes) {
        node->release();
    }
    m_nodes.clear();
    m_sinkNodes.clear();
    m_isCompiled = false;
}

} // namespace video
} // namespace sdk
