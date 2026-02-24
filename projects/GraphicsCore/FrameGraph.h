#pragma once

#include "BufferManager.h"
#include "TextureManager.h"

namespace Cue::GraphicsCore
{
    struct ResourceRef final
    {
        ResourceKind kind = ResourceKind::Buffer;
        uint32_t index = Handle<BufferTag>::k_invalid;
        uint32_t generation = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return index != Handle<BufferTag>::k_invalid;
        }

        bool operator==(const ResourceRef& other) const noexcept
        {
            return kind == other.kind && index == other.index && generation == other.generation;
        }
    };

    ResourceRef to_resource_ref(BufferHandle handle)
    {
        return ResourceRef{ ResourceKind::Buffer, handle.index, handle.generation };
    }

    ResourceRef to_resource_ref(TextureHandle handle)
    {
        return ResourceRef{ ResourceKind::Texture, handle.index, handle.generation };
    }

    struct ResourceAccess
    {
        ResourceRef handle{};
        ResourceAccessType type = ResourceAccessType::Read;
        ResourceState requiredState = ResourceState::Common;
        bool hasFinalState = false;
        ResourceState finalState = ResourceState::Common;
    };

    class FrameGraph;
    class BufferManager;

    class FrameGraphBuilder final
    {
    public:
        FrameGraphBuilder(FrameGraph& frameGraph, std::vector<ResourceAccess>& accesses)
            : m_frameGraph(frameGraph), m_accesses(accesses)
        {}

        BufferHandle create_buffer(std::string_view name);
        TextureHandle create_texture(std::string_view name);
        BufferHandle get_buffer(std::string_view name);
        TextureHandle get_texture(std::string_view name);
        void read(BufferHandle handle, ResourceState requiredState = ResourceState::ShaderResource);
        void read(TextureHandle handle, ResourceState requiredState = ResourceState::ShaderResource);
        void write(BufferHandle handle, ResourceState requiredState);
        void write(TextureHandle handle, ResourceState requiredState);
        void write(BufferHandle handle, ResourceState requiredState, ResourceState finalState);
        void write(TextureHandle handle, ResourceState requiredState, ResourceState finalState);

    private:
        FrameGraph& m_frameGraph;
        std::vector<ResourceAccess>& m_accesses;
    };

    class FrameGraphPass
    {
    public:
        virtual ~FrameGraphPass() = default;
        virtual const char* name() const = 0;
        virtual QueueType queue_type() const { return QueueType::Graphics; }
        virtual void setup(FrameGraphBuilder& builder) = 0;
        virtual void execute(ICommandContext& cmd) const = 0;
    };

    class FrameGraph final
    {
        friend class FrameGraphBuilder;
    public:
        FrameGraph(BufferManager& bufferManager, TextureManager& textureManager)
            : m_bufferManager(bufferManager), m_textureManager(textureManager)
        {}
        ~FrameGraph() = default;

        void add_pass(std::unique_ptr<FrameGraphPass> pass)
        {
            if (!pass)
            {
                throw std::runtime_error("add_pass: pass is null");
            }
            m_passes.push_back(CompiledPass{ std::move(pass), {} });
        }

        template <class TPass, class... Args,
            class = std::enable_if_t<std::is_base_of_v<FrameGraphPass, TPass>>>
        TPass& add_pass(Args&&... args)
        {
            auto pass = std::make_unique<TPass>(std::forward<Args>(args)...);
            TPass& ref = *pass;
            add_pass(std::move(pass));
            return ref;
        }

        bool build()
        {
            if (m_passes.empty())
            {
                return;
            }

            // 毎回 Pass 宣言から再構築し、編集後の再 build でも決定的な結果を保つ。
            m_resourceKinds.clear();
            m_resourceByNameId.clear();

            // 各 Pass の read/write 宣言を収集する。
            for (CompiledPass& compiled : m_passes)
            {
                compiled.accesses.clear();
                FrameGraphBuilder builder(*this, compiled.accesses);
                compiled.pass->setup(builder);
            }

            // 宣言済みの論理リソースに対して、模擬的な物理バッファを生成する。

            // データハザードに基づいて Pass 順を並べ替え、必要なバリアを事前計算する。

            return true;
        }

        void execute();

    private:
        struct CompiledPass
        {
            std::unique_ptr<FrameGraphPass> pass;
            std::vector<ResourceAccess> accesses;
        };

        struct PassExecutionInfo
        {
            QueueType queueType = QueueType::Graphics;
            uint64_t fenceValue = 0;
            bool submitted = false;
        };

        struct ResourceHazardState
        {
            int32_t lastWriter = -1;
            std::vector<int32_t> lastReaders;
        };

        struct BarrierEvent
        {
            ResourceRef handle{};
            ResourceState before = ResourceState::Common;
            ResourceState after = ResourceState::Common;
            bool beforePass = true;
        };
    private:
        std::vector<ResourceKind> m_resourceKinds;
        std::unordered_map<ResourceNameId, ResourceRef> m_resourceByNameId;
        std::vector<CompiledPass> m_passes;
        std::vector<std::vector<size_t>> m_dependenciesByPass;
        std::unordered_map<size_t, std::vector<BarrierEvent>> m_barriersByPass;

        BufferManager& m_bufferManager;
        TextureManager& m_textureManager;
    };
}
