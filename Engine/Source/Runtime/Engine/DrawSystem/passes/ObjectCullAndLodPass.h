#pragma once

/// ****************************************************************************
/// Build final visible object list with frustum culling and LOD selection
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "GpuData/Batching.h"

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cstring>

namespace Cue::DrawSystem
{
    class ObjectCullAndLodPass final : public RHI::FrameGraphPass
    {
    public:
        ObjectCullAndLodPass(
            const DrawFrameState& drawFrameState,
            RHI::IBufferManager* bufferManager,
            GpuData::ObjectCullLodStatsGpu* statsOutput,
            RHI::BufferHandle renderableInfoBuffer,
            RHI::BufferHandle viewProjectionBuffer,
            RHI::BufferHandle renderObjectBuffer,
            RHI::BufferHandle visibleObjectCountBuffer,
            RHI::ViewHandle visibleObjectCountUav,
            bool enableOcclusion = true,
            bool preferVisibilityPackableLod = false,
            bool enableStatsReadback = true)
            : m_drawFrameState(drawFrameState)
            , m_bufferManager(bufferManager)
            , m_statsOutput(statsOutput)
            , m_renderableInfoBuffer(renderableInfoBuffer)
            , m_viewProjectionBuffer(viewProjectionBuffer)
            , m_renderObjectBuffer(renderObjectBuffer)
            , m_visibleObjectCountBuffer(visibleObjectCountBuffer)
            , m_visibleObjectCountUav(visibleObjectCountUav)
            , m_enableOcclusion(enableOcclusion)
            , m_preferVisibilityPackableLod(preferVisibilityPackableLod)
            , m_enableStatsReadback(enableStatsReadback)
        {}

        const char* name() const noexcept override
        {
            return "ObjectCullAndLod";
        }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_renderableInfoBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_viewProjectionBuffer);
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
            m_hasPreviousHiZ = false;
            for (uint32_t hizIndex = 0u; hizIndex < 2u; ++hizIndex)
            {
                const bool first = hizIndex == 0u;
                result = builder.get_view(
                    first ? "ChunkHiZ.SRV0" : "ChunkHiZ.SRV1",
                    m_hizSrvs[hizIndex]);
                if (!result)
                {
                    return result;
                }
                result = builder.get_texture(
                    first ? "ChunkHiZ.Texture0" : "ChunkHiZ.Texture1",
                    m_hizTextures[hizIndex]);
                if (!result)
                {
                    return result;
                }
            }
            result = builder.get_buffer("ChunkOcclusionStatsBuffer", m_occlusionStatsBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc lodStatsDesc{};
            lodStatsDesc.name = "ObjectCullLodStatsBuffer";
            lodStatsDesc.type = RHI::BufferType::Raw;
            lodStatsDesc.defaultHeapCount = 1u;
            lodStatsDesc.uploadHeapCount = 0u;
            lodStatsDesc.readbackHeapCount =
                m_enableStatsReadback ? builder.buffer_count() : 0u;
            lodStatsDesc.initialState = RHI::ResourceState::UnorderedAccess;
            lodStatsDesc.stride = sizeof(GpuData::ObjectCullLodStatsGpu);
            lodStatsDesc.elementCount = 1u;
            lodStatsDesc.size = sizeof(GpuData::ObjectCullLodStatsGpu);
            lodStatsDesc.alignment = alignof(GpuData::ObjectCullLodStatsGpu);
            result = builder.create_buffer(lodStatsDesc, m_lodStatsBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc lodStatsUavDesc{};
            lodStatsUavDesc.name = "ObjectCullLodStatsBufferUAV";
            lodStatsUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            lodStatsUavDesc.bufferKind = RHI::BufferKind::Buffer;
            lodStatsUavDesc.bufferHandle = m_lodStatsBuffer;
            lodStatsUavDesc.numElements = lodStatsDesc.size / sizeof(uint32_t);
            result = builder.create_view(lodStatsUavDesc, m_lodStatsUav);
            if (!result)
            {
                return result;
            }

            if (m_enableStatsReadback && m_bufferManager == nullptr)
            {
                return Result::fail(
                    Code::InvalidState, Severity::Error,
                    "ObjectCullAndLodPass requires a buffer manager for stats readback.");
            }
            if (m_enableStatsReadback)
            {
                result = m_bufferManager->get_readback_buffer_view(
                    m_lodStatsBuffer, m_lodStatsReadbackView);
                if (!result)
                {
                    return result;
                }
            }
            result = builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ObjectCullAndLodRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::DescriptorTableSRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 4 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 5 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 3 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "ObjectCullAndLodCS";
            shaderDesc.filePath = "Shaders/D3D12/ObjectCullAndLod.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ObjectCullAndLodPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_renderableInfoBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_viewProjectionBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_meshRangeBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_renderObjectBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_visibleObjectCountBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            for (RHI::TextureHandle hizTexture : m_hizTextures)
            {
                result = builder.use_texture(
                    hizTexture, RHI::ResourceAccessType::Read,
                    RHI::ResourceState::ShaderResource,
                    RHI::ResourceState::ShaderResource);
                if (!result)
                {
                    return result;
                }
            }
            result = builder.use_buffer(
                m_occlusionStatsBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::UnorderedAccess);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_lodStatsBuffer,
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

            if (m_enableStatsReadback)
            {
                update_stats_from_readback(context.frame_index());
            }

            const uint32_t clearValues[4] = { 0, 0, 0, 0 };
            commandContext->clear_unordered_access_uint(
                m_visibleObjectCountUav, clearValues);
            commandContext->clear_unordered_access_uint(
                m_lodStatsUav, clearValues);

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, frameState.objectCount);
            commandContext->set_cbv(1, m_viewProjectionBuffer);
            commandContext->set_srv(2, m_renderableInfoBuffer);
            commandContext->set_srv(3, m_meshRangeBuffer);
            commandContext->set_uav(4, m_renderObjectBuffer);
            commandContext->set_uav(5, m_visibleObjectCountBuffer);
            const uint32_t readIndex = (context.frame_index() + 1u) & 1u;
            commandContext->set_compute_descriptor_table(6,
                m_hizSrvs[readIndex]);
            commandContext->set_32bit_constant(7, std::max(1u, context.width() / 4u));
            commandContext->set_32bit_constant(8, std::max(1u, context.height() / 4u));
            commandContext->set_32bit_constant(
                9, m_enableOcclusion && m_hasPreviousHiZ ? 1u : 0u);
            commandContext->set_32bit_constant(
                10, m_preferVisibilityPackableLod ? 1u : 0u);
            commandContext->set_uav(11, m_occlusionStatsBuffer);
            commandContext->set_uav(12, m_lodStatsBuffer);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
            m_hasPreviousHiZ = true;

            if (!m_enableStatsReadback || !should_copy_stats_to_readback())
            {
                return;
            }

            commandContext->uav_barrier(m_lodStatsBuffer);
            commandContext->resource_barrier(
                m_lodStatsBuffer,
                RHI::ResourceBarrierDesc{RHI::ResourceState::UnorderedAccess,
                    RHI::ResourceState::CopySource});

            RHI::BufferToReadbackCopyRegion statsCopyRegion{};
            statsCopyRegion.srcBufferHandle = m_lodStatsBuffer;
            statsCopyRegion.srcDefaultResourceIndex = 0u;
            statsCopyRegion.srcByteOffset = 0u;
            statsCopyRegion.dstBufferHandle = m_lodStatsBuffer;
            statsCopyRegion.dstReadbackResourceIndex = context.frame_index();
            statsCopyRegion.dstByteOffset = 0u;
            statsCopyRegion.byteSize = sizeof(GpuData::ObjectCullLodStatsGpu);
            commandContext->copy_buffer_region_to_readback(statsCopyRegion);

            commandContext->resource_barrier(
                m_lodStatsBuffer,
                RHI::ResourceBarrierDesc{RHI::ResourceState::CopySource,
                    RHI::ResourceState::UnorderedAccess});
        }

