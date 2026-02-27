#pragma once

#include "BufferManager.h"
#include "TextureManager.h"

#include <type_traits>
#include <utility>

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
            return (kind == other.kind) && (index == other.index) && (generation == other.generation);
        }
    };

    [[nodiscard]] inline ResourceRef to_resource_ref(BufferHandle handle) noexcept
    {
        return ResourceRef{ ResourceKind::Buffer, handle.index, handle.generation };
    }

    [[nodiscard]] inline ResourceRef to_resource_ref(TextureHandle handle) noexcept
    {
        return ResourceRef{ ResourceKind::Texture, handle.index, handle.generation };
    }

    struct ResourceAccess final
    {
        ResourceRef handle{};
        ResourceAccessType type = ResourceAccessType::Read;
        ResourceState requiredState = ResourceState::Common;
        bool hasFinalState = false;
        ResourceState finalState = ResourceState::Common;
    };

    struct ImportedBufferDesc final
    {
        std::string_view name{};
        BufferHandle sourceHandle{};
        CommandListType ownerQueue = CommandListType::Graphics;
        ResourceState initialState = ResourceState::Common;
        bool hasFinalState = false;
        ResourceState finalState = ResourceState::Common;
        bool hasInitialSyncPoint = false;
        QueueSyncPoint initialSyncPoint{};
    };

    struct ImportedTextureDesc final
    {
        std::string_view name{};
        TextureHandle sourceHandle{};
        CommandListType ownerQueue = CommandListType::Graphics;
        ResourceState initialState = ResourceState::Common;
        bool hasFinalState = false;
        ResourceState finalState = ResourceState::Common;
        bool hasInitialSyncPoint = false;
        QueueSyncPoint initialSyncPoint{};
    };

    class IFrameGraphRuntime
    {
    public:
        virtual ~IFrameGraphRuntime() = default;
        [[nodiscard]] virtual IQueueContext* get_queue_context(CommandListType queueType) = 0;
        [[nodiscard]] virtual ICommandContext* acquire_pass_command_context(CommandListType queueType, size_t passIndex) = 0;
    };

    class FrameGraph;

    class FrameGraphBuilder final
    {
    public:
        FrameGraphBuilder(FrameGraph& frameGraph, std::vector<ResourceAccess>& accesses) noexcept
            : m_frameGraph(frameGraph), m_accesses(accesses)
        {
        }

        [[nodiscard]] BufferHandle create_buffer(std::string_view name);
        [[nodiscard]] TextureHandle create_texture(std::string_view name);
        [[nodiscard]] BufferHandle import_buffer(const ImportedBufferDesc& desc);
        [[nodiscard]] TextureHandle import_texture(const ImportedTextureDesc& desc);
        [[nodiscard]] BufferHandle get_buffer(std::string_view name);
        [[nodiscard]] TextureHandle get_texture(std::string_view name);

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
        [[nodiscard]] virtual const char* name() const = 0;
        [[nodiscard]] virtual CommandListType queue_type() const
        {
            return CommandListType::Graphics;
        }
        virtual void setup(FrameGraphBuilder& builder) = 0;
        virtual void execute(ICommandContext& cmd) const = 0;
    };

    class FrameGraph final
    {
        friend class FrameGraphBuilder;

    public:
        FrameGraph(BufferManager& bufferManager, TextureManager& textureManager) noexcept
            : m_bufferManager(bufferManager), m_textureManager(textureManager)
        {
        }
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
        [[nodiscard]] Result execute(IFrameGraphRuntime& runtime);

    private:
        struct CompiledPass final
        {
            std::unique_ptr<FrameGraphPass> pass;
            std::vector<ResourceAccess> accesses;
            CommandListType queueType = CommandListType::Graphics;
            size_t originalIndex = 0;
        };

        struct LogicalResourceInfo final
        {
            ResourceKind kind = ResourceKind::Buffer;
            bool imported = false;
            CommandListType ownerQueue = CommandListType::Graphics;
            ResourceState initialState = ResourceState::Common;
            bool hasFinalState = false;
            ResourceState finalState = ResourceState::Common;
            bool hasInitialSyncPoint = false;
            QueueSyncPoint initialSyncPoint{};
            int32_t firstAccessPass = -1;
            int32_t lastAccessPass = -1;
            BufferHandle importedBufferHandle{};
            TextureHandle importedTextureHandle{};
        };

        struct ResourceHazardState final
        {
            int32_t lastWriter = -1;
            std::vector<int32_t> lastReaders;
        };

        struct BarrierEvent final
        {
            ResourceBarrierDesc barrier{};
            bool beforePass = true;
        };

        struct QueueWaitEvent final
        {
            bool isExternalSyncPoint = false;
            size_t sourcePassIndex = 0;
            CommandListType sourceQueue = CommandListType::Graphics;
            CommandListType waitQueue = CommandListType::Graphics;
            QueueSyncPoint externalSyncPoint{};
        };

        struct PassExecutionPlan final
        {
            CommandListType queueType = CommandListType::Graphics;
            std::vector<size_t> dependencies;
            std::vector<QueueWaitEvent> waitEvents;
            std::vector<BarrierEvent> preBarriers;
            std::vector<BarrierEvent> postBarriers;
        };

    private:
        [[nodiscard]] Result create_named_resource(ResourceKind kind, std::string_view name, ResourceRef& outRef);
        [[nodiscard]] Result import_named_buffer(const ImportedBufferDesc& desc, ResourceRef& outRef);
        [[nodiscard]] Result import_named_texture(const ImportedTextureDesc& desc, ResourceRef& outRef);
        [[nodiscard]] Result get_named_resource(ResourceKind kind, std::string_view name, ResourceRef& outRef) const;
        [[nodiscard]] Result validate_resource_ref(const ResourceRef& ref) const;
        [[nodiscard]] Result push_access(const ResourceAccess& access, std::vector<ResourceAccess>& accesses);

        void set_setup_error(Result result) noexcept;
        [[nodiscard]] bool has_setup_error() const noexcept;
        void reset_build_artifacts();

        [[nodiscard]] Result collect_pass_declarations();
        [[nodiscard]] Result compile_dependencies();
        [[nodiscard]] Result compile_execution_order();
        [[nodiscard]] Result compile_queue_waits();
        [[nodiscard]] Result compile_barriers();
        [[nodiscard]] Result issue_barriers(ICommandContext& cmd, const std::vector<BarrierEvent>& barriers);

    private:
        std::vector<ResourceKind> m_resourceKinds;
        std::vector<LogicalResourceInfo> m_resourceInfos;
        std::unordered_map<ResourceNameId, ResourceRef> m_resourceByNameId;
        std::vector<CompiledPass> m_passes;
        std::vector<PassExecutionPlan> m_plansByPass;
        std::vector<size_t> m_executionOrder;
        std::vector<QueueSyncPoint> m_signalPointByPass;
        Result m_setupError = Result::ok();
        bool m_isBuilt = false;

        BufferManager& m_bufferManager;
        TextureManager& m_textureManager;
    };
} // namespace Cue::GraphicsCore
