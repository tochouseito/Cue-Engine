#pragma once

/// ****************************************************************************
/// Clustered forward のライト割り当て
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "GpuData/ClusteredLighting.h"
#include "LightingSystem/GpuData/LightData.h"

// === C++ includes ===
#include <algorithm>
#include <cstdint>

namespace Cue::DrawSystem
{
    namespace ClusteredLighting
    {
        static constexpr uint32_t k_clusterCountX = 16u;
        static constexpr uint32_t k_clusterCountY = 9u;
        static constexpr uint32_t k_depthSliceCount = 24u;
        static constexpr uint32_t k_maxLightsPerCluster = 128u;
    } // namespace ClusteredLighting

    class BuildClusterGridPass final : public RHI::FrameGraphPass
    {
      public:
        explicit BuildClusterGridPass(RHI::BufferHandle viewProjectionBuffer)
            : m_viewProjectionBuffer(viewProjectionBuffer)
        {
        }

        const char* name() const noexcept override
        {
            return "BuildClusterGrid";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            m_screenWidth = builder.width();
            m_screenHeight = builder.height();
            m_tileCountX = ClusteredLighting::k_clusterCountX;
            m_tileCountY = ClusteredLighting::k_clusterCountY;
            m_clusterCount = m_tileCountX * m_tileCountY *
                             ClusteredLighting::k_depthSliceCount;
            if (m_tileCountX == 0u || m_tileCountY == 0u ||
                m_clusterCount == 0u)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                                    "Cluster count must not be zero.");
            }