    private:
        static constexpr uint64_t kStatsReadbackWarmupFrames = 8u;
        static constexpr uint64_t kStatsReadbackIntervalFrames = 30u;

        bool should_copy_stats_to_readback() noexcept
        {
            const uint64_t frameIndex = m_statsReadbackFrameCount++;
            return frameIndex < kStatsReadbackWarmupFrames ||
                (frameIndex % kStatsReadbackIntervalFrames) == 0u;
        }

        void update_stats_from_readback(uint32_t frameIndex) noexcept
        {
            if (m_statsOutput == nullptr ||
                frameIndex >= m_lodStatsReadbackView.mappedDatas.size())
            {
                return;
            }

            const std::byte* mappedData =
                m_lodStatsReadbackView.mappedDatas[frameIndex];
            if (mappedData == nullptr)
            {
                return;
            }

            GpuData::ObjectCullLodStatsGpu stats{};
            std::memcpy(&stats, mappedData, sizeof(stats));
            *m_statsOutput = stats;
        }

        const DrawFrameState& m_drawFrameState;
        RHI::IBufferManager* m_bufferManager = nullptr;
        GpuData::ObjectCullLodStatsGpu* m_statsOutput = nullptr;
        RHI::BufferHandle m_renderableInfoBuffer{};
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_meshRangeBuffer{};
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::ViewHandle m_visibleObjectCountUav{};
        std::array<RHI::ViewHandle, 2> m_hizSrvs{};
        std::array<RHI::TextureHandle, 2> m_hizTextures{};
        RHI::BufferHandle m_occlusionStatsBuffer{};
        RHI::BufferHandle m_lodStatsBuffer{};
        RHI::ViewHandle m_lodStatsUav{};
        RHI::ReadbackBufferView m_lodStatsReadbackView{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
        uint64_t m_statsReadbackFrameCount = 0;
        bool m_hasPreviousHiZ = false;
        bool m_enableOcclusion = true;
        bool m_preferVisibilityPackableLod = false;
        bool m_enableStatsReadback = true;
    };
}
