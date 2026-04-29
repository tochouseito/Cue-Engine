#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/RenderSceneState.h>
#include <GpuData/Batching.h>

namespace Cue
{
    namespace VisibleObjectBucketize
    {
        static constexpr uint32_t k_maxVisibleObjectCount = 1000;
        static constexpr uint32_t k_maxMeshCount = 4096;
    }

    class VisibleObjectBucketCountPass final : public RHI::FrameGraphPass
    {
    public:
        VisibleObjectBucketCountPass(
            const RenderSceneState& a_renderSceneState,
            RHI::BufferHandle a_renderObjectBufferHandle,
            RHI::BufferHandle a_visibleObjectCountBufferHandle)
            : m_renderSceneState(a_renderSceneState)
            , m_renderObjectBufferHandle(a_renderObjectBufferHandle)
            , m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle)
        {
        }

        const char* name() const noexcept override
        {
            return "VisibleObjectBucketCount";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            return a_frameIndex < m_renderSceneState.frameStates.size() &&
                !m_renderSceneState.frame_state(a_frameIndex).useCpuBatching;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_renderObjectBufferHandle);
            if (!result)
            {
                return result;
            }

            result = builder.read_buffer(m_visibleObjectCountBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc bucketCountBufferDesc{};
            bucketCountBufferDesc.name = "VisibleObjectBucketCountBuffer";
            bucketCountBufferDesc.type = RHI::BufferType::Raw;
            bucketCountBufferDesc.defaultHeapCount = 1;
            bucketCountBufferDesc.uploadHeapCount = 0;
            bucketCountBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            bucketCountBufferDesc.stride = sizeof(uint32_t);
            bucketCountBufferDesc.elementCount =
                VisibleObjectBucketize::k_maxMeshCount;
            bucketCountBufferDesc.size =
                bucketCountBufferDesc.stride * bucketCountBufferDesc.elementCount;
            bucketCountBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(bucketCountBufferDesc, m_bucketCountBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc bucketCountBufferUavDesc{};
            bucketCountBufferUavDesc.name = "VisibleObjectBucketCountBufferUAV";
            bucketCountBufferUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            bucketCountBufferUavDesc.bufferKind = RHI::BufferKind::Buffer;
            bucketCountBufferUavDesc.bufferHandle = m_bucketCountBufferHandle;
            bucketCountBufferUavDesc.firstElement = 0;
            bucketCountBufferUavDesc.numElements =
                VisibleObjectBucketize::k_maxMeshCount;
            result = builder.create_view(
                bucketCountBufferUavDesc,
                m_bucketCountBufferUavHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "VisibleObjectBucketCountRootSignature";
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                1 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::UAV,
                RHI::ShaderVisibility::All,
                0 });
            result = builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc computeShaderDesc{};
            computeShaderDesc.name = "VisibleObjectBucketCountCS";
            computeShaderDesc.filePath = "Shaders/D3D12/VisibleObjectBucketCount.hlsl";
            computeShaderDesc.entryPoint = "CSMain";
            computeShaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(computeShaderDesc, m_computeShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "VisibleObjectBucketCountPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.csHandle = m_computeShaderHandle;
            return builder.create_compute_pipeline(pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_renderObjectBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_visibleObjectCountBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_bucketCountBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            if (frameState.useCpuBatching)
            {
                return;
            }

            const uint32_t clearValues[4] = { 0, 0, 0, 0 };
            commandContext->clear_unordered_access_uint(
                m_bucketCountBufferUavHandle,
                clearValues);

            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipelineHandle);
            commandContext->set_srv(0, m_renderObjectBufferHandle);
            commandContext->set_srv(1, m_visibleObjectCountBufferHandle);
            commandContext->set_uav(2, m_bucketCountBufferHandle);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_renderObjectBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        RHI::BufferHandle m_bucketCountBufferHandle{};
        RHI::ViewHandle m_bucketCountBufferUavHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_computeShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };

    class VisibleObjectBucketPrefixPass final : public RHI::FrameGraphPass
    {
    public:
        explicit VisibleObjectBucketPrefixPass(const RenderSceneState& a_renderSceneState)
            : m_renderSceneState(a_renderSceneState)
        {
        }

        const char* name() const noexcept override
        {
            return "VisibleObjectBucketPrefix";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            return a_frameIndex < m_renderSceneState.frameStates.size() &&
                !m_renderSceneState.frame_state(a_frameIndex).useCpuBatching;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_buffer(
                "VisibleObjectBucketCountBuffer",
                m_bucketCountBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc bucketOffsetBufferDesc{};
            bucketOffsetBufferDesc.name = "VisibleObjectBucketOffsetBuffer";
            bucketOffsetBufferDesc.type = RHI::BufferType::Raw;
            bucketOffsetBufferDesc.defaultHeapCount = 1;
            bucketOffsetBufferDesc.uploadHeapCount = 0;
            bucketOffsetBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            bucketOffsetBufferDesc.stride = sizeof(uint32_t);
            bucketOffsetBufferDesc.elementCount =
                VisibleObjectBucketize::k_maxMeshCount;
            bucketOffsetBufferDesc.size =
                bucketOffsetBufferDesc.stride * bucketOffsetBufferDesc.elementCount;
            bucketOffsetBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(bucketOffsetBufferDesc, m_bucketOffsetBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc bucketCursorBufferDesc = bucketOffsetBufferDesc;
            bucketCursorBufferDesc.name = "VisibleObjectBucketCursorBuffer";
            result = builder.create_buffer(bucketCursorBufferDesc, m_bucketCursorBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "VisibleObjectBucketPrefixRootSignature";
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::_32BitConstants,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::UAV,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::UAV,
                RHI::ShaderVisibility::All,
                1 });
            result = builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc computeShaderDesc{};
            computeShaderDesc.name = "VisibleObjectBucketPrefixCS";
            computeShaderDesc.filePath = "Shaders/D3D12/VisibleObjectBucketPrefix.hlsl";
            computeShaderDesc.entryPoint = "CSMain";
            computeShaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(computeShaderDesc, m_computeShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "VisibleObjectBucketPrefixPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.csHandle = m_computeShaderHandle;
            return builder.create_compute_pipeline(pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_bucketCountBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_bucketOffsetBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_bucketCursorBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::UnorderedAccess);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            if (frameState.useCpuBatching)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipelineHandle);
            commandContext->set_32bit_constant(
                0,
                VisibleObjectBucketize::k_maxMeshCount);
            commandContext->set_srv(1, m_bucketCountBufferHandle);
            commandContext->set_uav(2, m_bucketOffsetBufferHandle);
            commandContext->set_uav(3, m_bucketCursorBufferHandle);
            commandContext->dispatch(1, 1, 1);
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_bucketCountBufferHandle{};
        RHI::BufferHandle m_bucketOffsetBufferHandle{};
        RHI::BufferHandle m_bucketCursorBufferHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_computeShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };

    class VisibleObjectBucketScatterPass final : public RHI::FrameGraphPass
    {
    public:
        VisibleObjectBucketScatterPass(
            const RenderSceneState& a_renderSceneState,
            RHI::BufferHandle a_renderObjectBufferHandle,
            RHI::BufferHandle a_visibleObjectCountBufferHandle)
            : m_renderSceneState(a_renderSceneState)
            , m_renderObjectBufferHandle(a_renderObjectBufferHandle)
            , m_visibleObjectCountBufferHandle(a_visibleObjectCountBufferHandle)
        {
        }

        const char* name() const noexcept override
        {
            return "VisibleObjectBucketScatter";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            return a_frameIndex < m_renderSceneState.frameStates.size() &&
                !m_renderSceneState.frame_state(a_frameIndex).useCpuBatching;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_renderObjectBufferHandle);
            if (!result)
            {
                return result;
            }

            result = builder.read_buffer(m_visibleObjectCountBufferHandle);
            if (!result)
            {
                return result;
            }

            result = builder.get_buffer(
                "VisibleObjectBucketOffsetBuffer",
                m_bucketOffsetBufferHandle);
            if (!result)
            {
                return result;
            }

            result = builder.get_buffer(
                "VisibleObjectBucketCursorBuffer",
                m_bucketCursorBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc sortedRenderObjectBufferDesc{};
            sortedRenderObjectBufferDesc.name = "SortedRenderObjectBuffer";
            sortedRenderObjectBufferDesc.type = RHI::BufferType::UnorderedAccess;
            sortedRenderObjectBufferDesc.defaultHeapCount = 1;
            sortedRenderObjectBufferDesc.uploadHeapCount = 0;
            sortedRenderObjectBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            sortedRenderObjectBufferDesc.stride = sizeof(GpuData::RenderObject);
            sortedRenderObjectBufferDesc.elementCount =
                VisibleObjectBucketize::k_maxVisibleObjectCount;
            sortedRenderObjectBufferDesc.size =
                sortedRenderObjectBufferDesc.stride *
                sortedRenderObjectBufferDesc.elementCount;
            sortedRenderObjectBufferDesc.alignment = alignof(GpuData::RenderObject);
            result = builder.create_buffer(
                sortedRenderObjectBufferDesc,
                m_sortedRenderObjectBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "VisibleObjectBucketScatterRootSignature";
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                1 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::SRV,
                RHI::ShaderVisibility::All,
                2 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::UAV,
                RHI::ShaderVisibility::All,
                0 });
            rootSignatureDesc.parameters.push_back({
                RHI::RootParameterType::UAV,
                RHI::ShaderVisibility::All,
                1 });
            result = builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc computeShaderDesc{};
            computeShaderDesc.name = "VisibleObjectBucketScatterCS";
            computeShaderDesc.filePath = "Shaders/D3D12/VisibleObjectBucketScatter.hlsl";
            computeShaderDesc.entryPoint = "CSMain";
            computeShaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(computeShaderDesc, m_computeShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "VisibleObjectBucketScatterPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.csHandle = m_computeShaderHandle;
            return builder.create_compute_pipeline(pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_renderObjectBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_visibleObjectCountBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_bucketOffsetBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_bucketCursorBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_sortedRenderObjectBufferHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const RenderFrameState& frameState =
                m_renderSceneState.frame_state(context.frame_index());
            if (frameState.useCpuBatching || frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipelineHandle);
            commandContext->set_srv(0, m_renderObjectBufferHandle);
            commandContext->set_srv(1, m_visibleObjectCountBufferHandle);
            commandContext->set_srv(2, m_bucketOffsetBufferHandle);
            commandContext->set_uav(3, m_bucketCursorBufferHandle);
            commandContext->set_uav(4, m_sortedRenderObjectBufferHandle);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
        }

    private:
        const RenderSceneState& m_renderSceneState;
        RHI::BufferHandle m_renderObjectBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        RHI::BufferHandle m_bucketOffsetBufferHandle{};
        RHI::BufferHandle m_bucketCursorBufferHandle{};
        RHI::BufferHandle m_sortedRenderObjectBufferHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_computeShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
}
