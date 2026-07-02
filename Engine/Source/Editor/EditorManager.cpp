#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>

// === Editor includes ===
#include "DebugCamera.h"
#include "Workspace/DebugView.h"
#include "Workspace/Dockspace.h"
#include "Workspace/GameView.h"

namespace Cue::Editor
{
    EditorManager::EditorManager() = default;

    EditorManager::~EditorManager() = default;

    void EditorManager::initialize(const EditorManagerSetupInfo& a_info)
    {
        CUE_ASSERT_MSG(a_info.backend != nullptr, "EditorManager: backend is null");
        CUE_ASSERT_MSG(a_info.debugCamera != nullptr, "EditorManager: debug camera is null");

        m_backend = a_info.backend;
        m_engine = a_info.engine;
        m_debugCamera = a_info.debugCamera;

        // CueEngine と同じく、EditorManager が Editor View の所有と更新順を集約する。
        m_dockspace = std::make_unique<Dockspace>();
        m_gameView = std::make_unique<GameView>(m_backend);
        m_debugView = std::make_unique<DebugView>(m_backend);
    }

    void EditorManager::update()
    {
        if (m_dockspace != nullptr)
        {
            m_dockspace->update();
        }
        if (m_gameView != nullptr)
        {
            m_gameView->update();
        }
        if (m_debugView != nullptr)
        {
            m_debugView->update();
        }

        if (m_debugCamera == nullptr || m_debugView == nullptr)
        {
            return;
        }

        DebugCameraViewport debugCameraViewport{};
        debugCameraViewport.width = m_debugView->viewport_width();
        debugCameraViewport.height = m_debugView->viewport_height();
        debugCameraViewport.isHovered = m_debugView->is_viewport_hovered();
        debugCameraViewport.isFocused = m_debugView->is_focused();
        m_debugCamera->update(debugCameraViewport);
    }
} // namespace Cue::Editor
