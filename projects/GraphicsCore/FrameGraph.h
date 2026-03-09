#pragma once
#include "GraphicsCommon.h"
#include "GraphicsInterface.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ShaderCompiler.h"
#include "PipelineManager.h"
#include "ViewManager.h"

namespace Cue::GraphicsCore
{
    // FrameGraph uses one reference type for both buffers and textures.
    // This points to a logical resource, not directly to the backend object.
    struct ResourceRef final
    {
        ResourceKind kind = ResourceKind::Buffer;
        uint32_t index = Handle<BufferTag>::k_invalid;
        uint32_t generation = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return index != Handle<BufferTag>::k_invalid;
        }
    };

    [[nodiscard]] constexpr ResourceRef to_resource_ref(BufferHandle handle) noexcept
    {
        return ResourceRef{ ResourceKind::Buffer, handle.index, handle.generation };
    }

    [[nodiscard]] constexpr ResourceRef to_resource_ref(TextureHandle handle) noexcept
    {
        return ResourceRef{ ResourceKind::Texture, handle.index, handle.generation };
    }

    // Access declaration recorded during setup().
    // build() uses this to derive dependencies and state transitions.
    struct ResourceAccess final
    {
        ResourceRef handle{};
        ResourceAccessType type = ResourceAccessType::Read;
        ResourceState requiredState = ResourceState::Common;
        bool hasFinalState = false;
        ResourceState finalState = ResourceState::Common;
    };

    // PSO/root signature/shader/descriptor declarations gathered in setup().
    // Physical handles are resolved later in build().
    struct PipelineBindingDecl final
    {
        bool declared = false;
        ResourceNameId nameId = 0;
        std::string debugName = {};
        GraphicsPipelineStateDesc desc = {};
        PipelineStateHandle handle = {};
    };

    struct RootSignatureBindingDecl final
    {
        bool declared = false;
        ResourceNameId nameId = 0;
        std::string debugName = {};
        RootSignatureDesc desc = {};
        RootSignatureHandle handle = {};
    };

    struct ShaderBindingDecl final
    {
        ResourceNameId nameId = 0;
        ShaderCompileDesc desc = {};
        ShaderBlobHandle handle = {};
    };

    struct DescriptorBindingDecl final
    {
        RootParameterType type = RootParameterType::SRV;
        ShaderVisibility visibility = ShaderVisibility::All;
        uint32_t shaderRegister = 0;
        bool hasBufferIndex = false;
        uint32_t bufferIndex = 0;
        ResourceRef resource{};
    };

    class FrameGraph;

    class FrameGraphBuilder final
    {
    public:
        FrameGraphBuilder(
            FrameGraph& frameGraph,
            std::vector<ResourceAccess>& accesses,
            PipelineBindingDecl& pipelineDecl,
            RootSignatureBindingDecl& rootSignatureDecl,
            std::vector<ShaderBindingDecl>& shaderDecls,
            std::vector<DescriptorBindingDecl>& descriptorDecls) noexcept
            : m_frameGraph(frameGraph)
            , m_accesses(accesses)
            , m_pipelineDecl(pipelineDecl)
            , m_rootSignatureDecl(rootSignatureDecl)
            , m_shaderDecls(shaderDecls)
            , m_descriptorDecls(descriptorDecls)
        {}

        // setup() only declares what the pass needs.
        // Actual resource creation happens in build().
        [[nodiscard]] BufferHandle create_buffer(std::string_view name, const BufferDesc& desc);
        [[nodiscard]] TextureHandle create_texture(std::string_view name, const TextureDesc& desc);
        [[nodiscard]] BufferHandle import_buffer(std::string_view name, const BufferDesc& desc, ResourceState initialState);
        [[nodiscard]] TextureHandle import_texture(std::string_view name, const TextureDesc& desc, ResourceState initialState);
        [[nodiscard]] BufferHandle get_buffer(std::string_view name);
        [[nodiscard]] TextureHandle get_texture(std::string_view name);

        void read(BufferHandle handle);
        void read(TextureHandle handle);
        void write(BufferHandle handle);
        void write(TextureHandle handle);
        void write(BufferHandle handle, ResourceState finalState);
        void write(TextureHandle handle, ResourceState finalState);
        void render(TextureHandle handle);
        void render(TextureHandle handle, ResourceState finalState);
        void cpy_src(BufferHandle handle);
        void cpy_src(TextureHandle handle);
        void cpy_dst(BufferHandle handle);
        void cpy_dst(TextureHandle handle);
        void cpy_dst(BufferHandle handle, ResourceState finalState);
        void cpy_dst(TextureHandle handle, ResourceState finalState);

        void use_pipeline(const GraphicsPipelineStateDesc& desc);
        void get_pipeline(std::string_view name);
        void use_root_signature(const RootSignatureDesc& desc);
        void get_root_signature(std::string_view name);
        void compile_shader(const ShaderCompileDesc& desc);
        void get_shader(std::string_view name);
        void bind_cbv(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_cbv_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_srv(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_srv(uint32_t shaderRegister, TextureHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_srv_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_srv_at(uint32_t shaderRegister, uint32_t bufferIndex, TextureHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_uav(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_uav(uint32_t shaderRegister, TextureHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_uav_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        void bind_uav_at(uint32_t shaderRegister, uint32_t bufferIndex, TextureHandle handle, ShaderVisibility visibility = ShaderVisibility::All);

    private:
        FrameGraph& m_frameGraph;
        std::vector<ResourceAccess>& m_accesses;
        PipelineBindingDecl& m_pipelineDecl;
        RootSignatureBindingDecl& m_rootSignatureDecl;
        std::vector<ShaderBindingDecl>& m_shaderDecls;
        std::vector<DescriptorBindingDecl>& m_descriptorDecls;
    };

    class FrameGraphContext final
    {
    public:
        struct ResolvedDescriptorBinding final
        {
            RootParameterType type = RootParameterType::SRV;
            ShaderVisibility visibility = ShaderVisibility::All;
            uint32_t shaderRegister = 0;
            ResourceKind resourceKind = ResourceKind::Buffer;
            ViewHandle viewHandle = {};
            DescriptorHandle descriptorHandle = {};
        };

        // Runtime view passed to execute().
        // It can resolve logical handles to the physical resource for the current frame.
        FrameGraphContext(
            FrameGraph& frameGraph,
            uint64_t frameNo,
            uint32_t frameResourceIndex,
            uint32_t swapchainImageIndex,
            ICommandContext& commandContext,
            PipelineStateHandle pipelineHandle,
            RootSignatureHandle rootSignatureHandle,
            std::vector<ShaderBlobHandle> shaderHandles,
            std::vector<ResolvedDescriptorBinding> descriptorBindings) noexcept
            : m_frameGraph(frameGraph)
            , m_frameNo(frameNo)
            , m_frameResourceIndex(frameResourceIndex)
            , m_swapchainImageIndex(swapchainImageIndex)
            , m_commandContext(commandContext)
            , m_pipelineHandle(pipelineHandle)
            , m_rootSignatureHandle(rootSignatureHandle)
            , m_shaderHandles(std::move(shaderHandles))
            , m_descriptorBindings(std::move(descriptorBindings))
        {}

        [[nodiscard]] uint64_t frame_no() const noexcept
        {
            return m_frameNo;
        }

        [[nodiscard]] uint32_t buffer_index() const noexcept
        {
            return frame_resource_index();
        }

        [[nodiscard]] uint32_t frame_resource_index() const noexcept
        {
            return m_frameResourceIndex;
        }

        [[nodiscard]] uint32_t swapchain_image_index() const noexcept
        {
            return m_swapchainImageIndex;
        }

        [[nodiscard]] ICommandContext& command_context() noexcept
        {
            return m_commandContext;
        }

        [[nodiscard]] IViewManager& view_manager() noexcept;

        [[nodiscard]] PipelineStateHandle pipeline_handle() const noexcept
        {
            return m_pipelineHandle;
        }

        [[nodiscard]] RootSignatureHandle root_signature_handle() const noexcept
        {
            return m_rootSignatureHandle;
        }

        [[nodiscard]] const std::vector<ShaderBlobHandle>& shader_handles() const noexcept
        {
            return m_shaderHandles;
        }

        [[nodiscard]] const std::vector<ResolvedDescriptorBinding>& descriptor_bindings() const noexcept
        {
            return m_descriptorBindings;
        }

        [[nodiscard]] Result resolve_buffer(BufferHandle logicalHandle, BufferHandle& outHandle) const;
        [[nodiscard]] Result resolve_texture(TextureHandle logicalHandle, TextureHandle& outHandle) const;

    private:
        FrameGraph& m_frameGraph;
        uint64_t m_frameNo = 0;
        uint32_t m_frameResourceIndex = 0;
        uint32_t m_swapchainImageIndex = 0;
        ICommandContext& m_commandContext;
        PipelineStateHandle m_pipelineHandle = {};
        RootSignatureHandle m_rootSignatureHandle = {};
        std::vector<ShaderBlobHandle> m_shaderHandles = {};
        std::vector<ResolvedDescriptorBinding> m_descriptorBindings = {};
    };

    class FrameGraphPass
    {
    public:
        virtual ~FrameGraphPass() = default;
        [[nodiscard]] virtual const char* name() const = 0;
        [[nodiscard]] virtual CommandListType queue_type() const
        {
            return CommandListType::Graphics;
        }
        virtual void setup(FrameGraphBuilder& builder) = 0;
        virtual void execute(FrameGraphContext& ctx) const = 0;
    };

    class FrameGraph final
    {
    public:
        FrameGraph(
            uint32_t& screenWidth,
            uint32_t& screenHeight,
            IBufferManager& bufferManager,
            ITextureManager& textureManager,
            IViewManager& viewManager,
            IPipelineManager& pipelineManager,
            uint32_t defaultBufferingCount = 1) noexcept
            : m_screenWidth(screenWidth)
            , m_screenHeight(screenHeight)
            , m_bufferManager(bufferManager)
            , m_textureManager(textureManager)
            , m_viewManager(viewManager)
            , m_pipelineManager(pipelineManager)
            , m_defaultBufferingCount((std::max)(defaultBufferingCount, 1u))
        {}
        ~FrameGraph() = default;

        [[nodiscard]] Result add_pass(std::unique_ptr<FrameGraphPass> pass);

        template <class TPass, class... Args, class = std::enable_if_t<std::is_base_of_v<FrameGraphPass, TPass>>>
        [[nodiscard]] TPass* add_pass(Args&&... args)
        {
            // 1) Passを生成して登録し、成功時のみポインタを返す。
            auto pass = std::make_unique<TPass>(std::forward<Args>(args)...);
            TPass* ref = pass.get();
            const Result result = add_pass(std::move(pass));
            if (!result)
            {
                return nullptr;
            }

            return ref;
        }

        [[nodiscard]] Result build();
        [[nodiscard]] Result execute(uint64_t frameNo, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, ICommandPool& commandPool, IQueuePool& queuePool);
        // Compact per-frame summary used by Engine::tick().
        struct ExecutionSummary final
        {
            uint64_t frameNo = 0;
            uint32_t bufferIndex = 0;
            uint32_t swapchainImageIndex = 0;
            size_t passCount = 0;
            size_t barrierCount = 0;
            size_t waitCount = 0;
            size_t descriptorCount = 0;
            size_t shaderCount = 0;
            PipelineStateHandle pipelineHandle = {};
            RootSignatureHandle rootSignatureHandle = {};
        };

        [[nodiscard]] const ExecutionSummary& last_execution_summary() const noexcept
        {
            return m_lastExecutionSummary;
        }

        [[nodiscard]] IViewManager& view_manager() noexcept
        {
            return m_viewManager;
        }

    private:
        friend class FrameGraphBuilder;
        friend class FrameGraphContext;

        // Metadata for a logical resource declared in setup().
        // Backend ownership stays in the managers; FrameGraph stores name and desc here.
        struct LogicalResource final
        {
            ResourceRef ref{};
            ResourceNameId nameId = 0;
            std::string debugName = {};
            ResourceState initialState = ResourceState::Common;
            bool external = false;
            uint32_t instanceCount = 1;
            ResourceInstanceSource instanceSource = ResourceInstanceSource::FrameResourceIndex;
            BufferDesc bufferDesc = {};
            TextureDesc textureDesc = {};
        };

        // Pass data after setup() has been captured.
        // execute() consumes these declarations without rebuilding them.
        struct CompiledPass final
        {
            std::unique_ptr<FrameGraphPass> pass = nullptr;
            std::vector<ResourceAccess> accesses = {};
            PipelineBindingDecl pipelineDecl = {};
            RootSignatureBindingDecl rootSignatureDecl = {};
            std::vector<ShaderBindingDecl> shaderDecls = {};
            std::vector<DescriptorBindingDecl> descriptorDecls = {};
        };

        struct ResourceHazardState final
        {
            int32_t lastWriter = -1;
            std::vector<int32_t> lastReaders = {};
        };

        struct BarrierEvent final
        {
            ResourceRef handle{};
            ResourceState before = ResourceState::Common;
            ResourceState after = ResourceState::Common;
            bool beforePass = true;
        };

        struct PassExecutionInfo final
        {
            CommandListType queueType = CommandListType::Graphics;
            IQueueContext* queueContext = nullptr;
            QueueSyncPoint signalPoint = {};
            bool submitted = false;
        };

        template <class HandleT>
        [[nodiscard]] HandleT declare_resource(std::string_view name, ResourceKind expectedKind);
        template <class HandleT>
        [[nodiscard]] HandleT find_resource(std::string_view name, ResourceKind expectedKind) const;
        [[nodiscard]] uint32_t resolve_buffering_count(uint32_t requestedCount) const noexcept;

        [[nodiscard]] BufferHandle create_buffer(std::string_view name, const BufferDesc& desc);
        [[nodiscard]] TextureHandle create_texture(std::string_view name, const TextureDesc& desc);
        [[nodiscard]] BufferHandle import_buffer(std::string_view name, const BufferDesc& desc, ResourceState initialState);
        [[nodiscard]] TextureHandle import_texture(std::string_view name, const TextureDesc& desc, ResourceState initialState);
        [[nodiscard]] BufferHandle get_buffer(std::string_view name);
        [[nodiscard]] TextureHandle get_texture(std::string_view name);

        [[nodiscard]] uint32_t resolve_resource_instance_index(const LogicalResource& resource, uint32_t frameResourceIndex, uint32_t swapchainImageIndex) const noexcept;
        [[nodiscard]] Result resolve_buffer(BufferHandle logicalHandle, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, BufferHandle& outHandle) const;
        [[nodiscard]] Result resolve_texture(TextureHandle logicalHandle, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, TextureHandle& outHandle) const;
        [[nodiscard]] Result resolve_descriptor_binding(const DescriptorBindingDecl& binding, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, FrameGraphContext::ResolvedDescriptorBinding& outBinding) const;
        [[nodiscard]] Result resolve_render_target_views(const std::vector<ResourceAccess>& accesses, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, std::vector<ViewHandle>& outRenderTargetViews, ViewHandle& outDepthStencilView) const;
        [[nodiscard]] Result make_barrier_desc(const BarrierEvent& event, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, ResourceBarrierDesc& outBarrier) const;

        void validate_resource_handle(BufferHandle handle) const;
        void validate_resource_handle(TextureHandle handle) const;
        void validate_resource_ref(ResourceRef ref, ResourceKind expectedKind) const;
        [[nodiscard]] const LogicalResource& get_logical_resource(ResourceRef ref) const;
        [[nodiscard]] std::vector<ResourceState> initial_resource_states() const;
        [[nodiscard]] Result materialize_declared_resources();
        [[nodiscard]] Result resolve_pipeline_artifacts();
        [[nodiscard]] Result topological_sort_by_resource_dependencies(std::vector<size_t>& outSorted) const;
        void reorder_compiled_passes(const std::vector<size_t>& sortedOrder);
        void build_execution_dependencies();
        void build_resource_barriers();
        void declare_pipeline(PipelineBindingDecl& outDecl, const GraphicsPipelineStateDesc& desc);
        void reference_pipeline(PipelineBindingDecl& outDecl, std::string_view name);
        void declare_root_signature(RootSignatureBindingDecl& outDecl, const RootSignatureDesc& desc);
        void reference_root_signature(RootSignatureBindingDecl& outDecl, std::string_view name);
        void declare_shader(std::vector<ShaderBindingDecl>& outDecls, const ShaderCompileDesc& desc);
        void reference_shader(std::vector<ShaderBindingDecl>& outDecls, std::string_view name);
        void declare_descriptor(std::vector<DescriptorBindingDecl>& outDecls, const DescriptorBindingDecl& desc);

    private:
        IBufferManager& m_bufferManager;
        ITextureManager& m_textureManager;
        IViewManager& m_viewManager;
        IPipelineManager& m_pipelineManager;
        uint32_t m_defaultBufferingCount = 1;
        std::vector<LogicalResource> m_resources;
        std::unordered_map<ResourceNameId, ResourceRef> m_resourceByNameId;
        std::vector<CompiledPass> m_passes;
        std::vector<std::vector<size_t>> m_dependenciesByPass;
        std::unordered_map<size_t, std::vector<BarrierEvent>> m_barriersByPass;
        std::unordered_map<ResourceNameId, PipelineBindingDecl> m_pipelineDecls;
        std::unordered_map<ResourceNameId, RootSignatureBindingDecl> m_rootSignatureDecls;
        std::unordered_map<ResourceNameId, ShaderBindingDecl> m_shaderDecls;
        ExecutionSummary m_lastExecutionSummary{};
        bool m_isBuilt = false;
        bool m_isDirty = false;
        uint32_t& m_screenWidth;
        uint32_t& m_screenHeight;
    };
    inline IViewManager& FrameGraphContext::view_manager() noexcept
    {
        return m_frameGraph.view_manager();
    }
} // namespace Cue::GraphicsCore
