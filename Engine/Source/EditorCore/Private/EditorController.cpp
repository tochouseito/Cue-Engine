#include <Cue/EditorCore/EditorController.h>

#include <Cue/EditorCore/Error.h>
#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace cue::editor_core
{
/// @brief DocumentStateId を一つの Controller Session へ結び付ける内部 Origin
class DocumentStateOrigin final
{
};

ProjectWorkspaceSession::ProjectWorkspaceSession(ProjectDescriptor &&a_descriptor) noexcept
    : m_descriptor(std::move(a_descriptor))
{
}

const ProjectDescriptor &ProjectWorkspaceSession::project_descriptor() const noexcept
{
    return m_descriptor;
}

std::span<const EditorDocument> ProjectWorkspaceSession::documents() const noexcept
{
    return m_documents;
}

const EditorDocument *ProjectWorkspaceSession::find_document(EditorDocumentId a_id) const noexcept
{
    const auto found = std::find_if(m_documents.begin(), m_documents.end(),
                                    [a_id](const EditorDocument &a_document) { return a_document.id() == a_id; });
    return found != m_documents.end() ? &*found : nullptr;
}

std::unique_ptr<EditorController> EditorController::create(ProjectDescriptor &&a_descriptor,
                                                           const AssertContext &a_assertContext) noexcept
{
    try
    {
        return std::make_unique<EditorController>(ConstructionKey{}, std::move(a_descriptor), a_assertContext);
    }
    catch (const std::bad_alloc &)
    {
        a_assertContext.fatal_handler().terminate("Cue.EditorCore controller allocation failed");
    }
    catch (...)
    {
        a_assertContext.fatal_handler().terminate("Cue.EditorCore controller construction failed");
    }

    std::terminate();
}

EditorController::EditorController(ConstructionKey, ProjectDescriptor &&a_descriptor,
                                   const AssertContext &a_assertContext)
    : m_stateOrigin(std::make_shared<DocumentStateOrigin>()), m_session(std::move(a_descriptor)),
      m_assertContext(&a_assertContext), m_ownerThread(std::this_thread::get_id())
{
}

EditorController::~EditorController() noexcept
{
    assert_owner_thread();
}

const ProjectWorkspaceSession &EditorController::session() const noexcept
{
    assert_owner_thread();
    return m_session;
}

Result<EditorDocumentId> EditorController::open_document(scene::SceneDocument &&a_document, RelativePath &&a_locator,
                                                         bool a_hasSavedDestination) noexcept
{
    assert_owner_thread();

    auto validation = a_document.validate();
    if (!validation)
    {
        return Result<EditorDocumentId>::failure(std::move(*validation.try_error()));
    }

    const auto locatorKey = a_locator.comparison_key(*m_assertContext);
    for (const EditorDocument &document : m_session.m_documents)
    {
        if (document.close_state() == DocumentCloseState::Closed)
        {
            continue;
        }

        if (document.scene_document().scene_asset_id() == a_document.scene_asset_id())
        {
            return Result<EditorDocumentId>::failure(make_editor_core_error(
                *m_assertContext, EditorCoreError::DuplicateScene, "Scene is already open in this project workspace"));
        }

        if (document.scene_locator().comparison_key(*m_assertContext) == locatorKey)
        {
            return Result<EditorDocumentId>::failure(
                make_editor_core_error(*m_assertContext, EditorCoreError::DuplicateLocator,
                                       "Scene locator is already open in this project workspace"));
        }
    }

    if (m_nextDocumentId == std::numeric_limits<std::uint64_t>::max())
    {
        return Result<EditorDocumentId>::failure(make_editor_core_error(
            *m_assertContext, EditorCoreError::DocumentIdExhausted, "Editor document identity space is exhausted"));
    }

    EditorDocumentId id(m_nextDocumentId);
    ++m_nextDocumentId;

    try
    {
        EditorDocument openedDocument(id, m_stateOrigin, std::move(a_document), std::move(a_locator),
                                      a_hasSavedDestination);
        m_session.m_documents.push_back(std::move(openedDocument));
    }
    catch (const std::bad_alloc &)
    {
        terminate_allocation();
    }
    catch (...)
    {
        terminate_exception();
    }

    return Result<EditorDocumentId>::success(std::move(id));
}

Result<void> EditorController::set_selection(EditorDocumentId a_documentId,
                                             std::span<const scene::ObjectId> a_objectIds,
                                             const scene::ObjectId *a_primaryObjectId) noexcept
{
    assert_owner_thread();
    EditorDocument *document = find_document(a_documentId);
    if (document == nullptr)
    {
        return Result<void>::failure(make_editor_document_error(*m_assertContext, EditorCoreError::DocumentNotFound,
                                                                "Editor document was not found", a_documentId.value()));
    }
    if (document->m_closeState != DocumentCloseState::Open)
    {
        return Result<void>::failure(make_editor_document_error(*m_assertContext, EditorCoreError::InvalidDocumentState,
                                                                "Selection can change only while the document is open",
                                                                a_documentId.value()));
    }

    std::vector<scene::ObjectId> nextSelection;
    std::optional<scene::ObjectId> nextPrimary;
    try
    {
        nextSelection.reserve((std::min)(a_objectIds.size(), document->m_document.object_count()));
        for (const scene::ObjectId &objectId : a_objectIds)
        {
            if (document->m_document.find_object(objectId) == nullptr ||
                std::find(nextSelection.begin(), nextSelection.end(), objectId) != nextSelection.end())
            {
                continue;
            }
            nextSelection.push_back(objectId);
        }

        if (a_primaryObjectId != nullptr &&
            std::find(nextSelection.begin(), nextSelection.end(), *a_primaryObjectId) != nextSelection.end())
        {
            nextPrimary = *a_primaryObjectId;
        }
        else if (!nextSelection.empty())
        {
            nextPrimary = nextSelection.front();
        }
    }
    catch (const std::bad_alloc &)
    {
        terminate_allocation();
    }
    catch (...)
    {
        terminate_exception();
    }

    document->m_selection = std::move(nextSelection);
    document->m_primarySelection = std::move(nextPrimary);
    return Result<void>::success();
}

Result<void> EditorController::clear_selection(EditorDocumentId a_documentId) noexcept
{
    return set_selection(a_documentId, {});
}

Result<void> EditorController::reconcile_selection(EditorDocumentId a_documentId) noexcept
{
    assert_owner_thread();
    EditorDocument *document = find_document(a_documentId);
    if (document == nullptr)
    {
        return Result<void>::failure(make_editor_document_error(*m_assertContext, EditorCoreError::DocumentNotFound,
                                                                "Editor document was not found", a_documentId.value()));
    }

    return set_selection(a_documentId, document->m_selection, document->try_primary_selection());
}

Result<DocumentStateId> EditorController::record_persistent_change(EditorDocumentId a_documentId) noexcept
{
    assert_owner_thread();
    EditorDocument *document = find_document(a_documentId);
    if (document == nullptr)
    {
        return Result<DocumentStateId>::failure(
            make_editor_document_error(*m_assertContext, EditorCoreError::DocumentNotFound,
                                       "Editor document was not found", a_documentId.value()));
    }
    if (document->m_closeState != DocumentCloseState::Open)
    {
        return Result<DocumentStateId>::failure(
            make_editor_document_error(*m_assertContext, EditorCoreError::InvalidDocumentState,
                                       "Persistent changes require an open document", a_documentId.value()));
    }
    if (document->m_nextStateId == std::numeric_limits<std::uint64_t>::max())
    {
        return Result<DocumentStateId>::failure(
            make_editor_document_error(*m_assertContext, EditorCoreError::RevisionExhausted,
                                       "Editor document state identity space is exhausted", a_documentId.value()));
    }

    document->m_currentStateId = DocumentStateId(m_stateOrigin, a_documentId, document->m_nextStateId);
    ++document->m_nextStateId;

    auto reconciled = reconcile_selection(a_documentId);
    if (!reconciled)
    {
        return Result<DocumentStateId>::failure(std::move(*reconciled.try_error()));
    }
    DocumentStateId issuedState = document->m_currentStateId;
    return Result<DocumentStateId>::success(std::move(issuedState));
}

Result<void> EditorController::mark_saved(EditorDocumentId a_documentId, DocumentStateId a_savedStateId) noexcept
{
    assert_owner_thread();
    EditorDocument *document = find_document(a_documentId);
    if (document == nullptr)
    {
        return Result<void>::failure(make_editor_document_error(*m_assertContext, EditorCoreError::DocumentNotFound,
                                                                "Editor document was not found", a_documentId.value()));
    }
    if (document->m_closeState == DocumentCloseState::Closed || a_savedStateId.m_origin != m_stateOrigin ||
        a_savedStateId.document_id() != a_documentId || a_savedStateId.value() == 0U ||
        a_savedStateId.value() >= document->m_nextStateId)
    {
        return Result<void>::failure(make_editor_document_error(*m_assertContext, EditorCoreError::InvalidSavedState,
                                                                "Saved state identity was not issued by this document",
                                                                a_documentId.value()));
    }

    document->m_savedStateId = a_savedStateId;
    document->m_hasSavedDestination = true;
    if (document->m_closeState == DocumentCloseState::SaveRequested)
    {
        if (!document->is_dirty() && document->m_externalChangeState == ExternalChangeState::None)
        {
            document->m_closeState = DocumentCloseState::Closed;
            erase_closed_document(a_documentId);
        }
        else
        {
            document->m_closeState = DocumentCloseState::AwaitingDecision;
        }
    }
    return Result<void>::success();
}

Result<void> EditorController::set_external_change_state(EditorDocumentId a_documentId,
                                                         ExternalChangeState a_state) noexcept
{
    assert_owner_thread();
    EditorDocument *document = find_document(a_documentId);
    if (document == nullptr)
    {
        return Result<void>::failure(make_editor_document_error(*m_assertContext, EditorCoreError::DocumentNotFound,
                                                                "Editor document was not found", a_documentId.value()));
    }
    if (document->m_closeState == DocumentCloseState::Closed)
    {
        return Result<void>::failure(make_editor_document_error(*m_assertContext, EditorCoreError::InvalidDocumentState,
                                                                "Closed document cannot receive external change state",
                                                                a_documentId.value()));
    }

    document->m_externalChangeState = a_state;
    return Result<void>::success();
}

Result<DocumentCloseState> EditorController::request_close(EditorDocumentId a_documentId) noexcept
{
    assert_owner_thread();
    EditorDocument *document = find_document(a_documentId);
    if (document == nullptr)
    {
        return Result<DocumentCloseState>::failure(
            make_editor_document_error(*m_assertContext, EditorCoreError::DocumentNotFound,
                                       "Editor document was not found", a_documentId.value()));
    }
    if (document->m_closeState != DocumentCloseState::Open)
    {
        return Result<DocumentCloseState>::success(DocumentCloseState(document->m_closeState));
    }

    if (document->requires_close_decision())
    {
        document->m_closeState = DocumentCloseState::AwaitingDecision;
        return Result<DocumentCloseState>::success(DocumentCloseState::AwaitingDecision);
    }

    document->m_closeState = DocumentCloseState::Closed;
    erase_closed_document(a_documentId);
    return Result<DocumentCloseState>::success(DocumentCloseState::Closed);
}

Result<DocumentCloseState> EditorController::respond_to_close(EditorDocumentId a_documentId,
                                                              CloseDecision a_decision) noexcept
{
    assert_owner_thread();
    EditorDocument *document = find_document(a_documentId);
    if (document == nullptr)
    {
        return Result<DocumentCloseState>::failure(
            make_editor_document_error(*m_assertContext, EditorCoreError::DocumentNotFound,
                                       "Editor document was not found", a_documentId.value()));
    }
    if (document->m_closeState != DocumentCloseState::AwaitingDecision)
    {
        return Result<DocumentCloseState>::failure(
            make_editor_document_error(*m_assertContext, EditorCoreError::InvalidCloseTransition,
                                       "Close decision requires an awaiting document", a_documentId.value()));
    }

    switch (a_decision)
    {
    case CloseDecision::Save:
        document->m_closeState = DocumentCloseState::SaveRequested;
        break;
    case CloseDecision::Discard:
        document->m_closeState = DocumentCloseState::Closed;
        erase_closed_document(a_documentId);
        return Result<DocumentCloseState>::success(DocumentCloseState::Closed);
    case CloseDecision::Cancel:
        document->m_closeState = DocumentCloseState::Open;
        break;
    }

    return Result<DocumentCloseState>::success(DocumentCloseState(document->m_closeState));
}

EditorDocument *EditorController::find_document(EditorDocumentId a_id) noexcept
{
    const auto found = std::find_if(m_session.m_documents.begin(), m_session.m_documents.end(),
                                    [a_id](const EditorDocument &a_document) { return a_document.id() == a_id; });
    return found != m_session.m_documents.end() ? &*found : nullptr;
}

void EditorController::erase_closed_document(EditorDocumentId a_id) noexcept
{
    const auto found = std::find_if(m_session.m_documents.begin(), m_session.m_documents.end(),
                                    [a_id](const EditorDocument &a_document) { return a_document.id() == a_id; });
    if (found != m_session.m_documents.end())
    {
        m_session.m_documents.erase(found);
    }
}

void EditorController::assert_owner_thread() const noexcept
{
    const bool isOwner = std::this_thread::get_id() == m_ownerThread;
    CUE_ASSERT(*m_assertContext, isOwner, "Cue.EditorCore controller API requires its owner thread");
    if (!isOwner)
    {
        m_assertContext->fatal_handler().terminate("Cue.EditorCore controller API requires its owner thread");
    }
}

[[noreturn]] void EditorController::terminate_allocation() const noexcept
{
    m_assertContext->fatal_handler().terminate("Cue.EditorCore allocation failed");
}

[[noreturn]] void EditorController::terminate_exception() const noexcept
{
    m_assertContext->fatal_handler().terminate("Cue.EditorCore unexpected exception");
}
} // namespace cue::editor_core
