#include "FrameGraph.h"

namespace Cue::GraphicsCore
{
    namespace
    {
        [[nodiscard]] Result make_default_buffer_view_desc(RootParameterType parameterType, BufferViewDesc& outDesc)
        {
            // 1) frame graph の bind_* 宣言を whole-resource view 既定表現へ正規化
            outDesc = {};
            switch (parameterType)
            {
            case RootParameterType::CBV:
                outDesc.type = ViewType::ConstantBuffer;
                return Result::ok();
            case RootParameterType::SRV:
                outDesc.type = ViewType::ShaderResource;
                return Result::ok();
            case RootParameterType::UAV:
                outDesc.type = ViewType::UnorderedAccess;
                return Result::ok();
            default:
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Unsupported root parameter type for buffer view");
            }
        }

        [[nodiscard]] Result make_default_texture_view_desc(RootParameterType parameterType, TextureViewDesc& outDesc)
        {
            // 1) texture descriptor も whole-resource view 既定値へ正規化
            outDesc = {};
            switch (parameterType)
            {
            case RootParameterType::SRV:
                outDesc.type = ViewType::ShaderResource;
                outDesc.mipLevels = 0;
                return Result::ok();
            case RootParameterType::UAV:
                outDesc.type = ViewType::UnorderedAccess;
                return Result::ok();
            default:
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Unsupported root parameter type for texture view");
            }
        }
    }

    Result FrameGraph::add_pass(std::unique_ptr<FrameGraphPass> pass)
    {
        // 1) null pass を拒否
        if (pass == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "FrameGraph pass is null");
        }

