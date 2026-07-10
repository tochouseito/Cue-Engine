#include "Hierarchy.h"

// === Runtime includes ===
#include <CQRS/CQRS.h>
#include <Command/Commands.h>
#include <GameCore/Components.h>
#include <GameCore/GameObject.h>
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    Hierarchy::Hierarchy(Core::CQRS::Bridge* a_commandBridge,
                         GameCore::GameWorld* a_gameWorld,
                         GameCore::EntityId* a_selectedEntityId,
                         GameCore::SceneId* a_selectedSceneId,
                         AssetSelection* a_selectedAsset) noexcept
        : m_commandBridge(a_commandBridge), m_gameWorld(a_gameWorld),
          m_selectedEntityId(a_selectedEntityId),
          m_selectedSceneId(a_selectedSceneId), m_selectedAsset(a_selectedAsset)
    {
    }

    void Hierarchy::set_game_world(GameCore::GameWorld* a_gameWorld) noexcept
    {
        m_gameWorld = a_gameWorld;
    }

    void Hierarchy::update()
    {
        ImGui::Begin("ヒエラルキー");

        if (m_gameWorld == nullptr)
        {
            ImGui::TextUnformatted("GameWorld が初期化されていません。");
            ImGui::End();
            return;
        }

        if (!refresh_objects())
        {
            ImGui::TextUnformatted("GameObject の列挙に失敗しました。");
            ImGui::End();
            return;
        }

        const bool worldSelected =
            selected_entity_id() == GameCore::k_invalidEntityId;
        ImGuiTreeNodeFlags worldFlags = ImGuiTreeNodeFlags_DefaultOpen |
                                        ImGuiTreeNodeFlags_OpenOnArrow |
                                        ImGuiTreeNodeFlags_SpanAvailWidth;
        if (worldSelected)
        {
            worldFlags |= ImGuiTreeNodeFlags_Selected;
        }
        if (m_roots.empty())
        {
            worldFlags |= ImGuiTreeNodeFlags_Leaf;
        }

        const bool isOpen = ImGui::TreeNodeEx("World", worldFlags);
        if (ImGui::IsItemClicked())
        {
            set_selected_entity_id(GameCore::k_invalidEntityId);
            set_selected_scene_id(GameCore::k_invalidSceneId);
        }
        draw_world_drop_target();

        if (isOpen)
        {
            for (const size_t objectIndex : m_roots)
            {
                draw_object_node(objectIndex);
            }
            ImGui::TreePop();
        }

        ImGui::End();
    }

    bool Hierarchy::refresh_objects()
    {
        m_objects.clear();
        m_roots.clear();
        m_objectIndexById.clear();

        size_t objectCount = 0;
        const Result countResult = m_gameWorld->object_count(objectCount);
        if (countResult)
        {
            m_objects.reserve(objectCount);
            m_objectIndexById.reserve(objectCount);
        }

        const Result enumerateResult = m_gameWorld->for_each_object(
            [this](GameCore::EntityId a_entityId, GameCore::GameObject a_object) {
                std::string objectName{};
                Result nameResult = a_object.name(objectName);
                if (!nameResult || objectName.empty())
                {
                    objectName = "GameObject";
                }

                GameCore::BaseComponent* base = nullptr;
                GameCore::EntityId parent = GameCore::k_invalidEntityId;
                GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
                if (a_object.get_component(base) && base != nullptr)
                {
                    // EditorPreview は検証用の一時 Object で、Scene
                    // 階層の編集対象から外す
                    if (base->tag == "EditorPreview")
                    {
                        return;
                    }
                    parent = base->parent;
                    sceneId = base->owningSceneId;
                }

                m_objects.push_back(
                    {std::move(objectName), a_entityId, parent, sceneId, {}});
            });
        if (!enumerateResult)
        {
            return false;
        }

        std::sort(m_objects.begin(), m_objects.end(),
                  [](const ObjectEntry& a_left, const ObjectEntry& a_right) {
                      if (a_left.sceneId != a_right.sceneId)
                      {
                          return a_left.sceneId < a_right.sceneId;
                      }
                      if (a_left.name == a_right.name)
                      {
                          return a_left.entityId < a_right.entityId;
                      }
                      return a_left.name < a_right.name;
                  });

        for (size_t objectIndex = 0; objectIndex < m_objects.size(); ++objectIndex)
        {
            m_objectIndexById.emplace(m_objects[objectIndex].entityId, objectIndex);
        }

        for (size_t objectIndex = 0; objectIndex < m_objects.size(); ++objectIndex)
        {
            ObjectEntry& object = m_objects[objectIndex];
            const auto parentIt = m_objectIndexById.find(object.parent);
            if (parentIt != m_objectIndexById.end() &&
                m_objects[parentIt->second].sceneId == object.sceneId)
            {
                // Scene をまたぐ親子関係は Scene serialize 時の所有境界を曖昧にするため
                // root 扱いにする
                m_objects[parentIt->second].children.push_back(objectIndex);
                continue;
            }

            m_roots.push_back(objectIndex);
        }

        validate_selection();
        return true;
    }

    void Hierarchy::validate_selection() noexcept
    {
        if (selected_entity_id() == GameCore::k_invalidEntityId)
        {
            return;
        }

        const bool hasSelectedObject = std::any_of(
            m_objects.begin(), m_objects.end(), [this](const ObjectEntry& a_object) {
                return a_object.entityId == selected_entity_id();
            });
        if (!hasSelectedObject)
        {
            set_selected_entity_id(GameCore::k_invalidEntityId);
            set_selected_scene_id(GameCore::k_invalidSceneId);
        }

        const bool hasRenamingObject = std::any_of(
            m_objects.begin(), m_objects.end(), [this](const ObjectEntry& a_object) {
                return a_object.entityId == m_renamingEntityId;
            });
        if (!hasRenamingObject)
        {
            cancel_rename();
        }
    }

    void Hierarchy::draw_object_node(size_t a_objectIndex)
    {
        if (a_objectIndex >= m_objects.size())
        {
            return;
        }

        const ObjectEntry& object = m_objects[a_objectIndex];
        ImGui::PushID(static_cast<int>(object.entityId));

        if (m_renamingEntityId == object.entityId)
        {
            draw_rename_input(object);
            ImGui::PopID();
            return;
        }

        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (object.children.empty())
        {
            flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (selected_entity_id() == object.entityId)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        const bool isOpen = ImGui::TreeNodeEx(object.name.c_str(), flags);
        if (ImGui::IsItemClicked())
        {
            set_selected_entity_id(object.entityId);
            set_selected_scene_id(object.sceneId);
        }
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            begin_rename(object);
        }

        draw_object_drag_source(object);
        draw_object_drop_target(object);
        draw_object_context_menu(object);

        if (isOpen)
        {
            for (const size_t childIndex : object.children)
            {
                draw_object_node(childIndex);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    void Hierarchy::draw_rename_input(const ObjectEntry& a_object)
    {
        if (m_focusRenameInput)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusRenameInput = false;
        }

        const bool submitted = ImGui::InputText(
            "##Rename", m_renameBuffer.data(), m_renameBuffer.size(),
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
        const bool deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit();
        const bool deactivated = ImGui::IsItemDeactivated();
        const bool isActive = ImGui::IsItemActive();
        const bool escapePressed =
            isActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

        if (submitted || deactivatedAfterEdit)
        {
            submit_rename_command(a_object.entityId);
        }
        else if (escapePressed || deactivated)
        {
            // 未編集の focus 喪失はキャンセル扱いにし、意図しない空 rename を送らない
            cancel_rename();
        }
    }

    void Hierarchy::draw_world_drop_target()
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(k_objectDragPayloadType);
            if (payload != nullptr && payload->DataSize == sizeof(DragObjectPayload))
            {
                const DragObjectPayload& dragPayload =
                    *static_cast<const DragObjectPayload*>(payload->Data);
                submit_parent_command(dragPayload.entityId, GameCore::k_invalidEntityId);
            }
            ImGui::EndDragDropTarget();
        }
    }

    void Hierarchy::draw_object_drag_source(const ObjectEntry& a_object)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
        {
            DragObjectPayload payload{};
            payload.entityId = a_object.entityId;
            payload.sceneId = a_object.sceneId;
            ImGui::SetDragDropPayload(k_objectDragPayloadType, &payload,
                                      sizeof(payload));
            ImGui::TextUnformatted(a_object.name.c_str());
            ImGui::EndDragDropSource();
        }
    }

    void Hierarchy::draw_object_drop_target(const ObjectEntry& a_object)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(k_objectDragPayloadType);
            if (payload != nullptr && payload->DataSize == sizeof(DragObjectPayload))
            {
                const DragObjectPayload& dragPayload =
                    *static_cast<const DragObjectPayload*>(payload->Data);
                if (dragPayload.sceneId == a_object.sceneId &&
                    dragPayload.entityId != a_object.entityId &&
                    !is_descendant_of(a_object.entityId, dragPayload.entityId))
                {
                    // 親子付け替えは Scene
                    // 所有境界と木構造の非循環性を保てる場合だけ許可する
                    submit_parent_command(dragPayload.entityId, a_object.entityId);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

    void Hierarchy::draw_object_context_menu(const ObjectEntry& a_object)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        if (ImGui::BeginPopupContextItem("HierarchyContextMenu"))
        {
            set_selected_entity_id(a_object.entityId);
            set_selected_scene_id(a_object.sceneId);

            if (ImGui::MenuItem("名前変更"))
            {
                begin_rename(a_object);
            }
            if (ImGui::MenuItem("削除"))
            {
                submit_delete_command(a_object.entityId);
            }

            ImGui::EndPopup();
        }
    }

    void Hierarchy::begin_rename(const ObjectEntry& a_object)
    {
        set_selected_entity_id(a_object.entityId);
        set_selected_scene_id(a_object.sceneId);
        m_renamingEntityId = a_object.entityId;
        m_focusRenameInput = true;
        std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');
        const size_t copyLength =
            (std::min)(a_object.name.size(), m_renameBuffer.size() - 1u);
        if (copyLength > 0)
        {
            std::memcpy(m_renameBuffer.data(), a_object.name.data(), copyLength);
        }
    }

    void Hierarchy::cancel_rename() noexcept
    {
        m_renamingEntityId = GameCore::k_invalidEntityId;
        m_focusRenameInput = false;
        std::fill(m_renameBuffer.begin(), m_renameBuffer.end(), '\0');
    }

    void Hierarchy::submit_rename_command(GameCore::EntityId a_entityId)
    {
        if (m_commandBridge == nullptr)
        {
            cancel_rename();
            return;
        }

        std::string newName = m_renameBuffer.data();
        const auto objectIt = std::find_if(m_objects.begin(), m_objects.end(),
                                           [a_entityId](const ObjectEntry& a_object) {
                                               return a_object.entityId == a_entityId;
                                           });

        if (objectIt != m_objects.end() && objectIt->name != newName)
        {
            (void)m_commandBridge->submit_command(
                std::make_unique<RenameObjectCommand>(a_entityId, std::move(newName)));
        }

        cancel_rename();
    }

    void Hierarchy::submit_delete_command(GameCore::EntityId a_entityId)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        (void)m_commandBridge->submit_command(
            std::make_unique<DeleteObjectCommand>(a_entityId));
        if (selected_entity_id() == a_entityId)
        {
            set_selected_entity_id(GameCore::k_invalidEntityId);
            set_selected_scene_id(GameCore::k_invalidSceneId);
        }
        if (m_renamingEntityId == a_entityId)
        {
            cancel_rename();
        }
    }

    void Hierarchy::submit_parent_command(GameCore::EntityId a_entityId,
                                          GameCore::EntityId a_parentId)
    {
        if (m_commandBridge == nullptr)
        {
            return;
        }

        (void)m_commandBridge->submit_command(
            std::make_unique<SetParentCommand>(a_entityId, a_parentId, true));
    }

    bool Hierarchy::is_descendant_of(
        GameCore::EntityId a_entityId,
        GameCore::EntityId a_possibleAncestor) const noexcept
    {
        GameCore::EntityId current = a_entityId;
        size_t visitedCount = 0;
        while (current != GameCore::k_invalidEntityId &&
               visitedCount <= m_objects.size())
        {
            if (current == a_possibleAncestor)
            {
                return true;
            }

            const auto it = m_objectIndexById.find(current);
            if (it == m_objectIndexById.end())
            {
                return false;
            }

            current = m_objects[it->second].parent;
            ++visitedCount;
        }

        return false;
    }

    GameCore::EntityId Hierarchy::selected_entity_id() const noexcept
    {
        return m_selectedEntityId != nullptr ? *m_selectedEntityId
                                             : GameCore::k_invalidEntityId;
    }

    void Hierarchy::set_selected_entity_id(GameCore::EntityId a_entityId) noexcept
    {
        if (m_selectedEntityId != nullptr)
        {
            *m_selectedEntityId = a_entityId;
        }
        if (m_selectedAsset != nullptr)
        {
            // Inspector の表示対象を一意にするため、GameObject 選択時は Asset
            // 選択を破棄する
            *m_selectedAsset = {};
        }
    }

    void Hierarchy::set_selected_scene_id(GameCore::SceneId a_sceneId) noexcept
    {
        if (m_selectedSceneId != nullptr)
        {
            *m_selectedSceneId = a_sceneId;
        }
    }
} // namespace Cue::Editor