            Result result = builder.read_buffer(m_viewProjectionBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc clusterBufferDesc{};
            clusterBufferDesc.name = "ClusterBuffer";
            clusterBufferDesc.type = RHI::BufferType::UnorderedAccess;
            clusterBufferDesc.defaultHeapCount = 1;
            clusterBufferDesc.uploadHeapCount = 0;
            clusterBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            clusterBufferDesc.stride = sizeof(GpuData::ClusterGpu);
            clusterBufferDesc.elementCount = m_clusterCount;
            clusterBufferDesc.size =
                clusterBufferDesc.stride * clusterBufferDesc.elementCount;
            clusterBufferDesc.alignment = alignof(GpuData::ClusterGpu);
            result =
                builder.create_buffer(clusterBufferDesc, m_clusterBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "BuildClusterGridRootSignature";
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 1});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 2});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 3});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 4});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 5});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
            result =
                builder.create_root_signature(rootSignatureDesc,
                                              m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "BuildClusterGridCS";
            shaderDesc.filePath = "Shaders/D3D12/BuildClusterGrid.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "BuildClusterGridPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_viewProjectionBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_clusterBuffer, RHI::ResourceAccessType::Write,
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

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, m_screenWidth);
            commandContext->set_32bit_constant(1, m_screenHeight);
            commandContext->set_32bit_constant(2, m_tileCountX);
            commandContext->set_32bit_constant(3, m_tileCountY);
            commandContext->set_32bit_constant(
                4, ClusteredLighting::k_depthSliceCount);
            commandContext->set_cbv(5, m_viewProjectionBuffer);
            commandContext->set_uav(6, m_clusterBuffer);
            commandContext->dispatch((m_tileCountX + 7u) / 8u,
                                     (m_tileCountY + 7u) / 8u,
                                     ClusteredLighting::k_depthSliceCount);
        }

      private:
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_clusterBuffer{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
        uint32_t m_screenWidth = 0;
        uint32_t m_screenHeight = 0;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        uint32_t m_clusterCount = 0;
    };

    class PreparePointLightsPass final : public RHI::FrameGraphPass
    {
      public:
        PreparePointLightsPass(RHI::BufferHandle viewProjectionBuffer,
                               RHI::BufferHandle lightFrameBuffer,
                               RHI::BufferHandle pointLightBuffer,
                               uint32_t maxPointLightCount)
            : m_viewProjectionBuffer(viewProjectionBuffer),
              m_lightFrameBuffer(lightFrameBuffer),
              m_pointLightBuffer(pointLightBuffer),
              m_maxPointLightCount((std::max)(maxPointLightCount, 1u))
        {
        }

        const char* name() const noexcept override
        {
            return "PreparePointLights";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            // PointLightBuffer は world-space で保持している。
            // ClusterLightCulling は view-space cluster と比較するため、
            // この pass で view-space light buffer を作る。
            Result result = builder.read_buffer(m_viewProjectionBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_lightFrameBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_pointLightBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc viewPointLightBufferDesc{};
            viewPointLightBufferDesc.name = "ViewPointLightBuffer";
            viewPointLightBufferDesc.type = RHI::BufferType::UnorderedAccess;
            viewPointLightBufferDesc.defaultHeapCount = 1;
            viewPointLightBufferDesc.uploadHeapCount = 0;
            viewPointLightBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            viewPointLightBufferDesc.stride = sizeof(GpuData::ViewPointLightGpu);
            viewPointLightBufferDesc.elementCount = m_maxPointLightCount;
            viewPointLightBufferDesc.size =
                viewPointLightBufferDesc.stride *
                viewPointLightBufferDesc.elementCount;
            viewPointLightBufferDesc.alignment =
                alignof(GpuData::ViewPointLightGpu);
            result = builder.create_buffer(viewPointLightBufferDesc,
                                           m_viewPointLightBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "PreparePointLightsRootSignature";
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 2});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 3});
            result = builder.create_root_signature(rootSignatureDesc,
                                                   m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "PreparePointLightsCS";
            shaderDesc.filePath = "Shaders/D3D12/PreparePointLights.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "PreparePointLightsPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_viewProjectionBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_lightFrameBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_pointLightBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_viewPointLightBuffer, RHI::ResourceAccessType::Write,
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

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_cbv(0, m_viewProjectionBuffer);
            commandContext->set_cbv(1, m_lightFrameBuffer);
            commandContext->set_srv(2, m_pointLightBuffer);
            commandContext->set_uav(3, m_viewPointLightBuffer);

            // dispatch は thread group に丸めるため、shader 側でもこの capacity
            // を使って余り thread の UAV 範囲外書き込みを防ぐ。
            commandContext->set_32bit_constant(4, m_maxPointLightCount);
            commandContext->dispatch((m_maxPointLightCount + 63u) / 64u, 1u,
                                     1u);
        }

      private:
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_lightFrameBuffer{};
        RHI::BufferHandle m_pointLightBuffer{};
        RHI::BufferHandle m_viewPointLightBuffer{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
        uint32_t m_maxPointLightCount = 0;
    };

    class ClusterLightCullingPass final : public RHI::FrameGraphPass
    {
      public:
        explicit ClusterLightCullingPass(uint32_t maxPointLightCount)
            : m_maxPointLightCount(maxPointLightCount)
        {
        }

        const char* name() const noexcept override
        {
            return "ClusterLightCulling";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            m_tileCountX = ClusteredLighting::k_clusterCountX;
            m_tileCountY = ClusteredLighting::k_clusterCountY;
            m_depthSliceCount = ClusteredLighting::k_depthSliceCount;
            m_clusterCount = m_tileCountX * m_tileCountY * m_depthSliceCount;
            m_pointLightCount = (std::max)(m_maxPointLightCount, 1u);
            m_maxLightsPerCluster =
                (std::min)(m_pointLightCount,
                           ClusteredLighting::k_maxLightsPerCluster);
            m_maxLightsPerCluster = (std::max)(m_maxLightsPerCluster, 1u);
            m_maxClusterLightItems =
                m_clusterCount * m_maxLightsPerCluster;
            if (m_clusterCount == 0u)
            {
                return Result::fail(
                    Code::InvalidArgument, Severity::Error,
                    "Cluster light culling cluster count must not be zero.");
            }

            Result result = builder.get_buffer("ViewPointLightBuffer",
                                               m_viewPointLightBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("ClusterBuffer", m_clusterBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc rangeBufferDesc{};
            rangeBufferDesc.name = "ClusterLightRangeBuffer";
            rangeBufferDesc.type = RHI::BufferType::UnorderedAccess;
            rangeBufferDesc.defaultHeapCount = 1;
            rangeBufferDesc.uploadHeapCount = 0;
            rangeBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            rangeBufferDesc.stride = sizeof(GpuData::ClusterLightRangeGpu);
            rangeBufferDesc.elementCount = m_clusterCount;
            rangeBufferDesc.size =
                rangeBufferDesc.stride * rangeBufferDesc.elementCount;
            rangeBufferDesc.alignment =
                alignof(GpuData::ClusterLightRangeGpu);
            result = builder.create_buffer(rangeBufferDesc,
                                           m_clusterLightRangeBuffer);
            if (!result)
            {
                return result;
            }

            // Each cluster owns a fixed range. This removes the private
            // 128-entry list and list-sharing synchronization from each
            // cluster thread while preserving the forward range/index ABI.
            RHI::BufferDesc indexBufferDesc{};
            indexBufferDesc.name = "ClusterLightIndexBuffer";
            indexBufferDesc.type = RHI::BufferType::UnorderedAccess;
            indexBufferDesc.defaultHeapCount = 1;
            indexBufferDesc.uploadHeapCount = 0;
            indexBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            indexBufferDesc.stride = sizeof(uint32_t);
            indexBufferDesc.elementCount = m_maxClusterLightItems;
            indexBufferDesc.size =
                indexBufferDesc.stride * indexBufferDesc.elementCount;
            indexBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(indexBufferDesc,
                                           m_clusterLightIndexBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc candidateCountDesc{};
            candidateCountDesc.name = "ClusterLightCandidateCountBuffer";
            candidateCountDesc.type = RHI::BufferType::Raw;
            candidateCountDesc.defaultHeapCount = 1;
            candidateCountDesc.uploadHeapCount = 0;
            candidateCountDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            candidateCountDesc.stride = sizeof(uint32_t);
            candidateCountDesc.elementCount = m_clusterCount;
            candidateCountDesc.size =
                candidateCountDesc.stride * candidateCountDesc.elementCount;
            candidateCountDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(candidateCountDesc,
                                           m_candidateCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc candidateCountUavDesc{};
            candidateCountUavDesc.name =
                "ClusterLightCandidateCountBufferUAV";
            candidateCountUavDesc.type =
                RHI::ViewType::UnorderedAccessRawBuffer;
            candidateCountUavDesc.bufferKind = RHI::BufferKind::Buffer;
            candidateCountUavDesc.bufferHandle = m_candidateCountBuffer;
            candidateCountUavDesc.numElements =
                candidateCountDesc.size / sizeof(uint32_t);
            result = builder.create_view(candidateCountUavDesc,
                                         m_candidateCountUav);
            if (!result)
            {
                return result;
            }

            result = create_candidate_pipeline(builder);
            if (!result)
            {
                return result;
            }
            return create_finalize_pipeline(builder);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_viewPointLightBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_clusterBuffer, RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_clusterLightRangeBuffer, RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_clusterLightIndexBuffer, RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_candidateCountBuffer, RHI::ResourceAccessType::Write,
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

            const uint32_t clearValues[4] = {0u, 0u, 0u, 0u};
            commandContext->clear_unordered_access_uint(
                m_candidateCountUav, clearValues);

            // ClearUnorderedAccessView and the following dispatch both access
            // this buffer as UAV. This RHI has no UAV-barrier primitive, so
            // use real transitions to make the clear visible before atomics.
            synchronize_uav(commandContext, m_candidateCountBuffer);

            // Phase 1: one thread per light visits only the cluster-axis ranges
            // overlapped by that light and appends directly to fixed ranges.
            commandContext->set_compute_pipeline(m_candidatePipeline);
            commandContext->set_32bit_constant(0, m_clusterCount);
            commandContext->set_32bit_constant(1, m_pointLightCount);
            commandContext->set_32bit_constant(2, m_tileCountX);
            commandContext->set_32bit_constant(3, m_tileCountY);
            commandContext->set_32bit_constant(4, m_depthSliceCount);
            commandContext->set_32bit_constant(5, m_maxLightsPerCluster);
            commandContext->set_srv(6, m_clusterBuffer);
            commandContext->set_srv(7, m_viewPointLightBuffer);
            commandContext->set_uav(8, m_candidateCountBuffer);
            commandContext->set_uav(9, m_clusterLightIndexBuffer);
            commandContext->dispatch((m_pointLightCount + 63u) / 64u,
                                     1u, 1u);

            // The finalize phase consumes the counters as SRV. A real state
            // transition provides the required UAV write/read ordering because
            // this RHI does not expose a separate UAV barrier command.
            commandContext->resource_barrier(
                m_candidateCountBuffer,
                RHI::ResourceBarrierDesc{
                    RHI::ResourceState::UnorderedAccess,
                    RHI::ResourceState::ShaderResource});

            // Phase 2: publish the fixed range/count records consumed by the
            // forward pixel shader.
            commandContext->set_compute_pipeline(m_finalizePipeline);
            commandContext->set_32bit_constant(0, m_clusterCount);
            commandContext->set_32bit_constant(1, m_maxLightsPerCluster);
            commandContext->set_srv(2, m_candidateCountBuffer);
            commandContext->set_uav(3, m_clusterLightRangeBuffer);
            commandContext->dispatch((m_clusterCount + 127u) / 128u,
                                     1u, 1u);
        }

      private:
        static void synchronize_uav(RHI::ICommandContext* commandContext,
                                    RHI::BufferHandle buffer)
        {
            commandContext->resource_barrier(
                buffer,
                RHI::ResourceBarrierDesc{
                    RHI::ResourceState::UnorderedAccess,
                    RHI::ResourceState::Common});
            commandContext->resource_barrier(
                buffer,
                RHI::ResourceBarrierDesc{
                    RHI::ResourceState::Common,
                    RHI::ResourceState::UnorderedAccess});
        }

        Result create_candidate_pipeline(RHI::FrameGraphBuilder& builder)
        {
            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name =
                "ClusterLightCandidateRootSignature";
            for (uint32_t shaderRegister = 0u; shaderRegister < 6u;
                 ++shaderRegister)
            {
                rootSignatureDesc.parameters.push_back(
                    {RHI::RootParameterType::_32BitConstants,
                     RHI::ShaderVisibility::All, shaderRegister});
            }
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::SRV,
                 RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::SRV,
                 RHI::ShaderVisibility::All, 1});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV,
                 RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV,
                 RHI::ShaderVisibility::All, 1});
            Result result = builder.create_root_signature(
                rootSignatureDesc, m_candidateRootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "ClusterLightBuildCandidatesCS";
            shaderDesc.filePath =
                "Shaders/D3D12/ClusterLightCulling.hlsl";
            shaderDesc.entryPoint = "BuildCandidatesCS";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(
                shaderDesc, m_candidateComputeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ClusterLightCandidatePipeline";
            pipelineDesc.rootSignatureHandle = m_candidateRootSignature;
            pipelineDesc.csHandle = m_candidateComputeShader;
            return builder.create_compute_pipeline(
                pipelineDesc, m_candidatePipeline);
        }

        Result create_finalize_pipeline(RHI::FrameGraphBuilder& builder)
        {
            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name =
                "ClusterLightFinalizeRootSignature";
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 5});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::SRV,
                 RHI::ShaderVisibility::All, 2});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV,
                 RHI::ShaderVisibility::All, 2});
            Result result = builder.create_root_signature(
                rootSignatureDesc, m_finalizeRootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "ClusterLightFinalizeClustersCS";
            shaderDesc.filePath =
                "Shaders/D3D12/ClusterLightCulling.hlsl";
            shaderDesc.entryPoint = "FinalizeClustersCS";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(
                shaderDesc, m_finalizeComputeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ClusterLightFinalizePipeline";
            pipelineDesc.rootSignatureHandle = m_finalizeRootSignature;
            pipelineDesc.csHandle = m_finalizeComputeShader;
            return builder.create_compute_pipeline(
                pipelineDesc, m_finalizePipeline);
        }

        RHI::BufferHandle m_viewPointLightBuffer{};
        RHI::BufferHandle m_clusterBuffer{};
        RHI::BufferHandle m_clusterLightRangeBuffer{};
        RHI::BufferHandle m_clusterLightIndexBuffer{};
        RHI::BufferHandle m_candidateCountBuffer{};
        RHI::ViewHandle m_candidateCountUav{};
        RHI::RootSignatureHandle m_candidateRootSignature{};
        RHI::RootSignatureHandle m_finalizeRootSignature{};
        RHI::ShaderBlobHandle m_candidateComputeShader{};
        RHI::ShaderBlobHandle m_finalizeComputeShader{};
        RHI::PipelineStateHandle m_candidatePipeline{};
        RHI::PipelineStateHandle m_finalizePipeline{};
        uint32_t m_maxPointLightCount = 0;
        uint32_t m_pointLightCount = 0;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        uint32_t m_depthSliceCount = 0;
        uint32_t m_clusterCount = 0;
        uint32_t m_maxLightsPerCluster = 0;
        uint32_t m_maxClusterLightItems = 0;
    };
} // namespace Cue::DrawSystem
