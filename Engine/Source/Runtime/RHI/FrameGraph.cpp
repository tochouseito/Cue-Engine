#include "FrameGraph.h"

// === C++ includes ===
#include <algorithm>
#include <chrono>
#include <deque>
#include <unordered_set>

namespace Cue::RHI
{
    namespace
    {
        using PassDependencyMap = std::unordered_map<ResourceId,
            std::vector<uint32_t>, ResourceIdHasher>;

        struct QueueExecutionState final
        {
            IQueueContext* queue = nullptr;
            queueContextLease queueLease{};
            bool submitted = false;
        };

        const QueueBatchInfo* find_batch_for_pass(
            const std::vector<QueueBatchInfo>& executionPlan,
            uint32_t passIndex,
            uint32_t& outBatchIndex) noexcept
        {
            for (uint32_t batchIndex = 0; batchIndex < executionPlan.size(); ++batchIndex)
            {
                const QueueBatchInfo& batch = executionPlan[batchIndex];
                if (std::find(batch.passIndices.begin(), batch.passIndices.end(),
                    passIndex) != batch.passIndices.end())
                {
                    outBatchIndex = batchIndex;
                    return &batch;
                }
            }

            return nullptr;
        }
    }

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

    Result FrameGraphBuilder::read_buffer(BufferHandle handle)
    {
        return register_buffer_access(
            handle,
            ResourceAccessType::Read,
            ResourceState::ShaderResource,
            ResourceState::ShaderResource);
    }

    Result FrameGraphBuilder::write_buffer(BufferHandle handle)
    {
        return register_buffer_access(
            handle,
            ResourceAccessType::Write,
            ResourceState::UnorderedAccess,
            ResourceState::UnorderedAccess);
    }

    Result FrameGraphBuilder::read_write_buffer(BufferHandle handle)
    {
        return register_buffer_access(
            handle,
            ResourceAccessType::ReadWrite,
            ResourceState::UnorderedAccess,
            ResourceState::UnorderedAccess);
    }

    Result FrameGraphBuilder::read_texture(TextureHandle handle)
    {
        return register_texture_access(
            handle,
            ResourceAccessType::Read,
            ResourceState::ShaderResource,
            ResourceState::ShaderResource);
    }

    Result FrameGraphBuilder::write_texture(TextureHandle handle)
    {
        return register_texture_access(
            handle,
            ResourceAccessType::Write,
            ResourceState::RenderTarget,
            ResourceState::RenderTarget);
    }

    Result FrameGraphBuilder::read_write_texture(TextureHandle handle)
    {
        return register_texture_access(
            handle,
            ResourceAccessType::ReadWrite,
            ResourceState::UnorderedAccess,
            ResourceState::UnorderedAccess);
    }

    Result FrameGraphBuilder::use_buffer(
        BufferHandle handle,
        ResourceAccessType accessType,
        ResourceState requiredState,
        ResourceState finalState)
    {
        return register_buffer_access(handle, accessType, requiredState, finalState);
    }

    Result FrameGraphBuilder::use_texture(
        TextureHandle handle,
        ResourceAccessType accessType,
        ResourceState requiredState,
        ResourceState finalState)
    {
        return register_texture_access(handle, accessType, requiredState, finalState);
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

    Result FrameGraphBuilder::register_buffer_access(
        BufferHandle handle,
        ResourceAccessType accessType)
    {
        return register_buffer_access(
            handle,
            accessType,
            ResourceState::Common,
            ResourceState::Common);
    }

    Result FrameGraphBuilder::register_buffer_access(
        BufferHandle handle,
        ResourceAccessType accessType,
        ResourceState requiredState,
        ResourceState finalState)
    {
        if (!handle.valid())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Failed to declare buffer access because the handle is invalid.");
        }

        if (m_buildInfo == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Failed to declare buffer access because no compiled pass is active.");
        }

        const ResourceId resourceId = FrameGraph::make_resource_id(handle);
        for (ResourceAccess& existingAccess : m_buildInfo->resourceAccesses)
        {
            if (existingAccess.resourceId == resourceId)
            {
                existingAccess.accessType = FrameGraph::merge_access_type(
                    existingAccess.accessType,
                    accessType);
                existingAccess.requiredState = requiredState;
                existingAccess.finalState = finalState;
                return Result::ok();
            }
        }

        m_buildInfo->resourceAccesses.push_back(
            ResourceAccess{ resourceId, accessType, requiredState, finalState });
        return Result::ok();
    }

