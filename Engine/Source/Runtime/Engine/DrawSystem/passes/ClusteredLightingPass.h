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

namespace Cue::DrawSystem
{
    namespace ClusteredLighting
    {
        static constexpr uint32_t k_tileSize = 32u;
        static constexpr uint32_t k_depthSliceCount = 16u;
        static constexpr uint32_t k_maxLightsPerCluster = 32u;
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
            m_tileCountX =
                (m_screenWidth + ClusteredLighting::k_tileSize - 1u) /
                ClusteredLighting::k_tileSize;
            m_tileCountY =
                (m_screenHeight + ClusteredLighting::k_tileSize - 1u) /
                ClusteredLighting::k_tileSize;
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

    class ClusterLightCullingPass final : public RHI::FrameGraphPass
    {
      public:
        ClusterLightCullingPass(RHI::BufferHandle viewProjectionBuffer,
                                RHI::BufferHandle lightFrameBuffer,
                                RHI::BufferHandle pointLightBuffer,
                                uint32_t maxPointLightCount)
            : m_viewProjectionBuffer(viewProjectionBuffer),
              m_lightFrameBuffer(lightFrameBuffer),
              m_pointLightBuffer(pointLightBuffer),
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
            m_screenWidth = builder.width();
            m_screenHeight = builder.height();
            m_tileCountX =
                (m_screenWidth + ClusteredLighting::k_tileSize - 1u) /
                ClusteredLighting::k_tileSize;
            m_tileCountY =
                (m_screenHeight + ClusteredLighting::k_tileSize - 1u) /
                ClusteredLighting::k_tileSize;
            m_depthSliceCount = ClusteredLighting::k_depthSliceCount;
            m_clusterCount = m_tileCountX * m_tileCountY * m_depthSliceCount;
            m_maxLightsPerCluster =
                (std::min)(m_maxPointLightCount,
                           ClusteredLighting::k_maxLightsPerCluster);
            m_maxLightsPerCluster = (std::max)(m_maxLightsPerCluster, 1u);
            if (m_clusterCount == 0u)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                                    "Cluster light culling cluster count must not be zero.");
            }

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

            RHI::BufferDesc indexBufferDesc{};
            indexBufferDesc.name = "ClusterLightIndexBuffer";
            indexBufferDesc.type = RHI::BufferType::UnorderedAccess;
            indexBufferDesc.defaultHeapCount = 1;
            indexBufferDesc.uploadHeapCount = 0;
            indexBufferDesc.initialState =
                RHI::ResourceState::UnorderedAccess;
            indexBufferDesc.stride = sizeof(uint32_t);
            indexBufferDesc.elementCount =
                m_clusterCount * m_maxLightsPerCluster;
            indexBufferDesc.size =
                indexBufferDesc.stride * indexBufferDesc.elementCount;
            indexBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(indexBufferDesc,
                                           m_clusterLightIndexBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ClusterLightCullingRootSignature";
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
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 6});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::_32BitConstants,
                 RHI::ShaderVisibility::All, 7});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 2});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
            rootSignatureDesc.parameters.push_back(
                {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1});
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
            return builder.use_buffer(
                m_clusterLightIndexBuffer, RHI::ResourceAccessType::Write,
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
            commandContext->set_32bit_constant(0, m_clusterCount);
            commandContext->set_32bit_constant(1, m_tileCountX);
            commandContext->set_32bit_constant(2, m_tileCountY);
            commandContext->set_32bit_constant(3, m_depthSliceCount);
            commandContext->set_32bit_constant(4, m_maxLightsPerCluster);
            commandContext->set_cbv(5, m_viewProjectionBuffer);
            commandContext->set_cbv(6, m_lightFrameBuffer);
            commandContext->set_srv(7, m_clusterBuffer);
            commandContext->set_srv(8, m_pointLightBuffer);
            commandContext->set_uav(9, m_clusterLightRangeBuffer);
            commandContext->set_uav(10, m_clusterLightIndexBuffer);
            commandContext->dispatch((m_clusterCount + 63u) / 64u, 1u, 1u);
        }

      private:
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_lightFrameBuffer{};
        RHI::BufferHandle m_pointLightBuffer{};
        RHI::BufferHandle m_clusterBuffer{};
        RHI::BufferHandle m_clusterLightRangeBuffer{};
        RHI::BufferHandle m_clusterLightIndexBuffer{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
        uint32_t m_maxPointLightCount = 0;
        uint32_t m_screenWidth = 0;
        uint32_t m_screenHeight = 0;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        uint32_t m_depthSliceCount = 0;
        uint32_t m_clusterCount = 0;
        uint32_t m_maxLightsPerCluster = 0;
    };
} // namespace Cue::DrawSystem
