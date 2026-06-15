#pragma once

/// ****************************************************************************
/// Draw statistics GPU readback pass
/// ****************************************************************************

// === RHI includes ===
#include <BufferManager.h>
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "Engine.h"
#include "GpuData/Batching.h"

// === C++ includes ===
#include <cstring>

namespace Cue::DrawSystem
{
    class DrawStatsReadbackPass final : public RHI::FrameGraphPass
    {
      public:
        DrawStatsReadbackPass(RHI::IBufferManager* bufferManager,
                              const DrawFrameState& drawFrameState,
                              RHI::BufferHandle renderObjectBuffer,
                              RHI::BufferHandle visibleObjectCountBuffer,
                              EngineDebugStats* statsOutput)
            : m_bufferManager(bufferManager),
              m_drawFrameState(drawFrameState),
              m_renderObjectBuffer(renderObjectBuffer),
              m_visibleObjectCountBuffer(visibleObjectCountBuffer),
              m_statsOutput(statsOutput)
        {
        }

        const char* name() const noexcept override
        {
            return "DrawStatsReadback";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
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
            result = builder.get_buffer("IndirectCommandCountBuffer",
                                        m_indirectCommandCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc statsBufferDesc{};
            statsBufferDesc.name = "DrawStatsBuffer";
            statsBufferDesc.type = RHI::BufferType::UnorderedAccess;
            statsBufferDesc.defaultHeapCount = 1;
            statsBufferDesc.uploadHeapCount = 0;
            statsBufferDesc.readbackHeapCount = builder.buffer_count();
            statsBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            statsBufferDesc.stride = sizeof(GpuData::DrawStatsGpu);
            statsBufferDesc.elementCount = 1u;
            statsBufferDesc.size =
                statsBufferDesc.stride * statsBufferDesc.elementCount;
            statsBufferDesc.alignment = alignof(GpuData::DrawStatsGpu);
            result = builder.create_buffer(statsBufferDesc, m_drawStatsBuffer);
            if (!result)
            {
                return result;
            }

            if (m_bufferManager == nullptr)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "DrawStatsReadbackPass requires a buffer manager.");
            }
            result = m_bufferManager->get_readback_buffer_view(
                m_drawStatsBuffer, m_drawStatsReadbackView);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "DrawStatsReadbackRootSignature";
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
            result = builder.create_root_signature(rootSignatureDesc,
                                                   m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "DrawStatsReadbackCS";
            shaderDesc.filePath = "Shaders/D3D12/DrawStatsReadback.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "DrawStatsReadbackPipeline";
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
            result = builder.use_buffer(
                m_visibleObjectCountBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_indirectCommandCountBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_drawStatsBuffer, RHI::ResourceAccessType::Write,
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

            update_stats_from_readback(context.frame_index());
            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, frameState.objectCount);
            commandContext->set_srv(1, m_renderObjectBuffer);
            commandContext->set_srv(2, m_visibleObjectCountBuffer);
            commandContext->set_srv(3, m_indirectCommandCountBuffer);
            commandContext->set_uav(4, m_drawStatsBuffer);
            commandContext->dispatch(1, 1, 1);

            commandContext->resource_barrier(
                m_drawStatsBuffer,
                RHI::ResourceBarrierDesc{
                    RHI::ResourceState::UnorderedAccess,
                    RHI::ResourceState::CopySource});

            RHI::BufferToReadbackCopyRegion copyRegion{};
            copyRegion.srcBufferHandle = m_drawStatsBuffer;
            copyRegion.srcDefaultResourceIndex = 0u;
            copyRegion.srcByteOffset = 0u;
            copyRegion.dstBufferHandle = m_drawStatsBuffer;
            copyRegion.dstReadbackResourceIndex = context.frame_index();
            copyRegion.dstByteOffset = 0u;
            copyRegion.byteSize = sizeof(GpuData::DrawStatsGpu);
            commandContext->copy_buffer_region_to_readback(copyRegion);

            commandContext->resource_barrier(
                m_drawStatsBuffer,
                RHI::ResourceBarrierDesc{
                    RHI::ResourceState::CopySource,
                    RHI::ResourceState::UnorderedAccess});
        }

      private:
        void update_stats_from_readback(uint32_t frameIndex) noexcept
        {
            if (m_statsOutput == nullptr ||
                frameIndex >= m_drawStatsReadbackView.mappedDatas.size())
            {
                return;
            }

            const std::byte* mappedData =
                m_drawStatsReadbackView.mappedDatas[frameIndex];
            if (mappedData == nullptr)
            {
                return;
            }

            GpuData::DrawStatsGpu stats{};
            std::memcpy(&stats, mappedData, sizeof(stats));
            m_statsOutput->visibleObjects = stats.visibleObjects;
            m_statsOutput->occludedObjects = stats.culledObjects;
            m_statsOutput->frustumCulledObjects = 0u;
            m_statsOutput->indirectDrawCount = stats.indirectDrawCount;
            m_statsOutput->instanceCount = stats.instanceCount;
            m_statsOutput->savedObjectEstimate = stats.culledObjects;
            m_statsOutput->lodObjectCounts = {
                stats.lod0Count,
                stats.lod1Count,
                stats.lod2Count,
                stats.lod3Count,
                stats.lod4Count,
            };
            m_statsOutput->impostorCount = stats.lod4Count;
        }

        RHI::IBufferManager* m_bufferManager = nullptr;
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::BufferHandle m_indirectCommandCountBuffer{};
        RHI::BufferHandle m_drawStatsBuffer{};
        RHI::ReadbackBufferView m_drawStatsReadbackView{};
        EngineDebugStats* m_statsOutput = nullptr;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };
} // namespace Cue::DrawSystem
