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

    FrameGraph::FrameGraph(const FrameGraphDesc& desc)
        : m_desc(desc)
    {

    }

    Result FrameGraph::build()
    {
        for (auto& compiledPass : m_passes)
        {
            FrameGraphBuilder builder(*this);
            compiledPass.pass->setup(builder);
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

            // コマンドコンテキストをセットアップ
            commandContext->reset();
            commandContext->setup(frameIndex);

            // パスの実行
            FrameGraphContext context{ FrameGraphContextDesc{ m_desc.width, m_desc.height, frameIndex, commandContext.get()}};
            compiledPass.pass->execute(context);

            // コマンドコンテキストをクローズして GPU に送信
            commandContext->close();

            QueueContextLease queueContext;
            result = m_desc.queuePool->get_queue_context(compiledPass.pass->type(), queueContext);
            std::vector<ICommandContext*> commandContexts { commandContext.get() };
            queueContext->submit(commandContexts);
            queueContext->signal();
            queueContext->wait();
            m_desc.queuePool->return_queue_context(queueContext);
            m_desc.commandPool->return_command_context(commandContext);
        }
        return Result::ok();
    }
}
