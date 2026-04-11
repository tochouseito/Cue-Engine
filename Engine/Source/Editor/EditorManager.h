#pragma once

// === Core includes ===
#include <CQRS/CQRS.h>

// === Win includes ===
#include <WinPlatform.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Engine includes ===
#include <Engine.h>

// === Editor includes ===
#include "Statistics.h"
#include "DebugView.h"
#include "Hierarchy.h"
#include "Inspector.h"

namespace Cue::Editor
{
    class EditorManager final
    {
    public:
        EditorManager(Core::CQRS::Bridge* bridge, PAL::Win::WinPlatform* platform, RHI::DX12::D3D12Backend* backend, Engine* engine)
            : m_bridge(bridge), m_platform(platform), m_backend(backend), m_engine(engine) {}
        ~EditorManager() = default;

        void initialize();
        void update();
    private:
        Core::CQRS::Bridge* m_bridge = nullptr;
        PAL::Win::WinPlatform* m_platform = nullptr;
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        Engine* m_engine = nullptr;
        std::unique_ptr<Statistics> m_statistics = nullptr;
        std::unique_ptr<DebugView> m_debugView = nullptr;
        std::unique_ptr<Hierarchy> m_hierarchy = nullptr;
        std::unique_ptr<Inspector> m_inspector = nullptr;
    };
}
