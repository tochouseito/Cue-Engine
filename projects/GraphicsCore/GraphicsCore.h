#pragma once
#include <Result.h>
#include "FrameGraph.h"
#include "ViewManager.h"

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
        virtual Result build_frame_graph() = 0;
        virtual Result render(uint64_t frameNo, uint32_t index) = 0;
        virtual Result present(uint64_t frameNo, uint32_t index) = 0;
        virtual IViewManager* get_view_manager() const
        {
            return nullptr;
        }
        FrameGraph* get_frame_graph() const { return m_frameGraph.get(); }
        virtual FrameGraph* get_present_frame_graph() const
        {
            return nullptr;
        }
    protected:
        std::unique_ptr<FrameGraph> m_frameGraph = nullptr;
    };
} // namespace Cue::GraphicsCore
