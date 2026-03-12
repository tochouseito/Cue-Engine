#pragma once
#include "GraphicsCommon.h"
#include "GraphicsInterface.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ShaderCompiler.h"
#include "PipelineManager.h"
#include "ViewManager.h"
#include "StaticMeshBufferPool.h"

namespace Cue::GraphicsCore
{
    // buffer と texture 共通参照
    // backend 実体ではなく論理 resource を指す
    struct ResourceRef final
    {
        ResourceKind kind = ResourceKind::Buffer;
        uint32_t index = Handle<BufferTag>::k_invalid;
        uint32_t generation = 0;

        /// @brief 有効判定
        [[nodiscard]] bool valid() const noexcept
        {
            return index != Handle<BufferTag>::k_invalid;
        }
    };

    /// @brief buffer handle を resource 参照へ変換
    [[nodiscard]] constexpr ResourceRef to_resource_ref(BufferHandle handle) noexcept
    {
        return ResourceRef{ ResourceKind::Buffer, handle.index, handle.generation };
    }

    /// @brief texture handle を resource 参照へ変換
    [[nodiscard]] constexpr ResourceRef to_resource_ref(TextureHandle handle) noexcept
    {
        return ResourceRef{ ResourceKind::Texture, handle.index, handle.generation };
    }

    // setup() で記録する access 宣言
    // build() で依存関係と state 遷移を導出
    struct ResourceAccess final
    {
        ResourceRef handle{};
        ResourceAccessType type = ResourceAccessType::Read;
        ResourceState requiredState = ResourceState::Common;
        bool hasFinalState = false;
        ResourceState finalState = ResourceState::Common;
    };

    // setup() で集約する pso と root signature と shader と descriptor 宣言
    // build() で物理 handle へ解決
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

    struct ResourceResolveContext final
    {
        uint32_t frameResourceIndex = 0;
        uint32_t swapchainImageIndex = 0;
    };

    class FrameGraph;

    class FrameGraphBuilder final
    {
    public:
        /// @brief builder 生成
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

        /// @brief buffer 作成宣言
        [[nodiscard]] BufferHandle create_buffer(std::string_view name, const BufferDesc& desc);
        /// @brief texture 作成宣言
        [[nodiscard]] TextureHandle create_texture(std::string_view name, const TextureDesc& desc);
        /// @brief 外部 buffer 取込宣言
        [[nodiscard]] BufferHandle import_buffer(std::string_view name, const BufferDesc& desc, ResourceState initialState);
        /// @brief 外部 texture 取込宣言
        [[nodiscard]] TextureHandle import_texture(std::string_view name, const TextureDesc& desc, ResourceState initialState);
        /// @brief 宣言済み buffer 取得
        [[nodiscard]] BufferHandle get_buffer(std::string_view name);
        /// @brief 宣言済み texture 取得
        [[nodiscard]] TextureHandle get_texture(std::string_view name);

        /// @brief buffer 読み取り宣言
        void read(BufferHandle handle);
        /// @brief texture 読み取り宣言
        void read(TextureHandle handle);
        /// @brief buffer 書き込み宣言
        void write(BufferHandle handle);
        /// @brief texture 書き込み宣言
        void write(TextureHandle handle);
        /// @brief buffer 書き込み宣言と終了 state 設定
        void write(BufferHandle handle, ResourceState finalState);
        /// @brief texture 書き込み宣言と終了 state 設定
        void write(TextureHandle handle, ResourceState finalState);
        /// @brief render target 書き込み宣言
        void render(TextureHandle handle);
        /// @brief render target 書き込み宣言と終了 state 設定
        void render(TextureHandle handle, ResourceState finalState);
        /// @brief depth 書き込み宣言
        void depth_write(TextureHandle handle);
        /// @brief depth 書き込み宣言と終了 state 設定
        void depth_write(TextureHandle handle, ResourceState finalState);
        /// @brief copy source として buffer 宣言
        void cpy_src(BufferHandle handle);
        /// @brief copy source として texture 宣言
        void cpy_src(TextureHandle handle);
        /// @brief copy dest として buffer 宣言
        void cpy_dst(BufferHandle handle);
        /// @brief copy dest として texture 宣言
        void cpy_dst(TextureHandle handle);
        /// @brief copy dest として buffer 宣言し終了 state 設定
        void cpy_dst(BufferHandle handle, ResourceState finalState);
        /// @brief copy dest として texture 宣言し終了 state 設定
        void cpy_dst(TextureHandle handle, ResourceState finalState);

