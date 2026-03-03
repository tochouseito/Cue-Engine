#include "FrameGraph.h"

namespace Cue::GraphicsCore
{
    Result FrameGraph::add_pass(std::unique_ptr<FrameGraphPass> pass)
    {
        if (pass == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "FrameGraph pass is null");
        }

        m_passes.push_back(CompiledPass{ std::move(pass), {} });
        m_isDirty = true;
        return Result::ok();
    }

    template <class HandleT>
    HandleT FrameGraph::declare_resource(std::string_view name, ResourceKind expectedKind)
    {
        const ResourceNameId nameId = fnv1a64(name);
        const auto found = m_resourceByNameId.find(nameId);
        if (found != m_resourceByNameId.end())
        {
            throw std::runtime_error("Duplicated logical resource name: " + std::string(name));
        }

        if (m_resources.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
        {
            throw std::overflow_error("FrameGraph resource index overflow");
        }

        const uint32_t index = static_cast<uint32_t>(m_resources.size());
        const ResourceState initialState =
            (expectedKind == ResourceKind::Texture && name.starts_with("SwapChain.BackBuffer."))
            ? ResourceState::Present
            : ResourceState::Common;

        const ResourceRef ref{ expectedKind, index, 1 };
        LogicalResource resource{
            ref,
            nameId,
            std::string(name),
            initialState
        };
        resource.bufferDesc.name = resource.debugName;
        resource.bufferDesc.bufferingCount = 1;
        resource.textureDesc.name = resource.debugName;

        m_resources.push_back(std::move(resource));
        m_resourceByNameId.emplace(nameId, ref);
        m_isDirty = true;
        return HandleT{ index, 1 };
    }

    template <class HandleT>
    HandleT FrameGraph::find_resource(std::string_view name, ResourceKind expectedKind) const
    {
        const ResourceNameId nameId = fnv1a64(name);
        const auto found = m_resourceByNameId.find(nameId);
        if (found == m_resourceByNameId.end())
        {
            throw std::runtime_error("Logical resource is not declared: " + std::string(name));
        }

        validate_resource_ref(found->second, expectedKind);
        return HandleT{ found->second.index, found->second.generation };
    }

    uint32_t FrameGraph::resolve_buffering_count(uint32_t requestedCount) const noexcept
    {
        if (requestedCount == 0)
        {
            return m_defaultBufferingCount;
        }

        return (std::max)(requestedCount, 1u);
    }

    BufferHandle FrameGraph::create_buffer(std::string_view name, const BufferDesc& desc)
    {
        const std::string_view resolvedName = desc.name.empty() ? name : desc.name;
        const BufferHandle handle = declare_resource<BufferHandle>(resolvedName, ResourceKind::Buffer);

        LogicalResource& resource = m_resources[handle.index];
        resource.bufferDesc = desc;
        resource.bufferDesc.name = resource.debugName;
        resource.bufferDesc.bufferingCount = resolve_buffering_count(desc.bufferingCount);

        return handle;
    }

    TextureHandle FrameGraph::create_texture(std::string_view name, const TextureDesc& desc)
    {
        const std::string_view resolvedName = desc.name.empty() ? name : desc.name;
        const TextureHandle handle = declare_resource<TextureHandle>(resolvedName, ResourceKind::Texture);

        LogicalResource& resource = m_resources[handle.index];
        resource.textureDesc = desc;
        resource.textureDesc.name = resource.debugName;

        return handle;
    }

    BufferHandle FrameGraph::import_buffer(std::string_view name, const BufferDesc& desc, ResourceState initialState)
    {
        const std::string_view resolvedName = desc.name.empty() ? name : desc.name;
        const BufferHandle handle = declare_resource<BufferHandle>(resolvedName, ResourceKind::Buffer);

        LogicalResource& resource = m_resources[handle.index];
        resource.bufferDesc = desc;
        resource.bufferDesc.name = resource.debugName;
        resource.bufferDesc.bufferingCount = resolve_buffering_count(desc.bufferingCount);
        resource.initialState = initialState;
        resource.external = true;

        return handle;
    }

    TextureHandle FrameGraph::import_texture(std::string_view name, const TextureDesc& desc, ResourceState initialState)
    {
        const std::string_view resolvedName = desc.name.empty() ? name : desc.name;
        const TextureHandle handle = declare_resource<TextureHandle>(resolvedName, ResourceKind::Texture);

        LogicalResource& resource = m_resources[handle.index];
        resource.textureDesc = desc;
        resource.textureDesc.name = resource.debugName;
        resource.initialState = initialState;
        resource.external = true;

        return handle;
    }

    BufferHandle FrameGraph::get_buffer(std::string_view name)
    {
        return find_resource<BufferHandle>(name, ResourceKind::Buffer);
    }

    TextureHandle FrameGraph::get_texture(std::string_view name)
    {
        return find_resource<TextureHandle>(name, ResourceKind::Texture);
    }

    void FrameGraph::validate_resource_handle(BufferHandle handle) const
    {
        validate_resource_ref(to_resource_ref(handle), ResourceKind::Buffer);
    }

    void FrameGraph::validate_resource_handle(TextureHandle handle) const
    {
        validate_resource_ref(to_resource_ref(handle), ResourceKind::Texture);
    }

    void FrameGraph::validate_resource_ref(ResourceRef ref, ResourceKind expectedKind) const
    {
        if (!ref.valid())
        {
            throw std::runtime_error("FrameGraph resource handle is invalid");
        }
        if (ref.kind != expectedKind)
        {
            throw std::runtime_error("FrameGraph resource kind mismatch");
        }
        if (ref.index >= m_resources.size())
        {
            throw std::runtime_error("FrameGraph resource handle is out of range");
        }
        if (m_resources[ref.index].ref.generation != ref.generation)
        {
            throw std::runtime_error("FrameGraph resource generation mismatch");
        }
    }

    const FrameGraph::LogicalResource& FrameGraph::get_logical_resource(ResourceRef ref) const
    {
        validate_resource_ref(ref, ref.kind);
        return m_resources[ref.index];
    }

    Result FrameGraph::resolve_buffer(BufferHandle logicalHandle, uint32_t bufferIndex, BufferHandle& outHandle) const
    {
        try
        {
            const LogicalResource& resource = get_logical_resource(to_resource_ref(logicalHandle));
            return m_bufferManager.get_buffer(resource.nameId, bufferIndex, outHandle);
        }
        catch (const std::exception& e)
        {
            Core::Logger::log(Core::LogSink::debugConsole, "[FrameGraph] resolve_buffer failed: {}\n", e.what());
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Failed to resolve logical buffer");
        }
    }

    Result FrameGraph::resolve_texture(TextureHandle logicalHandle, TextureHandle& outHandle) const
    {
        try
        {
            const LogicalResource& resource = get_logical_resource(to_resource_ref(logicalHandle));
            return m_textureManager.get_texture(resource.nameId, outHandle);
        }
        catch (const std::exception& e)
        {
            Core::Logger::log(Core::LogSink::debugConsole, "[FrameGraph] resolve_texture failed: {}\n", e.what());
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Failed to resolve logical texture");
        }
    }

    Result FrameGraph::resolve_descriptor_binding(const DescriptorBindingDecl& binding, uint32_t bufferIndex, FrameGraphContext::ResolvedDescriptorBinding& outBinding) const
    {
        if (binding.hasBufferIndex && binding.bufferIndex != bufferIndex)
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Info, 0, "Descriptor binding is not active for this buffer index");
        }

        outBinding.type = binding.type;
        outBinding.visibility = binding.visibility;
        outBinding.shaderRegister = binding.shaderRegister;

        if (binding.resource.kind == ResourceKind::Buffer)
        {
            BufferHandle physicalHandle{};
            const Result result = resolve_buffer(BufferHandle{ binding.resource.index, binding.resource.generation }, bufferIndex, physicalHandle);
            if (!result)
            {
                return result;
            }

            outBinding.resourceKind = ResourceKind::Buffer;
            outBinding.resourceIndex = physicalHandle.index;
            outBinding.resourceGeneration = physicalHandle.generation;
            return Result::ok();
        }

        TextureHandle physicalHandle{};
        const Result result = resolve_texture(TextureHandle{ binding.resource.index, binding.resource.generation }, physicalHandle);
        if (!result)
        {
            return result;
        }

        outBinding.resourceKind = ResourceKind::Texture;
        outBinding.resourceIndex = physicalHandle.index;
        outBinding.resourceGeneration = physicalHandle.generation;
        return Result::ok();
    }

    Result FrameGraph::make_barrier_desc(const BarrierEvent& event, uint32_t bufferIndex, ResourceBarrierDesc& outBarrier) const
    {
        outBarrier.before = event.before;
        outBarrier.after = event.after;

        if (event.handle.kind == ResourceKind::Buffer)
        {
            BufferHandle physicalHandle{};
            const Result result = resolve_buffer(BufferHandle{ event.handle.index, event.handle.generation }, bufferIndex, physicalHandle);
            if (!result)
            {
                return result;
            }

            outBarrier.kind = ResourceKind::Buffer;
            outBarrier.index = physicalHandle.index;
            outBarrier.generation = physicalHandle.generation;
            return Result::ok();
        }

        TextureHandle physicalHandle{};
        const Result result = resolve_texture(TextureHandle{ event.handle.index, event.handle.generation }, physicalHandle);
        if (!result)
        {
            return result;
        }

        outBarrier.kind = ResourceKind::Texture;
        outBarrier.index = physicalHandle.index;
        outBarrier.generation = physicalHandle.generation;
        return Result::ok();
    }

    std::vector<ResourceState> FrameGraph::initial_resource_states() const
    {
        std::vector<ResourceState> states;
        states.reserve(m_resources.size());
        for (const LogicalResource& resource : m_resources)
        {
            states.push_back(resource.initialState);
        }
        return states;
    }

    Result FrameGraph::materialize_declared_resources()
    {
        // build() maps logical resources to manager-owned backend resources.
        // External resources are imported and therefore skipped here.
        for (const LogicalResource& resource : m_resources)
        {
            if (resource.external)
            {
                continue;
            }

            if (resource.ref.kind == ResourceKind::Buffer)
            {
                BufferHandle existingHandle{};
                const Result getResult = m_bufferManager.get_buffer(resource.nameId, 0, existingHandle);
                if (getResult)
                {
                    continue;
                }

                BufferHandle createdHandle{};
                const Result createResult = m_bufferManager.create_buffer(resource.bufferDesc, createdHandle);
                if (!createResult)
                {
                    return createResult;
                }
                continue;
            }

            TextureHandle existingTexture{};
            const Result getResult = m_textureManager.get_texture(resource.nameId, existingTexture);
            if (getResult)
            {
                continue;
            }

            TextureHandle createdHandle{};
            const Result createResult = m_textureManager.create_texture(resource.textureDesc, createdHandle);
            if (!createResult)
            {
                return createResult;
            }
        }

        return Result::ok();
    }

    void FrameGraph::declare_pipeline(PipelineBindingDecl& outDecl, const PipelineDesc& desc)
    {
        if (outDecl.declared)
        {
            throw std::runtime_error("Pipeline is already declared for this pass");
        }
        if (desc.name.empty())
        {
            throw std::runtime_error("Pipeline logical name is empty");
        }

        const ResourceNameId nameId = fnv1a64(desc.name);
        if (m_pipelineDecls.contains(nameId))
        {
            throw std::runtime_error("Duplicated pipeline logical name: " + std::string(desc.name));
        }

        outDecl.declared = true;
        outDecl.nameId = nameId;
        outDecl.debugName = std::string(desc.name);
        outDecl.desc = desc;
        outDecl.desc.name = outDecl.debugName;
        m_pipelineDecls.emplace(nameId, outDecl);
    }

    void FrameGraph::declare_root_signature(RootSignatureBindingDecl& outDecl, const RootSignatureDesc& desc)
    {
        if (outDecl.declared)
        {
            throw std::runtime_error("Root signature is already declared for this pass");
        }
        if (desc.name.empty())
        {
            throw std::runtime_error("Root signature logical name is empty");
        }

        const ResourceNameId nameId = fnv1a64(desc.name);
        if (m_rootSignatureDecls.contains(nameId))
        {
            throw std::runtime_error("Duplicated root signature logical name: " + std::string(desc.name));
        }

        outDecl.declared = true;
        outDecl.nameId = nameId;
        outDecl.debugName = std::string(desc.name);
        outDecl.desc = desc;
        outDecl.desc.name = outDecl.debugName;
        m_rootSignatureDecls.emplace(nameId, outDecl);
    }

    void FrameGraph::declare_shader(std::vector<ShaderBindingDecl>& outDecls, const ShaderCompileDesc& desc)
    {
        if (desc.name.empty())
        {
            throw std::runtime_error("Shader logical name is empty");
        }

        const ResourceNameId nameId = fnv1a64(desc.name);
        if (m_shaderDecls.contains(nameId))
        {
            throw std::runtime_error("Duplicated shader logical name: " + std::string(desc.name));
        }

        const ShaderBindingDecl decl{ nameId, desc, {} };
        outDecls.push_back(decl);
        m_shaderDecls.emplace(nameId, decl);
    }

    void FrameGraph::reference_pipeline(PipelineBindingDecl& outDecl, std::string_view name)
    {
        if (outDecl.declared)
        {
            throw std::runtime_error("Pipeline is already declared for this pass");
        }

        const ResourceNameId nameId = fnv1a64(name);
        const auto it = m_pipelineDecls.find(nameId);
        if (it == m_pipelineDecls.end())
        {
            throw std::runtime_error("Pipeline logical name is not declared: " + std::string(name));
        }

        outDecl = it->second;
    }

    void FrameGraph::reference_root_signature(RootSignatureBindingDecl& outDecl, std::string_view name)
    {
        if (outDecl.declared)
        {
            throw std::runtime_error("Root signature is already declared for this pass");
        }

        const ResourceNameId nameId = fnv1a64(name);
        const auto it = m_rootSignatureDecls.find(nameId);
        if (it == m_rootSignatureDecls.end())
        {
            throw std::runtime_error("Root signature logical name is not declared: " + std::string(name));
        }

        outDecl = it->second;
    }

    void FrameGraph::reference_shader(std::vector<ShaderBindingDecl>& outDecls, std::string_view name)
    {
        const ResourceNameId nameId = fnv1a64(name);
        const auto it = m_shaderDecls.find(nameId);
        if (it == m_shaderDecls.end())
        {
            throw std::runtime_error("Shader logical name is not declared: " + std::string(name));
        }

        const auto exists = std::find_if(outDecls.begin(), outDecls.end(), [nameId](const ShaderBindingDecl& decl) { return decl.nameId == nameId; });
        if (exists != outDecls.end())
        {
            throw std::runtime_error("Shader is already referenced by this pass: " + std::string(name));
        }

        outDecls.push_back(it->second);
    }

    void FrameGraph::declare_descriptor(std::vector<DescriptorBindingDecl>& outDecls, const DescriptorBindingDecl& desc)
    {
        const auto exists = std::find_if(
            outDecls.begin(),
            outDecls.end(),
            [&desc](const DescriptorBindingDecl& current)
            {
                return current.type == desc.type
                    && current.shaderRegister == desc.shaderRegister
                    && current.visibility == desc.visibility
                    && current.hasBufferIndex == desc.hasBufferIndex
                    && (!current.hasBufferIndex || current.bufferIndex == desc.bufferIndex);
            });

        if (exists != outDecls.end())
        {
            throw std::runtime_error("Descriptor register is already declared for this pass");
        }

        outDecls.push_back(desc);
    }

    Result FrameGraph::resolve_pipeline_artifacts()
    {
        // Pipeline artifacts are shared through the manager registries.
        // Reuse by name when present, otherwise create here.
        for (auto& [nameId, rootSignatureDecl] : m_rootSignatureDecls)
        {
            Result result = m_pipelineManager.get_root_signature(nameId, rootSignatureDecl.handle);
            if (!result)
            {
                result = m_pipelineManager.create_root_signature(rootSignatureDecl.desc, rootSignatureDecl.handle);
                if (!result)
                {
                    return result;
                }
            }
        }

        for (auto& [nameId, shaderDecl] : m_shaderDecls)
        {
            Result result = m_pipelineManager.get_shader(nameId, shaderDecl.handle);
            if (!result)
            {
                result = m_pipelineManager.compile_shader(shaderDecl.desc, shaderDecl.handle);
                if (!result)
                {
                    return result;
                }
            }
        }

        for (auto& [nameId, pipelineDecl] : m_pipelineDecls)
        {
            Result result = m_pipelineManager.get_pipeline(nameId, pipelineDecl.handle);
            if (!result)
            {
                result = m_pipelineManager.create_pipeline(pipelineDecl.desc, pipelineDecl.handle);
                if (!result)
                {
                    return result;
                }
            }
        }

        for (CompiledPass& compiledPass : m_passes)
        {
            if (compiledPass.rootSignatureDecl.declared)
            {
                compiledPass.rootSignatureDecl.handle = m_rootSignatureDecls.at(compiledPass.rootSignatureDecl.nameId).handle;
            }

            for (ShaderBindingDecl& shaderDecl : compiledPass.shaderDecls)
            {
                shaderDecl.handle = m_shaderDecls.at(shaderDecl.nameId).handle;
            }

            if (compiledPass.pipelineDecl.declared)
            {
                compiledPass.pipelineDecl.handle = m_pipelineDecls.at(compiledPass.pipelineDecl.nameId).handle;
            }
        }

        return Result::ok();
    }

    Result FrameGraph::topological_sort_by_resource_dependencies(std::vector<size_t>& outSorted) const
    {
        // Derive pass order from resource hazards.
        // Reads depend on the latest writer; writes depend on both readers and writers.
        const size_t passCount = m_passes.size();
        std::vector<std::unordered_set<size_t>> adjacency(passCount);
        std::vector<size_t> indegree(passCount, 0);
        std::vector<ResourceHazardState> hazardByResource(m_resources.size());

        auto add_edge = [&adjacency, &indegree](size_t from, size_t to)
            {
                if (from == to)
                {
                    return;
                }

                if (adjacency[from].insert(to).second)
                {
                    ++indegree[to];
                }
            };

        for (size_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            for (const ResourceAccess& access : m_passes[passIndex].accesses)
            {
                ResourceHazardState& hazard = hazardByResource[access.handle.index];

                if (access.type == ResourceAccessType::Read)
                {
                    if (hazard.lastWriter >= 0)
                    {
                        add_edge(static_cast<size_t>(hazard.lastWriter), passIndex);
                    }

                    if (std::find(hazard.lastReaders.begin(), hazard.lastReaders.end(), static_cast<int32_t>(passIndex)) == hazard.lastReaders.end())
                    {
                        hazard.lastReaders.push_back(static_cast<int32_t>(passIndex));
                    }
                    continue;
                }

                if (hazard.lastWriter >= 0)
                {
                    add_edge(static_cast<size_t>(hazard.lastWriter), passIndex);
                }
                for (int32_t readerPass : hazard.lastReaders)
                {
                    add_edge(static_cast<size_t>(readerPass), passIndex);
                }

                hazard.lastReaders.clear();
                hazard.lastWriter = static_cast<int32_t>(passIndex);
            }
        }

        std::deque<size_t> zeroIndegree;
        for (size_t i = 0; i < passCount; ++i)
        {
            if (indegree[i] == 0)
            {
                zeroIndegree.push_back(i);
            }
        }

        outSorted.clear();
        outSorted.reserve(passCount);

        while (!zeroIndegree.empty())
        {
            const size_t node = zeroIndegree.front();
            zeroIndegree.pop_front();
            outSorted.push_back(node);

            for (size_t next : adjacency[node])
            {
                if (--indegree[next] == 0)
                {
                    zeroIndegree.push_back(next);
                }
            }
        }

        if (outSorted.size() != passCount)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "FrameGraph has cyclic pass dependencies");
        }

        return Result::ok();
    }

    void FrameGraph::reorder_compiled_passes(const std::vector<size_t>& sortedOrder)
    {
        std::vector<CompiledPass> reordered;
        reordered.reserve(m_passes.size());
        for (size_t oldIndex : sortedOrder)
        {
            reordered.push_back(std::move(m_passes[oldIndex]));
        }
        m_passes = std::move(reordered);
    }

    void FrameGraph::build_execution_dependencies()
    {
        // Build per-pass dependency lists used for cross-queue waits in execute().
        m_dependenciesByPass.assign(m_passes.size(), {});
        std::vector<ResourceHazardState> hazardByResource(m_resources.size());

        auto add_dependency = [this](size_t passIndex, size_t dependsOn)
            {
                if (passIndex == dependsOn)
                {
                    return;
                }

                std::vector<size_t>& deps = m_dependenciesByPass[passIndex];
                if (std::find(deps.begin(), deps.end(), dependsOn) == deps.end())
                {
                    deps.push_back(dependsOn);
                }
            };

        for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            for (const ResourceAccess& access : m_passes[passIndex].accesses)
            {
                ResourceHazardState& hazard = hazardByResource[access.handle.index];

                if (access.type == ResourceAccessType::Read)
                {
                    if (hazard.lastWriter >= 0)
                    {
                        add_dependency(passIndex, static_cast<size_t>(hazard.lastWriter));
                    }

                    if (std::find(hazard.lastReaders.begin(), hazard.lastReaders.end(), static_cast<int32_t>(passIndex)) == hazard.lastReaders.end())
                    {
                        hazard.lastReaders.push_back(static_cast<int32_t>(passIndex));
                    }
                    continue;
                }

                if (hazard.lastWriter >= 0)
                {
                    add_dependency(passIndex, static_cast<size_t>(hazard.lastWriter));
                }
                for (int32_t readerPass : hazard.lastReaders)
                {
                    add_dependency(passIndex, static_cast<size_t>(readerPass));
                }

                hazard.lastReaders.clear();
                hazard.lastWriter = static_cast<int32_t>(passIndex);
            }
        }
    }

    void FrameGraph::build_resource_barriers()
    {
        // Walk required states and emit barriers before/after each pass.
        // finalState covers cases where a pass must leave a resource in a known state.
        m_barriersByPass.clear();
        std::vector<ResourceState> trackedState = initial_resource_states();

        for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            for (const ResourceAccess& access : m_passes[passIndex].accesses)
            {
                ResourceState& current = trackedState[access.handle.index];
                if (current != access.requiredState)
                {
                    m_barriersByPass[passIndex].push_back(BarrierEvent{
                        access.handle,
                        current,
                        access.requiredState,
                        true
                        });
                    current = access.requiredState;
                }

                if (access.type == ResourceAccessType::Write)
                {
                    if (access.hasFinalState && current != access.finalState)
                    {
                        m_barriersByPass[passIndex].push_back(BarrierEvent{
                            access.handle,
                            current,
                            access.finalState,
                            false
                            });
                        current = access.finalState;
                    }
                    else if (access.hasFinalState)
                    {
                        current = access.finalState;
                    }
                }
            }
        }
    }

    Result FrameGraph::build()
    {
        try
        {
            // build() reconstructs the graph from declarations every time.
            // Order: setup() -> materialize/resolve -> sort -> dependency/barrier analysis.
            m_resources.clear();
            m_resourceByNameId.clear();
            m_dependenciesByPass.clear();
            m_barriersByPass.clear();
            m_pipelineDecls.clear();
            m_rootSignatureDecls.clear();
            m_shaderDecls.clear();

            for (CompiledPass& compiledPass : m_passes)
            {
                compiledPass.accesses.clear();
                compiledPass.pipelineDecl = {};
                compiledPass.rootSignatureDecl = {};
                compiledPass.shaderDecls.clear();
                compiledPass.descriptorDecls.clear();

                FrameGraphBuilder builder(
                    *this,
                    compiledPass.accesses,
                    compiledPass.pipelineDecl,
                    compiledPass.rootSignatureDecl,
                    compiledPass.shaderDecls,
                    compiledPass.descriptorDecls);
                compiledPass.pass->setup(builder);
            }

            const Result materializeResult = materialize_declared_resources();
            if (!materializeResult)
            {
                return materializeResult;
            }

            const Result resolvePipelineResult = resolve_pipeline_artifacts();
            if (!resolvePipelineResult)
            {
                return resolvePipelineResult;
            }

            std::vector<size_t> sortedOrder;
            const Result sortResult = topological_sort_by_resource_dependencies(sortedOrder);
            if (!sortResult)
            {
                return sortResult;
            }

            reorder_compiled_passes(sortedOrder);
            build_execution_dependencies();
            build_resource_barriers();

            Core::Logger::log(Core::LogSink::debugConsole, "FrameGraph build succeeded with {} passes and {} resources\n", m_passes.size(), m_resources.size());
            for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "  {}: {} (Queue={})\n", passIndex, m_passes[passIndex].pass->name(), command_list_type_to_string(m_passes[passIndex].pass->queue_type()));
            }

            m_isBuilt = true;
            m_isDirty = false;
            return Result::ok();
        }
        catch (const std::exception& e)
        {
            Core::Logger::log(Core::LogSink::debugConsole, "[FrameGraph] build failed: {}\n", e.what());
            m_isBuilt = false;
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "FrameGraph build failed");
        }
    }

    Result FrameGraph::execute(uint64_t frameNo, uint32_t index, ICommandPool& commandPool, IQueuePool& queuePool)
    {
        if (!m_isBuilt || m_isDirty)
        {
            const Result buildResult = build();
            if (!buildResult)
            {
                return buildResult;
            }
        }

        QueueContextLease graphicsQueue{};
        QueueContextLease computeQueue{};
        QueueContextLease copyQueue{};
        Result acquireResult = queuePool.acquire_queue(CommandListType::Graphics, graphicsQueue);
        if (!acquireResult)
        {
            return acquireResult;
        }
        acquireResult = queuePool.acquire_queue(CommandListType::Compute, computeQueue);
        if (!acquireResult)
        {
            return acquireResult;
        }
        acquireResult = queuePool.acquire_queue(CommandListType::Copy, copyQueue);
        if (!acquireResult)
        {
            return acquireResult;
        }

        std::vector<PassExecutionInfo> passExecution(m_passes.size());
        std::vector<ResourceState> runtimeTrackedState = initial_resource_states();
        ExecutionSummary summary{};
        summary.frameNo = frameNo;
        summary.bufferIndex = index;
        summary.passCount = m_passes.size();

        auto queue_for = [&graphicsQueue, &computeQueue, &copyQueue](CommandListType type) -> IQueueContext&
            {
                switch (type)
                {
                case CommandListType::Graphics:
                    return *graphicsQueue;
                case CommandListType::Compute:
                    return *computeQueue;
                case CommandListType::Copy:
                    return *copyQueue;
                default:
                    return *graphicsQueue;
                }
            };

        // Convert the precomputed barrier plan into backend barriers for the current frame index.
        auto emit_barriers = [this, index](ICommandContext& commandContext, size_t passIndex, bool beforePass, std::vector<ResourceState>& trackedState) -> Result
            {
                const auto barrierIt = m_barriersByPass.find(passIndex);
                if (barrierIt == m_barriersByPass.end())
                {
                    return Result::ok();
                }

                std::vector<ResourceBarrierDesc> barriers;
                for (const BarrierEvent& barrierEvent : barrierIt->second)
                {
                    if (barrierEvent.beforePass != beforePass)
                    {
                        continue;
                    }

                    ResourceBarrierDesc barrierDesc{};
                    const Result buildBarrierResult = make_barrier_desc(barrierEvent, index, barrierDesc);
                    if (!buildBarrierResult)
                    {
                        return buildBarrierResult;
                    }
                    barriers.push_back(barrierDesc);
                    trackedState[barrierEvent.handle.index] = barrierEvent.after;
                }

                if (barriers.empty())
                {
                    return Result::ok();
                }

                return commandContext.resource_barriers(barriers.data(), barriers.size());
            };

        for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            // Select the queue for this pass and inject waits when a dependency ran on another queue.
            const CompiledPass& compiledPass = m_passes[passIndex];
            const CommandListType queueType = compiledPass.pass->queue_type();
            IQueueContext& queue = queue_for(queueType);

            if (passIndex < m_dependenciesByPass.size())
            {
                for (size_t depPassIndex : m_dependenciesByPass[passIndex])
                {
                    if (depPassIndex >= passExecution.size())
                    {
                        continue;
                    }

                    const PassExecutionInfo& depInfo = passExecution[depPassIndex];
                    if (!depInfo.submitted || depInfo.queueType == queueType)
                    {
                        continue;
                    }

                    const Result waitResult = queue.wait(depInfo.signalPoint);
                    if (!waitResult)
                    {
                        return waitResult;
                    }
                    ++summary.waitCount;
                }
            }

            CommandContextLease commandContext{};
            const Result acquireContextResult = commandPool.acquire_context(queueType, commandContext);
            if (!acquireContextResult)
            {
                return acquireContextResult;
            }

            const Result resetResult = commandContext->reset();
            if (!resetResult)
            {
                return resetResult;
            }

            const Result preBarrierResult = emit_barriers(*commandContext, passIndex, true, runtimeTrackedState);
            if (!preBarrierResult)
            {
                return preBarrierResult;
            }

            commandContext->begin_event(compiledPass.pass->name());
            std::vector<ShaderBlobHandle> shaderHandles;
            shaderHandles.reserve(compiledPass.shaderDecls.size());
            for (const ShaderBindingDecl& shaderDecl : compiledPass.shaderDecls)
            {
                shaderHandles.push_back(shaderDecl.handle);
            }

            std::vector<FrameGraphContext::ResolvedDescriptorBinding> descriptorBindings;
            descriptorBindings.reserve(compiledPass.descriptorDecls.size());
            for (const DescriptorBindingDecl& descriptorDecl : compiledPass.descriptorDecls)
            {
                if (descriptorDecl.hasBufferIndex && descriptorDecl.bufferIndex != index)
                {
                    continue;
                }

                FrameGraphContext::ResolvedDescriptorBinding resolvedBinding{};
                const Result resolveDescriptorResult = resolve_descriptor_binding(descriptorDecl, index, resolvedBinding);
                if (!resolveDescriptorResult)
                {
                    return resolveDescriptorResult;
                }
                descriptorBindings.push_back(resolvedBinding);
            }
            summary.descriptorCount += descriptorBindings.size();
            summary.shaderCount += shaderHandles.size();
            if (compiledPass.pipelineDecl.handle.valid())
            {
                summary.pipelineHandle = compiledPass.pipelineDecl.handle;
            }
            if (compiledPass.rootSignatureDecl.handle.valid())
            {
                summary.rootSignatureHandle = compiledPass.rootSignatureDecl.handle;
            }

            // Passes only see resolved runtime data here.
            // They do not need to know about logical names or manager lookups.
            FrameGraphContext context(
                *this,
                frameNo,
                index,
                *commandContext,
                compiledPass.pipelineDecl.handle,
                compiledPass.rootSignatureDecl.handle,
                std::move(shaderHandles),
                std::move(descriptorBindings));
            compiledPass.pass->execute(context);
            commandContext->end_event();

            const Result postBarrierResult = emit_barriers(*commandContext, passIndex, false, runtimeTrackedState);
            if (!postBarrierResult)
            {
                return postBarrierResult;
            }

            const Result closeResult = commandContext->close();
            if (!closeResult)
            {
                return closeResult;
            }

            const Result submitResult = queue.submit(*commandContext);
            if (!submitResult)
            {
                return submitResult;
            }

            QueueSyncPoint signalPoint{};
            const Result signalResult = queue.signal(signalPoint);
            if (!signalResult)
            {
                return signalResult;
            }

            passExecution[passIndex] = PassExecutionInfo{ queueType, signalPoint, true };
        }

        size_t barrierCount = 0;
        for (const auto& [passIndex, barriers] : m_barriersByPass)
        {
            (void)passIndex;
            barrierCount += barriers.size();
        }
        summary.barrierCount = barrierCount;
        m_lastExecutionSummary = summary;

        return Result::ok();
    }

    BufferHandle FrameGraphBuilder::create_buffer(std::string_view name, const BufferDesc& desc)
    {
        return m_frameGraph.create_buffer(name, desc);
    }

    TextureHandle FrameGraphBuilder::create_texture(std::string_view name, const TextureDesc& desc)
    {
        return m_frameGraph.create_texture(name, desc);
    }

    BufferHandle FrameGraphBuilder::import_buffer(std::string_view name, const BufferDesc& desc, ResourceState initialState)
    {
        return m_frameGraph.import_buffer(name, desc, initialState);
    }

    TextureHandle FrameGraphBuilder::import_texture(std::string_view name, const TextureDesc& desc, ResourceState initialState)
    {
        return m_frameGraph.import_texture(name, desc, initialState);
    }

    BufferHandle FrameGraphBuilder::get_buffer(std::string_view name)
    {
        return m_frameGraph.get_buffer(name);
    }

    TextureHandle FrameGraphBuilder::get_texture(std::string_view name)
    {
        return m_frameGraph.get_texture(name);
    }

    void FrameGraphBuilder::read(BufferHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, ResourceState::ShaderResource });
    }

    void FrameGraphBuilder::read(TextureHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, ResourceState::ShaderResource });
    }

    void FrameGraphBuilder::write(BufferHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::UnorderedAccess });
    }

    void FrameGraphBuilder::write(TextureHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::UnorderedAccess });
    }

    void FrameGraphBuilder::write(BufferHandle handle, ResourceState finalState)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::UnorderedAccess, true, finalState });
    }

    void FrameGraphBuilder::write(TextureHandle handle, ResourceState finalState)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::UnorderedAccess, true, finalState });
    }

    void FrameGraphBuilder::render(TextureHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::RenderTarget });
    }

    void FrameGraphBuilder::render(TextureHandle handle, ResourceState finalState)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::RenderTarget, true, finalState });
    }

    void FrameGraphBuilder::cpy_src(BufferHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, ResourceState::CopySource });
    }

    void FrameGraphBuilder::cpy_src(TextureHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, ResourceState::CopySource });
    }

    void FrameGraphBuilder::cpy_dst(BufferHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::CopyDest });
    }

    void FrameGraphBuilder::cpy_dst(TextureHandle handle)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::CopyDest });
    }

    void FrameGraphBuilder::cpy_dst(BufferHandle handle, ResourceState finalState)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::CopyDest, true, finalState });
    }

    void FrameGraphBuilder::cpy_dst(TextureHandle handle, ResourceState finalState)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::CopyDest, true, finalState });
    }

    void FrameGraphBuilder::use_pipeline(const PipelineDesc& desc)
    {
        m_frameGraph.declare_pipeline(m_pipelineDecl, desc);
    }

    void FrameGraphBuilder::get_pipeline(std::string_view name)
    {
        m_frameGraph.reference_pipeline(m_pipelineDecl, name);
    }

    void FrameGraphBuilder::use_root_signature(const RootSignatureDesc& desc)
    {
        m_frameGraph.declare_root_signature(m_rootSignatureDecl, desc);
    }

    void FrameGraphBuilder::get_root_signature(std::string_view name)
    {
        m_frameGraph.reference_root_signature(m_rootSignatureDecl, name);
    }

    void FrameGraphBuilder::compile_shader(const ShaderCompileDesc& desc)
    {
        m_frameGraph.declare_shader(m_shaderDecls, desc);
    }

    void FrameGraphBuilder::get_shader(std::string_view name)
    {
        m_frameGraph.reference_shader(m_shaderDecls, name);
    }

    void FrameGraphBuilder::bind_cbv(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::CBV,
            visibility,
            shaderRegister,
            false,
            0,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_cbv_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::CBV,
            visibility,
            shaderRegister,
            true,
            bufferIndex,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_srv(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::SRV,
            visibility,
            shaderRegister,
            false,
            0,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_srv(uint32_t shaderRegister, TextureHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::SRV,
            visibility,
            shaderRegister,
            false,
            0,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_srv_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::SRV,
            visibility,
            shaderRegister,
            true,
            bufferIndex,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_srv_at(uint32_t shaderRegister, uint32_t bufferIndex, TextureHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::SRV,
            visibility,
            shaderRegister,
            true,
            bufferIndex,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_uav(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::UAV,
            visibility,
            shaderRegister,
            false,
            0,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_uav(uint32_t shaderRegister, TextureHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::UAV,
            visibility,
            shaderRegister,
            false,
            0,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_uav_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::UAV,
            visibility,
            shaderRegister,
            true,
            bufferIndex,
            to_resource_ref(handle)
            });
    }

    void FrameGraphBuilder::bind_uav_at(uint32_t shaderRegister, uint32_t bufferIndex, TextureHandle handle, ShaderVisibility visibility)
    {
        m_frameGraph.validate_resource_handle(handle);
        m_frameGraph.declare_descriptor(m_descriptorDecls, DescriptorBindingDecl{
            RootParameterType::UAV,
            visibility,
            shaderRegister,
            true,
            bufferIndex,
            to_resource_ref(handle)
            });
    }

    Result FrameGraphContext::resolve_buffer(BufferHandle logicalHandle, BufferHandle& outHandle) const
    {
        return m_frameGraph.resolve_buffer(logicalHandle, m_bufferIndex, outHandle);
    }

    Result FrameGraphContext::resolve_texture(TextureHandle logicalHandle, TextureHandle& outHandle) const
    {
        return m_frameGraph.resolve_texture(logicalHandle, outHandle);
    }
} // namespace Cue::GraphicsCore
