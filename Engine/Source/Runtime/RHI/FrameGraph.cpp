#include "FrameGraph.h"

namespace Cue::RHI
{
    namespace
    {
        bool is_texture_view_type(ViewType type)
        {
            switch (type)
            {
            case ViewType::ShaderResourceTexture2D:
            case ViewType::UnorderedAccessTexture2D:
            case ViewType::RenderTarget:
            case ViewType::DepthStencil:
                return true;
            default:
                return false;
            }
        }
    }

    Result FrameGraphPass::execute(FrameGraphPassContext& context)
    {
        context;
        return Result::fail(
            Code::Unsupported,
            Severity::Error,
            "This FrameGraphPass does not implement execution.");
    }

    FrameGraph::~FrameGraph()
    {
        (void)cleanup_built_state();
    }

    FrameGraphResourceRef FrameGraphBuilder::import_buffer(std::string_view name, BufferHandle handle)
    {
        return m_frameGraph.import_buffer_internal(name, handle);
    }

    FrameGraphResourceRef FrameGraphBuilder::import_texture(std::string_view name, TextureHandle handle, bool presentable)
    {
        return m_frameGraph.import_texture_internal(name, handle, presentable);
    }

    FrameGraphResourceRef FrameGraphBuilder::create_transient_buffer(std::string_view name, const BufferDesc& desc)
    {
        return m_frameGraph.create_transient_buffer_internal(name, desc);
    }

    FrameGraphResourceRef FrameGraphBuilder::create_transient_texture(std::string_view name, const TextureDesc& desc)
    {
        return m_frameGraph.create_transient_texture_internal(name, desc);
    }

    FrameGraphResourceRef FrameGraphBuilder::backbuffer_texture() const noexcept
    {
        return m_frameGraph.backbuffer_texture();
    }

    FrameGraphViewRef FrameGraphBuilder::create_buffer_view(FrameGraphResourceRef resource, const ViewDesc& desc)
    {
        return m_frameGraph.create_view_internal(m_passIndex, resource, desc, FrameGraph::LogicalResourceKind::Buffer);
    }

    FrameGraphViewRef FrameGraphBuilder::create_texture_view(FrameGraphResourceRef resource, const ViewDesc& desc)
    {
        return m_frameGraph.create_view_internal(m_passIndex, resource, desc, FrameGraph::LogicalResourceKind::Texture);
    }

    void FrameGraphBuilder::read(FrameGraphViewRef view)
    {
        m_frameGraph.register_access(m_passIndex, view, false);
    }

    void FrameGraphBuilder::write(FrameGraphViewRef view)
    {
        m_frameGraph.register_access(m_passIndex, view, true);
    }

    void FrameGraphBuilder::set_render_target(
        FrameGraphViewRef view,
        LoadOp loadOp,
        StoreOp storeOp,
        const std::array<float, 4>& clearColor)
    {
        m_frameGraph.register_render_target(m_passIndex, view, loadOp, storeOp, clearColor);
    }

    void FrameGraphBuilder::set_depth_stencil(
        FrameGraphViewRef view,
        LoadOp loadOp,
        StoreOp storeOp,
        float clearDepth,
        uint8_t clearStencil)
    {
        m_frameGraph.register_depth_stencil(m_passIndex, view, loadOp, storeOp, clearDepth, clearStencil);
    }

    void FrameGraphBuilder::set_side_effect(bool enabled)
    {
        m_frameGraph.m_passes[m_passIndex].sideEffect = enabled;
    }

    void FrameGraphBuilder::set_graphics_pipeline(PipelineStateHandle handle)
    {
        auto& pipeline = m_frameGraph.m_passes[m_passIndex].pipeline;
        pipeline.kind = FrameGraph::PipelineRequest::Kind::GraphicsExternal;
        pipeline.handle = handle;
    }

    void FrameGraphBuilder::set_graphics_pipeline(const GraphicsPipelineStateDesc& desc)
    {
        auto& pipeline = m_frameGraph.m_passes[m_passIndex].pipeline;
        pipeline.kind = FrameGraph::PipelineRequest::Kind::GraphicsOwned;
        pipeline.graphicsDesc = desc;
    }

