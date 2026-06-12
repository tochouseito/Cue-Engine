#pragma once

/// ****************************************************************************
/// Static mesh fixed-bucket indirect command generation
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/RenderFeatureSettings.h"
#include "GpuData/Batching.h"

namespace Cue::DrawSystem
{
    namespace StaticMeshBatching
    {
        // Fixed bucket batching currently targets the TestProject mesh set.
        // Keep this small because PrefixSum and command emission visit every
        // bucket, including empty ones, every frame.
        static constexpr uint32_t k_maxMeshBatchCount = 64u;
        static constexpr uint32_t k_maxMaterialBatchCount = 1u;
        static constexpr uint32_t k_depthBinCount = 8u;
        static constexpr uint32_t k_maxBatchCount =
            k_maxMeshBatchCount * k_maxMaterialBatchCount * k_depthBinCount;

        enum class Stage : uint8_t
        {
            Occluder,
            Final
        };

        [[nodiscard]] inline bool is_stage_enabled(
            Stage stage,
            const RenderFeatureSettings& featureSettings) noexcept
        {
            return stage == Stage::Occluder ? featureSettings.hiZEnabled
                                            : featureSettings.batchingEnabled;
        }
    } // namespace StaticMeshBatching

    class ResetBatchCountersPass final : public RHI::FrameGraphPass
    {
      public:
        explicit ResetBatchCountersPass(uint32_t maxObjectCount,
                                        bool createResources,
                                        StaticMeshBatching::Stage stage,
                                        const RenderFeatureSettings& featureSettings)
            : m_maxObjectCount(maxObjectCount),
              m_createResources(createResources),
              m_stage(stage),
              m_featureSettings(featureSettings)
        {
        }

        const char* name() const noexcept override
        {
            return "ResetBatchCounters";
        }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }
        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            a_frameIndex;
            return StaticMeshBatching::is_stage_enabled(
                m_stage, m_featureSettings);
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            if (!m_createResources)
            {
                Result result =
                    builder.get_buffer("IndirectCommandBuffer",
                                       m_indirectCommandBuffer);
                if (!result)
                {
                    return result;
                }
                result = builder.get_buffer("IndirectCommandCountBuffer",
                                            m_indirectCommandCountBuffer);
                if (!result)
                {
                    return result;
                }
                result = builder.get_buffer("RenderObjectIndexBuffer",
                                            m_renderObjectIndexBuffer);
                if (!result)
                {
                    return result;
                }
                result = builder.get_buffer("BatchObjectCountBuffer",
                                            m_batchObjectCountBuffer);
                if (!result)
                {
                    return result;
                }
                result = builder.get_buffer("BatchObjectStartBuffer",
                                            m_batchObjectStartBuffer);
                if (!result)
                {
                    return result;
                }
                result = builder.get_buffer("BatchObjectOffsetBuffer",
                                            m_batchObjectOffsetBuffer);
                if (!result)
                {
                    return result;
                }
                result = builder.get_view("IndirectCommandCountBufferUAV",
                                          m_indirectCommandCountUav);
                if (!result)
                {
                    return result;
                }
                result = builder.get_view("BatchObjectCountBufferUAV",
                                          m_batchObjectCountUav);
                if (!result)
                {
                    return result;
                }
                result = builder.get_view("BatchObjectStartBufferUAV",
                                          m_batchObjectStartUav);
                if (!result)
                {
                    return result;
                }
                return builder.get_view("BatchObjectOffsetBufferUAV",
                                        m_batchObjectOffsetUav);
            }

