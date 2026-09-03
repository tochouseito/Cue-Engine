#include <Cue/EditorCore/EditorDocument.h>

#include <utility>

namespace cue::editor_core
{
EditorDocument::EditorDocument(EditorDocumentId a_id, scene::SceneDocument &&a_document, RelativePath &&a_locator,
                               bool a_hasSavedDestination) noexcept
    : m_id(a_id), m_document(std::move(a_document)), m_locator(std::move(a_locator)),
      m_hasSavedDestination(a_hasSavedDestination), m_currentStateId(m_id, 1U), m_savedStateId(m_id, 1U)
{
}

EditorDocumentId EditorDocument::id() const noexcept
{
    return m_id;
}

const scene::SceneDocument &EditorDocument::scene_document() const noexcept
{
    return m_document;
}

const RelativePath &EditorDocument::scene_locator() const noexcept
{
    return m_locator;
}

bool EditorDocument::has_saved_destination() const noexcept
{
    return m_hasSavedDestination;
}

DocumentStateId EditorDocument::current_state_id() const noexcept
{
    return m_currentStateId;
}

DocumentStateId EditorDocument::saved_state_id() const noexcept
{
    return m_savedStateId;
}

bool EditorDocument::is_dirty() const noexcept
{
    return m_currentStateId != m_savedStateId;
}

std::span<const scene::ObjectId> EditorDocument::selection() const noexcept
{
    return m_selection;
}

const scene::ObjectId *EditorDocument::try_primary_selection() const noexcept
{
    return m_primarySelection.has_value() ? &m_primarySelection.value() : nullptr;
}

ExternalChangeState EditorDocument::external_change_state() const noexcept
{
    return m_externalChangeState;
}

DocumentCloseState EditorDocument::close_state() const noexcept
{
    return m_closeState;
}

bool EditorDocument::requires_close_decision() const noexcept
{
    return is_dirty() || !m_hasSavedDestination || m_externalChangeState != ExternalChangeState::None;
}
} // namespace cue::editor_core
