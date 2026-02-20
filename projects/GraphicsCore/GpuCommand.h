#pragma once
#include <cstdint>
#include <Result.h>

namespace Cue::GraphicsCore
{
    class CommandContext
    {
    public:
        CommandContext() = default;
        virtual ~CommandContext() = default;

        // Commands
        void begin_event(const char* name) {}
        void end_event() {}

        void set_render_targets() {}
        void clear_render_target() {}
        void set_depth_stencil() {}
        void clear_depth_stencil() {}
        void set_viewport() {}
        void set_scissor_rect() {}
        void bind_pipeline() {}
        void bind_descriptors() {}
    };

    enum class QueueType : uint8_t
    {
        Graphics,
        Compute,
        Copy
    };

    class QueueContext
    {
    public:
        QueueContext() = default;
        virtual ~QueueContext() = default;
        virtual Result submit(CommandContext* ctx, uint64_t signalFenceValue) = 0;
        virtual Result wait(uint64_t fenceValue) = 0;
    };
}
