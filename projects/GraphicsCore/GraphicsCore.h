#pragma once
#include <Result.h>
#include "FrameGraph.h"
#include "StaticMeshBufferPool.h"
#include "ViewManager.h"

namespace Cue::GraphicsCore
{
    struct backend_setup_info final
    {
        uint32_t bufferCount;
        uint32_t screenWidth;
        uint32_t screenHeight;
        StaticMeshBufferPoolDesc staticMeshBufferPoolDesc{};
    };

    class Backend
    {
    public:
        Backend() = default;
        virtual ~Backend() = default;
        virtual Result initialize(const backend_setup_info& info) = 0;
        virtual Result shutdown() = 0;
        virtual Result render(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph) = 0;
        virtual Result present(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph) = 0;
        virtual Result create_frame_graph(std::unique_ptr<FrameGraph>& outFG) = 0;
        virtual StaticMeshBufferPool* get_static_mesh_buffer_pool() = 0;
    protected:
        backend_setup_info m_setupInfo{};
    };
} // namespace Cue::GraphicsCore