    void FrameGraphBuilder::set_compute_pipeline(PipelineStateHandle handle)
    {
        auto& pipeline = m_frameGraph.m_passes[m_passIndex].pipeline;
        pipeline.kind = FrameGraph::PipelineRequest::Kind::ComputeExternal;
        pipeline.handle = handle;
    }

    void FrameGraphBuilder::set_compute_pipeline(const ComputePipelineStateDesc& desc)
    {
        auto& pipeline = m_frameGraph.m_passes[m_passIndex].pipeline;
        pipeline.kind = FrameGraph::PipelineRequest::Kind::ComputeOwned;
        pipeline.computeDesc = desc;
    }

    ViewHandle FrameGraphPassContext::resolve_view(FrameGraphViewRef view) const
    {
        if (!view.valid() || view.index >= m_frameGraph.m_views.size())
        {
            return {};
        }

        return m_frameGraph.m_views[view.index].handle;
    }

    PipelineStateHandle FrameGraphPassContext::pipeline() const
    {
        if (m_passIndex >= m_frameGraph.m_passes.size())
        {
            return {};
        }

        return m_frameGraph.m_passes[m_passIndex].pipeline.handle;
    }

    IStaticMeshPool* FrameGraphPassContext::static_mesh_pool() const noexcept
    {
        return m_frameGraph.m_staticMeshPool;
    }

    CommandListType FrameGraphPassContext::type() const noexcept
    {
        if (m_passIndex >= m_frameGraph.m_passes.size())
        {
            return CommandListType::Graphics;
        }

        return m_frameGraph.m_passes[m_passIndex].type;
    }

    std::vector<ViewHandle> FrameGraphPassContext::render_target_views() const
    {
        std::vector<ViewHandle> outViews{};
        if (m_passIndex >= m_frameGraph.m_passes.size())
        {
            return outViews;
        }

        const FrameGraph::PassRecord& pass = m_frameGraph.m_passes[m_passIndex];
        outViews.reserve(pass.renderTargets.size());
        for (const FrameGraph::RenderTargetBinding& binding : pass.renderTargets)
        {
            outViews.emplace_back(m_frameGraph.m_views[binding.viewIndex].handle);
        }
        return outViews;
    }

    ViewHandle FrameGraphPassContext::depth_stencil_view() const
    {
        if (m_passIndex >= m_frameGraph.m_passes.size())
        {
            return {};
        }

        const FrameGraph::PassRecord& pass = m_frameGraph.m_passes[m_passIndex];
        if (!pass.depthStencil.has_value())
        {
            return {};
        }

        return m_frameGraph.m_views[pass.depthStencil->viewIndex].handle;
    }

    Result FrameGraphPassContext::clear_render_target(FrameGraphViewRef view, const std::array<float, 4>& clearColor) const
    {
        if (!view.valid() || view.index >= m_frameGraph.m_views.size())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Invalid FrameGraphViewRef was passed to clear_render_target.");
        }

