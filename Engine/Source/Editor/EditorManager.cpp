#include "EditorManager.h"

namespace Cue::Editor
{
    void EditorManager::initialize()
    {
        m_statistics = std::make_unique<Statistics>(m_engine->frame_controller());
        m_debugView = std::make_unique<DebugView>(m_backend, m_bridge);
    }
    void EditorManager::update()
    {
        m_statistics->update();
        m_debugView->update();
    }
}