    Result FrameGraphBuilder::register_texture_access(
        TextureHandle handle,
        ResourceAccessType accessType)
    {
        return register_texture_access(
            handle,
            accessType,
            ResourceState::Common,
            ResourceState::Common);
    }

    Result FrameGraphBuilder::register_texture_access(
        TextureHandle handle,
        ResourceAccessType accessType,
        ResourceState requiredState,
        ResourceState finalState)
    {
        if (!handle.valid())
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "Failed to declare texture access because the handle is invalid.");
        }

        if (m_buildInfo == nullptr)
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "Failed to declare texture access because no compiled pass is active.");
        }

        const ResourceId resourceId = FrameGraph::make_resource_id(handle);
        for (ResourceAccess& existingAccess : m_buildInfo->resourceAccesses)
        {
            if (existingAccess.resourceId == resourceId)
            {
                existingAccess.accessType = FrameGraph::merge_access_type(
                    existingAccess.accessType,
                    accessType);
                existingAccess.requiredState = requiredState;
                existingAccess.finalState = finalState;
                return Result::ok();
            }
        }

        m_buildInfo->resourceAccesses.push_back(
            ResourceAccess{ resourceId, accessType, requiredState, finalState });
        return Result::ok();
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
            compiledPass.buildInfo = {};
            compiledPass.buildInfo.name = compiledPass.pass->name();
            compiledPass.buildInfo.queueType = compiledPass.pass->type();

            FrameGraphBuilder builder(*this, &compiledPass.buildInfo);
            Result result = compiledPass.pass->setup(builder);
            if (!result)
            {
                return result;
            }

            result = compiledPass.pass->describe_resources(builder);
            if (!result)
            {
                return result;
            }
        }

        Result dependencyResult = build_dependencies();
        if (!dependencyResult)
        {
            return dependencyResult;
        }

        Result executionPlanResult = build_execution_plan();
        if (!executionPlanResult)
        {
            return executionPlanResult;
        }

        m_passBuildInfos.clear();
        m_passBuildInfos.reserve(m_passes.size());
        for (const CompiledPass& compiledPass : m_passes)
        {
            m_passBuildInfos.push_back(compiledPass.buildInfo);
        }

        return validate_dependency_graph();
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
        using Clock = std::chrono::steady_clock;
        auto ms_since =
            [](const Clock::time_point& a_start, const Clock::time_point& a_end)
            {
                return std::chrono::duration<double, std::milli>(a_end - a_start)
                    .count();
            };

        const Clock::time_point executeStartTime = Clock::now();
        double queueWaitMs = 0.0;
        m_executionStats.passStats.clear();
        m_executionStats.passStats.reserve(m_passes.size());
        std::unordered_map<CommandListType, QueueExecutionState> queues{};
        std::vector<commandContextLease> stagedCommandContexts{};
        stagedCommandContexts.reserve(m_passes.size());

        auto ensure_queue = [&](CommandListType queueType) -> Result
        {
            if (queues.contains(queueType))
            {
                return Result::ok();
            }

            QueueExecutionState queueState{};
            const bool usePresentQueue =
                m_desc.usePresentQueue && queueType == CommandListType::Graphics;
            if (usePresentQueue)
            {
                queueState.queue = m_desc.queuePool->get_present_queue_context();
                if (queueState.queue == nullptr)
                {
                    return Result::fail(
                        Code::GetFailed,
                        Severity::Error,
                        "Failed to get present queue context for frame graph execution.");
                }
            }
            else
            {
                Result result = m_desc.queuePool->get_queue_context(
                    queueType, queueState.queueLease);
                if (!result)
                {
                    return Result::fail(
                        result.code,
                        Severity::Error,
                        "Failed to get queue context for frame graph execution.");
                }
                queueState.queue = queueState.queueLease.get();
            }
            queues.insert_or_assign(queueType, std::move(queueState));
            return Result::ok();
        };

        auto return_all_command_contexts = [&]()
        {
            for (commandContextLease& commandContext : stagedCommandContexts)
            {
                if (commandContext)
                {
                    m_desc.commandPool->return_command_context(commandContext);
                }
            }
        };

        auto return_all_queue_contexts = [&]()
        {
            for (auto& [queueType, queueState] : queues)
            {
                (void)queueType;
                if (queueState.queueLease)
                {
                    m_desc.queuePool->return_queue_context(queueState.queueLease);
                }
            }
        };

        for (const QueueBatchInfo& batch : m_executionPlan)
        {
            Result queueResult = ensure_queue(batch.queueType);
            if (!queueResult)
            {
                return_all_command_contexts();
                return_all_queue_contexts();
                return queueResult;
            }

            std::vector<ICommandContext*> commandContextPointers{};
            commandContextPointers.reserve(batch.passIndices.size());
            std::vector<FrameGraphExecutionStats::PassExecutionStats*> batchPassStats{};
            batchPassStats.reserve(batch.passIndices.size());
            for (uint32_t passIndex : batch.passIndices)
            {
                if (passIndex >= m_passes.size())
                {
                    return_all_command_contexts();
                    return_all_queue_contexts();
                    return Result::fail(
                        Code::InternalError,
                        Severity::Error,
                        "FrameGraph batch contains an invalid pass index.");
                }

                CompiledPass& compiledPass = m_passes[passIndex];
                m_executionStats.passStats.push_back(
                    FrameGraphExecutionStats::PassExecutionStats{
                        compiledPass.pass->name(),
                        compiledPass.pass->type() });
                FrameGraphExecutionStats::PassExecutionStats& passStats =
                    m_executionStats.passStats.back();
                commandContextLease commandContext{};
                const Clock::time_point acquireStartTime = Clock::now();
                Result result = m_desc.commandPool->get_command_context(
                    compiledPass.pass->type(), commandContext);
                if (!result)
                {
                    return_all_command_contexts();
                    return_all_queue_contexts();
                    return Result::fail(
                        result.code,
                        Severity::Error,
                        "Failed to get command context for frame graph batch.");
                }

                result = commandContext->reset();
                if (!result)
                {
                    m_desc.commandPool->return_command_context(commandContext);
                    return_all_command_contexts();
                    return_all_queue_contexts();
                    return Result::fail(
                        result.code,
                        Severity::Error,
                        "Failed to reset command context for frame graph batch.");
                }

                result = commandContext->setup(frameIndex, m_bufferCount);
                if (!result)
                {
                    m_desc.commandPool->return_command_context(commandContext);
                    return_all_command_contexts();
                    return_all_queue_contexts();
                    return Result::fail(
                        result.code,
                        Severity::Error,
                        "Failed to setup command context for frame graph batch.");
                }
                passStats.acquireResetSetupMs =
                    ms_since(acquireStartTime, Clock::now());

                const Clock::time_point preBarrierStartTime = Clock::now();
                for (const ResourceAccess& access :
                    compiledPass.buildInfo.resourceAccesses)
                {
                    ResourceBarrierDesc barrierDesc{};
                    barrierDesc.after = access.requiredState;
                    if (access.resourceId.kind == ResourceKind::Buffer)
                    {
                        result = commandContext->resource_barrier(
                            make_buffer_handle(access.resourceId), barrierDesc);
                    }
                    else
                    {
                        result = commandContext->resource_barrier(
                            make_texture_handle(access.resourceId), barrierDesc);
                    }

                    if (!result)
                    {
                        m_desc.commandPool->return_command_context(commandContext);
                        return_all_command_contexts();
                        return_all_queue_contexts();
                        return Result::fail(
                            result.code,
                            Severity::Error,
                            "Failed to transition resource before frame graph pass execution.");
                    }
                }
                passStats.preBarrierMs =
                    ms_since(preBarrierStartTime, Clock::now());

                FrameGraphContext context{ FrameGraphContextDesc{
                    m_desc.width, m_desc.height, frameIndex, commandContext.get()} };
                const Clock::time_point passExecuteStartTime = Clock::now();
                commandContext->begin_event(compiledPass.pass->name());
                compiledPass.pass->execute(context);
                commandContext->end_event();
                passStats.cpuExecuteMs =
                    ms_since(passExecuteStartTime, Clock::now());

                const Clock::time_point postBarrierStartTime = Clock::now();
                for (const ResourceAccess& access :
                    compiledPass.buildInfo.resourceAccesses)
                {
                    if (access.finalState == access.requiredState)
                    {
                        continue;
                    }

                    ResourceBarrierDesc barrierDesc{};
                    barrierDesc.after = access.finalState;
                    if (access.resourceId.kind == ResourceKind::Buffer)
                    {
                        result = commandContext->resource_barrier(
                            make_buffer_handle(access.resourceId), barrierDesc);
                    }
                    else
                    {
                        result = commandContext->resource_barrier(
                            make_texture_handle(access.resourceId), barrierDesc);
                    }

                    if (!result)
                    {
                        m_desc.commandPool->return_command_context(commandContext);
                        return_all_command_contexts();
                        return_all_queue_contexts();
                        return Result::fail(
                            result.code,
                            Severity::Error,
                            "Failed to transition resource after frame graph pass execution.");
                    }
                }
                passStats.postBarrierMs =
                    ms_since(postBarrierStartTime, Clock::now());

                const Clock::time_point closeStartTime = Clock::now();
                result = commandContext->close();
                if (!result)
                {
                    m_desc.commandPool->return_command_context(commandContext);
                    return_all_command_contexts();
                    return_all_queue_contexts();
                    return Result::fail(
                        result.code,
                        Severity::Error,
                        "Failed to close command context for frame graph batch.");
                }
                passStats.closeMs = ms_since(closeStartTime, Clock::now());

                commandContextPointers.push_back(commandContext.get());
                stagedCommandContexts.push_back(std::move(commandContext));
                batchPassStats.push_back(&passStats);
            }

            QueueExecutionState& queueState = queues.at(batch.queueType);
            for (uint32_t waitBatchIndex : batch.waitBatchIndices)
            {
                if (waitBatchIndex >= m_executionPlan.size())
                {
                    return_all_command_contexts();
                    return_all_queue_contexts();
                    return Result::fail(
                        Code::InternalError,
                        Severity::Error,
                        "FrameGraph batch wait index is out of range.");
                }

                const QueueBatchInfo& producerBatch = m_executionPlan[waitBatchIndex];
                QueueExecutionState& producerQueueState = queues.at(producerBatch.queueType);
                if (producerQueueState.queue == nullptr || queueState.queue == nullptr)
                {
                    return_all_command_contexts();
                    return_all_queue_contexts();
                    return Result::fail(
                        Code::InvalidState,
                        Severity::Error,
                        "FrameGraph queue state is not initialized.");
                }

                const Clock::time_point waitStartTime = Clock::now();
                Result waitResult = queueState.queue->wait_for_queue(
                    *producerQueueState.queue);
                if (!waitResult)
                {
                    return_all_command_contexts();
                    return_all_queue_contexts();
                    return Result::fail(
                        waitResult.code,
                        Severity::Error,
                        "Failed to wait for producer queue before frame graph batch submission.");
                }

                queueWaitMs += ms_since(waitStartTime, Clock::now());
            }

            const Clock::time_point submitSignalStartTime = Clock::now();
            Result submitResult = queueState.queue->submit(commandContextPointers);
            if (!submitResult)
            {
                return_all_command_contexts();
                return_all_queue_contexts();
                return Result::fail(
                    submitResult.code,
                    Severity::Error,
                    "Failed to submit frame graph batch.");
            }

            Result signalResult = queueState.queue->signal();
            if (!signalResult)
            {
                return_all_command_contexts();
                return_all_queue_contexts();
                return Result::fail(
                    signalResult.code,
                    Severity::Error,
                    "Failed to signal queue after frame graph batch submission.");
            }
            const double submitSignalMs =
                ms_since(submitSignalStartTime, Clock::now());
            const double submitSignalMsPerPass =
                batchPassStats.empty()
                ? 0.0
                : (submitSignalMs / static_cast<double>(batchPassStats.size()));
            for (FrameGraphExecutionStats::PassExecutionStats* passStats :
                batchPassStats)
            {
                if (passStats != nullptr)
                {
                    passStats->submitSignalMs = submitSignalMsPerPass;
                }
            }

            queueState.submitted = true;
        }

        for (auto& [queueType, queueState] : queues)
        {
            (void)queueType;
            if (!queueState.submitted || queueState.queue == nullptr)
            {
                continue;
            }

            const Clock::time_point waitStartTime = Clock::now();
            Result waitResult = queueState.queue->wait();
            if (!waitResult)
            {
                return_all_command_contexts();
                return_all_queue_contexts();
                return Result::fail(
                    waitResult.code,
                    Severity::Error,
                    "Failed to wait for frame graph queue completion.");
            }
            queueWaitMs += ms_since(waitStartTime, Clock::now());
        }

        return_all_command_contexts();
        return_all_queue_contexts();
        m_executionStats.totalExecuteMs =
            ms_since(executeStartTime, Clock::now());
        m_executionStats.queueWaitMs = queueWaitMs;
        m_executionStats.submitMs =
            m_executionStats.totalExecuteMs - m_executionStats.queueWaitMs;
        if (m_executionStats.submitMs < 0.0)
        {
            m_executionStats.submitMs = 0.0;
        }
        m_executionStats.hasGpuFrameMs = false;
        m_executionStats.gpuFrameMs = 0.0;
        return Result::ok();
    }

    ResourceId FrameGraph::make_resource_id(BufferHandle handle) noexcept
    {
        return ResourceId{
            ResourceKind::Buffer,
            handle.index,
            handle.generation };
    }

    ResourceId FrameGraph::make_resource_id(TextureHandle handle) noexcept
    {
        return ResourceId{
            ResourceKind::Texture,
            handle.index,
            handle.generation };
    }

    BufferHandle FrameGraph::make_buffer_handle(const ResourceId& resourceId) noexcept
    {
        return BufferHandle{
            resourceId.index,
            resourceId.generation };
    }

    TextureHandle FrameGraph::make_texture_handle(const ResourceId& resourceId) noexcept
    {
        return TextureHandle{
            resourceId.index,
            resourceId.generation };
    }

    ResourceAccessType FrameGraph::merge_access_type(
        ResourceAccessType current,
        ResourceAccessType incoming) noexcept
    {
        if (current == incoming)
        {
            return current;
        }

        return ResourceAccessType::ReadWrite;
    }

    bool FrameGraph::has_dependency(
        ResourceAccessType previous,
        ResourceAccessType next) noexcept
    {
        return previous != ResourceAccessType::Read ||
            next != ResourceAccessType::Read;
    }

    Result FrameGraph::build_dependencies()
    {
        for (CompiledPass& compiledPass : m_passes)
        {
            compiledPass.buildInfo.dependencyPassIndices.clear();
        }

        PassDependencyMap previousAccesses{};
        previousAccesses.reserve(m_passes.size() * 4);
        for (uint32_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            CompiledPass& compiledPass = m_passes[passIndex];
            std::unordered_set<uint32_t> dependencySet{};

            for (const ResourceAccess& currentAccess :
                compiledPass.buildInfo.resourceAccesses)
            {
                const auto previousIt =
                    previousAccesses.find(currentAccess.resourceId);
                if (previousIt != previousAccesses.end())
                {
                    for (uint32_t previousPassIndex : previousIt->second)
                    {
                        const CompiledPass& previousPass = m_passes[previousPassIndex];
                        const auto previousAccessIt = std::find_if(
                            previousPass.buildInfo.resourceAccesses.begin(),
                            previousPass.buildInfo.resourceAccesses.end(),
                            [&](const ResourceAccess& access)
                            {
                                return access.resourceId == currentAccess.resourceId;
                            });
                        if (previousAccessIt == previousPass.buildInfo.resourceAccesses.end())
                        {
                            continue;
                        }

                        if (has_dependency(
                            previousAccessIt->accessType,
                            currentAccess.accessType))
                        {
                            dependencySet.insert(previousPassIndex);
                        }
                    }
                }

                previousAccesses[currentAccess.resourceId].push_back(passIndex);
            }

            compiledPass.buildInfo.dependencyPassIndices.assign(
                dependencySet.begin(),
                dependencySet.end());
            std::sort(
                compiledPass.buildInfo.dependencyPassIndices.begin(),
                compiledPass.buildInfo.dependencyPassIndices.end());
        }

        return Result::ok();
    }

    Result FrameGraph::validate_dependency_graph() const
    {
        std::vector<uint32_t> indegrees(m_passes.size(), 0);
        std::vector<std::vector<uint32_t>> forwardEdges(m_passes.size());
        for (uint32_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            const CompiledPass& compiledPass = m_passes[passIndex];
            for (uint32_t dependencyPassIndex :
                compiledPass.buildInfo.dependencyPassIndices)
            {
                if (dependencyPassIndex >= m_passes.size())
                {
                    return Result::fail(
                        Code::InternalError,
                        Severity::Error,
                        "FrameGraph dependency index is out of range.");
                }

                forwardEdges[dependencyPassIndex].push_back(passIndex);
                indegrees[passIndex] += 1;
            }
        }

        std::deque<uint32_t> readyPassIndices{};
        for (uint32_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            if (indegrees[passIndex] == 0)
            {
                readyPassIndices.push_back(passIndex);
            }
        }

        uint32_t processedCount = 0;
        while (!readyPassIndices.empty())
        {
            const uint32_t passIndex = readyPassIndices.front();
            readyPassIndices.pop_front();
            processedCount += 1;

            for (uint32_t nextPassIndex : forwardEdges[passIndex])
            {
                if (--indegrees[nextPassIndex] == 0)
                {
                    readyPassIndices.push_back(nextPassIndex);
                }
            }
        }

        if (processedCount != m_passes.size())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "FrameGraph dependency graph contains a cycle.");
        }

        return Result::ok();
    }

    Result FrameGraph::build_execution_plan()
    {
        m_executionPlan.clear();
        if (m_passes.empty())
        {
            return Result::ok();
        }

        std::vector<uint32_t> indegrees(m_passes.size(), 0);
        std::vector<std::vector<uint32_t>> forwardEdges(m_passes.size());
        for (uint32_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            for (uint32_t dependencyPassIndex :
                m_passes[passIndex].buildInfo.dependencyPassIndices)
            {
                if (dependencyPassIndex >= m_passes.size())
                {
                    return Result::fail(
                        Code::InternalError,
                        Severity::Error,
                        "FrameGraph dependency index is out of range while building execution plan.");
                }

                forwardEdges[dependencyPassIndex].push_back(passIndex);
                indegrees[passIndex] += 1;
            }
        }

        std::vector<uint32_t> readyPassIndices{};
        readyPassIndices.reserve(m_passes.size());
        for (uint32_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            if (indegrees[passIndex] == 0)
            {
                readyPassIndices.push_back(passIndex);
            }
        }

        uint32_t processedCount = 0;
        while (!readyPassIndices.empty())
        {
            std::unordered_map<CommandListType, std::vector<uint32_t>> stageBatches{};
            std::vector<uint32_t> currentReadyPassIndices = readyPassIndices;
            readyPassIndices.clear();

            for (uint32_t passIndex : currentReadyPassIndices)
            {
                const CommandListType queueType =
                    m_passes[passIndex].buildInfo.queueType;
                stageBatches[queueType].push_back(passIndex);
            }

            std::vector<uint32_t> stageBatchIndices{};
            stageBatchIndices.reserve(stageBatches.size());
            for (auto& [queueType, passIndices] : stageBatches)
            {
                QueueBatchInfo batch{};
                batch.queueType = queueType;
                batch.passIndices = std::move(passIndices);
                m_executionPlan.push_back(std::move(batch));
                stageBatchIndices.push_back(
                    static_cast<uint32_t>(m_executionPlan.size() - 1));
            }

            std::sort(stageBatchIndices.begin(), stageBatchIndices.end(),
                [&](uint32_t a_left, uint32_t a_right)
                {
                    return static_cast<uint32_t>(m_executionPlan[a_left].queueType) <
                        static_cast<uint32_t>(m_executionPlan[a_right].queueType);
                });

            for (uint32_t batchIndex : stageBatchIndices)
            {
                QueueBatchInfo& batch = m_executionPlan[batchIndex];
                std::unordered_set<uint32_t> waitBatchSet{};
                for (uint32_t passIndex : batch.passIndices)
                {
                    const CompiledPass& compiledPass = m_passes[passIndex];
                    for (uint32_t dependencyPassIndex :
                        compiledPass.buildInfo.dependencyPassIndices)
                    {
                        uint32_t producerBatchIndex = 0;
                        const QueueBatchInfo* producerBatch = find_batch_for_pass(
                            m_executionPlan, dependencyPassIndex, producerBatchIndex);
                        if (producerBatch == nullptr)
                        {
                            return Result::fail(
                                Code::InternalError,
                                Severity::Error,
                                "Failed to resolve producer batch while building execution plan.");
                        }

                        if (producerBatch->queueType != batch.queueType)
                        {
                            waitBatchSet.insert(producerBatchIndex);
                        }
                    }
                }

                batch.waitBatchIndices.assign(
                    waitBatchSet.begin(), waitBatchSet.end());
                std::sort(batch.waitBatchIndices.begin(),
                    batch.waitBatchIndices.end());
            }

            for (uint32_t passIndex : currentReadyPassIndices)
            {
                processedCount += 1;
                for (uint32_t nextPassIndex : forwardEdges[passIndex])
                {
                    if (--indegrees[nextPassIndex] == 0)
                    {
                        readyPassIndices.push_back(nextPassIndex);
                    }
                }
            }
        }

        if (processedCount != m_passes.size())
        {
            return Result::fail(
                Code::InvalidState,
                Severity::Error,
                "FrameGraph execution plan could not process all passes.");
        }

        return Result::ok();
    }
}