        /// @brief pipeline 宣言
        void use_pipeline(const GraphicsPipelineStateDesc& desc);
        /// @brief 既存 pipeline 参照
        void get_pipeline(std::string_view name);
        /// @brief root signature 宣言
        void use_root_signature(const RootSignatureDesc& desc);
        /// @brief 既存 root signature 参照
        void get_root_signature(std::string_view name);
        /// @brief shader コンパイル宣言
        void compile_shader(const ShaderCompileDesc& desc);
        /// @brief 既存 shader 参照
        void get_shader(std::string_view name);
        /// @brief cbv bind 宣言
        void bind_cbv(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief frame index 付き cbv bind 宣言
        void bind_cbv_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief buffer srv bind 宣言
        void bind_srv(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief texture srv bind 宣言
        void bind_srv(uint32_t shaderRegister, TextureHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief frame index 付き buffer srv bind 宣言
        void bind_srv_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief frame index 付き texture srv bind 宣言
        void bind_srv_at(uint32_t shaderRegister, uint32_t bufferIndex, TextureHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief buffer uav bind 宣言
        void bind_uav(uint32_t shaderRegister, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief texture uav bind 宣言
        void bind_uav(uint32_t shaderRegister, TextureHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief frame index 付き buffer uav bind 宣言
        void bind_uav_at(uint32_t shaderRegister, uint32_t bufferIndex, BufferHandle handle, ShaderVisibility visibility = ShaderVisibility::All);
        /// @brief frame index 付き texture uav bind 宣言
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

        /// @brief context 生成
        FrameGraphContext(
            FrameGraph& frameGraph,
            uint64_t frameNo,
            ResourceResolveContext resolveContext,
            ICommandContext& commandContext,
            PipelineStateHandle pipelineHandle,
            RootSignatureHandle rootSignatureHandle,
            std::vector<ShaderBlobHandle> shaderHandles,
            std::vector<ResolvedDescriptorBinding> descriptorBindings) noexcept
            : m_frameGraph(frameGraph)
            , m_frameNo(frameNo)
            , m_resolveContext(resolveContext)
            , m_commandContext(commandContext)
            , m_pipelineHandle(pipelineHandle)
            , m_rootSignatureHandle(rootSignatureHandle)
            , m_shaderHandles(std::move(shaderHandles))
            , m_descriptorBindings(std::move(descriptorBindings))
        {}

        /// @brief フレーム番号取得
        [[nodiscard]] uint64_t frame_no() const noexcept
        {
            return m_frameNo;
        }

        /// @brief buffer index 取得
        [[nodiscard]] uint32_t buffer_index() const noexcept
        {
            return frame_resource_index();
        }

        /// @brief frame resource index 取得
        [[nodiscard]] uint32_t frame_resource_index() const noexcept
        {
            return m_resolveContext.frameResourceIndex;
        }

        /// @brief swapchain image index 取得
        [[nodiscard]] uint32_t swapchain_image_index() const noexcept
        {
            return m_resolveContext.swapchainImageIndex;
        }

        /// @brief command context 取得
        [[nodiscard]] ICommandContext& command_context() noexcept
        {
            return m_commandContext;
        }

        /// @brief buffer manager 取得
        [[nodiscard]] IBufferManager& buffer_manager() noexcept;

        /// @brief view manager 取得
        [[nodiscard]] IViewManager& view_manager() noexcept;
        /// @brief static mesh buffer pool 取得
        [[nodiscard]] StaticMeshBufferPool& static_mesh_buffer_pool() noexcept;
        /// @brief 画面幅取得
        [[nodiscard]] uint32_t screen_width() const noexcept;
        /// @brief 画面高さ取得
        [[nodiscard]] uint32_t screen_height() const noexcept;

        /// @brief pipeline handle 取得
        [[nodiscard]] PipelineStateHandle pipeline_handle() const noexcept
        {
            return m_pipelineHandle;
        }

        /// @brief root signature handle 取得
        [[nodiscard]] RootSignatureHandle root_signature_handle() const noexcept
        {
            return m_rootSignatureHandle;
        }

        /// @brief shader handle 群取得
        [[nodiscard]] const std::vector<ShaderBlobHandle>& shader_handles() const noexcept
        {
            return m_shaderHandles;
        }

        /// @brief descriptor bind 群取得
        [[nodiscard]] const std::vector<ResolvedDescriptorBinding>& descriptor_bindings() const noexcept
        {
            return m_descriptorBindings;
        }

        /// @brief 論理 buffer へ cpu 書き込み
        [[nodiscard]] Result write_buffer(BufferHandle logicalHandle, uint64_t byteOffset, const void* data, uint32_t byteSize) const;

        /// @brief 論理 buffer 解決
        [[nodiscard]] Result resolve_buffer(BufferHandle logicalHandle, BufferHandle& outHandle) const;
        /// @brief 論理 texture 解決
        [[nodiscard]] Result resolve_texture(TextureHandle logicalHandle, TextureHandle& outHandle) const;

    private:
        FrameGraph& m_frameGraph;
        uint64_t m_frameNo = 0;
        ResourceResolveContext m_resolveContext{};
        ICommandContext& m_commandContext;
        PipelineStateHandle m_pipelineHandle = {};
        RootSignatureHandle m_rootSignatureHandle = {};
        std::vector<ShaderBlobHandle> m_shaderHandles = {};
        std::vector<ResolvedDescriptorBinding> m_descriptorBindings = {};
    };

    class FrameGraphPass
    {
    public:
        /// @brief 破棄
        virtual ~FrameGraphPass() = default;
        /// @brief pass 名取得
        [[nodiscard]] virtual const char* name() const = 0;
        /// @brief queue 種別取得
        [[nodiscard]] virtual CommandListType queue_type() const
        {
            return CommandListType::Graphics;
        }
        /// @brief setup
        virtual void setup(FrameGraphBuilder& builder) = 0;
        /// @brief execute
        virtual void execute(FrameGraphContext& ctx) const = 0;
    };

    class FrameGraph final
    {
    public:
        /// @brief graph 生成
        FrameGraph(
            uint32_t& screenWidth,
            uint32_t& screenHeight,
            IBufferManager& bufferManager,
            ITextureManager& textureManager,
            IViewManager& viewManager,
            StaticMeshBufferPool& staticMeshBufferPool,
            IPipelineManager& pipelineManager,
            uint32_t defaultBufferingCount = 1) noexcept
            : m_screenWidth(screenWidth)
            , m_screenHeight(screenHeight)
            , m_bufferManager(bufferManager)
            , m_textureManager(textureManager)
            , m_viewManager(viewManager)
            , m_staticMeshBufferPool(staticMeshBufferPool)
            , m_pipelineManager(pipelineManager)
            , m_defaultBufferingCount((std::max)(defaultBufferingCount, 1u))
        {}
        /// @brief graph 破棄
        ~FrameGraph() = default;

        /// @brief pass 登録
        [[nodiscard]] Result add_pass(std::unique_ptr<FrameGraphPass> pass);

        template <class TPass, class... Args, class = std::enable_if_t<std::is_base_of_v<FrameGraphPass, TPass>>>
        [[nodiscard]] TPass* add_pass(Args&&... args)
        {
            // 1) pass 生成と登録
            auto pass = std::make_unique<TPass>(std::forward<Args>(args)...);
            TPass* ref = pass.get();
            const Result result = add_pass(std::move(pass));
            if (!result)
            {
                return nullptr;
            }

            return ref;
        }

        /// @brief graph build
        [[nodiscard]] Result build();
        /// @brief graph 実行
        [[nodiscard]] Result execute(uint64_t frameNo, uint32_t frameResourceIndex, uint32_t swapchainImageIndex, ICommandPool& commandPool, IQueuePool& queuePool);
        // engine::tick() 用フレーム実行概要
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

        /// @brief 直近実行概要取得
        [[nodiscard]] const ExecutionSummary& last_execution_summary() const noexcept
        {
            return m_lastExecutionSummary;
        }

        /// @brief view manager 取得
        [[nodiscard]] IViewManager& view_manager() noexcept
        {
            return m_viewManager;
        }

        /// @brief static mesh buffer pool 取得
        [[nodiscard]] StaticMeshBufferPool& static_mesh_buffer_pool() noexcept
        {
            return m_staticMeshBufferPool;
        }

    private:
        friend class FrameGraphBuilder;
        friend class FrameGraphContext;

        // setup() で宣言した論理 resource 情報
        // backend 所有は manager 側に残し name と desc だけ保持
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

        // setup() 後に確定した pass 情報
        // execute() では再構築せずそのまま使用
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
        /// @brief 論理 resource 宣言
        [[nodiscard]] HandleT declare_resource(std::string_view name, ResourceKind expectedKind);
        template <class HandleT>
        /// @brief 論理 resource 取得
        [[nodiscard]] HandleT find_resource(std::string_view name, ResourceKind expectedKind) const;
        /// @brief buffering count 正規化
        [[nodiscard]] uint32_t resolve_buffering_count(uint32_t requestedCount) const noexcept;

        /// @brief buffer 生成宣言実体
        [[nodiscard]] BufferHandle create_buffer(std::string_view name, const BufferDesc& desc);
        /// @brief texture 生成宣言実体
        [[nodiscard]] TextureHandle create_texture(std::string_view name, const TextureDesc& desc);
        /// @brief 外部 buffer 取込宣言実体
        [[nodiscard]] BufferHandle import_buffer(std::string_view name, const BufferDesc& desc, ResourceState initialState);
        /// @brief 外部 texture 取込宣言実体
        [[nodiscard]] TextureHandle import_texture(std::string_view name, const TextureDesc& desc, ResourceState initialState);
        /// @brief 宣言済み buffer 取得実体
        [[nodiscard]] BufferHandle get_buffer(std::string_view name);
        /// @brief 宣言済み texture 取得実体
        [[nodiscard]] TextureHandle get_texture(std::string_view name);

        /// @brief 実行時 instance 解決 context 正規化
        [[nodiscard]] ResourceResolveContext make_resolve_context(uint32_t frameResourceIndex, uint32_t swapchainImageIndex) const noexcept;
        /// @brief 実体 index 解決
        [[nodiscard]] uint32_t resolve_resource_instance_index(const LogicalResource& resource, const ResourceResolveContext& resolveContext) const noexcept;
        /// @brief 論理 buffer から物理 buffer 解決
        [[nodiscard]] Result resolve_buffer(BufferHandle logicalHandle, const ResourceResolveContext& resolveContext, BufferHandle& outHandle) const;
        /// @brief 論理 texture から物理 texture 解決
        [[nodiscard]] Result resolve_texture(TextureHandle logicalHandle, const ResourceResolveContext& resolveContext, TextureHandle& outHandle) const;
        /// @brief descriptor bind 解決
        [[nodiscard]] Result resolve_descriptor_binding(const DescriptorBindingDecl& binding, const ResourceResolveContext& resolveContext, FrameGraphContext::ResolvedDescriptorBinding& outBinding) const;
        /// @brief render target と depth view 解決
        [[nodiscard]] Result resolve_render_target_views(const std::vector<ResourceAccess>& accesses, const ResourceResolveContext& resolveContext, std::vector<ViewHandle>& outRenderTargetViews, ViewHandle& outDepthStencilView) const;
        /// @brief barrier 記述子生成
        [[nodiscard]] Result make_barrier_desc(const BarrierEvent& event, const ResourceResolveContext& resolveContext, ResourceBarrierDesc& outBarrier) const;

        /// @brief buffer handle 検証
        void validate_resource_handle(BufferHandle handle) const;
        /// @brief texture handle 検証
        void validate_resource_handle(TextureHandle handle) const;
        /// @brief resource 参照検証
        void validate_resource_ref(ResourceRef ref, ResourceKind expectedKind) const;
        /// @brief 論理 resource 取得
        [[nodiscard]] const LogicalResource& get_logical_resource(ResourceRef ref) const;
        /// @brief 初期 state 配列生成
        [[nodiscard]] std::vector<ResourceState> initial_resource_states() const;
        /// @brief resource 実体化
        [[nodiscard]] Result materialize_declared_resources();
        /// @brief pipeline 関連 handle 解決
        [[nodiscard]] Result resolve_pipeline_artifacts();
        /// @brief resource 依存順ソート
        [[nodiscard]] Result topological_sort_by_resource_dependencies(std::vector<size_t>& outSorted) const;
        /// @brief pass 並び替え
        void reorder_compiled_passes(const std::vector<size_t>& sortedOrder);
        /// @brief 実行依存構築
        void build_execution_dependencies();
        /// @brief barrier 構築
        void build_resource_barriers();
        /// @brief pipeline 宣言登録
        void declare_pipeline(PipelineBindingDecl& outDecl, const GraphicsPipelineStateDesc& desc);
        /// @brief pipeline 参照登録
        void reference_pipeline(PipelineBindingDecl& outDecl, std::string_view name);
        /// @brief root signature 宣言登録
        void declare_root_signature(RootSignatureBindingDecl& outDecl, const RootSignatureDesc& desc);
        /// @brief root signature 参照登録
        void reference_root_signature(RootSignatureBindingDecl& outDecl, std::string_view name);
        /// @brief shader 宣言登録
        void declare_shader(std::vector<ShaderBindingDecl>& outDecls, const ShaderCompileDesc& desc);
        /// @brief shader 参照登録
        void reference_shader(std::vector<ShaderBindingDecl>& outDecls, std::string_view name);
        /// @brief descriptor 宣言登録
        void declare_descriptor(std::vector<DescriptorBindingDecl>& outDecls, const DescriptorBindingDecl& desc);

    private:
        IBufferManager& m_bufferManager;
        ITextureManager& m_textureManager;
        IViewManager& m_viewManager;
        StaticMeshBufferPool& m_staticMeshBufferPool;
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
    /// @brief view manager 取得
    inline IViewManager& FrameGraphContext::view_manager() noexcept
    {
        return m_frameGraph.view_manager();
    }
    /// @brief buffer manager 取得
    inline IBufferManager& FrameGraphContext::buffer_manager() noexcept
    {
        return m_frameGraph.m_bufferManager;
    }
    /// @brief static mesh buffer pool 取得
    inline StaticMeshBufferPool& FrameGraphContext::static_mesh_buffer_pool() noexcept
    {
        return m_frameGraph.static_mesh_buffer_pool();
    }
    /// @brief 画面幅取得
    inline uint32_t FrameGraphContext::screen_width() const noexcept
    {
        return m_frameGraph.m_screenWidth;
    }
    /// @brief 画面高さ取得
    inline uint32_t FrameGraphContext::screen_height() const noexcept
    {
        return m_frameGraph.m_screenHeight;
    }
    inline Result FrameGraphContext::write_buffer(BufferHandle logicalHandle, uint64_t byteOffset, const void* data, uint32_t byteSize) const
    {
        // 1) 論理 handle を現在フレームの物理 buffer へ解決
        BufferHandle physicalHandle{};
        const Result resolveResult = resolve_buffer(logicalHandle, physicalHandle);
        if (!resolveResult)
        {
            return resolveResult;
        }

        // 2) cpu 書き込みを buffer manager へ移譲
        return m_frameGraph.m_bufferManager.write_buffer(physicalHandle, byteOffset, data, byteSize);
    }
} // 名前空間 cue::graphicscore