        // 2) pass を保持して dirty 状態へ更新
        m_passes.push_back(CompiledPass{ std::move(pass), {} });
        m_isDirty = true;
        return Result::ok();
    }

    template <class HandleT>
    HandleT FrameGraph::declare_resource(std::string_view name, ResourceKind expectedKind)
    {
        // 1) 論理名の重複を検査
        const ResourceNameId nameId = Core::fnv1a64(name);
        const auto found = m_resourceByNameId.find(nameId);
        if (found != m_resourceByNameId.end())
        {
            throw std::runtime_error("Duplicated logical resource name: " + std::string(name));
        }

        // 2) resource 配列上限を検査
        if (m_resources.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
        {
            throw std::overflow_error("FrameGraph resource index overflow");
        }

        // 3) 既定設定付き論理 resource を登録
        const uint32_t index = static_cast<uint32_t>(m_resources.size());

        const ResourceRef ref{ expectedKind, index, 1 };
        LogicalResource resource{
            ref,
            nameId,
            std::string(name),
            ResourceState::Common
        };
        resource.bufferDesc.name = resource.debugName;
        resource.bufferDesc.bufferingCount = 1;
        resource.textureDesc.name = resource.debugName;
        resource.textureDesc.bufferingCount = 1;

        m_resources.push_back(std::move(resource));
        m_resourceByNameId.emplace(nameId, ref);
        m_isDirty = true;
        return HandleT{ index, 1 };
    }

    template <class HandleT>
    HandleT FrameGraph::find_resource(std::string_view name, ResourceKind expectedKind) const
    {
        // 1) 論理名から resource 参照を検索
        const ResourceNameId nameId = Core::fnv1a64(name);
        const auto found = m_resourceByNameId.find(nameId);
        if (found == m_resourceByNameId.end())
        {
            throw std::runtime_error("Logical resource is not declared: " + std::string(name));
        }

        // 2) kind を検証して handle 化
        validate_resource_ref(found->second, expectedKind);
        return HandleT{ found->second.index, found->second.generation };
    }

    uint32_t FrameGraph::resolve_buffering_count(uint32_t requestedCount) const noexcept
    {
        // 1) 0 指定時は graph 既定値を採用
        if (requestedCount == 0)
        {
            return m_defaultBufferingCount;
        }

        // 2) 1 以上へ正規化
        return (std::max)(requestedCount, 1u);
    }

    BufferHandle FrameGraph::create_buffer(std::string_view name, const BufferDesc& desc)
    {
        // 1) 表示名を確定して論理 buffer を宣言
        const std::string_view resolvedName = desc.name.empty() ? name : desc.name;
        const BufferHandle handle = declare_resource<BufferHandle>(resolvedName, ResourceKind::Buffer);

        // 2) desc と instance 情報を保存
        LogicalResource& resource = m_resources[handle.index];
        resource.bufferDesc = desc;
        resource.bufferDesc.name = resource.debugName;
        resource.bufferDesc.bufferingCount = resolve_buffering_count(desc.bufferingCount);
        resource.instanceCount = resource.bufferDesc.bufferingCount;
        resource.instanceSource = desc.instanceSource;

        return handle;
    }

    TextureHandle FrameGraph::create_texture(std::string_view name, const TextureDesc& desc)
    {
        // 1) 表示名を確定して論理 texture を宣言
        const std::string_view resolvedName = desc.name.empty() ? name : desc.name;
        const TextureHandle handle = declare_resource<TextureHandle>(resolvedName, ResourceKind::Texture);

        // 2) desc と instance 情報を保存
        LogicalResource& resource = m_resources[handle.index];
        resource.textureDesc = desc;
        resource.textureDesc.name = resource.debugName;
        resource.textureDesc.bufferingCount = resolve_buffering_count(desc.bufferingCount);
        resource.instanceCount = resource.textureDesc.bufferingCount;
        resource.instanceSource = desc.instanceSource;
        resource.initialState = desc.initialState;

        return handle;
    }

    BufferHandle FrameGraph::import_buffer(std::string_view name, const BufferDesc& desc, ResourceState initialState)
    {
        // 1) 表示名を確定して外部 buffer を宣言
        const std::string_view resolvedName = desc.name.empty() ? name : desc.name;
        const BufferHandle handle = declare_resource<BufferHandle>(resolvedName, ResourceKind::Buffer);

        // 2) 外部 resource 設定を保存
        LogicalResource& resource = m_resources[handle.index];
        resource.bufferDesc = desc;
        resource.bufferDesc.name = resource.debugName;
        resource.bufferDesc.bufferingCount = resolve_buffering_count(desc.bufferingCount);
        resource.initialState = initialState;
        resource.external = true;
        resource.instanceCount = resource.bufferDesc.bufferingCount;
        resource.instanceSource = desc.instanceSource;

        return handle;
    }

    TextureHandle FrameGraph::import_texture(std::string_view name, const TextureDesc& desc, ResourceState initialState)
    {
        // 1) 表示名を確定して外部 texture を宣言
        const std::string_view resolvedName = desc.name.empty() ? name : desc.name;
        const TextureHandle handle = declare_resource<TextureHandle>(resolvedName, ResourceKind::Texture);

        // 2) 外部 resource 設定を保存
        LogicalResource& resource = m_resources[handle.index];
        resource.textureDesc = desc;
        resource.textureDesc.name = resource.debugName;
        resource.textureDesc.bufferingCount = resolve_buffering_count(desc.bufferingCount);
        resource.initialState = initialState;
        resource.external = true;
        resource.instanceCount = resource.textureDesc.bufferingCount;
        resource.instanceSource = desc.instanceSource;

        return handle;
    }

    BufferHandle FrameGraph::get_buffer(std::string_view name)
    {
        // 1) 論理名から buffer handle を取得
        return find_resource<BufferHandle>(name, ResourceKind::Buffer);
    }

    TextureHandle FrameGraph::get_texture(std::string_view name)
    {
        // 1) 論理名から texture handle を取得
        return find_resource<TextureHandle>(name, ResourceKind::Texture);
    }

    void FrameGraph::validate_resource_handle(BufferHandle handle) const
    {
        // 1) buffer handle を resource 参照として検証
        validate_resource_ref(to_resource_ref(handle), ResourceKind::Buffer);
    }

    void FrameGraph::validate_resource_handle(TextureHandle handle) const
    {
        // 1) texture handle を resource 参照として検証
        validate_resource_ref(to_resource_ref(handle), ResourceKind::Texture);
    }

    void FrameGraph::validate_resource_ref(ResourceRef ref, ResourceKind expectedKind) const
    {
        // 1) handle 有効性を検証
        if (!ref.valid())
        {
            throw std::runtime_error("FrameGraph resource handle is invalid");
        }
        // 2) kind と index 範囲を検証
        if (ref.kind != expectedKind)
        {
            throw std::runtime_error("FrameGraph resource kind mismatch");
        }
        if (ref.index >= m_resources.size())
        {
            throw std::runtime_error("FrameGraph resource handle is out of range");
        }
        // 3) generation 一致を検証
        if (m_resources[ref.index].ref.generation != ref.generation)
        {
            throw std::runtime_error("FrameGraph resource generation mismatch");
        }
    }

    const FrameGraph::LogicalResource& FrameGraph::get_logical_resource(ResourceRef ref) const
    {
        // 1) resource 参照を検証して論理 resource を返す
        validate_resource_ref(ref, ref.kind);
        return m_resources[ref.index];
    }

    uint32_t FrameGraph::resolve_resource_instance_index(const LogicalResource& resource, uint32_t frameResourceIndex, uint32_t swapchainImageIndex) const noexcept
    {
        // 1) 単一実体なら index 0 固定
        if (resource.instanceCount <= 1)
        {
            return 0;
        }

        // 2) instance source に応じて実体 index を選択
        switch (resource.instanceSource)
        {
        case ResourceInstanceSource::Fixed:
            return 0;
        case ResourceInstanceSource::FrameResourceIndex:
            return frameResourceIndex % resource.instanceCount;
        case ResourceInstanceSource::SwapchainImageIndex:
            return swapchainImageIndex % resource.instanceCount;
        default:
            return 0;
        }
    }

    Result FrameGraph::resolve_buffer(BufferHandle logicalHandle, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, BufferHandle& outHandle) const
    {
        try
        {
            // 1) 論理 resource と instance index を解決
            const LogicalResource& resource = get_logical_resource(to_resource_ref(logicalHandle));
            const uint32_t instanceIndex = resolve_resource_instance_index(resource, frameResourceIndex, swapchainImageIndex);
            // 2) manager から物理 buffer を取得
            return m_bufferManager.get_buffer(resource.nameId, instanceIndex, outHandle);
        }
        catch (const std::exception& e)
        {
            // 1) 例外内容をログ化して失敗へ変換
            Core::Logger::log(Core::LogSink::debugConsole, "[FrameGraph] resolve_buffer failed: {}\n", e.what());
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Failed to resolve logical buffer");
        }
    }

    Result FrameGraph::resolve_texture(TextureHandle logicalHandle, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, TextureHandle& outHandle) const
    {
        try
        {
            // 1) 論理 resource と instance index を解決
            const LogicalResource& resource = get_logical_resource(to_resource_ref(logicalHandle));
            const uint32_t instanceIndex = resolve_resource_instance_index(resource, frameResourceIndex, swapchainImageIndex);
            // 2) manager から物理 texture を取得
            return m_textureManager.get_texture(resource.nameId, instanceIndex, outHandle);
        }
        catch (const std::exception& e)
        {
            // 1) 例外内容をログ化して失敗へ変換
            Core::Logger::log(Core::LogSink::debugConsole, "[FrameGraph] resolve_texture failed: {}\n", e.what());
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Failed to resolve logical texture");
        }
    }

    Result FrameGraph::resolve_descriptor_binding(const DescriptorBindingDecl& binding, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, FrameGraphContext::ResolvedDescriptorBinding& outBinding) const
    {
        // 1) 対象フレームの buffer index binding だけを追加
        if (binding.hasBufferIndex && binding.bufferIndex != frameResourceIndex)
        {
            return Result::fail(Facility::Graphics, Code::NotFound, Severity::Info, 0, "Descriptor binding is not active for this buffer index");
        }

        outBinding.type = binding.type;
        outBinding.visibility = binding.visibility;
        outBinding.shaderRegister = binding.shaderRegister;

        if (binding.resource.kind == ResourceKind::Buffer)
        {
            // 2) buffer 実体と既定 view を解決
            BufferHandle physicalHandle{};
            const Result result = resolve_buffer(BufferHandle{ binding.resource.index, binding.resource.generation }, frameResourceIndex, swapchainImageIndex, physicalHandle);
            if (!result)
            {
                return result;
            }

            BufferViewDesc viewDesc{};
            const Result viewDescResult = make_default_buffer_view_desc(binding.type, viewDesc);
            if (!viewDescResult)
            {
                return viewDescResult;
            }

            outBinding.resourceKind = ResourceKind::Buffer;
            const Result viewResult = m_viewManager.get_buffer_view(physicalHandle, viewDesc, outBinding.viewHandle);
            if (!viewResult)
            {
                return viewResult;
            }

            // 3) buffer view から descriptor handle を取得
            return m_viewManager.get_descriptor_handle(outBinding.viewHandle, outBinding.descriptorHandle);
        }

        // 4) texture 実体と既定 view を解決
        if (binding.resource.kind != ResourceKind::Texture)
        {
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Unsupported descriptor resource kind");
        }

        TextureHandle physicalHandle{};
        const Result result = resolve_texture(TextureHandle{ binding.resource.index, binding.resource.generation }, frameResourceIndex, swapchainImageIndex, physicalHandle);
        if (!result)
        {
            return result;
        }

        TextureViewDesc viewDesc{};
        const Result viewDescResult = make_default_texture_view_desc(binding.type, viewDesc);
        if (!viewDescResult)
        {
            return viewDescResult;
        }

        outBinding.resourceKind = ResourceKind::Texture;
        const Result viewResult = m_viewManager.get_texture_view(physicalHandle, viewDesc, outBinding.viewHandle);
        if (!viewResult)
        {
            return viewResult;
        }

        // 5) texture view から descriptor handle を取得
        return m_viewManager.get_descriptor_handle(outBinding.viewHandle, outBinding.descriptorHandle);
    }

    Result FrameGraph::resolve_render_target_views(const std::vector<ResourceAccess>& accesses, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, std::vector<ViewHandle>& outRenderTargetViews, ViewHandle& outDepthStencilView) const
    {
        // 1) graphics pass の output attachment を access 宣言から導出
        outRenderTargetViews.clear();
        outDepthStencilView = {};

        for (const ResourceAccess& access : accesses)
        {
            // 2) texture かつ render/depth access だけを対象にする
            if (access.handle.kind != ResourceKind::Texture)
            {
                continue;
            }

            if (access.requiredState != ResourceState::RenderTarget && access.requiredState != ResourceState::DepthWrite)
            {
                continue;
            }

            // 3) 実体 texture から attachment view を取得
            TextureHandle physicalHandle{};
            const Result resolveResult = resolve_texture(TextureHandle{ access.handle.index, access.handle.generation }, frameResourceIndex, swapchainImageIndex, physicalHandle);
            if (!resolveResult)
            {
                return resolveResult;
            }

            TextureViewDesc viewDesc{};
            viewDesc.type = access.requiredState == ResourceState::RenderTarget ? ViewType::RenderTarget : ViewType::DepthStencil;

            ViewHandle viewHandle{};
            const Result viewResult = m_viewManager.get_texture_view(physicalHandle, viewDesc, viewHandle);
            if (!viewResult)
            {
                return viewResult;
            }

            if (access.requiredState == ResourceState::RenderTarget)
            {
                // 4) render target view の重複登録を避ける
                const auto found = std::find(outRenderTargetViews.begin(), outRenderTargetViews.end(), viewHandle);
                if (found == outRenderTargetViews.end())
                {
                    outRenderTargetViews.push_back(viewHandle);
                }
                continue;
            }

            // 5) depth view は 1 つだけ許可する
            if (outDepthStencilView.valid() && !(outDepthStencilView == viewHandle))
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Multiple depth stencil outputs are not supported");
            }

            outDepthStencilView = viewHandle;
        }

        return Result::ok();
    }

    Result FrameGraph::make_barrier_desc(const BarrierEvent& event, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, ResourceBarrierDesc& outBarrier) const
    {
        // 1) 前後 state を barrier へ転記
        outBarrier.before = event.before;
        outBarrier.after = event.after;

        if (event.handle.kind == ResourceKind::Buffer)
        {
            // 2) buffer barrier 用の物理 handle を解決
            BufferHandle physicalHandle{};
            const Result result = resolve_buffer(BufferHandle{ event.handle.index, event.handle.generation }, frameResourceIndex, swapchainImageIndex, physicalHandle);
            if (!result)
            {
                return result;
            }

            outBarrier.kind = ResourceKind::Buffer;
            outBarrier.index = physicalHandle.index;
            outBarrier.generation = physicalHandle.generation;
            return Result::ok();
        }

        // 3) texture barrier 用の物理 handle を解決
        TextureHandle physicalHandle{};
        const Result result = resolve_texture(TextureHandle{ event.handle.index, event.handle.generation }, frameResourceIndex, swapchainImageIndex, physicalHandle);
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
        // 1) 論理 resource 初期 state を配列へ展開
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
        // 1) 論理 resource を manager 所有の backend resource へ対応付け
        for (LogicalResource& resource : m_resources)
        {
            if (resource.external)
            {
                // 2) external resource は manager 側の実体数を優先
                uint32_t actualInstanceCount = 0;
                Result instanceCountResult = Result::ok();
                if (resource.ref.kind == ResourceKind::Buffer)
                {
                    instanceCountResult = m_bufferManager.get_buffer_instance_count(resource.nameId, actualInstanceCount);
                }
                else
                {
                    instanceCountResult = m_textureManager.get_texture_instance_count(resource.nameId, actualInstanceCount);
                }

                if (!instanceCountResult)
                {
                    // 3) 未登録時は宣言側 instance count を暫定採用
                    resource.instanceCount = (std::max)(resource.instanceCount, 1u);
                    continue;
                }

                resource.instanceCount = (std::max)(actualInstanceCount, 1u);
                continue;
            }

            if (resource.ref.kind == ResourceKind::Buffer)
            {
                // 4) 内部 buffer は既存実体を再利用し未作成なら生成
                BufferHandle existingHandle{};
                const Result getResult = m_bufferManager.get_buffer(resource.nameId, 0, existingHandle);
                if (getResult)
                {
                    continue;
                }

                BufferHandle createdHandle{};
                BufferDesc resolvedDesc = resource.bufferDesc;
                resolvedDesc.name = resource.debugName;
                const Result createResult = m_bufferManager.create_buffer(resolvedDesc, createdHandle);
                if (!createResult)
                {
                    return createResult;
                }
                continue;
            }

            // 5) 内部 texture は画面サイズ補完後に未作成分だけ生成
            TextureHandle existingTexture{};
            const Result getResult = m_textureManager.get_texture(resource.nameId, 0, existingTexture);
            if (getResult)
            {
                continue;
            }

            TextureHandle createdHandle{};
            TextureDesc resolvedDesc = resource.textureDesc;
            resolvedDesc.name = resource.debugName;
            if (resolvedDesc.width == 0)
            {
                resolvedDesc.width = m_screenWidth;
            }
            if (resolvedDesc.height == 0)
            {
                resolvedDesc.height = m_screenHeight;
            }

            const Result createResult = m_textureManager.create_texture(resolvedDesc, createdHandle);
            if (!createResult)
            {
                return createResult;
            }
        }

        return Result::ok();
    }

    void FrameGraph::declare_pipeline(PipelineBindingDecl& outDecl, const GraphicsPipelineStateDesc& desc)
    {
        // 1) 多重宣言と空名を拒否
        if (outDecl.declared)
        {
            throw std::runtime_error("Pipeline is already declared for this pass");
        }
        if (desc.name.empty())
        {
            throw std::runtime_error("Pipeline logical name is empty");
        }

        const ResourceNameId nameId = Core::fnv1a64(desc.name);
        if (m_pipelineDecls.contains(nameId))
        {
            throw std::runtime_error("Duplicated pipeline logical name: " + std::string(desc.name));
        }

        // 2) pass 側宣言と全体宣言テーブルを同期
        outDecl.declared = true;
        outDecl.nameId = nameId;
        outDecl.debugName = std::string(desc.name);
        outDecl.desc = desc;
        outDecl.desc.name = outDecl.debugName;
        m_pipelineDecls.emplace(nameId, outDecl);
    }

    void FrameGraph::declare_root_signature(RootSignatureBindingDecl& outDecl, const RootSignatureDesc& desc)
    {
        // 1) 多重宣言と空名を拒否
        if (outDecl.declared)
        {
            throw std::runtime_error("Root signature is already declared for this pass");
        }
        if (desc.name.empty())
        {
            throw std::runtime_error("Root signature logical name is empty");
        }

        const ResourceNameId nameId = Core::fnv1a64(desc.name);
        if (m_rootSignatureDecls.contains(nameId))
        {
            throw std::runtime_error("Duplicated root signature logical name: " + std::string(desc.name));
        }

        // 2) pass 側宣言と全体宣言テーブルを同期
        outDecl.declared = true;
        outDecl.nameId = nameId;
        outDecl.debugName = std::string(desc.name);
        outDecl.desc = desc;
        outDecl.desc.name = outDecl.debugName;
        m_rootSignatureDecls.emplace(nameId, outDecl);
    }

    void FrameGraph::declare_shader(std::vector<ShaderBindingDecl>& outDecls, const ShaderCompileDesc& desc)
    {
        // 1) 空名と重複名を拒否
        if (desc.name.empty())
        {
            throw std::runtime_error("Shader logical name is empty");
        }

        const ResourceNameId nameId = Core::fnv1a64(desc.name);
        if (m_shaderDecls.contains(nameId))
        {
            throw std::runtime_error("Duplicated shader logical name: " + std::string(desc.name));
        }

        // 2) pass 側宣言と全体宣言テーブルへ追加
        const ShaderBindingDecl decl{ nameId, desc, {} };
        outDecls.push_back(decl);
        m_shaderDecls.emplace(nameId, decl);
    }

    void FrameGraph::reference_pipeline(PipelineBindingDecl& outDecl, std::string_view name)
    {
        // 1) 多重参照を拒否して全体宣言から検索
        if (outDecl.declared)
        {
            throw std::runtime_error("Pipeline is already declared for this pass");
        }

        const ResourceNameId nameId = Core::fnv1a64(name);
        const auto it = m_pipelineDecls.find(nameId);
        if (it == m_pipelineDecls.end())
        {
            throw std::runtime_error("Pipeline logical name is not declared: " + std::string(name));
        }

        // 2) pass 側宣言へ既存設定をコピー
        outDecl = it->second;
    }

    void FrameGraph::reference_root_signature(RootSignatureBindingDecl& outDecl, std::string_view name)
    {
        // 1) 多重参照を拒否して全体宣言から検索
        if (outDecl.declared)
        {
            throw std::runtime_error("Root signature is already declared for this pass");
        }

        const ResourceNameId nameId = Core::fnv1a64(name);
        const auto it = m_rootSignatureDecls.find(nameId);
        if (it == m_rootSignatureDecls.end())
        {
            throw std::runtime_error("Root signature logical name is not declared: " + std::string(name));
        }

        // 2) pass 側宣言へ既存設定をコピー
        outDecl = it->second;
    }

    void FrameGraph::reference_shader(std::vector<ShaderBindingDecl>& outDecls, std::string_view name)
    {
        // 1) 全体宣言から shader を検索
        const ResourceNameId nameId = Core::fnv1a64(name);
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

        // 2) pass 側宣言へ既存 shader を追加
        outDecls.push_back(it->second);
    }

    void FrameGraph::declare_descriptor(std::vector<DescriptorBindingDecl>& outDecls, const DescriptorBindingDecl& desc)
    {
        // 1) register と visibility と buffer index の重複を検査
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

        // 2) pass 側 descriptor 宣言へ追加
        outDecls.push_back(desc);
    }

    Result FrameGraph::resolve_pipeline_artifacts()
    {
        // 1) root signature を name 単位で再利用
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

        // 2) shader も name 単位で再利用
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

        // 3) pass 宣言から pso 未解決 handle を補完
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

            if (!compiledPass.pipelineDecl.declared)
            {
                continue;
            }

            PipelineBindingDecl& pipelineDecl = m_pipelineDecls.at(compiledPass.pipelineDecl.nameId);
            if (!pipelineDecl.desc.rootSignatureHandle.valid() && compiledPass.rootSignatureDecl.handle.valid())
            {
                pipelineDecl.desc.rootSignatureHandle = compiledPass.rootSignatureDecl.handle;
            }

            for (const ShaderBindingDecl& shaderDecl : compiledPass.shaderDecls)
            {
                const ShaderBindingDecl& resolvedShaderDecl = m_shaderDecls.at(shaderDecl.nameId);
                if (!pipelineDecl.desc.vsHandle.valid() && resolvedShaderDecl.desc.targetProfile.starts_with("vs_"))
                {
                    pipelineDecl.desc.vsHandle = resolvedShaderDecl.handle;
                    continue;
                }

                if (!pipelineDecl.desc.psHandle.valid() && resolvedShaderDecl.desc.targetProfile.starts_with("ps_"))
                {
                    pipelineDecl.desc.psHandle = resolvedShaderDecl.handle;
                }
            }
        }

        // 4) 補完済み pso 設定から handle を確定
        for (auto& [nameId, pipelineDecl] : m_pipelineDecls)
        {
            if (!pipelineDecl.desc.rootSignatureHandle.valid() || !pipelineDecl.desc.vsHandle.valid() || !pipelineDecl.desc.psHandle.valid())
            {
                return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Error, 0, "Pipeline declaration is missing root signature or shader handles.");
            }

            Result result = m_pipelineManager.get_pipeline(nameId, pipelineDecl.handle);
            if (!result)
            {
                result = m_pipelineManager.create_graphics_pipeline(pipelineDecl.desc, pipelineDecl.handle);
                if (!result)
                {
                    return result;
                }
            }
        }

        // 5) compiled pass 側 handle を同期
        for (CompiledPass& compiledPass : m_passes)
        {
            if (compiledPass.pipelineDecl.declared)
            {
                compiledPass.pipelineDecl.handle = m_pipelineDecls.at(compiledPass.pipelineDecl.nameId).handle;
            }
        }

        return Result::ok();
    }

    Result FrameGraph::topological_sort_by_resource_dependencies(std::vector<size_t>& outSorted) const
    {
        // 1) resource hazard 追跡用の隣接行列と入次数を初期化
        const size_t passCount = m_passes.size();
        std::vector<std::unordered_set<size_t>> adjacency(passCount);
        std::vector<size_t> indegree(passCount, 0);
        std::vector<ResourceHazardState> hazardByResource(m_resources.size());

        // 2) 重複無しで依存 edge を追加する関数を用意
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

        // 3) read/write hazard から pass 依存 edge を構築
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

        // 4) 入次数 0 の pass から処理開始
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

        // 5) kahn 法で依存順を確定
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

        // 6) 未処理 node が残る場合は循環依存として失敗
        if (outSorted.size() != passCount)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "FrameGraph has cyclic pass dependencies");
        }

        return Result::ok();
    }

    void FrameGraph::reorder_compiled_passes(const std::vector<size_t>& sortedOrder)
    {
        // 1) 依存順どおりに compiled pass 配列を再構築
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
        // 1) execute() の cross queue wait 用依存配列を初期化
        m_dependenciesByPass.assign(m_passes.size(), {});
        std::vector<ResourceHazardState> hazardByResource(m_resources.size());

        // 2) 重複無しで依存先を追加する関数を用意
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

        // 3) read/write hazard から pass ごとの依存先を抽出
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
        // 1) pass ごとの barrier 蓄積先と追跡 state を初期化
        m_barriersByPass.clear();
        std::vector<ResourceState> trackedState = initial_resource_states();

        // 2) access 宣言から実行前後 barrier を生成
        for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            for (const ResourceAccess& access : m_passes[passIndex].accesses)
            {
                ResourceState& current = trackedState[access.handle.index];
                if (current != access.requiredState)
                {
                    // 3) 実行前に required state へ遷移
                    m_barriersByPass[passIndex].push_back(BarrierEvent{
                        access.handle,
                        current,
                        access.requiredState,
                        true });
                    current = access.requiredState;
                }

                if (access.type == ResourceAccessType::Write)
                {
                    if (access.hasFinalState && current != access.finalState)
                    {
                        // 4) write 完了後に指定 final state へ遷移
                        m_barriersByPass[passIndex].push_back(BarrierEvent{
                            access.handle,
                            current,
                            access.finalState,
                            false });
                        current = access.finalState;
                    }
                    else if (access.hasFinalState)
                    {
                        // 5) barrier 不要でも追跡 state は final state へ更新
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
            // 1) 前回 build 結果を破棄して宣言テーブルを初期化
            m_resources.clear();
            m_resourceByNameId.clear();
            m_dependenciesByPass.clear();
            m_barriersByPass.clear();
            m_pipelineDecls.clear();
            m_rootSignatureDecls.clear();
            m_shaderDecls.clear();

            // 2) 各 pass の setup() を再実行して resource と pipeline 宣言を収集
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

            // 3) 宣言済み resource を manager 実体へ対応付け
            const Result materializeResult = materialize_declared_resources();
            if (!materializeResult)
            {
                return materializeResult;
            }

            // 4) pipeline と root signature と shader の handle を解決
            const Result resolvePipelineResult = resolve_pipeline_artifacts();
            if (!resolvePipelineResult)
            {
                return resolvePipelineResult;
            }

            // 5) resource 依存から pass 順序を決定
            std::vector<size_t> sortedOrder;
            const Result sortResult = topological_sort_by_resource_dependencies(sortedOrder);
            if (!sortResult)
            {
                return sortResult;
            }

            // 6) 並び替え後の pass 群から実行依存と barrier を構築
            reorder_compiled_passes(sortedOrder);
            build_execution_dependencies();
            build_resource_barriers();

            // 7) build 完了状態を記録して実行可能にする
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
            // 1) 例外内容をログへ残して build 失敗へ変換
            Core::Logger::log(Core::LogSink::debugConsole, "[FrameGraph] build failed: {}\n", e.what());
            m_isBuilt = false;
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "FrameGraph build failed");
        }
    }

    Result FrameGraph::execute(uint64_t frameNo, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, ICommandPool& commandPool, IQueuePool& queuePool)
    {
        // 1) 未 build または dirty 状態なら実行前に再 build
        if (!m_isBuilt || m_isDirty)
        {
            const Result buildResult = build();
            if (!buildResult)
            {
                return buildResult;
            }
        }

        // 2) queue 種別ごとの実行先を確保
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

        // 3) 実行中 state と実行概要を初期化
        std::vector<PassExecutionInfo> passExecution(m_passes.size());
        std::vector<ResourceState> runtimeTrackedState = initial_resource_states();
        ExecutionSummary summary{};
        summary.frameNo = frameNo;
        summary.bufferIndex = frameResourceIndex;
        summary.swapchainImageIndex = swapchainImageIndex;
        summary.passCount = m_passes.size();

        // 4) pass の queue 種別から実行 queue を引く関数を用意
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

        // 5) 事前計算 barrier を現在 frame 用 backend barrier へ変換
        auto emit_barriers = [this, frameResourceIndex, swapchainImageIndex](ICommandContext& commandContext, size_t passIndex, bool beforePass, std::vector<ResourceState>& trackedState) -> Result
            {
                // 1) pass に紐づく barrier 群を取得
                const auto barrierIt = m_barriersByPass.find(passIndex);
                if (barrierIt == m_barriersByPass.end())
                {
                    return Result::ok();
                }

                // 2) 実行タイミング一致分だけ backend barrier を構築
                std::vector<ResourceBarrierDesc> barriers;
                for (const BarrierEvent& barrierEvent : barrierIt->second)
                {
                    if (barrierEvent.beforePass != beforePass)
                    {
                        continue;
                    }

                    ResourceBarrierDesc barrierDesc{};
                    const Result buildBarrierResult = make_barrier_desc(barrierEvent, frameResourceIndex, swapchainImageIndex, barrierDesc);
                    if (!buildBarrierResult)
                    {
                        return buildBarrierResult;
                    }
                    barriers.push_back(barrierDesc);
                    trackedState[barrierEvent.handle.index] = barrierEvent.after;
                }

                // 3) 発行対象が無ければ処理を打ち切り
                if (barriers.empty())
                {
                    return Result::ok();
                }

                // 4) command context へ barrier を発行
                return commandContext.resource_barriers(barriers.data(), barriers.size());
            };

        // 6) 依存順に各 pass を記録コマンドへ変換
        for (size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
        {
            // 1) 実行対象 pass と queue を確定
            const CompiledPass& compiledPass = m_passes[passIndex];
            const CommandListType queueType = compiledPass.pass->queue_type();
            IQueueContext& queue = queue_for(queueType);

            // 2) cross queue 依存があれば wait を注入
            if (passIndex < m_dependenciesByPass.size())
            {
                for (size_t depPassIndex : m_dependenciesByPass[passIndex])
                {
                    if (depPassIndex >= passExecution.size())
                    {
                        continue;
                    }

                    const PassExecutionInfo& depInfo = passExecution[depPassIndex];
                    if (!depInfo.submitted || depInfo.queueContext == nullptr || depInfo.queueContext == &queue)
                    {
                        continue;
                    }

                    const Result waitResult = queue.wait(*depInfo.queueContext, depInfo.signalPoint);
                    if (!waitResult)
                    {
                        return waitResult;
                    }
                    ++summary.waitCount;
                }
            }

            // 3) pass 記録用 command context を取得して初期化
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

            // 4) 実行前 barrier で required state へ遷移
            const Result preBarrierResult = emit_barriers(*commandContext, passIndex, true, runtimeTrackedState);
            if (!preBarrierResult)
            {
                return preBarrierResult;
            }

            // 5) pass 実行用の shader と descriptor を解決
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
                if (descriptorDecl.hasBufferIndex && descriptorDecl.bufferIndex != frameResourceIndex)
                {
                    continue;
                }

                FrameGraphContext::ResolvedDescriptorBinding resolvedBinding{};
                const Result resolveDescriptorResult = resolve_descriptor_binding(descriptorDecl, frameResourceIndex, swapchainImageIndex, resolvedBinding);
                if (!resolveDescriptorResult)
                {
                    return resolveDescriptorResult;
                }
                descriptorBindings.push_back(resolvedBinding);
            }

            // 6) デバッグ用実行概要へ bind 数を反映
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

            // 7) pass へ渡す前に command context の記録準備を完了
            const Result setupResult = commandContext->setup();
            if (!setupResult)
            {
                return setupResult;
            }

            if (queueType == CommandListType::Graphics
                && compiledPass.pipelineDecl.handle.valid()
                && compiledPass.rootSignatureDecl.handle.valid())
            {
                // 8) graphics pass は pso と root signature を先に bind
                const Result bindPipelineResult = commandContext->set_graphics_pipeline(
                    compiledPass.pipelineDecl.handle,
                    compiledPass.rootSignatureDecl.handle);
                if (!bindPipelineResult)
                {
                    return bindPipelineResult;
                }

                // 9) 解決済み descriptor を root parameter へ反映
                for (const FrameGraphContext::ResolvedDescriptorBinding& descriptorBinding : descriptorBindings)
                {
                    const Result bindDescriptorResult = commandContext->set_graphics_descriptor_table(
                        descriptorBinding.shaderRegister,
                        descriptorBinding.descriptorHandle);
                    if (!bindDescriptorResult)
                    {
                        return bindDescriptorResult;
                    }
                }
            }

            // 10) pass 実行用 context を構築
            FrameGraphContext context(
                *this,
                frameNo,
                frameResourceIndex,
                swapchainImageIndex,
                *commandContext,
                compiledPass.pipelineDecl.handle,
                compiledPass.rootSignatureDecl.handle,
                std::move(shaderHandles),
                std::move(descriptorBindings));
            if (queueType == CommandListType::Graphics)
            {
                // 11) graphics pass は viewport と scissor を既定サイズへ設定
                const Result viewportResult = commandContext->set_viewport_scissor(m_screenWidth, m_screenHeight);
                if (!viewportResult)
                {
                    return viewportResult;
                }

                // 12) render target と depth view を解決して om へ bind
                std::vector<ViewHandle> renderTargetViews;
                renderTargetViews.reserve(compiledPass.accesses.size());
                ViewHandle depthStencilView{};
                const Result resolveRenderTargetResult = resolve_render_target_views(compiledPass.accesses, frameResourceIndex, swapchainImageIndex, renderTargetViews, depthStencilView);
                if (!resolveRenderTargetResult)
                {
                    return resolveRenderTargetResult;
                }

                const Result bindRenderTargetResult = commandContext->set_render_targets(
                    renderTargetViews.empty() ? nullptr : renderTargetViews.data(),
                    static_cast<uint32_t>(renderTargetViews.size()),
                    depthStencilView);
                if (!bindRenderTargetResult)
                {
                    return bindRenderTargetResult;
                }
            }

            // 13) pass 本体を実行
            compiledPass.pass->execute(context);
            commandContext->end_event();

            // 14) 実行後 barrier を発行して最終 state を反映
            const Result postBarrierResult = emit_barriers(*commandContext, passIndex, false, runtimeTrackedState);
            if (!postBarrierResult)
            {
                return postBarrierResult;
            }

            // 15) command list を close して queue へ submit
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

            // 16) 完了点を signal して context 退役条件へ渡す
            QueueSyncPoint signalPoint{};
            const Result signalResult = queue.signal(signalPoint);
            if (!signalResult)
            {
                return signalResult;
            }

            const Result retireContextResult = commandPool.retire_context(std::move(commandContext), queue, signalPoint);
            if (!retireContextResult)
            {
                return retireContextResult;
            }

            passExecution[passIndex] = PassExecutionInfo{ queueType, &queue, signalPoint, true };
        }

        // 7) 集計済み barrier 数を反映して直近実行概要を更新
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
        // 1) buffer 作成宣言を frame graph 本体へ移譲
        return m_frameGraph.create_buffer(name, desc);
    }

    TextureHandle FrameGraphBuilder::create_texture(std::string_view name, const TextureDesc& desc)
    {
        // 1) texture 作成宣言を frame graph 本体へ移譲
        return m_frameGraph.create_texture(name, desc);
    }

    BufferHandle FrameGraphBuilder::import_buffer(std::string_view name, const BufferDesc& desc, ResourceState initialState)
    {
        // 1) 外部 buffer 取込宣言を frame graph 本体へ移譲
        return m_frameGraph.import_buffer(name, desc, initialState);
    }

    TextureHandle FrameGraphBuilder::import_texture(std::string_view name, const TextureDesc& desc, ResourceState initialState)
    {
        // 1) 外部 texture 取込宣言を frame graph 本体へ移譲
        return m_frameGraph.import_texture(name, desc, initialState);
    }

    BufferHandle FrameGraphBuilder::get_buffer(std::string_view name)
    {
        // 1) 宣言済み buffer 参照を frame graph 本体へ移譲
        return m_frameGraph.get_buffer(name);
    }

    TextureHandle FrameGraphBuilder::get_texture(std::string_view name)
    {
        // 1) 宣言済み texture 参照を frame graph 本体へ移譲
        return m_frameGraph.get_texture(name);
    }

    void FrameGraphBuilder::read(BufferHandle handle)
    {
        // 1) buffer handle を検証して read access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, ResourceState::ShaderResource });
    }

    void FrameGraphBuilder::read(TextureHandle handle)
    {
        // 1) texture handle を検証して read access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, ResourceState::ShaderResource });
    }

    void FrameGraphBuilder::write(BufferHandle handle)
    {
        // 1) buffer handle を検証して uav write access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::UnorderedAccess });
    }

    void FrameGraphBuilder::write(TextureHandle handle)
    {
        // 1) texture handle を検証して uav write access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::UnorderedAccess });
    }

    void FrameGraphBuilder::write(BufferHandle handle, ResourceState finalState)
    {
        // 1) buffer handle を検証して最終 state 付き write access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::UnorderedAccess, true, finalState });
    }

    void FrameGraphBuilder::write(TextureHandle handle, ResourceState finalState)
    {
        // 1) texture handle を検証して最終 state 付き write access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::UnorderedAccess, true, finalState });
    }

    void FrameGraphBuilder::render(TextureHandle handle)
    {
        // 1) texture handle を検証して render target 書き込みを記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::RenderTarget });
    }

    void FrameGraphBuilder::render(TextureHandle handle, ResourceState finalState)
    {
        // 1) texture handle を検証して最終 state 付き render target 書き込みを記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::RenderTarget, true, finalState });
    }

    void FrameGraphBuilder::depth_write(TextureHandle handle)
    {
        // 1) texture handle を検証して depth 書き込みを記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::DepthWrite });
    }

    void FrameGraphBuilder::depth_write(TextureHandle handle, ResourceState finalState)
    {
        // 1) texture handle を検証して最終 state 付き depth 書き込みを記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::DepthWrite, true, finalState });
    }

    void FrameGraphBuilder::cpy_src(BufferHandle handle)
    {
        // 1) buffer handle を検証して copy source access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, ResourceState::CopySource });
    }

    void FrameGraphBuilder::cpy_src(TextureHandle handle)
    {
        // 1) texture handle を検証して copy source access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Read, ResourceState::CopySource });
    }

    void FrameGraphBuilder::cpy_dst(BufferHandle handle)
    {
        // 1) buffer handle を検証して copy dest access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::CopyDest });
    }

    void FrameGraphBuilder::cpy_dst(TextureHandle handle)
    {
        // 1) texture handle を検証して copy dest access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::CopyDest });
    }

    void FrameGraphBuilder::cpy_dst(BufferHandle handle, ResourceState finalState)
    {
        // 1) buffer handle を検証して最終 state 付き copy dest access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::CopyDest, true, finalState });
    }

    void FrameGraphBuilder::cpy_dst(TextureHandle handle, ResourceState finalState)
    {
        // 1) texture handle を検証して最終 state 付き copy dest access を記録
        m_frameGraph.validate_resource_handle(handle);
        m_accesses.push_back(ResourceAccess{ to_resource_ref(handle), ResourceAccessType::Write, ResourceState::CopyDest, true, finalState });
    }

    void FrameGraphBuilder::use_pipeline(const GraphicsPipelineStateDesc& desc)
    {
        // 1) pipeline 宣言を compiled pass へ記録
        m_frameGraph.declare_pipeline(m_pipelineDecl, desc);
    }

    void FrameGraphBuilder::get_pipeline(std::string_view name)
    {
        // 1) pipeline 参照を compiled pass へ記録
        m_frameGraph.reference_pipeline(m_pipelineDecl, name);
    }

    void FrameGraphBuilder::use_root_signature(const RootSignatureDesc& desc)
    {
        // 1) root signature 宣言を compiled pass へ記録
        m_frameGraph.declare_root_signature(m_rootSignatureDecl, desc);
    }

    void FrameGraphBuilder::get_root_signature(std::string_view name)
    {
        // 1) root signature 参照を compiled pass へ記録
        m_frameGraph.reference_root_signature(m_rootSignatureDecl, name);
    }

    void FrameGraphBuilder::compile_shader(const ShaderCompileDesc& desc)
    {
        // 1) shader compile 宣言を compiled pass へ記録
        m_frameGraph.declare_shader(m_shaderDecls, desc);
    }

    void FrameGraphBuilder::get_shader(std::string_view name)
    {
        // 1) shader 参照を compiled pass へ記録
        m_frameGraph.reference_shader(m_shaderDecls, name);
    }

    void FrameGraphBuilder::bind_cbv(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility)
    {
        // 1) buffer handle を検証して cbv bind 宣言を登録
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
        // 1) frame index 条件付き cbv bind 宣言を登録
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
        // 1) buffer handle を検証して srv bind 宣言を登録
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
        // 1) texture handle を検証して srv bind 宣言を登録
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
        // 1) frame index 条件付き buffer srv bind 宣言を登録
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
        // 1) frame index 条件付き texture srv bind 宣言を登録
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
        // 1) buffer handle を検証して uav bind 宣言を登録
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
        // 1) texture handle を検証して uav bind 宣言を登録
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
        // 1) frame index 条件付き buffer uav bind 宣言を登録
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
        // 1) frame index 条件付き texture uav bind 宣言を登録
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
        // 1) 現在 frame index で論理 buffer を物理解決
        return m_frameGraph.resolve_buffer(logicalHandle, m_frameResourceIndex, m_swapchainImageIndex, outHandle);
    }

    Result FrameGraphContext::resolve_texture(TextureHandle logicalHandle, TextureHandle& outHandle) const
    {
        // 1) 現在 frame index で論理 texture を物理解決
        return m_frameGraph.resolve_texture(logicalHandle, m_frameResourceIndex, m_swapchainImageIndex, outHandle);
    }
} // 名前空間 cue::graphicscore
