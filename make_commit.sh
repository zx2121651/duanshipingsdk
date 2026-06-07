# Create a proper script to do the string replacement accurately.
sed -i 's/void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) override;/void dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) override;\n\n    void executeCommands();/' sdk-core/core/src/rhi/GLRenderDevice.h
sed -i 's/PrimitiveTopology m_currentTopology = PrimitiveTopology::TriangleStrip;/PrimitiveTopology m_currentTopology = PrimitiveTopology::TriangleStrip;\n    std::vector<std::function<void()>> m_commands;/' sdk-core/core/src/rhi/GLRenderDevice.h
sed -i 's/~GLRenderDevice() override;/~GLRenderDevice() override {}/' sdk-core/core/src/rhi/GLRenderDevice.h
python3 test2.py
