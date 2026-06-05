#pragma once

/// ****************************************************************************
/// Clustered forward light assignment
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "GpuData/ClusteredLighting.h"
#include "LightingSystem/GpuData/LightData.h"

// === C++ includes ===
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Cue::DrawSystem
{
    namespace ClusteredLighting
    {
        static constexpr uint32_t k_clusterCountX = 16u;
        static constexpr uint32_t k_clusterCountY = 9u;
        static constexpr uint32_t k_depthSliceCount = 24u;
        static constexpr uint32_t k_maxLightsPerCluster = 128u;
        static constexpr uint32_t k_maxClusterLightItemsPerCluster = 64u;
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
        ClusterLightCullingPass(RHI::IBufferManager* bufferManager,
                                uint32_t maxPointLightCount,
                                GpuData::ClusterLightingStatsGpu* statsOutput)
            : m_bufferManager(bufferManager),
              m_statsOutput(statsOutput),
              m_maxPointLightCount(maxPointLightCount)
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
            // fixed grid count 方式。記事の 16x8x24 に近い粒度で、
            // ここでは 16x9x24 を固定 cluster grid として使う。
            m_screenWidth = builder.width();
            m_screenHeight = builder.height();
            m_tileCountX = ClusteredLighting::k_clusterCountX;
            m_tileCountY = ClusteredLighting::k_clusterCountY;
            m_depthSliceCount = ClusteredLighting::k_depthSliceCount;
            m_clusterCount = m_tileCountX * m_tileCountY * m_depthSliceCount;
            m_pointLightCount = m_maxPointLightCount;
            m_maxLightsPerCluster =
                (std::min)(m_maxPointLightCount,
                           ClusteredLighting::k_maxLightsPerCluster);
            m_maxLightsPerCluster = (std::max)(m_maxLightsPerCluster, 1u);
            m_maxClusterLightItems =
                m_clusterCount *
                ClusteredLighting::k_maxClusterLightItemsPerCluster;
            if (m_clusterCount == 0u)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                                    "Cluster light culling cluster count must not be zero.");
            }

            Result result = Result::ok();
            result = builder.get_buffer("ViewPointLightBuffer",
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
            // cluster -> compact light list の参照表。
            // Forward shader は pixel の cluster id からここを引き、offset/count を得る。
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

            RHI::BufferDesc indexBufferDesc{};
            indexBufferDesc.name = "ClusterLightIndexBuffer";
            // 全 cluster 固定領域ではなく、代表 cluster が append した light id
            // だけを詰める compact buffer。
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

            RHI::BufferDesc itemCounterBufferDesc{};
            itemCounterBufferDesc.name = "ClusterLightItemCounterBuffer";
            // compact buffer の append offset。代表 cluster が InterlockedAdd で
            // list の書き込み開始位置を確保する。
            itemCounterBufferDesc.type = RHI::BufferType::Raw;
            itemCounterBufferDesc.defaultHeapCount = 1;
            itemCounterBufferDesc.uploadHeapCount = 0;
            itemCounterBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            itemCounterBufferDesc.stride = sizeof(uint32_t);
            itemCounterBufferDesc.elementCount = 1u;
            itemCounterBufferDesc.size =
                itemCounterBufferDesc.stride *
                itemCounterBufferDesc.elementCount;
            itemCounterBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(itemCounterBufferDesc,
                                           m_clusterLightItemCounterBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc itemCounterUavDesc{};
            itemCounterUavDesc.name = "ClusterLightItemCounterBufferUAV";
            itemCounterUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            itemCounterUavDesc.bufferKind = RHI::BufferKind::Buffer;
            itemCounterUavDesc.bufferHandle = m_clusterLightItemCounterBuffer;
            itemCounterUavDesc.numElements =
                itemCounterBufferDesc.size / sizeof(uint32_t);
            result = builder.create_view(itemCounterUavDesc,
                                         m_clusterLightItemCounterUav);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc statsBufferDesc{};
            statsBufferDesc.name = "ClusterLightStatsBuffer";
            // GPU 上で assignment 結果を集計し、readback heap 経由で ImGui に出す。
            // stats なしでは hash sharing / compaction の効果を判断できない。
            statsBufferDesc.type = RHI::BufferType::Raw;
            statsBufferDesc.defaultHeapCount = 1;
            statsBufferDesc.uploadHeapCount = 0;
            statsBufferDesc.readbackHeapCount = builder.buffer_count();
            statsBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            statsBufferDesc.stride = sizeof(GpuData::ClusterLightingStatsGpu);
            statsBufferDesc.elementCount = 1u;
            statsBufferDesc.size =
                statsBufferDesc.stride * statsBufferDesc.elementCount;
            statsBufferDesc.alignment =
                alignof(GpuData::ClusterLightingStatsGpu);
            result = builder.create_buffer(statsBufferDesc,
                                           m_clusterLightStatsBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc statsUavDesc{};
            statsUavDesc.name = "ClusterLightStatsBufferUAV";
            statsUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            statsUavDesc.bufferKind = RHI::BufferKind::Buffer;
            statsUavDesc.bufferHandle = m_clusterLightStatsBuffer;
            statsUavDesc.numElements =
                statsBufferDesc.size / sizeof(uint32_t);
            result = builder.create_view(statsUavDesc,
                                         m_clusterLightStatsUav);
            if (!result)
            {
                return result;
            }
            if (m_bufferManager == nullptr)
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "ClusterLightCullingPass requires a buffer manager.");
            }
            result = m_bufferManager->get_readback_buffer_view(
                m_clusterLightStatsBuffer,
                m_clusterLightStatsReadbackView);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ClusterLightCullingRootSignature";
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 0});
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
                {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 2});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 3});
            result =
                builder.create_root_signature(rootSignatureDesc,
                                              m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "ClusterLightCullingCS";
            shaderDesc.filePath = "Shaders/D3D12/ClusterLightCulling.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ClusterLightCullingPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = Result::ok();
            result = builder.use_buffer(
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
            result = builder.use_buffer(
                m_clusterLightItemCounterBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::UnorderedAccess);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_clusterLightStatsBuffer,
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

            // 前回この frame slot にコピーされた GPU stats を CPU 側へ反映する。
            // render graph は waitForCompletion なので、この readback は安定している。
            update_stats_from_readback(context.frame_index());

            const uint32_t clearValues[4] = {0u, 0u, 0u, 0u};

            // counter/stats は frame ごとに再構築する。残値があると avg/max や
            // compact buffer offset が破綻する。
            commandContext->clear_unordered_access_uint(
                m_clusterLightItemCounterUav, clearValues);
            commandContext->clear_unordered_access_uint(
                m_clusterLightStatsUav, clearValues);

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, m_clusterCount);
            commandContext->set_32bit_constant(1, m_pointLightCount);
            commandContext->set_32bit_constant(2, m_maxClusterLightItems);
            commandContext->set_32bit_constant(3, m_maxLightsPerCluster);
            commandContext->set_srv(4, m_clusterBuffer);
            commandContext->set_srv(5, m_viewPointLightBuffer);
            commandContext->set_uav(6, m_clusterLightRangeBuffer);
            commandContext->set_uav(7, m_clusterLightIndexBuffer);
            commandContext->set_uav(8, m_clusterLightItemCounterBuffer);
            commandContext->set_uav(9, m_clusterLightStatsBuffer);
            commandContext->dispatch((m_clusterCount + 127u) / 128u, 1u, 1u);

            // stats buffer は ImGui 表示用に CPU readback する。
            // UAV 書き込み後なので CopySource へ遷移してから readback heap へコピーする。
            commandContext->resource_barrier(
                m_clusterLightStatsBuffer,
                RHI::ResourceBarrierDesc{
                    RHI::ResourceState::UnorderedAccess,
                    RHI::ResourceState::CopySource});

            RHI::BufferToReadbackCopyRegion statsCopyRegion{};
            statsCopyRegion.srcBufferHandle = m_clusterLightStatsBuffer;
            statsCopyRegion.srcDefaultResourceIndex = 0u;
            statsCopyRegion.srcByteOffset = 0u;
            statsCopyRegion.dstBufferHandle = m_clusterLightStatsBuffer;
            statsCopyRegion.dstReadbackResourceIndex = context.frame_index();
            statsCopyRegion.dstByteOffset = 0u;
            statsCopyRegion.byteSize =
                sizeof(GpuData::ClusterLightingStatsGpu);
            commandContext->copy_buffer_region_to_readback(statsCopyRegion);

            // 後続 frame でも UAV として使うため、FrameGraph の final state と
            // 実 resource state を揃えておく。
            commandContext->resource_barrier(
                m_clusterLightStatsBuffer,
                RHI::ResourceBarrierDesc{
                    RHI::ResourceState::CopySource,
                    RHI::ResourceState::UnorderedAccess});
        }

      private:
        void update_stats_from_readback(uint32_t frameIndex) noexcept
        {
            // GPU stats は 1 buffer に default/readback heap を併設している。
            // frame index に対応する readback slot から構造体として読む。
            if (m_statsOutput == nullptr ||
                frameIndex >=
                    m_clusterLightStatsReadbackView.mappedDatas.size())
            {
                return;
            }

            const std::byte* mappedData =
                m_clusterLightStatsReadbackView.mappedDatas[frameIndex];
            if (mappedData == nullptr)
            {
                return;
            }

            GpuData::ClusterLightingStatsGpu stats{};
            std::memcpy(&stats, mappedData, sizeof(stats));
            *m_statsOutput = stats;
        }

        RHI::IBufferManager* m_bufferManager = nullptr;
        GpuData::ClusterLightingStatsGpu* m_statsOutput = nullptr;
        RHI::BufferHandle m_viewPointLightBuffer{};
        RHI::BufferHandle m_clusterBuffer{};
        RHI::BufferHandle m_clusterLightRangeBuffer{};
        RHI::BufferHandle m_clusterLightIndexBuffer{};
        RHI::BufferHandle m_clusterLightItemCounterBuffer{};
        RHI::BufferHandle m_clusterLightStatsBuffer{};
        RHI::ViewHandle m_clusterLightItemCounterUav{};
        RHI::ViewHandle m_clusterLightStatsUav{};
        RHI::ReadbackBufferView m_clusterLightStatsReadbackView{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
        uint32_t m_maxPointLightCount = 0;
        uint32_t m_pointLightCount = 0;
        uint32_t m_screenWidth = 0;
        uint32_t m_screenHeight = 0;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        uint32_t m_depthSliceCount = 0;
        uint32_t m_clusterCount = 0;
        uint32_t m_maxLightsPerCluster = 0;
        uint32_t m_maxClusterLightItems = 0;
    };
} // namespace Cue::DrawSystem
