#pragma once

#include <vector>
#include <string>
#include <memory>
#include "VideoFrame.h"

namespace sdk {
namespace video {

class PipelineNode {
public:
    explicit PipelineNode(const std::string& name) : m_name(name) {}
    virtual ~PipelineNode() = default;

    const std::string& getName() const { return m_name; }

    // Connect this node's output to another node's input
    void addOutput(std::shared_ptr<PipelineNode> node) {
        if (node) {
            m_outputs.push_back(node);
            node->m_inputs.push_back(this);
        }
    }

    const std::vector<PipelineNode*>& getInputs() const { return m_inputs; }
    const std::vector<std::shared_ptr<PipelineNode>>& getOutputs() const { return m_outputs; }

    // Pull-model: Requests a frame for a given timestamp
    // Upstream nodes will evaluate and process recursively
    virtual ResultPayload<VideoFrame> pullFrame(int64_t timestampNs) = 0;

    // Initialization hook (e.g., compiling shaders, allocating resources)
    virtual Result initialize() { return Result::ok(); }
    virtual void release() {}

protected:
    std::string m_name;
    std::vector<PipelineNode*> m_inputs; // Weak references to upstream nodes
    std::vector<std::shared_ptr<PipelineNode>> m_outputs; // Strong references to downstream
};

typedef std::shared_ptr<PipelineNode> NodePtr;

} // namespace video
} // namespace sdk
