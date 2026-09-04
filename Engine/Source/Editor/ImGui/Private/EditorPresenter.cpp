#include <Cue/Editor/ImGui/EditorPresenter.h>

#include <Cue/EditorCore/Error.h>
#include <Cue/Foundation/Assert.h>
#include <Cue/Scene/Error.h>
#include <Cue/Schema/Descriptor.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <imgui.h>

namespace
{
/// @brief Allocation失敗をEditor PresentationのFatal境界へ渡す
[[noreturn]] void terminate_allocation(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("Editor Presentation allocation failed");
    std::terminate();
}

/// @brief 予期しない例外をEditor PresentationのFatal境界へ渡す
[[noreturn]] void terminate_exception(const cue::AssertContext &a_context) noexcept
{
    a_context.fatal_handler().terminate("Editor Presentation unexpected exception");
    std::terminate();
}

/// @brief SceneComponentが保持するStable Type Identityを返す
[[nodiscard]] std::optional<cue::schema::TypeId> component_type_id(
    const cue::scene::SceneComponent &a_component) noexcept
{
    if (const cue::scene::KnownComponentData *known = a_component.try_known(); known != nullptr)
    {
        return known->type_id();
    }
    if (const cue::scene::OpaqueComponentData *opaque = a_component.try_opaque(); opaque != nullptr)
    {
        return opaque->type_id();
    }
    return std::nullopt;
}

/// @brief Stable Type Identityを診断用の32桁Hex文字列へ変換する
[[nodiscard]] std::string type_id_text(cue::schema::TypeId a_typeId)
{
    constexpr char k_hexDigits[] = "0123456789abcdef";
    std::string text;
    text.resize(32U);
    std::size_t index = 0U;
    for (const std::uint8_t byte : a_typeId.bytes())
    {
        text[index++] = k_hexDigits[(byte >> 4U) & 0x0FU];
        text[index++] = k_hexDigits[byte & 0x0FU];
    }
    return text;
}

/// @brief Object名の制御Byteを可視表現へEscapeしてImGuiのNUL終端Labelへ渡せるようにする
[[nodiscard]] std::string object_name_label(std::string_view a_name)
{
    constexpr char k_hexDigits[] = "0123456789abcdef";
    std::string label;
    label.reserve(a_name.size());
    for (const unsigned char byte : a_name)
    {
        switch (byte)
        {
        case 0U:
            label.append("\\0");
            break;
        case static_cast<unsigned char>('\n'):
            label.append("\\n");
            break;
        case static_cast<unsigned char>('\r'):
            label.append("\\r");
            break;
        case static_cast<unsigned char>('\t'):
            label.append("\\t");
            break;
        default:
            if (byte < 0x20U || byte == 0x7FU)
            {
                label.append("\\x");
                label.push_back(k_hexDigits[(byte >> 4U) & 0x0FU]);
                label.push_back(k_hexDigits[byte & 0x0FU]);
            }
            else
            {
                label.push_back(static_cast<char>(byte));
            }
            break;
        }
    }
    return label;
}

/// @brief Stable Component IdentityをImGui ID用のCanonical文字列へ変換する
[[nodiscard]] cue::scene::IdentityText component_id_text(const cue::scene::ComponentInstanceId &a_componentId) noexcept
{
    return a_componentId.canonical_text();
}

/// @brief ImGuiの可変長Text編集へ所有StringとFatal境界を渡す
struct StringInputContext final
{
    std::string *value;
    const cue::AssertContext *assertContext;
};

/// @brief ImGuiのBuffer拡張要求へstd::stringの所有Bufferを追従させる
int resize_string_input(ImGuiInputTextCallbackData *a_data) noexcept
{
    auto *context = static_cast<StringInputContext *>(a_data->UserData);
    try
    {
        context->value->resize(static_cast<std::size_t>(a_data->BufTextLen));
        a_data->Buf = context->value->data();
        return 0;
    }
    catch (const std::bad_alloc &)
    {
        terminate_allocation(*context->assertContext);
    }
    catch (...)
    {
        terminate_exception(*context->assertContext);
    }
}

/// @brief Scene Object名を切り捨てずにImGuiの可変長InputTextへ接続する
[[nodiscard]] bool input_object_name(std::string &a_value, const cue::AssertContext &a_assertContext)
{
    StringInputContext context{&a_value, &a_assertContext};
    const bool submitted = ImGui::InputText("Name", a_value.data(), a_value.capacity() + 1U,
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
                                            resize_string_input, &context);
    a_value.resize(std::strlen(a_value.c_str()));
    return submitted;
}

/// @brief Stable Object Identityの16 byteをHierarchy索引用Hashへ変換する
struct ObjectIdHash final
{
    [[nodiscard]] std::size_t operator()(const cue::scene::ObjectId &a_objectId) const noexcept
    {
        std::size_t hash = 0U;
        for (const std::uint8_t byte : a_objectId.bytes())
        {
            hash = (hash * 131U) ^ static_cast<std::size_t>(byte);
        }
        return hash;
    }
};

using HierarchyChildIndex =
    std::unordered_map<cue::scene::ObjectId, std::vector<const cue::scene::SceneObject *>, ObjectIdHash>;
using HierarchySelectionIndex = std::unordered_set<cue::scene::ObjectId, ObjectIdHash>;

/// @brief Frame単位Selection索引にStable Object Identityが含まれるか判定する
[[nodiscard]] bool is_selected(const HierarchySelectionIndex &a_selection,
                               const cue::scene::ObjectId &a_objectId) noexcept
{
    return a_selection.contains(a_objectId);
}

/// @brief Frame単位Child索引から一ObjectとChild群を再帰描画する
void draw_object_node(const cue::scene::SceneObject &a_object, const HierarchyChildIndex &a_childrenByParent,
                      const HierarchySelectionIndex &a_selectionIndex,
                      std::span<const cue::scene::ObjectId> a_selection,
                      std::optional<cue::editor_core::EditorIntent> &a_pendingIntent)
{
    const auto children = a_childrenByParent.find(a_object.id());
    const bool hasChildren = children != a_childrenByParent.end() && !children->second.empty();

    const cue::scene::IdentityText objectIdText = a_object.id().canonical_text();
    ImGui::PushID(objectIdText.data(), objectIdText.data() + objectIdText.size());
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (is_selected(a_selectionIndex, a_object.id()))
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    const std::string name = object_name_label(a_object.name());
    const bool isOpen = ImGui::TreeNodeEx("##Object", flags, "%s", name.c_str());
    if (!a_pendingIntent.has_value() && ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
    {
        std::vector<cue::scene::ObjectId> nextSelection;
        std::optional<cue::scene::ObjectId> primary;
        if (ImGui::GetIO().KeyCtrl)
        {
            nextSelection.assign(a_selection.begin(), a_selection.end());
            const auto found = std::find(nextSelection.begin(), nextSelection.end(), a_object.id());
            if (found == nextSelection.end())
            {
                nextSelection.push_back(a_object.id());
                primary = a_object.id();
            }
            else
            {
                nextSelection.erase(found);
                if (!nextSelection.empty())
                {
                    primary = nextSelection.back();
                }
            }
        }
        else
        {
            nextSelection.push_back(a_object.id());
            primary = a_object.id();
        }
        a_pendingIntent.emplace(cue::editor_core::SelectObjectsIntent{std::move(nextSelection), std::move(primary)});
    }

    if (hasChildren && isOpen)
    {
        for (const cue::scene::SceneObject *child : children->second)
        {
            draw_object_node(*child, a_childrenByParent, a_selectionIndex, a_selection, a_pendingIntent);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

/// @brief Schema RegistryからComponent表示名を取得し、未知TypeはStable Identity表示へ退避する
[[nodiscard]] std::string component_label(const cue::scene::SceneComponent &a_component,
                                          const cue::schema::SchemaRegistry &a_registry,
                                          const cue::AssertContext &a_assertContext)
{
    const std::optional<cue::schema::TypeId> typeId = component_type_id(a_component);
    if (!typeId.has_value())
    {
        return "無効なComponent";
    }
    cue::Result<const cue::schema::TypeDescriptor *> descriptor = a_registry.find(*typeId, a_assertContext);
    if (descriptor)
    {
        return std::string((*descriptor.try_value())->name());
    }
    return "Unknown Component [" + type_id_text(*typeId) + "]";
}

/// @brief Field Descriptorがあれば診断名を返し、未登録FieldはStable数値を返す
[[nodiscard]] std::string field_label(const cue::schema::TypeDescriptor *a_descriptor, cue::schema::FieldId a_fieldId,
                                      const cue::AssertContext &a_assertContext)
{
    if (a_descriptor != nullptr)
    {
        cue::Result<const cue::schema::FieldDescriptor *> field = a_descriptor->find_field(a_fieldId, a_assertContext);
        if (field)
        {
            return std::string((*field.try_value())->name());
        }
    }
    return "Field " + std::to_string(a_fieldId.value());
}

/// @brief 型付きAuthoring Field ValueをInspectorのRead-only表示へ変換する
void draw_field_value(const cue::scene::FieldValue &a_value)
{
    switch (a_value.kind())
    {
    case cue::scene::FieldValueKind::Boolean:
        ImGui::TextUnformatted(*a_value.try_boolean() ? "true" : "false");
        break;
    case cue::scene::FieldValueKind::SignedInteger:
        ImGui::Text("%lld", static_cast<long long>(*a_value.try_signed_integer()));
        break;
    case cue::scene::FieldValueKind::UnsignedInteger:
        ImGui::Text("%llu", static_cast<unsigned long long>(*a_value.try_unsigned_integer()));
        break;
    case cue::scene::FieldValueKind::FloatingPoint:
        ImGui::Text("%.6g", *a_value.try_floating_point());
        break;
    case cue::scene::FieldValueKind::String:
    {
        const std::string &text = *a_value.try_string();
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        break;
    }
    case cue::scene::FieldValueKind::AssetReference:
    {
        const std::string_view token = a_value.try_asset_reference()->token();
        ImGui::TextUnformatted(token.data(), token.data() + token.size());
        break;
    }
    }
}

/// @brief 成功したSemantic Intentを利用者向けStatusへ変換する
[[nodiscard]] std::string_view intent_status(const cue::editor_core::EditorIntent &a_intent) noexcept
{
    return std::visit(
        /// @brief Intent AlternativeごとにPresentationだけが所有する成功Messageを返す
        [](const auto &a_typedIntent) noexcept -> std::string_view
        {
            using Intent = std::remove_cvref_t<decltype(a_typedIntent)>;
            if constexpr (std::is_same_v<Intent, cue::editor_core::SelectObjectsIntent>)
            {
                return "選択を更新しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::AddObjectIntent>)
            {
                return "Objectを追加しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::DeleteObjectIntent>)
            {
                return "ObjectとChildを削除しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::DuplicateObjectIntent>)
            {
                return "ObjectとChildを複製しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::RenameObjectIntent>)
            {
                return "Object名を変更しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::ReparentObjectIntent>)
            {
                return a_typedIntent.parentId.has_value() ? "Parentを変更しました。" : "ObjectをRootへ移動しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::EditTransformIntent>)
            {
                return "Transformを変更しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::AddComponentIntent>)
            {
                return "Componentを追加しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::RemoveComponentIntent>)
            {
                return "Componentを削除しました。";
            }
            else if constexpr (std::is_same_v<Intent, cue::editor_core::UndoIntent>)
            {
                return "直前の編集を元に戻しました。";
            }
            else
            {
                return "取り消した編集を再適用しました。";
            }
        },
        a_intent);
}

} // namespace

namespace cue::editor
{
using editor_core::AddComponentIntent;
using editor_core::AddObjectIntent;
using editor_core::DeleteObjectIntent;
using editor_core::DuplicateObjectIntent;
using editor_core::EditorComponentTemplate;
using editor_core::EditorIntent;
using editor_core::EditTransformIntent;
using editor_core::RedoIntent;
using editor_core::RemoveComponentIntent;
using editor_core::RenameObjectIntent;
using editor_core::ReparentObjectIntent;
using editor_core::SelectObjectsIntent;
using editor_core::UndoIntent;

EditorPresenter::EditorPresenter(editor_core::EditorController &a_controller,
                                 editor_core::EditorDocumentId a_documentId,
                                 scene::SceneIdentitySource &a_identitySource,
                                 const schema::SchemaRegistry &a_schemaRegistry,
                                 std::vector<editor_core::EditorComponentTemplate> a_componentTemplates,
                                 const AssertContext &a_assertContext) noexcept
    : m_controller(&a_controller), m_identitySource(&a_identitySource), m_schemaRegistry(&a_schemaRegistry),
      m_assertContext(&a_assertContext), m_componentTemplates(std::move(a_componentTemplates)),
      m_documentId(a_documentId)
{
}

std::unique_ptr<EditorPresenter> EditorPresenter::create(
    editor_core::EditorController &a_controller, editor_core::EditorDocumentId a_documentId,
    scene::SceneIdentitySource &a_identitySource, const schema::SchemaRegistry &a_schemaRegistry,
    std::vector<editor_core::EditorComponentTemplate> a_componentTemplates,
    const AssertContext &a_assertContext) noexcept
{
    try
    {
        return std::unique_ptr<EditorPresenter>(new EditorPresenter(a_controller, a_documentId, a_identitySource,
                                                                    a_schemaRegistry, std::move(a_componentTemplates),
                                                                    a_assertContext));
    }
    catch (const std::bad_alloc &)
    {
        terminate_allocation(a_assertContext);
    }
    catch (...)
    {
        terminate_exception(a_assertContext);
    }
}

void EditorPresenter::draw() noexcept
{
    try
    {
        std::optional<EditorIntent> pendingIntent;
        const editor_core::EditorDocument *document = m_controller->session().find_document(m_documentId);

        ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        constexpr ImGuiWindowFlags k_windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse |
                                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        if (ImGui::Begin("CueEngine Editor", nullptr, k_windowFlags))
        {
            if (document == nullptr)
            {
                ImGui::TextUnformatted("編集対象のScene Documentが開かれていません。");
            }
            else
            {
                draw_menu(*document, pendingIntent);
                draw_hierarchy(*document, pendingIntent);
                ImGui::SameLine();
                draw_inspector(*document, pendingIntent);
            }

            if (!m_message.empty())
            {
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      m_hasError ? ImVec4(1.0F, 0.35F, 0.35F, 1.0F) : ImVec4(0.45F, 0.9F, 0.55F, 1.0F));
                ImGui::TextWrapped("%s", m_message.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();

        // Scene ViewのPointerやSpanを使用し終えたFrame末尾だけでController Mutationを行う
        if (pendingIntent.has_value())
        {
            static_cast<void>(submit(std::move(*pendingIntent)));
        }
    }
    catch (const std::bad_alloc &)
    {
        terminate_allocation(*m_assertContext);
    }
    catch (...)
    {
        terminate_exception(*m_assertContext);
    }
}

Result<void> EditorPresenter::submit(editor_core::EditorIntent a_intent) noexcept
{
    try
    {
        const std::string_view status = intent_status(a_intent);
        Result<void> result =
            m_controller->execute_intent(m_documentId, std::move(a_intent), *m_identitySource, m_componentTemplates);
        if (!result)
        {
            set_error(*result.try_error());
            return result;
        }
        set_status(status);
        return result;
    }
    catch (const std::bad_alloc &)
    {
        terminate_allocation(*m_assertContext);
    }
    catch (...)
    {
        terminate_exception(*m_assertContext);
    }
}

void EditorPresenter::draw_menu(const editor_core::EditorDocument &a_document,
                                std::optional<EditorIntent> &a_pendingIntent)
{
    if (!ImGui::BeginMenuBar())
    {
        return;
    }
    if (ImGui::BeginMenu("編集"))
    {
        std::string undoLabel = "元に戻す";
        if (!a_document.undo_label().empty())
        {
            undoLabel.append(": ");
            undoLabel.append(a_document.undo_label());
        }
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, a_document.can_undo()))
        {
            a_pendingIntent.emplace(UndoIntent{});
        }

        std::string redoLabel = "やり直す";
        if (!a_document.redo_label().empty())
        {
            redoLabel.append(": ");
            redoLabel.append(a_document.redo_label());
        }
        if (!a_pendingIntent.has_value() && ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, a_document.can_redo()))
        {
            a_pendingIntent.emplace(RedoIntent{});
        }
        ImGui::EndMenu();
    }
    const bool canUseShortcut =
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && !ImGui::GetIO().WantTextInput;
    if (!a_pendingIntent.has_value() && canUseShortcut && a_document.can_undo() &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
    {
        a_pendingIntent.emplace(UndoIntent{});
    }
    if (!a_pendingIntent.has_value() && canUseShortcut && a_document.can_redo() &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
    {
        a_pendingIntent.emplace(RedoIntent{});
    }
    ImGui::EndMenuBar();
}

void EditorPresenter::draw_hierarchy(const editor_core::EditorDocument &a_document,
                                     std::optional<EditorIntent> &a_pendingIntent)
{
    const scene::SceneDocument &sceneDocument = a_document.scene_document();
    const scene::ObjectId *primarySelection = a_document.try_primary_selection();
    ImGui::BeginChild("Hierarchy", ImVec2(ImGui::GetContentRegionAvail().x * 0.36F, -1.0F), true);
    ImGui::TextUnformatted("Hierarchy");

    if (ImGui::Button("Objectを追加") && !a_pendingIntent.has_value())
    {
        const std::optional<scene::ObjectId> parentId =
            primarySelection != nullptr ? std::optional<scene::ObjectId>(*primarySelection) : std::nullopt;
        a_pendingIntent.emplace(AddObjectIntent{parentId, "GameObject"});
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(primarySelection == nullptr);
    if (ImGui::Button("複製") && !a_pendingIntent.has_value())
    {
        a_pendingIntent.emplace(DuplicateObjectIntent{*primarySelection});
    }
    ImGui::SameLine();
    if (ImGui::Button("削除") && !a_pendingIntent.has_value())
    {
        a_pendingIntent.emplace(DeleteObjectIntent{*primarySelection});
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    HierarchyChildIndex childrenByParent;
    childrenByParent.reserve(sceneDocument.object_count());
    for (const scene::SceneObject &object : sceneDocument.objects())
    {
        const scene::ObjectId *parentId = object.try_parent_id();
        if (parentId != nullptr)
        {
            childrenByParent[*parentId].push_back(&object);
        }
    }
    HierarchySelectionIndex selectionIndex;
    selectionIndex.reserve(a_document.selection().size());
    selectionIndex.insert(a_document.selection().begin(), a_document.selection().end());

    for (const scene::SceneObject &object : sceneDocument.objects())
    {
        if (object.try_parent_id() == nullptr)
        {
            draw_object_node(object, childrenByParent, selectionIndex, a_document.selection(), a_pendingIntent);
        }
    }
    ImGui::EndChild();
}

void EditorPresenter::draw_inspector(const editor_core::EditorDocument &a_document,
                                     std::optional<EditorIntent> &a_pendingIntent)
{
    ImGui::BeginChild("Inspector", ImVec2(0.0F, -1.0F), true);
    ImGui::TextUnformatted("Inspector");
    const scene::ObjectId *primarySelection = a_document.try_primary_selection();
    if (primarySelection == nullptr)
    {
        ImGui::TextDisabled("HierarchyからObjectを選択してください。");
        ImGui::EndChild();
        return;
    }

    const scene::SceneDocument &sceneDocument = a_document.scene_document();
    const scene::SceneObject *object = sceneDocument.find_object(*primarySelection);
    if (object == nullptr)
    {
        ImGui::TextUnformatted("選択ObjectがSceneに存在しません。");
        ImGui::EndChild();
        return;
    }
    sync_inspector(a_document, *object);

    bool commitName = false;
    if (m_name.find('\0') != std::string::npos)
    {
        ImGui::TextDisabled("NameにU+0000が含まれるためInspectorでは編集できません（%zu bytes）。", m_name.size());
    }
    else
    {
        commitName = input_object_name(m_name, *m_assertContext);
        commitName = commitName || ImGui::IsItemDeactivatedAfterEdit();
    }
    if (commitName && !a_pendingIntent.has_value())
    {
        a_pendingIntent.emplace(RenameObjectIntent{object->id(), m_name});
    }

    const scene::ObjectId *parentId = object->try_parent_id();
    std::string parentPreview = "Scene Root";
    if (parentId != nullptr)
    {
        const scene::SceneObject *parent = sceneDocument.find_object(*parentId);
        parentPreview = parent != nullptr ? object_name_label(parent->name()) : "Missing Parent";
    }
    if (ImGui::BeginCombo("Parent", parentPreview.c_str()))
    {
        if (ImGui::Selectable("Scene Root", parentId == nullptr) && !a_pendingIntent.has_value())
        {
            a_pendingIntent.emplace(ReparentObjectIntent{object->id(), std::nullopt});
        }
        for (const scene::SceneObject &candidate : sceneDocument.objects())
        {
            if (candidate.id() == object->id())
            {
                continue;
            }
            const scene::IdentityText candidateId = candidate.id().canonical_text();
            ImGui::PushID(candidateId.data(), candidateId.data() + candidateId.size());
            const bool isCurrent = parentId != nullptr && *parentId == candidate.id();
            const std::string candidateName = object_name_label(candidate.name());
            if (ImGui::Selectable(candidateName.c_str(), isCurrent) && !a_pendingIntent.has_value())
            {
                a_pendingIntent.emplace(ReparentObjectIntent{object->id(), candidate.id()});
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Core Transform");
    ImGui::InputFloat3("Translation", m_translation.data());
    ImGui::InputFloat4("Rotation (Quaternion)", m_rotation.data());
    ImGui::InputFloat3("Scale", m_scale.data());
    if (ImGui::Button("Transformを適用") && !a_pendingIntent.has_value())
    {
        Result<math::Tolerance> tolerance =
            math::Tolerance::create(m_assertContext->fatal_handler(), 0.00001F, 0.00001F);
        if (!tolerance)
        {
            set_error(*tolerance.try_error());
        }
        else
        {
            Result<math::Quaternion> rotation = math::normalize(
                m_assertContext->fatal_handler(),
                math::Quaternion{m_rotation[0], m_rotation[1], m_rotation[2], m_rotation[3]}, *tolerance.try_value());
            if (!rotation)
            {
                set_error(*rotation.try_error());
            }
            else
            {
                Result<math::Transform> transform = math::Transform::create(
                    m_assertContext->fatal_handler(),
                    math::Vector3{m_translation[0], m_translation[1], m_translation[2]}, *rotation.try_value(),
                    math::Vector3{m_scale[0], m_scale[1], m_scale[2]}, *tolerance.try_value());
                if (!transform)
                {
                    set_error(*transform.try_error());
                }
                else
                {
                    a_pendingIntent.emplace(EditTransformIntent{object->id(), std::move(*transform.try_value())});
                }
            }
        }
    }

    ImGui::SeparatorText("Components");
    for (const scene::SceneComponent &component : object->components())
    {
        const scene::IdentityText componentId = component_id_text(component.instance_id());
        ImGui::PushID(componentId.data(), componentId.data() + componentId.size());
        const std::string label = component_label(component, *m_schemaRegistry, *m_assertContext);
        if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            const scene::KnownComponentData *known = component.try_known();
            if (known != nullptr)
            {
                const schema::TypeDescriptor *descriptor = nullptr;
                Result<const schema::TypeDescriptor *> found =
                    m_schemaRegistry->find(known->type_id(), *m_assertContext);
                if (found)
                {
                    descriptor = *found.try_value();
                }
                for (const scene::KnownFieldData &field : known->known_fields())
                {
                    const std::string fieldName = field_label(descriptor, field.id(), *m_assertContext);
                    ImGui::TextUnformatted(fieldName.c_str());
                    ImGui::SameLine();
                    draw_field_value(field.value());
                }
                if (!known->unknown_fields().empty())
                {
                    ImGui::TextDisabled("未知Field %zu件は保持されますが編集できません。",
                                        known->unknown_fields().size());
                }
            }
            else
            {
                ImGui::TextDisabled("未知Component Dataは保持されますが編集できません。");
            }
            if (ImGui::Button("Componentを削除") && !a_pendingIntent.has_value())
            {
                a_pendingIntent.emplace(RemoveComponentIntent{object->id(), component.instance_id()});
            }
        }
        ImGui::PopID();
    }

    if (m_componentTemplates.empty())
    {
        ImGui::TextDisabled("追加可能なComponentは登録されていません。");
    }
    else if (ImGui::BeginCombo("Componentを追加", "選択してください"))
    {
        for (const EditorComponentTemplate &componentTemplate : m_componentTemplates)
        {
            const std::optional<schema::TypeId> typeId = component_type_id(componentTemplate.prototype);
            std::string typeIdText;
            if (typeId.has_value())
            {
                typeIdText = type_id_text(*typeId);
                ImGui::PushID(typeIdText.data(), typeIdText.data() + typeIdText.size());
            }
            else
            {
                ImGui::PushID(&componentTemplate);
            }
            ImGui::BeginDisabled(!typeId.has_value());
            const std::string componentName = object_name_label(componentTemplate.displayName);
            if (ImGui::Selectable(componentName.c_str()) && typeId.has_value() && !a_pendingIntent.has_value())
            {
                a_pendingIntent.emplace(AddComponentIntent{object->id(), *typeId});
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::EndChild();
}

void EditorPresenter::sync_inspector(const editor_core::EditorDocument &a_document, const scene::SceneObject &a_object)
{
    const std::uint64_t stateValue = a_document.current_state_id().value();
    if (m_inspectorObjectId.has_value() && *m_inspectorObjectId == a_object.id() && m_inspectorStateValue == stateValue)
    {
        return;
    }

    m_name.assign(a_object.name());
    const math::Vector3 translation = a_object.transform().translation();
    const math::Quaternion rotation = a_object.transform().rotation();
    const math::Vector3 scale = a_object.transform().scale();
    m_translation = {translation.x, translation.y, translation.z};
    m_rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
    m_scale = {scale.x, scale.y, scale.z};
    m_inspectorObjectId = a_object.id();
    m_inspectorStateValue = stateValue;
}

void EditorPresenter::set_error(const Error &a_error) noexcept
{
    const ErrorCode &code = a_error.code();
    const ErrorCode &rootCode = a_error.root_code();
    const char *message = "編集操作に失敗しました。入力内容とLogを確認してください。";
    if (code.domain() == "Cue.EditorCore")
    {
        switch (static_cast<editor_core::EditorCoreError>(code.value()))
        {
        case editor_core::EditorCoreError::DocumentNotFound:
            message = "編集対象のScene Documentが見つかりません。";
            break;
        case editor_core::EditorCoreError::UndoUnavailable:
            message = "元に戻せる編集履歴がありません。";
            break;
        case editor_core::EditorCoreError::RedoUnavailable:
            message = "やり直せる編集履歴がありません。";
            break;
        case editor_core::EditorCoreError::InvalidDocumentState:
            message = "現在のDocument状態では編集できません。";
            break;
        default:
            break;
        }
    }
    if (rootCode.domain() == "Cue.Scene")
    {
        switch (static_cast<scene::SceneError>(rootCode.value()))
        {
        case scene::SceneError::InvalidName:
            message = "Object名は空でない有効なUTF-8文字列にしてください。";
            break;
        case scene::SceneError::ObjectNotFound:
            message = "対象ObjectがSceneに存在しません。Hierarchyを更新してください。";
            break;
        case scene::SceneError::HierarchyCycle:
            message = "ChildをParentに指定するとHierarchyが循環するため変更できません。";
            break;
        case scene::SceneError::HierarchyDepthExceeded:
            message = "Hierarchyの深さ上限を超えるため変更できません。";
            break;
        case scene::SceneError::DuplicateComponentId:
            message = "Component Identityが重複したため追加できません。";
            break;
        case scene::SceneError::ComponentNotFound:
            message = "対象Componentが見つかりません。Inspectorを更新してください。";
            break;
        case scene::SceneError::UnknownSchemaType:
            message = "追加するComponent Templateが登録されていません。";
            break;
        case scene::SceneError::UnsupportedComponentOperation:
            message = "未知Componentは安全に複製できないため、この操作を実行できません。";
            break;
        default:
            break;
        }
    }

    try
    {
        m_message.assign(message);
        m_message.append("\n診断: ");
        m_message.append(rootCode.domain());
        m_message.push_back('/');
        m_message.append(std::to_string(rootCode.value()));
        m_message.append(" - ");
        m_message.append(a_error.summary());
        m_hasError = true;
    }
    catch (...)
    {
        terminate_allocation(*m_assertContext);
    }
}

void EditorPresenter::set_status(std::string_view a_status) noexcept
{
    try
    {
        m_message.assign(a_status);
        m_hasError = false;
    }
    catch (...)
    {
        terminate_allocation(*m_assertContext);
    }
}

std::string_view EditorPresenter::message() const noexcept
{
    return m_message;
}

bool EditorPresenter::has_error_message() const noexcept
{
    return m_hasError;
}
} // namespace cue::editor
