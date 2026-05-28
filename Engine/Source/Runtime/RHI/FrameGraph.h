// FrameGraph の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include "RHICommon.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ViewManager.h"
#include "PipelineManager.h"

// === C++ includes ===
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace Cue::RHI
{
    enum class ResourceKind : uint8_t
    {
        Buffer,
        Texture
    };

    enum class ResourceAccessType : uint8_t
    {
        Read,
        Write,
        ReadWrite
    };

    struct ResourceId final
    {
        ResourceKind kind = ResourceKind::Buffer;
        uint32_t index = Core::Handle<Core::TestTag>::k_invalid;
        uint32_t generation = 0;

        bool valid() const noexcept
        {
            return index != Core::Handle<Core::TestTag>::k_invalid;
        }

        bool operator==(const ResourceId& a_other) const noexcept
        {
            return kind == a_other.kind &&
                index == a_other.index &&
                generation == a_other.generation;
        }
    };

    struct ResourceIdHasher final
    {
        size_t operator()(const ResourceId& a_id) const noexcept
        {
            const uint64_t kindBits =
                static_cast<uint64_t>(static_cast<uint8_t>(a_id.kind)) << 63;
            const uint64_t indexBits = static_cast<uint64_t>(a_id.index) << 31;
            const uint64_t generationBits = static_cast<uint64_t>(a_id.generation);
            return static_cast<size_t>(kindBits ^ indexBits ^ generationBits);
        }
    };

    // setup() 後に記録する access 宣言
    // build() で依存関係と state 遷移を導出
    struct ResourceAccess final
    {
        ResourceId resourceId{};
        ResourceAccessType accessType = ResourceAccessType::Read;
        ResourceState requiredState = ResourceState::Common;
        ResourceState finalState = ResourceState::Common;
    };

    struct PassBuildInfo;
    class FrameGraph;

    class FrameGraphBuilder final
    {
    public:
        FrameGraphBuilder(FrameGraph& frameGraph, PassBuildInfo* buildInfo = nullptr)
            : m_frameGraph(frameGraph)
            , m_buildInfo(buildInfo)
        {}

        /// @brief buffer 作成宣言
        Result create_buffer(const BufferDesc& desc, BufferHandle& out);
        /// @brief texture 作成宣言
        Result create_texture(const TextureDesc& desc, TextureHandle& out);
        /// @brief 宣言済み buffer 取得
        Result get_buffer(std::string_view name, BufferHandle& out);
        /// @brief 宣言済み texture 取得
        Result get_texture(std::string_view name, TextureHandle& out);
        /// @brief view 作成宣言
        Result create_view(const ViewDesc& desc, ViewHandle& out);
        /// @brief 宣言済み view 取得
        Result get_view(std::string_view name, ViewHandle& out);
        /// @brief ルートシグネチャ作成宣言
        Result create_root_signature(const RootSignatureDesc& desc, RootSignatureHandle& out);
        /// @brief 宣言済みルートシグネチャ取得
        Result get_root_signature(std::string_view name, RootSignatureHandle& out);
        /// @brief シェーダーブロブ作成宣言
        Result create_shader_blob(const ShaderCompileDesc& desc, ShaderBlobHandle& out);
        /// @brief 宣言済みシェーダーブロブ取得
        Result get_shader_blob(std::string_view name, ShaderBlobHandle& out);
        /// @brief グラフィックスパイプライン作成宣言
        Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, PipelineStateHandle& out);
        /// @brief 宣言済みグラフィックスパイプライン取得
        Result get_graphics_pipeline(std::string_view name, PipelineStateHandle& out);
        /// @brief コンピュートパイプライン作成宣言
        Result create_compute_pipeline(const ComputePipelineStateDesc& desc, PipelineStateHandle& out);
        /// @brief 宣言済みコンピュートパイプライン取得
        Result get_compute_pipeline(std::string_view name, PipelineStateHandle& out);

        /// @brief render target 書き込み宣言
        Result render(const TextureHandle* handles, size_t count);

        Result read_buffer(BufferHandle handle);
        Result write_buffer(BufferHandle handle);
        Result read_write_buffer(BufferHandle handle);
        Result read_texture(TextureHandle handle);
        Result write_texture(TextureHandle handle);
        Result read_write_texture(TextureHandle handle);
        Result use_buffer(
            BufferHandle handle,
            ResourceAccessType accessType,
            ResourceState requiredState,
            ResourceState finalState);
        Result use_texture(
            TextureHandle handle,
            ResourceAccessType accessType,
            ResourceState requiredState,
            ResourceState finalState);

        uint32_t width() const noexcept;
        uint32_t height() const noexcept;
        const uint32_t& buffer_count() const noexcept;
    private:
        Result register_buffer_access(BufferHandle handle, ResourceAccessType accessType);
        Result register_texture_access(TextureHandle handle, ResourceAccessType accessType);
        Result register_buffer_access(
            BufferHandle handle,
            ResourceAccessType accessType,
            ResourceState requiredState,
            ResourceState finalState);
        Result register_texture_access(
            TextureHandle handle,
            ResourceAccessType accessType,
            ResourceState requiredState,
            ResourceState finalState);

        FrameGraph& m_frameGraph;
        PassBuildInfo* m_buildInfo = nullptr;
        std::vector<TextureHandle> m_renderTargets;
    };

    struct FrameGraphContextDesc final
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t frameIndex = 0;
        ICommandContext* commandContext = nullptr;
        void* passStats = nullptr;
    };

    class FrameGraphContext final
    {
    public:
        FrameGraphContext(const FrameGraphContextDesc& desc) : m_desc(desc) {}
        ~FrameGraphContext() = default;

        const uint32_t width() const noexcept { return m_desc.width; }
        const uint32_t height() const noexcept { return m_desc.height; }
        const uint32_t frame_index() const noexcept { return m_desc.frameIndex; }
        ICommandContext* commandContext() const noexcept { return m_desc.commandContext; }
        void* pass_stats() const noexcept { return m_desc.passStats; }
    private:
        FrameGraphContextDesc m_desc{};
    };

    class FrameGraphPass
    {
    public:
        virtual ~FrameGraphPass() = default;
        virtual const char* name() const noexcept = 0;
        virtual CommandListType type() const noexcept = 0;
        virtual bool is_enabled(uint32_t a_frameIndex) const noexcept
        {
            a_frameIndex;
            return true;
        }
        virtual Result setup(FrameGraphBuilder& builder) = 0;
        virtual Result describe_resources(FrameGraphBuilder& builder) = 0;
        virtual void execute(FrameGraphContext& context) = 0;
    };

    struct PassBuildInfo final
    {
        std::string_view name{};
        CommandListType queueType = CommandListType::Graphics;
        std::vector<ResourceAccess> resourceAccesses{};
        std::vector<uint32_t> dependencyPassIndices{};
    };

    struct QueueBatchInfo final
    {
        CommandListType queueType = CommandListType::Graphics;
        std::vector<uint32_t> passIndices{};
        std::vector<uint32_t> waitBatchIndices{};
    };

    struct FrameGraphExecutionStats final
    {
        struct PassExecutionStats final
        {
            struct DetailTiming final
            {
                std::string label{};
                double elapsedMs = 0.0;
            };

            std::string_view name{};
            CommandListType queueType = CommandListType::Graphics;
            double acquireResetSetupMs = 0.0;
            double preBarrierMs = 0.0;
            double cpuExecuteMs = 0.0;
            double postBarrierMs = 0.0;
            double closeMs = 0.0;
            double submitExecuteListsMs = 0.0;
            double submitSignalOnlyMs = 0.0;
            double submitSignalMs = 0.0;
            uint32_t submittedCommandListCount = 0;
            bool hasGpuExecuteMs = false;
            double gpuExecuteMs = 0.0;
            std::vector<DetailTiming> detailTimings{};
        };

        double totalExecuteMs = 0.0;
        double submitMs = 0.0;
        double queueWaitMs = 0.0;
        double interQueueWaitMs = 0.0;
        double finalQueueWaitMs = 0.0;
        double contextRecycleWaitMs = 0.0;
        double finalGraphicsWaitMs = 0.0;
        double finalComputeWaitMs = 0.0;
        double finalCopyWaitMs = 0.0;
        uint64_t graphicsFenceValue = 0;
        uint64_t computeFenceValue = 0;
        uint64_t copyFenceValue = 0;
        bool hasGpuFrameMs = false;
        double gpuFrameMs = 0.0;
        std::vector<PassExecutionStats> passStats{};
    };

    struct FrameGraphDesc final
    {
        IBufferManager* bufferManager = nullptr;
        ITextureManager* textureManager = nullptr;
        IViewManager* viewManager = nullptr;
        IPipelineManager* pipelineManager = nullptr;
        ICommandPool* commandPool = nullptr;
        IQueuePool* queuePool = nullptr;
        bool usePresentQueue = true;
        bool enableProfiling = false;
        bool waitForCompletion = false;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// @brief フレーム単位の描画依存関係を保持するプレースホルダー
    class FrameGraph final
    {
        friend class FrameGraphBuilder;
        friend class FrameGraphContext;
    public:
        FrameGraph(const FrameGraphDesc& desc, const uint32_t& bufferCount);
        // コピー禁止
        FrameGraph(const FrameGraph&) = delete;
        FrameGraph& operator=(const FrameGraph&) = delete;
        // ムーブ禁止
        FrameGraph(FrameGraph&&) = delete;
        FrameGraph& operator=(FrameGraph&&) = delete;
        ~FrameGraph();

        /// @brief パスの追加
        void add_pass(std::unique_ptr<FrameGraphPass> pass)
        {
            // null pass は受け付けない
            if (pass == nullptr)
            {
                return;
            }

            m_passes.push_back(CompiledPass{ std::move(pass) });
        }
        /// @brief 描画依存関係を構築する
        Result build();
        /// @brief サイズを更新して再構築する
        Result rebuild(uint32_t a_width, uint32_t a_height);
        /// @brief パスの実行
        Result execute(uint32_t frameIndex);

        const std::vector<PassBuildInfo>& pass_build_infos() const noexcept
        {
            return m_passBuildInfos;
        }

        FrameGraphExecutionStats execution_stats_copy() const noexcept;
        FrameGraphExecutionStats execution_stats_summary_copy() const noexcept;
    private:
        Result cleanup_build_resources();
        Result build_dependencies();
        Result validate_dependency_graph() const;
        Result build_execution_plan();

        struct CompiledPass final
        {
            std::unique_ptr<FrameGraphPass> pass = nullptr;
            PassBuildInfo buildInfo{};
        };
    private:
        static ResourceId make_resource_id(BufferHandle handle) noexcept;
        static ResourceId make_resource_id(TextureHandle handle) noexcept;
        static BufferHandle make_buffer_handle(const ResourceId& resourceId) noexcept;
        static TextureHandle make_texture_handle(const ResourceId& resourceId) noexcept;
        static ResourceAccessType merge_access_type(
            ResourceAccessType current,
            ResourceAccessType incoming) noexcept;
        static bool has_dependency(
            ResourceAccessType previous,
            ResourceAccessType next) noexcept;

        FrameGraphDesc m_desc;
        const uint32_t& m_bufferCount;
        std::vector<CompiledPass> m_passes;
        std::vector<PassBuildInfo> m_passBuildInfos;
        std::vector<QueueBatchInfo> m_executionPlan;
        mutable std::mutex m_executionStatsMutex{};
        FrameGraphExecutionStats m_executionStats{};
        std::vector<BufferHandle> m_createdBuffers;
        std::vector<TextureHandle> m_createdTextures;
        std::vector<ViewHandle> m_createdViews;
        std::vector<RootSignatureHandle> m_createdRootSignatures;
        std::vector<ShaderBlobHandle> m_createdShaderBlobs;
        std::vector<PipelineStateHandle> m_createdGraphicsPipelines;
        std::vector<PipelineStateHandle> m_createdComputePipelines;
    };
}
