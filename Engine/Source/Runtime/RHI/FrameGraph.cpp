#include "FrameGraph.h"

namespace Cue::RHI
{
    Result FrameGraphBuilder::create_buffer(const BufferDesc& desc, BufferHandle& out)
    {
        Result result = m_frameGraph.m_desc.bufferManager->create_buffer(desc, out);
        if (result)
        {
            m_frameGraph.m_createdBuffers.push_back(out);
        }
        return result;
    }

    Result FrameGraphBuilder::create_texture(const TextureDesc& desc, TextureHandle& out)
    {
        Result result = m_frameGraph.m_desc.textureManager->create_texture(desc, out);
        if (result)
        {
            m_frameGraph.m_createdTextures.push_back(out);
        }
        return result;
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
        Result result = m_frameGraph.m_desc.viewManager->create_view(desc, out);
        if (result)
        {
            m_frameGraph.m_createdViews.push_back(out);
        }
        return result;
    }

    Result FrameGraphBuilder::get_view(std::string_view name, ViewHandle& out)
    {
        return m_frameGraph.m_desc.viewManager->get_view(name, out);
    }

    Result FrameGraphBuilder::create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out)
    {
        Result result =
            m_frameGraph.m_desc.pipelineManager->create_root_signature(desc, out);
        if (result)
        {
            m_frameGraph.m_createdRootSignatures.push_back(out);
        }
        return result;
    }

    Result FrameGraphBuilder::get_root_signature(std::string_view name, RootSignatureHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->get_root_signature(name, out);
    }

    Result FrameGraphBuilder::create_shader_blob(const ShaderCompileDesc& desc, ShaderBlobHandle& out)
    {
        Result result =
            m_frameGraph.m_desc.pipelineManager->create_shader_blob(desc, out);
        if (result)
        {
            m_frameGraph.m_createdShaderBlobs.push_back(out);
        }
        return result;
    }

    Result FrameGraphBuilder::get_shader_blob(std::string_view name, ShaderBlobHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->get_shader_blob(name, out);
    }

    Result FrameGraphBuilder::create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& out)
    {
        Result result =
            m_frameGraph.m_desc.pipelineManager->create_graphics_pipeline(desc, out);
        if (result)
        {
            m_frameGraph.m_createdGraphicsPipelines.push_back(out);
        }
        return result;
    }

    Result FrameGraphBuilder::get_graphics_pipeline(std::string_view name, PipelineStateHandle& out)
    {
        return m_frameGraph.m_desc.pipelineManager->get_graphics_pipeline(name, out);
    }

    Result FrameGraphBuilder::create_compute_pipeline(const ComputePipelineStateDesc& desc, PipelineStateHandle& out)
    {
        Result result =
            m_frameGraph.m_desc.pipelineManager->create_compute_pipeline(desc, out);
        if (result)
        {
            m_frameGraph.m_createdComputePipelines.push_back(out);
        }
        return result;
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
    FrameGraph::~FrameGraph()
    {
        (void)cleanup_build_resources();
    }

    Result FrameGraph::cleanup_build_resources()
    {
        if (m_desc.pipelineManager != nullptr)
        {
            for (auto it = m_createdGraphicsPipelines.rbegin();
                it != m_createdGraphicsPipelines.rend(); ++it)
            {
                Result result =
                    m_desc.pipelineManager->destroy_graphics_pipeline(*it);
                if (!result)
                {
                    return result;
                }
            }

            for (auto it = m_createdComputePipelines.rbegin();
                it != m_createdComputePipelines.rend(); ++it)
            {
                Result result =
                    m_desc.pipelineManager->destroy_compute_pipeline(*it);
                if (!result)
                {
                    return result;
                }
            }

            for (auto it = m_createdShaderBlobs.rbegin();
                it != m_createdShaderBlobs.rend(); ++it)
            {
                Result result = m_desc.pipelineManager->destroy_shader_blob(*it);
                if (!result)
                {
                    return result;
                }
            }

            for (auto it = m_createdRootSignatures.rbegin();
                it != m_createdRootSignatures.rend(); ++it)
            {
                Result result =
                    m_desc.pipelineManager->destroy_root_signature(*it);
                if (!result)
                {
                    return result;
                }
            }
        }

        if (m_desc.viewManager != nullptr)
        {
            for (auto it = m_createdViews.rbegin(); it != m_createdViews.rend(); ++it)
            {
                Result result = m_desc.viewManager->destroy_view(*it);
                if (!result)
                {
                    return result;
                }
            }
        }

        if (m_desc.textureManager != nullptr)
        {
            for (auto it = m_createdTextures.rbegin();
                it != m_createdTextures.rend(); ++it)
            {
                Result result = m_desc.textureManager->destroy_texture(*it);
                if (!result)
                {
                    return result;
                }
            }
        }

        if (m_desc.bufferManager != nullptr)
        {
            for (auto it = m_createdBuffers.rbegin();
                it != m_createdBuffers.rend(); ++it)
            {
                Result result = m_desc.bufferManager->destroy_buffer(*it);
                if (!result)
                {
                    return result;
                }
            }
        }

        m_createdGraphicsPipelines.clear();
        m_createdComputePipelines.clear();
        m_createdShaderBlobs.clear();
        m_createdRootSignatures.clear();
        m_createdViews.clear();
        m_createdTextures.clear();
        m_createdBuffers.clear();

        return Result::ok();
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
    Result FrameGraph::rebuild(uint32_t a_width, uint32_t a_height)
    {
        Result result = cleanup_build_resources();
        if (!result)
        {
            return result;
        }

        m_desc.width = a_width;
        m_desc.height = a_height;
        return build();
    }

    Result FrameGraph::execute(uint32_t frameIndex)
    {
        for (auto& compiledPass : m_passes)
        {
            // コマンドコンテキストをプールから取得
            commandContextLease commandContext;
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

            queueContextLease queueContext;
            IQueueContext* queueContextPtr = nullptr;
            const bool usePresentQueue = m_desc.usePresentQueue && compiledPass.pass->type() == CommandListType::Graphics;
            if (usePresentQueue)
            {
                queueContextPtr = m_desc.queuePool->get_present_queue_context();
                if (queueContextPtr == nullptr)
                {
                    m_desc.commandPool->return_command_context(commandContext);
                    return Result::fail(
                        Code::GetFailed,
                        Severity::Error,
                        "Failed to get present queue context for frame graph pass.");
                }
            }
            else
            {
                result = m_desc.queuePool->get_queue_context(compiledPass.pass->type(), queueContext);
                if (result)
                {
                    queueContextPtr = queueContext.get();
                }
            }

            if (!result || queueContextPtr == nullptr)
            {
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result ? Code::GetFailed : result.code,
                    Severity::Error,
                    "Failed to get queue context for frame graph pass.");
            }
            std::vector<ICommandContext*> commandContexts { commandContext.get() };
            result = queueContextPtr->submit(commandContexts);
            if (!result)
            {
                if (!usePresentQueue)
                {
                    m_desc.queuePool->return_queue_context(queueContext);
                }
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to submit frame graph pass.");
            }
            result = queueContextPtr->signal();
            if (!result)
            {
                if (!usePresentQueue)
                {
                    m_desc.queuePool->return_queue_context(queueContext);
                }
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to signal queue after frame graph pass submission.");
            }
            result = queueContextPtr->wait();
            if (!result)
            {
                if (!usePresentQueue)
                {
                    m_desc.queuePool->return_queue_context(queueContext);
                }
                m_desc.commandPool->return_command_context(commandContext);
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to wait for frame graph pass completion.");
            }
            if (!usePresentQueue)
            {
                m_desc.queuePool->return_queue_context(queueContext);
            }
            m_desc.commandPool->return_command_context(commandContext);
        }
        return Result::ok();
    }
}
