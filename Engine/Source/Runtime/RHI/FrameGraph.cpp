#include "FrameGraph.h"

namespace Cue::RHI
{
    Result FrameGraphBuilder::create_buffer(const BufferDesc& desc, BufferHandle& out)
    {
        return m_frameGraph.m_desc.bufferManager->create_buffer(desc, out);
    }

    Result FrameGraphBuilder::create_texture(const TextureDesc& desc, TextureHandle& out)
    {
        return m_frameGraph.m_desc.textureManager->create_texture(desc, out);
    }

    Result FrameGraphBuilder::get_buffer(std::string_view name, BufferHandle& out)
    {
        return m_frameGraph.m_desc.bufferManager->get_buffer(name, out);
    }

    Result FrameGraphBuilder::get_texture(std::string_view name, TextureHandle& out)
    {
        return m_frameGraph.m_desc.textureManager->get_texture(name, out);
    }

    Result FrameGraphBuilder::create_view(const ViewDesc& desc, ViewHandle& out)
    {
        return m_frameGraph.m_desc.viewManager->create_view(desc, out);
    }

    Result FrameGraphBuilder::get_view(std::string_view name, ViewHandle& out)
    {
        return m_frameGraph.m_desc.viewManager->get_view(name, out);
    }

    Result FrameGraphBuilder::create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->create_root_signature(desc, out);
    }

    Result FrameGraphBuilder::get_root_signature(std::string_view name, RootSignatureHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->get_root_signature(name, out);
    }

    Result FrameGraphBuilder::create_shader_blob(const ShaderCompileDesc& desc, ShaderBlobHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->create_shader_blob(desc, out);
    }

    Result FrameGraphBuilder::get_shader_blob(std::string_view name, ShaderBlobHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->get_shader_blob(name, out);
    }

    Result FrameGraphBuilder::create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->create_graphics_pipeline(desc, out);
    }

    Result FrameGraphBuilder::get_graphics_pipeline(std::string_view name, PipelineStateHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->get_graphics_pipeline(name, out);
    }

    Result FrameGraphBuilder::create_compute_pipeline(const ComputePipelineStateDesc& desc, PipelineStateHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->create_compute_pipeline(desc, out);
    }

    Result FrameGraphBuilder::get_compute_pipeline(std::string_view name, PipelineStateHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->get_compute_pipeline(name, out);
    }

    Result FrameGraphBuilder::render(const TextureHandle* handles, size_t count)
    {
        m_renderTargets.clear();
        for (size_t i = 0; i < count; ++i)
        {
            if (!handles[i].valid())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Invalid texture handle provided.");
            }
            m_renderTargets.push_back(handles[i]);
        }
        return Result::ok();
    }

    uint32_t FrameGraphBuilder::width() const noexcept
    {
        return m_frameGraph.m_desc.width;
    }

    uint32_t FrameGraphBuilder::height() const noexcept
    {
        return m_frameGraph.m_desc.height;
    }

    const uint32_t& FrameGraphBuilder::buffer_count() const noexcept
    {
        return m_frameGraph.m_bufferCount;
    }

    FrameGraph::FrameGraph(const FrameGraphDesc& desc, const uint32_t& bufferCount)
        : m_desc(desc), m_bufferCount(bufferCount)
    {

    }

    Result FrameGraph::build()
    {
        for (auto& compiledPass : m_passes)
        {
            FrameGraphBuilder builder(*this);
            Result result = compiledPass.pass->setup(builder);
            if (!result)
            {
                return result;
            }
        }
        return Result::ok();
    }

    Result FrameGraph::execute(uint32_t frameIndex)
    {
        for (auto& compiledPass : m_passes)
        {
            // コマンドコンテキストをプールから取得
            CommandContextLease commandContext;
            Result result = m_desc.commandPool->get_command_context(compiledPass.pass->type(), commandContext);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to get command context for frame graph pass.");
            }

            // コマンドコンテキストをセットアップ
            result = commandContext->reset();
            if (!result)
            {
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to reset command context for frame graph pass.");
            }

            result = commandContext->setup(frameIndex, m_bufferCount);
            if (!result)
            {
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to setup command context for frame graph pass.");
            }

            // パスの実行
            FrameGraphContext context{ FrameGraphContextDesc{ m_desc.width, m_desc.height, frameIndex, commandContext.get()}};
            commandContext->begin_event(compiledPass.pass->name());
            compiledPass.pass->execute(context);
            commandContext->end_event();

            // コマンドコンテキストをクローズして GPU に送信
            result = commandContext->close();
            if (!result)
            {
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to close command context for frame graph pass.");
            }

            QueueContextLease queueContext;
            result = m_desc.queuePool->get_queue_context(compiledPass.pass->type(), queueContext);
            if (!result)
            {
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to get queue context for frame graph pass.");
            }
            std::vector<ICommandContext*> commandContexts { commandContext.get() };
            result = queueContext->submit(commandContexts);
            if (!result)
            {
                m_desc.queuePool->return_queue_context(queueContext);
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to submit frame graph pass.");
            }
            result = queueContext->signal();
            if (!result)
            {
                m_desc.queuePool->return_queue_context(queueContext);
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to signal queue after frame graph pass submission.");
            }
            result = queueContext->wait();
            if (!result)
            {
                m_desc.queuePool->return_queue_context(queueContext);
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to wait for frame graph pass completion.");
            }
            m_desc.queuePool->return_queue_context(queueContext);
            m_desc.commandPool->return_command_context(commandContext);
        }
        return Result::ok();
    }
}