            RHI::BufferDesc commandBufferDesc{};
            commandBufferDesc.name = "IndirectCommandBuffer";
            commandBufferDesc.type = RHI::BufferType::UnorderedAccess;
            commandBufferDesc.defaultHeapCount = 1;
            commandBufferDesc.uploadHeapCount = 0;
            commandBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            commandBufferDesc.stride = sizeof(GpuData::IndirectCommand);
            commandBufferDesc.elementCount = m_maxObjectCount;
            commandBufferDesc.size =
                commandBufferDesc.stride * commandBufferDesc.elementCount;
            commandBufferDesc.alignment = alignof(GpuData::IndirectCommand);
            Result result = builder.create_buffer(commandBufferDesc,
                                                  m_indirectCommandBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc indirectCountBufferDesc{};
            indirectCountBufferDesc.name = "IndirectCommandCountBuffer";
            indirectCountBufferDesc.type = RHI::BufferType::Raw;
            indirectCountBufferDesc.defaultHeapCount = 1;
            indirectCountBufferDesc.uploadHeapCount = 0;
            indirectCountBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            indirectCountBufferDesc.stride = sizeof(uint32_t);
            indirectCountBufferDesc.elementCount = 1;
            indirectCountBufferDesc.size = sizeof(uint32_t);
            indirectCountBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(indirectCountBufferDesc,
                                           m_indirectCommandCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc indirectCountUavDesc{};
            indirectCountUavDesc.name = "IndirectCommandCountBufferUAV";
            indirectCountUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            indirectCountUavDesc.bufferKind = RHI::BufferKind::Buffer;
            indirectCountUavDesc.bufferHandle = m_indirectCommandCountBuffer;
            indirectCountUavDesc.numElements =
                indirectCountBufferDesc.size / sizeof(uint32_t);
            result = builder.create_view(indirectCountUavDesc,
                                         m_indirectCommandCountUav);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc objectIndexBufferDesc{};
            objectIndexBufferDesc.name = "RenderObjectIndexBuffer";
            objectIndexBufferDesc.type = RHI::BufferType::UnorderedAccess;
            objectIndexBufferDesc.defaultHeapCount = 1;
            objectIndexBufferDesc.uploadHeapCount = 0;
            objectIndexBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            objectIndexBufferDesc.stride = sizeof(uint32_t);
            objectIndexBufferDesc.elementCount = m_maxObjectCount;
            objectIndexBufferDesc.size = objectIndexBufferDesc.stride *
                                         objectIndexBufferDesc.elementCount;
            objectIndexBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(objectIndexBufferDesc,
                                           m_renderObjectIndexBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc batchCountBufferDesc{};
            batchCountBufferDesc.name = "BatchObjectCountBuffer";
            batchCountBufferDesc.type = RHI::BufferType::Raw;
            batchCountBufferDesc.defaultHeapCount = 1;
            batchCountBufferDesc.uploadHeapCount = 0;
            batchCountBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            batchCountBufferDesc.stride = sizeof(uint32_t);
            batchCountBufferDesc.elementCount =
                StaticMeshBatching::k_maxBatchCount;
            batchCountBufferDesc.size =
                batchCountBufferDesc.stride * batchCountBufferDesc.elementCount;
            batchCountBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(batchCountBufferDesc,
                                           m_batchObjectCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc batchCountUavDesc{};
            batchCountUavDesc.name = "BatchObjectCountBufferUAV";
            batchCountUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            batchCountUavDesc.bufferKind = RHI::BufferKind::Buffer;
            batchCountUavDesc.bufferHandle = m_batchObjectCountBuffer;
            batchCountUavDesc.numElements =
                batchCountBufferDesc.size / sizeof(uint32_t);
            result =
                builder.create_view(batchCountUavDesc, m_batchObjectCountUav);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc batchStartBufferDesc{};
            batchStartBufferDesc.name = "BatchObjectStartBuffer";
            batchStartBufferDesc.type = RHI::BufferType::Raw;
            batchStartBufferDesc.defaultHeapCount = 1;
            batchStartBufferDesc.uploadHeapCount = 0;
            batchStartBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            batchStartBufferDesc.stride = sizeof(uint32_t);
            batchStartBufferDesc.elementCount =
                StaticMeshBatching::k_maxBatchCount;
            batchStartBufferDesc.size =
                batchStartBufferDesc.stride * batchStartBufferDesc.elementCount;
            batchStartBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(batchStartBufferDesc,
                                           m_batchObjectStartBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc batchStartUavDesc{};
            batchStartUavDesc.name = "BatchObjectStartBufferUAV";
            batchStartUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            batchStartUavDesc.bufferKind = RHI::BufferKind::Buffer;
            batchStartUavDesc.bufferHandle = m_batchObjectStartBuffer;
            batchStartUavDesc.numElements =
                batchStartBufferDesc.size / sizeof(uint32_t);
            result =
                builder.create_view(batchStartUavDesc, m_batchObjectStartUav);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc batchOffsetBufferDesc{};
            batchOffsetBufferDesc.name = "BatchObjectOffsetBuffer";
            batchOffsetBufferDesc.type = RHI::BufferType::Raw;
            batchOffsetBufferDesc.defaultHeapCount = 1;
            batchOffsetBufferDesc.uploadHeapCount = 0;
            batchOffsetBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            batchOffsetBufferDesc.stride = sizeof(uint32_t);
            batchOffsetBufferDesc.elementCount =
                StaticMeshBatching::k_maxBatchCount;
            batchOffsetBufferDesc.size = batchOffsetBufferDesc.stride *
                                         batchOffsetBufferDesc.elementCount;
            batchOffsetBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(batchOffsetBufferDesc,
                                           m_batchObjectOffsetBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc batchOffsetUavDesc{};
            batchOffsetUavDesc.name = "BatchObjectOffsetBufferUAV";
            batchOffsetUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            batchOffsetUavDesc.bufferKind = RHI::BufferKind::Buffer;
            batchOffsetUavDesc.bufferHandle = m_batchObjectOffsetBuffer;
            batchOffsetUavDesc.numElements =
                batchOffsetBufferDesc.size / sizeof(uint32_t);
            return builder.create_view(batchOffsetUavDesc,
                                       m_batchObjectOffsetUav);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_indirectCommandCountBuffer, RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::UnorderedAccess);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_batchObjectCountBuffer,
                                        RHI::ResourceAccessType::Write,
                                        RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::UnorderedAccess);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_batchObjectStartBuffer,
                                        RHI::ResourceAccessType::Write,
                                        RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::UnorderedAccess);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(m_batchObjectOffsetBuffer,
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

            const uint32_t clearValues[4] = { 0, 0, 0, 0 };
            commandContext->clear_unordered_access_uint(
                m_indirectCommandCountUav, clearValues);
            commandContext->clear_unordered_access_uint(m_batchObjectCountUav,
                                                        clearValues);
            commandContext->clear_unordered_access_uint(m_batchObjectStartUav,
                                                        clearValues);
            commandContext->clear_unordered_access_uint(m_batchObjectOffsetUav,
                                                        clearValues);
        }

      private:
        uint32_t m_maxObjectCount = 0;
        bool m_createResources = true;
        StaticMeshBatching::Stage m_stage = StaticMeshBatching::Stage::Final;
        const RenderFeatureSettings& m_featureSettings;
        RHI::BufferHandle m_indirectCommandBuffer{};
        RHI::BufferHandle m_indirectCommandCountBuffer{};
        RHI::BufferHandle m_renderObjectIndexBuffer{};
        RHI::BufferHandle m_batchObjectCountBuffer{};
        RHI::BufferHandle m_batchObjectStartBuffer{};
        RHI::BufferHandle m_batchObjectOffsetBuffer{};
        RHI::ViewHandle m_indirectCommandCountUav{};
        RHI::ViewHandle m_batchObjectCountUav{};
        RHI::ViewHandle m_batchObjectStartUav{};
        RHI::ViewHandle m_batchObjectOffsetUav{};
    };

    class BatchCountPass final : public RHI::FrameGraphPass
    {
      public:
        BatchCountPass(const DrawFrameState& drawFrameState,
                       RHI::BufferHandle renderObjectBuffer,
                       RHI::BufferHandle visibleObjectCountBuffer,
                       StaticMeshBatching::Stage stage,
                       const RenderFeatureSettings& featureSettings)
            : m_drawFrameState(drawFrameState),
              m_renderObjectBuffer(renderObjectBuffer),
              m_visibleObjectCountBuffer(visibleObjectCountBuffer),
              m_stage(stage),
              m_featureSettings(featureSettings)
        {
        }

        const char* name() const noexcept override
        {
            return "BatchCount";
        }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }
        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            a_frameIndex;
            return StaticMeshBatching::is_stage_enabled(
                m_stage, m_featureSettings);
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_renderObjectBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_visibleObjectCountBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("BatchObjectCountBuffer",
                                        m_batchObjectCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "BatchCountRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            result = builder.create_root_signature(rootSignatureDesc,
                                                   m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "BatchCountCS";
            shaderDesc.filePath = "Shaders/D3D12/StaticMeshBatchCount.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "BatchCountPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_renderObjectBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_visibleObjectCountBuffer,
                                        RHI::ResourceAccessType::Read,
                                        RHI::ResourceState::ShaderResource,
                                        RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(m_batchObjectCountBuffer,
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

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(
                0, StaticMeshBatching::k_maxMeshBatchCount);
            commandContext->set_32bit_constant(
                1, StaticMeshBatching::k_maxMaterialBatchCount);
            commandContext->set_32bit_constant(
                2, StaticMeshBatching::k_depthBinCount);
            commandContext->set_srv(3, m_renderObjectBuffer);
            commandContext->set_srv(4, m_visibleObjectCountBuffer);
            commandContext->set_uav(5, m_batchObjectCountBuffer);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1,
                                     1);
        }

      private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::BufferHandle m_batchObjectCountBuffer{};
        StaticMeshBatching::Stage m_stage = StaticMeshBatching::Stage::Final;
        const RenderFeatureSettings& m_featureSettings;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };

    class PrefixSumPass final : public RHI::FrameGraphPass
    {
      public:
        PrefixSumPass(
            StaticMeshBatching::Stage stage,
            const RenderFeatureSettings& featureSettings)
            : m_stage(stage),
              m_featureSettings(featureSettings)
        {
        }

        const char* name() const noexcept override
        {
            return "PrefixSum";
        }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }
        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            a_frameIndex;
            return StaticMeshBatching::is_stage_enabled(
                m_stage, m_featureSettings);
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_buffer("BatchObjectCountBuffer",
                                               m_batchObjectCountBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("BatchObjectStartBuffer",
                                        m_batchObjectStartBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("BatchObjectOffsetBuffer",
                                        m_batchObjectOffsetBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "PrefixSumRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1 });
            result = builder.create_root_signature(rootSignatureDesc,
                                                   m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "PrefixSumCS";
            shaderDesc.filePath = "Shaders/D3D12/StaticMeshBatchPrefixSum.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "PrefixSumPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_batchObjectCountBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_batchObjectStartBuffer,
                                        RHI::ResourceAccessType::Write,
                                        RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(m_batchObjectOffsetBuffer,
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

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(
                0, StaticMeshBatching::k_maxBatchCount);
            commandContext->set_srv(1, m_batchObjectCountBuffer);
            commandContext->set_uav(2, m_batchObjectStartBuffer);
            commandContext->set_uav(3, m_batchObjectOffsetBuffer);
            commandContext->dispatch(1, 1, 1);
        }

      private:
        RHI::BufferHandle m_batchObjectCountBuffer{};
        RHI::BufferHandle m_batchObjectStartBuffer{};
        RHI::BufferHandle m_batchObjectOffsetBuffer{};
        StaticMeshBatching::Stage m_stage = StaticMeshBatching::Stage::Final;
        const RenderFeatureSettings& m_featureSettings;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };

    class BatchFillPass final : public RHI::FrameGraphPass
    {
      public:
        BatchFillPass(const DrawFrameState& drawFrameState,
                      RHI::BufferHandle renderObjectBuffer,
                      RHI::BufferHandle visibleObjectCountBuffer,
                      uint32_t maxDrawInstanceCount,
                      StaticMeshBatching::Stage stage,
                      const RenderFeatureSettings& featureSettings)
            : m_drawFrameState(drawFrameState),
              m_renderObjectBuffer(renderObjectBuffer),
              m_visibleObjectCountBuffer(visibleObjectCountBuffer),
              m_maxDrawInstanceCount(maxDrawInstanceCount),
              m_stage(stage),
              m_featureSettings(featureSettings)
        {
        }

        const char* name() const noexcept override
        {
            return "BatchFill";
        }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }
        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            a_frameIndex;
            return StaticMeshBatching::is_stage_enabled(
                m_stage, m_featureSettings);
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_renderObjectBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_visibleObjectCountBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("RenderObjectIndexBuffer",
                                        m_renderObjectIndexBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("BatchObjectOffsetBuffer",
                                        m_batchObjectOffsetBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "BatchFillRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1 });
            result = builder.create_root_signature(rootSignatureDesc,
                                                   m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "BatchFillCS";
            shaderDesc.filePath = "Shaders/D3D12/StaticMeshBatchFill.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "BatchFillPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_renderObjectBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_visibleObjectCountBuffer,
                                        RHI::ResourceAccessType::Read,
                                        RHI::ResourceState::ShaderResource,
                                        RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_renderObjectIndexBuffer,
                                        RHI::ResourceAccessType::Write,
                                        RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(m_batchObjectOffsetBuffer,
                                      RHI::ResourceAccessType::Write,
                                      RHI::ResourceState::UnorderedAccess,
                                      RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(
                0, StaticMeshBatching::k_maxMeshBatchCount);
            commandContext->set_32bit_constant(
                1, StaticMeshBatching::k_maxMaterialBatchCount);
            commandContext->set_32bit_constant(
                2, StaticMeshBatching::k_depthBinCount);
            commandContext->set_32bit_constant(3, m_maxDrawInstanceCount);
            commandContext->set_srv(4, m_renderObjectBuffer);
            commandContext->set_srv(5, m_visibleObjectCountBuffer);
            commandContext->set_uav(6, m_renderObjectIndexBuffer);
            commandContext->set_uav(7, m_batchObjectOffsetBuffer);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1,
                                     1);
        }

      private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::BufferHandle m_renderObjectIndexBuffer{};
        RHI::BufferHandle m_batchObjectOffsetBuffer{};
        uint32_t m_maxDrawInstanceCount = 0;
        StaticMeshBatching::Stage m_stage = StaticMeshBatching::Stage::Final;
        const RenderFeatureSettings& m_featureSettings;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };

    class IndirectCommandEmitPass final : public RHI::FrameGraphPass
    {
      public:
        IndirectCommandEmitPass(
            StaticMeshBatching::Stage stage,
            const RenderFeatureSettings& featureSettings)
            : m_stage(stage),
              m_featureSettings(featureSettings)
        {
        }

        const char* name() const noexcept override
        {
            return "IndirectCommandEmit";
        }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }
        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            a_frameIndex;
            return StaticMeshBatching::is_stage_enabled(
                m_stage, m_featureSettings);
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result =
                builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("BatchObjectCountBuffer",
                                        m_batchObjectCountBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("BatchObjectStartBuffer",
                                        m_batchObjectStartBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("IndirectCommandBuffer",
                                        m_indirectCommandBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("IndirectCommandCountBuffer",
                                        m_indirectCommandCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "IndirectCommandEmitRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1 });
            result = builder.create_root_signature(rootSignatureDesc,
                                                   m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "IndirectCommandEmitCS";
            shaderDesc.filePath =
                "Shaders/D3D12/StaticMeshIndirectCommandEmit.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "IndirectCommandEmitPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_meshRangeBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_batchObjectCountBuffer,
                                        RHI::ResourceAccessType::Read,
                                        RHI::ResourceState::ShaderResource,
                                        RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_batchObjectStartBuffer,
                                        RHI::ResourceAccessType::Read,
                                        RHI::ResourceState::ShaderResource,
                                        RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(m_indirectCommandBuffer,
                                        RHI::ResourceAccessType::Write,
                                        RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::IndirectArgument);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(m_indirectCommandCountBuffer,
                                      RHI::ResourceAccessType::Write,
                                      RHI::ResourceState::UnorderedAccess,
                                      RHI::ResourceState::IndirectArgument);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(
                0, StaticMeshBatching::k_maxBatchCount);
            commandContext->set_32bit_constant(
                1, StaticMeshBatching::k_maxMaterialBatchCount);
            commandContext->set_32bit_constant(
                2, StaticMeshBatching::k_depthBinCount);
            commandContext->set_srv(3, m_meshRangeBuffer);
            commandContext->set_srv(4, m_batchObjectCountBuffer);
            commandContext->set_srv(5, m_batchObjectStartBuffer);
            commandContext->set_uav(6, m_indirectCommandBuffer);
            commandContext->set_uav(7, m_indirectCommandCountBuffer);
            commandContext->dispatch(1, 1, 1);
        }

      private:
        RHI::BufferHandle m_meshRangeBuffer{};
        RHI::BufferHandle m_batchObjectCountBuffer{};
        RHI::BufferHandle m_batchObjectStartBuffer{};
        RHI::BufferHandle m_indirectCommandBuffer{};
        RHI::BufferHandle m_indirectCommandCountBuffer{};
        StaticMeshBatching::Stage m_stage = StaticMeshBatching::Stage::Final;
        const RenderFeatureSettings& m_featureSettings;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };

    class DirectCommandEmitPass final : public RHI::FrameGraphPass
    {
      public:
        DirectCommandEmitPass(const DrawFrameState& drawFrameState,
                              RHI::BufferHandle renderObjectBuffer,
                              RHI::BufferHandle visibleObjectCountBuffer,
                              uint32_t maxCommandCount,
                              const RenderFeatureSettings& featureSettings)
            : m_drawFrameState(drawFrameState),
              m_renderObjectBuffer(renderObjectBuffer),
              m_visibleObjectCountBuffer(visibleObjectCountBuffer),
              m_maxCommandCount(maxCommandCount),
              m_featureSettings(featureSettings)
        {
        }

        const char* name() const noexcept override
        {
            return "DirectCommandEmit";
        }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }
        bool is_enabled(uint32_t a_frameIndex) const noexcept override
        {
            a_frameIndex;
            return !m_featureSettings.batchingEnabled;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result =
                builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_renderObjectBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_visibleObjectCountBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("RenderObjectIndexBuffer",
                                        m_renderObjectIndexBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("IndirectCommandBuffer",
                                        m_indirectCommandBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("IndirectCommandCountBuffer",
                                        m_indirectCommandCountBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("IndirectCommandCountBufferUAV",
                                      m_indirectCommandCountUav);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "DirectCommandEmitRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                  RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 2 });
            result = builder.create_root_signature(rootSignatureDesc,
                                                   m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "DirectCommandEmitCS";
            shaderDesc.filePath =
                "Shaders/D3D12/StaticMeshDirectCommandEmit.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "DirectCommandEmitPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_meshRangeBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_renderObjectBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_visibleObjectCountBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_renderObjectIndexBuffer, RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_indirectCommandBuffer, RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::IndirectArgument);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_indirectCommandCountBuffer, RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::IndirectArgument);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.objectCount == 0)
            {
                return;
            }

            const uint32_t clearValues[4] = { 0, 0, 0, 0 };
            commandContext->clear_unordered_access_uint(
                m_indirectCommandCountUav, clearValues);
            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, m_maxCommandCount);
            commandContext->set_srv(1, m_meshRangeBuffer);
            commandContext->set_srv(2, m_renderObjectBuffer);
            commandContext->set_srv(3, m_visibleObjectCountBuffer);
            commandContext->set_uav(4, m_renderObjectIndexBuffer);
            commandContext->set_uav(5, m_indirectCommandBuffer);
            commandContext->set_uav(6, m_indirectCommandCountBuffer);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1,
                                     1);
        }

      private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_meshRangeBuffer{};
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::BufferHandle m_renderObjectIndexBuffer{};
        RHI::BufferHandle m_indirectCommandBuffer{};
        RHI::BufferHandle m_indirectCommandCountBuffer{};
        RHI::ViewHandle m_indirectCommandCountUav{};
        uint32_t m_maxCommandCount = 0;
        const RenderFeatureSettings& m_featureSettings;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };
} // namespace Cue::DrawSystem