        const ViewHandle resolvedView = m_frameGraph.m_views[view.index].handle;
        if (!resolvedView.valid())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "FrameGraph view was not materialized before clear_render_target.");
        }

        return m_frameGraph.m_viewManager->clear_render_target(
            *m_commandContext,
            resolvedView,
            m_frameIndex,
            clearColor);
    }

    Result FrameGraph::add_pass(std::unique_ptr<FrameGraphPass> pass)
    {
        return add_pass_common(std::move(pass));
    }

    FrameGraphResourceRef FrameGraph::import_buffer(std::string_view name, BufferHandle handle)
    {
        return import_buffer_internal(name, handle);
    }

    FrameGraphResourceRef FrameGraph::import_texture(std::string_view name, TextureHandle handle, bool presentable)
    {
        return import_texture_internal(name, handle, presentable);
    }

    FrameGraphResourceRef FrameGraph::create_transient_buffer(std::string_view name, const BufferDesc& desc)
    {
        return create_transient_buffer_internal(name, desc);
    }

    FrameGraphResourceRef FrameGraph::create_transient_texture(std::string_view name, const TextureDesc& desc)
    {
        return create_transient_texture_internal(name, desc);
    }

    Result FrameGraph::build()
    {
        // 以前 build した実体を片付けて、宣言から再構築できる状態へ戻します。
        Result result = cleanup_built_state();
        if (!result)
        {
            return result;
        }

        // 論理 resource / view / pipeline を backend の handle へ順に落とし込みます。
        result = materialize_resources();
        if (!result)
        {
            return result;
        }
        result = materialize_views();
        if (!result)
        {
            return result;
        }
        result = materialize_pipelines();
        if (!result)
        {
            return result;
        }

        // access 競合から依存を作り、登録順を保ちながら実行順を確定します。
        result = build_dependencies();
        if (!result)
        {
            return result;
        }
        result = build_execution_order();
        if (!result)
        {
            return result;
        }

        m_isBuilt = true;
        return Result::ok();
    }

    Result FrameGraph::execute(uint32_t frameIndex)
    {
        // build 前の graph は実体化済み handle を持たないので、そのままでは実行できません。
        if (!m_isBuilt)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Error,
                "FrameGraph must be built before execute.");
        }
        if (!m_commandPool || !m_queuePool)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "FrameGraph execute requires a valid command pool and queue pool.");
        }

        // 実行に必要な queue を pass 種別ごとに先に確保しておきます。
        std::array<QueueContextLease, 3> queues{};
        std::array<bool, 3> queueAcquired = { false, false, false };
        for (uint32_t passIndex : m_executionOrder)
        {
            const size_t queueIndex = command_list_type_index(m_passes[passIndex].type);
            if (!queueAcquired[queueIndex])
            {
                Result result = m_queuePool->get_queue_context(m_passes[passIndex].type, queues[queueIndex]);
                if (!result)
                {
                    return result;
                }
                queueAcquired[queueIndex] = true;
            }
        }

        std::vector<ICommandContext*> submitContexts{};
        std::vector<CommandContextLease> inFlightContexts{};
        for (uint32_t passIndex : m_executionOrder)
        {
            const PassRecord& pass = m_passes[passIndex];
            const size_t queueIndex = command_list_type_index(pass.type);
            IQueueContext* queue = queues[queueIndex].get();
            if (!queue)
            {
                return Result::fail(
                    Code::InternalError,
                    Severity::Error,
                    "Queue context was not acquired for the pass type.");
            }

            // 異なる queue にまたがる依存だけを待ち、同一 queue 内の順序は submit 順へ任せます。
            for (uint32_t dependencyIndex : pass.dependencies)
            {
                const CommandListType dependencyType = m_passes[dependencyIndex].type;
                if (dependencyType != pass.type)
                {
                    IQueueContext* dependencyQueue = queues[command_list_type_index(dependencyType)].get();
                    if (dependencyQueue)
                    {
                        Result waitResult = queue->wait_for_queue(*dependencyQueue);
                        if (!waitResult)
                        {
                            return waitResult;
                        }
                    }
                }
            }

            // pass ごとに command context を取得し、event 範囲の中で execute を呼びます。
            CommandContextLease commandContext{};
            Result result = m_commandPool->get_command_context(pass.type, commandContext);
            if (!result)
            {
                for (CommandContextLease& inFlightContext : inFlightContexts)
                {
                    (void)m_commandPool->return_command_context(inFlightContext);
                }
                return result;
            }

            result = commandContext->reset();
            if (!result)
            {
                (void)m_commandPool->return_command_context(commandContext);
                for (CommandContextLease& inFlightContext : inFlightContexts)
                {
                    (void)m_commandPool->return_command_context(inFlightContext);
                }
                return result;
            }

            commandContext->begin_event(pass.name.c_str());
            if (!pass.pass)
            {
                (void)m_commandPool->return_command_context(commandContext);
                return Result::fail(
                    Code::InternalError,
                    Severity::Error,
                    "FrameGraph pass instance was not initialized.");
            }

            FrameGraphPassContext context(*this, passIndex, frameIndex, *commandContext);
            result = pass.pass->execute(context);
            commandContext->end_event();

            if (!result)
            {
                (void)m_commandPool->return_command_context(commandContext);
                for (CommandContextLease& inFlightContext : inFlightContexts)
                {
                    (void)m_commandPool->return_command_context(inFlightContext);
                }
                return result;
            }

            result = commandContext->close();
            if (!result)
            {
                (void)m_commandPool->return_command_context(commandContext);
                for (CommandContextLease& inFlightContext : inFlightContexts)
                {
                    (void)m_commandPool->return_command_context(inFlightContext);
                }
                return result;
            }

            submitContexts.clear();
            submitContexts.emplace_back(commandContext.get());
            result = queue->submit(submitContexts);
            if (!result)
            {
                (void)m_commandPool->return_command_context(commandContext);
                for (CommandContextLease& inFlightContext : inFlightContexts)
                {
                    (void)m_commandPool->return_command_context(inFlightContext);
                }
                return result;
            }

            result = queue->signal();
            if (!result)
            {
                (void)m_commandPool->return_command_context(commandContext);
                for (CommandContextLease& inFlightContext : inFlightContexts)
                {
                    (void)m_commandPool->return_command_context(inFlightContext);
                }
                return result;
            }
            inFlightContexts.emplace_back(std::move(commandContext));
        }

        // submit 済み queue を最後に待ち合わせてから lease を返却し、再利用と GPU 実行を競合させません。
        for (size_t i = 0; i < queues.size(); ++i)
        {
            if (!queueAcquired[i] || !queues[i])
            {
                continue;
            }

            Result waitResult = queues[i]->wait();
            if (!waitResult)
            {
                return waitResult;
            }
            Result returnResult = m_queuePool->return_queue_context(queues[i]);
            if (!returnResult)
            {
                return returnResult;
            }
        }

        for (CommandContextLease& inFlightContext : inFlightContexts)
        {
            Result returnResult = m_commandPool->return_command_context(inFlightContext);
            if (!returnResult)
            {
                return returnResult;
            }
        }

        return Result::ok();
    }

    FrameGraphResourceRef FrameGraph::import_buffer_internal(std::string_view name, BufferHandle handle)
    {
        LogicalResource resource{};
        resource.name = std::string(name);
        resource.kind = LogicalResourceKind::Buffer;
        resource.imported = true;
        resource.bufferHandle = handle;

        m_resources.emplace_back(std::move(resource));
        return FrameGraphResourceRef{ static_cast<uint32_t>(m_resources.size() - 1) };
    }

    FrameGraphResourceRef FrameGraph::import_texture_internal(std::string_view name, TextureHandle handle, bool presentable)
    {
        LogicalResource resource{};
        resource.name = std::string(name);
        resource.kind = LogicalResourceKind::Texture;
        resource.imported = true;
        resource.presentable = presentable;
        resource.textureHandle = handle;

        m_resources.emplace_back(std::move(resource));
        return FrameGraphResourceRef{ static_cast<uint32_t>(m_resources.size() - 1) };
    }

    FrameGraphResourceRef FrameGraph::create_transient_buffer_internal(std::string_view name, const BufferDesc& desc)
    {
        LogicalResource resource{};
        resource.name = std::string(name);
        resource.kind = LogicalResourceKind::Buffer;
        resource.imported = false;
        resource.bufferDesc = desc;

        m_resources.emplace_back(std::move(resource));
        return FrameGraphResourceRef{ static_cast<uint32_t>(m_resources.size() - 1) };
    }

    FrameGraphResourceRef FrameGraph::create_transient_texture_internal(std::string_view name, const TextureDesc& desc)
    {
        LogicalResource resource{};
        resource.name = std::string(name);
        resource.kind = LogicalResourceKind::Texture;
        resource.imported = false;
        resource.textureDesc = desc;

        m_resources.emplace_back(std::move(resource));
        return FrameGraphResourceRef{ static_cast<uint32_t>(m_resources.size() - 1) };
    }

    FrameGraphViewRef FrameGraph::create_view_internal(
        uint32_t passIndex,
        FrameGraphResourceRef resource,
        const ViewDesc& desc,
        LogicalResourceKind expectedKind)
    {
        if (!resource.valid() || resource.index >= m_resources.size() || passIndex >= m_passes.size())
        {
            return {};
        }

        const LogicalResource& logicalResource = m_resources[resource.index];
        if (logicalResource.kind != expectedKind)
        {
            return {};
        }
        if (expectedKind == LogicalResourceKind::Texture && !is_texture_view_type(desc.type))
        {
            return {};
        }
        if (expectedKind == LogicalResourceKind::Buffer && is_texture_view_type(desc.type))
        {
            return {};
        }

        LogicalView view{};
        view.resourceIndex = resource.index;
        view.desc = desc;

        m_views.emplace_back(std::move(view));
        return FrameGraphViewRef{ static_cast<uint32_t>(m_views.size() - 1) };
    }

    void FrameGraph::register_access(uint32_t passIndex, FrameGraphViewRef view, bool isWrite)
    {
        if (!view.valid() || view.index >= m_views.size() || passIndex >= m_passes.size())
        {
            return;
        }

        AccessRecord access{};
        access.viewIndex = view.index;
        access.isWrite = isWrite;
        access.requiredState = resolve_required_state(m_passes[passIndex], m_views[view.index], isWrite);
        m_passes[passIndex].accesses.emplace_back(access);
    }

    void FrameGraph::register_render_target(
        uint32_t passIndex,
        FrameGraphViewRef view,
        LoadOp loadOp,
        StoreOp storeOp,
        const std::array<float, 4>& clearColor)
    {
        if (!view.valid() || view.index >= m_views.size() || passIndex >= m_passes.size())
        {
            return;
        }

        RenderTargetBinding binding{};
        binding.viewIndex = view.index;
        binding.loadOp = loadOp;
        binding.storeOp = storeOp;
        binding.clearColor = clearColor;
        m_passes[passIndex].renderTargets.emplace_back(binding);
        register_access(passIndex, view, true);
    }

    void FrameGraph::register_depth_stencil(
        uint32_t passIndex,
        FrameGraphViewRef view,
        LoadOp loadOp,
        StoreOp storeOp,
        float clearDepth,
        uint8_t clearStencil)
    {
        if (!view.valid() || view.index >= m_views.size() || passIndex >= m_passes.size())
        {
            return;
        }

        DepthStencilBinding binding{};
        binding.viewIndex = view.index;
        binding.loadOp = loadOp;
        binding.storeOp = storeOp;
        binding.clearDepth = clearDepth;
        binding.clearStencil = clearStencil;
        m_passes[passIndex].depthStencil = binding;
        register_access(passIndex, view, true);
    }

    Result FrameGraph::add_pass_common(std::unique_ptr<FrameGraphPass> pass)
    {
        if (!pass)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "FrameGraph pass must not be null.");
        }

        PassRecord record{};
        record.name = std::string(pass->name());
        record.type = pass->type();
        record.sideEffect = pass->side_effect();
        record.registrationIndex = static_cast<uint32_t>(m_passes.size());
        record.pass = std::move(pass);
        m_passes.emplace_back(std::move(record));

        FrameGraphBuilder builder(*this, static_cast<uint32_t>(m_passes.size() - 1));
        Result result = m_passes.back().pass->setup(builder);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result FrameGraph::cleanup_built_state()
    {
        // build で作った view handle を先に消して、resource 破棄時に dangling descriptor を残しません。
        for (LogicalView& view : m_views)
        {
            if (view.handle.valid())
            {
                Result result = m_viewManager->destroy_view(view.handle);
                if (!result)
                {
                    return result;
                }
                view.handle = {};
            }
        }

        for (PassRecord& pass : m_passes)
        {
            PipelineRequest& pipeline = pass.pipeline;
            if (!pipeline.handle.valid())
            {
                continue;
            }

            Result result = Result::ok();
            if (pipeline.kind == PipelineRequest::Kind::GraphicsOwned)
            {
                result = m_pipelineManager->destroy_graphics_pipeline(pipeline.handle);
            }
            else if (pipeline.kind == PipelineRequest::Kind::ComputeOwned)
            {
                result = m_pipelineManager->destroy_compute_pipeline(pipeline.handle);
            }
            if (!result)
            {
                return result;
            }

            if (pipeline.kind == PipelineRequest::Kind::GraphicsOwned || pipeline.kind == PipelineRequest::Kind::ComputeOwned)
            {
                pipeline.handle = {};
            }
        }

        // transient resource だけをここで破棄し、imported resource の寿命は外側へ残します。
        for (LogicalResource& resource : m_resources)
        {
            if (resource.imported)
            {
                continue;
            }

            if (resource.kind == LogicalResourceKind::Buffer && resource.bufferHandle.valid())
            {
                Result result = m_bufferManager->destroy_buffer(resource.bufferHandle);
                if (!result)
                {
                    return result;
                }
                resource.bufferHandle = {};
            }
            else if (resource.kind == LogicalResourceKind::Texture && resource.textureHandle.valid())
            {
                Result result = m_textureManager->destroy_texture(resource.textureHandle);
                if (!result)
                {
                    return result;
                }
                resource.textureHandle = {};
            }
        }

        m_executionOrder.clear();
        m_isBuilt = false;
        return Result::ok();
    }

    Result FrameGraph::materialize_resources()
    {
        // imported resource は外部所有なので飛ばし、transient だけを manager 経由で実体化します。
        for (LogicalResource& resource : m_resources)
        {
            if (resource.imported)
            {
                continue;
            }

            if (resource.kind == LogicalResourceKind::Buffer)
            {
                if (resource.bufferHandle.valid())
                {
                    continue;
                }

                BufferDesc desc = resource.bufferDesc;
                desc.name = resource.name;
                Result result = m_bufferManager->create_buffer(desc, resource.bufferHandle);
                if (!result)
                {
                    return result;
                }
            }
            else
            {
                if (resource.textureHandle.valid())
                {
                    continue;
                }

                TextureDesc desc = resource.textureDesc;
                desc.name = resource.name;
                Result result = m_textureManager->create_texture(desc, resource.textureHandle);
                if (!result)
                {
                    return result;
                }
            }
        }

        return Result::ok();
    }

    Result FrameGraph::materialize_views()
    {
        // view 記述へ解決済み handle を埋め込み、pass 実行時は ViewHandle だけ取れればよい形にします。
        for (LogicalView& view : m_views)
        {
            LogicalResource& resource = m_resources[view.resourceIndex];
            ViewDesc desc = view.desc;
            if (desc.name.empty())
            {
                desc.name = resource.name;
            }
            desc.resourceIndex = 0;
            if (resource.kind == LogicalResourceKind::Buffer)
            {
                desc.bufferHandle = resource.bufferHandle;
            }
            else
            {
                desc.textureHandle = resource.textureHandle;
            }

            Result result = m_viewManager->create_view(desc, view.handle);
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result FrameGraph::materialize_pipelines()
    {
        // pass 種別と pipeline 要求の整合を確認してから、owned pipeline だけを生成します。
        for (PassRecord& pass : m_passes)
        {
            Result result = validate_pass(pass);
            if (!result)
            {
                return result;
            }

            PipelineRequest& pipeline = pass.pipeline;
            if (pipeline.kind == PipelineRequest::Kind::GraphicsOwned)
            {
                result = m_pipelineManager->create_graphics_pipeline(pipeline.graphicsDesc, pipeline.handle);
            }
            else if (pipeline.kind == PipelineRequest::Kind::ComputeOwned)
            {
                result = m_pipelineManager->create_compute_pipeline(pipeline.computeDesc, pipeline.handle);
            }

            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result FrameGraph::build_dependencies()
    {
        // 依存は同じ resource 上で view 範囲が重なり、どちらかが write するときだけ張ります。
        for (PassRecord& pass : m_passes)
        {
            pass.dependencies.clear();
        }

        for (size_t lhsIndex = 0; lhsIndex < m_passes.size(); ++lhsIndex)
        {
            const PassRecord& lhsPass = m_passes[lhsIndex];
            for (size_t rhsIndex = lhsIndex + 1; rhsIndex < m_passes.size(); ++rhsIndex)
            {
                const PassRecord& rhsPass = m_passes[rhsIndex];
                bool depends = false;

                for (const AccessRecord& lhsAccess : lhsPass.accesses)
                {
                    for (const AccessRecord& rhsAccess : rhsPass.accesses)
                    {
                        const LogicalView& lhsView = m_views[lhsAccess.viewIndex];
                        const LogicalView& rhsView = m_views[rhsAccess.viewIndex];
                        if (lhsView.resourceIndex != rhsView.resourceIndex)
                        {
                            continue;
                        }
                        if (!views_overlap(lhsView, rhsView))
                        {
                            continue;
                        }
                        if (lhsAccess.isWrite || rhsAccess.isWrite)
                        {
                            depends = true;
                            break;
                        }
                    }

                    if (depends)
                    {
                        break;
                    }
                }

                if (depends)
                {
                    m_passes[rhsIndex].dependencies.emplace_back(static_cast<uint32_t>(lhsIndex));
                }
            }
        }

        return Result::ok();
    }

    Result FrameGraph::validate_pass(const PassRecord& pass) const
    {
        if (pass.type == CommandListType::Graphics)
        {
            if (pass.pipeline.kind == PipelineRequest::Kind::ComputeExternal || pass.pipeline.kind == PipelineRequest::Kind::ComputeOwned)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Graphics pass cannot use a compute pipeline.");
            }
        }
        else if (pass.type == CommandListType::Compute)
        {
            if (!pass.renderTargets.empty() || pass.depthStencil.has_value())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Compute pass cannot bind render target or depth stencil attachments.");
            }
            if (pass.pipeline.kind == PipelineRequest::Kind::GraphicsExternal || pass.pipeline.kind == PipelineRequest::Kind::GraphicsOwned)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Compute pass cannot use a graphics pipeline.");
            }
        }
        else
        {
            if (!pass.renderTargets.empty() || pass.depthStencil.has_value())
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Copy pass cannot bind render target or depth stencil attachments.");
            }
            if (pass.pipeline.kind != PipelineRequest::Kind::None)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Copy pass cannot use a pipeline state.");
            }
        }

        return Result::ok();
    }

    Result FrameGraph::build_execution_order()
    {
        // トポロジカルソートを使い、依存を守りつつ候補が複数あれば登録順を優先します。
        m_executionOrder.clear();
        if (m_passes.empty())
        {
            return Result::ok();
        }

        std::vector<uint32_t> indegrees(m_passes.size(), 0);
        std::vector<std::vector<uint32_t>> outgoing(m_passes.size());
        for (uint32_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            for (uint32_t dependencyIndex : m_passes[passIndex].dependencies)
            {
                ++indegrees[passIndex];
                outgoing[dependencyIndex].emplace_back(passIndex);
            }
        }

        std::vector<bool> scheduled(m_passes.size(), false);
        for (size_t scheduledCount = 0; scheduledCount < m_passes.size(); ++scheduledCount)
        {
            uint32_t candidate = UINT32_MAX;
            for (uint32_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
            {
                if (scheduled[passIndex] || indegrees[passIndex] != 0)
                {
                    continue;
                }

                if (candidate == UINT32_MAX || m_passes[passIndex].registrationIndex < m_passes[candidate].registrationIndex)
                {
                    candidate = passIndex;
                }
            }

            if (candidate == UINT32_MAX)
            {
                return Result::fail(
                    Code::InternalError,
                    Severity::Error,
                    "FrameGraph contains a cyclic dependency.");
            }

            scheduled[candidate] = true;
            m_executionOrder.emplace_back(candidate);
            for (uint32_t nextPass : outgoing[candidate])
            {
                if (indegrees[nextPass] > 0)
                {
                    --indegrees[nextPass];
                }
            }
        }

        return Result::ok();
    }

    ResourceState FrameGraph::resolve_required_state(const PassRecord& pass, const LogicalView& view, bool isWrite) const
    {
        // 要求 state は queue 種別と view 用途から決め、個別 pass に API 固有 state を持ち込ませません。
        if (pass.type == CommandListType::Copy)
        {
            return isWrite ? ResourceState::CopyDest : ResourceState::CopySource;
        }

        switch (view.desc.type)
        {
        case ViewType::RenderTarget:
            return ResourceState::RenderTarget;
        case ViewType::DepthStencil:
            return ResourceState::DepthWrite;
        case ViewType::UnorderedAccessBuffer:
        case ViewType::UnorderedAccessRawBuffer:
        case ViewType::UnorderedAccessTexture2D:
            return ResourceState::UnorderedAccess;
        case ViewType::ConstantBuffer:
        case ViewType::ShaderResourceBuffer:
        case ViewType::ShaderResourceRawBuffer:
        case ViewType::ShaderResourceTexture2D:
            return ResourceState::ShaderResource;
        default:
            return isWrite ? ResourceState::Common : ResourceState::ShaderResource;
        }
    }

    bool FrameGraph::views_overlap(const LogicalView& lhs, const LogicalView& rhs) const
    {
        // buffer は byte range、texture は mip と array slice の重なりで競合判定します。
        if (lhs.resourceIndex != rhs.resourceIndex)
        {
            return false;
        }

        const LogicalResource& resource = m_resources[lhs.resourceIndex];
        if (resource.kind == LogicalResourceKind::Buffer)
        {
            const uint64_t lhsBegin = lhs.desc.byteOffset;
            const uint64_t rhsBegin = rhs.desc.byteOffset;
            const uint64_t lhsEnd = lhs.desc.byteSize == 0 ? UINT64_MAX : lhsBegin + lhs.desc.byteSize;
            const uint64_t rhsEnd = rhs.desc.byteSize == 0 ? UINT64_MAX : rhsBegin + rhs.desc.byteSize;
            return ranges_overlap(lhsBegin, lhsEnd, rhsBegin, rhsEnd);
        }

        const uint64_t lhsMipBegin = lhs.desc.mipSlice;
        const uint64_t lhsMipEnd = lhs.desc.mipSlice + lhs.desc.mipLevels;
        const uint64_t rhsMipBegin = rhs.desc.mipSlice;
        const uint64_t rhsMipEnd = rhs.desc.mipSlice + rhs.desc.mipLevels;
        const uint64_t lhsSliceBegin = lhs.desc.firstArraySlice;
        const uint64_t lhsSliceEnd = lhs.desc.firstArraySlice + lhs.desc.arraySize;
        const uint64_t rhsSliceBegin = rhs.desc.firstArraySlice;
        const uint64_t rhsSliceEnd = rhs.desc.firstArraySlice + rhs.desc.arraySize;
        return ranges_overlap(lhsMipBegin, lhsMipEnd, rhsMipBegin, rhsMipEnd)
            && ranges_overlap(lhsSliceBegin, lhsSliceEnd, rhsSliceBegin, rhsSliceEnd);
    }

    bool FrameGraph::ranges_overlap(uint64_t lhsBegin, uint64_t lhsEnd, uint64_t rhsBegin, uint64_t rhsEnd)
    {
        return lhsBegin < rhsEnd && rhsBegin < lhsEnd;
    }

    size_t FrameGraph::command_list_type_index(CommandListType type)
    {
        switch (type)
        {
        case CommandListType::Graphics:
            return 0;
        case CommandListType::Compute:
            return 1;
        case CommandListType::Copy:
            return 2;
        default:
            return 0;
        }
    }
}
