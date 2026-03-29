#pragma once

// === RHI includes ===
#include "RHICommon.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ViewManager.h"
#include "PipelineManager.h"
#include "StaticMeshPool.h"

// === C++ includes ===
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace Cue::RHI
{
    // FrameGraph 内で登録した論理 resource を指す軽量参照です。
    struct FrameGraphResourceRef final
    {
        uint32_t index = UINT32_MAX;

        bool valid() const noexcept
        {
            return index != UINT32_MAX;
        }
    };

    // FrameGraph 内で生成した論理 view を指す軽量参照です。
    struct FrameGraphViewRef final
    {
        uint32_t index = UINT32_MAX;

        bool valid() const noexcept
        {
            return index != UINT32_MAX;
        }
    };

    enum class LoadOp : uint8_t
    {
        Load,
        Clear,
        DontCare
    };

    enum class StoreOp : uint8_t
    {
        Store,
        DontCare
    };

    class FrameGraph;
    class FrameGraphBuilder;
    class FrameGraphPassContext;

    // Pass は setup で宣言だけを行い、execute で実際のコマンド記録を行います。
    class FrameGraphPass
    {
    public:
        virtual ~FrameGraphPass() = default;

        virtual std::string_view name() const noexcept = 0;
        virtual CommandListType type() const noexcept = 0;
        virtual Result setup(FrameGraphBuilder& builder) = 0;
        virtual Result execute(FrameGraphPassContext& context);
        virtual bool side_effect() const noexcept
        {
            return false;
        }
    };

    // Builder は pass 登録中だけ使う宣言用 API です。
    // resource / view / access / attachment / pipeline の要求を FrameGraph へ積み込みます。
    class FrameGraphBuilder final
    {
    public:
        FrameGraphBuilder(FrameGraph& frameGraph, uint32_t passIndex) :
            m_frameGraph(frameGraph),
            m_passIndex(passIndex)
        {
        }

        FrameGraphResourceRef import_buffer(std::string_view name, BufferHandle handle);
        FrameGraphResourceRef import_texture(std::string_view name, TextureHandle handle, bool presentable = false);
        FrameGraphResourceRef create_transient_buffer(std::string_view name, const BufferDesc& desc);
        FrameGraphResourceRef create_transient_texture(std::string_view name, const TextureDesc& desc);
        FrameGraphResourceRef backbuffer_texture() const noexcept;

        FrameGraphViewRef create_buffer_view(FrameGraphResourceRef resource, const ViewDesc& desc);
        FrameGraphViewRef create_texture_view(FrameGraphResourceRef resource, const ViewDesc& desc);

        void read(FrameGraphViewRef view);
        void write(FrameGraphViewRef view);
        void set_render_target(
            FrameGraphViewRef view,
            LoadOp loadOp,
            StoreOp storeOp,
            const std::array<float, 4>& clearColor = { 0.0f, 0.0f, 0.0f, 1.0f });
        void set_depth_stencil(
            FrameGraphViewRef view,
            LoadOp loadOp,
            StoreOp storeOp,
            float clearDepth = 1.0f,
            uint8_t clearStencil = 0);
        void set_side_effect(bool enabled = true);

        void set_graphics_pipeline(PipelineStateHandle handle);
        void set_graphics_pipeline(const GraphicsPipelineStateDesc& desc);
        void set_compute_pipeline(PipelineStateHandle handle);
        void set_compute_pipeline(const ComputePipelineStateDesc& desc);
    private:
        FrameGraph& m_frameGraph;
        uint32_t m_passIndex = UINT32_MAX;
    };

    // PassContext は build 済み graph から実体化済みハンドルを引く実行用 API です。
    class FrameGraphPassContext
    {
    public:
        uint32_t frame_index() const noexcept
        {
            return m_frameIndex;
        }

        ICommandContext& command_context() const noexcept
        {
            return *m_commandContext;
        }

        ViewHandle resolve_view(FrameGraphViewRef view) const;
        PipelineStateHandle pipeline() const;
        IStaticMeshPool* static_mesh_pool() const noexcept;
        CommandListType type() const noexcept;
        std::vector<ViewHandle> render_target_views() const;
        ViewHandle depth_stencil_view() const;
        Result clear_render_target(FrameGraphViewRef view, const std::array<float, 4>& clearColor) const;

    protected:
        FrameGraphPassContext(FrameGraph& frameGraph, uint32_t passIndex, uint32_t frameIndex, ICommandContext& commandContext) :
            m_frameGraph(frameGraph),
            m_passIndex(passIndex),
            m_frameIndex(frameIndex),
            m_commandContext(&commandContext)
        {
        }

        friend class FrameGraph;

        FrameGraph& m_frameGraph;
        uint32_t m_passIndex = UINT32_MAX;
        uint32_t m_frameIndex = 0;
        ICommandContext* m_commandContext = nullptr;
    };

    // FrameGraph は pass の宣言を受け取り、resource と view を実体化し、
    // access 関係から依存順を組み立てて実行する調停層です。
    class FrameGraph final
    {
    public:
        FrameGraph(
            IBufferManager* bufferManager,
            ITextureManager* textureManager,
            IViewManager* viewManager,
            IPipelineManager* pipelineManager,
            IStaticMeshPool* staticMeshPool,
            ICommandPool* commandPool,
            IQueuePool* queuePool,
            TextureHandle swapChainTextureHandle = {}) :
            m_bufferManager(bufferManager),
            m_textureManager(textureManager),
            m_viewManager(viewManager),
            m_pipelineManager(pipelineManager),
            m_staticMeshPool(staticMeshPool),
            m_commandPool(commandPool),
            m_queuePool(queuePool)
        {
            if (swapChainTextureHandle.valid())
            {
                m_swapChainBackBuffer = import_texture_internal("SwapChainBackBuffer", swapChainTextureHandle, true);
            }
        }

        FrameGraph(const FrameGraph&) = delete;
        FrameGraph& operator=(const FrameGraph&) = delete;
        FrameGraph(FrameGraph&&) = delete;
        FrameGraph& operator=(FrameGraph&&) = delete;
        ~FrameGraph();

        Result add_pass(std::unique_ptr<FrameGraphPass> pass);

        FrameGraphResourceRef import_buffer(std::string_view name, BufferHandle handle);
        FrameGraphResourceRef import_texture(std::string_view name, TextureHandle handle, bool presentable = false);
        FrameGraphResourceRef create_transient_buffer(std::string_view name, const BufferDesc& desc);
        FrameGraphResourceRef create_transient_texture(std::string_view name, const TextureDesc& desc);
        FrameGraphResourceRef backbuffer_texture() const noexcept
        {
            return m_swapChainBackBuffer;
        }

        Result build();
        Result execute(uint32_t frameIndex);

    private:
        // 論理 resource は import / transient の別と、最終的に解決される handle を保持します。
        enum class LogicalResourceKind : uint8_t
        {
            Buffer,
            Texture
        };

        struct LogicalResource final
        {
            std::string name = {};
            LogicalResourceKind kind = LogicalResourceKind::Buffer;
            bool imported = false;
            bool presentable = false;
            BufferDesc bufferDesc = {};
            TextureDesc textureDesc = {};
            BufferHandle bufferHandle = {};
            TextureHandle textureHandle = {};
        };

        // 論理 view はどの resource のどの範囲をどう見るかだけを保持し、
        // build 時に backend view handle へ materialize されます。
        struct LogicalView final
        {
            uint32_t resourceIndex = UINT32_MAX;
            ViewDesc desc = {};
            ViewHandle handle = {};
        };

        // access record は pass が view を読むか書くかと、その用途から導いた要求 state を保持します。
        struct AccessRecord final
        {
            uint32_t viewIndex = UINT32_MAX;
            bool isWrite = false;
            ResourceState requiredState = ResourceState::Common;
        };

        // render target / depth stencil は通常 access と別に attachment 情報を持ちます。
        struct RenderTargetBinding final
        {
            uint32_t viewIndex = UINT32_MAX;
            LoadOp loadOp = LoadOp::Load;
            StoreOp storeOp = StoreOp::Store;
            std::array<float, 4> clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        };

        struct DepthStencilBinding final
        {
            uint32_t viewIndex = UINT32_MAX;
            LoadOp loadOp = LoadOp::Load;
            StoreOp storeOp = StoreOp::Store;
            float clearDepth = 1.0f;
            uint8_t clearStencil = 0;
        };

        // pipeline request は外部 handle 利用か、FrameGraph 所有で build 時に生成するかを表します。
        struct PipelineRequest final
        {
            enum class Kind : uint8_t
            {
                None,
                GraphicsExternal,
                GraphicsOwned,
                ComputeExternal,
                ComputeOwned
            };

            Kind kind = Kind::None;
            PipelineStateHandle handle = {};
            GraphicsPipelineStateDesc graphicsDesc = {};
            ComputePipelineStateDesc computeDesc = {};
        };

        // pass record は setup で宣言した内容をまとめ、build と execute の両方で参照します。
        struct PassRecord final
        {
            std::string name = {};
            CommandListType type = CommandListType::Graphics;
            bool sideEffect = false;
            uint32_t registrationIndex = 0;
            std::unique_ptr<FrameGraphPass> pass = nullptr;
            std::vector<AccessRecord> accesses = {};
            std::vector<RenderTargetBinding> renderTargets = {};
            std::optional<DepthStencilBinding> depthStencil = std::nullopt;
            PipelineRequest pipeline = {};
            std::vector<uint32_t> dependencies = {};
        };

        // build の前半では論理宣言を実体へ変換し、後半で依存と実行順を確定します。
        FrameGraphResourceRef import_buffer_internal(std::string_view name, BufferHandle handle);
        FrameGraphResourceRef import_texture_internal(std::string_view name, TextureHandle handle, bool presentable);
        FrameGraphResourceRef create_transient_buffer_internal(std::string_view name, const BufferDesc& desc);
        FrameGraphResourceRef create_transient_texture_internal(std::string_view name, const TextureDesc& desc);
        FrameGraphViewRef create_view_internal(uint32_t passIndex, FrameGraphResourceRef resource, const ViewDesc& desc, LogicalResourceKind expectedKind);
        void register_access(uint32_t passIndex, FrameGraphViewRef view, bool isWrite);
        void register_render_target(
            uint32_t passIndex,
            FrameGraphViewRef view,
            LoadOp loadOp,
            StoreOp storeOp,
            const std::array<float, 4>& clearColor);
        void register_depth_stencil(
            uint32_t passIndex,
            FrameGraphViewRef view,
            LoadOp loadOp,
            StoreOp storeOp,
            float clearDepth,
            uint8_t clearStencil);

        Result add_pass_common(std::unique_ptr<FrameGraphPass> pass);
        Result cleanup_built_state();
        Result materialize_resources();
        Result materialize_views();
        Result materialize_pipelines();
        Result build_dependencies();
        Result validate_pass(const PassRecord& pass) const;
        Result build_execution_order();

        ResourceState resolve_required_state(const PassRecord& pass, const LogicalView& view, bool isWrite) const;
        bool views_overlap(const LogicalView& lhs, const LogicalView& rhs) const;
        static bool ranges_overlap(uint64_t lhsBegin, uint64_t lhsEnd, uint64_t rhsBegin, uint64_t rhsEnd);
        static size_t command_list_type_index(CommandListType type);

        friend class FrameGraphBuilder;
        friend class FrameGraphPassContext;

    private:
        // manager 群は build/execute 時の実体解決先です。
        IBufferManager* m_bufferManager = nullptr;
        ITextureManager* m_textureManager = nullptr;
        IViewManager* m_viewManager = nullptr;
        IPipelineManager* m_pipelineManager = nullptr;
        IStaticMeshPool* m_staticMeshPool = nullptr;
        ICommandPool* m_commandPool = nullptr;
        IQueuePool* m_queuePool = nullptr;

        // ここから下は pass 登録で蓄積した宣言と、build 後に得られる実行計画です。
        std::vector<LogicalResource> m_resources = {};
        std::vector<LogicalView> m_views = {};
        std::vector<PassRecord> m_passes = {};
        std::vector<uint32_t> m_executionOrder = {};
        FrameGraphResourceRef m_swapChainBackBuffer = {};
        bool m_isBuilt = false;
    };
}
