#pragma once

// === RHI includes ===
#include <BufferManager.h>
#include <FrameGraph.h>

// === Engine includes ===
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace Cue
{
    class DebugDrawPass final : public RHI::FrameGraphPass
    {
    public:
        DebugDrawPass(std::string_view a_name,
            std::string_view a_colorName,
            std::string_view a_colorRtvName,
            std::string_view a_depthName,
            std::string_view a_depthDsvName,
            GameCore::GameWorld& a_world,
            RHI::IBufferManager& a_bufferManager,
            RHI::BufferHandle a_viewProjectionBufferHandle) noexcept
            : m_name(a_name)
            , m_colorName(a_colorName)
            , m_colorRtvName(a_colorRtvName)
            , m_depthName(a_depthName)
            , m_depthDsvName(a_depthDsvName)
            , m_world(a_world)
            , m_bufferManager(a_bufferManager)
            , m_viewProjectionBufferHandle(a_viewProjectionBufferHandle)
        {}

        const char* name() const noexcept override
        {
            return m_name.c_str();
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture(m_colorName, m_colorHandle);
            if (!result)
            {
                return result;
            }
            result = builder.render(&m_colorHandle, 1);
            if (!result)
            {
                return result;
            }
            result = builder.get_view(m_colorRtvName, m_colorRtvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_texture(m_depthName, m_depthHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view(m_depthDsvName, m_depthDsvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_viewProjectionBufferHandle);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc vertexBufferDesc{};
            vertexBufferDesc.name = m_name + ".VertexBuffer";
            vertexBufferDesc.type = RHI::BufferType::Vertex;
            vertexBufferDesc.defaultHeapCount = 1;
            vertexBufferDesc.uploadHeapCount = builder.buffer_count();
            vertexBufferDesc.initialState = RHI::ResourceState::Common;
            vertexBufferDesc.stride = sizeof(DebugDrawVertex);
            vertexBufferDesc.elementCount = k_maxVertexCount;
            vertexBufferDesc.size = sizeof(DebugDrawVertex) * k_maxVertexCount;
            vertexBufferDesc.alignment = sizeof(DebugDrawVertex);
            result = builder.create_buffer(vertexBufferDesc, m_vertexBufferHandle);
            if (!result)
            {
                return result;
            }

            result = m_bufferManager.create_slot_uploaders<DebugDrawVertex>(
                m_vertexBufferHandle, builder.buffer_count(), m_uploaders);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = m_name + ".RootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::CBV,
                RHI::ShaderVisibility::Vertex,
                0 });
            result = builder.create_root_signature(
                rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = m_name + ".VS";
            vertexShaderDesc.filePath = "Shaders/D3D12/DebugDraw.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(
                vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = m_name + ".PS";
            pixelShaderDesc.filePath = "Shaders/D3D12/DebugDraw.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(
                pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = m_name + ".Pipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.inputElements = {
                { "POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float,
                    0, 0 },
                { "COLOR", 0, RHI::InputElementFormat::R32G32B32A32_Float,
                    0, sizeof(Math::float4) },
            };
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = true;
            pipelineDesc.depthStencilState.depthWriteMask =
                RHI::DepthWriteMask::Zero;
            pipelineDesc.depthStencilState.depthFunc =
                RHI::ComparisonFunc::LessEqual;
            pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
            pipelineDesc.primitiveTopologyType = RHI::PrimitiveTopologyType::Line;
            pipelineDesc.blendMode = { RHI::BlendMode::Normal };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            return builder.create_graphics_pipeline(
                pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_colorHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_texture(
                m_depthHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::DepthWrite,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_vertexBufferHandle,
                RHI::ResourceAccessType::ReadWrite,
                RHI::ResourceState::CopyDest,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_viewProjectionBufferHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::Common,
                RHI::ResourceState::Common);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            build_vertices();
            if (m_vertices.empty() ||
                context.frame_index() >= m_uploaders.size())
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            RHI::SlotUploader<DebugDrawVertex>& uploader =
                m_uploaders[context.frame_index()];
            uploader.begin_frame();
            for (uint32_t vertexIndex = 0;
                 vertexIndex < static_cast<uint32_t>(m_vertices.size());
                 ++vertexIndex)
            {
                if (!uploader.push(vertexIndex, m_vertices[vertexIndex]))
                {
                    return;
                }
            }
            if (!uploader.commit())
            {
                return;
            }

            RHI::BufferCopyRegion region{};
            region.srcBufferHandle = m_vertexBufferHandle;
            region.srcUploadResourceIndex = context.frame_index();
            region.srcByteOffset = 0;
            region.dstBufferHandle = m_vertexBufferHandle;
            region.dstDefaultResourceIndex = 0;
            region.dstByteOffset = 0;
            region.byteSize =
                static_cast<uint64_t>(m_vertices.size()) *
                sizeof(DebugDrawVertex);
            Result result = commandContext->copy_buffer_region(region);
            if (!result)
            {
                return;
            }

            RHI::ResourceBarrierDesc barrier{};
            barrier.before = RHI::ResourceState::CopyDest;
            barrier.after = RHI::ResourceState::Common;
            result = commandContext->resource_barrier(
                m_vertexBufferHandle, barrier);
            if (!result)
            {
                return;
            }

            commandContext->set_render_targets(
                &m_colorRtvHandle,
                1,
                m_depthDsvHandle);
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Line);
            commandContext->set_cbv(0, m_viewProjectionBufferHandle);
            commandContext->set_vertex_buffer(0, m_vertexBufferHandle);
            commandContext->draw_instanced(
                static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);
        }

    private:
        struct DebugDrawVertex final
        {
            Math::float4 position{};
            Math::float4 color{};
        };

        static constexpr uint32_t k_maxVertexCount = 65536;
        static constexpr uint32_t k_sphereSegmentCount = 24;

        void push_vertex(
            const Math::float3& a_position,
            const Math::float4& a_color)
        {
            if (m_vertices.size() >= k_maxVertexCount)
            {
                return;
            }

            m_vertices.push_back(DebugDrawVertex{
                Math::float4(a_position.x, a_position.y, a_position.z, 1.0f),
                a_color });
        }

        void push_line(
            const Math::float3& a_start,
            const Math::float3& a_end,
            const Math::float4& a_color)
        {
            if (m_vertices.size() + 2u > k_maxVertexCount)
            {
                return;
            }

            push_vertex(a_start, a_color);
            push_vertex(a_end, a_color);
        }

        void push_box(const GameCore::DebugDrawPrimitive& a_primitive)
        {
            const Math::float3 c = a_primitive.center;
            const Math::float3 e = a_primitive.halfExtent;
            const std::array<Math::float3, 8> corners = {
                Math::float3(c.x - e.x, c.y - e.y, c.z - e.z),
                Math::float3(c.x + e.x, c.y - e.y, c.z - e.z),
                Math::float3(c.x + e.x, c.y - e.y, c.z + e.z),
                Math::float3(c.x - e.x, c.y - e.y, c.z + e.z),
                Math::float3(c.x - e.x, c.y + e.y, c.z - e.z),
                Math::float3(c.x + e.x, c.y + e.y, c.z - e.z),
                Math::float3(c.x + e.x, c.y + e.y, c.z + e.z),
                Math::float3(c.x - e.x, c.y + e.y, c.z + e.z),
            };
            constexpr std::array<std::array<uint32_t, 2>, 12> k_edges = {
                std::array<uint32_t, 2>{ 0, 1 },
                std::array<uint32_t, 2>{ 1, 2 },
                std::array<uint32_t, 2>{ 2, 3 },
                std::array<uint32_t, 2>{ 3, 0 },
                std::array<uint32_t, 2>{ 4, 5 },
                std::array<uint32_t, 2>{ 5, 6 },
                std::array<uint32_t, 2>{ 6, 7 },
                std::array<uint32_t, 2>{ 7, 4 },
                std::array<uint32_t, 2>{ 0, 4 },
                std::array<uint32_t, 2>{ 1, 5 },
                std::array<uint32_t, 2>{ 2, 6 },
                std::array<uint32_t, 2>{ 3, 7 },
            };

            for (const std::array<uint32_t, 2>& edge : k_edges)
            {
                push_line(corners[edge[0]], corners[edge[1]],
                    a_primitive.color);
            }
        }

        void push_sphere(const GameCore::DebugDrawPrimitive& a_primitive)
        {
            constexpr float k_tau = Math::k_pi * 2.0f;
            for (uint32_t axis = 0; axis < 3u; ++axis)
            {
                for (uint32_t segment = 0;
                     segment < k_sphereSegmentCount;
                     ++segment)
                {
                    const float t0 =
                        k_tau * static_cast<float>(segment) /
                        static_cast<float>(k_sphereSegmentCount);
                    const float t1 =
                        k_tau * static_cast<float>(segment + 1u) /
                        static_cast<float>(k_sphereSegmentCount);
                    const float c0 = std::cos(t0) * a_primitive.radius;
                    const float s0 = std::sin(t0) * a_primitive.radius;
                    const float c1 = std::cos(t1) * a_primitive.radius;
                    const float s1 = std::sin(t1) * a_primitive.radius;

                    Math::float3 p0 = a_primitive.center;
                    Math::float3 p1 = a_primitive.center;
                    if (axis == 0u)
                    {
                        p0.x += c0;
                        p0.y += s0;
                        p1.x += c1;
                        p1.y += s1;
                    }
                    else if (axis == 1u)
                    {
                        p0.x += c0;
                        p0.z += s0;
                        p1.x += c1;
                        p1.z += s1;
                    }
                    else
                    {
                        p0.y += c0;
                        p0.z += s0;
                        p1.y += c1;
                        p1.z += s1;
                    }
                    push_line(p0, p1, a_primitive.color);
                }
            }
        }

        void push_nav_path_lines()
        {
            GameCore::NavMeshDebugGeometry geometry{};
            const Result result = m_world.build_navigation_debug_geometry(geometry);
            if (!result)
            {
                return;
            }

            const Math::float4 color(0.0f, 0.85f, 1.0f, 1.0f);
            for (const GameCore::NavDebugLine& line : geometry.pathLines)
            {
                push_line(line.start, line.end, color);
            }
        }

        void build_vertices()
        {
            m_vertices.clear();
            for (const GameCore::DebugDrawPrimitive& primitive :
                 m_world.debug_draw().primitives())
            {
                switch (primitive.type)
                {
                case GameCore::DebugDrawPrimitiveType::Line:
                    push_line(primitive.start, primitive.end, primitive.color);
                    break;
                case GameCore::DebugDrawPrimitiveType::Sphere:
                    push_sphere(primitive);
                    break;
                case GameCore::DebugDrawPrimitiveType::Box:
                    push_box(primitive);
                    break;
                default:
                    break;
                }
            }

            push_nav_path_lines();
        }

        std::string m_name{};
        std::string m_colorName{};
        std::string m_colorRtvName{};
        std::string m_depthName{};
        std::string m_depthDsvName{};
        GameCore::GameWorld& m_world;
        RHI::IBufferManager& m_bufferManager;
        RHI::BufferHandle m_viewProjectionBufferHandle{};
        RHI::BufferHandle m_vertexBufferHandle{};
        RHI::TextureHandle m_colorHandle{};
        RHI::TextureHandle m_depthHandle{};
        RHI::ViewHandle m_colorRtvHandle{};
        RHI::ViewHandle m_depthDsvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
        std::vector<RHI::SlotUploader<DebugDrawVertex>> m_uploaders{};
        std::vector<DebugDrawVertex> m_vertices{};
    };
} // namespace Cue
