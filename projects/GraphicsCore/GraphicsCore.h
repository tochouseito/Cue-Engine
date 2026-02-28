#pragma once
#include <Result.h>
#include "FrameGraph.h"

namespace Cue::GraphicsCore
{
    struct backend_setup_info final
    {
        uint32_t bufferCount = 2;
    };

    class Backend
    {
    public:
        Backend() = default;
        virtual ~Backend() = default;
        virtual Result initialize(const backend_setup_info& info) = 0;
        virtual Result shutdown() = 0;
        virtual [[nodiscard]] Result create_frame_graph_runtime(std::unique_ptr<IFrameGraphRuntime>* outRuntime) = 0;
    protected:
        std::unique_ptr<IFrameGraphRuntime> m_frameGraphRuntime = nullptr;
        std::unique_ptr<FrameGraph> m_frameGraph = nullptr;
    };
} // namespace Cue::GraphicsCore
