#pragma once

// === RHI includes ===
#include "RHICommon.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ViewManager.h"
#include "PipelineManager.h"
#include "StaticMeshPool.h"

namespace Cue::RHI
{
    enum class ResourceAccessType : uint8_t
    {
        Read,
        Write,
        ReadWrite
    };

    // setup() で記録する access 宣言
    // build() で依存関係と state 遷移を導出
    struct ResourceAccess final
    {
    };

    class FrameGraph;

    class FrameGraphBuilder final
    {
    public:
        FrameGraphBuilder(FrameGraph& frameGraph) : m_frameGraph(frameGraph) {}

        /// @brief buffer 作成宣言
        Result create_buffer(const BufferDesc& desc, bufferHandle& out);
        /// @brief texture 作成宣言
        Result create_texture(const TextureDesc& desc, textureHandle& out);
        /// @brief 宣言済み buffer 取得
        Result get_buffer(std::string_view name, bufferHandle& out);
        /// @brief 宣言済み texture 取得
        Result get_texture(std::string_view name, textureHandle& out);
        /// @brief view 作成宣言
        Result create_view(const ViewDesc& desc, viewHandle& out);
        /// @brief 宣言済み view 取得
        Result get_view(std::string_view name, viewHandle& out);
        /// @brief ルートシグネチャ作成宣言
        Result create_root_signature(const RootSignatureDesc& desc, rootSignatureHandle& out);
        /// @brief 宣言済みルートシグネチャ取得
        Result get_root_signature(std::string_view name, rootSignatureHandle& out);
        /// @brief シェーダーブロブ作成宣言
        Result create_shader_blob(const ShaderCompileDesc& desc, shaderBlobHandle& out);
        /// @brief 宣言済みシェーダーブロブ取得
        Result get_shader_blob(std::string_view name, shaderBlobHandle& out);
        /// @brief グラフィックスパイプライン作成宣言
        Result create_graphics_pipeline(const GraphicsPipelineStateDesc& desc, pipelineStateHandle& out);
        /// @brief 宣言済みグラフィックスパイプライン取得
        Result get_graphics_pipeline(std::string_view name, pipelineStateHandle& out);
        /// @brief コンピュートパイプライン作成宣言
        Result create_compute_pipeline(const ComputePipelineStateDesc& desc, pipelineStateHandle& out);
        /// @brief 宣言済みコンピュートパイプライン取得
        Result get_compute_pipeline(std::string_view name, pipelineStateHandle& out);

        /// @brief render target 書き込み宣言
        Result render(const textureHandle* handles, size_t count);

        uint32_t width() const noexcept;
        uint32_t height() const noexcept;
        const uint32_t& buffer_count() const noexcept;
    private:
        FrameGraph& m_frameGraph;
        std::vector<textureHandle> m_renderTargets;
    };

    struct FrameGraphContextDesc final
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t frameIndex = 0;
        ICommandContext* commandContext = nullptr;
    };

    class FrameGraphContext final
    {
    public:
        FrameGraphContext(const FrameGraphContextDesc& desc) : m_desc(desc) {}
        ~FrameGraphContext() = default;

        const uint32_t width() const noexcept { return m_desc.width; }
        const uint32_t height() const noexcept { return m_desc.height; }
        ICommandContext* commandContext() const noexcept { return m_desc.commandContext; }
    private:
        FrameGraphContextDesc m_desc{};
    };

    class FrameGraphPass
    {
    public:
        virtual ~FrameGraphPass() = default;
        virtual const char* name() const noexcept = 0;
        virtual CommandListType type() const noexcept = 0;
        virtual Result setup(FrameGraphBuilder& builder) = 0;
        virtual void execute(FrameGraphContext& context) = 0;
    };

    struct FrameGraphDesc final
    {
        IBufferManager* bufferManager = nullptr;
        ITextureManager* textureManager = nullptr;
        IViewManager* viewManager = nullptr;
        IPipelineManager* pipelineManager = nullptr;
        IStaticMeshPool* staticMeshPool = nullptr;
        ICommandPool* commandPool = nullptr;
        IQueuePool* queuePool = nullptr;
        bool usePresentQueue = true;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// @brief フレーム単位の描画依存関係を保持するプレースホルダーです。
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
        ~FrameGraph() = default;

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
        /// @brief 描画依存関係を構築します。
        Result build();
        /// @brief パスの実行
        Result execute(uint32_t frameIndex);
    private:
        struct CompiledPass final
        {
            std::unique_ptr<FrameGraphPass> pass = nullptr;
        };
    private:
        FrameGraphDesc m_desc;
        const uint32_t& m_bufferCount;
        std::vector<CompiledPass> m_passes;
    };
}
